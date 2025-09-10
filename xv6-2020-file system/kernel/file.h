struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// inode的内存副本
struct inode {
  uint dev;           // 设备号
  uint inum;          // inode号
  int ref;            // 引用计数
  struct sleeplock lock; // 保护以下所有内容
  int valid;          // inode是否已从磁盘读取?

  short type;         // 磁盘inode的副本
  short major;        // 主设备号
  short minor;        // 次设备号
  short nlink;        // 链接数
  uint size;          // 文件大小
  uint addrs[NDIRECT+2]; // 块地址数组 [直接块+间接块+二级间接块]
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
