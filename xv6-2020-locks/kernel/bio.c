// Buffer cache.
//
// The buffer cache uses a hash table with fine-grained locking to reduce contention.
// Each hash bucket has its own lock, allowing concurrent access to different blocks.
// Cached copies of disk block contents are stored in memory to reduce disk reads
// and provide synchronization for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 哈希表优化配置
#define HASH_BUCKET_COUNT 13        // 使用质数减少哈希冲突

extern uint ticks;  // 系统时钟计数，用于LRU时间戳

// 缓冲区缓存管理结构
struct {
  struct spinlock global_lock;                    // 全局锁，用于保护整体状态
  struct buf buffer_pool[NBUF];                   // 缓冲区池
  int allocated_buffer_count;                     // 已分配的缓冲区数量
  
  // 哈希表结构：每个桶维护一个链表和独立的锁
  struct buf hash_buckets[HASH_BUCKET_COUNT];     // 哈希桶头节点
  struct spinlock bucket_locks[HASH_BUCKET_COUNT]; // 每个桶的独立锁
  struct spinlock eviction_lock;                  // 驱逐操作的序列化锁
} buffer_cache;

// 初始化缓冲区缓存系统
// 设置哈希表、锁和缓冲区池
void
binit(void)
{
  struct buf *buffer_ptr;
  int bucket_index;

  buffer_cache.allocated_buffer_count = 0;
  
  // 初始化各种锁
  initlock(&buffer_cache.global_lock, "bcache");
  initlock(&buffer_cache.eviction_lock, "bcache.eviction");
  
  // 初始化每个哈希桶的锁
  for(bucket_index = 0; bucket_index < HASH_BUCKET_COUNT; bucket_index++) {
    initlock(&buffer_cache.bucket_locks[bucket_index], "bcache.bucket");
  }

  // 初始化缓冲区池中每个缓冲区的睡眠锁
  for(buffer_ptr = buffer_cache.buffer_pool; buffer_ptr < buffer_cache.buffer_pool + NBUF; buffer_ptr++){
    initsleeplock(&buffer_ptr->lock, "buffer");
  }
}

// 计算设备号和块号对应的哈希桶索引
static uint
calculate_hash_index(uint blockno)
{
  return blockno % HASH_BUCKET_COUNT;
}

// 在缓冲区缓存中查找指定的块
// 如果未找到，则分配一个新的缓冲区
// 返回已锁定的缓冲区
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *buffer_ptr;
  // 计算哈希桶索引
  int bucket_index = calculate_hash_index(blockno);
  struct buf *prev_ptr, *min_buffer_ptr = 0, *min_prev_ptr;
  uint min_timestamp;
  int i;

  // 在指定哈希桶中查找缓冲区
  acquire(&buffer_cache.bucket_locks[bucket_index]);
  for(buffer_ptr = buffer_cache.hash_buckets[bucket_index].next; buffer_ptr; buffer_ptr = buffer_ptr->next){
    if(buffer_ptr->dev == dev && buffer_ptr->blockno == blockno){
      buffer_ptr->refcnt++;
      release(&buffer_cache.bucket_locks[bucket_index]);
      acquiresleep(&buffer_ptr->lock);
      return buffer_ptr;
    }
  }

  // 缓存中未找到，检查是否有未使用的缓冲区
  acquire(&buffer_cache.global_lock);
  if(buffer_cache.allocated_buffer_count < NBUF) {
    buffer_ptr = &buffer_cache.buffer_pool[buffer_cache.allocated_buffer_count++];
    release(&buffer_cache.global_lock);
    buffer_ptr->dev = dev;
    buffer_ptr->blockno = blockno;
    buffer_ptr->valid = 0;
    buffer_ptr->refcnt = 1;
    buffer_ptr->timestamp = ticks;
    buffer_ptr->next = buffer_cache.hash_buckets[bucket_index].next;
    buffer_cache.hash_buckets[bucket_index].next = buffer_ptr;
    release(&buffer_cache.bucket_locks[bucket_index]);
    acquiresleep(&buffer_ptr->lock);
    return buffer_ptr;
  }
  release(&buffer_cache.global_lock);
  release(&buffer_cache.bucket_locks[bucket_index]);

  // 从所有哈希桶中选择最近最少使用的块进行替换
  // 基于时间戳的LRU替换策略
  acquire(&buffer_cache.eviction_lock);
  for(i = 0; i < HASH_BUCKET_COUNT; ++i) {
      min_timestamp = -1;
      acquire(&buffer_cache.bucket_locks[bucket_index]);
      for(prev_ptr = &buffer_cache.hash_buckets[bucket_index], buffer_ptr = prev_ptr->next; buffer_ptr; prev_ptr = buffer_ptr, buffer_ptr = buffer_ptr->next) {
          // 再次检查是否在其他线程操作期间找到了该块
          if(bucket_index == calculate_hash_index(blockno) && buffer_ptr->dev == dev && buffer_ptr->blockno == blockno){
              buffer_ptr->refcnt++;
              release(&buffer_cache.bucket_locks[bucket_index]);
              release(&buffer_cache.eviction_lock);
              acquiresleep(&buffer_ptr->lock);
              return buffer_ptr;
          }
          if(buffer_ptr->refcnt == 0 && buffer_ptr->timestamp < min_timestamp) {
              min_buffer_ptr = buffer_ptr;
              min_prev_ptr = prev_ptr;
              min_timestamp = buffer_ptr->timestamp;
          }
      }
      // 找到一个未使用的块进行替换
      if(min_buffer_ptr) {
          min_buffer_ptr->dev = dev;
          min_buffer_ptr->blockno = blockno;
          min_buffer_ptr->valid = 0;
          min_buffer_ptr->refcnt = 1;
          // 如果块在其他桶中，需要将其移动到正确的桶
          if(bucket_index != calculate_hash_index(blockno)) {
              min_prev_ptr->next = min_buffer_ptr->next;    // 从当前桶移除
              release(&buffer_cache.bucket_locks[bucket_index]);
              bucket_index = calculate_hash_index(blockno);  // 获取正确的桶索引
              acquire(&buffer_cache.bucket_locks[bucket_index]);
              min_buffer_ptr->next = buffer_cache.hash_buckets[bucket_index].next;    // 移动到正确的桶
              buffer_cache.hash_buckets[bucket_index].next = min_buffer_ptr;
          }
          release(&buffer_cache.bucket_locks[bucket_index]);
          release(&buffer_cache.eviction_lock);
          acquiresleep(&min_buffer_ptr->lock);
          return min_buffer_ptr;
      }
      release(&buffer_cache.bucket_locks[bucket_index]);
      if(++bucket_index == HASH_BUCKET_COUNT) {
          bucket_index = 0;
      }
  }
  panic("bget: no buffers");
}

// 返回带有指定块内容的已锁定缓冲区
// 如果块不在缓存中，则从磁盘读取
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *buffer_ptr;

  buffer_ptr = bget(dev, blockno);
  if(!buffer_ptr->valid) {
    virtio_disk_rw(buffer_ptr, 0);
    buffer_ptr->valid = 1;
  }
  return buffer_ptr;
}

// 将缓冲区内容写入磁盘
// 调用者必须持有缓冲区锁
void
bwrite(struct buf *buffer_ptr)
{
  if(!holdingsleep(&buffer_ptr->lock))
    panic("bwrite");
  virtio_disk_rw(buffer_ptr, 1);
}

// 释放已锁定的缓冲区
// 移动到最近使用列表的头部
void
brelse(struct buf *buffer_ptr)
{
  int bucket_index;
  if(!holdingsleep(&buffer_ptr->lock))
    panic("brelse");

  releasesleep(&buffer_ptr->lock);

  // 获取对应哈希桶的锁
  bucket_index = calculate_hash_index(buffer_ptr->blockno);
  acquire(&buffer_cache.bucket_locks[bucket_index]);
  buffer_ptr->refcnt--;
  if (buffer_ptr->refcnt == 0) {
    // 更新时间戳以记录最近访问时间
    buffer_ptr->timestamp = ticks;
  }
  
  release(&buffer_cache.bucket_locks[bucket_index]);
}

// 增加缓冲区的引用计数（固定缓冲区）
// 防止缓冲区被回收
void
bpin(struct buf *buffer_ptr) {
  // 获取缓冲区对应哈希桶的锁
  int bucket_index = calculate_hash_index(buffer_ptr->blockno);
  acquire(&buffer_cache.bucket_locks[bucket_index]);
  buffer_ptr->refcnt++;
  release(&buffer_cache.bucket_locks[bucket_index]);
}

// 减少缓冲区的引用计数（取消固定缓冲区）
// 允许缓冲区被回收
void
bunpin(struct buf *buffer_ptr) {
  // 获取缓冲区对应哈希桶的锁
  int bucket_index = calculate_hash_index(buffer_ptr->blockno);
  acquire(&buffer_cache.bucket_locks[bucket_index]);
  buffer_ptr->refcnt--;
  release(&buffer_cache.bucket_locks[bucket_index]);
}