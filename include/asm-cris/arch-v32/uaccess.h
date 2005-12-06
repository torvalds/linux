/*
 * Authors:    Hans-Peter Nilsson (hp@axis.com)
 *
 */
#ifndef _CRIS_ARCH_UACCESS_H
#define _CRIS_ARCH_UACCESS_H

/*
 * We don't tell gcc that we are accessing memory, but this is OK
 * because we do not write to any memory gcc knows about, so there
 * are no aliasing issues.
 *
 * Note that PC at a fault is the address *at* the faulting
 * instruction for CRISv32.
 */
#define __put_user_asm(x, addr, err, op)			\
	__asm__ __volatile__(					\
		"2:	"op" %1,[%2]\n"				\
		"4:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"3:	move.d %3,%0\n"				\
		"	jump 4b\n"				\
		"	nop\n"					\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.dword 2b,3b\n"				\
		"	.previous\n"				\
		: "=r" (err)					\
		: "r" (x), "r" (addr), "g" (-EFAULT), "0" (err))

#define __put_user_asm_64(x, addr, err) do {			\
	int dummy_for_put_user_asm_64_;				\
	__asm__ __volatile__(					\
		"2:	move.d %M2,[%1+]\n"			\
		"4:	move.d %H2,[%1]\n"			\
		"5:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"3:	move.d %4,%0\n"				\
		"	jump 5b\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.dword 2b,3b\n"				\
		"	.dword 4b,3b\n"				\
		"	.previous\n"				\
		: "=r" (err), "=b" (dummy_for_put_user_asm_64_)	\
		: "r" (x), "1" (addr), "g" (-EFAULT),		\
		  "0" (err));					\
	} while (0)

/* See comment before __put_user_asm.  */

#define __get_user_asm(x, addr, err, op)		\
	__asm__ __volatile__(				\
		"2:	"op" [%2],%1\n"			\
		"4:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"3:	move.d %3,%0\n"			\
		"	jump 4b\n"			\
		"	moveq 0,%1\n"			\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.dword 2b,3b\n"			\
		"	.previous\n"			\
		: "=r" (err), "=r" (x)			\
		: "r" (addr), "g" (-EFAULT), "0" (err))

#define __get_user_asm_64(x, addr, err) do {		\
	int dummy_for_get_user_asm_64_;			\
	__asm__ __volatile__(				\
		"2:	move.d [%2+],%M1\n"		\
		"4:	move.d [%2],%H1\n"		\
		"5:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"3:	move.d %4,%0\n"			\
		"	jump 5b\n"			\
		"	moveq 0,%1\n"			\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.dword 2b,3b\n"			\
		"	.dword 4b,3b\n"			\
		"	.previous\n"			\
		: "=r" (err), "=r" (x),			\
		  "=b" (dummy_for_get_user_asm_64_)	\
		: "2" (addr), "g" (-EFAULT), "0" (err));\
	} while (0)

/*
 * Copy a null terminated string from userspace.
 *
 * Must return:
 * -EFAULT		for an exception
 * count		if we hit the buffer limit
 * bytes copied		if we hit a null byte
 * (without the null byte)
 */
static inline long
__do_strncpy_from_user(char *dst, const char *src, long count)
{
	long res;

	if (count == 0)
		return 0;

	/*
	 * Currently, in 2.4.0-test9, most ports use a simple byte-copy loop.
	 *  So do we.
	 *
	 *  This code is deduced from:
	 *
	 *	char tmp2;
	 *	long tmp1, tmp3;
	 *	tmp1 = count;
	 *	while ((*dst++ = (tmp2 = *src++)) != 0
	 *	       && --tmp1)
	 *	  ;
	 *
	 *	res = count - tmp1;
	 *
	 *  with tweaks.
	 */

	__asm__ __volatile__ (
		"	move.d %3,%0\n"
		"5:	move.b [%2+],$acr\n"
		"1:	beq 2f\n"
		"	move.b $acr,[%1+]\n"

		"	subq 1,%0\n"
		"2:	bne 1b\n"
		"	move.b [%2+],$acr\n"

		"	sub.d %3,%0\n"
		"	neg.d %0,%0\n"
		"3:\n"
		"	.section .fixup,\"ax\"\n"
		"4:	move.d %7,%0\n"
		"	jump 3b\n"
		"	nop\n"

		/* The address for a fault at the first move is trivial.
		   The address for a fault at the second move is that of
		   the preceding branch insn, since the move insn is in
		   its delay-slot.  That address is also a branch
		   target.  Just so you don't get confused...  */
		"	.previous\n"
		"	.section __ex_table,\"a\"\n"
		"	.dword 5b,4b\n"
		"	.dword 2b,4b\n"
		"	.previous"
		: "=r" (res), "=b" (dst), "=b" (src), "=r" (count)
		: "3" (count), "1" (dst), "2" (src), "g" (-EFAULT)
		: "acr");

	return res;
}

/* A few copy asms to build up the more complex ones from.

   Note again, a post-increment is performed regardless of whether a bus
   fault occurred in that instruction, and PC for a faulted insn is the
   address for the insn, or for the preceding branch when in a delay-slot.  */

#define __asm_copy_user_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm__ __volatile__ (				\
			COPY				\
		"1:\n"					\
		"	.section .fixup,\"ax\"\n"	\
			FIXUP				\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
			TENTRY				\
		"	.previous\n"			\
		: "=b" (to), "=b" (from), "=r" (ret)	\
		: "0" (to), "1" (from), "2" (ret)	\
		: "acr", "memory")

#define __asm_copy_from_user_1(to, from, ret) \
	__asm_copy_user_cont(to, from, ret,	\
		"2:	move.b [%1+],$acr\n"	\
		"	move.b $acr,[%0+]\n",	\
		"3:	addq 1,%2\n"		\
		"	jump 1b\n"		\
		"	clear.b [%0+]\n",	\
		"	.dword 2b,3b\n")

#define __asm_copy_from_user_2x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
			COPY				\
		"2:	move.w [%1+],$acr\n"		\
		"	move.w $acr,[%0+]\n",		\
			FIXUP				\
		"3:	addq 2,%2\n"			\
		"	jump 1b\n"			\
		"	clear.w [%0+]\n",		\
			TENTRY				\
		"	.dword 2b,3b\n")

#define __asm_copy_from_user_2(to, from, ret) \
	__asm_copy_from_user_2x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_3(to, from, ret)		\
	__asm_copy_from_user_2x_cont(to, from, ret,	\
		"4:	move.b [%1+],$acr\n"		\
		"	move.b $acr,[%0+]\n",		\
		"5:	addq 1,%2\n"			\
		"	clear.b [%0+]\n",		\
		"	.dword 4b,5b\n")

#define __asm_copy_from_user_4x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
			COPY				\
		"2:	move.d [%1+],$acr\n"		\
		"	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"3:	addq 4,%2\n"			\
		"	jump 1b\n"			\
		"	clear.d [%0+]\n",		\
			TENTRY				\
		"	.dword 2b,3b\n")

#define __asm_copy_from_user_4(to, from, ret) \
	__asm_copy_from_user_4x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_5(to, from, ret) \
	__asm_copy_from_user_4x_cont(to, from, ret,	\
		"4:	move.b [%1+],$acr\n"		\
		"	move.b $acr,[%0+]\n",		\
		"5:	addq 1,%2\n"			\
		"	clear.b [%0+]\n",		\
		"	.dword 4b,5b\n")

#define __asm_copy_from_user_6x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_4x_cont(to, from, ret,	\
			COPY				\
		"4:	move.w [%1+],$acr\n"		\
		"	move.w $acr,[%0+]\n",		\
			FIXUP				\
		"5:	addq 2,%2\n"			\
		"	clear.w [%0+]\n",		\
			TENTRY				\
		"	.dword 4b,5b\n")

#define __asm_copy_from_user_6(to, from, ret) \
	__asm_copy_from_user_6x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_7(to, from, ret) \
	__asm_copy_from_user_6x_cont(to, from, ret,	\
		"6:	move.b [%1+],$acr\n"		\
		"	move.b $acr,[%0+]\n",		\
		"7:	addq 1,%2\n"			\
		"	clear.b [%0+]\n",		\
		"	.dword 6b,7b\n")

#define __asm_copy_from_user_8x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_4x_cont(to, from, ret,	\
			COPY				\
		"4:	move.d [%1+],$acr\n"		\
		"	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"5:	addq 4,%2\n"			\
		"	clear.d [%0+]\n",		\
			TENTRY				\
		"	.dword 4b,5b\n")

#define __asm_copy_from_user_8(to, from, ret) \
	__asm_copy_from_user_8x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_9(to, from, ret) \
	__asm_copy_from_user_8x_cont(to, from, ret,	\
		"6:	move.b [%1+],$acr\n"		\
		"	move.b $acr,[%0+]\n",		\
		"7:	addq 1,%2\n"			\
		"	clear.b [%0+]\n",		\
		"	.dword 6b,7b\n")

#define __asm_copy_from_user_10x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_8x_cont(to, from, ret,	\
			COPY				\
		"6:	move.w [%1+],$acr\n"		\
		"	move.w $acr,[%0+]\n",		\
			FIXUP				\
		"7:	addq 2,%2\n"			\
		"	clear.w [%0+]\n",		\
			TENTRY				\
		"	.dword 6b,7b\n")

#define __asm_copy_from_user_10(to, from, ret) \
	__asm_copy_from_user_10x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_11(to, from, ret)		\
	__asm_copy_from_user_10x_cont(to, from, ret,	\
		"8:	move.b [%1+],$acr\n"		\
		"	move.b $acr,[%0+]\n",		\
		"9:	addq 1,%2\n"			\
		"	clear.b [%0+]\n",		\
		"	.dword 8b,9b\n")

#define __asm_copy_from_user_12x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_8x_cont(to, from, ret,	\
			COPY				\
		"6:	move.d [%1+],$acr\n"		\
		"	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"7:	addq 4,%2\n"			\
		"	clear.d [%0+]\n",		\
			TENTRY				\
		"	.dword 6b,7b\n")

#define __asm_copy_from_user_12(to, from, ret) \
	__asm_copy_from_user_12x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_13(to, from, ret) \
	__asm_copy_from_user_12x_cont(to, from, ret,	\
		"8:	move.b [%1+],$acr\n"		\
		"	move.b $acr,[%0+]\n",		\
		"9:	addq 1,%2\n"			\
		"	clear.b [%0+]\n",		\
		"	.dword 8b,9b\n")

#define __asm_copy_from_user_14x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_12x_cont(to, from, ret,	\
			COPY				\
		"8:	move.w [%1+],$acr\n"		\
		"	move.w $acr,[%0+]\n",		\
			FIXUP				\
		"9:	addq 2,%2\n"			\
		"	clear.w [%0+]\n",		\
			TENTRY				\
		"	.dword 8b,9b\n")

#define __asm_copy_from_user_14(to, from, ret) \
	__asm_copy_from_user_14x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_15(to, from, ret) \
	__asm_copy_from_user_14x_cont(to, from, ret,	\
		"10:	move.b [%1+],$acr\n"		\
		"	move.b $acr,[%0+]\n",		\
		"11:	addq 1,%2\n"			\
		"	clear.b [%0+]\n",		\
		"	.dword 10b,11b\n")

#define __asm_copy_from_user_16x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_12x_cont(to, from, ret,	\
			COPY				\
		"8:	move.d [%1+],$acr\n"		\
		"	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"9:	addq 4,%2\n"			\
		"	clear.d [%0+]\n",		\
			TENTRY				\
		"	.dword 8b,9b\n")

#define __asm_copy_from_user_16(to, from, ret) \
	__asm_copy_from_user_16x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_20x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_16x_cont(to, from, ret,	\
			COPY				\
		"10:	move.d [%1+],$acr\n"		\
		"	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"11:	addq 4,%2\n"			\
		"	clear.d [%0+]\n",		\
			TENTRY				\
		"	.dword 10b,11b\n")

#define __asm_copy_from_user_20(to, from, ret) \
	__asm_copy_from_user_20x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_24x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_20x_cont(to, from, ret,	\
			COPY				\
		"12:	move.d [%1+],$acr\n"		\
		"	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"13:	addq 4,%2\n"			\
		"	clear.d [%0+]\n",		\
			TENTRY				\
		"	.dword 12b,13b\n")

#define __asm_copy_from_user_24(to, from, ret) \
	__asm_copy_from_user_24x_cont(to, from, ret, "", "", "")

/* And now, the to-user ones.  */

#define __asm_copy_to_user_1(to, from, ret)	\
	__asm_copy_user_cont(to, from, ret,	\
		"	move.b [%1+],$acr\n"	\
		"2:	move.b $acr,[%0+]\n",	\
		"3:	jump 1b\n"		\
		"	addq 1,%2\n",		\
		"	.dword 2b,3b\n")

#define __asm_copy_to_user_2x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
			COPY				\
		"	move.w [%1+],$acr\n"		\
		"2:	move.w $acr,[%0+]\n",		\
			FIXUP				\
		"3:	jump 1b\n"			\
		"	addq 2,%2\n",			\
			TENTRY				\
		"	.dword 2b,3b\n")

#define __asm_copy_to_user_2(to, from, ret) \
	__asm_copy_to_user_2x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_3(to, from, ret) \
	__asm_copy_to_user_2x_cont(to, from, ret,	\
		"	move.b [%1+],$acr\n"		\
		"4:	move.b $acr,[%0+]\n",		\
		"5:	addq 1,%2\n",			\
		"	.dword 4b,5b\n")

#define __asm_copy_to_user_4x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
			COPY				\
		"	move.d [%1+],$acr\n"		\
		"2:	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"3:	jump 1b\n"			\
		"	addq 4,%2\n",			\
			TENTRY				\
		"	.dword 2b,3b\n")

#define __asm_copy_to_user_4(to, from, ret) \
	__asm_copy_to_user_4x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_5(to, from, ret) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
		"	move.b [%1+],$acr\n"		\
		"4:	move.b $acr,[%0+]\n",		\
		"5:	addq 1,%2\n",			\
		"	.dword 4b,5b\n")

#define __asm_copy_to_user_6x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
			COPY				\
		"	move.w [%1+],$acr\n"		\
		"4:	move.w $acr,[%0+]\n",		\
			FIXUP				\
		"5:	addq 2,%2\n",			\
			TENTRY				\
		"	.dword 4b,5b\n")

#define __asm_copy_to_user_6(to, from, ret) \
	__asm_copy_to_user_6x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_7(to, from, ret) \
	__asm_copy_to_user_6x_cont(to, from, ret,	\
		"	move.b [%1+],$acr\n"		\
		"6:	move.b $acr,[%0+]\n",		\
		"7:	addq 1,%2\n",			\
		"	.dword 6b,7b\n")

#define __asm_copy_to_user_8x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
			COPY				\
		"	move.d [%1+],$acr\n"		\
		"4:	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"5:	addq 4,%2\n",			\
			TENTRY				\
		"	.dword 4b,5b\n")

#define __asm_copy_to_user_8(to, from, ret) \
	__asm_copy_to_user_8x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_9(to, from, ret) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
		"	move.b [%1+],$acr\n"		\
		"6:	move.b $acr,[%0+]\n",		\
		"7:	addq 1,%2\n",			\
		"	.dword 6b,7b\n")

#define __asm_copy_to_user_10x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
			COPY				\
		"	move.w [%1+],$acr\n"		\
		"6:	move.w $acr,[%0+]\n",		\
			FIXUP				\
		"7:	addq 2,%2\n",			\
			TENTRY				\
		"	.dword 6b,7b\n")

#define __asm_copy_to_user_10(to, from, ret) \
	__asm_copy_to_user_10x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_11(to, from, ret) \
	__asm_copy_to_user_10x_cont(to, from, ret,	\
		"	move.b [%1+],$acr\n"		\
		"8:	move.b $acr,[%0+]\n",		\
		"9:	addq 1,%2\n",			\
		"	.dword 8b,9b\n")

#define __asm_copy_to_user_12x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
			COPY				\
		"	move.d [%1+],$acr\n"		\
		"6:	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"7:	addq 4,%2\n",			\
			TENTRY				\
		"	.dword 6b,7b\n")

#define __asm_copy_to_user_12(to, from, ret) \
	__asm_copy_to_user_12x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_13(to, from, ret) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
		"	move.b [%1+],$acr\n"		\
		"8:	move.b $acr,[%0+]\n",		\
		"9:	addq 1,%2\n",			\
		"	.dword 8b,9b\n")

#define __asm_copy_to_user_14x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
			COPY				\
		"	move.w [%1+],$acr\n"		\
		"8:	move.w $acr,[%0+]\n",		\
			FIXUP				\
		"9:	addq 2,%2\n",			\
			TENTRY				\
		"	.dword 8b,9b\n")

#define __asm_copy_to_user_14(to, from, ret)	\
	__asm_copy_to_user_14x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_15(to, from, ret) \
	__asm_copy_to_user_14x_cont(to, from, ret,	\
		"	move.b [%1+],$acr\n"		\
		"10:	move.b $acr,[%0+]\n",		\
		"11:	addq 1,%2\n",			\
		"	.dword 10b,11b\n")

#define __asm_copy_to_user_16x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
			COPY				\
		"	move.d [%1+],$acr\n"		\
		"8:	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"9:	addq 4,%2\n",			\
			TENTRY				\
		"	.dword 8b,9b\n")

#define __asm_copy_to_user_16(to, from, ret) \
	__asm_copy_to_user_16x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_20x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_16x_cont(to, from, ret,	\
			COPY				\
		"	move.d [%1+],$acr\n"		\
		"10:	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"11:	addq 4,%2\n",			\
			TENTRY				\
		"	.dword 10b,11b\n")

#define __asm_copy_to_user_20(to, from, ret) \
	__asm_copy_to_user_20x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_24x_cont(to, from, ret, COPY, FIXUP, TENTRY)	\
	__asm_copy_to_user_20x_cont(to, from, ret,	\
			COPY				\
		"	move.d [%1+],$acr\n"		\
		"12:	move.d $acr,[%0+]\n",		\
			FIXUP				\
		"13:	addq 4,%2\n",			\
			TENTRY				\
		"	.dword 12b,13b\n")

#define __asm_copy_to_user_24(to, from, ret)	\
	__asm_copy_to_user_24x_cont(to, from, ret, "", "", "")

/* Define a few clearing asms with exception handlers.  */

/* This frame-asm is like the __asm_copy_user_cont one, but has one less
   input.  */

#define __asm_clear(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm__ __volatile__ (				\
			CLEAR				\
		"1:\n"					\
		"	.section .fixup,\"ax\"\n"	\
			FIXUP				\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
			TENTRY				\
		"	.previous"			\
		: "=b" (to), "=r" (ret)			\
		: "0" (to), "1" (ret)			\
		: "memory")

#define __asm_clear_1(to, ret) \
	__asm_clear(to, ret,			\
		"2:	clear.b [%0+]\n",	\
		"3:	jump 1b\n"		\
		"	addq 1,%1\n",		\
		"	.dword 2b,3b\n")

#define __asm_clear_2(to, ret) \
	__asm_clear(to, ret,			\
		"2:	clear.w [%0+]\n",	\
		"3:	jump 1b\n"		\
		"	addq 2,%1\n",		\
		"	.dword 2b,3b\n")

#define __asm_clear_3(to, ret) \
     __asm_clear(to, ret,			\
		 "2:	clear.w [%0+]\n"	\
		 "3:	clear.b [%0+]\n",	\
		 "4:	addq 2,%1\n"		\
		 "5:	jump 1b\n"		\
		 "	addq 1,%1\n",		\
		 "	.dword 2b,4b\n"		\
		 "	.dword 3b,5b\n")

#define __asm_clear_4x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear(to, ret,				\
			CLEAR				\
		"2:	clear.d [%0+]\n",		\
			FIXUP				\
		"3:	jump 1b\n"			\
		"	addq 4,%1\n",			\
			TENTRY				\
		"	.dword 2b,3b\n")

#define __asm_clear_4(to, ret) \
	__asm_clear_4x_cont(to, ret, "", "", "")

#define __asm_clear_8x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_4x_cont(to, ret,			\
			CLEAR				\
		"4:	clear.d [%0+]\n",		\
			FIXUP				\
		"5:	addq 4,%1\n",			\
			TENTRY				\
		"	.dword 4b,5b\n")

#define __asm_clear_8(to, ret) \
	__asm_clear_8x_cont(to, ret, "", "", "")

#define __asm_clear_12x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_8x_cont(to, ret,			\
			CLEAR				\
		"6:	clear.d [%0+]\n",		\
			FIXUP				\
		"7:	addq 4,%1\n",			\
			TENTRY				\
		"	.dword 6b,7b\n")

#define __asm_clear_12(to, ret) \
	__asm_clear_12x_cont(to, ret, "", "", "")

#define __asm_clear_16x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_12x_cont(to, ret,			\
			CLEAR				\
		"8:	clear.d [%0+]\n",		\
			FIXUP				\
		"9:	addq 4,%1\n",			\
			TENTRY				\
		"	.dword 8b,9b\n")

#define __asm_clear_16(to, ret) \
	__asm_clear_16x_cont(to, ret, "", "", "")

#define __asm_clear_20x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_16x_cont(to, ret,			\
			CLEAR				\
		"10:	clear.d [%0+]\n",		\
			FIXUP				\
		"11:	addq 4,%1\n",			\
			TENTRY				\
		"	.dword 10b,11b\n")

#define __asm_clear_20(to, ret) \
	__asm_clear_20x_cont(to, ret, "", "", "")

#define __asm_clear_24x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_20x_cont(to, ret,			\
			CLEAR				\
		"12:	clear.d [%0+]\n",		\
			FIXUP				\
		"13:	addq 4,%1\n",			\
			TENTRY				\
		"	.dword 12b,13b\n")

#define __asm_clear_24(to, ret) \
	__asm_clear_24x_cont(to, ret, "", "", "")

/*
 * Return the size of a string (including the ending 0)
 *
 * Return length of string in userspace including terminating 0
 * or 0 for error.  Return a value greater than N if too long.
 */

static inline long
strnlen_user(const char *s, long n)
{
	long res, tmp1;

	if (!access_ok(VERIFY_READ, s, 0))
		return 0;

	/*
	 * This code is deduced from:
	 *
	 *	tmp1 = n;
	 *	while (tmp1-- > 0 && *s++)
	 *	  ;
	 *
	 *	res = n - tmp1;
	 *
	 *  (with tweaks).
	 */

	__asm__ __volatile__ (
		"	move.d %1,$acr\n"
		"	cmpq 0,$acr\n"
		"0:\n"
		"	ble 1f\n"
		"	subq 1,$acr\n"

		"4:	test.b [%0+]\n"
		"	bne 0b\n"
		"	cmpq 0,$acr\n"
		"1:\n"
		"	move.d %1,%0\n"
		"	sub.d $acr,%0\n"
		"2:\n"
		"	.section .fixup,\"ax\"\n"

		"3:	jump 2b\n"
		"	clear.d %0\n"

		"	.previous\n"
		"	.section __ex_table,\"a\"\n"
		"	.dword 4b,3b\n"
		"	.previous\n"
		: "=r" (res), "=r" (tmp1)
		: "0" (s), "1" (n)
		: "acr");

	return res;
}

#endif
