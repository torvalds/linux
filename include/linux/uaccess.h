#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

#include <linux/sched.h>
#include <asm/uaccess.h>

static __always_inline void pagefault_disabled_inc(void)
{
	current->pagefault_disabled++;
}

static __always_inline void pagefault_disabled_dec(void)
{
	current->pagefault_disabled--;
	WARN_ON(current->pagefault_disabled < 0);
}

/*
 * These routines enable/disable the pagefault handler. If disabled, it will
 * not take any locks and go straight to the fixup table.
 *
 * User access methods will not sleep when called from a pagefault_disabled()
 * environment.
 */
static inline void pagefault_disable(void)
{
	pagefault_disabled_inc();
	/*
	 * make sure to have issued the store before a pagefault
	 * can hit.
	 */
	barrier();
}

static inline void pagefault_enable(void)
{
	/*
	 * make sure to issue those last loads/stores before enabling
	 * the pagefault handler again.
	 */
	barrier();
	pagefault_disabled_dec();
}

/*
 * Is the pagefault handler disabled? If so, user access methods will not sleep.
 */
#define pagefault_disabled() (current->pagefault_disabled != 0)

/*
 * The pagefault handler is in general disabled by pagefault_disable() or
 * when in irq context (via in_atomic()).
 *
 * This function should only be used by the fault handlers. Other users should
 * stick to pagefault_disabled().
 * Please NEVER use preempt_disable() to disable the fault handler. With
 * !CONFIG_PREEMPT_COUNT, this is like a NOP. So the handler won't be disabled.
 * in_atomic() will report different values based on !CONFIG_PREEMPT_COUNT.
 */
#define faulthandler_disabled() (pagefault_disabled() || in_atomic())

#ifndef ARCH_HAS_NOCACHE_UACCESS

static inline unsigned long __copy_from_user_inatomic_nocache(void *to,
				const void __user *from, unsigned long n)
{
	return __copy_from_user_inatomic(to, from, n);
}

static inline unsigned long __copy_from_user_nocache(void *to,
				const void __user *from, unsigned long n)
{
	return __copy_from_user(to, from, n);
}

#endif		/* ARCH_HAS_NOCACHE_UACCESS */

/**
 * probe_kernel_address(): safely attempt to read from a location
 * @addr: address to read from - its type is type typeof(retval)*
 * @retval: read into this variable
 *
 * Safely read from address @addr into variable @revtal.  If a kernel fault
 * happens, handle that and return -EFAULT.
 * We ensure that the __get_user() is executed in atomic context so that
 * do_page_fault() doesn't attempt to take mmap_sem.  This makes
 * probe_kernel_address() suitable for use within regions where the caller
 * already holds mmap_sem, or other locks which nest inside mmap_sem.
 * This must be a macro because __get_user() needs to know the types of the
 * args.
 *
 * We don't include enough header files to be able to do the set_fs().  We
 * require that the probe_kernel_address() caller will do that.
 */
#define probe_kernel_address(addr, retval)		\
	({						\
		long ret;				\
		mm_segment_t old_fs = get_fs();		\
							\
		set_fs(KERNEL_DS);			\
		pagefault_disable();			\
		ret = __copy_from_user_inatomic(&(retval), (__force typeof(retval) __user *)(addr), sizeof(retval));		\
		pagefault_enable();			\
		set_fs(old_fs);				\
		ret;					\
	})

/*
 * probe_kernel_read(): safely attempt to read from a location
 * @dst: pointer to the buffer that shall take the data
 * @src: address to read from
 * @size: size of the data chunk
 *
 * Safely read from address @src to the buffer at @dst.  If a kernel fault
 * happens, handle that and return -EFAULT.
 */
extern long probe_kernel_read(void *dst, const void *src, size_t size);
extern long __probe_kernel_read(void *dst, const void *src, size_t size);

/*
 * probe_kernel_write(): safely attempt to write to a location
 * @dst: address to write to
 * @src: pointer to the data that shall be written
 * @size: size of the data chunk
 *
 * Safely write to address @dst from the buffer at @src.  If a kernel fault
 * happens, handle that and return -EFAULT.
 */
extern long notrace probe_kernel_write(void *dst, const void *src, size_t size);
extern long notrace __probe_kernel_write(void *dst, const void *src, size_t size);

#endif		/* __LINUX_UACCESS_H__ */
