#include "kernel/types.h"
#include "user/user.h"

// 素数筛选函数 - 每个进程负责一个素数的筛选
void sieve(int left_pipe[2])
{
    // 关闭写端，只从左邻居读取
    close(left_pipe[1]);
    
    int prime;
    // 读取第一个数字，它一定是素数
    if (read(left_pipe[0], &prime, sizeof(int)) != sizeof(int)) {
        close(left_pipe[0]);
        exit(0);
    }
    
    printf("prime %d\n", prime);
    
    // 创建到右邻居的管道
    int right_pipe[2];
    pipe(right_pipe);
    
    // 创建子进程来处理下一阶段的筛选
    if (fork() == 0) {
        // 子进程：关闭当前阶段的管道，继续下一阶段筛选
        close(left_pipe[0]);
        sieve(right_pipe);
    } else {
        // 父进程：过滤数据并传递给子进程
        close(right_pipe[0]); // 父进程不需要从right_pipe读取
        
        int next_number;
        // 读取剩余的数字，过滤掉能被当前素数整除的数字
        while (read(left_pipe[0], &next_number, sizeof(int)) == sizeof(int)) {
            if (next_number % prime != 0) {
                // 不能被当前素数整除，传递给下一个进程
                write(right_pipe[1], &next_number, sizeof(int));
            }
        }
        
        close(left_pipe[0]);
        close(right_pipe[1]); // 关闭写端，通知子进程没有更多数据
        wait(0); // 等待子进程完成
    }
    
    exit(0);
}

int main(int argc, char const *argv[])
{
    // 创建初始管道
    int initial_pipe[2];
    pipe(initial_pipe);
    
    // 主进程向管道写入 2 到 35 的所有数字
    for (int i = 2; i <= 35; i++) {
        write(initial_pipe[1], &i, sizeof(int));
    }
    
    // 关闭写端，准备让子进程读取
    close(initial_pipe[1]);
    
    // 创建第一个筛选进程
    if (fork() == 0) {
        sieve(initial_pipe);
    } else {
        // 主进程关闭读端并等待所有子进程完成
        close(initial_pipe[0]);
        wait(0);
    }
    
    exit(0);
}