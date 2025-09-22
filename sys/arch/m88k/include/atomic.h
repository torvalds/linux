/*	$OpenBSD: atomic.h,v 1.18 2025/07/04 13:22:29 miod Exp $	*/

/* Public Domain */

#ifndef _M88K_ATOMIC_H_
#define _M88K_ATOMIC_H_

#if defined(_KERNEL)

#ifdef MULTIPROCESSOR

/* actual implementation is hairy, see atomic.S */
void		atomic_setbits_int(volatile unsigned int *, unsigned int);
void		atomic_clearbits_int(volatile unsigned int *, unsigned int);
unsigned int	atomic_add_int_nv_mp(volatile unsigned int *, unsigned int);
unsigned int	atomic_sub_int_nv_mp(volatile unsigned int *, unsigned int);
unsigned int	atomic_cas_uint_mp(volatile unsigned int *, unsigned int,
		     unsigned int);
unsigned int	atomic_swap_uint_mp(volatile unsigned int *, unsigned int);

#define	atomic_add_int_nv	atomic_add_int_nv_mp
#define	atomic_sub_int_nv	atomic_sub_int_nv_mp
#define	atomic_cas_uint		atomic_cas_uint_mp
#define	atomic_swap_uint	atomic_swap_uint_mp

#else

#include <machine/asm_macro.h>
#include <machine/psl.h>

static __inline void
atomic_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	u_int psr;

	psr = get_psr();
	set_psr(psr | PSR_IND);
	*uip |= v;
	set_psr(psr);
}

static __inline void
atomic_clearbits_int(volatile unsigned int *uip, unsigned int v)
{
	u_int psr;

	psr = get_psr();
	set_psr(psr | PSR_IND);
	*uip &= ~v;
	set_psr(psr);
}

static __inline unsigned int
atomic_add_int_nv_sp(volatile unsigned int *uip, unsigned int v)
{
	u_int psr;
	unsigned int nv;

	psr = get_psr();
	set_psr(psr | PSR_IND);
	*uip += v;
	nv = *uip;
	set_psr(psr);

	return nv;
}

static __inline unsigned int
atomic_sub_int_nv_sp(volatile unsigned int *uip, unsigned int v)
{
	u_int psr;
	unsigned int nv;

	psr = get_psr();
	set_psr(psr | PSR_IND);
	*uip -= v;
	nv = *uip;
	set_psr(psr);

	return nv;
}

static inline unsigned int
atomic_cas_uint_sp(volatile unsigned int *p, unsigned int o, unsigned int n)
{
	u_int psr;
	unsigned int ov;

	psr = get_psr();
	set_psr(psr | PSR_IND);
	ov = *p;
	if (ov == o)
		*p = n;
	set_psr(psr);

	return ov;
}

static inline unsigned int
atomic_swap_uint_sp(volatile unsigned int *p, unsigned int v)
{
	u_int psr;
	unsigned int ov;

	psr = get_psr();
	set_psr(psr | PSR_IND);
	ov = *p;
	*p = v;
	set_psr(psr);

	return ov;
}

#define	atomic_add_int_nv	atomic_add_int_nv_sp
#define	atomic_sub_int_nv	atomic_sub_int_nv_sp
#define	atomic_cas_uint		atomic_cas_uint_sp
#define	atomic_swap_uint	atomic_swap_uint_sp

#endif	/* MULTIPROCESSOR */

static __inline__ unsigned int
atomic_clear_int(volatile unsigned int *uip)
{
	u_int oldval;

	oldval = 0;
	__asm__ volatile
	    ("xmem %0, %2, %%r0" : "+r"(oldval), "+m"(*uip) : "r"(uip));
	return oldval;
}

#define	atomic_add_long_nv(p,v) \
	((unsigned long)atomic_add_int_nv((unsigned int *)p, (unsigned int)v))
#define	atomic_sub_long_nv(p,v) \
	((unsigned long)atomic_sub_int_nv((unsigned int *)p, (unsigned int)v))

#define	atomic_cas_ulong(p,o,n) \
	((unsigned long)atomic_cas_uint((unsigned int *)p, (unsigned int)o, \
	 (unsigned int)n))
#define	atomic_cas_ptr(p,o,n) \
	((void *)atomic_cas_uint((void *)p, (unsigned int)o, (unsigned int)n))

#define	atomic_swap_ulong(p,o) \
	((unsigned long)atomic_swap_uint((unsigned int *)p, (unsigned int)o)
#define	atomic_swap_ptr(p,o) \
	((void *)atomic_swap_uint((void *)p, (unsigned int)o))

static inline void
__sync_synchronize(void)
{
	/* flush_pipeline(); */
	__asm__ volatile ("tb1 0, %%r0, 0" ::: "memory");
}

#else /* _KERNEL */

#if !defined(__GNUC__) || (__GNUC__ < 4)

/*
 * Atomic routines are not available to userland, but we need to prevent
 * <sys/atomic.h> from declaring them as inline wrappers of __sync_* functions,
 * which are not available with gcc 3.
 */

#define	atomic_cas_uint		UNIMPLEMENTED
#define	atomic_cas_ulong	UNIMPLEMENTED
#define	atomic_cas_ptr		UNIMPLEMENTED

#define	atomic_swap_uint	UNIMPLEMENTED
#define	atomic_swap_ulong	UNIMPLEMENTED
#define	atomic_swap_ptr		UNIMPLEMENTED

#define	atomic_add_int_nv	UNIMPLEMENTED
#define	atomic_add_long_nv	UNIMPLEMENTED
#define	atomic_add_int		UNIMPLEMENTED
#define	atomic_add_long		UNIMPLEMENTED

#define	atomic_inc_int		UNIMPLEMENTED
#define	atomic_inc_long		UNIMPLEMENTED

#define	atomic_sub_int_nv	UNIMPLEMENTED
#define	atomic_sub_long_nv	UNIMPLEMENTED
#define	atomic_sub_int		UNIMPLEMENTED
#define	atomic_sub_long		UNIMPLEMENTED

#define	atomic_dec_int		UNIMPLEMENTED
#define	atomic_dec_long		UNIMPLEMENTED

#endif	/* gcc < 4 */

/* trap numbers below 128 would cause a privileged instruction fault */
#define	__membar() do {						\
	__asm volatile("tb1 0, %%r0, 128" ::: "memory");	\
} while (0)

#define	membar_enter()		__membar()
#define	membar_exit()		__membar()
#define	membar_producer()	__membar()
#define	membar_consumer()	__membar()
#define	membar_sync()		__membar()

#endif /* defined(_KERNEL) */

#endif /* _M88K_ATOMIC_H_ */
