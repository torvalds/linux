#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#ifdef CONFIG_X86_64
#define __ALIGN .p2align 4,,15
#define __ALIGN_STR ".p2align 4,,15"
#endif

#ifdef CONFIG_X86_32
#define asmlinkage CPP_ASMLINKAGE __attribute__((regparm(0)))
/*
 * For 32-bit UML - mark functions implemented in assembly that use
 * regparm input parameters:
 */
#define asmregparm __attribute__((regparm(3)))

#define asmlinkage_protect(n, ret, args...) \
	__asmlinkage_protect##n(ret, ##args)
#define __asmlinkage_protect_n(ret, args...) \
	__asm__ __volatile__ ("" : "=r" (ret) : "0" (ret), ##args)
#define __asmlinkage_protect0(ret) \
	__asmlinkage_protect_n(ret)
#define __asmlinkage_protect1(ret, arg1) \
	__asmlinkage_protect_n(ret, "g" (arg1))
#define __asmlinkage_protect2(ret, arg1, arg2) \
	__asmlinkage_protect_n(ret, "g" (arg1), "g" (arg2))
#define __asmlinkage_protect3(ret, arg1, arg2, arg3) \
	__asmlinkage_protect_n(ret, "g" (arg1), "g" (arg2), "g" (arg3))
#define __asmlinkage_protect4(ret, arg1, arg2, arg3, arg4) \
	__asmlinkage_protect_n(ret, "g" (arg1), "g" (arg2), "g" (arg3), \
			      "g" (arg4))
#define __asmlinkage_protect5(ret, arg1, arg2, arg3, arg4, arg5) \
	__asmlinkage_protect_n(ret, "g" (arg1), "g" (arg2), "g" (arg3), \
			      "g" (arg4), "g" (arg5))
#define __asmlinkage_protect6(ret, arg1, arg2, arg3, arg4, arg5, arg6) \
	__asmlinkage_protect_n(ret, "g" (arg1), "g" (arg2), "g" (arg3), \
			      "g" (arg4), "g" (arg5), "g" (arg6))

#endif

#ifdef CONFIG_X86_ALIGNMENT_16
#define __ALIGN .align 16,0x90
#define __ALIGN_STR ".align 16,0x90"
#endif

#endif

