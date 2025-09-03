#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

/**
 * 封装系统调用sysinfo的辅助函数
 * 如果系统调用失败则直接退出程序
 */
void
get_sysinfo(struct sysinfo *info) {
  if (sysinfo(info) < 0) {
    printf("FAIL: sysinfo failed");
    exit(1);
  }
}

/**
 * 计算系统中可用的物理内存页面数量
 * 
 * 实现原理:
 * 1. 记录初始堆大小
 * 2. 不断申请内存直到失败
 * 3. 统计总共申请的内存量
 * 4. 恢复初始堆大小
 * 
 * 返回值: 系统当前可用的物理内存字节数
 */
int
count_free_mem()
{
  uint64 initial_heap = (uint64)sbrk(0);    // 保存初始堆大小
  struct sysinfo sys_status;
  int total_free = 0;

  // 不断申请内存直到失败
  while(1){
    if((uint64)sbrk(PGSIZE) == 0xffffffffffffffff){
      break;
    }
    total_free += PGSIZE;
  }

  // 验证此时系统确实没有空闲内存了
  get_sysinfo(&sys_status);
  if (sys_status.freemem != 0) {
    printf("FAIL: there is no free mem, but sysinfo.freemem=%d\n",
      sys_status.freemem);
    exit(1);
  }

  // 恢复到初始堆大小
  sbrk(-((uint64)sbrk(0) - initial_heap));
  return total_free;
}

/**
 * 测试内存相关功能
 * 
 * 测试步骤:
 * 1. 检查系统报告的空闲内存是否准确
 * 2. 分配一页内存，验证空闲内存是否相应减少
 * 3. 释放这页内存，验证空闲内存是否恢复
 */
void
test_memory() {
  struct sysinfo sys_status;
  uint64 expected_free = count_free_mem();  // 获取预期的空闲内存
  
  // 测试1: 验证初始空闲内存统计
  get_sysinfo(&sys_status);
  if (sys_status.freemem != expected_free) {
    printf("FAIL: free mem %d (bytes) instead of %d\n", sys_status.freemem, expected_free);
    exit(1);
  }
  
  // 测试2: 分配一页内存后的空闲内存统计
  if((uint64)sbrk(PGSIZE) == 0xffffffffffffffff){
    printf("sbrk failed");
    exit(1);
  }
  get_sysinfo(&sys_status);
  if (sys_status.freemem != expected_free-PGSIZE) {
    printf("FAIL: free mem %d (bytes) instead of %d\n", 
           expected_free-PGSIZE, sys_status.freemem);
    exit(1);
  }
  
  // 测试3: 释放内存后的空闲内存统计
  if((uint64)sbrk(-PGSIZE) == 0xffffffffffffffff){
    printf("sbrk failed");
    exit(1);
  }
  get_sysinfo(&sys_status);
  if (sys_status.freemem != expected_free) {
    printf("FAIL: free mem %d (bytes) instead of %d\n", 
           expected_free, sys_status.freemem);
    exit(1);
  }
}

/**
 * 测试sysinfo系统调用的基本功能
 * 
 * 测试内容:
 * 1. 验证正常调用是否成功
 * 2. 验证传入无效地址时是否正确返回错误
 */
void
test_sysinfo_call() {
  struct sysinfo sys_status;
  
  // 测试1: 正常调用
  if (sysinfo(&sys_status) < 0) {
    printf("FAIL: sysinfo failed with valid argument\n");
    exit(1);
  }

  // 测试2: 传入无效地址
  // 0xeaeb0b5b00002f5e 是一个无效的用户空间地址
  if (sysinfo((struct sysinfo *) 0xeaeb0b5b00002f5e) != -1) {
    printf("FAIL: sysinfo succeeded with invalid argument\n");
    exit(1);
  }
}

/**
 * 测试进程计数功能
 * 
 * 测试步骤:
 * 1. 获取当前进程数
 * 2. 创建子进程，验证进程数是否增加
 * 3. 等待子进程结束，验证进程数是否恢复
 */
void 
test_process_count() {
  struct sysinfo sys_status;
  uint64 initial_procs;
  int status;
  int pid;
  
  // 获取初始进程数
  get_sysinfo(&sys_status);
  initial_procs = sys_status.nproc;

  // 创建子进程
  pid = fork();
  if(pid < 0){
    printf("sysinfotest: fork failed\n");
    exit(1);
  }

  if(pid == 0){  // 子进程
    get_sysinfo(&sys_status);
    // 验证进程数是否增加了1
    if(sys_status.nproc != initial_procs + 1) {
      printf("sysinfotest: FAIL nproc is %d instead of %d\n", 
             sys_status.nproc, initial_procs + 1);
      exit(1);
    }
    exit(0);
  }

  // 父进程等待子进程结束
  wait(&status);
  
  // 验证进程数是否恢复到原值
  get_sysinfo(&sys_status);
  if(sys_status.nproc != initial_procs) {
    printf("sysinfotest: FAIL nproc is %d instead of %d\n", 
           sys_status.nproc, initial_procs);
    exit(1);
  }
}

/**
 * 主测试程序
 * 依次运行所有测试用例
 */
int
main(int argc, char *argv[])
{
  printf("sysinfotest: start\n");
  test_sysinfo_call();    // 测试系统调用基本功能
  test_memory();          // 测试内存统计功能
  test_process_count();   // 测试进程计数功能
  printf("sysinfotest: OK\n");
  exit(0);
}
