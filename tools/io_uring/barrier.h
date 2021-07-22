#ifndef LIBURING_BARRIER_H
#define LIBURING_BARRIER_H

#if defined(__x86_64) || defined(__i386__)
#define read_barrier()	__asm__ __volatile__("":::"memory")
#define write_barrier()	__asm__ __volatile__("":::"memory")
#else
/*
 * Add arch appropriate definitions. Be safe and use full barriers for
 * archs we don't have support for.
 */
#define read_barrier()	__sync_synchronize()
#define write_barrier()	__sync_synchronize()
#endif

#endif
