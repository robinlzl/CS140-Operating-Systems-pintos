#include <stdio.h>
#include <syscall-nr.h>
//#include <sched.h>
#include "threads/interrupt.h"
#include "devices/shutdown.h"
#include "threads/thread.h"
#include "../threads/thread.h"
#include "process.h"
#include "../filesys/filesys.h"
#include "../threads/interrupt.h"
#include "../lib/kernel/list.h"
#include "../threads/malloc.h"
#include "threads/vaddr.h"
#include "pagedir.h"
#include "../devices/input.h"
#include "../filesys/file.h"
#include "../lib/kernel/stdio.h"
#include "userprog/syscall.h"
#include "vm/paging.h"
#include "userprog/exception.h"
#include "exception.h"
#include "../vm/paging.h"
#include "../lib/syscall-nr.h"
#include "../threads/palloc.h"

static void munmap(int map_id);
static struct user_file_info *find_open_file(int fd);
static void syscall_handler (struct intr_frame *);
static bool equals_fd(const struct list_elem *elem, void *fd);

/* Projects 2 and later. */
static void halt (void);
static int exec (const char *file);
static int wait (int);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static unsigned tell (int fd);
static void close (int fd);
static void check_pointer(uint32_t esp, void *s, bool grow, bool prohibit, const char *name);
static bool check_pointer_nonsastik(uint32_t esp, void *s, bool grow, bool prohibit, const char *name);

static int FD_C = 2;
static bool check_pointer_nonsastik(uint32_t esp, void *s, bool grow, bool prohibit, const char *name){
  if((unsigned int)s >= (unsigned  int)PHYS_BASE)
    return 0;
  if(!pagedir_get_page(thread_current()->pagedir, s)){
    if(!(grow && (is_user_vaddr(s) && stack_resized(esp, s)))) {
      return 0;
    }
  }
  supp_pagedir_set_prohibit(s, prohibit);
  if(prohibit)
    ASSERT(pagedir_get_page(thread_current()->pagedir, s));
  return 1;
}
static void check_pointer(uint32_t esp, void *s, bool grow, bool prohibit, const char *name){
  if(!check_pointer_nonsastik(esp, s, grow, prohibit, name)) {
    exit(-1);
  }
}

static void *get_arg_pointer(void *pointer, int i, int len, bool grow, bool prohibit, const char *name){

  void **p = (void**)pointer + i;
  check_pointer((uint32_t)pointer, p, grow, false, name);
  check_pointer((uint32_t)pointer, (char*)p + 3, grow, false, name);
  check_pointer((uint32_t)pointer, *p, grow, false, name);
  //check_pointer((char*)*p + 3);

  char *ret = (char*)*p;
  check_pointer((uint32_t)pointer, ret, grow, prohibit, name);
  if(len == -1){
    for(;*ret;check_pointer((uint32_t)pointer, ++ret, grow, prohibit, name));
  }else{
    check_pointer((uint32_t)pointer, ret + len, grow, prohibit, name);
    for(;len >= 0; len--, check_pointer((uint32_t)pointer, ret++, grow, prohibit, name));
  }
  void *rett = *p;
  return rett;
}
static int get_arg(void *pointer, int i, bool grow, bool prohibit, const char *name){
  int *p = (((int*)pointer) + (i));
  check_pointer((uint32_t)pointer, p, grow, prohibit, name);
  check_pointer((uint32_t)pointer, (char*)p + 3, grow, prohibit, name);

  return *p;
}

#define ITH_ARG_POINTER(f, i, TYPE, len, grow, prohibit, NAME) ((TYPE)get_arg_pointer((void*)f->esp, i, len, grow, prohibit, NAME))

#define ITH_ARG(f, i, TYPE, grow, prohibit, NAME) ((TYPE)(get_arg((void*)f->esp, i, grow, prohibit, NAME)))

void
syscall_init (void) {
  lock_init(&fileSystem);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  int sys_call_id = ITH_ARG(f, 0, int, false, false, "sys_call_id");
  uint32_t ret = 23464464;
  int mmap_fd, mmap_size;
  switch (sys_call_id){
    /* Projects 2 and later. */
    case SYS_HALT:                   /* Halt the operating system. */
      halt();
      break;
    case SYS_EXIT:                   /* Terminate this process. */
      exit(ITH_ARG(f, 1, int, false, false, "EXIT1"));
      break;
    case SYS_EXEC:                   /* Start another process. */
      ret = exec(ITH_ARG_POINTER(f, 1, const char *, -1, false, true, "EXEC1"));
      ITH_ARG_POINTER(f, 1, const char *, -1, false, false, "EXEC1*");
      break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
      ret = wait(ITH_ARG(f, 1, int, false, false,"WAIT1"));
      break;
    case SYS_CREATE:                 /* Create a file. */
      ret = create(ITH_ARG_POINTER(f, 1, char *, -1, false, true, "CREATE1"),
                   ITH_ARG(f, 2, unsigned int, false, false, "CREATE2"));
      ITH_ARG_POINTER(f, 1, char *, -1, false, false, "CREATE1*");
      break;
    case SYS_REMOVE:                 /* Delete a file. */
      ret = remove(ITH_ARG_POINTER(f, 1, char *, -1, false, true, "REMOVE1"));
      ITH_ARG_POINTER(f, 1, char *, -1, false, false, "REMOVE1*");
      break;
    case SYS_OPEN:                   /* Open a file. */
      ret = open(ITH_ARG_POINTER(f, 1, char *, -1, false, true, "OPEN1"));
      ITH_ARG_POINTER(f, 1, char *, -1, false, false, "OPEN1*");
      break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
      ret = filesize(ITH_ARG(f, 1, int, false, false, "FILESIZE1"));
      break;
    case SYS_READ:                   /* Read from a file. */ // todo
      ret = read_sys(ITH_ARG(f, 1, int, false, false,"READ1"),
                 ITH_ARG_POINTER(f, 2, void*, ITH_ARG(f, 3, unsigned int, false, false,"READ33"), true, true,"READ2"),
                 ITH_ARG(f, 3, unsigned int, false, false, "READ3"));
      ITH_ARG_POINTER(f, 2, void*, ITH_ARG(f, 3, unsigned int, false, false,"READ33*"), false, false,"READ2*");
      break;
    case SYS_WRITE:                  /* Write to a file. */ // todo
      ret = write_sys(ITH_ARG(f, 1, int, false, false,"WRITE1"),
                  ITH_ARG_POINTER(f, 2,const void *, ITH_ARG(f, 3, unsigned int, false, false,"WRITE33"), true, true,"WRITE2"),
                  ITH_ARG(f, 3, unsigned int, false, false,"WRITE3"));

      ITH_ARG_POINTER(f, 2,const void *, ITH_ARG(f, 3, unsigned int, false, false,"WRITE33*"), false, false,"WRITE2*");

      break;
    case SYS_SEEK:                   /* Change position in a file. */
      seek_sys(ITH_ARG(f, 1, int, false, false,"SEEK1"), ITH_ARG(f, 2, unsigned int, false, false,"SEEK2"));
      break;
    case SYS_TELL:                   /* Report current position in a file. */
      ret = tell(ITH_ARG(f, 1, int, false, false,"TELL1"));
      break;
    case SYS_CLOSE:                  /* Close a file. */
      close(ITH_ARG(f, 1, int, false, false, "close1"));
      break;
    case SYS_MMAP:
      ret = mmap(ITH_ARG(f, 1, int, false, false, "MMAP"),
                 (void*)ITH_ARG(f, 2, int, false, false, "MMAP2"));
      break;
    case SYS_MUNMAP:
      munmap(ITH_ARG(f, 1, int, false, false, "MUNMAP1"));
      break;
    default:
      exit(-1);
  }
  if(ret != 23464464){
    f->eax = ret;
  }
}

int mmap(int fd, void *vaddr){
  int len = filesize(fd);
  if(len == 0) return -1;
  if(len == -1) return -1;
  if(vaddr == 0) return -1;
  if(pg_round_down(vaddr) != vaddr) return -1;

  int i;
  for(i = 0; i < len; i+= PGSIZE){
    struct supp_pagedir_entry ** ee = supp_pagedir_lookup(thread_current()->supp_pagedir, vaddr + i * PGSIZE, false);
    if(ee) return -1;
  }

  for(i = 0; i < len; i += PGSIZE, vaddr += PGSIZE){
    supp_pagedir_virtual_create(vaddr, PAL_ZERO | PAL_USER);
    supp_pagedir_set_readfile(vaddr, fd, i, ((i + PGSIZE) < len) ? (i + PGSIZE) : len, false);
  }
}

static void munmap(int map_id){

}

/* Terminate this process. */
void exit (int status){
  struct thread *t = thread_current()->parent_thread;
  if(t != NULL) {
    struct thread_child *tc = thread_set_child_exit_status(t, thread_tid(), status);
    if (tc != NULL) {
      sema_up(&tc->semaphore);
    }
  }

  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
}
/* Halt the operating system. */
static void halt(){
  shutdown_power_off();
}
/* Start another process. */
static tid_t exec (const char * cmd_line ){
  tid_t processId = process_execute(cmd_line);

  return processId;
}
/* Open a file. */
static int open (const char *file_name){
  int ret_FDC;
  lock_acquire(&fileSystem);
  struct file *f = filesys_open(file_name);
  if(f == NULL) ret_FDC = -1;
  else {
    struct user_file_info *info = malloc(sizeof(struct user_file_info));
    info->f = f;
    ret_FDC = info->fd = FD_C++;
    list_push_back(&thread_current()->open_files, &info->link);
  }
  lock_release((&fileSystem));
  return ret_FDC;
}
/* Wait for a child process to die. */
static int wait (int pid){
  return process_wait(pid);
}
/* Create a file. */
static bool create (const char * file , unsigned initial_size ){
  bool ans;
  lock_acquire(&fileSystem);
  ans = filesys_create(file, initial_size);
  lock_release((&fileSystem));
  return ans;
}
/* Delete a file. */
static bool remove (const char * file) {
  bool ans;
  lock_acquire(&fileSystem);
  ans = filesys_remove(file);;
  lock_release((&fileSystem));
  return ans;
}


static bool equals_fd(const struct list_elem *elem, void *fd){
  return list_entry (elem, struct user_file_info, link)->fd == *(int*)fd;
}
static struct user_file_info *find_open_file(int fd){
  struct list_elem *e =  list_find(&thread_current()->open_files, equals_fd, (void*)&fd);
  if(e == NULL) return NULL;
  else return list_entry(e, struct user_file_info, link);
}
/* Obtain a file's size. */
static int filesize (int fd){
  int ans;
  lock_acquire(&fileSystem);
  struct user_file_info *f= find_open_file(fd);
  if(f == NULL) ans = -1;
  else ans = file_length(f->f);
  lock_release((&fileSystem));
  return ans;
}
/* Read from a file. */
int read_sys (int fd, void * buffer, unsigned size){
  if(fd == 0){
    unsigned i;
    char *s = (char*)buffer;
    for(i = 0; i < size; i++, s++) *s = input_getc();
    return size;
  }else{
    int ans;
    lock_acquire(&fileSystem);
    struct user_file_info *f= find_open_file(fd);
    if(f == NULL) ans = -1;
    else ans = file_read(f->f, buffer, size);
    lock_release((&fileSystem));
    return ans;
  }
}
/* Write to a file. */
int write_sys (int fd , const void * buffer , unsigned size ){
  if(1 == fd){
    putbuf(buffer, size);
    return size;
  }else{
    int ans;
    lock_acquire(&fileSystem);
    struct user_file_info *f= find_open_file(fd);
    if(f == NULL) ans = -1;
    else ans = file_write(f->f, buffer, size);
    lock_release((&fileSystem));
    return ans;
  }
}
/* Change position in a file. */
void seek_sys (int fd, unsigned position){
  lock_acquire(&fileSystem);
  struct user_file_info *f= find_open_file(fd);
  if(f != NULL) file_seek(f->f, position);
  lock_release((&fileSystem));
}
/* Report current position in a file. */
static unsigned tell (int fd){
  int ans = 0;
  lock_acquire(&fileSystem);
  struct user_file_info *f= find_open_file(fd);
  if(f != NULL) file_tell(f->f);
  lock_release((&fileSystem));
  return ans;
}
/* Close a file. */
static void close (int fd){
  lock_acquire(&fileSystem);
  struct user_file_info *f= find_open_file(fd);
  if(f != NULL) {
    file_close(f->f);
    list_remove(&f->link);
    free(f);
  }
  lock_release((&fileSystem));

}