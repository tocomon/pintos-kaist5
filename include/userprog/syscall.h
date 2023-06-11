#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

void syscall_init (void);
struct lock filesys_lock; // 파일 동기화를 위한 전역변수

#endif /* userprog/syscall.h */
