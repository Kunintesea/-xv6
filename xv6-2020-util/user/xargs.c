#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAX_LINE_LEN 512

int main(int argc, char *argv[]) {
    int argstart = 1;  // 命令参数开始位置
    
    // 解析 -n 选项（虽然我们不完全实现，但至少要能解析）
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        argstart = 3;  // 跳过 -n 和数字参数
    }
    
    if (argc < argstart + 1) {
        printf("Usage: xargs [-n num] <command> [args...]\n");
        exit(1);
    }
    
    // 检查初始参数数量
    int base_argc = argc - argstart;
    if (base_argc >= MAXARG) {
        printf("xargs: too many initial arguments\n");
        exit(1);
    }
    
    char line[MAX_LINE_LEN];
    char *exec_argv[MAXARG];
    
    // 复制基础命令和参数到exec_argv
    for (int i = 0; i < base_argc; i++) {
        exec_argv[i] = argv[i + argstart];
    }
    
    char c;
    int line_pos = 0;
    int read_size;
    
    // 逐字符读取标准输入
    while ((read_size = read(0, &c, sizeof(char))) == sizeof(char)) {
        if (c == '\n') {
            // 处理一行数据
            if (line_pos > 0) {
                line[line_pos] = 0;  // null终止字符串
                
                // 检查参数数量
                if (base_argc + 1 >= MAXARG) {
                    printf("xargs: too many arguments\n");
                    exit(1);
                }
                
                // 将这一行作为额外参数添加到exec_argv
                exec_argv[base_argc] = line;
                exec_argv[base_argc + 1] = 0;  // null终止参数列表
                
                // fork并执行命令
                if (fork() == 0) {
                    exec(exec_argv[0], exec_argv);
                    printf("xargs: exec failed\n");
                    exit(1);
                } else {
                    wait(0);  // 等待子进程完成
                }
            }
            line_pos = 0;  // 重置行位置
        } else {
            // 累积字符到当前行
            if (line_pos >= MAX_LINE_LEN - 1) {
                printf("xargs: line too long\n");
                exit(1);
            }
            line[line_pos++] = c;
        }
    }
    
    // 处理最后一行（如果没有以换行符结尾）
    if (line_pos > 0) {
        line[line_pos] = 0;
        
        if (base_argc + 1 >= MAXARG) {
            printf("xargs: too many arguments\n");
            exit(1);
        }
        
        exec_argv[base_argc] = line;
        exec_argv[base_argc + 1] = 0;
        
        if (fork() == 0) {
            exec(exec_argv[0], exec_argv);
            printf("xargs: exec failed\n");
            exit(1);
        } else {
            wait(0);
        }
    }
    
    exit(0);
}