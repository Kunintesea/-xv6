#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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
  struct proc *p;
  
  // 获取系统调用参数：内存大小调整量
  if(argint(0, &n) < 0)
    return -1;
    
  p = myproc();
  addr = p->sz;  // 保存原始内存大小作为返回值

  if(n >= 0) {
    // 扩展内存：懒分配策略
    // 只调整进程内存大小，不立即分配物理内存
    // 物理内存将在访问时通过页面错误机制按需分配
    
    // 防止整数溢出攻击
    if(addr + n < addr) {
      return -1;
    }
    
    // 检查是否超出合理的内存限制（防止过度消耗虚拟地址空间）
    if(addr + n > MAXVA) {
      return -1;
    }
    
    p->sz += n;  // 更新进程内存大小
  } else {
    // 缩小内存：立即释放物理内存
    // 确保不会缩减到栈区域以下，保护栈不被破坏
    if(addr + n < PGROUNDUP(p->trapframe->sp)) {
      return -1;
    }
    
    // 调用实际的内存释放函数
    p->sz = uvmdealloc(p->pagetable, addr, addr + n);
  }

  return addr;  // 返回调整前的内存大小
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
