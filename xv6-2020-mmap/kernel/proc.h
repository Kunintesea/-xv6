// Saved registers for kernel context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int noff;                   // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// the sscratch register points here.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate { UNUSED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// 虚拟内存区域(VMA)结构，记录mmap映射的内存区域信息
struct virtual_memory_area {
    uint64 start_address;        // 映射区域起始虚拟地址
    int length;                  // 映射区域长度(字节)
    int protection_flags;        // 访问权限(PROT_READ/PROT_WRITE等)
    int mapping_flags;           // 映射标志(MAP_SHARED/MAP_PRIVATE)
    int file_offset;             // 文件偏移量
    struct file* mapped_file;    // 指向被映射文件的指针
    int is_valid;                // 该VMA槽位是否有效
};

// 每个进程状态
struct proc {
  struct spinlock lock;

  // 使用这些字段时必须持有p->lock:
  enum procstate state;        // 进程状态
  struct proc *parent;         // 父进程
  void *chan;                  // 如果非零，在chan上睡眠
  int killed;                  // 如果非零，已被杀死
  int xstate;                  // 返回给父进程wait的退出状态
  int pid;                     // 进程ID

  // 这些是进程私有的，不需要持有p->lock
  uint64 kstack;               // 内核栈的虚拟地址
  uint64 sz;                   // 进程内存大小(字节)
  pagetable_t pagetable;       // 用户页表
  struct trapframe *trapframe; // trampoline.S的数据页
  struct context context;      // 在此处swtch()运行进程
  struct file *ofile[NOFILE];  // 打开的文件
  struct inode *cwd;           // 当前目录
  char name[16];               // 进程名称(调试用)
  struct virtual_memory_area vma[NVMA];  // 虚拟内存区域数组
};
