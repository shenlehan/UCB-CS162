#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127
#define MAX_ARGC 100
#define MAX_FDS 128

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  struct list child_list;
  struct wait_status* wait_st; /* pointer to this process’s wait status. */
  struct file* fd_table[MAX_FDS]; /* File descriptor table  */
  struct file* exec; /* pointer to the executable file (write-denied while running) */
  struct lock pcb_lock;
  struct dir* cwd; /* Current working directory */
};

struct init_status {
   /* Used in process_fork */
   struct thread* parent;
   const struct intr_frame* if_;
   /* Used in process_execute */
   char* file_name;
   /* Semaphore used to synchronize process_execute */
   struct semaphore load_sema;
   bool load_success;
   struct list* list_of_parent;
 };

/* A struct contains information for wait*/
struct wait_status {
   pid_t pid;
   /* Semaphore used to synchronize process_wait */
   struct semaphore wait_sema;
   /* Since a wait_status is owned both by child and parent, 
      this variable is used for deciding who should free this struct.
      It can only be 0, 1 or 2. */
   uint8_t ref_cnt;
   /* Indicate whether the process that 
      calls wait has already called wait on pid */
   bool waited;
   /* A lock for shared variables */
   struct lock lck;
   int exit_code;
   struct list_elem elem;
};
 
void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(void);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */
