#ifndef __BARRIER_H
#define __BARRIER_H

#define mb() \
__asm__ __volatile__("mb": : :"memory")

#define rmb() \
__asm__ __volatile__("mb": : :"memory")

#define wmb() \
__asm__ __volatile__("wmb": : :"memory")

#define read_barrier_depends() \
__asm__ __volatile__("mb": : :"memory")

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	barrier()
#endif

#define set_mb(var, value) \
do { var = value; mb(); } while (0)

#define set_wmb(var, value) \
do { var = value; wmb(); } while (0)

#endif		/* __BARRIER_H */
