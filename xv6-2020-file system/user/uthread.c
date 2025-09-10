#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* 用户级线程可能的状态 */
#define THREAD_FREE        0x0  // 线程空闲，可被分配
#define THREAD_RUNNING     0x1  // 线程正在运行
#define THREAD_RUNNABLE    0x2  // 线程就绪，等待调度

#define THREAD_STACK_SIZE  8192 // 每个线程的栈大小
#define MAX_THREAD_COUNT   4    // 最大线程数量

// 用户级线程上下文：保存线程切换时需要恢复的寄存器
// 与内核中的 struct context 类似，但用于用户级线程切换
struct thread_context {
  uint64 return_addr;    // ra: 返回地址寄存器
  uint64 stack_pointer;  // sp: 栈指针寄存器

  // RISC-V callee-saved 寄存器 (被调用者保存的寄存器)
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

// 用户级线程控制块 (Thread Control Block)
struct user_thread {
  char       stack[THREAD_STACK_SIZE]; // 线程私有栈空间
  int        state;                    // 线程状态 (FREE/RUNNING/RUNNABLE)
  struct thread_context context;       // 线程上下文 (寄存器状态)
};

struct user_thread thread_pool[MAX_THREAD_COUNT]; // 线程池
struct user_thread *current_running_thread;       // 当前运行的线程
extern void thread_switch(uint64, uint64);        // 汇编实现的线程切换函数
              
// 初始化用户级线程系统
// 将主线程 (main) 设置为线程0，并标记为运行状态
void 
thread_init(void)
{
  // main() 是线程0，它会首次调用 thread_schedule()
  // 它需要一个栈，这样第一次 thread_switch() 才能保存线程0的状态
  // thread_schedule() 不会再次运行主线程，因为它的状态设置为 RUNNING，
  // 而 thread_schedule() 只选择 RUNNABLE 的线程
  current_running_thread = &thread_pool[0];
  current_running_thread->state = THREAD_RUNNING;
}

// 用户级线程调度器
// 负责选择下一个可运行的线程并进行线程切换
// 采用简单的轮转调度算法 (Round Robin)
void 
thread_schedule(void)
{
  struct user_thread *thread_iter, *next_runnable_thread;
  struct user_thread *prev_thread;

  /* 查找下一个可运行的线程 */
  next_runnable_thread = 0;
  thread_iter = current_running_thread + 1;
  
  // 从当前线程的下一个开始查找，实现轮转调度
  for(int i = 0; i < MAX_THREAD_COUNT; i++){
    if(thread_iter >= thread_pool + MAX_THREAD_COUNT)
      thread_iter = thread_pool;  // 循环回到线程池开始
    if(thread_iter->state == THREAD_RUNNABLE) {
      next_runnable_thread = thread_iter;
      break;
    }
    thread_iter = thread_iter + 1;
  }

  if (next_runnable_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  /* 如果需要切换线程则进行上下文切换 */
  if (current_running_thread != next_runnable_thread) {
    next_runnable_thread->state = THREAD_RUNNING;
    prev_thread = current_running_thread;
    current_running_thread = next_runnable_thread;
    
    // 执行底层的线程上下文切换：保存当前线程寄存器，恢复目标线程寄存器
    thread_switch((uint64)&prev_thread->context, (uint64)&current_running_thread->context);
  } else {
    next_runnable_thread = 0;
  }
}

// 创建新的用户级线程
// 找到一个空闲的线程槽，设置线程入口函数和栈
void 
thread_create(void (*thread_entry_func)())
{
  struct user_thread *new_thread;

  // 在线程池中查找空闲的线程槽
  for (new_thread = thread_pool; new_thread < thread_pool + MAX_THREAD_COUNT; new_thread++) {
    if (new_thread->state == THREAD_FREE) break;
  }
  
  new_thread->state = THREAD_RUNNABLE;
  
  // 设置线程上下文：
  // - return_addr: 线程入口函数地址，线程首次运行时会跳转到这里
  // - stack_pointer: 栈顶指针，指向该线程专用栈的顶部
  new_thread->context.return_addr = (uint64) thread_entry_func;
  new_thread->context.stack_pointer = (uint64) new_thread->stack + THREAD_STACK_SIZE;
}

// 当前线程主动让出CPU
// 将当前线程状态改为可运行，然后触发调度
void 
thread_yield(void)
{
  current_running_thread->state = THREAD_RUNNABLE;
  thread_schedule();
}

// 全局线程同步标志和计数器
volatile int thread_a_started, thread_b_started, thread_c_started;
volatile int thread_a_count, thread_b_count, thread_c_count;

// 测试线程A：执行100次循环并输出
void 
thread_a(void)
{
  int loop_count;
  printf("thread_a started\n");
  thread_a_started = 1;
  
  // 等待其他线程启动，实现同步
  while(thread_b_started == 0 || thread_c_started == 0)
    thread_yield();
  
  for (loop_count = 0; loop_count < 100; loop_count++) {
    printf("thread_a %d\n", loop_count);
    thread_a_count += 1;
    thread_yield();
  }
  printf("thread_a: exit after %d\n", thread_a_count);

  // 线程结束：标记为空闲状态并触发调度
  current_running_thread->state = THREAD_FREE;
  thread_schedule();
}

// 测试线程B：执行100次循环并输出
void 
thread_b(void)
{
  int loop_count;
  printf("thread_b started\n");
  thread_b_started = 1;
  
  // 等待其他线程启动，实现同步
  while(thread_a_started == 0 || thread_c_started == 0)
    thread_yield();
  
  for (loop_count = 0; loop_count < 100; loop_count++) {
    printf("thread_b %d\n", loop_count);
    thread_b_count += 1;
    thread_yield();
  }
  printf("thread_b: exit after %d\n", thread_b_count);

  // 线程结束：标记为空闲状态并触发调度
  current_running_thread->state = THREAD_FREE;
  thread_schedule();
}

// 测试线程C：执行100次循环并输出
void 
thread_c(void)
{
  int loop_count;
  printf("thread_c started\n");
  thread_c_started = 1;
  
  // 等待其他线程启动，实现同步
  while(thread_a_started == 0 || thread_b_started == 0)
    thread_yield();
  
  for (loop_count = 0; loop_count < 100; loop_count++) {
    printf("thread_c %d\n", loop_count);
    thread_c_count += 1;
    thread_yield();
  }
  printf("thread_c: exit after %d\n", thread_c_count);

  // 线程结束：标记为空闲状态并触发调度
  current_running_thread->state = THREAD_FREE;
  thread_schedule();
}

// 用户级线程测试主程序
int 
main(int argc, char *argv[]) 
{
  // 初始化同步标志和计数器
  thread_a_started = thread_b_started = thread_c_started = 0;
  thread_a_count = thread_b_count = thread_c_count = 0;
  
  // 初始化线程系统
  thread_init();
  
  // 创建三个测试线程
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  
  // 开始线程调度 (主线程让出控制权)
  thread_schedule();
  
  exit(0);
}
