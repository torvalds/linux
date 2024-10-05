// SPDX-License-Identifier: GPL-2.0-only
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/security.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mman.h>
#include <linux/hugetlb.h>
#include <linux/vmalloc.h>
#include <linux/userfaultfd_k.h>
#include <linux/elf.h>
#include <linux/elf-randomize.h>
#include <linux/personality.h>
#include <linux/random.h>
#include <linux/processor.h>
#include <linux/sizes.h>
#include <linux/compat.h>

#include <linux/uaccess.h>

#include <kunit/visibility.h>

#include "internal.h"
#include "swap.h"

/**
 * kfree_const - conditionally free memory
 * @x: pointer to the memory
 *
 * Function calls kfree only if @x is not in .rodata section.
 */
void kfree_const(const void *x)
{
	if (!is_kernel_rodata((unsigned long)x))
		kfree(x);
}
EXPORT_SYMBOL(kfree_const);

/**
 * kstrdup - allocate space for and copy an existing string
 * @s: the string to duplicate
 * @gfp: the GFP mask used in the kmalloc() call when allocating memory
 *
 * Return: newly allocated copy of @s or %NULL in case of error
 */
noinline
char *kstrdup(const char *s, gfp_t gfp)
{
	size_t len;
	char *buf;

	if (!s)
		return NULL;

	len = strlen(s) + 1;
	buf = kmalloc_track_caller(len, gfp);
	if (buf)
		memcpy(buf, s, len);
	return buf;
}
EXPORT_SYMBOL(kstrdup);

/**
 * kstrdup_const - conditionally duplicate an existing const string
 * @s: the string to duplicate
 * @gfp: the GFP mask used in the kmalloc() call when allocating memory
 *
 * Note: Strings allocated by kstrdup_const should be freed by kfree_const and
 * must not be passed to krealloc().
 *
 * Return: source string if it is in .rodata section otherwise
 * fallback to kstrdup.
 */
const char *kstrdup_const(const char *s, gfp_t gfp)
{
	if (is_kernel_rodata((unsigned long)s))
		return s;

	return kstrdup(s, gfp);
}
EXPORT_SYMBOL(kstrdup_const);

/**
 * kstrndup - allocate space for and copy an existing string
 * @s: the string to duplicate
 * @max: read at most @max chars from @s
 * @gfp: the GFP mask used in the kmalloc() call when allocating memory
 *
 * Note: Use kmemdup_nul() instead if the size is known exactly.
 *
 * Return: newly allocated copy of @s or %NULL in case of error
 */
char *kstrndup(const char *s, size_t max, gfp_t gfp)
{
	size_t len;
	char *buf;

	if (!s)
		return NULL;

	len = strnlen(s, max);
	buf = kmalloc_track_caller(len+1, gfp);
	if (buf) {
		memcpy(buf, s, len);
		buf[len] = '\0';
	}
	return buf;
}
EXPORT_SYMBOL(kstrndup);

/**
 * kmemdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 * @gfp: GFP mask to use
 *
 * Return: newly allocated copy of @src or %NULL in case of error,
 * result is physically contiguous. Use kfree() to free.
 */
void *kmemdup_noprof(const void *src, size_t len, gfp_t gfp)
{
	void *p;

	p = kmalloc_node_track_caller_noprof(len, gfp, NUMA_NO_NODE, _RET_IP_);
	if (p)
		memcpy(p, src, len);
	return p;
}
EXPORT_SYMBOL(kmemdup_noprof);

/**
 * kmemdup_array - duplicate a given array.
 *
 * @src: array to duplicate.
 * @count: number of elements to duplicate from array.
 * @element_size: size of each element of array.
 * @gfp: GFP mask to use.
 *
 * Return: duplicated array of @src or %NULL in case of error,
 * result is physically contiguous. Use kfree() to free.
 */
void *kmemdup_array(const void *src, size_t count, size_t element_size, gfp_t gfp)
{
	return kmemdup(src, size_mul(element_size, count), gfp);
}
EXPORT_SYMBOL(kmemdup_array);

/**
 * kvmemdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 * @gfp: GFP mask to use
 *
 * Return: newly allocated copy of @src or %NULL in case of error,
 * result may be not physically contiguous. Use kvfree() to free.
 */
void *kvmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *p;

	p = kvmalloc(len, gfp);
	if (p)
		memcpy(p, src, len);
	return p;
}
EXPORT_SYMBOL(kvmemdup);

/**
 * kmemdup_nul - Create a NUL-terminated string from unterminated data
 * @s: The data to stringify
 * @len: The size of the data
 * @gfp: the GFP mask used in the kmalloc() call when allocating memory
 *
 * Return: newly allocated copy of @s with NUL-termination or %NULL in
 * case of error
 */
char *kmemdup_nul(const char *s, size_t len, gfp_t gfp)
{
	char *buf;

	if (!s)
		return NULL;

	buf = kmalloc_track_caller(len + 1, gfp);
	if (buf) {
		memcpy(buf, s, len);
		buf[len] = '\0';
	}
	return buf;
}
EXPORT_SYMBOL(kmemdup_nul);

static kmem_buckets *user_buckets __ro_after_init;

static int __init init_user_buckets(void)
{
	user_buckets = kmem_buckets_create("memdup_user", 0, 0, INT_MAX, NULL);

	return 0;
}
subsys_initcall(init_user_buckets);

/**
 * memdup_user - duplicate memory region from user space
 *
 * @src: source address in user space
 * @len: number of bytes to copy
 *
 * Return: an ERR_PTR() on failure.  Result is physically
 * contiguous, to be freed by kfree().
 */
void *memdup_user(const void __user *src, size_t len)
{
	void *p;

	p = kmem_buckets_alloc_track_caller(user_buckets, len, GFP_USER | __GFP_NOWARN);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, src, len)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}

	return p;
}
EXPORT_SYMBOL(memdup_user);

/**
 * vmemdup_user - duplicate memory region from user space
 *
 * @src: source address in user space
 * @len: number of bytes to copy
 *
 * Return: an ERR_PTR() on failure.  Result may be not
 * physically contiguous.  Use kvfree() to free.
 */
void *vmemdup_user(const void __user *src, size_t len)
{
	void *p;

	p = kmem_buckets_valloc(user_buckets, len, GFP_USER);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, src, len)) {
		kvfree(p);
		return ERR_PTR(-EFAULT);
	}

	return p;
}
EXPORT_SYMBOL(vmemdup_user);

/**
 * strndup_user - duplicate an existing string from user space
 * @s: The string to duplicate
 * @n: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Return: newly allocated copy of @s or an ERR_PTR() in case of error
 */
char *strndup_user(const char __user *s, long n)
{
	char *p;
	long length;

	length = strnlen_user(s, n);

	if (!length)
		return ERR_PTR(-EFAULT);

	if (length > n)
		return ERR_PTR(-EINVAL);

	p = memdup_user(s, length);

	if (IS_ERR(p))
		return p;

	p[length - 1] = '\0';

	return p;
}
EXPORT_SYMBOL(strndup_user);

/**
 * memdup_user_nul - duplicate memory region from user space and NUL-terminate
 *
 * @src: source address in user space
 * @len: number of bytes to copy
 *
 * Return: an ERR_PTR() on failure.
 */
void *memdup_user_nul(const void __user *src, size_t len)
{
	char *p;

	/*
	 * Always use GFP_KERNEL, since copy_from_user() can sleep and
	 * cause pagefault, which makes it pointless to use GFP_NOFS
	 * or GFP_ATOMIC.
	 */
	p = kmalloc_track_caller(len + 1, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, src, len)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}
	p[len] = '\0';

	return p;
}
EXPORT_SYMBOL(memdup_user_nul);

/* Check if the vma is being used as a stack by this task */
int vma_is_stack_for_current(struct vm_area_struct *vma)
{
	struct task_struct * __maybe_unused t = current;

	return (vma->vm_start <= KSTK_ESP(t) && vma->vm_end >= KSTK_ESP(t));
}

/*
 * Change backing file, only valid to use during initial VMA setup.
 */
void vma_set_file(struct vm_area_struct *vma, struct file *file)
{
	/* Changing an anonymous vma with this is illegal */
	get_file(file);
	swap(vma->vm_file, file);
	fput(file);
}
EXPORT_SYMBOL(vma_set_file);

#ifndef STACK_RND_MASK
#define STACK_RND_MASK (0x7ff >> (PAGE_SHIFT - 12))     /* 8MB of VA */
#endif

unsigned long randomize_stack_top(unsigned long stack_top)
{
	unsigned long random_variable = 0;

	if (current->flags & PF_RANDOMIZE) {
		random_variable = get_random_long();
		random_variable &= STACK_RND_MASK;
		random_variable <<= PAGE_SHIFT;
	}
#ifdef CONFIG_STACK_GROWSUP
	return PAGE_ALIGN(stack_top) + random_variable;
#else
	return PAGE_ALIGN(stack_top) - random_variable;
#endif
}

/**
 * randomize_page - Generate a random, page aligned address
 * @start:	The smallest acceptable address the caller will take.
 * @range:	The size of the area, starting at @start, within which the
 *		random address must fall.
 *
 * If @start + @range would overflow, @range is capped.
 *
 * NOTE: Historical use of randomize_range, which this replaces, presumed that
 * @start was already page aligned.  We now align it regardless.
 *
 * Return: A page aligned address within [start, start + range).  On error,
 * @start is returned.
 */
unsigned long randomize_page(unsigned long start, unsigned long range)
{
	if (!PAGE_ALIGNED(start)) {
		range -= PAGE_ALIGN(start) - start;
		start = PAGE_ALIGN(start);
	}

	if (start > ULONG_MAX - range)
		range = ULONG_MAX - start;

	range >>= PAGE_SHIFT;

	if (range == 0)
		return start;

	return start + (get_random_long() % range << PAGE_SHIFT);
}

#ifdef CONFIG_ARCH_WANT_DEFAULT_TOPDOWN_MMAP_LAYOUT
unsigned long __weak arch_randomize_brk(struct mm_struct *mm)
{
	/* Is the current task 32bit ? */
	if (!IS_ENABLED(CONFIG_64BIT) || is_compat_task())
		return randomize_page(mm->brk, SZ_32M);

	return randomize_page(mm->brk, SZ_1G);
}

unsigned long arch_mmap_rnd(void)
{
	unsigned long rnd;

#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
	if (is_compat_task())
		rnd = get_random_long() & ((1UL << mmap_rnd_compat_bits) - 1);
	else
#endif /* CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS */
		rnd = get_random_long() & ((1UL << mmap_rnd_bits) - 1);

	return rnd << PAGE_SHIFT;
}

static int mmap_is_legacy(struct rlimit *rlim_stack)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;

	/* On parisc the stack always grows up - so a unlimited stack should
	 * not be an indicator to use the legacy memory layout. */
	if (rlim_stack->rlim_cur == RLIM_INFINITY &&
		!IS_ENABLED(CONFIG_STACK_GROWSUP))
		return 1;

	return sysctl_legacy_va_layout;
}

/*
 * Leave enough space between the mmap area and the stack to honour ulimit in
 * the face of randomisation.
 */
#define MIN_GAP		(SZ_128M)
#define MAX_GAP		(STACK_TOP / 6 * 5)

static unsigned long mmap_base(unsigned long rnd, struct rlimit *rlim_stack)
{
#ifdef CONFIG_STACK_GROWSUP
	/*
	 * For an upwards growing stack the calculation is much simpler.
	 * Memory for the maximum stack size is reserved at the top of the
	 * task. mmap_base starts directly below the stack and grows
	 * downwards.
	 */
	return PAGE_ALIGN_DOWN(mmap_upper_limit(rlim_stack) - rnd);
#else
	unsigned long gap = rlim_stack->rlim_cur;
	unsigned long pad = stack_guard_gap;

	/* Account for stack randomization if necessary */
	if (current->flags & PF_RANDOMIZE)
		pad += (STACK_RND_MASK << PAGE_SHIFT);

	/* Values close to RLIM_INFINITY can overflow. */
	if (gap + pad > gap)
		gap += pad;

	if (gap < MIN_GAP && MIN_GAP < MAX_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;

	return PAGE_ALIGN(STACK_TOP - gap - rnd);
#endif
}

void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	unsigned long random_factor = 0UL;

	if (current->flags & PF_RANDOMIZE)
		random_factor = arch_mmap_rnd();

	if (mmap_is_legacy(rlim_stack)) {
		mm->mmap_base = TASK_UNMAPPED_BASE + random_factor;
		clear_bit(MMF_TOPDOWN, &mm->flags);
	} else {
		mm->mmap_base = mmap_base(random_factor, rlim_stack);
		set_bit(MMF_TOPDOWN, &mm->flags);
	}
}
#elif defined(CONFIG_MMU) && !defined(HAVE_ARCH_PICK_MMAP_LAYOUT)
void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	mm->mmap_base = TASK_UNMAPPED_BASE;
	clear_bit(MMF_TOPDOWN, &mm->flags);
}
#endif
#ifdef CONFIG_MMU
EXPORT_SYMBOL_IF_KUNIT(arch_pick_mmap_layout);
#endif

/**
 * __account_locked_vm - account locked pages to an mm's locked_vm
 * @mm:          mm to account against
 * @pages:       number of pages to account
 * @inc:         %true if @pages should be considered positive, %false if not
 * @task:        task used to check RLIMIT_MEMLOCK
 * @bypass_rlim: %true if checking RLIMIT_MEMLOCK should be skipped
 *
 * Assumes @task and @mm are valid (i.e. at least one reference on each), and
 * that mmap_lock is held as writer.
 *
 * Return:
 * * 0       on success
 * * -ENOMEM if RLIMIT_MEMLOCK would be exceeded.
 */
int __account_locked_vm(struct mm_struct *mm, unsigned long pages, bool inc,
			struct task_struct *task, bool bypass_rlim)
{
	unsigned long locked_vm, limit;
	int ret = 0;

	mmap_assert_write_locked(mm);

	locked_vm = mm->locked_vm;
	if (inc) {
		if (!bypass_rlim) {
			limit = task_rlimit(task, RLIMIT_MEMLOCK) >> PAGE_SHIFT;
			if (locked_vm + pages > limit)
				ret = -ENOMEM;
		}
		if (!ret)
			mm->locked_vm = locked_vm + pages;
	} else {
		WARN_ON_ONCE(pages > locked_vm);
		mm->locked_vm = locked_vm - pages;
	}

	pr_debug("%s: [%d] caller %ps %c%lu %lu/%lu%s\n", __func__, task->pid,
		 (void *)_RET_IP_, (inc) ? '+' : '-', pages << PAGE_SHIFT,
		 locked_vm << PAGE_SHIFT, task_rlimit(task, RLIMIT_MEMLOCK),
		 ret ? " - exceeded" : "");

	return ret;
}
EXPORT_SYMBOL_GPL(__account_locked_vm);

/**
 * account_locked_vm - account locked pages to an mm's locked_vm
 * @mm:          mm to account against, may be NULL
 * @pages:       number of pages to account
 * @inc:         %true if @pages should be considered positive, %false if not
 *
 * Assumes a non-NULL @mm is valid (i.e. at least one reference on it).
 *
 * Return:
 * * 0       on success, or if mm is NULL
 * * -ENOMEM if RLIMIT_MEMLOCK would be exceeded.
 */
int account_locked_vm(struct mm_struct *mm, unsigned long pages, bool inc)
{
	int ret;

	if (pages == 0 || !mm)
		return 0;

	mmap_write_lock(mm);
	ret = __account_locked_vm(mm, pages, inc, current,
				  capable(CAP_IPC_LOCK));
	mmap_write_unlock(mm);

	return ret;
}
EXPORT_SYMBOL_GPL(account_locked_vm);

unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff)
{
	unsigned long ret;
	struct mm_struct *mm = current->mm;
	unsigned long populate;
	LIST_HEAD(uf);

	ret = security_mmap_file(file, prot, flag);
	if (!ret) {
		if (mmap_write_lock_killable(mm))
			return -EINTR;
		ret = do_mmap(file, addr, len, prot, flag, 0, pgoff, &populate,
			      &uf);
		mmap_write_unlock(mm);
		userfaultfd_unmap_complete(mm, &uf);
		if (populate)
			mm_populate(ret, populate);
	}
	return ret;
}

unsigned long vm_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	if (unlikely(offset + PAGE_ALIGN(len) < offset))
		return -EINVAL;
	if (unlikely(offset_in_page(offset)))
		return -EINVAL;

	return vm_mmap_pgoff(file, addr, len, prot, flag, offset >> PAGE_SHIFT);
}
EXPORT_SYMBOL(vm_mmap);

static gfp_t kmalloc_gfp_adjust(gfp_t flags, size_t size)
{
	/*
	 * We want to attempt a large physically contiguous block first because
	 * it is less likely to fragment multiple larger blocks and therefore
	 * contribute to a long term fragmentation less than vmalloc fallback.
	 * However make sure that larger requests are not too disruptive - no
	 * OOM killer and no allocation failure warnings as we have a fallback.
	 */
	if (size > PAGE_SIZE) {
		flags |= __GFP_NOWARN;

		if (!(flags & __GFP_RETRY_MAYFAIL))
			flags |= __GFP_NORETRY;

		/* nofail semantic is implemented by the vmalloc fallback */
		flags &= ~__GFP_NOFAIL;
	}

	return flags;
}

/**
 * __kvmalloc_node - attempt to allocate physically contiguous memory, but upon
 * failure, fall back to non-contiguous (vmalloc) allocation.
 * @size: size of the request.
 * @b: which set of kmalloc buckets to allocate from.
 * @flags: gfp mask for the allocation - must be compatible (superset) with GFP_KERNEL.
 * @node: numa node to allocate from
 *
 * Uses kmalloc to get the memory but if the allocation fails then falls back
 * to the vmalloc allocator. Use kvfree for freeing the memory.
 *
 * GFP_NOWAIT and GFP_ATOMIC are not supported, neither is the __GFP_NORETRY modifier.
 * __GFP_RETRY_MAYFAIL is supported, and it should be used only if kmalloc is
 * preferable to the vmalloc fallback, due to visible performance drawbacks.
 *
 * Return: pointer to the allocated memory of %NULL in case of failure
 */
void *__kvmalloc_node_noprof(DECL_BUCKET_PARAMS(size, b), gfp_t flags, int node)
{
	void *ret;

	/*
	 * It doesn't really make sense to fallback to vmalloc for sub page
	 * requests
	 */
	ret = __kmalloc_node_noprof(PASS_BUCKET_PARAMS(size, b),
				    kmalloc_gfp_adjust(flags, size),
				    node);
	if (ret || size <= PAGE_SIZE)
		return ret;

	/* non-sleeping allocations are not supported by vmalloc */
	if (!gfpflags_allow_blocking(flags))
		return NULL;

	/* Don't even allow crazy sizes */
	if (unlikely(size > INT_MAX)) {
		WARN_ON_ONCE(!(flags & __GFP_NOWARN));
		return NULL;
	}

	/*
	 * kvmalloc() can always use VM_ALLOW_HUGE_VMAP,
	 * since the callers already cannot assume anything
	 * about the resulting pointer, and cannot play
	 * protection games.
	 */
	return __vmalloc_node_range_noprof(size, 1, VMALLOC_START, VMALLOC_END,
			flags, PAGE_KERNEL, VM_ALLOW_HUGE_VMAP,
			node, __builtin_return_address(0));
}
EXPORT_SYMBOL(__kvmalloc_node_noprof);

/**
 * kvfree() - Free memory.
 * @addr: Pointer to allocated memory.
 *
 * kvfree frees memory allocated by any of vmalloc(), kmalloc() or kvmalloc().
 * It is slightly more efficient to use kfree() or vfree() if you are certain
 * that you know which one to use.
 *
 * Context: Either preemptible task context or not-NMI interrupt.
 */
void kvfree(const void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}
EXPORT_SYMBOL(kvfree);

/**
 * kvfree_sensitive - Free a data object containing sensitive information.
 * @addr: address of the data object to be freed.
 * @len: length of the data object.
 *
 * Use the special memzero_explicit() function to clear the content of a
 * kvmalloc'ed object containing sensitive data to make sure that the
 * compiler won't optimize out the data clearing.
 */
void kvfree_sensitive(const void *addr, size_t len)
{
	if (likely(!ZERO_OR_NULL_PTR(addr))) {
		memzero_explicit((void *)addr, len);
		kvfree(addr);
	}
}
EXPORT_SYMBOL(kvfree_sensitive);

/**
 * kvrealloc - reallocate memory; contents remain unchanged
 * @p: object to reallocate memory for
 * @size: the size to reallocate
 * @flags: the flags for the page level allocator
 *
 * If @p is %NULL, kvrealloc() behaves exactly like kvmalloc(). If @size is 0
 * and @p is not a %NULL pointer, the object pointed to is freed.
 *
 * If __GFP_ZERO logic is requested, callers must ensure that, starting with the
 * initial memory allocation, every subsequent call to this API for the same
 * memory allocation is flagged with __GFP_ZERO. Otherwise, it is possible that
 * __GFP_ZERO is not fully honored by this API.
 *
 * In any case, the contents of the object pointed to are preserved up to the
 * lesser of the new and old sizes.
 *
 * This function must not be called concurrently with itself or kvfree() for the
 * same memory allocation.
 *
 * Return: pointer to the allocated memory or %NULL in case of error
 */
void *kvrealloc_noprof(const void *p, size_t size, gfp_t flags)
{
	void *n;

	if (is_vmalloc_addr(p))
		return vrealloc_noprof(p, size, flags);

	n = krealloc_noprof(p, size, kmalloc_gfp_adjust(flags, size));
	if (!n) {
		/* We failed to krealloc(), fall back to kvmalloc(). */
		n = kvmalloc_noprof(size, flags);
		if (!n)
			return NULL;

		if (p) {
			/* We already know that `p` is not a vmalloc address. */
			kasan_disable_current();
			memcpy(n, kasan_reset_tag(p), ksize(p));
			kasan_enable_current();

			kfree(p);
		}
	}

	return n;
}
EXPORT_SYMBOL(kvrealloc_noprof);

/**
 * __vmalloc_array - allocate memory for a virtually contiguous array.
 * @n: number of elements.
 * @size: element size.
 * @flags: the type of memory to allocate (see kmalloc).
 */
void *__vmalloc_array_noprof(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;
	return __vmalloc_noprof(bytes, flags);
}
EXPORT_SYMBOL(__vmalloc_array_noprof);

/**
 * vmalloc_array - allocate memory for a virtually contiguous array.
 * @n: number of elements.
 * @size: element size.
 */
void *vmalloc_array_noprof(size_t n, size_t size)
{
	return __vmalloc_array_noprof(n, size, GFP_KERNEL);
}
EXPORT_SYMBOL(vmalloc_array_noprof);

/**
 * __vcalloc - allocate and zero memory for a virtually contiguous array.
 * @n: number of elements.
 * @size: element size.
 * @flags: the type of memory to allocate (see kmalloc).
 */
void *__vcalloc_noprof(size_t n, size_t size, gfp_t flags)
{
	return __vmalloc_array_noprof(n, size, flags | __GFP_ZERO);
}
EXPORT_SYMBOL(__vcalloc_noprof);

/**
 * vcalloc - allocate and zero memory for a virtually contiguous array.
 * @n: number of elements.
 * @size: element size.
 */
void *vcalloc_noprof(size_t n, size_t size)
{
	return __vmalloc_array_noprof(n, size, GFP_KERNEL | __GFP_ZERO);
}
EXPORT_SYMBOL(vcalloc_noprof);

struct anon_vma *folio_anon_vma(const struct folio *folio)
{
	unsigned long mapping = (unsigned long)folio->mapping;

	if ((mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_ANON)
		return NULL;
	return (void *)(mapping - PAGE_MAPPING_ANON);
}

/**
 * folio_mapping - Find the mapping where this folio is stored.
 * @folio: The folio.
 *
 * For folios which are in the page cache, return the mapping that this
 * page belongs to.  Folios in the swap cache return the swap mapping
 * this page is stored in (which is different from the mapping for the
 * swap file or swap device where the data is stored).
 *
 * You can call this for folios which aren't in the swap cache or page
 * cache and it will return NULL.
 */
struct address_space *folio_mapping(struct folio *folio)
{
	struct address_space *mapping;

	/* This happens if someone calls flush_dcache_page on slab page */
	if (unlikely(folio_test_slab(folio)))
		return NULL;

	if (unlikely(folio_test_swapcache(folio)))
		return swap_address_space(folio->swap);

	mapping = folio->mapping;
	if ((unsigned long)mapping & PAGE_MAPPING_FLAGS)
		return NULL;

	return mapping;
}
EXPORT_SYMBOL(folio_mapping);

/**
 * folio_copy - Copy the contents of one folio to another.
 * @dst: Folio to copy to.
 * @src: Folio to copy from.
 *
 * The bytes in the folio represented by @src are copied to @dst.
 * Assumes the caller has validated that @dst is at least as large as @src.
 * Can be called in atomic context for order-0 folios, but if the folio is
 * larger, it may sleep.
 */
void folio_copy(struct folio *dst, struct folio *src)
{
	long i = 0;
	long nr = folio_nr_pages(src);

	for (;;) {
		copy_highpage(folio_page(dst, i), folio_page(src, i));
		if (++i == nr)
			break;
		cond_resched();
	}
}
EXPORT_SYMBOL(folio_copy);

int folio_mc_copy(struct folio *dst, struct folio *src)
{
	long nr = folio_nr_pages(src);
	long i = 0;

	for (;;) {
		if (copy_mc_highpage(folio_page(dst, i), folio_page(src, i)))
			return -EHWPOISON;
		if (++i == nr)
			break;
		cond_resched();
	}

	return 0;
}
EXPORT_SYMBOL(folio_mc_copy);

int sysctl_overcommit_memory __read_mostly = OVERCOMMIT_GUESS;
int sysctl_overcommit_ratio __read_mostly = 50;
unsigned long sysctl_overcommit_kbytes __read_mostly;
int sysctl_max_map_count __read_mostly = DEFAULT_MAX_MAP_COUNT;
unsigned long sysctl_user_reserve_kbytes __read_mostly = 1UL << 17; /* 128MB */
unsigned long sysctl_admin_reserve_kbytes __read_mostly = 1UL << 13; /* 8MB */

int overcommit_ratio_handler(const struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret == 0 && write)
		sysctl_overcommit_kbytes = 0;
	return ret;
}

static void sync_overcommit_as(struct work_struct *dummy)
{
	percpu_counter_sync(&vm_committed_as);
}

int overcommit_policy_handler(const struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	int new_policy = -1;
	int ret;

	/*
	 * The deviation of sync_overcommit_as could be big with loose policy
	 * like OVERCOMMIT_ALWAYS/OVERCOMMIT_GUESS. When changing policy to
	 * strict OVERCOMMIT_NEVER, we need to reduce the deviation to comply
	 * with the strict "NEVER", and to avoid possible race condition (even
	 * though user usually won't too frequently do the switching to policy
	 * OVERCOMMIT_NEVER), the switch is done in the following order:
	 *	1. changing the batch
	 *	2. sync percpu count on each CPU
	 *	3. switch the policy
	 */
	if (write) {
		t = *table;
		t.data = &new_policy;
		ret = proc_dointvec_minmax(&t, write, buffer, lenp, ppos);
		if (ret || new_policy == -1)
			return ret;

		mm_compute_batch(new_policy);
		if (new_policy == OVERCOMMIT_NEVER)
			schedule_on_each_cpu(sync_overcommit_as);
		sysctl_overcommit_memory = new_policy;
	} else {
		ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	}

	return ret;
}

int overcommit_kbytes_handler(const struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write)
		sysctl_overcommit_ratio = 0;
	return ret;
}

/*
 * Committed memory limit enforced when OVERCOMMIT_NEVER policy is used
 */
unsigned long vm_commit_limit(void)
{
	unsigned long allowed;

	if (sysctl_overcommit_kbytes)
		allowed = sysctl_overcommit_kbytes >> (PAGE_SHIFT - 10);
	else
		allowed = ((totalram_pages() - hugetlb_total_pages())
			   * sysctl_overcommit_ratio / 100);
	allowed += total_swap_pages;

	return allowed;
}

/*
 * Make sure vm_committed_as in one cacheline and not cacheline shared with
 * other variables. It can be updated by several CPUs frequently.
 */
struct percpu_counter vm_committed_as ____cacheline_aligned_in_smp;

/*
 * The global memory commitment made in the system can be a metric
 * that can be used to drive ballooning decisions when Linux is hosted
 * as a guest. On Hyper-V, the host implements a policy engine for dynamically
 * balancing memory across competing virtual machines that are hosted.
 * Several metrics drive this policy engine including the guest reported
 * memory commitment.
 *
 * The time cost of this is very low for small platforms, and for big
 * platform like a 2S/36C/72T Skylake server, in worst case where
 * vm_committed_as's spinlock is under severe contention, the time cost
 * could be about 30~40 microseconds.
 */
unsigned long vm_memory_committed(void)
{
	return percpu_counter_sum_positive(&vm_committed_as);
}
EXPORT_SYMBOL_GPL(vm_memory_committed);

/*
 * Check that a process has enough memory to allocate a new virtual
 * mapping. 0 means there is enough memory for the allocation to
 * succeed and -ENOMEM implies there is not.
 *
 * We currently support three overcommit policies, which are set via the
 * vm.overcommit_memory sysctl.  See Documentation/mm/overcommit-accounting.rst
 *
 * Strict overcommit modes added 2002 Feb 26 by Alan Cox.
 * Additional code 2002 Jul 20 by Robert Love.
 *
 * cap_sys_admin is 1 if the process has admin privileges, 0 otherwise.
 *
 * Note this is a helper function intended to be used by LSMs which
 * wish to use this logic.
 */
int __vm_enough_memory(struct mm_struct *mm, long pages, int cap_sys_admin)
{
	long allowed;
	unsigned long bytes_failed;

	vm_acct_memory(pages);

	/*
	 * Sometimes we want to use more memory than we have
	 */
	if (sysctl_overcommit_memory == OVERCOMMIT_ALWAYS)
		return 0;

	if (sysctl_overcommit_memory == OVERCOMMIT_GUESS) {
		if (pages > totalram_pages() + total_swap_pages)
			goto error;
		return 0;
	}

	allowed = vm_commit_limit();
	/*
	 * Reserve some for root
	 */
	if (!cap_sys_admin)
		allowed -= sysctl_admin_reserve_kbytes >> (PAGE_SHIFT - 10);

	/*
	 * Don't let a single process grow so big a user can't recover
	 */
	if (mm) {
		long reserve = sysctl_user_reserve_kbytes >> (PAGE_SHIFT - 10);

		allowed -= min_t(long, mm->total_vm / 32, reserve);
	}

	if (percpu_counter_read_positive(&vm_committed_as) < allowed)
		return 0;
error:
	bytes_failed = pages << PAGE_SHIFT;
	pr_warn_ratelimited("%s: pid: %d, comm: %s, bytes: %lu not enough memory for the allocation\n",
			    __func__, current->pid, current->comm, bytes_failed);
	vm_unacct_memory(pages);

	return -ENOMEM;
}

/**
 * get_cmdline() - copy the cmdline value to a buffer.
 * @task:     the task whose cmdline value to copy.
 * @buffer:   the buffer to copy to.
 * @buflen:   the length of the buffer. Larger cmdline values are truncated
 *            to this length.
 *
 * Return: the size of the cmdline field copied. Note that the copy does
 * not guarantee an ending NULL byte.
 */
int get_cmdline(struct task_struct *task, char *buffer, int buflen)
{
	int res = 0;
	unsigned int len;
	struct mm_struct *mm = get_task_mm(task);
	unsigned long arg_start, arg_end, env_start, env_end;
	if (!mm)
		goto out;
	if (!mm->arg_end)
		goto out_mm;	/* Shh! No looking before we're done */

	spin_lock(&mm->arg_lock);
	arg_start = mm->arg_start;
	arg_end = mm->arg_end;
	env_start = mm->env_start;
	env_end = mm->env_end;
	spin_unlock(&mm->arg_lock);

	len = arg_end - arg_start;

	if (len > buflen)
		len = buflen;

	res = access_process_vm(task, arg_start, buffer, len, FOLL_FORCE);

	/*
	 * If the nul at the end of args has been overwritten, then
	 * assume application is using setproctitle(3).
	 */
	if (res > 0 && buffer[res-1] != '\0' && len < buflen) {
		len = strnlen(buffer, res);
		if (len < res) {
			res = len;
		} else {
			len = env_end - env_start;
			if (len > buflen - res)
				len = buflen - res;
			res += access_process_vm(task, env_start,
						 buffer+res, len,
						 FOLL_FORCE);
			res = strnlen(buffer, res);
		}
	}
out_mm:
	mmput(mm);
out:
	return res;
}

int __weak memcmp_pages(struct page *page1, struct page *page2)
{
	char *addr1, *addr2;
	int ret;

	addr1 = kmap_local_page(page1);
	addr2 = kmap_local_page(page2);
	ret = memcmp(addr1, addr2, PAGE_SIZE);
	kunmap_local(addr2);
	kunmap_local(addr1);
	return ret;
}

#ifdef CONFIG_PRINTK
/**
 * mem_dump_obj - Print available provenance information
 * @object: object for which to find provenance information.
 *
 * This function uses pr_cont(), so that the caller is expected to have
 * printed out whatever preamble is appropriate.  The provenance information
 * depends on the type of object and on how much debugging is enabled.
 * For example, for a slab-cache object, the slab name is printed, and,
 * if available, the return address and stack trace from the allocation
 * and last free path of that object.
 */
void mem_dump_obj(void *object)
{
	const char *type;

	if (kmem_dump_obj(object))
		return;

	if (vmalloc_dump_obj(object))
		return;

	if (is_vmalloc_addr(object))
		type = "vmalloc memory";
	else if (virt_addr_valid(object))
		type = "non-slab/vmalloc memory";
	else if (object == NULL)
		type = "NULL pointer";
	else if (object == ZERO_SIZE_PTR)
		type = "zero-size pointer";
	else
		type = "non-paged memory";

	pr_cont(" %s\n", type);
}
EXPORT_SYMBOL_GPL(mem_dump_obj);
#endif

/*
 * A driver might set a page logically offline -- PageOffline() -- and
 * turn the page inaccessible in the hypervisor; after that, access to page
 * content can be fatal.
 *
 * Some special PFN walkers -- i.e., /proc/kcore -- read content of random
 * pages after checking PageOffline(); however, these PFN walkers can race
 * with drivers that set PageOffline().
 *
 * page_offline_freeze()/page_offline_thaw() allows for a subsystem to
 * synchronize with such drivers, achieving that a page cannot be set
 * PageOffline() while frozen.
 *
 * page_offline_begin()/page_offline_end() is used by drivers that care about
 * such races when setting a page PageOffline().
 */
static DECLARE_RWSEM(page_offline_rwsem);

void page_offline_freeze(void)
{
	down_read(&page_offline_rwsem);
}

void page_offline_thaw(void)
{
	up_read(&page_offline_rwsem);
}

void page_offline_begin(void)
{
	down_write(&page_offline_rwsem);
}
EXPORT_SYMBOL(page_offline_begin);

void page_offline_end(void)
{
	up_write(&page_offline_rwsem);
}
EXPORT_SYMBOL(page_offline_end);

#ifndef flush_dcache_folio
void flush_dcache_folio(struct folio *folio)
{
	long i, nr = folio_nr_pages(folio);

	for (i = 0; i < nr; i++)
		flush_dcache_page(folio_page(folio, i));
}
EXPORT_SYMBOL(flush_dcache_folio);
#endif
