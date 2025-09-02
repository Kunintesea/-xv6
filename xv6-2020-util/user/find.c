#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

// 递归查找函数
void find(char *path, const char *filename)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    
    // 尝试打开目录
    if ((fd = open(path, 0)) < 0) {
        printf("find: cannot open %s\n", path);
        return;
    }
    
    // 获取路径状态信息
    if (fstat(fd, &st) < 0) {
        printf("find: cannot fstat %s\n", path);
        close(fd);
        return;
    }
    
    // 确保传入的路径是一个目录
    if (st.type != T_DIR) {
        printf("find: %s is not a directory\n", path);
        close(fd);
        return;
    }
    
    // 检查路径长度，避免缓冲区溢出
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        printf("find: path too long\n");
        close(fd);
        return;
    }
    
    // 构建基础路径
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';  // 添加路径分隔符
    
    // 遍历目录中的所有条目
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)
            continue;  // 跳过无效条目
        
        // 构建完整的文件路径
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;  // 确保字符串以null结尾
        
        // 获取当前条目的状态信息
        if (stat(buf, &st) < 0) {
            printf("find: cannot stat %s\n", buf);
            continue;
        }
        
        // 如果是目录且不是"."或".."，则递归搜索
        if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {
            find(buf, filename);
        } 
        // 如果是文件且名称匹配，则打印路径
        else if (st.type == T_FILE && strcmp(filename, p) == 0) {
            printf("%s\n", buf);
        }
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    // 检查命令行参数数量
    if (argc != 3) {
        printf("Usage: find <directory> <filename>\n");
        exit(1);
    }
    
    // 开始查找
    find(argv[1], argv[2]);
    exit(0);
}