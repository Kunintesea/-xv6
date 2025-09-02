#include "kernel/types.h"  // 系统类型定义
#include "user/user.h"     // 用户空间系统调用接口

// para_count是参数个数
int main(int para_count, char const *paras[])
{
  // 检查命令行参数数量，应该有且仅有一个参数（除了程序名）
  if (para_count != 2) { 
    // 参数数量错误，打印使用说明并退出
    printf("error! format should be: sleep <time>\n");
    exit(1);  // 以错误状态退出
  }
  
  // 将字符串参数转换为整数，然后调用系统调用 sleep
  // atoi() 将字符串转换为整数
  // sleep() 系统调用让当前进程休眠指定的时钟中断次数
  sleep(atoi(paras[1]));
  
  // 正常退出程序
  exit(0);
}