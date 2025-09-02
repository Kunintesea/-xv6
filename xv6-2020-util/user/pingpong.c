#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char const *argv[]) {
    char byte = 'A'; // 用于传送的字节
    // 管道数组：[0]读端，[1]写端
    int parent_to_child[2]; // 父进程->子进程的管道
    int child_to_parent[2]; // 子进程->父进程的管道
    
    // 创建两个管道
    if (pipe(parent_to_child) < 0) {
        printf("pipe() failed\n");
        exit(1);
    }
    if (pipe(child_to_parent) < 0) {
        printf("pipe() failed\n");
        exit(1);
    }
    
    // 使用fork创建子进程
    int pid = fork();
    
    // 错误处理
    if (pid < 0) {
        printf("fork() failed\n");
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        exit(1);
    } else if (pid == 0) { 
        // 子进程代码
        // 关闭不需要的管道端
        close(parent_to_child[1]); // 子进程不需要写入parent_to_child
        close(child_to_parent[0]); // 子进程不需要从child_to_parent读取
        
        // 从父进程读取字节
        if (read(parent_to_child[0], &byte, sizeof(char)) != sizeof(char)) {
            printf("child read() error\n");
            exit(1);
        } else {
            printf("%d: received ping\n", getpid());
        }
        
        // 向父进程写回字节
        if (write(child_to_parent[1], &byte, sizeof(char)) != sizeof(char)) {
            printf("child write() error\n");
            exit(1);
        }
        
        // 关闭使用过的管道端
        close(parent_to_child[0]);
        close(child_to_parent[1]);
        exit(0);
    } else { 
        // 父进程代码
        // 关闭不需要的管道端
        close(parent_to_child[0]); // 父进程不需要从parent_to_child读取
        close(child_to_parent[1]); // 父进程不需要写入child_to_parent
        
        // 向子进程发送字节
        if (write(parent_to_child[1], &byte, sizeof(char)) != sizeof(char)) {
            printf("parent write() error\n");
            exit(1);
        }
        
        // 从子进程读取字节
        if (read(child_to_parent[0], &byte, sizeof(char)) != sizeof(char)) {
            printf("parent read() error\n");
            exit(1);
        } else {
            printf("%d: received pong\n", getpid());
        }
        
        // 关闭使用过的管道端
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        
        // 等待子进程结束
        wait(0);
        exit(0);
    }
}