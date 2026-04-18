#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include <stdint.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

static void syscall_handler(struct intr_frame*);
static void terminate_with_status(int status) NO_RETURN;

#define ERROR -1

/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful,
-1 if a segfault occurred. */
static int get_user(const uint8_t * uaddr) {
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
UDST must be below PHYS_BASE.
Returns true if successful,
false if a segfault occurred. */
static bool put_user(uint8_t* udst, uint8_t byte) {
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:" : "=&a"(error_code), "=m"(*udst) : "q"(byte));
  return error_code != -1;
}

static bool access_ok(const uint8_t* uaddr, size_t size);

static bool get_user_dwords(uint32_t *dst, const uint8_t *uaddr) {
  if (!access_ok((uint8_t*)uaddr, 4)) {
    return false;
  }
  
  int value = 0;
  for (int i = 0; i < 4; ++i) {
    int byte = get_user(uaddr + i);
    if (byte == -1)
    return false;
  ((uint8_t*)&value)[i] = byte;
}
*dst = value;
return true;
}

static bool check_user_buf(const uint8_t*, size_t);
static int check_user_str(const uint8_t*, size_t);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

static void sys_exit(int);
static pid_t sys_exec(const char*);

static int get_fd(void);
static struct file* get_fp(int);
static struct inode* get_inode_from_fd(int);

static bool sys_create(const char*, unsigned);
static bool sys_remove(const char*);
static int sys_open(const char*);
static int sys_filesize(int);
static int sys_read(int, void*, unsigned);
static int sys_write(int, const void*, unsigned);
static void sys_seek(int, unsigned);
static int sys_tell(int);
static void sys_close(int);
static block_sector_t sys_inumber(int);
static bool sys_chdir(const char*);
static bool sys_mkdir(const char*);
static bool sys_isdir(int fd);
static bool sys_readdir(int, char*);

bool check_address(const void *addr, uint32_t len) {
  for (int i = 0; i < len; ++i) {
    const uint8_t* vaddr = (const uint8_t*)addr + i;
    if (vaddr == NULL) { // NULL address
      return false;
    }
    if (vaddr >= PHYS_BASE) { // in kernel space
      return false;
    }
    uint32_t* pd = thread_current()->pcb->pagedir;
    void *phy_addr = pagedir_get_page(pd, vaddr);
    if (phy_addr == NULL) {
      return false;
    }
  }

  return true;
}

static void terminate_with_status(int status) {
  thread_current()->exit_status = status;
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit();
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  uint32_t syscall_num;
  if (!get_user_dwords(&syscall_num, (const uint8_t*)args)) {
    goto fail;
  }

  uint32_t arg1, arg2, arg3;
  switch (syscall_num) {
    case SYS_READ:
    case SYS_WRITE:
      if (!get_user_dwords(&arg3, (const uint8_t*)(args + 3))) {
        goto fail;
      }
      
      __attribute__((fallthrough));

    case SYS_CREATE:
    case SYS_SEEK:
    case SYS_READDIR:
      if (!get_user_dwords(&arg2, (const uint8_t*)(args + 2))) {
        goto fail;
      }
      __attribute__((fallthrough));

    case SYS_PRACTICE:
    case SYS_EXIT:
    case SYS_WAIT:
    case SYS_EXEC:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_TELL:
    case SYS_CLOSE:
    case SYS_INUMBER:
    case SYS_CHDIR:
    case SYS_MKDIR:
    case SYS_ISDIR:
      if (!get_user_dwords(&arg1, (const uint8_t*)(args + 1))) {
        goto fail;
      }
      __attribute__((fallthrough));
    
    case SYS_HALT:
    case SYS_FORK:
      break;
    default:
      break;

    fail:
      sys_exit(ERROR);      
  }

  switch (syscall_num) {
  case SYS_EXIT:
    sys_exit((int)arg1);
    break;
  case SYS_PRACTICE:
    f->eax = arg1 + 1;
    break;
  case SYS_HALT:
    shutdown_power_off();
    break;
  case SYS_EXEC:
    f->eax = sys_exec((const char*)arg1);
    break;
  case SYS_WAIT:
    f->eax = process_wait((pid_t)arg1);
    break;
  case SYS_FORK:
    f->eax = process_fork(f);
    break;
  case SYS_CREATE:
    f->eax = sys_create((const char*)arg1, (unsigned)arg2);
    break;
  case SYS_REMOVE:
    f->eax = sys_remove((const char*)arg1);
    break;
  case SYS_OPEN:
    f->eax = sys_open((const char*)arg1);
    break;
  case SYS_FILESIZE:
    f->eax = sys_filesize((int)arg1);
    break;
  case SYS_READ:
    f->eax = sys_read((int)arg1, (void*)arg2, (unsigned)arg3);
    break;
  case SYS_WRITE:
    f->eax = sys_write((int)arg1, (const void*)arg2, (unsigned)arg3);
    break;
  case SYS_SEEK:
    sys_seek((int)arg1, (unsigned)arg2);
    break;
  case SYS_TELL:
    f->eax = sys_tell((int)arg1);
    break;
  case SYS_CLOSE:
    sys_close((int)arg1);
    break;
  case SYS_INUMBER:
    f->eax = sys_inumber((int)arg1);
    break;
  case SYS_CHDIR:
    f->eax = sys_chdir((const char*)arg1);
    break;
  case SYS_MKDIR:
    f->eax = sys_mkdir((const char*)arg1);
    break;
  case SYS_READDIR:
    f->eax = sys_readdir((int)arg1, (char*)arg2);
    break;
  case SYS_ISDIR:
    f->eax = sys_isdir((int)arg1);
    break;
  default:
    break;
  }

}

static void sys_exit(int status) {
  thread_current()->pcb->wait_st->exit_code = status;
  process_exit();
}

static pid_t sys_exec(const char *cmd_line) {
  int status = check_user_str((const uint8_t*)cmd_line, PGSIZE);
  if (status < 0) {
    sys_exit(ERROR);
  }
  return process_execute(cmd_line);
}

static bool sys_create(const char* file, unsigned initial_size) {
  int status = check_user_str((const uint8_t*)file, PGSIZE);
  if (status < 0) {
    sys_exit(ERROR);
  }

  bool ret = filesys_create(file, initial_size);
  return ret;
}

static bool sys_remove(const char* file) {
  int status = check_user_str((const uint8_t*)file, PGSIZE);
  if (status < 0) {
    sys_exit(ERROR);
  }
  bool ret = filesys_remove(file);
  return ret;
}

static int sys_open(const char* file) {
  int status = check_user_str((const uint8_t*)file, PGSIZE);
  if (status < 0) {
    sys_exit(ERROR);
  }
  struct file* fp = filesys_open(file);
  if (fp == NULL) {
    return ERROR;
  } else {
    int fd = get_fd();
    if (fd == -1) {
      printf("sys_open: No availabe file descriptor\n");
      return ERROR;
    }
    thread_current()->pcb->fd_table[fd] = fp;
    return fd;
  }
}

static int sys_filesize(int fd) {
  int ret;
  struct file* fp;
  fp = get_fp(fd);
  if (fp == NULL) {
    ret = ERROR;
  } else {
    ret = file_length(fp);
  }

  return ret;
}

static int sys_read(int fd, void* buffer, unsigned size) {
  if (!access_ok(buffer, size) || !check_user_buf(buffer, size)) {
    sys_exit(ERROR);
  }
  if (inode_is_dir(get_inode_from_fd(fd))) {
    return ERROR;
  }

  if (size == 0) {
    return 0;
  }
  int bytes_read = 0;
  if (fd == STDIN_FILENO) {
    for (unsigned i = 0; i < size; i++) {
      uint8_t c = input_getc();
      if (!put_user((uint8_t*)buffer + i, c)) {
        sys_exit(ERROR);
      }
      bytes_read++;
    }
  } else {
    struct file* fp = get_fp(fd);
    if (fp == NULL) {
      bytes_read = ERROR;
    } else {
      ASSERT(size > 0);
      bytes_read = file_read(fp, buffer, size);
    }
    if (bytes_read == ERROR) {
      return ERROR;
    }
  }
  return bytes_read;
}

static int sys_write(int fd, const void* buffer, unsigned size) {
  if (!access_ok(buffer, size) || !check_user_buf(buffer, size)) {
    sys_exit(ERROR);
  }
  if (inode_is_dir(get_inode_from_fd(fd))) {
    return ERROR;
  }

  int ret;
  if (fd == STDOUT_FILENO) {
    ret = size;
    putbuf(buffer, size);
  } else {
    struct file* fp = get_fp(fd);
    if (fp == NULL) {
      ret = ERROR;
    } else {
      ret = file_write(fp, buffer, size);
    }
  }
  return ret;
}

static void sys_seek(int fd, unsigned position) {
  struct file* fp;
  if ((fp = get_fp(fd)) != NULL) {
    file_seek(fp, position);
  }
}

static int sys_tell(int fd) {
  struct file* fp;
  int ret;
  if ((fp = get_fp(fd)) == NULL) {
    ret = ERROR;
  } else {
    ret = file_tell(fp);
  }
  if (ret == ERROR) {
    sys_exit(ERROR);
  }
  return ret;
}

static void sys_close(int fd) {
  struct file* fp;
  int ret;
  if ((fp = get_fp(fd)) == NULL) {
    ret = ERROR;
  } else {
    thread_current()->pcb->fd_table[fd] = NULL;
    file_close(fp);
  }
  if (ret == ERROR) {
    sys_exit(ERROR);
  }
}

static block_sector_t sys_inumber(int fd) {
  struct file* fp = get_fp(fd);
  return inode_get_inumber(file_get_inode(fp));
}

static bool sys_chdir(const char* dir) {
  struct dir* parent;
  char name[NAME_MAX + 1];

  if (!dir_resolve_path(dir, &parent, name))
    return false;

  struct inode* inode;
  bool found = dir_lookup(parent, name, &inode);
  dir_close(parent);

  if (!found || !inode_is_dir(inode)) {
    inode_close(inode);
    return false;
  }

  struct thread* cur = thread_current();
  dir_close(cur->pcb->cwd);
  cur->pcb->cwd = dir_open(inode);
  return true;
}

static bool sys_mkdir(const char* dir) {
  int status = check_user_str((const uint8_t*)dir, PGSIZE);
  if (status < 0)
    sys_exit(ERROR);
  bool ret = filesys_create_dir(dir);
  return ret;
}

static bool sys_readdir(int fd, char* name) {
  if (!check_user_buf((const uint8_t*)name, NAME_MAX + 1)) {
    sys_exit(ERROR);
  }

  struct file* fp = get_fp(fd);
  if (fp == NULL)
    return false;

  struct inode* inode = file_get_inode(fp);
  if (inode == NULL || !inode_is_dir(inode))
    return false;

  return dir_readdir_by_inode(inode, file_tell(fp), file_pos_ptr(fp), name);
}

static bool sys_isdir(int fd) {
  struct inode* inode = get_inode_from_fd(fd);
  if (inode == NULL)
    return false;
  return inode_is_dir(inode);
}

/* Get file* of fd, return NULL if failed */
static struct file* get_fp(int fd) {
  struct file** fd_table = thread_current()->pcb->fd_table;
  struct file* fp;
  if (fd <= 1 || fd >= MAX_FDS || (fp = fd_table[fd]) == NULL) {
    return NULL;
  }
  return fp;
}

/* Get inode from fd，whether it is file or directory */
static struct inode* get_inode_from_fd(int fd) {
  struct file* fp = get_fp(fd);
  if (fp == NULL)
    return NULL;
  return file_get_inode(fp);
}
/* Get next available fd, return -1 if no available fd */
static int get_fd(void) {
  struct file** fd_table = thread_current()->pcb->fd_table;
  for (int i = 2; i < MAX_FDS; i++) {
    if (fd_table[i] == NULL) {
      return i;
    }
  }
  return ERROR;
}

/* Quickly check whether a buffer in user adress space is valid */
static bool access_ok(const uint8_t* uaddr, size_t size) {
  bool ok = true;
  if (size != 0) {
    ok = is_user_vaddr(uaddr + size - 1);
  }
  ok = ok && is_user_vaddr(uaddr) && uaddr != NULL;
  return ok;
}
/* Safely check a string at UADDR of up to max_len(including null terminator) size, return -1 if invalid address,
   return -2 if its length exceed MAX_LEN, return its length if succeed(excluding null terminator) */
static int check_user_str(const uint8_t* uaddr, size_t max_len) {
  size_t i = 0;
  int c;
  while (i < max_len) {
    if (!is_user_vaddr(uaddr)) {
      return ERROR;
    }
    c = get_user(uaddr + i);
    if (c == -1) {
      return ERROR;
    }
    if (c == '\0') {
      return (int)i;
    }
    i++;
  }
  return -2;
}

/* Check whether a buffer of size SIZE starting at address UADDR is valid */
static bool check_user_buf(const uint8_t* uaddr, size_t size) {
  if (!access_ok(uaddr, size)) {
    return false;
  }

  for (size_t i = 0; i < size; i++) {
    int c = get_user(uaddr + i);
    if (c == -1) {
      return false;
    }
  }
  return true;
}