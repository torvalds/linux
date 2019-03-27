/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * bsd specific headers common to the driver
 */

#ifndef _OCS_OS_H
#define _OCS_OS_H

/***************************************************************************
 * OS specific includes
 */
#include "opt_stack.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/endian.h>
#include <sys/stddef.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <sys/bitstring.h>
#include <sys/stack.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/stdarg.h>

#include <dev/pci/pcivar.h>

#include <sys/sema.h>
#include <sys/time.h>

#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/unistd.h>
#include <sys/sched.h>

#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/ioccom.h>
#include <sys/ctype.h>

#include <sys/linker.h>		/* for debug of memory allocations */

/* OCS_OS_MAX_ISR_TIME_MSEC -  maximum time driver code should spend in an interrupt
 * or kernel thread context without yielding
 */
#define OCS_OS_MAX_ISR_TIME_MSEC	1000

/* BSD driver specific definitions */

#define ARRAY_SIZE(x)   (sizeof(x) / sizeof((x)[0]))

#define OCS_MAX_LUN			512
#define OCS_NUM_UNSOLICITED_FRAMES	1024

#define OCS_MAX_DOMAINS			1
#define OCS_MAX_REMOTE_NODES		2048
#define OCS_MAX_TARGETS			1024
#define OCS_MAX_INITIATORS		1024
/** Reserve this number of IO for each intiator to return FULL/BUSY status */
#define OCS_RSVD_INI_IO			8

#define OCS_MIN_DMA_ALIGNMENT		16
#define OCS_MAX_DMA_ALLOC		(64*1024)	/* maxium DMA allocation that is expected to reliably succeed  */

/*
 * Macros used to size the CQ hash table. We want to round up to the next
 * power of 2 for the hash.
 */
#define B2(x)   (   (x) | (   (x) >> 1) )
#define B4(x)   ( B2(x) | ( B2(x) >> 2) )
#define B8(x)   ( B4(x) | ( B4(x) >> 4) )
#define B16(x)  ( B8(x) | ( B8(x) >> 8) )
#define B32(x)  (B16(x) | (B16(x) >>16) )
#define B32_NEXT_POWER_OF_2(x)      (B32((x)-1) + 1)

/*
 * likely/unlikely - branch prediction hint
 */
#define likely(x)               __builtin_expect(!!(x), 1)
#define unlikely(x)             __builtin_expect(!!(x), 0)

/***************************************************************************
 * OS abstraction
 */

/**
 * @brief Min/Max macros
 *
 */
#define OCS_MAX(x, y)		((x) > (y) ? (x) : (y))
#define OCS_MIN(x, y)		((x) < (y) ? (x) : (y))

#define PRIX64	"lX"
#define PRIx64	"lx"
#define PRId64	"ld"
#define PRIu64	"lu"

/**
 * Enable optional features
 *  - OCS_INCLUDE_DEBUG include low-level SLI debug support
 */
#define OCS_INCLUDE_DEBUG

/**
 * @brief Set the Nth bit
 *
 * @todo move to a private file used internally?
 */
#ifndef BIT
#define BIT(n)		(1U << (n))
#endif

/***************************************************************************
 * Platform specific operations
 */

typedef struct ocs_softc ocs_t;

/**
 * @ingroup os
 * @typedef ocs_os_handle_t
 * @brief OS specific handle or driver context
 *
 * This can be anything from a void * to some other OS specific type. The lower
 * layers make no assumption about its value and pass it back as the first
 * parameter to most OS functions.
 */
typedef ocs_t * ocs_os_handle_t;

/**
 * @ingroup os
 * @brief return the lower 32-bits of a bus address
 *
 * @param addr Physical or bus address to convert
 * @return lower 32-bits of a bus address
 *
 * @note this may be a good cadidate for an inline or macro
 */
static inline uint32_t ocs_addr32_lo(uintptr_t addr)
{
#if defined(__LP64__)
	return (uint32_t)(addr & 0xffffffffUL);
#else
	return addr;
#endif
}

/**
 * @ingroup os
 * @brief return the upper 32-bits of a bus address
 *
 * @param addr Physical or bus address to convert
 * @return upper 32-bits of a bus address
 *
 * @note this may be a good cadidate for an inline or macro
 */
static inline uint32_t ocs_addr32_hi(uintptr_t addr)
{
#if defined(__LP64__)
	return (uint32_t)(addr >> 32);
#else
	return 0;
#endif
}

/**
 * @ingroup os
 * @brief return the log2(val)
 *
 * @param val number to use (assumed to be exact power of 2)
 *
 * @return log base 2 of val
 */
static inline uint32_t ocs_lg2(uint32_t val)
{
#if defined(__GNUC__)
	/*
	 * clz = "count leading zero's"
	 *
	 * Assuming val is an exact power of 2, the most significant bit
	 * will be the log base 2 of val
	 */
	return 31 - __builtin_clz(val);
#else
#error You need to provide a non-GCC version of this function
#endif
}

/**
 * @ingroup os
 * @brief optimization barrier
 *
 * Optimization barrier. Prevents compiler re-ordering
 * instructions across barrier.
 *
 * @return none
 */
#define ocs_barrier()	 __asm __volatile("" : : : "memory");

/**
 * @ingroup os
 * @brief convert a big endian 32 bit value to the host's native format
 *
 * @param val 32 bit big endian value
 *
 * @return value converted to the host's native endianness
 */
#define ocs_be32toh(val)	be32toh(val)

/**
 * @ingroup os
 * @brief convert a 32 bit value from the host's native format to big endian
 *
 * @param val 32 bit native endian value
 *
 * @return value converted to big endian
 */
#define ocs_htobe32(val)	htobe32(val)

/**
 * @ingroup os
 * @brief convert a 16 bit value from the host's native format to big endian
 *
 * @param v 16 bit native endian value
 *
 * @return value converted to big endian
 */
#define ocs_htobe16(v)	htobe16(v)
#define ocs_be16toh(v)	be16toh(v)


#define ocs_htobe64(v)	htobe64(v)
#define ocs_be64toh(v)	be64toh(v)

/**
 * @ingroup os
 * @brief Delay execution by the given number of micro-seconds
 *
 * @param usec number of micro-seconds to "busy-wait"
 *
 * @note The value of usec may be greater than 1,000,000
 */
#define ocs_udelay(usec) DELAY(usec)

/**
 * @ingroup os
 * @brief Delay execution by the given number of milli-seconds
 *
 * @param msec number of milli-seconds to "busy-wait"
 *
 * @note The value of usec may be greater than 1,000,000
 */
#define ocs_msleep(msec) ocs_udelay((msec)*1000)

/**
 * @ingroup os
 * @brief Get time of day in msec
 *
 * @return time of day in msec
 */
static inline time_t
ocs_msectime(void)
{
	struct timeval tv;

	getmicrotime(&tv);
	return (tv.tv_sec*1000) + (tv.tv_usec / 1000);
}

/**
 * @ingroup os
 * @brief Copy length number of bytes from the source to destination address
 *
 * @param d pointer to the destination memory
 * @param s pointer to the source memory
 * @param l number of bytes to copy
 *
 * @return original value of dst pointer
 */
#define ocs_memcpy(d, s, l)		memcpy(d, s, l)

#define ocs_strlen(s)			strlen(s)
#define ocs_strcpy(d,s)			strcpy(d, s)
#define ocs_strncpy(d,s, n)		strncpy(d, s, n)
#define ocs_strcat(d, s)		strcat(d, s)
#define ocs_strtoul(s,ep,b)		strtoul(s,ep,b)
#define ocs_strtoull(s,ep,b)		((uint64_t)strtouq(s,ep,b))
#define ocs_atoi(s)			strtol(s, 0, 0)
#define ocs_strcmp(d,s)			strcmp(d,s)
#define ocs_strcasecmp(d,s)		strcasecmp(d,s)
#define ocs_strncmp(d,s,n)		strncmp(d,s,n)
#define ocs_strstr(h,n)			strstr(h,n)
#define ocs_strsep(h, n)		strsep(h, n)
#define ocs_strchr(s,c)			strchr(s,c)
#define ocs_copy_from_user(dst, src, n)	copyin(src, dst, n)
#define ocs_copy_to_user(dst, src, n)	copyout(src, dst, n)
#define ocs_snprintf(buf, n, fmt, ...)	snprintf(buf, n, fmt, ##__VA_ARGS__)
#define ocs_vsnprintf(buf, n, fmt, ap)	vsnprintf((char*)buf, n, fmt, ap)
#define ocs_sscanf(buf,fmt, ...)	sscanf(buf, fmt, ##__VA_ARGS__)
#define ocs_printf			printf
#define ocs_isspace(c)			isspace(c)
#define ocs_isdigit(c)			isdigit(c)
#define ocs_isxdigit(c)			isxdigit(c)

extern uint64_t ocs_get_tsc(void);
extern void *ocs_ioctl_preprocess(ocs_os_handle_t os, void *arg, size_t size);
extern int32_t ocs_ioctl_postprocess(ocs_os_handle_t os, void *arg, void *kern_ptr, size_t size);
extern void ocs_ioctl_free(ocs_os_handle_t os, void *kern_ptr, size_t size);
extern char *ocs_strdup(const char *s);

/**
 * @ingroup os
 * @brief Set the value of each byte in memory
 *
 * @param b pointer to the memory
 * @param c value used to set memory
 * @param l number of bytes to set
 *
 * @return original value of mem pointer
 */
#define ocs_memset(b, c, l) memset(b, c, l)

#define LOG_CRIT	0
#define LOG_ERR		1
#define LOG_WARN	2
#define LOG_INFO	3
#define LOG_TEST	4
#define LOG_DEBUG	5

extern int loglevel;

extern void _ocs_log(ocs_t *ocs, const char *func, int line, const char *fmt, ...);

#define ocs_log_crit(os, fmt, ...)	ocs_log(os, LOG_CRIT, fmt, ##__VA_ARGS__);
#define ocs_log_err(os, fmt, ...)	ocs_log(os, LOG_ERR, fmt, ##__VA_ARGS__);
#define ocs_log_warn(os, fmt, ...)	ocs_log(os, LOG_WARN, fmt, ##__VA_ARGS__);
#define ocs_log_info(os, fmt, ...)	ocs_log(os, LOG_INFO, fmt, ##__VA_ARGS__);
#define ocs_log_test(os, fmt, ...)	ocs_log(os, LOG_TEST, fmt, ##__VA_ARGS__);
#define ocs_log_debug(os, fmt, ...)	ocs_log(os, LOG_DEBUG, fmt, ##__VA_ARGS__);

#define ocs_log(os, level, fmt, ...)                    \
	do {                                            \
		if (level <= loglevel) {                \
			_ocs_log(os, __func__, __LINE__, fmt, ##__VA_ARGS__);   \
		}                                       \
	} while (0)

static inline uint32_t ocs_roundup(uint32_t x, uint32_t y)
{
	return (((x + y - 1) / y) * y);
}

static inline uint32_t ocs_rounddown(uint32_t x, uint32_t y)
{
	return ((x / y) * y);
}

/***************************************************************************
 * Memory allocation interfaces
 */

#define OCS_M_ZERO	M_ZERO
#define OCS_M_NOWAIT	M_NOWAIT

/**
 * @ingroup os
 * @brief Allocate host memory
 *
 * @param os OS handle
 * @param size number of bytes to allocate
 * @param flags additional options
 *
 * Flags include
 *  - OCS_M_ZERO zero memory after allocating
 *  - OCS_M_NOWAIT do not block/sleep waiting for an allocation request
 *
 * @return pointer to allocated memory, NULL otherwise
 */
extern void *ocs_malloc(ocs_os_handle_t os, size_t size, int32_t flags);

/**
 * @ingroup os
 * @brief Free host memory
 *
 * @param os OS handle
 * @param addr pointer to memory
 * @param size bytes to free
 */
extern void ocs_free(ocs_os_handle_t os, void *addr, size_t size);

/**
 * @ingroup os
 * @brief generic DMA memory descriptor for driver allocations
 *
 * Memory regions ultimately used by the hardware are described using
 * this structure. All implementations must include the structure members
 * defined in the first section, and they may also add their own structure
 * members in the second section.
 *
 * Note that each region described by ocs_dma_s is assumed to be physically
 * contiguous.
 */
typedef struct ocs_dma_s {
	/*
	 * OCS layer requires the following members
	 */
	void		*virt;	/**< virtual address of the memory used by the CPU */
	void		*alloc;	/**< originally allocated virtual address used to restore virt if modified */
	uintptr_t	phys;	/**< physical or bus address of the memory used by the hardware */
	size_t		size;	/**< size in bytes of the memory */
	/*
	 * Implementation specific fields allowed here
	 */
	size_t		len;	/**< application specific length */
	bus_dma_tag_t	tag;
	bus_dmamap_t	map;
} ocs_dma_t;

/**
 * @ingroup os
 * @brief Returns maximum supported DMA allocation size
 *
 * @param os OS specific handle or driver context
 * @param align alignment requirement for DMA allocation
 *
 * Return maximum supported DMA allocation size, given alignment
 * requirement.
 *
 * @return maxiumum supported DMA allocation size
 */
static inline uint32_t ocs_max_dma_alloc(ocs_os_handle_t os, size_t align)
{
	return ~((uint32_t)0); /* no max */
}

/**
 * @ingroup os
 * @brief Allocate a DMA capable block of memory
 *
 * @param os OS specific handle or driver context
 * @param dma DMA descriptor containing results of memory allocation
 * @param size Size in bytes of desired allocation
 * @param align Alignment in bytes of the requested allocation
 *
 * @return 0 on success, non-zero otherwise
 */
extern int32_t ocs_dma_alloc(ocs_os_handle_t, ocs_dma_t *, size_t, size_t);

/**
 * @ingroup os
 * @brief Free a DMA capable block of memory
 *
 * @param os OS specific handle or driver context
 * @param dma DMA descriptor for memory to be freed
 *
 * @return 0 if memory is de-allocated, non-zero otherwise
 */
extern int32_t ocs_dma_free(ocs_os_handle_t, ocs_dma_t *);
extern int32_t ocs_dma_copy_in(ocs_dma_t *dma, void *buffer, uint32_t buffer_length);
extern int32_t ocs_dma_copy_out(ocs_dma_t *dma, void *buffer, uint32_t buffer_length);

static inline int32_t ocs_dma_valid(ocs_dma_t *dma)
{
	return (dma->size != 0);
}

/**
 * @ingroup os
 * @brief Synchronize the DMA buffer memory
 *
 * Ensures memory coherency between the CPU and device
 *
 * @param dma DMA descriptor of memory to synchronize
 * @param flags Describes direction of synchronization
 *   - OCS_DMASYNC_PREREAD sync needed before hardware updates host memory
 *   - OCS_DMASYNC_PREWRITE sync needed after CPU updates host memory but before hardware can access
 *   - OCS_DMASYNC_POSTREAD sync needed after hardware updates host memory but before CPU can access
 *   - OCS_DMASYNC_POSTWRITE sync needed after hardware updates host memory
 */
extern void ocs_dma_sync(ocs_dma_t *, uint32_t);

#define OCS_DMASYNC_PREWRITE BUS_DMASYNC_PREWRITE
#define OCS_DMASYNC_POSTREAD BUS_DMASYNC_POSTREAD


/***************************************************************************
 * Locking
 */

/**
 * @ingroup os
 * @typedef ocs_lock_t
 * @brief Define the type used implement locking
 */
#define MAX_LOCK_DESC_LEN	64
typedef struct ocs_lock_s {
	struct	mtx lock;
	char	name[MAX_LOCK_DESC_LEN];
} ocs_lock_t;

/**
 * @ingroup os
 * @brief Initialize a lock
 *
 * @param lock lock to initialize
 * @param name string identifier for the lock
 */
extern void ocs_lock_init(void *os, ocs_lock_t *lock, const char *name, ...);

/**
 * @ingroup os
 * @brief Free a previously allocated lock
 *
 * @param lock lock to free
 */
static inline void
ocs_lock_free(ocs_lock_t *lock)
{

	if (mtx_initialized(&(lock)->lock)) {
		mtx_assert(&(lock)->lock, MA_NOTOWNED);
		mtx_destroy(&(lock)->lock);
	} else {
		panic("XXX trying to free with un-initialized mtx!?!?\n");
	}
}

/**
 * @ingroup os
 * @brief Acquire a lock
 *
 * @param lock lock to obtain
 */
static inline void
ocs_lock(ocs_lock_t *lock)
{

	if (mtx_initialized(&(lock)->lock)) {
		mtx_assert(&(lock)->lock, MA_NOTOWNED);
		mtx_lock(&(lock)->lock);
	} else {
		panic("XXX trying to lock with un-initialized mtx!?!?\n");
	}
}

/**
 * @ingroup os
 * @brief Release a lock
 *
 * @param lock lock to release
 */
static inline void
ocs_unlock(ocs_lock_t *lock)
{

	if (mtx_initialized(&(lock)->lock)) {
		mtx_assert(&(lock)->lock, MA_OWNED | MA_NOTRECURSED);
		mtx_unlock(&(lock)->lock);
	} else {
		panic("XXX trying to unlock with un-initialized mtx!?!?\n");
	}
}

/**
 * @ingroup os
 * @typedef ocs_lock_t
 * @brief Define the type used implement recursive locking
 */
typedef struct ocs_lock_s ocs_rlock_t;

/**
 * @ingroup os
 * @brief Initialize a recursive lock
 *
 * @param ocs pointer to ocs structure
 * @param lock lock to initialize
 * @param name string identifier for the lock
 */
static inline void
ocs_rlock_init(ocs_t *ocs, ocs_rlock_t *lock, const char *name)
{
	ocs_strncpy(lock->name, name, MAX_LOCK_DESC_LEN);
	mtx_init(&(lock)->lock, lock->name, NULL, MTX_DEF | MTX_RECURSE | MTX_DUPOK);
}

/**
 * @ingroup os
 * @brief Free a previously allocated recursive lock
 *
 * @param lock lock to free
 */
static inline void
ocs_rlock_free(ocs_rlock_t *lock)
{
	if (mtx_initialized(&(lock)->lock)) {
		mtx_destroy(&(lock)->lock);
	} else {
		panic("XXX trying to free with un-initialized mtx!?!?\n");
	}
}

/**
 * @brief try to acquire a recursive lock
 *
 * Attempt to acquire a recursive lock, return TRUE if successful
 *
 * @param lock pointer to recursive lock
 *
 * @return TRUE if lock was acquired, FALSE if not
 */
static inline int32_t
ocs_rlock_try(ocs_rlock_t *lock)
{
	int rc = mtx_trylock(&(lock)->lock);

	return rc != 0;
}

/**
 * @ingroup os
 * @brief Acquire a recursive lock
 *
 * @param lock lock to obtain
 */
static inline void
ocs_rlock_acquire(ocs_rlock_t *lock)
{
	if (mtx_initialized(&(lock)->lock)) {
		mtx_lock(&(lock)->lock);
	} else {
		panic("XXX trying to lock with un-initialized mtx!?!?\n");
	}
}

/**
 * @ingroup os
 * @brief Release a recursive lock
 *
 * @param lock lock to release
 */
static inline void
ocs_rlock_release(ocs_rlock_t *lock)
{
	if (mtx_initialized(&(lock)->lock)) {
		mtx_assert(&(lock)->lock, MA_OWNED);
		mtx_unlock(&(lock)->lock);
	} else {
		panic("XXX trying to unlock with un-initialized mtx!?!?\n");
	}
}

/**
 * @brief counting semaphore
 *
 * Declaration of the counting semaphore object
 *
 */
typedef struct {
	char name[32];
	struct sema sem;		/**< OS counting semaphore structure */
} ocs_sem_t;

#define OCS_SEM_FOREVER		(-1)
#define OCS_SEM_TRY		(0)

/**
 * @brief Initialize a counting semaphore
 *
 * The semaphore is initiatlized to the value
 *
 * @param sem pointer to semaphore
 * @param val initial value
 * @param name label for the semaphore
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

extern int ocs_sem_init(ocs_sem_t *sem, int val, const char *name, ...) __attribute__((format(printf, 3, 4)));

/**
 * @brief execute a P (decrement) operation
 *
 * A P (decrement and block if negative) operation is performed on the semaphore.
 *
 * If timeout_usec is zero, the semaphore attempts one time and returns 0 if acquired.
 * If timeout_usec is greater than zero, then the call will block until the semaphore
 * is acquired, or a timeout occurred.  If timeout_usec is less than zero, then
 * the call will block until the semaphore is acquired.
 *
 * @param sem pointer to semaphore
 * @param timeout_usec timeout in microseconds
 *
 * @return returns 0 for success, negative value if the semaphore was not acquired.
 */

static inline int
ocs_sem_p(ocs_sem_t *sem, int timeout_usec)
{
	int32_t rc = 0;

	if (timeout_usec == 0) {
		rc = sema_trywait(&sem->sem);
		if (rc == 0) {
			rc = -1;
		}
	} else if (timeout_usec > 0) {
		struct timeval tv;
		uint32_t ticks;

		tv.tv_sec = timeout_usec / 1000000;
		tv.tv_usec = timeout_usec % 1000000;
		ticks = tvtohz(&tv);
		if (ticks == 0) {
			ticks ++;
		}
		rc = sema_timedwait(&sem->sem, ticks);
		if (rc != 0) {
			rc = -1;
		}
	} else {
		sema_wait(&sem->sem);
	}
	if (rc)
		rc = -1;

	return rc;
}

/**
 * @brief perform a V (increment) operation on a counting semaphore
 *
 * The semaphore is incremented, unblocking one thread that is waiting on the
 * sempahore
 *
 * @param sem pointer to the semaphore
 *
 * @return none
 */

static inline void
ocs_sem_v(ocs_sem_t *sem)
{
	sema_post(&sem->sem);
}

/***************************************************************************
 * Bitmap
 */

/**
 * @ingroup os
 * @typedef ocs_bitmap_t
 * @brief Define the type used implement bit-maps
 */
typedef bitstr_t ocs_bitmap_t;

/**
 * @ingroup os
 * @brief Allocate a bitmap
 *
 * @param n_bits Minimum number of entries in the bit-map
 *
 * @return pointer to the bit-map or NULL on error
 */
extern ocs_bitmap_t *ocs_bitmap_alloc(uint32_t n_bits);

/**
 * @ingroup os
 * @brief Free a bit-map
 *
 * @param bitmap Bit-map to free
 */
extern void ocs_bitmap_free(ocs_bitmap_t *bitmap);

/**
 * @ingroup os
 * @brief Find next unset bit and set it
 *
 * @param bitmap bit map to search
 * @param n_bits number of bits in map
 *
 * @return bit position or -1 if map is full
 */
extern int32_t ocs_bitmap_find(ocs_bitmap_t *bitmap, uint32_t n_bits);

/**
 * @ingroup os
 * @brief search for next (un)set bit
 *
 * @param bitmap bit map to search
 * @param set search for a set or unset bit
 * @param n_bits number of bits in map
 *
 * @return bit position or -1
 */
extern int32_t ocs_bitmap_search(ocs_bitmap_t *bitmap, uint8_t set, uint32_t n_bits);

/**
 * @ingroup os
 * @brief clear the specified bit
 *
 * @param bitmap pointer to bit map
 * @param bit bit number to clear
 */
extern void ocs_bitmap_clear(ocs_bitmap_t *bitmap, uint32_t bit);

extern int32_t ocs_get_property(const char *prop_name, char *buffer, uint32_t buffer_len);

/***************************************************************************
 * Timer Routines
 *
 * Functions for setting, querying and canceling timers.
 */
typedef struct {
	struct callout	callout;
	struct mtx	lock;

	void	(*func)(void *);
	void	*data;
} ocs_timer_t;

/**
 * @ingroup os
 * @brief Initialize and set a timer
 *
 * @param os OS handle
 * @param timer    pointer to the structure allocated for this timer
 * @param func     the function to call when the timer expires
 * @param data     Data to pass to the provided timer function when the timer
 *                 expires.
 * @param timeout_ms the timeout in milliseconds
 */
extern int32_t ocs_setup_timer(ocs_os_handle_t os, ocs_timer_t *timer, void(*func)(void *arg),
			       void *data, uint32_t timeout_ms);

/**
 * @ingroup os
 * @brief Modify a timer's expiration
 *
 * @param timer    pointer to the structure allocated for this timer
 * @param timeout_ms the timeout in milliseconds
 */
extern int32_t ocs_mod_timer(ocs_timer_t *timer, uint32_t timeout_ms);

/**
 * @ingroup os
 * @brief Queries to see if a timer is pending.
 *
 * @param timer    pointer to the structure allocated for this timer
 *
 * @return non-zero if the timer is pending
 */
extern int32_t ocs_timer_pending(ocs_timer_t *timer);

/**
 * @ingroup os
 * @brief Remove a pending timer
 *
 * @param timer    pointer to the structure allocated for this timer
 *                 expires.
 */
extern int32_t ocs_del_timer(ocs_timer_t *timer);

/***************************************************************************
 * Atomics
 *
 */

typedef uint32_t ocs_atomic_t;

/**
 * @ingroup os
 * @brief initialize an atomic
 *
 * @param a    pointer to the atomic object
 * @param v    initial value
 *
 * @return none
 */
#define ocs_atomic_init(a, v)	ocs_atomic_set(a, v)

/**
 * @ingroup os
 * @brief adds an integer to an atomic value
 *
 * @param a    pointer to the atomic object
 * @param v    value to increment
 *
 * @return the value of the atomic before incrementing.
 */
#define ocs_atomic_add_return(a, v)	atomic_fetchadd_32(a, v)

/**
 * @ingroup os
 * @brief subtracts an integer to an atomic value
 *
 * @param a    pointer to the atomic object
 * @param v    value to increment
 *
 * @return the value of the atomic before subtracting.
 */
#define ocs_atomic_sub_return(a, v)     atomic_fetchadd_32(a, (-(v)))

/**
 * @ingroup os
 * @brief returns the current value of an atomic object
 *
 * @param a    pointer to the atomic object
 *
 * @return the value of the atomic.
 */
#define ocs_atomic_read(a)		atomic_load_acq_32(a)

/**
 * @ingroup os
 * @brief sets the current value of an atomic object
 *
 * @param a    pointer to the atomic object
 */
#define ocs_atomic_set(a, v)		atomic_store_rel_32(a, v)

/**
 * @ingroup os
 * @brief Sets atomic to 0, returns previous value
 *
 * @param a    pointer to the atomic object
 *
 * @return the value of the atomic before the operation.
 */
#define ocs_atomic_read_and_clear	atomic_readandclear_32(a)

/**
 * @brief OCS thread structure
 *
 */

typedef struct ocs_thread_s ocs_thread_t;

typedef int32_t (*ocs_thread_fctn)(ocs_thread_t *mythread);

struct ocs_thread_s  {
	struct thread *tcb;			/*<< thread control block */
	ocs_thread_fctn fctn;			/*<< thread function */
	char *name;				/*<< name of thread */
	void *arg;				/*<< pointer to thread argument */
	ocs_atomic_t terminate;			/*<< terminate request */
	int32_t retval;				/*<< return value */
	uint32_t cpu_affinity;			/*<< cpu affinity */
};
#define OCS_THREAD_DEFAULT_STACK_SIZE_PAGES	8

/**
 * @brief OCS thread start options
 *
 */

typedef enum {
	OCS_THREAD_RUN,				/*<< run immediately */
	OCS_THREAD_CREATE,			/*<< create and wait for start request */
} ocs_thread_start_e;


extern int32_t ocs_thread_create(ocs_os_handle_t os, ocs_thread_t *thread, ocs_thread_fctn fctn,
				 const char *name, void *arg, ocs_thread_start_e start_option);
extern int32_t ocs_thread_start(ocs_thread_t *thread);
extern void *ocs_thread_get_arg(ocs_thread_t *mythread);
extern int32_t ocs_thread_terminate(ocs_thread_t *thread);
extern int32_t ocs_thread_terminate_requested(ocs_thread_t *thread);
extern int32_t ocs_thread_get_retval(ocs_thread_t *thread);
extern void ocs_thread_yield(ocs_thread_t *thread);
extern ocs_thread_t *ocs_thread_self(void);
extern int32_t ocs_thread_setcpu(ocs_thread_t *thread, uint32_t cpu);
extern int32_t ocs_thread_getcpu(void);


/***************************************************************************
 * PCI
 *
 * Several functions below refer to a "register set". This is one or
 * more PCI BARs that constitute a PCI address. For example, if a MMIO
 * region is described using both BAR[0] and BAR[1], the combination of
 * BARs defines register set 0.
 */

/**
 * @brief tracks mapped PCI memory regions
 */
typedef struct ocs_pci_reg_s {
	uint32_t		rid;
	struct resource		*res;
	bus_space_tag_t		btag;
	bus_space_handle_t	bhandle;
} ocs_pci_reg_t;

#define PCI_MAX_BAR				6
#define PCI_64BIT_BAR0				0

#define PCI_VENDOR_EMULEX       		0x10df		/* Emulex */

#define PCI_PRODUCT_EMULEX_OCE16001		0xe200		/* OneCore 16Gb FC (lancer) */
#define PCI_PRODUCT_EMULEX_OCE16002		0xe200		/* OneCore 16Gb FC (lancer) */
#define PCI_PRODUCT_EMULEX_LPE31004		0xe300  /* LightPulse 16Gb x 4 FC (lancer-g6) */
#define PCI_PRODUCT_EMULEX_LPE32002		0xe300  /* LightPulse 32Gb x 2 FC (lancer-g6) */
#define PCI_PRODUCT_EMULEX_OCE1600_VF		0xe208
#define PCI_PRODUCT_EMULEX_OCE50102		0xe260		/* OneCore FCoE (lancer) */
#define PCI_PRODUCT_EMULEX_OCE50102_VF		0xe268

/**
 * @ingroup os
 * @brief Get the PCI bus, device, and function values
 *
 * @param ocs OS specific handle or driver context
 * @param bus Pointer to location to store the bus number.
 * @param dev Pointer to location to store the device number.
 * @param func Pointer to location to store the function number.
 *
 * @return Returns 0.
 */
extern int32_t
ocs_get_bus_dev_func(ocs_t *ocs, uint8_t* bus, uint8_t* dev, uint8_t* func);

extern ocs_t *ocs_get_instance(uint32_t index);
extern uint32_t ocs_instance(void *os);


/**
 * @ingroup os
 * @brief Read a 32 bit value from the specified configuration register
 *
 * @param os OS specific handle or driver context
 * @param reg register offset
 *
 * @return The 32 bit value
 */
extern uint32_t ocs_config_read32(ocs_os_handle_t os, uint32_t reg);

/**
 * @ingroup os
 * @brief Read a 16 bit value from the specified configuration
 *        register
 *
 * @param os OS specific handle or driver context
 * @param reg register offset
 *
 * @return The 16 bit value
 */
extern uint16_t ocs_config_read16(ocs_os_handle_t os, uint32_t reg);

/**
 * @ingroup os
 * @brief Read a 8 bit value from the specified configuration
 *        register
 *
 * @param os OS specific handle or driver context
 * @param reg register offset
 *
 * @return The 8 bit value
 */
extern uint8_t ocs_config_read8(ocs_os_handle_t os, uint32_t reg);

/**
 * @ingroup os
 * @brief Write a 8 bit value to the specified configuration
 *        register
 *
 * @param os OS specific handle or driver context
 * @param reg register offset
 * @param val value to write
 *
 * @return None
 */
extern void ocs_config_write8(ocs_os_handle_t os, uint32_t reg, uint8_t val);

/**
 * @ingroup os
 * @brief Write a 16 bit value to the specified configuration
 *        register
 *
 * @param os OS specific handle or driver context
 * @param reg register offset
 * @param val value to write
 *
 * @return None
 */
extern void ocs_config_write16(ocs_os_handle_t os, uint32_t reg, uint16_t val);

/**
 * @ingroup os
 * @brief Write a 32 bit value to the specified configuration
 *        register
 *
 * @param os OS specific handle or driver context
 * @param reg register offset
 * @param val value to write
 *
 * @return None
 */
extern void ocs_config_write32(ocs_os_handle_t os, uint32_t reg, uint32_t val);

/**
 * @ingroup os
 * @brief Read a PCI register
 *
 * @param os OS specific handle or driver context
 * @param rset Which "register set" to use
 * @param off  Register offset
 *
 * @return 32 bit conents of the register
 */
extern uint32_t ocs_reg_read32(ocs_os_handle_t os, uint32_t rset, uint32_t off);

/**
 * @ingroup os
 * @brief Read a PCI register
 *
 * @param os OS specific handle or driver context
 * @param rset Which "register set" to use
 * @param off  Register offset
 *
 * @return 16 bit conents of the register
 */
extern uint16_t ocs_reg_read16(ocs_os_handle_t os, uint32_t rset, uint32_t off);

/**
 * @ingroup os
 * @brief Read a PCI register
 *
 * @param os OS specific handle or driver context
 * @param rset Which "register set" to use
 * @param off  Register offset
 *
 * @return 8 bit conents of the register
 */
extern uint8_t ocs_reg_read8(ocs_os_handle_t os, uint32_t rset, uint32_t off);

/**
 * @ingroup os
 * @brief Write a PCI register
 *
 * @param os OS specific handle or driver context
 * @param rset Which "register set" to use
 * @param off  Register offset
 * @param val  32-bit value to write
 */
extern void ocs_reg_write32(ocs_os_handle_t os, uint32_t rset, uint32_t off, uint32_t val);

/**
 * @ingroup os
 * @brief Write a PCI register
 *
 * @param os OS specific handle or driver context
 * @param rset Which "register set" to use
 * @param off  Register offset
 * @param val  16-bit value to write
 */
extern void ocs_reg_write16(ocs_os_handle_t os, uint32_t rset, uint32_t off, uint16_t val);

/**
 * @ingroup os
 * @brief Write a PCI register
 *
 * @param os OS specific handle or driver context
 * @param rset Which "register set" to use
 * @param off  Register offset
 * @param val  8-bit value to write
 */
extern void ocs_reg_write8(ocs_os_handle_t os, uint32_t rset, uint32_t off, uint8_t val);

/**
 * @ingroup os
 * @brief Disable interrupts
 *
 * @param os OS specific handle or driver context
 */
extern void ocs_intr_disable(ocs_os_handle_t os);

/**
 * @ingroup os
 * @brief Enable interrupts
 *
 * @param os OS specific handle or driver context
 */
extern void ocs_intr_enable(ocs_os_handle_t os);

/**
 * @ingroup os
 * @brief Return model string
 *
 * @param os OS specific handle or driver context
 */
extern const char *ocs_pci_model(uint16_t vendor, uint16_t device);

extern void ocs_print_stack(void);

extern void ocs_abort(void) __attribute__((noreturn));

/***************************************************************************
 * Reference counting
 *
 */

/**
 * @ingroup os
 * @brief reference counter object
 */
typedef void (*ocs_ref_release_t)(void *arg);
typedef struct ocs_ref_s {
	ocs_ref_release_t release; /* release function to call */
	void *arg;
	uint32_t count;		/* ref count; no need to be atomic if we have a lock */
} ocs_ref_t;

/**
 * @ingroup os
 * @brief initialize given reference object
 *
 * @param ref Pointer to reference object
 * @param release Function to be called when count is 0.
 * @param arg Argument to be passed to release function.
 */
static inline void
ocs_ref_init(ocs_ref_t *ref, ocs_ref_release_t release, void *arg)
{
	ref->release = release;
	ref->arg = arg;
	ocs_atomic_init(&ref->count, 1);
}

/**
 * @ingroup os
 * @brief Return reference count value
 *
 * @param ref Pointer to reference object
 *
 * @return Count value of given reference object
 */
static inline uint32_t
ocs_ref_read_count(ocs_ref_t *ref)
{
	return ocs_atomic_read(&ref->count);
}

/**
 * @ingroup os
 * @brief Set count on given reference object to a value.
 *
 * @param ref Pointer to reference object
 * @param i Set count to this value
 */
static inline void
ocs_ref_set(ocs_ref_t *ref, int i)
{
	ocs_atomic_set(&ref->count, i);
}

/**
 * @ingroup os
 * @brief Take a reference on given object.
 *
 * @par Description
 * This function takes a reference on an object.
 *
 * Note: this function should only be used if the caller can
 * guarantee that the reference count is >= 1 and will stay >= 1
 * for the duration of this call (i.e. won't go to zero). If it
 * can't (the refcount may go to zero during this call),
 * ocs_ref_get_unless_zero() should be used instead.
 *
 * @param ref Pointer to reference object
 *
 */
static inline void
ocs_ref_get(ocs_ref_t *ref)
{
	ocs_atomic_add_return(&ref->count, 1);
}

/**
 * @ingroup os
 * @brief Take a reference on given object if count is not zero.
 *
 * @par Description
 * This function takes a reference on an object if and only if
 * the given reference object is "active" or valid.
 *
 * @param ref Pointer to reference object
 *
 * @return non-zero if "get" succeeded; Return zero if ref count
 * is zero.
 */
static inline uint32_t
ocs_ref_get_unless_zero(ocs_ref_t *ref)
{
	uint32_t rc = 0;
	rc = ocs_atomic_read(&ref->count);
		if (rc != 0) {
			ocs_atomic_add_return(&ref->count, 1);
		}
	return rc;
}

/**
 * @ingroup os
 * @brief Decrement reference on given object
 *
 * @par Description
 * This function decrements the reference count on the given
 * reference object. If the reference count becomes zero, the
 * "release" function (set during "init" time) is called.
 *
 * @param ref Pointer to reference object
 *
 * @return non-zero if release function was called; zero
 * otherwise.
 */
static inline uint32_t
ocs_ref_put(ocs_ref_t *ref)
{
	uint32_t rc = 0;
	if (ocs_atomic_sub_return(&ref->count, 1) == 1) {
		ref->release(ref->arg);
		rc = 1;
	}
	return rc;
}

/**
 * @ingroup os
 * @brief Get the OS system ticks
 *
 * @return number of ticks that have occurred since the system
 * booted.
 */
static inline uint64_t
ocs_get_os_ticks(void)
{
	return ticks;
}

/**
 * @ingroup os
 * @brief Get the OS system tick frequency
 *
 * @return frequency of system ticks.
 */
static inline uint32_t
ocs_get_os_tick_freq(void)
{
	return hz;
}

/*****************************************************************************
 *
 * CPU topology API
 */

typedef struct {
	uint32_t num_cpus;	/* Number of CPU cores */
	uint8_t hyper;		/* TRUE if threaded CPUs */
} ocs_cpuinfo_t;

extern int32_t ocs_get_cpuinfo(ocs_cpuinfo_t *cpuinfo);
extern uint32_t ocs_get_num_cpus(void);

#include "ocs_list.h"
#include "ocs_utils.h"
#include "ocs_mgmt.h"
#include "ocs_common.h"

#endif /* !_OCS_OS_H */
