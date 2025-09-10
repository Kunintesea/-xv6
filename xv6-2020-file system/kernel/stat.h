#define T_DIR     1   // 目录类型
#define T_FILE    2   // 普通文件类型  
#define T_DEVICE  3   // 设备文件类型
#define T_SYMLINK 4   // 符号链接文件类型

struct stat {
  int dev;     // 文件系统的磁盘设备号
  uint ino;    // inode编号
  short type;  // 文件类型
  short nlink; // 文件链接数
  uint64 size; // 文件大小(字节)
};
