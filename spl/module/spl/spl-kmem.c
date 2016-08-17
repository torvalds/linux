/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/vmem.h>
#include <linux/mm.h>
#include <linux/ratelimit.h>

/*
 * As a general rule kmem_alloc() allocations should be small, preferably
 * just a few pages since they must by physically contiguous.  Therefore, a
 * rate limited warning will be printed to the console for any kmem_alloc()
 * which exceeds a reasonable threshold.
 *
 * The default warning threshold is set to sixteen pages but capped at 64K to
 * accommodate systems using large pages.  This value was selected to be small
 * enough to ensure the largest allocations are quickly noticed and fixed.
 * But large enough to avoid logging any warnings when a allocation size is
 * larger than optimal but not a serious concern.  Since this value is tunable,
 * developers are encouraged to set it lower when testing so any new largish
 * allocations are quickly caught.  These warnings may be disabled by setting
 * the threshold to zero.
 */
/* BEGIN CSTYLED */
unsigned int spl_kmem_alloc_warn = MIN(16 * PAGE_SIZE, 64 * 1024);
module_param(spl_kmem_alloc_warn, uint, 0644);
MODULE_PARM_DESC(spl_kmem_alloc_warn,
	"Warning threshold in bytes for a kmem_alloc()");
EXPORT_SYMBOL(spl_kmem_alloc_warn);

/*
 * Large kmem_alloc() allocations will fail if they exceed KMALLOC_MAX_SIZE.
 * Allocations which are marginally smaller than this limit may succeed but
 * should still be avoided due to the expense of locating a contiguous range
 * of free pages.  Therefore, a maximum kmem size with reasonable safely
 * margin of 4x is set.  Kmem_alloc() allocations larger than this maximum
 * will quickly fail.  Vmem_alloc() allocations less than or equal to this
 * value will use kmalloc(), but shift to vmalloc() when exceeding this value.
 */
unsigned int spl_kmem_alloc_max = (KMALLOC_MAX_SIZE >> 2);
module_param(spl_kmem_alloc_max, uint, 0644);
MODULE_PARM_DESC(spl_kmem_alloc_max,
	"Maximum size in bytes for a kmem_alloc()");
EXPORT_SYMBOL(spl_kmem_alloc_max);
/* END CSTYLED */

int
kmem_debugging(void)
{
	return (0);
}
EXPORT_SYMBOL(kmem_debugging);

char *
kmem_vasprintf(const char *fmt, va_list ap)
{
	va_list aq;
	char *ptr;

	do {
		va_copy(aq, ap);
		ptr = kvasprintf(kmem_flags_convert(KM_SLEEP), fmt, aq);
		va_end(aq);
	} while (ptr == NULL);

	return (ptr);
}
EXPORT_SYMBOL(kmem_vasprintf);

char *
kmem_asprintf(const char *fmt, ...)
{
	va_list ap;
	char *ptr;

	do {
		va_start(ap, fmt);
		ptr = kvasprintf(kmem_flags_convert(KM_SLEEP), fmt, ap);
		va_end(ap);
	} while (ptr == NULL);

	return (ptr);
}
EXPORT_SYMBOL(kmem_asprintf);

static char *
__strdup(const char *str, int flags)
{
	char *ptr;
	int n;

	n = strlen(str);
	ptr = kmalloc(n + 1, kmem_flags_convert(flags));
	if (ptr)
		memcpy(ptr, str, n + 1);

	return (ptr);
}

char *
strdup(const char *str)
{
	return (__strdup(str, KM_SLEEP));
}
EXPORT_SYMBOL(strdup);

void
strfree(char *str)
{
	kfree(str);
}
EXPORT_SYMBOL(strfree);

/*
 * Limit the number of large allocation stack traces dumped to not more than
 * 5 every 60 seconds to prevent denial-of-service attacks from debug code.
 */
DEFINE_RATELIMIT_STATE(kmem_alloc_ratelimit_state, 60 * HZ, 5);

/*
 * General purpose unified implementation of kmem_alloc(). It is an
 * amalgamation of Linux and Illumos allocator design. It should never be
 * exported to ensure that code using kmem_alloc()/kmem_zalloc() remains
 * relatively portable.  Consumers may only access this function through
 * wrappers that enforce the common flags to ensure portability.
 */
inline void *
spl_kmem_alloc_impl(size_t size, int flags, int node)
{
	gfp_t lflags = kmem_flags_convert(flags);
	int use_vmem = 0;
	void *ptr;

	/*
	 * Log abnormally large allocations and rate limit the console output.
	 * Allocations larger than spl_kmem_alloc_warn should be performed
	 * through the vmem_alloc()/vmem_zalloc() interfaces.
	 */
	if ((spl_kmem_alloc_warn > 0) && (size > spl_kmem_alloc_warn) &&
	    !(flags & KM_VMEM) && __ratelimit(&kmem_alloc_ratelimit_state)) {
		printk(KERN_WARNING
		    "Large kmem_alloc(%lu, 0x%x), please file an issue at:\n"
		    "https://github.com/zfsonlinux/zfs/issues/new\n",
		    (unsigned long)size, flags);
		dump_stack();
	}

	/*
	 * Use a loop because kmalloc_node() can fail when GFP_KERNEL is used
	 * unlike kmem_alloc() with KM_SLEEP on Illumos.
	 */
	do {
		/*
		 * Calling kmalloc_node() when the size >= spl_kmem_alloc_max
		 * is unsafe.  This must fail for all for kmem_alloc() and
		 * kmem_zalloc() callers.
		 *
		 * For vmem_alloc() and vmem_zalloc() callers it is permissible
		 * to use __vmalloc().  However, in general use of __vmalloc()
		 * is strongly discouraged because a global lock must be
		 * acquired.  Contention on this lock can significantly
		 * impact performance so frequently manipulating the virtual
		 * address space is strongly discouraged.
		 */
		if ((size > spl_kmem_alloc_max) || use_vmem) {
			if (flags & KM_VMEM) {
				ptr = __vmalloc(size, lflags, PAGE_KERNEL);
			} else {
				return (NULL);
			}
		} else {
			ptr = kmalloc_node(size, lflags, node);
		}

		if (likely(ptr) || (flags & KM_NOSLEEP))
			return (ptr);

		/*
		 * For vmem_alloc() and vmem_zalloc() callers retry immediately
		 * using __vmalloc() which is unlikely to fail.
		 */
		if ((flags & KM_VMEM) && (use_vmem == 0))  {
			use_vmem = 1;
			continue;
		}

		if (unlikely(__ratelimit(&kmem_alloc_ratelimit_state))) {
			printk(KERN_WARNING
			    "Possible memory allocation deadlock: "
			    "size=%lu lflags=0x%x",
			    (unsigned long)size, lflags);
			dump_stack();
		}

		/*
		 * Use cond_resched() instead of congestion_wait() to avoid
		 * deadlocking systems where there are no block devices.
		 */
		cond_resched();
	} while (1);

	return (NULL);
}

inline void
spl_kmem_free_impl(const void *buf, size_t size)
{
	if (is_vmalloc_addr(buf))
		vfree(buf);
	else
		kfree(buf);
}

/*
 * Memory allocation and accounting for kmem_* * style allocations.  When
 * DEBUG_KMEM is enabled the total memory allocated will be tracked and
 * any memory leaked will be reported during module unload.
 *
 * ./configure --enable-debug-kmem
 */
#ifdef DEBUG_KMEM

/* Shim layer memory accounting */
#ifdef HAVE_ATOMIC64_T
atomic64_t kmem_alloc_used = ATOMIC64_INIT(0);
unsigned long long kmem_alloc_max = 0;
#else  /* HAVE_ATOMIC64_T */
atomic_t kmem_alloc_used = ATOMIC_INIT(0);
unsigned long long kmem_alloc_max = 0;
#endif /* HAVE_ATOMIC64_T */

EXPORT_SYMBOL(kmem_alloc_used);
EXPORT_SYMBOL(kmem_alloc_max);

inline void *
spl_kmem_alloc_debug(size_t size, int flags, int node)
{
	void *ptr;

	ptr = spl_kmem_alloc_impl(size, flags, node);
	if (ptr) {
		kmem_alloc_used_add(size);
		if (unlikely(kmem_alloc_used_read() > kmem_alloc_max))
			kmem_alloc_max = kmem_alloc_used_read();
	}

	return (ptr);
}

inline void
spl_kmem_free_debug(const void *ptr, size_t size)
{
	kmem_alloc_used_sub(size);
	spl_kmem_free_impl(ptr, size);
}

/*
 * When DEBUG_KMEM_TRACKING is enabled not only will total bytes be tracked
 * but also the location of every alloc and free.  When the SPL module is
 * unloaded a list of all leaked addresses and where they were allocated
 * will be dumped to the console.  Enabling this feature has a significant
 * impact on performance but it makes finding memory leaks straight forward.
 *
 * Not surprisingly with debugging enabled the xmem_locks are very highly
 * contended particularly on xfree().  If we want to run with this detailed
 * debugging enabled for anything other than debugging  we need to minimize
 * the contention by moving to a lock per xmem_table entry model.
 *
 * ./configure --enable-debug-kmem-tracking
 */
#ifdef DEBUG_KMEM_TRACKING

#include <linux/hash.h>
#include <linux/ctype.h>

#define	KMEM_HASH_BITS		10
#define	KMEM_TABLE_SIZE		(1 << KMEM_HASH_BITS)

typedef struct kmem_debug {
	struct hlist_node kd_hlist;	/* Hash node linkage */
	struct list_head kd_list;	/* List of all allocations */
	void *kd_addr;			/* Allocation pointer */
	size_t kd_size;			/* Allocation size */
	const char *kd_func;		/* Allocation function */
	int kd_line;			/* Allocation line */
} kmem_debug_t;

static spinlock_t kmem_lock;
static struct hlist_head kmem_table[KMEM_TABLE_SIZE];
static struct list_head kmem_list;

static kmem_debug_t *
kmem_del_init(spinlock_t *lock, struct hlist_head *table,
    int bits, const void *addr)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kmem_debug *p;
	unsigned long flags;

	spin_lock_irqsave(lock, flags);

	head = &table[hash_ptr((void *)addr, bits)];
	hlist_for_each(node, head) {
		p = list_entry(node, struct kmem_debug, kd_hlist);
		if (p->kd_addr == addr) {
			hlist_del_init(&p->kd_hlist);
			list_del_init(&p->kd_list);
			spin_unlock_irqrestore(lock, flags);
			return (p);
		}
	}

	spin_unlock_irqrestore(lock, flags);

	return (NULL);
}

inline void *
spl_kmem_alloc_track(size_t size, int flags,
    const char *func, int line, int node)
{
	void *ptr = NULL;
	kmem_debug_t *dptr;
	unsigned long irq_flags;

	dptr = kmalloc(sizeof (kmem_debug_t), kmem_flags_convert(flags));
	if (dptr == NULL)
		return (NULL);

	dptr->kd_func = __strdup(func, flags);
	if (dptr->kd_func == NULL) {
		kfree(dptr);
		return (NULL);
	}

	ptr = spl_kmem_alloc_debug(size, flags, node);
	if (ptr == NULL) {
		kfree(dptr->kd_func);
		kfree(dptr);
		return (NULL);
	}

	INIT_HLIST_NODE(&dptr->kd_hlist);
	INIT_LIST_HEAD(&dptr->kd_list);

	dptr->kd_addr = ptr;
	dptr->kd_size = size;
	dptr->kd_line = line;

	spin_lock_irqsave(&kmem_lock, irq_flags);
	hlist_add_head(&dptr->kd_hlist,
	    &kmem_table[hash_ptr(ptr, KMEM_HASH_BITS)]);
	list_add_tail(&dptr->kd_list, &kmem_list);
	spin_unlock_irqrestore(&kmem_lock, irq_flags);

	return (ptr);
}

inline void
spl_kmem_free_track(const void *ptr, size_t size)
{
	kmem_debug_t *dptr;

	/* Ignore NULL pointer since we haven't tracked it at all */
	if (ptr == NULL)
		return;

	/* Must exist in hash due to kmem_alloc() */
	dptr = kmem_del_init(&kmem_lock, kmem_table, KMEM_HASH_BITS, ptr);
	ASSERT3P(dptr, !=, NULL);
	ASSERT3S(dptr->kd_size, ==, size);

	kfree(dptr->kd_func);
	kfree(dptr);

	spl_kmem_free_debug(ptr, size);
}
#endif /* DEBUG_KMEM_TRACKING */
#endif /* DEBUG_KMEM */

/*
 * Public kmem_alloc(), kmem_zalloc() and kmem_free() interfaces.
 */
void *
spl_kmem_alloc(size_t size, int flags, const char *func, int line)
{
	ASSERT0(flags & ~KM_PUBLIC_MASK);

#if !defined(DEBUG_KMEM)
	return (spl_kmem_alloc_impl(size, flags, NUMA_NO_NODE));
#elif !defined(DEBUG_KMEM_TRACKING)
	return (spl_kmem_alloc_debug(size, flags, NUMA_NO_NODE));
#else
	return (spl_kmem_alloc_track(size, flags, func, line, NUMA_NO_NODE));
#endif
}
EXPORT_SYMBOL(spl_kmem_alloc);

void *
spl_kmem_zalloc(size_t size, int flags, const char *func, int line)
{
	ASSERT0(flags & ~KM_PUBLIC_MASK);

	flags |= KM_ZERO;

#if !defined(DEBUG_KMEM)
	return (spl_kmem_alloc_impl(size, flags, NUMA_NO_NODE));
#elif !defined(DEBUG_KMEM_TRACKING)
	return (spl_kmem_alloc_debug(size, flags, NUMA_NO_NODE));
#else
	return (spl_kmem_alloc_track(size, flags, func, line, NUMA_NO_NODE));
#endif
}
EXPORT_SYMBOL(spl_kmem_zalloc);

void
spl_kmem_free(const void *buf, size_t size)
{
#if !defined(DEBUG_KMEM)
	return (spl_kmem_free_impl(buf, size));
#elif !defined(DEBUG_KMEM_TRACKING)
	return (spl_kmem_free_debug(buf, size));
#else
	return (spl_kmem_free_track(buf, size));
#endif
}
EXPORT_SYMBOL(spl_kmem_free);

#if defined(DEBUG_KMEM) && defined(DEBUG_KMEM_TRACKING)
static char *
spl_sprintf_addr(kmem_debug_t *kd, char *str, int len, int min)
{
	int size = ((len - 1) < kd->kd_size) ? (len - 1) : kd->kd_size;
	int i, flag = 1;

	ASSERT(str != NULL && len >= 17);
	memset(str, 0, len);

	/*
	 * Check for a fully printable string, and while we are at
	 * it place the printable characters in the passed buffer.
	 */
	for (i = 0; i < size; i++) {
		str[i] = ((char *)(kd->kd_addr))[i];
		if (isprint(str[i])) {
			continue;
		} else {
			/*
			 * Minimum number of printable characters found
			 * to make it worthwhile to print this as ascii.
			 */
			if (i > min)
				break;

			flag = 0;
			break;
		}
	}

	if (!flag) {
		sprintf(str, "%02x%02x%02x%02x%02x%02x%02x%02x",
		    *((uint8_t *)kd->kd_addr),
		    *((uint8_t *)kd->kd_addr + 2),
		    *((uint8_t *)kd->kd_addr + 4),
		    *((uint8_t *)kd->kd_addr + 6),
		    *((uint8_t *)kd->kd_addr + 8),
		    *((uint8_t *)kd->kd_addr + 10),
		    *((uint8_t *)kd->kd_addr + 12),
		    *((uint8_t *)kd->kd_addr + 14));
	}

	return (str);
}

static int
spl_kmem_init_tracking(struct list_head *list, spinlock_t *lock, int size)
{
	int i;

	spin_lock_init(lock);
	INIT_LIST_HEAD(list);

	for (i = 0; i < size; i++)
		INIT_HLIST_HEAD(&kmem_table[i]);

	return (0);
}

static void
spl_kmem_fini_tracking(struct list_head *list, spinlock_t *lock)
{
	unsigned long flags;
	kmem_debug_t *kd;
	char str[17];

	spin_lock_irqsave(lock, flags);
	if (!list_empty(list))
		printk(KERN_WARNING "%-16s %-5s %-16s %s:%s\n", "address",
		    "size", "data", "func", "line");

	list_for_each_entry(kd, list, kd_list) {
		printk(KERN_WARNING "%p %-5d %-16s %s:%d\n", kd->kd_addr,
		    (int)kd->kd_size, spl_sprintf_addr(kd, str, 17, 8),
		    kd->kd_func, kd->kd_line);
	}

	spin_unlock_irqrestore(lock, flags);
}
#endif /* DEBUG_KMEM && DEBUG_KMEM_TRACKING */

int
spl_kmem_init(void)
{
#ifdef DEBUG_KMEM
	kmem_alloc_used_set(0);

#ifdef DEBUG_KMEM_TRACKING
	spl_kmem_init_tracking(&kmem_list, &kmem_lock, KMEM_TABLE_SIZE);
#endif /* DEBUG_KMEM_TRACKING */
#endif /* DEBUG_KMEM */

	return (0);
}

void
spl_kmem_fini(void)
{
#ifdef DEBUG_KMEM
	/*
	 * Display all unreclaimed memory addresses, including the
	 * allocation size and the first few bytes of what's located
	 * at that address to aid in debugging.  Performance is not
	 * a serious concern here since it is module unload time.
	 */
	if (kmem_alloc_used_read() != 0)
		printk(KERN_WARNING "kmem leaked %ld/%llu bytes\n",
		    (unsigned long)kmem_alloc_used_read(), kmem_alloc_max);

#ifdef DEBUG_KMEM_TRACKING
	spl_kmem_fini_tracking(&kmem_list, &kmem_lock);
#endif /* DEBUG_KMEM_TRACKING */
#endif /* DEBUG_KMEM */
}
