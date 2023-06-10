#include "userprog/syscall.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include <stdio.h>
#include <syscall-nr.h>

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/init.h"
#include "userprog/process.h"

#include "devices/input.h"
#include "lib/kernel/console.h"

#include "string.h"
#include "threads/palloc.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
struct thread *get_child_process(int pid);
int wait(int pid);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	check_address(&f->rsp);		 // 스택 포인터가 유저 영역인지 확인
	uint64_t sys_num = f->R.rax; // syscall number
	char file, thread_name, cmd_line;
	int status, fd;
	int pid;
	unsigned initial_size, size, position;
	void *buffer;
	struct thread *curr = thread_current();
	struct file *file_o;
	switch (sys_num)
	{
	case SYS_HALT:
		power_off();
		break;
	case SYS_EXIT:
		status = f->R.rdi;
		exit(status);
		thread_exit();
		break;
	case SYS_FORK:
		thread_name = f->R.rdi;
		struct thread *child;

		pid = process_fork(thread_name, f);
		child = get_child_process(pid);
		sema_down(&child->wait_sema);
		f->R.rax = pid;
		break;
	case SYS_EXEC:
		cmd_line = f->R.rdi;
		char *cmd_line_copy = palloc_get_page(0);
		if (cmd_line_copy == NULL)
			exit(-1);
		strlcpy(cmd_line_copy, cmd_line, PGSIZE);

		if (process_exec(cmd_line_copy) == -1)
			exit(-1);
		// file_exec(file);
		break;
	case SYS_WAIT:
		pid = f->R.rdi;
		f->R.rax = wait(pid);
		// file_wait(pid);
		break;
	case SYS_CREATE:
		file = f->R.rdi;
		initial_size = f->R.rsi;
		lock_acquire(&filesys_lock);
		check_address(file);
		bool success = filesys_create(file, initial_size);
		lock_release(&filesys_lock);
		f->R.rax = success;
		break;
	case SYS_REMOVE:
		file = f->R.rdi;
		check_address(file);
		filesys_remove(file);
		break;
	case SYS_OPEN:

		file = f->R.rdi;
		check_address(file);
		lock_acquire(&filesys_lock);
		file_o = filesys_open(file);
		if (file_o == NULL)
		{
			lock_release(&filesys_lock);
			return -1;
		}
		fd = process_add_file(file_o);
		if (fd == -1)
			file_close(file_o);
		lock_release(&filesys_lock);
		break;
	case SYS_FILESIZE:
		fd = f->R.rdi;
		file_length(fd);
		break;
	case SYS_READ:
		fd = f->R.rdi;
		buffer = f->R.rsi;
		size = f->R.rdx;
		if (fd == -1)
			return -1;
		else if (fd == 0)
		{
			lock_acquire(&filesys_lock);
			return file_read(input_getc(), buffer, size);
			lock_release(&filesys_lock);
		}
		break;
	case SYS_WRITE:
		fd = f->R.rdi;
		buffer = f->R.rsi;
		size = f->R.rdx;
		int bytes_write;
		// get_argument(fd, buffer, size);
		//  f->R.rax =
		if (fd == 0)
			exit(-1);
		else if (fd == 1)
		{
			putbuf(buffer, size);
			bytes_write = size;
		}
		else
		{
			lock_acquire(&filesys_lock);
			bytes_write = file_write(fd, buffer, size);
			lock_release(&filesys_lock);
		}
		return bytes_write;
		break;
	case SYS_SEEK:
		fd = f->R.rdi;
		position = f->R.rsi;
		file_seek(fd, position);
		break;
	case SYS_TELL:
		fd = f->R.rdi;
		file_tell(fd);
		break;
	case SYS_CLOSE:
		fd = f->R.rdi;
		file = process_get_file(fd);
		if (file == NULL)
			return;
		file_close(fd);
		break;
	default:
		break;
	}
	printf("system call!\n");
	thread_exit();
}

void check_address(void *addr)
{
	if (addr == NULL)
		exit(-1);
	if (!is_user_vaddr(addr))
		exit(-1);
}

void get_argument(void *rsp, int *arg, int count)
{ // copy syscall argv into kernel
}

struct thread *get_child_process(int pid)
{
	struct list_elem *e;
	struct thread *curr = thread_current();
	for (e = list_begin(&curr->child_list); e != list_end(&curr->child_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, c_elem);
		if (pid == t->tid)
			return t;
	}
	return NULL;
}

int wait(int pid)
{
	return process_wait(pid);
}

void exit(int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status; // 이거 wait에서 사용?
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}