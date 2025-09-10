	.text

	/*
         * 用户级线程上下文切换函数
         * 保存当前线程的寄存器状态，恢复目标线程的寄存器状态
         * 
         * 参数:
         *   a0: 指向当前线程上下文的指针 (struct thread_context *)
         *   a1: 指向目标线程上下文的指针 (struct thread_context *)
         */

	.globl thread_switch
thread_switch:
	/* 保存当前线程的 callee-saved 寄存器到 a0 指向的上下文中 */
    sd ra, 0(a0)      # 保存返回地址 (return_addr)
    sd sp, 8(a0)      # 保存栈指针 (stack_pointer)
    sd s0, 16(a0)     # 保存 callee-saved 寄存器 s0-s11
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    /* 从 a1 指向的上下文中恢复目标线程的 callee-saved 寄存器 */
    ld ra, 0(a1)      # 恢复返回地址 (return_addr)
    ld sp, 8(a1)      # 恢复栈指针 (stack_pointer)
    ld s0, 16(a1)     # 恢复 callee-saved 寄存器 s0-s11
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)
    
	ret    /* 返回到 ra 寄存器指向的地址 (目标线程的执行点) */