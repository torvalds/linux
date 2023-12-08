/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_ASM_X86_RMWcc
#define _TOOLS_LINUX_ASM_X86_RMWcc

#define __GEN_RMWcc(fullop, var, cc, ...)				\
do {									\
	asm_volatile_goto (fullop "; j" cc " %l[cc_label]"		\
			: : "m" (var), ## __VA_ARGS__ 			\
			: "memory" : cc_label);				\
	return 0;							\
cc_label:								\
	return 1;							\
} while (0)

#define GEN_UNARY_RMWcc(op, var, arg0, cc) 				\
	__GEN_RMWcc(op " " arg0, var, cc)

#define GEN_BINARY_RMWcc(op, var, vcon, val, arg0, cc)			\
	__GEN_RMWcc(op " %1, " arg0, var, cc, vcon (val))

#endif /* _TOOLS_LINUX_ASM_X86_RMWcc */
