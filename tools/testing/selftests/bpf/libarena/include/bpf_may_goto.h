#pragma once

/*
 * Note that cond_break can only be portably used in the body of a breakable
 * construct, whereas can_loop can be used anywhere.
 */
#ifdef __BPF_FEATURE_MAY_GOTO
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("may_goto %l[l_break]"	\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("may_goto %l[l_break]"	\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#else
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long ((%l[l_break] - 1b - 8) / 8) & 0xffff;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long ((%l[l_break] - 1b - 8) / 8) & 0xffff;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#else
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long (((%l[l_break] - 1b - 8) / 8) & 0xffff) << 16;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long (((%l[l_break] - 1b - 8) / 8) & 0xffff) << 16;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#endif
#endif

#define cond_break __cond_break(break)
#define cond_break_label(label) __cond_break(goto label)
