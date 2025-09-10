// 磁盘文件系统格式定义
// 内核和用户程序都使用此头文件


#define ROOTINO  1   // 根目录inode编号
#define BSIZE 1024  // 块大小

// 文件系统块组织结构常量
#define NDIRECT 11                                        // 直接块数量
#define NINDIRECT (BSIZE / sizeof(uint))                  // 间接块指针数量
#define NDOUBLY_INDIRECT (NINDIRECT * NINDIRECT)          // 二级间接块数量
#define MAXFILE (NDIRECT + NINDIRECT + NDOUBLY_INDIRECT)  // 文件最大块数
#define NSYMLINK 10                                       // 最大符号链接深度

// 磁盘布局结构:
// [ 引导块 | 超级块 | 日志 | inode块 |
//                           空闲位图 | 数据块]
//
// mkfs计算超级块并构建初始文件系统。超级块描述磁盘布局:
struct superblock {
  uint magic;        // 必须是FSMAGIC
  uint size;         // 文件系统镜像大小(块数)
  uint nblocks;      // 数据块数量
  uint ninodes;      // inode数量
  uint nlog;         // 日志块数量
  uint logstart;     // 第一个日志块的块号
  uint inodestart;   // 第一个inode块的块号
  uint bmapstart;    // 第一个空闲映射块的块号
};

#define FSMAGIC 0x10203040

// 磁盘inode结构
struct dinode {
  short type;           // 文件类型
  short major;          // 主设备号(仅T_DEVICE)
  short minor;          // 次设备号(仅T_DEVICE)
  short nlink;          // 文件系统中inode的链接数
  uint size;            // 文件大小(字节)
  uint addrs[NDIRECT+2];   // 数据块地址数组 [直接块+间接块+二级间接块]
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

