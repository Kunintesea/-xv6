#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void) {
    // 测试小端序
    unsigned int i = 0x00646c72;
    printf("Test: H%x Wo%s\n", 57616, &i);
    
    // 测试缺少参数
    printf("Missing: x=%d y=%d\n", 3);
    
    exit(0);
}
