/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

#include <linux/instrumented.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#define uaccess_kernel() segment_eq(get_fs(), KERNEL_DS)

#include <asm/uaccess.h>

/*
 * Architectures should provide two primitives (raw_copy_{to,from}_user())
 * and get rid of their private instances of copy_{to,from}_user() and
 * __copy_{to,from}_user{,_inatomic}().
 *
 * raw_copy_{to,from}_user(to, from, size) should copy up to size bytes and
 * return the amount left to copy.  They should assume that access_ok() has
 * already been checked (and succeeded); they should *not* zero-pad anything.
 * No KASAN or object size checks either - those belong here.
 *
 * Both of these functions should attempt to copy size bytes starting at from
 * into the area starting at to.  They must not fetch or store anything
 * outside of those areas.  Return value must be between 0 (everything
 * copied successfully) and size (nothing copied).
 *
 * If raw_copy_{to,from}_user(to, from, size) returns N, size - N bytes starting
 * at to must become equal to the bytes fetched from the corresponding area
 * starting at from.  All data past to + size - N must be left unmodified.
 *
 * If copying succeeds, the return value must be 0.  If some data cannot be
 * fetched, it is permitted to copy less than had been fetched; the only
 * hard requirement is that not storing anything at all (i.e. returning size)
 * should happen only when nothing could be copied.  In other words, you don't
 * have to squeeze as much as possible - it is allowed, but not necessary.
 *
 * For raw_copy_from_user() to always points to kernel memory and no faults
 * on store should happen.  Interpretation of from is affected by set_fs().
 * For raw_copy_to_user() it's the other way round.
 *
 * Both can be inlined - it's up to architectures whether it wants to bother
 * with that.  They should not be used directly; they are used to implement
 * the 6 functions (copy_{to,from}_user(), __copy_{to,from}_user_inatomic())
 * that are used instead.  Out of those, __... ones are inlined.  Plain
 * copy_{to,from}_user() might or might not be inlined.  If you want them
 * inlined, have asm/uaccess.h define INLINE_COPY_{TO,FROM}_USER.
 *
 * NOTE: only copy_from_user() zero-pads the destination in case of short copy.
 * Neither __copy_from_user() nor __copy_from_user_inatomic() zero anything
 * at all; their callers absolutely must check the return value.
 *
 * Biarch ones should also provide raw_copy_in_user() - similar to the above,
 * but both source and destination are __user pointers (affected by set_fs()
 * as usual) and both source and destination can trigger faults.
 */

static __always_inline __must_check unsigned long
__copy_from_user_inatomic(void *to, const void __user *from, unsigned long n)
{
	instrument_copy_from_user(to, from, n);
	check_object_size(to, n, false);
	return raw_copy_from_user(to, from, n);
}

static __always_inline __must_check unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	might_fault();
	instrument_copy_from_user(to, from, n);
	check_object_size(to, n, false);
	return raw_copy_from_user(to, from, n);
}

/**
 * __copy_to_user_inatomic: - Copy a block of data into user space, with less checking.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.
 *
 * Copy data from kernel space to user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 * The caller should also make sure he pins the user space address
 * so that we don't result in page fault and sleep.
 */
static __always_inline __must_check unsigned long
__copy_to_user_inatomic(void __user *to, const void *from, unsigned long n)
{
	instrument_copy_to_user(to, from, n);
	check_object_size(from, n, true);
	return raw_copy_to_user(to, from, n);
}

static __always_inline __must_check unsigned long
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	instrument_copy_to_user(to, from, n);
	check_object_size(from, n, true);
	return raw_copy_to_user(to, from, n);
}

#ifdef INLINE_COPY_FROM_USER
static inline __must_check unsigned long
_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long res = n;
	might_fault();
	if (likely(access_ok(from, n))) {
		instrument_copy_from_user(to, from, n);
		res = raw_copy_from_user(to, from, n);
	}
	if (unlikely(res))
		memset(to + (n - res), 0, res);
	return res;
}
#else
extern __must_check unsigned long
_copy_from_user(void *, const void __user *, unsigned long);
#endif

#ifdef INLINE_COPY_TO_USER
static inline __must_check unsigned long
_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}
#else
extern __must_check unsigned long
_copy_to_user(void __user *, const void *, unsigned long);
#endif

static __always_inline unsigned long __must_check
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (likely(check_copy_size(to, n, false)))
		n = _copy_from_user(to, from, n);
	return n;
}

static __always_inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (likely(check_copy_size(from, n, true)))
		n = _copy_to_user(to, from, n);
	return n;
}
#ifdef CONFIG_COMPAT
static __always_inline unsigned long __must_check
copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	might_fault();
	if (access_ok(to, n) && access_ok(from, n))
		n = raw_copy_in_user(to, from, n);
	return n;
}
#endif

static __always_inline void pagefault_disabled_inc(void)
{
	current->pagefault_disabled++;
}

static __always_inline void pagefault_disabled_dec(void)
{
	current->pagefault_disabled--;
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
static inline bool pagefault_disabled(void)
{
	return current->pagefault_disabled != 0;
}

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

static inline __must_check unsigned long
__copy_from_user_inatomic_nocache(void *to, const void __user *from,
				  unsigned long n)
{
	return __copy_from_user_inatomic(to, from, n);
}

#endif		/* ARCH_HAS_NOCACHE_UACCESS */

extern __must_check int check_zeroed_user(const void __user *from, size_t size);

/**
 * copy_struct_from_user: copy a struct from userspace
 * @dst:   Destination address, in kernel space. This buffer must be @ksize
 *         bytes long.
 * @ksize: Size of @dst struct.
 * @src:   Source address, in userspace.
 * @usize: (Alleged) size of @src struct.
 *
 * Copies a struct from userspace to kernel space, in a way that guarantees
 * backwards-compatibility for struct syscall arguments (as long as future
 * struct extensions are made such that all new fields are *appended* to the
 * old struct, and zeroed-out new fields have the same meaning as the old
 * struct).
 *
 * @ksize is just sizeof(*dst), and @usize should've been passed by userspace.
 * The recommended usage is something like the following:
 *
 *   SYSCALL_DEFINE2(foobar, const struct foo __user *, uarg, size_t, usize)
 *   {
 *      int err;
 *      struct foo karg = {};
 *
 *      if (usize > PAGE_SIZE)
 *        return -E2BIG;
 *      if (usize < FOO_SIZE_VER0)
 *        return -EINVAL;
 *
 *      err = copy_struct_from_user(&karg, sizeof(karg), uarg, usize);
 *      if (err)
 *        return err;
 *
 *      // ...
 *   }
 *
 * There are three cases to consider:
 *  * If @usize == @ksize, then it's copied verbatim.
 *  * If @usize < @ksize, then the userspace has passed an old struct to a
 *    newer kernel. The rest of the trailing bytes in @dst (@ksize - @usize)
 *    are to be zero-filled.
 *  * If @usize > @ksize, then the userspace has passed a new struct to an
 *    older kernel. The trailing bytes unknown to the kernel (@usize - @ksize)
 *    are checked to ensure they are zeroed, otherwise -E2BIG is returned.
 *
 * Returns (in all cases, some data may have been copied):
 *  * -E2BIG:  (@usize > @ksize) and there are non-zero trailing bytes in @src.
 *  * -EFAULT: access to userspace failed.
 */
static __always_inline __must_check int
copy_struct_from_user(void *dst, size_t ksize, const void __user *src,
		      size_t usize)
{
	size_t size = min(ksize, usize);
	size_t rest = max(ksize, usize) - size;

	/* Deal with trailing bytes. */
	if (usize < ksize) {
		memset(dst + size, 0, rest);
	} else if (usize > ksize) {
		int ret = check_zeroed_user(src + size, rest);
		if (ret <= 0)
			return ret ?: -E2BIG;
	}
	/* Copy the interoperable parts of the struct. */
	if (copy_from_user(dst, src, size))
		return -EFAULT;
	return 0;
}

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
extern long probe_kernel_read_strict(void *dst, const void *src, size_t size);
extern long __probe_kernel_read(void *dst, const void *src, size_t size);

/*
 * probe_user_read(): safely attempt to read from a location in user space
 * @dst: pointer to the buffer that shall take the data
 * @src: address to read from
 * @size: size of the data chunk
 *
 * Safely read from address @src to the buffer at @dst.  If a kernel fault
 * happens, handle that and return -EFAULT.
 */
extern long probe_user_read(void *dst, const void __user *src, size_t size);
extern long __probe_user_read(void *dst, const void __user *src, size_t size);

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

/*
 * probe_user_write(): safely attempt to write to a location in user space
 * @dst: address to write to
 * @src: pointer to the data that shall be written
 * @size: size of the data chunk
 *
 * Safely write to address @dst from the buffer at @src.  If a kernel fault
 * happens, handle that and return -EFAULT.
 */
extern long notrace probe_user_write(void __user *dst, const void *src, size_t size);
extern long notrace __probe_user_write(void __user *dst, const void *src, size_t size);

extern long strncpy_from_unsafe(char *dst, const void *unsafe_addr, long count);
extern long strncpy_from_unsafe_strict(char *dst, const void *unsafe_addr,
				       long count);
extern long __strncpy_from_unsafe(char *dst, const void *unsafe_addr, long count);
extern long strncpy_from_unsafe_user(char *dst, const void __user *unsafe_addr,
				     long count);
extern long strnlen_unsafe_user(const void __user *unsafe_addr, long count);

/**
 * probe_kernel_address(): safely attempt to read from a location
 * @addr: address to read from
 * @retval: read into this variable
 *
 * Returns 0 on success, or -EFAULT.
 */
#define probe_kernel_address(addr, retval)		\
	probe_kernel_read(&retval, addr, sizeof(retval))

#ifndef user_access_begin
#define user_access_begin(ptr,len) access_ok(ptr, len)
#define user_access_end() do { } while (0)
#define unsafe_op_wrap(op, err) do { if (unlikely(op)) goto err; } while (0)
#define unsafe_get_user(x,p,e) unsafe_op_wrap(__get_user(x,p),e)
#define unsafe_put_user(x,p,e) unsafe_op_wrap(__put_user(x,p),e)
#define unsafe_copy_to_user(d,s,l,e) unsafe_op_wrap(__copy_to_user(d,s,l),e)
static inline unsigned long user_access_save(void) { return 0UL; }
static inline void user_access_restore(unsigned long flags) { }
#endif

#ifdef CONFIG_HARDENED_USERCOPY
void usercopy_warn(const char *name, const char *detail, bool to_user,
		   unsigned long offset, unsigned long len);
void __noreturn usercopy_abort(const char *name, const char *detail,
			       bool to_user, unsigned long offset,
			       unsigned long len);
#endif

#endif		/* __LINUX_UACCESS_H__ */
