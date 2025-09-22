/*	$OpenBSD: atomic.h,v 1.12 2019/10/28 09:41:37 visa Exp $	*/

/* Public Domain */

#ifndef _MIPS64_ATOMIC_H_
#define _MIPS64_ATOMIC_H_

#if defined(_KERNEL)

/* wait until the bits to set are clear, and set them */
static __inline void
atomic_wait_and_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int tmp0, tmp1;

	__asm__ volatile (
	"1:	ll	%0,	0(%2)\n"
	"	and	%1,	%0,	%3\n"
	"	bnez	%1,	1b\n"
	"	or	%0,	%3,	%0\n"
	"	sc	%0,	0(%2)\n"
	"	beqz	%0,	1b\n"
	"	 nop\n" :
		"=&r"(tmp0), "=&r"(tmp1) :
		"r"(uip), "r"(v) : "memory");
}

static __inline void
atomic_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int tmp;

	__asm__ volatile (
	"1:	ll	%0,	0(%1)\n"
	"	or	%0,	%2,	%0\n"
	"	sc	%0,	0(%1)\n"
	"	beqz	%0,	1b\n"
	"	 nop\n" :
		"=&r"(tmp) :
		"r"(uip), "r"(v) : "memory");
}

static __inline void
atomic_clearbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int tmp;

	__asm__ volatile (
	"1:	ll	%0,	0(%1)\n"
	"	and	%0,	%2,	%0\n"
	"	sc	%0,	0(%1)\n"
	"	beqz	%0,	1b\n"
	"	 nop\n" :
		"=&r"(tmp) :
		"r"(uip), "r"(~v) : "memory");
}

#endif /* defined(_KERNEL) */

static inline unsigned int
_atomic_cas_uint(volatile unsigned int *p, unsigned int o, unsigned int n)
{
	unsigned int rv, wv;

	__asm__ volatile (
	"1:	ll	%0,	%1\n"
	"	bne	%0,	%4,	2f\n"
	"	move	%2,	%3\n"
	"	sc	%2,	%1\n"
	"	beqz	%2,	1b\n"
	"2:	nop\n"
	    : "=&r" (rv), "+m" (*p), "=&r" (wv)
	    : "r" (n), "Ir" (o));

	return (rv);
}
#define atomic_cas_uint(_p, _o, _n) _atomic_cas_uint((_p), (_o), (_n))

static inline unsigned long
_atomic_cas_ulong(volatile unsigned long *p, unsigned long o, unsigned long n)
{
	unsigned long rv, wv;

	__asm__ volatile (
	"1:	lld	%0,	%1\n"
	"	bne	%0,	%4,	2f\n"
	"	move	%2,	%3\n"
	"	scd	%2,	%1\n"
	"	beqz	%2,	1b\n"
	"2:	nop\n"
	    : "=&r" (rv), "+m" (*p), "=&r" (wv)
	    : "r" (n), "Ir" (o));

	return (rv);
}
#define atomic_cas_ulong(_p, _o, _n) _atomic_cas_ulong((_p), (_o), (_n))

static inline void *
_atomic_cas_ptr(volatile void *pp, void *o, void *n)
{
	void * volatile *p = pp;
	void *rv, *wv;

	__asm__ volatile (
	"1:	lld	%0,	%1\n"
	"	bne	%0,	%4,	2f\n"
	"	move	%2,	%3\n"
	"	scd	%2,	%1\n"
	"	beqz	%2,	1b\n"
	"2:	nop\n"
	    : "=&r" (rv), "+m" (*p), "=&r" (wv)
	    : "r" (n), "Ir" (o));

	return (rv);
}
#define atomic_cas_ptr(_p, _o, _n) _atomic_cas_ptr((_p), (_o), (_n))



static inline unsigned int
_atomic_swap_uint(volatile unsigned int *uip, unsigned int v)
{
	unsigned int o, t;

	__asm__ volatile (
	"1:	ll	%0,	%1\n"
	"	move	%2,	%3\n"
	"	sc	%2,	%1\n"
	"	beqz	%2,	1b\n"
	"	nop\n" 
	    : "=&r" (o), "+m" (*uip), "=&r" (t)
	    : "r" (v));

	return (o);
}
#define atomic_swap_uint(_p, _v) _atomic_swap_uint((_p), (_v))

static inline unsigned long
_atomic_swap_ulong(volatile unsigned long *uip, unsigned long v)
{
	unsigned long o, t;

	__asm__ volatile (
	"1:	lld	%0,	%1\n"
	"	move	%2,	%3\n"
	"	scd	%2,	%1\n"
	"	beqz	%2,	1b\n"
	"	nop\n" 
	    : "=&r" (o), "+m" (*uip), "=&r" (t)
	    : "r" (v));

	return (o);
}
#define atomic_swap_ulong(_p, _v) _atomic_swap_ulong((_p), (_v))


static inline void *
_atomic_swap_ptr(volatile void *uipp, void *n)
{
	void * volatile *uip = uipp;
	void *o, *t;

	__asm__ volatile (
	"1:	lld	%0,	%1\n"
	"	move	%2,	%3\n"
	"	scd	%2,	%1\n"
	"	beqz	%2,	1b\n"
	"	nop\n"
	    : "=&r" (o), "+m" (*uip), "=&r" (t)
	    : "r" (n));

	return (o);
}
#define atomic_swap_ptr(_p, _n) _atomic_swap_ptr((_p), (_n))

static inline unsigned int
_atomic_add_int_nv(volatile unsigned int *uip, unsigned int v)
{
	unsigned int rv, nv;

	__asm__ volatile (
	"1:	ll	%0,	%1\n"
	"	addu	%2,	%0,	%3\n"
	"	sc	%2,	%1\n"
	"	beqz	%2,	1b\n"
	"	nop\n"
	    : "=&r" (rv), "+m" (*uip), "=&r" (nv)
	    : "Ir" (v));

	return (rv + v);
}
#define atomic_add_int_nv(_uip, _v) _atomic_add_int_nv((_uip), (_v))
#define atomic_sub_int_nv(_uip, _v) _atomic_add_int_nv((_uip), 0 - (_v))

static inline unsigned long
_atomic_add_long_nv(volatile unsigned long *uip, unsigned long v)
{
	unsigned long rv, nv;

	__asm__ volatile (
	"1:	lld	%0,	%1\n"
	"	daddu	%2,	%0,	%3\n"
	"	scd	%2,	%1\n"
	"	beqz	%2,	1b\n"
	"	nop\n"
	    : "=&r" (rv), "+m" (*uip), "=&r" (nv)
	    : "Ir" (v));

	return (rv + v);
}
#define atomic_add_long_nv(_uip, _v) _atomic_add_long_nv((_uip), (_v))
#define atomic_sub_long_nv(_uip, _v) _atomic_add_long_nv((_uip), 0UL - (_v))

#endif /* _MIPS64_ATOMIC_H_ */
