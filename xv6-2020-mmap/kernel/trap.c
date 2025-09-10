#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } 
  // 处理访问mmap映射内存产生的页面错误
  else if (r_scause() == 12 || r_scause() == 13 || r_scause() == 15) {
    char *physical_page;
    uint64 fault_virtual_address = PGROUNDDOWN(r_stval());
    struct virtual_memory_area *target_vma = 0;
    int page_flags = PTE_U;
    int vma_index;
    
    // 在当前进程的VMA数组中查找包含错误地址的VMA
    for (vma_index = 0; vma_index < NVMA; ++vma_index) {
      if (p->vma[vma_index].is_valid && 
          fault_virtual_address >= p->vma[vma_index].start_address &&
          fault_virtual_address < p->vma[vma_index].start_address + p->vma[vma_index].length) {
        target_vma = &p->vma[vma_index];
        break;
      }
    }
    
    if (!target_vma) {
      goto err;  // 未找到对应的VMA，这是一个非法访问
    }
    
    // 处理写权限页面错误：如果页面已存在但需要设置脏位
    if (r_scause() == 15 && (target_vma->protection_flags & PROT_WRITE) && walkaddr(p->pagetable, fault_virtual_address)) {
      if (uvmsetdirtywrite(p->pagetable, fault_virtual_address)) {
        goto err;
      }
    } else {
      // 懒加载：分配物理页面并从文件读取内容
      if ((physical_page = kalloc()) == 0) {
        goto err;  // 内存分配失败
      }
      memset(physical_page, 0, PGSIZE);  // 清零页面
      
      // 从映射文件中读取内容到新分配的页面
      ilock(target_vma->mapped_file->ip);
      uint64 file_offset = fault_virtual_address - target_vma->start_address + target_vma->file_offset;
      if (readi(target_vma->mapped_file->ip, 0, (uint64)physical_page, file_offset, PGSIZE) < 0) {
        iunlock(target_vma->mapped_file->ip);
        kfree(physical_page);
        goto err;
      }
      iunlock(target_vma->mapped_file->ip);
      
      // 根据VMA的保护标志设置页面权限
      if ((target_vma->protection_flags & PROT_READ)) {
        page_flags |= PTE_R;
      }
      // 对于存储页面错误，如果有写权限则设置写和脏位
      if (r_scause() == 15 && (target_vma->protection_flags & PROT_WRITE)) {
        page_flags |= PTE_W | PTE_D;
      }
      if ((target_vma->protection_flags & PROT_EXEC)) {
        page_flags |= PTE_X;
      }
      
      // 将物理页面映射到用户进程的页表中
      if (mappages(p->pagetable, fault_virtual_address, PGSIZE, (uint64)physical_page, page_flags) != 0) {
        kfree(physical_page);
        goto err;
      }
    }
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
err:
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

