#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

// 哈希表配置参数
#define HASH_BUCKET_COUNT 7    // 哈希桶数量 (使用质数减少冲突)
#define TOTAL_KEYS 100000      // 测试键的总数

// 哈希表条目结构：使用链表解决哈希冲突
struct hash_entry {
  int key;                     // 键
  int value;                   // 值
  struct hash_entry *next;     // 指向下一个条目的指针
};

// 全局数据结构
struct hash_entry *hash_table[HASH_BUCKET_COUNT];  // 哈希表 (桶数组)
int test_keys[TOTAL_KEYS];                         // 测试用的键数组
int thread_count = 1;                              // 线程数量

// 细粒度锁：每个哈希桶一个互斥锁，提升并发性能
pthread_mutex_t bucket_locks[HASH_BUCKET_COUNT];

// 获取当前时间 (用于性能测试)
double
get_current_time()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// 向哈希表中插入新条目
// 参数: key-键, value-值, bucket_head-桶头指针的地址, next_entry-下一个条目
static void 
insert_hash_entry(int key, int value, struct hash_entry **bucket_head, struct hash_entry *next_entry)
{
  struct hash_entry *new_entry = malloc(sizeof(struct hash_entry));
  new_entry->key = key;
  new_entry->value = value;
  new_entry->next = next_entry;
  *bucket_head = new_entry;
}

// 向哈希表中添加或更新键值对
// 使用细粒度锁定：只锁定相关的哈希桶，提升并发性能
static 
void hash_put(int key, int value)
{
  int bucket_index = key % HASH_BUCKET_COUNT;  // 计算哈希桶索引

  // 检查键是否已存在于哈希表中
  struct hash_entry *existing_entry = 0;
  for (existing_entry = hash_table[bucket_index]; existing_entry != 0; existing_entry = existing_entry->next) {
    if (existing_entry->key == key)
      break;
  }
  
  if(existing_entry){
    // 键已存在：更新值 (无需加锁，只是简单的写操作)
    existing_entry->value = value;
  } else {
    // 新键：需要插入新条目 (加锁保护链表操作)
    pthread_mutex_lock(&bucket_locks[bucket_index]);
    insert_hash_entry(key, value, &hash_table[bucket_index], hash_table[bucket_index]);
    pthread_mutex_unlock(&bucket_locks[bucket_index]);
  }
}

// 从哈希表中查找指定键的条目
// 读操作通常不需要加锁 (假设插入操作是原子的)
static struct hash_entry*
hash_get(int key)
{
  int bucket_index = key % HASH_BUCKET_COUNT;  // 计算哈希桶索引

  struct hash_entry *current_entry = 0;
  for (current_entry = hash_table[bucket_index]; current_entry != 0; current_entry = current_entry->next) {
    if (current_entry->key == key) break;
  }

  return current_entry;
}

// 插入操作线程函数
// 每个线程负责插入一部分键值对
static void *
put_worker_thread(void *thread_arg)
{
  int thread_id = (int) (long) thread_arg;         // 线程编号
  int keys_per_thread = TOTAL_KEYS/thread_count;   // 每个线程处理的键数量

  // 插入分配给该线程的键值对
  for (int i = 0; i < keys_per_thread; i++) {
    hash_put(test_keys[keys_per_thread * thread_id + i], thread_id);
  }

  return NULL;
}

// 查找操作线程函数  
// 每个线程查找所有键并统计丢失的键数量
static void *
get_worker_thread(void *thread_arg)
{
  int thread_id = (int) (long) thread_arg;  // 线程编号
  int missing_keys_count = 0;               // 丢失键的计数

  // 查找所有键，统计丢失的数量
  for (int i = 0; i < TOTAL_KEYS; i++) {
    struct hash_entry *found_entry = hash_get(test_keys[i]);
    if (found_entry == 0) missing_keys_count++;
  }
  
  printf("%d: %d keys missing\n", thread_id, missing_keys_count);
  return NULL;
}

// 并发哈希表性能测试主程序
int
main(int argc, char *argv[])
{
  pthread_t *thread_handles;
  void *thread_return_value;
  double test_start_time, test_end_time;

  // 解析命令行参数
  if (argc < 2) {
    fprintf(stderr, "Usage: %s thread_count\n", argv[0]);
    exit(-1);
  }
  thread_count = atoi(argv[1]);
  thread_handles = malloc(sizeof(pthread_t) * thread_count);
  
  // 初始化随机数生成器和测试数据
  srandom(0);
  assert(TOTAL_KEYS % thread_count == 0);
  for (int i = 0; i < TOTAL_KEYS; i++) {
    test_keys[i] = random();
  }

  // 初始化每个哈希桶的互斥锁
  for(int i = 0; i < HASH_BUCKET_COUNT; ++i) {
      pthread_mutex_init(&bucket_locks[i], NULL);
  }

  //
  // 测试1: 并发插入性能 (PUT操作)
  //
  test_start_time = get_current_time();
  for(int i = 0; i < thread_count; i++) {
    assert(pthread_create(&thread_handles[i], NULL, put_worker_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < thread_count; i++) {
    assert(pthread_join(thread_handles[i], &thread_return_value) == 0);
  }
  test_end_time = get_current_time();

  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         TOTAL_KEYS, test_end_time - test_start_time, TOTAL_KEYS / (test_end_time - test_start_time));

  //
  // 测试2: 并发查找性能 (GET操作)
  //
  test_start_time = get_current_time();
  for(int i = 0; i < thread_count; i++) {
    assert(pthread_create(&thread_handles[i], NULL, get_worker_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < thread_count; i++) {
    assert(pthread_join(thread_handles[i], &thread_return_value) == 0);
  }
  test_end_time = get_current_time();

  printf("%d gets, %.3f seconds, %.0f gets/second\n",
         TOTAL_KEYS*thread_count, test_end_time - test_start_time, (TOTAL_KEYS*thread_count) / (test_end_time - test_start_time));
}
