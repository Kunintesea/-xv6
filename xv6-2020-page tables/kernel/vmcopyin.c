#include "param.h"
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"

//
// 本文件包含 copyin_new() 和 copyinstr_new()，
// 用于替换 vm.c 中的 copyin 和 copyinstr 函数。
// 这些函数直接在内核页表中访问用户地址，避免了软件页表遍历。
//

static struct copyin_stats {
  int copy_count;        // copyin 调用次数
  int copystr_count;     // copyinstr 调用次数
} stats;

int
statscopyin(char *buf, int sz) {
  int n;
  n = snprintf(buf, sz, "copyin: %d\n", stats.copy_count);
  n += snprintf(buf+n, sz, "copyinstr: %d\n", stats.copystr_count);
  return n;
}

// 从用户空间复制数据到内核空间
// 利用进程内核页表中的用户映射直接访问用户内存
// 返回 0 表示成功，-1 表示失败
int
copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  struct proc *p = myproc();

  // 检查地址范围和溢出
  if (srcva >= p->sz || srcva+len >= p->sz || srcva+len < srcva)
    return -1;
  
  // 直接通过虚拟地址访问用户内存（利用内核页表中的用户映射）
  memmove((void *) dst, (void *)srcva, len);
  stats.copy_count++;   // 统计调用次数
  return 0;
}

// 从用户空间复制以空字符结尾的字符串到内核空间
// 最多复制 max 个字节，遇到 '\0' 则停止
// 返回 0 表示成功，-1 表示失败
int
copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  struct proc *p = myproc();
  char *src = (char *) srcva;
  
  stats.copystr_count++;   // 统计调用次数
  
  // 逐字符复制，直到遇到空字符或达到最大长度
  for(int i = 0; i < max && srcva + i < p->sz; i++){
    dst[i] = src[i];
    if(src[i] == '\0')
      return 0;
  }
  return -1;
}
