#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"


void freebytes(uint64 *);
void procnum(uint64 *);

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// 添加sys_trace函数
/**
 * sys_trace - 实现系统调用追踪功能
 * 
 * 通过位掩码设置需要追踪的系统调用:
 * - 掩码中的每一位对应一个系统调用号
 * - 为1表示需要追踪该系统调用
 * - 为0表示不追踪
 */
uint64
sys_trace(void)
{
  int trace_mask;
  // 从用户空间获取追踪掩码参数
  if(argint(0, &trace_mask) < 0)
    return -1;
  
  struct proc *p = myproc();
  p->tracing = trace_mask;  // 设置进程的追踪掩码
  return 0;
}

/**
 * sys_sysinfo - 获取系统信息
 * 
 * 收集并返回系统状态信息:
 * - 空闲内存大小
 * - 进程数量
 * 将信息通过copyout传递到用户空间
 */
uint64
sys_sysinfo(void)
{
  struct sysinfo sys_info;

  // 收集系统信息
  freebytes(&(sys_info.freemem));   // 获取空闲内存大小
  procnum(&(sys_info.nproc));       // 获取进程数量

  // 获取用户提供的缓冲区地址
  uint64 user_buf;
  if(argaddr(0, &user_buf) < 0)
    return -1;

  // 将系统信息拷贝到用户空间
  if(copyout(myproc()->pagetable, user_buf, (char*)&sys_info, sizeof(sys_info)) < 0)
    return -1;
  return 0;
}
