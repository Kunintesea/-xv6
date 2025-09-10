//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"
// 定义max和min宏定义
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}


// mmap系统调用实现
// 将文件映射到进程的虚拟地址空间
uint64 sys_mmap(void) {
  uint64 address;
  int length, protection_flags, mapping_flags, file_offset;
  struct file *mapped_file;
  struct virtual_memory_area *vma = 0;
  struct proc *current_process = myproc();
  int vma_index;

  // 获取系统调用参数
  if (argaddr(0, &address) < 0 || argint(1, &length) < 0
      || argint(2, &protection_flags) < 0 || argint(3, &mapping_flags) < 0
      || argfd(4, 0, &mapped_file) < 0 || argint(5, &file_offset) < 0) {
    return -1;
  }
  
  // 参数有效性检查
  if (mapping_flags != MAP_SHARED && mapping_flags != MAP_PRIVATE) {
    return -1;
  }
  
  // 检查文件写权限：文件不可写但要求写权限且为共享映射时返回错误
  if (mapping_flags == MAP_SHARED && mapped_file->writable == 0 && (protection_flags & PROT_WRITE)) {
    return -1;
  }
  
  // 检查长度和偏移的有效性
  if (length < 0 || file_offset < 0 || file_offset % PGSIZE) {
    return -1;
  }

  // 在当前进程的VMA数组中寻找空闲槽位
  for (vma_index = 0; vma_index < NVMA; ++vma_index) {
    if (!current_process->vma[vma_index].is_valid) {
      vma = &current_process->vma[vma_index];
      break;
    }
  }
  if (!vma) {
    return -1;  // 没有可用的VMA槽位
  }

  // 寻找合适的虚拟地址空间进行映射
  address = MMAPMINADDR;
  for (vma_index = 0; vma_index < NVMA; ++vma_index) {
    if (current_process->vma[vma_index].is_valid) {
      // 找到已映射区域的最高地址，确保不重叠
      uint64 end_addr = current_process->vma[vma_index].start_address + current_process->vma[vma_index].length;
      if (end_addr > address) {
        address = end_addr;
      }
    }
  }
  address = PGROUNDUP(address);  // 页面对齐
  
  // 检查映射地址是否超出有效范围
  if (address + length > TRAPFRAME) {
    return -1;
  }
  
  // 初始化VMA结构
  vma->start_address = address;   
  vma->length = length;
  vma->protection_flags = protection_flags;
  vma->mapping_flags = mapping_flags;
  vma->file_offset = file_offset;
  vma->mapped_file = mapped_file;
  vma->is_valid = 1;
  
  // 增加文件引用计数，防止文件被过早释放
  filedup(mapped_file);

  return address;
}

// munmap系统调用实现
// 取消指定地址范围的内存映射
uint64 sys_munmap(void) {
  uint64 unmap_address, virtual_address;
  int unmap_length;
  struct proc *current_process = myproc();
  struct virtual_memory_area *target_vma = 0;
  uint max_write_size, write_bytes, current_write_bytes;
  int vma_index;

  // 获取系统调用参数并进行基本检查
  if (argaddr(0, &unmap_address) < 0 || argint(1, &unmap_length) < 0) {
    return -1;
  }
  if (unmap_address % PGSIZE || unmap_length < 0) {
    return -1;
  }

  // 在进程的VMA数组中查找包含指定地址范围的VMA
  for (vma_index = 0; vma_index < NVMA; ++vma_index) {
    if (current_process->vma[vma_index].is_valid && 
        unmap_address >= current_process->vma[vma_index].start_address &&
        unmap_address + unmap_length <= current_process->vma[vma_index].start_address + current_process->vma[vma_index].length) {
      target_vma = &current_process->vma[vma_index];
      break;
    }
  }
  
  // 未找到匹配的VMA则返回错误
  if (!target_vma) {
    return -1;
  }

  if (unmap_length == 0) {
    return 0;
  }
  
  // 对于MAP_SHARED映射，需要将修改写回文件
  if ((target_vma->mapping_flags & MAP_SHARED)) {
    // 计算一次可以写入磁盘的最大字节数
    max_write_size = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    
    for (virtual_address = unmap_address; virtual_address < unmap_address + unmap_length; virtual_address += PGSIZE) {
      // 检查页面是否被修改过（脏页检查）
      if (uvmgetdirty(current_process->pagetable, virtual_address) == 0) {
        continue;  // 未修改的页面无需写回
      }
      
      // 计算当前页面需要写入的字节数
      write_bytes = min(PGSIZE, unmap_address + unmap_length - virtual_address);
      
      // 分批写入，避免超过文件系统单次操作限制
      for (int offset = 0; offset < write_bytes; offset += current_write_bytes) {
        current_write_bytes = min(max_write_size, write_bytes - offset);
        
        begin_op();
        ilock(target_vma->mapped_file->ip);
        
        // 计算文件中的偏移位置并写入数据
        uint64 file_offset = virtual_address - target_vma->start_address + target_vma->file_offset + offset;
        if (writei(target_vma->mapped_file->ip, 1, virtual_address + offset, file_offset, current_write_bytes) != current_write_bytes) {
          iunlock(target_vma->mapped_file->ip);
          end_op();
          return -1;
        }
        
        iunlock(target_vma->mapped_file->ip);
        end_op();
      }
    }
  }
  
  // 从用户页表中取消映射指定页面
  uvmunmap(current_process->pagetable, unmap_address, (unmap_length - 1) / PGSIZE + 1, 1);
  
  // 更新VMA结构：根据取消映射的位置进行不同处理
  if (unmap_address == target_vma->start_address && unmap_length == target_vma->length) {
    // 完全取消映射：清空整个VMA
    target_vma->start_address = 0;
    target_vma->length = 0;
    target_vma->file_offset = 0;
    target_vma->mapping_flags = 0;
    target_vma->protection_flags = 0;
    target_vma->is_valid = 0;
    fileclose(target_vma->mapped_file);
    target_vma->mapped_file = 0;
  } else if (unmap_address == target_vma->start_address) {
    // 从头部取消映射：调整VMA起始位置和大小
    target_vma->start_address += unmap_length;
    target_vma->file_offset += unmap_length;
    target_vma->length -= unmap_length;
  } else if (unmap_address + unmap_length == target_vma->start_address + target_vma->length) {
    // 从尾部取消映射：仅调整VMA大小
    target_vma->length -= unmap_length;
  } else {
    // 不支持在VMA中间打洞
    panic("unexpected munmap: 不支持在映射区域中间取消映射");
  }
  
  return 0;
}

