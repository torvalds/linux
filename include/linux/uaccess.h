/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

#include <linux/cleanup.h>
#include <linux/fault-inject-usercopy.h>
#include <linux/instrumented.h>
#include <linux/minmax.h>
#include <linux/nospec.h>
#include <linux/sched.h>
#include <linux/ucopysize.h>

#include <asm/uaccess.h>

/*
 * Architectures that support memory tagging (assigning tags to memory regions,
 * embedding these tags into addresses that point to these memory regions, and
 * checking that the memory and the pointer tags match on memory accesses)
 * redefine this macro to strip tags from pointers.
 *
 * Passing down mm_struct allows to define untagging rules on per-process
 * basis.
 *
 * It's defined as noop for architectures that don't support memory tagging.
 */
#ifndef untagged_addr
#define untagged_addr(addr) (addr)
#endif

#ifndef untagged_addr_remote
#define untagged_addr_remote(mm, addr)	({		\
	mmap_assert_locked(mm);				\
	untagged_addr(addr);				\
})
#endif

#ifdef masked_user_access_begin
 #define can_do_masked_user_access() 1
# ifndef masked_user_write_access_begin
#  define masked_user_write_access_begin masked_user_access_begin
# endif
# ifndef masked_user_read_access_begin
#  define masked_user_read_access_begin masked_user_access_begin
#endif
#else
 #define can_do_masked_user_access() 0
 #define masked_user_access_begin(src) NULL
 #define masked_user_read_access_begin(src) NULL
 #define masked_user_write_access_begin(src) NULL
 #define mask_user_address(src) (src)
#endif

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
	unsigned long res;

	instrument_copy_from_user_before(to, from, n);
	check_object_size(to, n, false);
	res = raw_copy_from_user(to, from, n);
	instrument_copy_from_user_after(to, from, n, res);
	return res;
}

static __always_inline __must_check unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long res;

	might_fault();
	instrument_copy_from_user_before(to, from, n);
	if (should_fail_usercopy())
		return n;
	check_object_size(to, n, false);
	res = raw_copy_from_user(to, from, n);
	instrument_copy_from_user_after(to, from, n, res);
	return res;
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
	if (should_fail_usercopy())
		return n;
	instrument_copy_to_user(to, from, n);
	check_object_size(from, n, true);
	return raw_copy_to_user(to, from, n);
}

static __always_inline __must_check unsigned long
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	if (should_fail_usercopy())
		return n;
	instrument_copy_to_user(to, from, n);
	check_object_size(from, n, true);
	return raw_copy_to_user(to, from, n);
}

/*
 * Architectures that #define INLINE_COPY_TO_USER use this function
 * directly in the normal copy_to/from_user(), the other ones go
 * through an extern _copy_to/from_user(), which expands the same code
 * here.
 */
static inline __must_check unsigned long
_inline_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long res = n;
	might_fault();
	if (should_fail_usercopy())
		goto fail;
	if (can_do_masked_user_access())
		from = mask_user_address(from);
	else {
		if (!access_ok(from, n))
			goto fail;
		/*
		 * Ensure that bad access_ok() speculation will not
		 * lead to nasty side effects *after* the copy is
		 * finished:
		 */
		barrier_nospec();
	}
	instrument_copy_from_user_before(to, from, n);
	res = raw_copy_from_user(to, from, n);
	instrument_copy_from_user_after(to, from, n, res);
	if (likely(!res))
		return 0;
fail:
	memset(to + (n - res), 0, res);
	return res;
}
#ifndef INLINE_COPY_FROM_USER
extern __must_check unsigned long
_copy_from_user(void *, const void __user *, unsigned long);
#endif

static inline __must_check unsigned long
_inline_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	if (should_fail_usercopy())
		return n;
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}
#ifndef INLINE_COPY_TO_USER
extern __must_check unsigned long
_copy_to_user(void __user *, const void *, unsigned long);
#endif

static __always_inline unsigned long __must_check
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (!check_copy_size(to, n, false))
		return n;
#ifdef INLINE_COPY_FROM_USER
	return _inline_copy_from_user(to, from, n);
#else
	return _copy_from_user(to, from, n);
#endif
}

static __always_inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (!check_copy_size(from, n, true))
		return n;

#ifdef INLINE_COPY_TO_USER
	return _inline_copy_to_user(to, from, n);
#else
	return _copy_to_user(to, from, n);
#endif
}

#ifndef copy_mc_to_kernel
/*
 * Without arch opt-in this generic copy_mc_to_kernel() will not handle
 * #MC (or arch equivalent) during source read.
 */
static inline unsigned long __must_check
copy_mc_to_kernel(void *dst, const void *src, size_t cnt)
{
	memcpy(dst, src, cnt);
	return 0;
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

DEFINE_LOCK_GUARD_0(pagefault, pagefault_disable(), pagefault_enable())

#ifndef CONFIG_ARCH_HAS_SUBPAGE_FAULTS

/**
 * probe_subpage_writeable: probe the user range for write faults at sub-page
 *			    granularity (e.g. arm64 MTE)
 * @uaddr: start of address range
 * @size: size of address range
 *
 * Returns 0 on success, the number of bytes not probed on fault.
 *
 * It is expected that the caller checked for the write permission of each
 * page in the range either by put_user() or GUP. The architecture port can
 * implement a more efficient get_user() probing if the same sub-page faults
 * are triggered by either a read or a write.
 */
static inline size_t probe_subpage_writeable(char __user *uaddr, size_t size)
{
	return 0;
}

#endif /* CONFIG_ARCH_HAS_SUBPAGE_FAULTS */

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

	/* Double check if ksize is larger than a known object size. */
	if (WARN_ON_ONCE(ksize > __builtin_object_size(dst, 1)))
		return -E2BIG;

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

/**
 * copy_struct_to_user: copy a struct to userspace
 * @dst:   Destination address, in userspace. This buffer must be @ksize
 *         bytes long.
 * @usize: (Alleged) size of @dst struct.
 * @src:   Source address, in kernel space.
 * @ksize: Size of @src struct.
 * @ignored_trailing: Set to %true if there was a non-zero byte in @src that
 * userspace cannot see because they are using an smaller struct.
 *
 * Copies a struct from kernel space to userspace, in a way that guarantees
 * backwards-compatibility for struct syscall arguments (as long as future
 * struct extensions are made such that all new fields are *appended* to the
 * old struct, and zeroed-out new fields have the same meaning as the old
 * struct).
 *
 * Some syscalls may wish to make sure that userspace knows about everything in
 * the struct, and if there is a non-zero value that userspce doesn't know
 * about, they want to return an error (such as -EMSGSIZE) or have some other
 * fallback (such as adding a "you're missing some information" flag). If
 * @ignored_trailing is non-%NULL, it will be set to %true if there was a
 * non-zero byte that could not be copied to userspace (ie. was past @usize).
 *
 * While unconditionally returning an error in this case is the simplest
 * solution, for maximum backward compatibility you should try to only return
 * -EMSGSIZE if the user explicitly requested the data that couldn't be copied.
 * Note that structure sizes can change due to header changes and simple
 * recompilations without code changes(!), so if you care about
 * @ignored_trailing you probably want to make sure that any new field data is
 * associated with a flag. Otherwise you might assume that a program knows
 * about data it does not.
 *
 * @ksize is just sizeof(*src), and @usize should've been passed by userspace.
 * The recommended usage is something like the following:
 *
 *   SYSCALL_DEFINE2(foobar, struct foo __user *, uarg, size_t, usize)
 *   {
 *      int err;
 *      bool ignored_trailing;
 *      struct foo karg = {};
 *
 *      if (usize > PAGE_SIZE)
 *		return -E2BIG;
 *      if (usize < FOO_SIZE_VER0)
 *		return -EINVAL;
 *
 *      // ... modify karg somehow ...
 *
 *      err = copy_struct_to_user(uarg, usize, &karg, sizeof(karg),
 *				  &ignored_trailing);
 *      if (err)
 *		return err;
 *      if (ignored_trailing)
 *		return -EMSGSIZE:
 *
 *      // ...
 *   }
 *
 * There are three cases to consider:
 *  * If @usize == @ksize, then it's copied verbatim.
 *  * If @usize < @ksize, then the kernel is trying to pass userspace a newer
 *    struct than it supports. Thus we only copy the interoperable portions
 *    (@usize) and ignore the rest (but @ignored_trailing is set to %true if
 *    any of the trailing (@ksize - @usize) bytes are non-zero).
 *  * If @usize > @ksize, then the kernel is trying to pass userspace an older
 *    struct than userspace supports. In order to make sure the
 *    unknown-to-the-kernel fields don't contain garbage values, we zero the
 *    trailing (@usize - @ksize) bytes.
 *
 * Returns (in all cases, some data may have been copied):
 *  * -EFAULT: access to userspace failed.
 */
static __always_inline __must_check int
copy_struct_to_user(void __user *dst, size_t usize, const void *src,
		    size_t ksize, bool *ignored_trailing)
{
	size_t size = min(ksize, usize);
	size_t rest = max(ksize, usize) - size;

	/* Double check if ksize is larger than a known object size. */
	if (WARN_ON_ONCE(ksize > __builtin_object_size(src, 1)))
		return -E2BIG;

	/* Deal with trailing bytes. */
	if (usize > ksize) {
		if (clear_user(dst + size, rest))
			return -EFAULT;
	}
	if (ignored_trailing)
		*ignored_trailing = ksize < usize &&
			memchr_inv(src + size, 0, rest) != NULL;
	/* Copy the interoperable parts of the struct. */
	if (copy_to_user(dst, src, size))
		return -EFAULT;
	return 0;
}

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size);

long copy_from_kernel_nofault(void *dst, const void *src, size_t size);
long notrace copy_to_kernel_nofault(void *dst, const void *src, size_t size);

long copy_from_user_nofault(void *dst, const void __user *src, size_t size);
long notrace copy_to_user_nofault(void __user *dst, const void *src,
		size_t size);

long strncpy_from_kernel_nofault(char *dst, const void *unsafe_addr,
		long count);

long strncpy_from_user_nofault(char *dst, const void __user *unsafe_addr,
		long count);
long strnlen_user_nofault(const void __user *unsafe_addr, long count);

#ifdef arch_get_kernel_nofault
/*
 * Wrap the architecture implementation so that @label can be outside of a
 * cleanup() scope. A regular C goto works correctly, but ASM goto does
 * not. Clang rejects such an attempt, but GCC silently emits buggy code.
 */
#define __get_kernel_nofault(dst, src, type, label)		\
do {								\
	__label__ local_label;					\
	arch_get_kernel_nofault(dst, src, type, local_label);	\
	if (0) {						\
	local_label:						\
		goto label;					\
	}							\
} while (0)

#define __put_kernel_nofault(dst, src, type, label)		\
do {								\
	__label__ local_label;					\
	arch_put_kernel_nofault(dst, src, type, local_label);	\
	if (0) {						\
	local_label:						\
		goto label;					\
	}							\
} while (0)

#elif !defined(__get_kernel_nofault) /* arch_get_kernel_nofault */

#define __get_kernel_nofault(dst, src, type, label)	\
do {							\
	type __user *p = (type __force __user *)(src);	\
	type data;					\
	if (__get_user(data, p))			\
		goto label;				\
	*(type *)dst = data;				\
} while (0)

#define __put_kernel_nofault(dst, src, type, label)	\
do {							\
	type __user *p = (type __force __user *)(dst);	\
	type data = *(type *)src;			\
	if (__put_user(data, p))			\
		goto label;				\
} while (0)

#endif  /* !__get_kernel_nofault */

/**
 * get_kernel_nofault(): safely attempt to read from a location
 * @val: read into this variable
 * @ptr: address to read from
 *
 * Returns 0 on success, or -EFAULT.
 */
#define get_kernel_nofault(val, ptr) ({				\
	const typeof(val) *__gk_ptr = (ptr);			\
	copy_from_kernel_nofault(&(val), __gk_ptr, sizeof(val));\
})

#ifdef user_access_begin

#ifdef arch_unsafe_get_user
/*
 * Wrap the architecture implementation so that @label can be outside of a
 * cleanup() scope. A regular C goto works correctly, but ASM goto does
 * not. Clang rejects such an attempt, but GCC silently emits buggy code.
 *
 * Some architectures use internal local labels already, but this extra
 * indirection here is harmless because the compiler optimizes it out
 * completely in any case. This construct just ensures that the ASM GOTO
 * target is always in the local scope. The C goto 'label' works correctly
 * when leaving a cleanup() scope.
 */
#define unsafe_get_user(x, ptr, label)			\
do {							\
	__label__ local_label;				\
	arch_unsafe_get_user(x, ptr, local_label);	\
	if (0) {					\
	local_label:					\
		goto label;				\
	}						\
} while (0)

#define unsafe_put_user(x, ptr, label)			\
do {							\
	__label__ local_label;				\
	arch_unsafe_put_user(x, ptr, local_label);	\
	if (0) {					\
	local_label:					\
		goto label;				\
	}						\
} while (0)
#endif /* arch_unsafe_get_user */

#else /* user_access_begin */
#define user_access_begin(ptr,len) access_ok(ptr, len)
#define user_access_end() do { } while (0)
#define unsafe_op_wrap(op, err) do { if (unlikely(op)) goto err; } while (0)
#define unsafe_get_user(x,p,e) unsafe_op_wrap(__get_user(x,p),e)
#define unsafe_put_user(x,p,e) unsafe_op_wrap(__put_user(x,p),e)
#define unsafe_copy_to_user(d,s,l,e) unsafe_op_wrap(__copy_to_user(d,s,l),e)
#define unsafe_copy_from_user(d,s,l,e) unsafe_op_wrap(__copy_from_user(d,s,l),e)
static inline unsigned long user_access_save(void) { return 0UL; }
static inline void user_access_restore(unsigned long flags) { }
#endif /* !user_access_begin */

#ifndef user_write_access_begin
#define user_write_access_begin user_access_begin
#define user_write_access_end user_access_end
#endif
#ifndef user_read_access_begin
#define user_read_access_begin user_access_begin
#define user_read_access_end user_access_end
#endif

/* Define RW variant so the below _mode macro expansion works */
#define masked_user_rw_access_begin(u)	masked_user_access_begin(u)
#define user_rw_access_begin(u, s)	user_access_begin(u, s)
#define user_rw_access_end()		user_access_end()

/* Scoped user access */
#define USER_ACCESS_GUARD(_mode)				\
static __always_inline void __user *				\
class_user_##_mode##_begin(void __user *ptr)			\
{								\
	return ptr;						\
}								\
								\
static __always_inline void					\
class_user_##_mode##_end(void __user *ptr)			\
{								\
	user_##_mode##_access_end();				\
}								\
								\
DEFINE_CLASS(user_ ##_mode## _access, void __user *,		\
	     class_user_##_mode##_end(_T),			\
	     class_user_##_mode##_begin(ptr), void __user *ptr)	\
								\
static __always_inline class_user_##_mode##_access_t		\
class_user_##_mode##_access_ptr(void __user *scope)		\
{								\
	return scope;						\
}

USER_ACCESS_GUARD(read)
USER_ACCESS_GUARD(write)
USER_ACCESS_GUARD(rw)
#undef USER_ACCESS_GUARD

/**
 * __scoped_user_access_begin - Start a scoped user access
 * @mode:	The mode of the access class (read, write, rw)
 * @uptr:	The pointer to access user space memory
 * @size:	Size of the access
 * @elbl:	Error label to goto when the access region is rejected
 *
 * Internal helper for __scoped_user_access(). Don't use directly.
 */
#define __scoped_user_access_begin(mode, uptr, size, elbl)		\
({									\
	typeof(uptr) __retptr;						\
									\
	if (can_do_masked_user_access()) {				\
		__retptr = masked_user_##mode##_access_begin(uptr);	\
	} else {							\
		__retptr = uptr;					\
		if (!user_##mode##_access_begin(uptr, size))		\
			goto elbl;					\
	}								\
	__retptr;							\
})

/**
 * __scoped_user_access - Open a scope for user access
 * @mode:	The mode of the access class (read, write, rw)
 * @uptr:	The pointer to access user space memory
 * @size:	Size of the access
 * @elbl:	Error label to goto when the access region is rejected. It
 *		must be placed outside the scope
 *
 * If the user access function inside the scope requires a fault label, it
 * can use @elbl or a different label outside the scope, which requires
 * that user access which is implemented with ASM GOTO has been properly
 * wrapped. See unsafe_get_user() for reference.
 *
 *	scoped_user_rw_access(ptr, efault) {
 *		unsafe_get_user(rval, &ptr->rval, efault);
 *		unsafe_put_user(wval, &ptr->wval, efault);
 *	}
 *	return 0;
 *  efault:
 *	return -EFAULT;
 *
 * The scope is internally implemented as a autoterminating nested for()
 * loop, which can be left with 'return', 'break' and 'goto' at any
 * point.
 *
 * When the scope is left user_##@_mode##_access_end() is automatically
 * invoked.
 *
 * When the architecture supports masked user access and the access region
 * which is determined by @uptr and @size is not a valid user space
 * address, i.e. < TASK_SIZE, the scope sets the pointer to a faulting user
 * space address and does not terminate early. This optimizes for the good
 * case and lets the performance uncritical bad case go through the fault.
 *
 * The eventual modification of the pointer is limited to the scope.
 * Outside of the scope the original pointer value is unmodified, so that
 * the original pointer value is available for diagnostic purposes in an
 * out of scope fault path.
 *
 * Nesting scoped user access into a user access scope is invalid and fails
 * the build. Nesting into other guards, e.g. pagefault is safe.
 *
 * The masked variant does not check the size of the access and relies on a
 * mapping hole (e.g. guard page) to catch an out of range pointer, the
 * first access to user memory inside the scope has to be within
 * @uptr ... @uptr + PAGE_SIZE - 1
 *
 * Don't use directly. Use scoped_masked_user_$MODE_access() instead.
 */
#define __scoped_user_access(mode, uptr, size, elbl)					\
for (bool done = false; !done; done = true)						\
	for (void __user *_tmpptr = __scoped_user_access_begin(mode, uptr, size, elbl); \
	     !done; done = true)							\
		for (CLASS(user_##mode##_access, scope)(_tmpptr); !done; done = true)	\
			/* Force modified pointer usage within the scope */		\
			for (const typeof(uptr) uptr = _tmpptr; !done; done = true)

/**
 * scoped_user_read_access_size - Start a scoped user read access with given size
 * @usrc:	Pointer to the user space address to read from
 * @size:	Size of the access starting from @usrc
 * @elbl:	Error label to goto when the access region is rejected
 *
 * For further information see __scoped_user_access() above.
 */
#define scoped_user_read_access_size(usrc, size, elbl)		\
	__scoped_user_access(read, usrc, size, elbl)

/**
 * scoped_user_read_access - Start a scoped user read access
 * @usrc:	Pointer to the user space address to read from
 * @elbl:	Error label to goto when the access region is rejected
 *
 * The size of the access starting from @usrc is determined via sizeof(*@usrc)).
 *
 * For further information see __scoped_user_access() above.
 */
#define scoped_user_read_access(usrc, elbl)				\
	scoped_user_read_access_size(usrc, sizeof(*(usrc)), elbl)

/**
 * scoped_user_write_access_size - Start a scoped user write access with given size
 * @udst:	Pointer to the user space address to write to
 * @size:	Size of the access starting from @udst
 * @elbl:	Error label to goto when the access region is rejected
 *
 * For further information see __scoped_user_access() above.
 */
#define scoped_user_write_access_size(udst, size, elbl)			\
	__scoped_user_access(write, udst, size, elbl)

/**
 * scoped_user_write_access - Start a scoped user write access
 * @udst:	Pointer to the user space address to write to
 * @elbl:	Error label to goto when the access region is rejected
 *
 * The size of the access starting from @udst is determined via sizeof(*@udst)).
 *
 * For further information see __scoped_user_access() above.
 */
#define scoped_user_write_access(udst, elbl)				\
	scoped_user_write_access_size(udst, sizeof(*(udst)), elbl)

/**
 * scoped_user_rw_access_size - Start a scoped user read/write access with given size
 * @uptr	Pointer to the user space address to read from and write to
 * @size:	Size of the access starting from @uptr
 * @elbl:	Error label to goto when the access region is rejected
 *
 * For further information see __scoped_user_access() above.
 */
#define scoped_user_rw_access_size(uptr, size, elbl)			\
	__scoped_user_access(rw, uptr, size, elbl)

/**
 * scoped_user_rw_access - Start a scoped user read/write access
 * @uptr	Pointer to the user space address to read from and write to
 * @elbl:	Error label to goto when the access region is rejected
 *
 * The size of the access starting from @uptr is determined via sizeof(*@uptr)).
 *
 * For further information see __scoped_user_access() above.
 */
#define scoped_user_rw_access(uptr, elbl)				\
	scoped_user_rw_access_size(uptr, sizeof(*(uptr)), elbl)

/**
 * get_user_inline - Read user data inlined
 * @val:	The variable to store the value read from user memory
 * @usrc:	Pointer to the user space memory to read from
 *
 * Return: 0 if successful, -EFAULT when faulted
 *
 * Inlined variant of get_user(). Only use when there is a demonstrable
 * performance reason.
 */
#define get_user_inline(val, usrc)				\
({								\
	__label__ efault;					\
	typeof(usrc) _tmpsrc = usrc;				\
	int _ret = 0;						\
								\
	scoped_user_read_access(_tmpsrc, efault)		\
		unsafe_get_user(val, _tmpsrc, efault);		\
	if (0) {						\
	efault:							\
		_ret = -EFAULT;					\
	}							\
	_ret;							\
})

/**
 * put_user_inline - Write to user memory inlined
 * @val:	The value to write
 * @udst:	Pointer to the user space memory to write to
 *
 * Return: 0 if successful, -EFAULT when faulted
 *
 * Inlined variant of put_user(). Only use when there is a demonstrable
 * performance reason.
 */
#define put_user_inline(val, udst)				\
({								\
	__label__ efault;					\
	typeof(udst) _tmpdst = udst;				\
	int _ret = 0;						\
								\
	scoped_user_write_access(_tmpdst, efault)		\
		unsafe_put_user(val, _tmpdst, efault);		\
	if (0) {						\
	efault:							\
		_ret = -EFAULT;					\
	}							\
	_ret;							\
})

#ifdef CONFIG_HARDENED_USERCOPY
void __noreturn usercopy_abort(const char *name, const char *detail,
			       bool to_user, unsigned long offset,
			       unsigned long len);
#endif

#endif		/* __LINUX_UACCESS_H__ */
