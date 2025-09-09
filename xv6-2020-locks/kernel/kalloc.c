// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// 空闲页面链表节点
struct free_page_node {
  struct free_page_node *next;
};

// 每CPU内存分配器：减少锁竞争，提升并发性能
// 每个CPU维护独立的空闲页面链表和锁
struct per_cpu_allocator {
  struct spinlock lock;              // 保护该CPU空闲链表的锁
  struct free_page_node *freelist;  // 该CPU的空闲页面链表
  char lock_name[16];                // 锁的名称 (便于调试)
} cpu_allocators[NCPU];

// 初始化每CPU内存分配器
void
kinit()
{
  int cpu_index;
  
  // 为每个CPU初始化独立的分配器
  for (cpu_index = 0; cpu_index < NCPU; ++cpu_index) {
    snprintf(cpu_allocators[cpu_index].lock_name, 16, "kmem_cpu_%d", cpu_index);
    initlock(&cpu_allocators[cpu_index].lock, cpu_allocators[cpu_index].lock_name);
    cpu_allocators[cpu_index].freelist = 0;  // 初始化为空链表
  }
  
  // 将所有可用内存添加到当前CPU的空闲链表中
  freerange(end, (void*)PHYSTOP);
}

// 将指定范围内的物理内存添加到空闲链表
void
freerange(void *pa_start, void *pa_end)
{
  char *page_addr;
  page_addr = (char*)PGROUNDUP((uint64)pa_start);
  for(; page_addr + PGSIZE <= (char*)pa_end; page_addr += PGSIZE)
    kfree(page_addr);
}

// 释放一页物理内存
// 将页面添加到当前CPU的空闲链表中
void
kfree(void *pa)
{
  struct free_page_node *free_page;
  int current_cpu_id;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  free_page = (struct free_page_node*)pa;

  // 获取当前CPU ID (关闭中断防止CPU切换)
  push_off();
  current_cpu_id = cpuid();
  pop_off();
  
  // 将页面添加到当前CPU的空闲链表头部
  acquire(&cpu_allocators[current_cpu_id].lock);
  free_page->next = cpu_allocators[current_cpu_id].freelist;
  cpu_allocators[current_cpu_id].freelist = free_page;
  release(&cpu_allocators[current_cpu_id].lock);
}

// 从其他CPU"窃取"页面：当当前CPU空闲链表为空时使用
// 使用快慢指针找到链表中点，窃取前半部分以减少频繁窃取
// 返回窃取到的页面链表头，如果所有CPU都没有空闲页面则返回0
struct free_page_node *
steal_pages_from_other_cpu(int current_cpu_id) 
{
    int checked_cpu_count;
    int target_cpu_id = current_cpu_id;
    struct free_page_node *fast_ptr, *slow_ptr, *stolen_list_head;
    
    // 轮询检查其他CPU的空闲链表
    for (checked_cpu_count = 1; checked_cpu_count < NCPU; ++checked_cpu_count) {
        // 循环到下一个CPU
        if (++target_cpu_id == NCPU) {
            target_cpu_id = 0;
        }
        
        acquire(&cpu_allocators[target_cpu_id].lock);
        if (cpu_allocators[target_cpu_id].freelist) {
            // 使用快慢指针算法找到链表中点，窃取前半部分
            slow_ptr = stolen_list_head = cpu_allocators[target_cpu_id].freelist;
            fast_ptr = slow_ptr->next;
            
            // 快指针每次移动2步，慢指针每次移动1步
            while (fast_ptr) {
                fast_ptr = fast_ptr->next;
                if (fast_ptr) {
                    slow_ptr = slow_ptr->next;
                    fast_ptr = fast_ptr->next;
                }
            }
            
            // 将目标CPU的空闲链表从中点处断开
            cpu_allocators[target_cpu_id].freelist = slow_ptr->next;
            release(&cpu_allocators[target_cpu_id].lock);
            
            // 断开窃取的链表
            slow_ptr->next = 0;
            return stolen_list_head;
        }
        release(&cpu_allocators[target_cpu_id].lock);
    }
    return 0;  // 所有CPU都没有空闲页面
}

// 分配一页物理内存
// 首先尝试从当前CPU的空闲链表分配，如果为空则从其他CPU窃取
// 返回页面地址，如果无法分配则返回0
void *
kalloc(void)
{
  struct free_page_node *allocated_page;
  int current_cpu_id;
  
  // 获取当前CPU ID (关闭中断防止CPU切换)
  push_off();
  current_cpu_id = cpuid();
  pop_off();
  
  // 首先尝试从当前CPU的空闲链表分配
  acquire(&cpu_allocators[current_cpu_id].lock);
  allocated_page = cpu_allocators[current_cpu_id].freelist;
  if(allocated_page)
    cpu_allocators[current_cpu_id].freelist = allocated_page->next;
  release(&cpu_allocators[current_cpu_id].lock);
  
  // 如果当前CPU没有空闲页面，尝试从其他CPU窃取
  if(!allocated_page && (allocated_page = steal_pages_from_other_cpu(current_cpu_id))) {
    acquire(&cpu_allocators[current_cpu_id].lock);
    // 将窃取的页面链表（除第一个页面外）添加到当前CPU的空闲链表
    cpu_allocators[current_cpu_id].freelist = allocated_page->next;
    release(&cpu_allocators[current_cpu_id].lock);
  }

  if(allocated_page)
    memset((char*)allocated_page, 5, PGSIZE); // 填充垃圾数据用于调试
  return (void*)allocated_page;
}
