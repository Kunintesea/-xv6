// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// COW (Copy-On-Write) 页面引用计数管理结构
// 每个物理页面对应一个引用计数和锁
struct page_ref_entry {
  uint8 ref_count;               // 引用计数
  struct spinlock lock;          // 保护引用计数的锁
} page_ref_table[(PHYSTOP - KERNBASE) >> 12];


void freerange(void *pa_start, void *pa_end);

// COW (Copy-On-Write) 引用计数函数前向声明
void inc_ref_count(uint64 pa);
uint8 dec_ref_count(uint64 pa);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct free_page {
  struct free_page *next;
};

struct {
  struct spinlock lock;
  struct free_page *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // 初始化时先增加引用计数，防止 kfree 中减1时下溢出错误
    inc_ref_count((uint64)p);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct free_page *page;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  // 检查引用计数，只有当引用计数为0时才真正释放页面
  if (dec_ref_count((uint64) pa)) {
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  page = (struct free_page*)pa;

  acquire(&kmem.lock);
  page->next = kmem.freelist;
  kmem.freelist = page;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct free_page *page;

  acquire(&kmem.lock);
  page = kmem.freelist;
  if(page)
    kmem.freelist = page->next;
  release(&kmem.lock);

  // 新分配的页面设置引用计数为1
  inc_ref_count((uint64)page);

  if(page)
    memset((char*)page, 5, PGSIZE); // fill with junk
  return (void*)page;
}

// 增加页面引用计数 (COW机制使用)
void 
inc_ref_count(uint64 pa) 
{
  uint64 page_index;
  
  if (pa < KERNBASE) {
    return;
  }
  page_index = (pa - KERNBASE) >> 12;
  acquire(&page_ref_table[page_index].lock);
  ++page_ref_table[page_index].ref_count;
  release(&page_ref_table[page_index].lock);
}

// 减少页面引用计数 (COW机制使用)
// 返回值: 减少后的引用计数
uint8 
dec_ref_count(uint64 pa) 
{
  uint8 remaining_refs;
  uint64 page_index;
  
  if (pa < KERNBASE) {
    return 0;
  }
  page_index = (pa - KERNBASE) >> 12;
  acquire(&page_ref_table[page_index].lock);
  remaining_refs = --page_ref_table[page_index].ref_count;
  release(&page_ref_table[page_index].lock);
  return remaining_refs;
}
