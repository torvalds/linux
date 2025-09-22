/*	$OpenBSD: atomic.h,v 1.12 2023/04/10 04:21:20 jsg Exp $	*/

/* Public Domain */

#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

#if defined(_KERNEL)

typedef volatile u_int __cpu_simple_lock_t __attribute__((__aligned__(16)));

#define __SIMPLELOCK_LOCKED	0
#define __SIMPLELOCK_UNLOCKED	1

static inline void
__cpu_simple_lock_init(__cpu_simple_lock_t *l)
{
	*l = __SIMPLELOCK_UNLOCKED;
}

static inline unsigned int
__cpu_simple_lock_ldcws(__cpu_simple_lock_t *l)
{
	unsigned int o;

	asm volatile("ldcws 0(%2), %0" : "=&r" (o), "+m" (l) : "r" (l));

	return (o);
}

static inline int
__cpu_simple_lock_try(__cpu_simple_lock_t *l)
{
	return (__cpu_simple_lock_ldcws(l) == __SIMPLELOCK_UNLOCKED);
}

static inline void
__cpu_simple_lock(__cpu_simple_lock_t *l)
{
	while (!__cpu_simple_lock_ldcws(l))
		;
}

static inline void
__cpu_simple_unlock(__cpu_simple_lock_t *l)
{
	*l = __SIMPLELOCK_UNLOCKED;
}

#ifdef MULTIPROCESSOR
extern __cpu_simple_lock_t atomic_lock;
#define ATOMIC_LOCK	__cpu_simple_lock(&atomic_lock);
#define ATOMIC_UNLOCK	__cpu_simple_unlock(&atomic_lock);
#else
#define ATOMIC_LOCK
#define ATOMIC_UNLOCK
#endif

static inline register_t
atomic_enter(void)
{
	register_t eiem;

	__asm volatile("mfctl	%%cr15, %0": "=r" (eiem));
	__asm volatile("mtctl	%r0, %cr15");
	ATOMIC_LOCK;

	return (eiem);
}

static inline void
atomic_leave(register_t eiem)
{
	ATOMIC_UNLOCK;
	__asm volatile("mtctl	%0, %%cr15":: "r" (eiem));
}

static inline unsigned int
_atomic_cas_uint(volatile unsigned int *uip, unsigned int o, unsigned int n)
{
	register_t eiem;
	unsigned int rv;

	eiem = atomic_enter();
	rv = *uip;
	if (rv == o)
		*uip = n;
	atomic_leave(eiem);

	return (rv);
}
#define atomic_cas_uint(_p, _o, _n) _atomic_cas_uint((_p), (_o), (_n))

static inline unsigned long
_atomic_cas_ulong(volatile unsigned long *uip, unsigned long o, unsigned long n)
{
	register_t eiem;
	unsigned long rv;

	eiem = atomic_enter();
	rv = *uip;
	if (rv == o)
		*uip = n;
	atomic_leave(eiem);

	return (rv);
}
#define atomic_cas_ulong(_p, _o, _n) _atomic_cas_ulong((_p), (_o), (_n))

static inline void *
_atomic_cas_ptr(volatile void *uip, void *o, void *n)
{
	register_t eiem;
	void * volatile *uipp = (void * volatile *)uip;
	void *rv;

	eiem = atomic_enter();
	rv = *uipp;
	if (rv == o)
		*uipp = n;
	atomic_leave(eiem);

	return (rv);
}
#define atomic_cas_ptr(_p, _o, _n) _atomic_cas_ptr((_p), (_o), (_n))

static inline unsigned int
_atomic_swap_uint(volatile unsigned int *uip, unsigned int n)
{
	register_t eiem;
	unsigned int rv;

	eiem = atomic_enter();
	rv = *uip;
	*uip = n;
	atomic_leave(eiem);

	return (rv);
}
#define atomic_swap_uint(_p, _n) _atomic_swap_uint((_p), (_n))

static inline unsigned long
_atomic_swap_ulong(volatile unsigned long *uip, unsigned long n)
{
	register_t eiem;
	unsigned long rv;

	eiem = atomic_enter();
	rv = *uip;
	*uip = n;
	atomic_leave(eiem);

	return (rv);
}
#define atomic_swap_ulong(_p, _n) _atomic_swap_ulong((_p), (_n))

static inline void *
_atomic_swap_ptr(volatile void *uip, void *n)
{
	register_t eiem;
	void * volatile *uipp = (void * volatile *)uip;
	void *rv;

	eiem = atomic_enter();
	rv = *uipp;
	*uipp = n;
	atomic_leave(eiem);

	return (rv);
}
#define atomic_swap_ptr(_p, _n) _atomic_swap_ptr((_p), (_n))

static __inline unsigned int
_atomic_add_int_nv(volatile unsigned int *uip, unsigned int v)
{
	register_t eiem;
	unsigned int rv;

	eiem = atomic_enter();
	rv = *uip + v;
	*uip = rv;
	atomic_leave(eiem);

	return (rv);
}
#define atomic_add_int_nv(_uip, _v) _atomic_add_int_nv((_uip), (_v))
#define atomic_sub_int_nv(_uip, _v) _atomic_add_int_nv((_uip), 0 - (_v))

static __inline unsigned long
_atomic_add_long_nv(volatile unsigned long *uip, unsigned long v)
{
	register_t eiem;
	unsigned long rv;

	eiem = atomic_enter();
	rv = *uip + v;
	*uip = rv;
	atomic_leave(eiem);

	return (rv);
}
#define atomic_add_long_nv(_uip, _v) _atomic_add_long_nv((_uip), (_v))
#define atomic_sub_long_nv(_uip, _v) _atomic_add_long_nv((_uip), 0 - (_v))

static __inline void
atomic_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	register_t eiem;

	eiem = atomic_enter();
	*uip |= v;
	atomic_leave(eiem);
}

static __inline void
atomic_clearbits_int(volatile unsigned int *uip, unsigned int v)
{
	register_t eiem;

	eiem = atomic_enter();
	*uip &= ~v;
	atomic_leave(eiem);
}

static __inline void
atomic_setbits_long(volatile unsigned long *uip, unsigned long v)
{
	register_t eiem;

	eiem = atomic_enter();
	*uip |= v;
	atomic_leave(eiem);
}

static __inline void
atomic_clearbits_long(volatile unsigned long *uip, unsigned long v)
{
	register_t eiem;

	eiem = atomic_enter();
	*uip &= ~v;
	atomic_leave(eiem);
}

#endif /* defined(_KERNEL) */

/*
 * Although the PA-RISC 2.0 architecture allows an implementation to
 * be weakly ordered, all PA-RISC processors to date implement a
 * strong memory ordering model.  So all we need is a compiler
 * barrier.
 */

static inline void
__insn_barrier(void)
{
	__asm volatile("" : : : "memory");
}

#define membar_enter()		__insn_barrier()
#define membar_exit()		__insn_barrier()
#define membar_producer()	__insn_barrier()
#define membar_consumer()	__insn_barrier()
#define membar_sync()		__insn_barrier()

#endif /* _MACHINE_ATOMIC_H_ */
