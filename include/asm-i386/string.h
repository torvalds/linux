#ifndef _I386_STRING_H_
#define _I386_STRING_H_

#ifdef __KERNEL__
/*
 * On a 486 or Pentium, we are better off not using the
 * byte string operations. But on a 386 or a PPro the
 * byte string ops are faster than doing it by hand
 * (MUCH faster on a Pentium).
 */

/*
 * This string-include defines all string functions as inline
 * functions. Use gcc. It also assumes ds=es=data space, this should be
 * normal. Most of the string-functions are rather heavily hand-optimized,
 * see especially strsep,strstr,str[c]spn. They should work, but are not
 * very easy to understand. Everything is done entirely within the register
 * set, making the functions fast and clean. String instructions have been
 * used through-out, making for "slightly" unclear code :-)
 *
 *		NO Copyright (C) 1991, 1992 Linus Torvalds,
 *		consider these trivial functions to be PD.
 */

/* AK: in fact I bet it would be better to move this stuff all out of line.
 */

#define __HAVE_ARCH_STRCPY
static inline char * strcpy(char * dest,const char *src)
{
int d0, d1, d2;
__asm__ __volatile__(
	"1:\tlodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b"
	: "=&S" (d0), "=&D" (d1), "=&a" (d2)
	:"0" (src),"1" (dest) : "memory");
return dest;
}

#define __HAVE_ARCH_STRNCPY
static inline char * strncpy(char * dest,const char *src,size_t count)
{
int d0, d1, d2, d3;
__asm__ __volatile__(
	"1:\tdecl %2\n\t"
	"js 2f\n\t"
	"lodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n\t"
	"rep\n\t"
	"stosb\n"
	"2:"
	: "=&S" (d0), "=&D" (d1), "=&c" (d2), "=&a" (d3)
	:"0" (src),"1" (dest),"2" (count) : "memory");
return dest;
}

#define __HAVE_ARCH_STRCAT
static inline char * strcat(char * dest,const char * src)
{
int d0, d1, d2, d3;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"decl %1\n"
	"1:\tlodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b"
	: "=&S" (d0), "=&D" (d1), "=&a" (d2), "=&c" (d3)
	: "0" (src), "1" (dest), "2" (0), "3" (0xffffffffu):"memory");
return dest;
}

#define __HAVE_ARCH_STRNCAT
static inline char * strncat(char * dest,const char * src,size_t count)
{
int d0, d1, d2, d3;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"decl %1\n\t"
	"movl %8,%3\n"
	"1:\tdecl %3\n\t"
	"js 2f\n\t"
	"lodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n"
	"2:\txorl %2,%2\n\t"
	"stosb"
	: "=&S" (d0), "=&D" (d1), "=&a" (d2), "=&c" (d3)
	: "0" (src),"1" (dest),"2" (0),"3" (0xffffffffu), "g" (count)
	: "memory");
return dest;
}

#define __HAVE_ARCH_STRCMP
static inline int strcmp(const char * cs,const char * ct)
{
int d0, d1;
register int __res;
__asm__ __volatile__(
	"1:\tlodsb\n\t"
	"scasb\n\t"
	"jne 2f\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n\t"
	"xorl %%eax,%%eax\n\t"
	"jmp 3f\n"
	"2:\tsbbl %%eax,%%eax\n\t"
	"orb $1,%%al\n"
	"3:"
	:"=a" (__res), "=&S" (d0), "=&D" (d1)
	:"1" (cs),"2" (ct)
	:"memory");
return __res;
}

#define __HAVE_ARCH_STRNCMP
static inline int strncmp(const char * cs,const char * ct,size_t count)
{
register int __res;
int d0, d1, d2;
__asm__ __volatile__(
	"1:\tdecl %3\n\t"
	"js 2f\n\t"
	"lodsb\n\t"
	"scasb\n\t"
	"jne 3f\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n"
	"2:\txorl %%eax,%%eax\n\t"
	"jmp 4f\n"
	"3:\tsbbl %%eax,%%eax\n\t"
	"orb $1,%%al\n"
	"4:"
	:"=a" (__res), "=&S" (d0), "=&D" (d1), "=&c" (d2)
	:"1" (cs),"2" (ct),"3" (count)
	:"memory");
return __res;
}

#define __HAVE_ARCH_STRCHR
static inline char * strchr(const char * s, int c)
{
int d0;
register char * __res;
__asm__ __volatile__(
	"movb %%al,%%ah\n"
	"1:\tlodsb\n\t"
	"cmpb %%ah,%%al\n\t"
	"je 2f\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n\t"
	"movl $1,%1\n"
	"2:\tmovl %1,%0\n\t"
	"decl %0"
	:"=a" (__res), "=&S" (d0)
	:"1" (s),"0" (c)
	:"memory");
return __res;
}

#define __HAVE_ARCH_STRRCHR
static inline char * strrchr(const char * s, int c)
{
int d0, d1;
register char * __res;
__asm__ __volatile__(
	"movb %%al,%%ah\n"
	"1:\tlodsb\n\t"
	"cmpb %%ah,%%al\n\t"
	"jne 2f\n\t"
	"leal -1(%%esi),%0\n"
	"2:\ttestb %%al,%%al\n\t"
	"jne 1b"
	:"=g" (__res), "=&S" (d0), "=&a" (d1)
	:"0" (0),"1" (s),"2" (c)
	:"memory");
return __res;
}

#define __HAVE_ARCH_STRLEN
static inline size_t strlen(const char * s)
{
int d0;
register int __res;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"notl %0\n\t"
	"decl %0"
	:"=c" (__res), "=&D" (d0)
	:"1" (s),"a" (0), "0" (0xffffffffu)
	:"memory");
return __res;
}

static __always_inline void * __memcpy(void * to, const void * from, size_t n)
{
int d0, d1, d2;
__asm__ __volatile__(
	"rep ; movsl\n\t"
	"movl %4,%%ecx\n\t"
	"andl $3,%%ecx\n\t"
#if 1	/* want to pay 2 byte penalty for a chance to skip microcoded rep? */
	"jz 1f\n\t"
#endif
	"rep ; movsb\n\t"
	"1:"
	: "=&c" (d0), "=&D" (d1), "=&S" (d2)
	: "0" (n/4), "g" (n), "1" ((long) to), "2" ((long) from)
	: "memory");
return (to);
}

/*
 * This looks ugly, but the compiler can optimize it totally,
 * as the count is constant.
 */
static __always_inline void * __constant_memcpy(void * to, const void * from, size_t n)
{
	long esi, edi;
	if (!n) return to;
#if 1	/* want to do small copies with non-string ops? */
	switch (n) {
		case 1: *(char*)to = *(char*)from; return to;
		case 2: *(short*)to = *(short*)from; return to;
		case 4: *(int*)to = *(int*)from; return to;
#if 1	/* including those doable with two moves? */
		case 3: *(short*)to = *(short*)from;
			*((char*)to+2) = *((char*)from+2); return to;
		case 5: *(int*)to = *(int*)from;
			*((char*)to+4) = *((char*)from+4); return to;
		case 6: *(int*)to = *(int*)from;
			*((short*)to+2) = *((short*)from+2); return to;
		case 8: *(int*)to = *(int*)from;
			*((int*)to+1) = *((int*)from+1); return to;
#endif
	}
#endif
	esi = (long) from;
	edi = (long) to;
	if (n >= 5*4) {
		/* large block: use rep prefix */
		int ecx;
		__asm__ __volatile__(
			"rep ; movsl"
			: "=&c" (ecx), "=&D" (edi), "=&S" (esi)
			: "0" (n/4), "1" (edi),"2" (esi)
			: "memory"
		);
	} else {
		/* small block: don't clobber ecx + smaller code */
		if (n >= 4*4) __asm__ __volatile__("movsl"
			:"=&D"(edi),"=&S"(esi):"0"(edi),"1"(esi):"memory");
		if (n >= 3*4) __asm__ __volatile__("movsl"
			:"=&D"(edi),"=&S"(esi):"0"(edi),"1"(esi):"memory");
		if (n >= 2*4) __asm__ __volatile__("movsl"
			:"=&D"(edi),"=&S"(esi):"0"(edi),"1"(esi):"memory");
		if (n >= 1*4) __asm__ __volatile__("movsl"
			:"=&D"(edi),"=&S"(esi):"0"(edi),"1"(esi):"memory");
	}
	switch (n % 4) {
		/* tail */
		case 0: return to;
		case 1: __asm__ __volatile__("movsb"
			:"=&D"(edi),"=&S"(esi):"0"(edi),"1"(esi):"memory");
			return to;
		case 2: __asm__ __volatile__("movsw"
			:"=&D"(edi),"=&S"(esi):"0"(edi),"1"(esi):"memory");
			return to;
		default: __asm__ __volatile__("movsw\n\tmovsb"
			:"=&D"(edi),"=&S"(esi):"0"(edi),"1"(esi):"memory");
			return to;
	}
}

#define __HAVE_ARCH_MEMCPY

#ifdef CONFIG_X86_USE_3DNOW

#include <asm/mmx.h>

/*
 *	This CPU favours 3DNow strongly (eg AMD Athlon)
 */

static inline void * __constant_memcpy3d(void * to, const void * from, size_t len)
{
	if (len < 512)
		return __constant_memcpy(to, from, len);
	return _mmx_memcpy(to, from, len);
}

static __inline__ void *__memcpy3d(void *to, const void *from, size_t len)
{
	if (len < 512)
		return __memcpy(to, from, len);
	return _mmx_memcpy(to, from, len);
}

#define memcpy(t, f, n) \
(__builtin_constant_p(n) ? \
 __constant_memcpy3d((t),(f),(n)) : \
 __memcpy3d((t),(f),(n)))

#else

/*
 *	No 3D Now!
 */
 
#define memcpy(t, f, n) \
(__builtin_constant_p(n) ? \
 __constant_memcpy((t),(f),(n)) : \
 __memcpy((t),(f),(n)))

#endif

#define __HAVE_ARCH_MEMMOVE
void *memmove(void * dest,const void * src, size_t n);

#define memcmp __builtin_memcmp

#define __HAVE_ARCH_MEMCHR
static inline void * memchr(const void * cs,int c,size_t count)
{
int d0;
register void * __res;
if (!count)
	return NULL;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"je 1f\n\t"
	"movl $1,%0\n"
	"1:\tdecl %0"
	:"=D" (__res), "=&c" (d0)
	:"a" (c),"0" (cs),"1" (count)
	:"memory");
return __res;
}

static inline void * __memset_generic(void * s, char c,size_t count)
{
int d0, d1;
__asm__ __volatile__(
	"rep\n\t"
	"stosb"
	: "=&c" (d0), "=&D" (d1)
	:"a" (c),"1" (s),"0" (count)
	:"memory");
return s;
}

/* we might want to write optimized versions of these later */
#define __constant_count_memset(s,c,count) __memset_generic((s),(c),(count))

/*
 * memset(x,0,y) is a reasonably common thing to do, so we want to fill
 * things 32 bits at a time even when we don't know the size of the
 * area at compile-time..
 */
static __always_inline void * __constant_c_memset(void * s, unsigned long c, size_t count)
{
int d0, d1;
__asm__ __volatile__(
	"rep ; stosl\n\t"
	"testb $2,%b3\n\t"
	"je 1f\n\t"
	"stosw\n"
	"1:\ttestb $1,%b3\n\t"
	"je 2f\n\t"
	"stosb\n"
	"2:"
	:"=&c" (d0), "=&D" (d1)
	:"a" (c), "q" (count), "0" (count/4), "1" ((long) s)
	:"memory");
return (s);	
}

/* Added by Gertjan van Wingerde to make minix and sysv module work */
#define __HAVE_ARCH_STRNLEN
static inline size_t strnlen(const char * s, size_t count)
{
int d0;
register int __res;
__asm__ __volatile__(
	"movl %2,%0\n\t"
	"jmp 2f\n"
	"1:\tcmpb $0,(%0)\n\t"
	"je 3f\n\t"
	"incl %0\n"
	"2:\tdecl %1\n\t"
	"cmpl $-1,%1\n\t"
	"jne 1b\n"
	"3:\tsubl %2,%0"
	:"=a" (__res), "=&d" (d0)
	:"c" (s),"1" (count)
	:"memory");
return __res;
}
/* end of additional stuff */

#define __HAVE_ARCH_STRSTR

extern char *strstr(const char *cs, const char *ct);

/*
 * This looks horribly ugly, but the compiler can optimize it totally,
 * as we by now know that both pattern and count is constant..
 */
static __always_inline void * __constant_c_and_count_memset(void * s, unsigned long pattern, size_t count)
{
	switch (count) {
		case 0:
			return s;
		case 1:
			*(unsigned char *)s = pattern;
			return s;
		case 2:
			*(unsigned short *)s = pattern;
			return s;
		case 3:
			*(unsigned short *)s = pattern;
			*(2+(unsigned char *)s) = pattern;
			return s;
		case 4:
			*(unsigned long *)s = pattern;
			return s;
	}
#define COMMON(x) \
__asm__  __volatile__( \
	"rep ; stosl" \
	x \
	: "=&c" (d0), "=&D" (d1) \
	: "a" (pattern),"0" (count/4),"1" ((long) s) \
	: "memory")
{
	int d0, d1;
	switch (count % 4) {
		case 0: COMMON(""); return s;
		case 1: COMMON("\n\tstosb"); return s;
		case 2: COMMON("\n\tstosw"); return s;
		default: COMMON("\n\tstosw\n\tstosb"); return s;
	}
}
  
#undef COMMON
}

#define __constant_c_x_memset(s, c, count) \
(__builtin_constant_p(count) ? \
 __constant_c_and_count_memset((s),(c),(count)) : \
 __constant_c_memset((s),(c),(count)))

#define __memset(s, c, count) \
(__builtin_constant_p(count) ? \
 __constant_count_memset((s),(c),(count)) : \
 __memset_generic((s),(c),(count)))

#define __HAVE_ARCH_MEMSET
#define memset(s, c, count) \
(__builtin_constant_p(c) ? \
 __constant_c_x_memset((s),(0x01010101UL*(unsigned char)(c)),(count)) : \
 __memset((s),(c),(count)))

/*
 * find the first occurrence of byte 'c', or 1 past the area if none
 */
#define __HAVE_ARCH_MEMSCAN
static inline void * memscan(void * addr, int c, size_t size)
{
	if (!size)
		return addr;
	__asm__("repnz; scasb\n\t"
		"jnz 1f\n\t"
		"dec %%edi\n"
		"1:"
		: "=D" (addr), "=c" (size)
		: "0" (addr), "1" (size), "a" (c)
		: "memory");
	return addr;
}

#endif /* __KERNEL__ */

#endif
