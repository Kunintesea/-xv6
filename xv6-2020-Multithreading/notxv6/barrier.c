#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

// 全局配置
static int total_thread_count = 1;  // 参与屏障同步的总线程数
static int global_round = 0;        // 全局轮次计数器

// 屏障同步状态结构
struct barrier_state {
  pthread_mutex_t barrier_mutex;    // 保护屏障状态的互斥锁
  pthread_cond_t barrier_cond;      // 用于线程同步的条件变量
  int arrived_thread_count;         // 当前轮次已到达屏障的线程数
  int current_round;                // 当前屏障轮次
} barrier_sync_state;

// 初始化屏障同步机制
static void
initialize_barrier(void)
{
  assert(pthread_mutex_init(&barrier_sync_state.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&barrier_sync_state.barrier_cond, NULL) == 0);
  barrier_sync_state.arrived_thread_count = 0;
  barrier_sync_state.current_round = 0;
}

// 屏障同步函数：确保所有线程同时到达此点后才能继续执行
// 实现原理：
// 1. 每个线程到达屏障时先获取锁
// 2. 增加已到达线程计数，如果未达到总数则等待
// 3. 最后一个到达的线程负责唤醒所有等待的线程并重置状态
static void 
synchronization_barrier()
{
  // 获取屏障锁，保护临界区
  pthread_mutex_lock(&barrier_sync_state.barrier_mutex);
  
  // 增加当前轮次到达屏障的线程数
  barrier_sync_state.arrived_thread_count++;
  
  // 检查是否所有线程都已到达屏障
  if(barrier_sync_state.arrived_thread_count != total_thread_count) {    
    // 还有线程未到达：当前线程进入等待状态
    // pthread_cond_wait 会自动释放锁并等待条件变量
    pthread_cond_wait(&barrier_sync_state.barrier_cond, &barrier_sync_state.barrier_mutex);
  } else {  
    // 所有线程都已到达：作为最后一个线程，负责统一释放
    
    // 重置到达线程计数，为下一轮做准备
    barrier_sync_state.arrived_thread_count = 0;
    
    // 增加轮次计数
    barrier_sync_state.current_round++;
    
    // 广播唤醒所有等待的线程
    pthread_cond_broadcast(&barrier_sync_state.barrier_cond);
  }
  
  // 释放屏障锁
  pthread_mutex_unlock(&barrier_sync_state.barrier_mutex);
}

// 工作线程函数：执行多轮屏障同步测试
// 每轮都会调用屏障同步，然后随机休眠一段时间
static void *
worker_thread(void *thread_arg)
{
  long thread_id = (long) thread_arg;      // 线程ID
  int iteration_count;                     // 迭代计数器

  // 执行20000轮屏障同步测试
  for (iteration_count = 0; iteration_count < 20000; iteration_count++) {
    // 获取当前轮次，用于验证同步正确性
    int current_barrier_round = barrier_sync_state.current_round;
    
    // 断言：迭代次数应该等于屏障轮次，确保同步正确
    assert(iteration_count == current_barrier_round);
    
    // 调用屏障同步：等待所有线程到达
    synchronization_barrier();
    
    // 同步后随机休眠，模拟实际工作负载
    usleep(random() % 100);
  }

  return 0;
}

// 屏障同步测试主程序
int
main(int argc, char *argv[])
{
  pthread_t *thread_handles;              // 线程句柄数组
  void *thread_return_value;               // 线程返回值
  long thread_index;                       // 线程索引

  // 解析命令行参数
  if (argc < 2) {
    fprintf(stderr, "Usage: %s thread_count\n", argv[0]);
    exit(-1);
  }
  
  total_thread_count = atoi(argv[1]);
  thread_handles = malloc(sizeof(pthread_t) * total_thread_count);
  
  // 初始化随机数生成器
  srandom(0);

  // 初始化屏障同步机制
  initialize_barrier();

  // 创建工作线程
  for(thread_index = 0; thread_index < total_thread_count; thread_index++) {
    assert(pthread_create(&thread_handles[thread_index], NULL, worker_thread, (void *) thread_index) == 0);
  }
  
  // 等待所有线程完成
  for(thread_index = 0; thread_index < total_thread_count; thread_index++) {
    assert(pthread_join(thread_handles[thread_index], &thread_return_value) == 0);
  }
  
  printf("OK; passed\n");
  
  return 0;
}
