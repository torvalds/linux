/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also entry.S and others).
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/memory.c': 'copy_page_range()'
 */

#include <linux/anon_inodes.h>
#include <linux/slab.h>
#include <linux/sched/autogroup.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/user.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/stat.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/seq_file.h>
#include <linux/rtmutex.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/personality.h>
#include <linux/mempolicy.h>
#include <linux/sem.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/iocontext.h>
#include <linux/key.h>
#include <linux/binfmts.h>
#include <linux/mman.h>
#include <linux/mmu_notifier.h>
#include <linux/hmm.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/nsproxy.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/cgroup.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/seccomp.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/jiffies.h>
#include <linux/futex.h>
#include <linux/compat.h>
#include <linux/kthread.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/rcupdate.h>
#include <linux/ptrace.h>
#include <linux/mount.h>
#include <linux/audit.h>
#include <linux/memcontrol.h>
#include <linux/ftrace.h>
#include <linux/proc_fs.h>
#include <linux/profile.h>
#include <linux/rmap.h>
#include <linux/ksm.h>
#include <linux/acct.h>
#include <linux/userfaultfd_k.h>
#include <linux/tsacct_kern.h>
#include <linux/cn_proc.h>
#include <linux/freezer.h>
#include <linux/delayacct.h>
#include <linux/taskstats_kern.h>
#include <linux/random.h>
#include <linux/tty.h>
#include <linux/blkdev.h>
#include <linux/fs_struct.h>
#include <linux/magic.h>
#include <linux/sched/mm.h>
#include <linux/perf_event.h>
#include <linux/posix-timers.h>
#include <linux/user-return-notifier.h>
#include <linux/oom.h>
#include <linux/khugepaged.h>
#include <linux/signalfd.h>
#include <linux/uprobes.h>
#include <linux/aio.h>
#include <linux/compiler.h>
#include <linux/sysctl.h>
#include <linux/kcov.h>
#include <linux/livepatch.h>
#include <linux/thread_info.h>
#include <linux/cpufreq_times.h>
#include <linux/scs.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include <trace/events/sched.h>

#define CREATE_TRACE_POINTS
#include <trace/events/task.h>

/*
 * Minimum number of threads to boot the kernel
 */
#define MIN_THREADS 20

/*
 * Maximum number of threads
 */
#define MAX_THREADS FUTEX_TID_MASK

/*
 * Protected counters by write_lock_irq(&tasklist_lock)
 */
unsigned long total_forks;	/* Handle normal Linux uptimes. */
int nr_threads;			/* The idle threads do not count.. */

int max_threads;		/* tunable limit on nr_threads */

DEFINE_PER_CPU(unsigned long, process_counts) = 0;

__cacheline_aligned DEFINE_RWLOCK(tasklist_lock);  /* outer */

#ifdef CONFIG_PROVE_RCU
int lockdep_tasklist_lock_is_held(void)
{
	return lockdep_is_held(&tasklist_lock);
}
EXPORT_SYMBOL_GPL(lockdep_tasklist_lock_is_held);
#endif /* #ifdef CONFIG_PROVE_RCU */

int nr_processes(void)
{
	int cpu;
	int total = 0;

	for_each_possible_cpu(cpu)
		total += per_cpu(process_counts, cpu);

	return total;
}

void __weak arch_release_task_struct(struct task_struct *tsk)
{
}

#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
static struct kmem_cache *task_struct_cachep;

static inline struct task_struct *alloc_task_struct_node(int node)
{
	return kmem_cache_alloc_node(task_struct_cachep, GFP_KERNEL, node);
}

static inline void free_task_struct(struct task_struct *tsk)
{
	kmem_cache_free(task_struct_cachep, tsk);
}
#endif

#ifndef CONFIG_ARCH_THREAD_STACK_ALLOCATOR

/*
 * Allocate pages if THREAD_SIZE is >= PAGE_SIZE, otherwise use a
 * kmemcache based allocator.
 */
# if THREAD_SIZE >= PAGE_SIZE || defined(CONFIG_VMAP_STACK)

#ifdef CONFIG_VMAP_STACK
/*
 * vmalloc() is a bit slow, and calling vfree() enough times will force a TLB
 * flush.  Try to minimize the number of calls by caching stacks.
 */
#define NR_CACHED_STACKS 2
static DEFINE_PER_CPU(struct vm_struct *, cached_stacks[NR_CACHED_STACKS]);

static int free_vm_stack_cache(unsigned int cpu)
{
	struct vm_struct **cached_vm_stacks = per_cpu_ptr(cached_stacks, cpu);
	int i;

	for (i = 0; i < NR_CACHED_STACKS; i++) {
		struct vm_struct *vm_stack = cached_vm_stacks[i];

		if (!vm_stack)
			continue;

		vfree(vm_stack->addr);
		cached_vm_stacks[i] = NULL;
	}

	return 0;
}
#endif

static unsigned long *alloc_thread_stack_node(struct task_struct *tsk, int node)
{
#ifdef CONFIG_VMAP_STACK
	void *stack;
	int i;

	for (i = 0; i < NR_CACHED_STACKS; i++) {
		struct vm_struct *s;

		s = this_cpu_xchg(cached_stacks[i], NULL);

		if (!s)
			continue;

		/* Clear stale pointers from reused stack. */
		memset(s->addr, 0, THREAD_SIZE);

		tsk->stack_vm_area = s;
		tsk->stack = s->addr;
		return s->addr;
	}

	stack = __vmalloc_node_range(THREAD_SIZE, THREAD_ALIGN,
				     VMALLOC_START, VMALLOC_END,
				     THREADINFO_GFP,
				     PAGE_KERNEL,
				     0, node, __builtin_return_address(0));

	/*
	 * We can't call find_vm_area() in interrupt context, and
	 * free_thread_stack() can be called in interrupt context,
	 * so cache the vm_struct.
	 */
	if (stack) {
		tsk->stack_vm_area = find_vm_area(stack);
		tsk->stack = stack;
	}
	return stack;
#else
	struct page *page = alloc_pages_node(node, THREADINFO_GFP,
					     THREAD_SIZE_ORDER);

	if (likely(page)) {
		tsk->stack = page_address(page);
		return tsk->stack;
	}
	return NULL;
#endif
}

static inline void free_thread_stack(struct task_struct *tsk)
{
#ifdef CONFIG_VMAP_STACK
	if (task_stack_vm_area(tsk)) {
		int i;

		for (i = 0; i < NR_CACHED_STACKS; i++) {
			if (this_cpu_cmpxchg(cached_stacks[i],
					NULL, tsk->stack_vm_area) != NULL)
				continue;

			return;
		}

		vfree_atomic(tsk->stack);
		return;
	}
#endif

	__free_pages(virt_to_page(tsk->stack), THREAD_SIZE_ORDER);
}
# else
static struct kmem_cache *thread_stack_cache;

static unsigned long *alloc_thread_stack_node(struct task_struct *tsk,
						  int node)
{
	unsigned long *stack;
	stack = kmem_cache_alloc_node(thread_stack_cache, THREADINFO_GFP, node);
	tsk->stack = stack;
	return stack;
}

static void free_thread_stack(struct task_struct *tsk)
{
	kmem_cache_free(thread_stack_cache, tsk->stack);
}

void thread_stack_cache_init(void)
{
	thread_stack_cache = kmem_cache_create_usercopy("thread_stack",
					THREAD_SIZE, THREAD_SIZE, 0, 0,
					THREAD_SIZE, NULL);
	BUG_ON(thread_stack_cache == NULL);
}
# endif
#endif

/* SLAB cache for signal_struct structures (tsk->signal) */
static struct kmem_cache *signal_cachep;

/* SLAB cache for sighand_struct structures (tsk->sighand) */
struct kmem_cache *sighand_cachep;

/* SLAB cache for files_struct structures (tsk->files) */
struct kmem_cache *files_cachep;

/* SLAB cache for fs_struct structures (tsk->fs) */
struct kmem_cache *fs_cachep;

/* SLAB cache for vm_area_struct structures */
static struct kmem_cache *vm_area_cachep;

/* SLAB cache for mm_struct structures (tsk->mm) */
static struct kmem_cache *mm_cachep;

struct vm_area_struct *vm_area_alloc(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	vma = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (vma)
		vma_init(vma, mm);
	return vma;
}

struct vm_area_struct *vm_area_dup(struct vm_area_struct *orig)
{
	struct vm_area_struct *new = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);

	if (new) {
		*new = *orig;
		INIT_LIST_HEAD(&new->anon_vma_chain);
	}
	return new;
}

void vm_area_free(struct vm_area_struct *vma)
{
	kmem_cache_free(vm_area_cachep, vma);
}

static void account_kernel_stack(struct task_struct *tsk, int account)
{
	void *stack = task_stack_page(tsk);
	struct vm_struct *vm = task_stack_vm_area(tsk);

	BUILD_BUG_ON(IS_ENABLED(CONFIG_VMAP_STACK) && PAGE_SIZE % 1024 != 0);

	if (vm) {
		int i;

		BUG_ON(vm->nr_pages != THREAD_SIZE / PAGE_SIZE);

		for (i = 0; i < THREAD_SIZE / PAGE_SIZE; i++) {
			mod_zone_page_state(page_zone(vm->pages[i]),
					    NR_KERNEL_STACK_KB,
					    PAGE_SIZE / 1024 * account);
		}

		/* All stack pages belong to the same memcg. */
		mod_memcg_page_state(vm->pages[0], MEMCG_KERNEL_STACK_KB,
				     account * (THREAD_SIZE / 1024));
	} else {
		/*
		 * All stack pages are in the same zone and belong to the
		 * same memcg.
		 */
		struct page *first_page = virt_to_page(stack);

		mod_zone_page_state(page_zone(first_page), NR_KERNEL_STACK_KB,
				    THREAD_SIZE / 1024 * account);

		mod_memcg_page_state(first_page, MEMCG_KERNEL_STACK_KB,
				     account * (THREAD_SIZE / 1024));
	}
}

static void release_task_stack(struct task_struct *tsk)
{
	if (WARN_ON(tsk->state != TASK_DEAD))
		return;  /* Better to leak the stack than to free prematurely */

	account_kernel_stack(tsk, -1);
	free_thread_stack(tsk);
	tsk->stack = NULL;
#ifdef CONFIG_VMAP_STACK
	tsk->stack_vm_area = NULL;
#endif
}

#ifdef CONFIG_THREAD_INFO_IN_TASK
void put_task_stack(struct task_struct *tsk)
{
	if (atomic_dec_and_test(&tsk->stack_refcount))
		release_task_stack(tsk);
}
#endif

void free_task(struct task_struct *tsk)
{
	cpufreq_task_times_exit(tsk);
	scs_release(tsk);

#ifndef CONFIG_THREAD_INFO_IN_TASK
	/*
	 * The task is finally done with both the stack and thread_info,
	 * so free both.
	 */
	release_task_stack(tsk);
#else
	/*
	 * If the task had a separate stack allocation, it should be gone
	 * by now.
	 */
	WARN_ON_ONCE(atomic_read(&tsk->stack_refcount) != 0);
#endif
	rt_mutex_debug_task_free(tsk);
	ftrace_graph_exit_task(tsk);
	put_seccomp_filter(tsk);
	arch_release_task_struct(tsk);
	if (tsk->flags & PF_KTHREAD)
		free_kthread_struct(tsk);
	free_task_struct(tsk);
}
EXPORT_SYMBOL(free_task);

#ifdef CONFIG_MMU
static __latent_entropy int dup_mmap(struct mm_struct *mm,
					struct mm_struct *oldmm)
{
	struct vm_area_struct *mpnt, *tmp, *prev, **pprev;
	struct rb_node **rb_link, *rb_parent;
	int retval;
	unsigned long charge;
	LIST_HEAD(uf);

	uprobe_start_dup_mmap();
	if (down_write_killable(&oldmm->mmap_sem)) {
		retval = -EINTR;
		goto fail_uprobe_end;
	}
	flush_cache_dup_mm(oldmm);
	uprobe_dup_mmap(oldmm, mm);
	/*
	 * Not linked in yet - no deadlock potential:
	 */
	down_write_nested(&mm->mmap_sem, SINGLE_DEPTH_NESTING);

	/* No ordering required: file already has been exposed. */
	RCU_INIT_POINTER(mm->exe_file, get_mm_exe_file(oldmm));

	mm->total_vm = oldmm->total_vm;
	mm->data_vm = oldmm->data_vm;
	mm->exec_vm = oldmm->exec_vm;
	mm->stack_vm = oldmm->stack_vm;

	rb_link = &mm->mm_rb.rb_node;
	rb_parent = NULL;
	pprev = &mm->mmap;
	retval = ksm_fork(mm, oldmm);
	if (retval)
		goto out;
	retval = khugepaged_fork(mm, oldmm);
	if (retval)
		goto out;

	prev = NULL;
	for (mpnt = oldmm->mmap; mpnt; mpnt = mpnt->vm_next) {
		struct file *file;

		if (mpnt->vm_flags & VM_DONTCOPY) {
			vm_stat_account(mm, mpnt->vm_flags, -vma_pages(mpnt));
			continue;
		}
		charge = 0;
		/*
		 * Don't duplicate many vmas if we've been oom-killed (for
		 * example)
		 */
		if (fatal_signal_pending(current)) {
			retval = -EINTR;
			goto out;
		}
		if (mpnt->vm_flags & VM_ACCOUNT) {
			unsigned long len = vma_pages(mpnt);

			if (security_vm_enough_memory_mm(oldmm, len)) /* sic */
				goto fail_nomem;
			charge = len;
		}
		tmp = vm_area_dup(mpnt);
		if (!tmp)
			goto fail_nomem;
		retval = vma_dup_policy(mpnt, tmp);
		if (retval)
			goto fail_nomem_policy;
		tmp->vm_mm = mm;
		retval = dup_userfaultfd(tmp, &uf);
		if (retval)
			goto fail_nomem_anon_vma_fork;
		if (tmp->vm_flags & VM_WIPEONFORK) {
			/* VM_WIPEONFORK gets a clean slate in the child. */
			tmp->anon_vma = NULL;
			if (anon_vma_prepare(tmp))
				goto fail_nomem_anon_vma_fork;
		} else if (anon_vma_fork(tmp, mpnt))
			goto fail_nomem_anon_vma_fork;
		tmp->vm_flags &= ~(VM_LOCKED | VM_LOCKONFAULT);
		tmp->vm_next = tmp->vm_prev = NULL;
		file = tmp->vm_file;
		if (file) {
			struct inode *inode = file_inode(file);
			struct address_space *mapping = file->f_mapping;

			get_file(file);
			if (tmp->vm_flags & VM_DENYWRITE)
				atomic_dec(&inode->i_writecount);
			i_mmap_lock_write(mapping);
			if (tmp->vm_flags & VM_SHARED)
				atomic_inc(&mapping->i_mmap_writable);
			flush_dcache_mmap_lock(mapping);
			/* insert tmp into the share list, just after mpnt */
			vma_interval_tree_insert_after(tmp, mpnt,
					&mapping->i_mmap);
			flush_dcache_mmap_unlock(mapping);
			i_mmap_unlock_write(mapping);
		}

		/*
		 * Clear hugetlb-related page reserves for children. This only
		 * affects MAP_PRIVATE mappings. Faults generated by the child
		 * are not guaranteed to succeed, even if read-only
		 */
		if (is_vm_hugetlb_page(tmp))
			reset_vma_resv_huge_pages(tmp);

		/*
		 * Link in the new vma and copy the page table entries.
		 */
		*pprev = tmp;
		pprev = &tmp->vm_next;
		tmp->vm_prev = prev;
		prev = tmp;

		__vma_link_rb(mm, tmp, rb_link, rb_parent);
		rb_link = &tmp->vm_rb.rb_right;
		rb_parent = &tmp->vm_rb;

		mm->map_count++;
		if (!(tmp->vm_flags & VM_WIPEONFORK))
			retval = copy_page_range(mm, oldmm, mpnt);

		if (tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);

		if (retval)
			goto out;
	}
	/* a new mm has just been created */
	retval = arch_dup_mmap(oldmm, mm);
out:
	up_write(&mm->mmap_sem);
	flush_tlb_mm(oldmm);
	up_write(&oldmm->mmap_sem);
	dup_userfaultfd_complete(&uf);
fail_uprobe_end:
	uprobe_end_dup_mmap();
	return retval;
fail_nomem_anon_vma_fork:
	mpol_put(vma_policy(tmp));
fail_nomem_policy:
	vm_area_free(tmp);
fail_nomem:
	retval = -ENOMEM;
	vm_unacct_memory(charge);
	goto out;
}

static inline int mm_alloc_pgd(struct mm_struct *mm)
{
	mm->pgd = pgd_alloc(mm);
	if (unlikely(!mm->pgd))
		return -ENOMEM;
	return 0;
}

static inline void mm_free_pgd(struct mm_struct *mm)
{
	pgd_free(mm, mm->pgd);
}
#else
static int dup_mmap(struct mm_struct *mm, struct mm_struct *oldmm)
{
	down_write(&oldmm->mmap_sem);
	RCU_INIT_POINTER(mm->exe_file, get_mm_exe_file(oldmm));
	up_write(&oldmm->mmap_sem);
	return 0;
}
#define mm_alloc_pgd(mm)	(0)
#define mm_free_pgd(mm)
#endif /* CONFIG_MMU */

static void check_mm(struct mm_struct *mm)
{
	int i;

	for (i = 0; i < NR_MM_COUNTERS; i++) {
		long x = atomic_long_read(&mm->rss_stat.count[i]);

		if (unlikely(x))
			printk(KERN_ALERT "BUG: Bad rss-counter state "
					  "mm:%p idx:%d val:%ld\n", mm, i, x);
	}

	if (mm_pgtables_bytes(mm))
		pr_alert("BUG: non-zero pgtables_bytes on freeing mm: %ld\n",
				mm_pgtables_bytes(mm));

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
	VM_BUG_ON_MM(mm->pmd_huge_pte, mm);
#endif
}

#define allocate_mm()	(kmem_cache_alloc(mm_cachep, GFP_KERNEL))
#define free_mm(mm)	(kmem_cache_free(mm_cachep, (mm)))

/*
 * Called when the last reference to the mm
 * is dropped: either by a lazy thread or by
 * mmput. Free the page directory and the mm.
 */
void __mmdrop(struct mm_struct *mm)
{
	BUG_ON(mm == &init_mm);
	WARN_ON_ONCE(mm == current->mm);
	WARN_ON_ONCE(mm == current->active_mm);
	mm_free_pgd(mm);
	destroy_context(mm);
	hmm_mm_destroy(mm);
	mmu_notifier_mm_destroy(mm);
	check_mm(mm);
	put_user_ns(mm->user_ns);
	free_mm(mm);
}
EXPORT_SYMBOL_GPL(__mmdrop);

static void mmdrop_async_fn(struct work_struct *work)
{
	struct mm_struct *mm;

	mm = container_of(work, struct mm_struct, async_put_work);
	__mmdrop(mm);
}

static void mmdrop_async(struct mm_struct *mm)
{
	if (unlikely(atomic_dec_and_test(&mm->mm_count))) {
		INIT_WORK(&mm->async_put_work, mmdrop_async_fn);
		schedule_work(&mm->async_put_work);
	}
}

static inline void free_signal_struct(struct signal_struct *sig)
{
	taskstats_tgid_free(sig);
	sched_autogroup_exit(sig);
	/*
	 * __mmdrop is not safe to call from softirq context on x86 due to
	 * pgd_dtor so postpone it to the async context
	 */
	if (sig->oom_mm)
		mmdrop_async(sig->oom_mm);
	kmem_cache_free(signal_cachep, sig);
}

static inline void put_signal_struct(struct signal_struct *sig)
{
	if (atomic_dec_and_test(&sig->sigcnt))
		free_signal_struct(sig);
}

void __put_task_struct(struct task_struct *tsk)
{
	WARN_ON(!tsk->exit_state);
	WARN_ON(atomic_read(&tsk->usage));
	WARN_ON(tsk == current);

	cgroup_free(tsk);
	task_numa_free(tsk, true);
	security_task_free(tsk);
	exit_creds(tsk);
	delayacct_tsk_free(tsk);
	put_signal_struct(tsk->signal);

	if (!profile_handoff_task(tsk))
		free_task(tsk);
}
EXPORT_SYMBOL_GPL(__put_task_struct);

void __init __weak arch_task_cache_init(void) { }

/*
 * set_max_threads
 */
static void set_max_threads(unsigned int max_threads_suggested)
{
	u64 threads;

	/*
	 * The number of threads shall be limited such that the thread
	 * structures may only consume a small part of the available memory.
	 */
	if (fls64(totalram_pages) + fls64(PAGE_SIZE) > 64)
		threads = MAX_THREADS;
	else
		threads = div64_u64((u64) totalram_pages * (u64) PAGE_SIZE,
				    (u64) THREAD_SIZE * 8UL);

	if (threads > max_threads_suggested)
		threads = max_threads_suggested;

	max_threads = clamp_t(u64, threads, MIN_THREADS, MAX_THREADS);
}

#ifdef CONFIG_ARCH_WANTS_DYNAMIC_TASK_STRUCT
/* Initialized by the architecture: */
int arch_task_struct_size __read_mostly;
#endif

static void task_struct_whitelist(unsigned long *offset, unsigned long *size)
{
	/* Fetch thread_struct whitelist for the architecture. */
	arch_thread_struct_whitelist(offset, size);

	/*
	 * Handle zero-sized whitelist or empty thread_struct, otherwise
	 * adjust offset to position of thread_struct in task_struct.
	 */
	if (unlikely(*size == 0))
		*offset = 0;
	else
		*offset += offsetof(struct task_struct, thread);
}

void __init fork_init(void)
{
	int i;
#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
#ifndef ARCH_MIN_TASKALIGN
#define ARCH_MIN_TASKALIGN	0
#endif
	int align = max_t(int, L1_CACHE_BYTES, ARCH_MIN_TASKALIGN);
	unsigned long useroffset, usersize;

	/* create a slab on which task_structs can be allocated */
	task_struct_whitelist(&useroffset, &usersize);
	task_struct_cachep = kmem_cache_create_usercopy("task_struct",
			arch_task_struct_size, align,
			SLAB_PANIC|SLAB_ACCOUNT,
			useroffset, usersize, NULL);
#endif

	/* do the arch specific task caches init */
	arch_task_cache_init();

	set_max_threads(MAX_THREADS);

	init_task.signal->rlim[RLIMIT_NPROC].rlim_cur = max_threads/2;
	init_task.signal->rlim[RLIMIT_NPROC].rlim_max = max_threads/2;
	init_task.signal->rlim[RLIMIT_SIGPENDING] =
		init_task.signal->rlim[RLIMIT_NPROC];

	for (i = 0; i < UCOUNT_COUNTS; i++) {
		init_user_ns.ucount_max[i] = max_threads/2;
	}

#ifdef CONFIG_VMAP_STACK
	cpuhp_setup_state(CPUHP_BP_PREPARE_DYN, "fork:vm_stack_cache",
			  NULL, free_vm_stack_cache);
#endif

	scs_init();

	lockdep_init_task(&init_task);
}

int __weak arch_dup_task_struct(struct task_struct *dst,
					       struct task_struct *src)
{
	*dst = *src;
	return 0;
}

void set_task_stack_end_magic(struct task_struct *tsk)
{
	unsigned long *stackend;

	stackend = end_of_stack(tsk);
	*stackend = STACK_END_MAGIC;	/* for overflow detection */
}

static struct task_struct *dup_task_struct(struct task_struct *orig, int node)
{
	struct task_struct *tsk;
	unsigned long *stack;
	struct vm_struct *stack_vm_area;
	int err;

	if (node == NUMA_NO_NODE)
		node = tsk_fork_get_node(orig);
	tsk = alloc_task_struct_node(node);
	if (!tsk)
		return NULL;

	stack = alloc_thread_stack_node(tsk, node);
	if (!stack)
		goto free_tsk;

	stack_vm_area = task_stack_vm_area(tsk);

	err = arch_dup_task_struct(tsk, orig);

	/*
	 * arch_dup_task_struct() clobbers the stack-related fields.  Make
	 * sure they're properly initialized before using any stack-related
	 * functions again.
	 */
	tsk->stack = stack;
#ifdef CONFIG_VMAP_STACK
	tsk->stack_vm_area = stack_vm_area;
#endif
#ifdef CONFIG_THREAD_INFO_IN_TASK
	atomic_set(&tsk->stack_refcount, 1);
#endif

	if (err)
		goto free_stack;

	err = scs_prepare(tsk, node);
	if (err)
		goto free_stack;

#ifdef CONFIG_SECCOMP
	/*
	 * We must handle setting up seccomp filters once we're under
	 * the sighand lock in case orig has changed between now and
	 * then. Until then, filter must be NULL to avoid messing up
	 * the usage counts on the error path calling free_task.
	 */
	tsk->seccomp.filter = NULL;
#endif

	setup_thread_stack(tsk, orig);
	clear_user_return_notifier(tsk);
	clear_tsk_need_resched(tsk);
	set_task_stack_end_magic(tsk);

#ifdef CONFIG_STACKPROTECTOR
	tsk->stack_canary = get_random_canary();
#endif

	/*
	 * One for us, one for whoever does the "release_task()" (usually
	 * parent)
	 */
	atomic_set(&tsk->usage, 2);
#ifdef CONFIG_BLK_DEV_IO_TRACE
	tsk->btrace_seq = 0;
#endif
	tsk->splice_pipe = NULL;
	tsk->task_frag.page = NULL;
	tsk->wake_q.next = NULL;

	account_kernel_stack(tsk, 1);

	kcov_task_init(tsk);

#ifdef CONFIG_FAULT_INJECTION
	tsk->fail_nth = 0;
#endif

#ifdef CONFIG_BLK_CGROUP
	tsk->throttle_queue = NULL;
	tsk->use_memdelay = 0;
#endif

#ifdef CONFIG_MEMCG
	tsk->active_memcg = NULL;
#endif
	return tsk;

free_stack:
	free_thread_stack(tsk);
free_tsk:
	free_task_struct(tsk);
	return NULL;
}

__cacheline_aligned_in_smp DEFINE_SPINLOCK(mmlist_lock);

static unsigned long default_dump_filter = MMF_DUMP_FILTER_DEFAULT;

static int __init coredump_filter_setup(char *s)
{
	default_dump_filter =
		(simple_strtoul(s, NULL, 0) << MMF_DUMP_FILTER_SHIFT) &
		MMF_DUMP_FILTER_MASK;
	return 1;
}

__setup("coredump_filter=", coredump_filter_setup);

#include <linux/init_task.h>

static void mm_init_aio(struct mm_struct *mm)
{
#ifdef CONFIG_AIO
	spin_lock_init(&mm->ioctx_lock);
	mm->ioctx_table = NULL;
#endif
}

static __always_inline void mm_clear_owner(struct mm_struct *mm,
					   struct task_struct *p)
{
#ifdef CONFIG_MEMCG
	if (mm->owner == p)
		WRITE_ONCE(mm->owner, NULL);
#endif
}

static void mm_init_owner(struct mm_struct *mm, struct task_struct *p)
{
#ifdef CONFIG_MEMCG
	mm->owner = p;
#endif
}

static void mm_init_uprobes_state(struct mm_struct *mm)
{
#ifdef CONFIG_UPROBES
	mm->uprobes_state.xol_area = NULL;
#endif
}

static struct mm_struct *mm_init(struct mm_struct *mm, struct task_struct *p,
	struct user_namespace *user_ns)
{
	mm->mmap = NULL;
	mm->mm_rb = RB_ROOT;
	mm->vmacache_seqnum = 0;
	atomic_set(&mm->mm_users, 1);
	atomic_set(&mm->mm_count, 1);
	init_rwsem(&mm->mmap_sem);
	INIT_LIST_HEAD(&mm->mmlist);
	mm->core_state = NULL;
	mm_pgtables_bytes_init(mm);
	mm->map_count = 0;
	mm->locked_vm = 0;
	mm->pinned_vm = 0;
	memset(&mm->rss_stat, 0, sizeof(mm->rss_stat));
	spin_lock_init(&mm->page_table_lock);
	spin_lock_init(&mm->arg_lock);
	mm_init_cpumask(mm);
	mm_init_aio(mm);
	mm_init_owner(mm, p);
	RCU_INIT_POINTER(mm->exe_file, NULL);
	mmu_notifier_mm_init(mm);
	hmm_mm_init(mm);
	init_tlb_flush_pending(mm);
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
	mm->pmd_huge_pte = NULL;
#endif
	mm_init_uprobes_state(mm);

	if (current->mm) {
		mm->flags = current->mm->flags & MMF_INIT_MASK;
		mm->def_flags = current->mm->def_flags & VM_INIT_DEF_MASK;
	} else {
		mm->flags = default_dump_filter;
		mm->def_flags = 0;
	}

	if (mm_alloc_pgd(mm))
		goto fail_nopgd;

	if (init_new_context(p, mm))
		goto fail_nocontext;

	mm->user_ns = get_user_ns(user_ns);
	return mm;

fail_nocontext:
	mm_free_pgd(mm);
fail_nopgd:
	free_mm(mm);
	return NULL;
}

/*
 * Allocate and initialize an mm_struct.
 */
struct mm_struct *mm_alloc(void)
{
	struct mm_struct *mm;

	mm = allocate_mm();
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(*mm));
	return mm_init(mm, current, current_user_ns());
}

static inline void __mmput(struct mm_struct *mm)
{
	VM_BUG_ON(atomic_read(&mm->mm_users));

	uprobe_clear_state(mm);
	exit_aio(mm);
	ksm_exit(mm);
	khugepaged_exit(mm); /* must run before exit_mmap */
	exit_mmap(mm);
	mm_put_huge_zero_page(mm);
	set_mm_exe_file(mm, NULL);
	if (!list_empty(&mm->mmlist)) {
		spin_lock(&mmlist_lock);
		list_del(&mm->mmlist);
		spin_unlock(&mmlist_lock);
	}
	if (mm->binfmt)
		module_put(mm->binfmt->module);
	mmdrop(mm);
}

/*
 * Decrement the use count and release all resources for an mm.
 */
void mmput(struct mm_struct *mm)
{
	might_sleep();

	if (atomic_dec_and_test(&mm->mm_users))
		__mmput(mm);
}
EXPORT_SYMBOL_GPL(mmput);

#ifdef CONFIG_MMU
static void mmput_async_fn(struct work_struct *work)
{
	struct mm_struct *mm = container_of(work, struct mm_struct,
					    async_put_work);

	__mmput(mm);
}

void mmput_async(struct mm_struct *mm)
{
	if (atomic_dec_and_test(&mm->mm_users)) {
		INIT_WORK(&mm->async_put_work, mmput_async_fn);
		schedule_work(&mm->async_put_work);
	}
}
#endif

/**
 * set_mm_exe_file - change a reference to the mm's executable file
 *
 * This changes mm's executable file (shown as symlink /proc/[pid]/exe).
 *
 * Main users are mmput() and sys_execve(). Callers prevent concurrent
 * invocations: in mmput() nobody alive left, in execve task is single
 * threaded. sys_prctl(PR_SET_MM_MAP/EXE_FILE) also needs to set the
 * mm->exe_file, but does so without using set_mm_exe_file() in order
 * to do avoid the need for any locks.
 */
void set_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file)
{
	struct file *old_exe_file;

	/*
	 * It is safe to dereference the exe_file without RCU as
	 * this function is only called if nobody else can access
	 * this mm -- see comment above for justification.
	 */
	old_exe_file = rcu_dereference_raw(mm->exe_file);

	if (new_exe_file)
		get_file(new_exe_file);
	rcu_assign_pointer(mm->exe_file, new_exe_file);
	if (old_exe_file)
		fput(old_exe_file);
}

/**
 * get_mm_exe_file - acquire a reference to the mm's executable file
 *
 * Returns %NULL if mm has no associated executable file.
 * User must release file via fput().
 */
struct file *get_mm_exe_file(struct mm_struct *mm)
{
	struct file *exe_file;

	rcu_read_lock();
	exe_file = rcu_dereference(mm->exe_file);
	if (exe_file && !get_file_rcu(exe_file))
		exe_file = NULL;
	rcu_read_unlock();
	return exe_file;
}
EXPORT_SYMBOL(get_mm_exe_file);

/**
 * get_task_exe_file - acquire a reference to the task's executable file
 *
 * Returns %NULL if task's mm (if any) has no associated executable file or
 * this is a kernel thread with borrowed mm (see the comment above get_task_mm).
 * User must release file via fput().
 */
struct file *get_task_exe_file(struct task_struct *task)
{
	struct file *exe_file = NULL;
	struct mm_struct *mm;

	task_lock(task);
	mm = task->mm;
	if (mm) {
		if (!(task->flags & PF_KTHREAD))
			exe_file = get_mm_exe_file(mm);
	}
	task_unlock(task);
	return exe_file;
}
EXPORT_SYMBOL(get_task_exe_file);

/**
 * get_task_mm - acquire a reference to the task's mm
 *
 * Returns %NULL if the task has no mm.  Checks PF_KTHREAD (meaning
 * this kernel workthread has transiently adopted a user mm with use_mm,
 * to do its AIO) is not set and if so returns a reference to it, after
 * bumping up the use count.  User must release the mm via mmput()
 * after use.  Typically used by /proc and ptrace.
 */
struct mm_struct *get_task_mm(struct task_struct *task)
{
	struct mm_struct *mm;

	task_lock(task);
	mm = task->mm;
	if (mm) {
		if (task->flags & PF_KTHREAD)
			mm = NULL;
		else
			mmget(mm);
	}
	task_unlock(task);
	return mm;
}
EXPORT_SYMBOL_GPL(get_task_mm);

struct mm_struct *mm_access(struct task_struct *task, unsigned int mode)
{
	struct mm_struct *mm;
	int err;

	err =  mutex_lock_killable(&task->signal->cred_guard_mutex);
	if (err)
		return ERR_PTR(err);

	mm = get_task_mm(task);
	if (mm && mm != current->mm &&
			!ptrace_may_access(task, mode)) {
		mmput(mm);
		mm = ERR_PTR(-EACCES);
	}
	mutex_unlock(&task->signal->cred_guard_mutex);

	return mm;
}

static void complete_vfork_done(struct task_struct *tsk)
{
	struct completion *vfork;

	task_lock(tsk);
	vfork = tsk->vfork_done;
	if (likely(vfork)) {
		tsk->vfork_done = NULL;
		complete(vfork);
	}
	task_unlock(tsk);
}

static int wait_for_vfork_done(struct task_struct *child,
				struct completion *vfork)
{
	int killed;

	freezer_do_not_count();
	cgroup_enter_frozen();
	killed = wait_for_completion_killable(vfork);
	cgroup_leave_frozen(false);
	freezer_count();

	if (killed) {
		task_lock(child);
		child->vfork_done = NULL;
		task_unlock(child);
	}

	put_task_struct(child);
	return killed;
}

/* Please note the differences between mmput and mm_release.
 * mmput is called whenever we stop holding onto a mm_struct,
 * error success whatever.
 *
 * mm_release is called after a mm_struct has been removed
 * from the current process.
 *
 * This difference is important for error handling, when we
 * only half set up a mm_struct for a new process and need to restore
 * the old one.  Because we mmput the new mm_struct before
 * restoring the old one. . .
 * Eric Biederman 10 January 1998
 */
void mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	/* Get rid of any futexes when releasing the mm */
#ifdef CONFIG_FUTEX
	if (unlikely(tsk->robust_list)) {
		exit_robust_list(tsk);
		tsk->robust_list = NULL;
	}
#ifdef CONFIG_COMPAT
	if (unlikely(tsk->compat_robust_list)) {
		compat_exit_robust_list(tsk);
		tsk->compat_robust_list = NULL;
	}
#endif
	if (unlikely(!list_empty(&tsk->pi_state_list)))
		exit_pi_state_list(tsk);
#endif

	uprobe_free_utask(tsk);

	/* Get rid of any cached register state */
	deactivate_mm(tsk, mm);

	/*
	 * Signal userspace if we're not exiting with a core dump
	 * because we want to leave the value intact for debugging
	 * purposes.
	 */
	if (tsk->clear_child_tid) {
		if (!(tsk->signal->flags & SIGNAL_GROUP_COREDUMP) &&
		    atomic_read(&mm->mm_users) > 1) {
			/*
			 * We don't check the error code - if userspace has
			 * not set up a proper pointer then tough luck.
			 */
			put_user(0, tsk->clear_child_tid);
			do_futex(tsk->clear_child_tid, FUTEX_WAKE,
					1, NULL, NULL, 0, 0);
		}
		tsk->clear_child_tid = NULL;
	}

	/*
	 * All done, finally we can wake up parent and return this mm to him.
	 * Also kthread_stop() uses this completion for synchronization.
	 */
	if (tsk->vfork_done)
		complete_vfork_done(tsk);
}

/*
 * Allocate a new mm structure and copy contents from the
 * mm structure of the passed in task structure.
 */
static struct mm_struct *dup_mm(struct task_struct *tsk)
{
	struct mm_struct *mm, *oldmm = current->mm;
	int err;

	mm = allocate_mm();
	if (!mm)
		goto fail_nomem;

	memcpy(mm, oldmm, sizeof(*mm));

	if (!mm_init(mm, tsk, mm->user_ns))
		goto fail_nomem;

	err = dup_mmap(mm, oldmm);
	if (err)
		goto free_pt;

	mm->hiwater_rss = get_mm_rss(mm);
	mm->hiwater_vm = mm->total_vm;

	if (mm->binfmt && !try_module_get(mm->binfmt->module))
		goto free_pt;

	return mm;

free_pt:
	/* don't put binfmt in mmput, we haven't got module yet */
	mm->binfmt = NULL;
	mm_init_owner(mm, NULL);
	mmput(mm);

fail_nomem:
	return NULL;
}

static int copy_mm(unsigned long clone_flags, struct task_struct *tsk)
{
	struct mm_struct *mm, *oldmm;
	int retval;

	tsk->min_flt = tsk->maj_flt = 0;
	tsk->nvcsw = tsk->nivcsw = 0;
#ifdef CONFIG_DETECT_HUNG_TASK
	tsk->last_switch_count = tsk->nvcsw + tsk->nivcsw;
	tsk->last_switch_time = 0;
#endif

	tsk->mm = NULL;
	tsk->active_mm = NULL;

	/*
	 * Are we cloning a kernel thread?
	 *
	 * We need to steal a active VM for that..
	 */
	oldmm = current->mm;
	if (!oldmm)
		return 0;

	/* initialize the new vmacache entries */
	vmacache_flush(tsk);

	if (clone_flags & CLONE_VM) {
		mmget(oldmm);
		mm = oldmm;
		goto good_mm;
	}

	retval = -ENOMEM;
	mm = dup_mm(tsk);
	if (!mm)
		goto fail_nomem;

good_mm:
	tsk->mm = mm;
	tsk->active_mm = mm;
	return 0;

fail_nomem:
	return retval;
}

static int copy_fs(unsigned long clone_flags, struct task_struct *tsk)
{
	struct fs_struct *fs = current->fs;
	if (clone_flags & CLONE_FS) {
		/* tsk->fs is already what we want */
		spin_lock(&fs->lock);
		if (fs->in_exec) {
			spin_unlock(&fs->lock);
			return -EAGAIN;
		}
		fs->users++;
		spin_unlock(&fs->lock);
		return 0;
	}
	tsk->fs = copy_fs_struct(fs);
	if (!tsk->fs)
		return -ENOMEM;
	return 0;
}

static int copy_files(unsigned long clone_flags, struct task_struct *tsk)
{
	struct files_struct *oldf, *newf;
	int error = 0;

	/*
	 * A background process may not have any files ...
	 */
	oldf = current->files;
	if (!oldf)
		goto out;

	if (clone_flags & CLONE_FILES) {
		atomic_inc(&oldf->count);
		goto out;
	}

	newf = dup_fd(oldf, &error);
	if (!newf)
		goto out;

	tsk->files = newf;
	error = 0;
out:
	return error;
}

static int copy_io(unsigned long clone_flags, struct task_struct *tsk)
{
#ifdef CONFIG_BLOCK
	struct io_context *ioc = current->io_context;
	struct io_context *new_ioc;

	if (!ioc)
		return 0;
	/*
	 * Share io context with parent, if CLONE_IO is set
	 */
	if (clone_flags & CLONE_IO) {
		ioc_task_link(ioc);
		tsk->io_context = ioc;
	} else if (ioprio_valid(ioc->ioprio)) {
		new_ioc = get_task_io_context(tsk, GFP_KERNEL, NUMA_NO_NODE);
		if (unlikely(!new_ioc))
			return -ENOMEM;

		new_ioc->ioprio = ioc->ioprio;
		put_io_context(new_ioc);
	}
#endif
	return 0;
}

static int copy_sighand(unsigned long clone_flags, struct task_struct *tsk)
{
	struct sighand_struct *sig;

	if (clone_flags & CLONE_SIGHAND) {
		atomic_inc(&current->sighand->count);
		return 0;
	}
	sig = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
	rcu_assign_pointer(tsk->sighand, sig);
	if (!sig)
		return -ENOMEM;

	atomic_set(&sig->count, 1);
	spin_lock_irq(&current->sighand->siglock);
	memcpy(sig->action, current->sighand->action, sizeof(sig->action));
	spin_unlock_irq(&current->sighand->siglock);
	return 0;
}

void __cleanup_sighand(struct sighand_struct *sighand)
{
	if (atomic_dec_and_test(&sighand->count)) {
		signalfd_cleanup(sighand);
		/*
		 * sighand_cachep is SLAB_TYPESAFE_BY_RCU so we can free it
		 * without an RCU grace period, see __lock_task_sighand().
		 */
		kmem_cache_free(sighand_cachep, sighand);
	}
}

#ifdef CONFIG_POSIX_TIMERS
/*
 * Initialize POSIX timer handling for a thread group.
 */
static void posix_cpu_timers_init_group(struct signal_struct *sig)
{
	unsigned long cpu_limit;

	cpu_limit = READ_ONCE(sig->rlim[RLIMIT_CPU].rlim_cur);
	if (cpu_limit != RLIM_INFINITY) {
		sig->cputime_expires.prof_exp = cpu_limit * NSEC_PER_SEC;
		sig->cputimer.running = true;
	}

	/* The timer lists. */
	INIT_LIST_HEAD(&sig->cpu_timers[0]);
	INIT_LIST_HEAD(&sig->cpu_timers[1]);
	INIT_LIST_HEAD(&sig->cpu_timers[2]);
}
#else
static inline void posix_cpu_timers_init_group(struct signal_struct *sig) { }
#endif

static int copy_signal(unsigned long clone_flags, struct task_struct *tsk)
{
	struct signal_struct *sig;

	if (clone_flags & CLONE_THREAD)
		return 0;

	sig = kmem_cache_zalloc(signal_cachep, GFP_KERNEL);
	tsk->signal = sig;
	if (!sig)
		return -ENOMEM;

	sig->nr_threads = 1;
	atomic_set(&sig->live, 1);
	atomic_set(&sig->sigcnt, 1);

	/* list_add(thread_node, thread_head) without INIT_LIST_HEAD() */
	sig->thread_head = (struct list_head)LIST_HEAD_INIT(tsk->thread_node);
	tsk->thread_node = (struct list_head)LIST_HEAD_INIT(sig->thread_head);

	init_waitqueue_head(&sig->wait_chldexit);
	sig->curr_target = tsk;
	init_sigpending(&sig->shared_pending);
	INIT_HLIST_HEAD(&sig->multiprocess);
	seqlock_init(&sig->stats_lock);
	prev_cputime_init(&sig->prev_cputime);

#ifdef CONFIG_POSIX_TIMERS
	INIT_LIST_HEAD(&sig->posix_timers);
	hrtimer_init(&sig->real_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sig->real_timer.function = it_real_fn;
#endif

	task_lock(current->group_leader);
	memcpy(sig->rlim, current->signal->rlim, sizeof sig->rlim);
	task_unlock(current->group_leader);

	posix_cpu_timers_init_group(sig);

	tty_audit_fork(sig);
	sched_autogroup_fork(sig);

	sig->oom_score_adj = current->signal->oom_score_adj;
	sig->oom_score_adj_min = current->signal->oom_score_adj_min;

	mutex_init(&sig->cred_guard_mutex);

	return 0;
}

static void copy_seccomp(struct task_struct *p)
{
#ifdef CONFIG_SECCOMP
	/*
	 * Must be called with sighand->lock held, which is common to
	 * all threads in the group. Holding cred_guard_mutex is not
	 * needed because this new task is not yet running and cannot
	 * be racing exec.
	 */
	assert_spin_locked(&current->sighand->siglock);

	/* Ref-count the new filter user, and assign it. */
	get_seccomp_filter(current);
	p->seccomp = current->seccomp;

	/*
	 * Explicitly enable no_new_privs here in case it got set
	 * between the task_struct being duplicated and holding the
	 * sighand lock. The seccomp state and nnp must be in sync.
	 */
	if (task_no_new_privs(current))
		task_set_no_new_privs(p);

	/*
	 * If the parent gained a seccomp mode after copying thread
	 * flags and between before we held the sighand lock, we have
	 * to manually enable the seccomp thread flag here.
	 */
	if (p->seccomp.mode != SECCOMP_MODE_DISABLED)
		set_tsk_thread_flag(p, TIF_SECCOMP);
#endif
}

SYSCALL_DEFINE1(set_tid_address, int __user *, tidptr)
{
	current->clear_child_tid = tidptr;

	return task_pid_vnr(current);
}

static void rt_mutex_init_task(struct task_struct *p)
{
	raw_spin_lock_init(&p->pi_lock);
#ifdef CONFIG_RT_MUTEXES
	p->pi_waiters = RB_ROOT_CACHED;
	p->pi_top_task = NULL;
	p->pi_blocked_on = NULL;
#endif
}

#ifdef CONFIG_POSIX_TIMERS
/*
 * Initialize POSIX timer handling for a single task.
 */
static void posix_cpu_timers_init(struct task_struct *tsk)
{
	tsk->cputime_expires.prof_exp = 0;
	tsk->cputime_expires.virt_exp = 0;
	tsk->cputime_expires.sched_exp = 0;
	INIT_LIST_HEAD(&tsk->cpu_timers[0]);
	INIT_LIST_HEAD(&tsk->cpu_timers[1]);
	INIT_LIST_HEAD(&tsk->cpu_timers[2]);
}
#else
static inline void posix_cpu_timers_init(struct task_struct *tsk) { }
#endif

static inline void init_task_pid_links(struct task_struct *task)
{
	enum pid_type type;

	for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type) {
		INIT_HLIST_NODE(&task->pid_links[type]);
	}
}

static inline void
init_task_pid(struct task_struct *task, enum pid_type type, struct pid *pid)
{
	if (type == PIDTYPE_PID)
		task->thread_pid = pid;
	else
		task->signal->pids[type] = pid;
}

static inline void rcu_copy_process(struct task_struct *p)
{
#ifdef CONFIG_PREEMPT_RCU
	p->rcu_read_lock_nesting = 0;
	p->rcu_read_unlock_special.s = 0;
	p->rcu_blocked_node = NULL;
	INIT_LIST_HEAD(&p->rcu_node_entry);
#endif /* #ifdef CONFIG_PREEMPT_RCU */
#ifdef CONFIG_TASKS_RCU
	p->rcu_tasks_holdout = false;
	INIT_LIST_HEAD(&p->rcu_tasks_holdout_list);
	p->rcu_tasks_idle_cpu = -1;
#endif /* #ifdef CONFIG_TASKS_RCU */
}

static void __delayed_free_task(struct rcu_head *rhp)
{
	struct task_struct *tsk = container_of(rhp, struct task_struct, rcu);

	free_task(tsk);
}

static __always_inline void delayed_free_task(struct task_struct *tsk)
{
	if (IS_ENABLED(CONFIG_MEMCG))
		call_rcu(&tsk->rcu, __delayed_free_task);
	else
		free_task(tsk);
}

static int pidfd_release(struct inode *inode, struct file *file)
{
	struct pid *pid = file->private_data;

	file->private_data = NULL;
	put_pid(pid);
	return 0;
}

#ifdef CONFIG_PROC_FS
static void pidfd_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct pid_namespace *ns = proc_pid_ns(file_inode(m->file));
	struct pid *pid = f->private_data;

	seq_put_decimal_ull(m, "Pid:\t", pid_nr_ns(pid, ns));
	seq_putc(m, '\n');
}
#endif

/*
 * Poll support for process exit notification.
 */
static unsigned int pidfd_poll(struct file *file, struct poll_table_struct *pts)
{
	struct task_struct *task;
	struct pid *pid = file->private_data;
	int poll_flags = 0;

	poll_wait(file, &pid->wait_pidfd, pts);

	rcu_read_lock();
	task = pid_task(pid, PIDTYPE_PID);
	/*
	 * Inform pollers only when the whole thread group exits.
	 * If the thread group leader exits before all other threads in the
	 * group, then poll(2) should block, similar to the wait(2) family.
	 */
	if (!task || (task->exit_state && thread_group_empty(task)))
		poll_flags = POLLIN | POLLRDNORM;
	rcu_read_unlock();

	return poll_flags;
}

const struct file_operations pidfd_fops = {
	.release = pidfd_release,
	.poll = pidfd_poll,
#ifdef CONFIG_PROC_FS
	.show_fdinfo = pidfd_show_fdinfo,
#endif
};

/**
 * pidfd_create() - Create a new pid file descriptor.
 *
 * @pid:  struct pid that the pidfd will reference
 *
 * This creates a new pid file descriptor with the O_CLOEXEC flag set.
 *
 * Note, that this function can only be called after the fd table has
 * been unshared to avoid leaking the pidfd to the new process.
 *
 * Return: On success, a cloexec pidfd is returned.
 *         On error, a negative errno number will be returned.
 */
static int pidfd_create(struct pid *pid)
{
	int fd;

	fd = anon_inode_getfd("[pidfd]", &pidfd_fops, get_pid(pid),
			      O_RDWR | O_CLOEXEC);
	if (fd < 0)
		put_pid(pid);

	return fd;
}

static void copy_oom_score_adj(u64 clone_flags, struct task_struct *tsk)
{
	/* Skip if kernel thread */
	if (!tsk->mm)
		return;

	/* Skip if spawning a thread or using vfork */
	if ((clone_flags & (CLONE_VM | CLONE_THREAD | CLONE_VFORK)) != CLONE_VM)
		return;

	/* We need to synchronize with __set_oom_adj */
	mutex_lock(&oom_adj_mutex);
	set_bit(MMF_MULTIPROCESS, &tsk->mm->flags);
	/* Update the values in case they were changed after copy_signal */
	tsk->signal->oom_score_adj = current->signal->oom_score_adj;
	tsk->signal->oom_score_adj_min = current->signal->oom_score_adj_min;
	mutex_unlock(&oom_adj_mutex);
}

/*
 * This creates a new process as a copy of the old one,
 * but does not actually start it yet.
 *
 * It copies the registers, and all the appropriate
 * parts of the process environment (as per the clone
 * flags). The actual kick-off is left to the caller.
 */
static __latent_entropy struct task_struct *copy_process(
					unsigned long clone_flags,
					unsigned long stack_start,
					unsigned long stack_size,
					int __user *parent_tidptr,
					int __user *child_tidptr,
					struct pid *pid,
					int trace,
					unsigned long tls,
					int node)
{
	int pidfd = -1, retval;
	struct task_struct *p;
	struct multiprocess_signals delayed;

	/*
	 * Don't allow sharing the root directory with processes in a different
	 * namespace
	 */
	if ((clone_flags & (CLONE_NEWNS|CLONE_FS)) == (CLONE_NEWNS|CLONE_FS))
		return ERR_PTR(-EINVAL);

	if ((clone_flags & (CLONE_NEWUSER|CLONE_FS)) == (CLONE_NEWUSER|CLONE_FS))
		return ERR_PTR(-EINVAL);

	/*
	 * Thread groups must share signals as well, and detached threads
	 * can only be started up within the thread group.
	 */
	if ((clone_flags & CLONE_THREAD) && !(clone_flags & CLONE_SIGHAND))
		return ERR_PTR(-EINVAL);

	/*
	 * Shared signal handlers imply shared VM. By way of the above,
	 * thread groups also imply shared VM. Blocking this case allows
	 * for various simplifications in other code.
	 */
	if ((clone_flags & CLONE_SIGHAND) && !(clone_flags & CLONE_VM))
		return ERR_PTR(-EINVAL);

	/*
	 * Siblings of global init remain as zombies on exit since they are
	 * not reaped by their parent (swapper). To solve this and to avoid
	 * multi-rooted process trees, prevent global and container-inits
	 * from creating siblings.
	 */
	if ((clone_flags & CLONE_PARENT) &&
				current->signal->flags & SIGNAL_UNKILLABLE)
		return ERR_PTR(-EINVAL);

	/*
	 * If the new process will be in a different pid or user namespace
	 * do not allow it to share a thread group with the forking task.
	 */
	if (clone_flags & CLONE_THREAD) {
		if ((clone_flags & (CLONE_NEWUSER | CLONE_NEWPID)) ||
		    (task_active_pid_ns(current) !=
				current->nsproxy->pid_ns_for_children))
			return ERR_PTR(-EINVAL);
	}

	if (clone_flags & CLONE_PIDFD) {
		int reserved;

		/*
		 * - CLONE_PARENT_SETTID is useless for pidfds and also
		 *   parent_tidptr is used to return pidfds.
		 * - CLONE_DETACHED is blocked so that we can potentially
		 *   reuse it later for CLONE_PIDFD.
		 * - CLONE_THREAD is blocked until someone really needs it.
		 */
		if (clone_flags &
		    (CLONE_DETACHED | CLONE_PARENT_SETTID | CLONE_THREAD))
			return ERR_PTR(-EINVAL);

		/*
		 * Verify that parent_tidptr is sane so we can potentially
		 * reuse it later.
		 */
		if (get_user(reserved, parent_tidptr))
			return ERR_PTR(-EFAULT);

		if (reserved != 0)
			return ERR_PTR(-EINVAL);
	}

	/*
	 * Force any signals received before this point to be delivered
	 * before the fork happens.  Collect up signals sent to multiple
	 * processes that happen during the fork and delay them so that
	 * they appear to happen after the fork.
	 */
	sigemptyset(&delayed.signal);
	INIT_HLIST_NODE(&delayed.node);

	spin_lock_irq(&current->sighand->siglock);
	if (!(clone_flags & CLONE_THREAD))
		hlist_add_head(&delayed.node, &current->signal->multiprocess);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	retval = -ERESTARTNOINTR;
	if (signal_pending(current))
		goto fork_out;

	retval = -ENOMEM;
	p = dup_task_struct(current, node);
	if (!p)
		goto fork_out;

	cpufreq_task_times_init(p);

	/*
	 * This _must_ happen before we call free_task(), i.e. before we jump
	 * to any of the bad_fork_* labels. This is to avoid freeing
	 * p->set_child_tid which is (ab)used as a kthread's data pointer for
	 * kernel threads (PF_KTHREAD).
	 */
	p->set_child_tid = (clone_flags & CLONE_CHILD_SETTID) ? child_tidptr : NULL;
	/*
	 * Clear TID on mm_release()?
	 */
	p->clear_child_tid = (clone_flags & CLONE_CHILD_CLEARTID) ? child_tidptr : NULL;

	ftrace_graph_init_task(p);

	rt_mutex_init_task(p);

#ifdef CONFIG_PROVE_LOCKING
	DEBUG_LOCKS_WARN_ON(!p->hardirqs_enabled);
	DEBUG_LOCKS_WARN_ON(!p->softirqs_enabled);
#endif
	retval = -EAGAIN;
	if (atomic_read(&p->real_cred->user->processes) >=
			task_rlimit(p, RLIMIT_NPROC)) {
		if (p->real_cred->user != INIT_USER &&
		    !capable(CAP_SYS_RESOURCE) && !capable(CAP_SYS_ADMIN))
			goto bad_fork_free;
	}
	current->flags &= ~PF_NPROC_EXCEEDED;

	retval = copy_creds(p, clone_flags);
	if (retval < 0)
		goto bad_fork_free;

	/*
	 * If multiple threads are within copy_process(), then this check
	 * triggers too late. This doesn't hurt, the check is only there
	 * to stop root fork bombs.
	 */
	retval = -EAGAIN;
	if (nr_threads >= max_threads)
		goto bad_fork_cleanup_count;

	delayacct_tsk_init(p);	/* Must remain after dup_task_struct() */
	p->flags &= ~(PF_SUPERPRIV | PF_WQ_WORKER | PF_IDLE);
	p->flags |= PF_FORKNOEXEC;
	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	rcu_copy_process(p);
	p->vfork_done = NULL;
	spin_lock_init(&p->alloc_lock);

	init_sigpending(&p->pending);

	p->utime = p->stime = p->gtime = 0;
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	p->utimescaled = p->stimescaled = 0;
#endif
	prev_cputime_init(&p->prev_cputime);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
	seqcount_init(&p->vtime.seqcount);
	p->vtime.starttime = 0;
	p->vtime.state = VTIME_INACTIVE;
#endif

#if defined(SPLIT_RSS_COUNTING)
	memset(&p->rss_stat, 0, sizeof(p->rss_stat));
#endif

	p->default_timer_slack_ns = current->timer_slack_ns;

#ifdef CONFIG_PSI
	p->psi_flags = 0;
#endif

	task_io_accounting_init(&p->ioac);
	acct_clear_integrals(p);

	posix_cpu_timers_init(p);

	p->io_context = NULL;
	audit_set_context(p, NULL);
	cgroup_fork(p);
#ifdef CONFIG_NUMA
	p->mempolicy = mpol_dup(p->mempolicy);
	if (IS_ERR(p->mempolicy)) {
		retval = PTR_ERR(p->mempolicy);
		p->mempolicy = NULL;
		goto bad_fork_cleanup_threadgroup_lock;
	}
#endif
#ifdef CONFIG_CPUSETS
	p->cpuset_mem_spread_rotor = NUMA_NO_NODE;
	p->cpuset_slab_spread_rotor = NUMA_NO_NODE;
	seqcount_init(&p->mems_allowed_seq);
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	p->irq_events = 0;
	p->hardirqs_enabled = 0;
	p->hardirq_enable_ip = 0;
	p->hardirq_enable_event = 0;
	p->hardirq_disable_ip = _THIS_IP_;
	p->hardirq_disable_event = 0;
	p->softirqs_enabled = 1;
	p->softirq_enable_ip = _THIS_IP_;
	p->softirq_enable_event = 0;
	p->softirq_disable_ip = 0;
	p->softirq_disable_event = 0;
	p->hardirq_context = 0;
	p->softirq_context = 0;
#endif

	p->pagefault_disabled = 0;

#ifdef CONFIG_LOCKDEP
	p->lockdep_depth = 0; /* no locks held yet */
	p->curr_chain_key = 0;
	p->lockdep_recursion = 0;
	lockdep_init_task(p);
#endif

#ifdef CONFIG_DEBUG_MUTEXES
	p->blocked_on = NULL; /* not blocked yet */
#endif
#ifdef CONFIG_BCACHE
	p->sequential_io	= 0;
	p->sequential_io_avg	= 0;
#endif

	/* Perform scheduler related setup. Assign this task to a CPU. */
	retval = sched_fork(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_policy;

	retval = perf_event_init_task(p);
	if (retval)
		goto bad_fork_cleanup_policy;
	retval = audit_alloc(p);
	if (retval)
		goto bad_fork_cleanup_perf;
	/* copy all the process information */
	shm_init_task(p);
	retval = security_task_alloc(p, clone_flags);
	if (retval)
		goto bad_fork_cleanup_audit;
	retval = copy_semundo(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_security;
	retval = copy_files(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_semundo;
	retval = copy_fs(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_files;
	retval = copy_sighand(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_fs;
	retval = copy_signal(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_sighand;
	retval = copy_mm(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_signal;
	retval = copy_namespaces(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_mm;
	retval = copy_io(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_namespaces;
	retval = copy_thread_tls(clone_flags, stack_start, stack_size, p, tls);
	if (retval)
		goto bad_fork_cleanup_io;

	if (pid != &init_struct_pid) {
		pid = alloc_pid(p->nsproxy->pid_ns_for_children);
		if (IS_ERR(pid)) {
			retval = PTR_ERR(pid);
			goto bad_fork_cleanup_thread;
		}
	}

	/*
	 * This has to happen after we've potentially unshared the file
	 * descriptor table (so that the pidfd doesn't leak into the child
	 * if the fd table isn't shared).
	 */
	if (clone_flags & CLONE_PIDFD) {
		retval = pidfd_create(pid);
		if (retval < 0)
			goto bad_fork_free_pid;

		pidfd = retval;
		retval = put_user(pidfd, parent_tidptr);
		if (retval)
			goto bad_fork_put_pidfd;
	}

#ifdef CONFIG_BLOCK
	p->plug = NULL;
#endif
#ifdef CONFIG_FUTEX
	p->robust_list = NULL;
#ifdef CONFIG_COMPAT
	p->compat_robust_list = NULL;
#endif
	INIT_LIST_HEAD(&p->pi_state_list);
	p->pi_state_cache = NULL;
#endif
	/*
	 * sigaltstack should be cleared when sharing the same VM
	 */
	if ((clone_flags & (CLONE_VM|CLONE_VFORK)) == CLONE_VM)
		sas_ss_reset(p);

	/*
	 * Syscall tracing and stepping should be turned off in the
	 * child regardless of CLONE_PTRACE.
	 */
	user_disable_single_step(p);
	clear_tsk_thread_flag(p, TIF_SYSCALL_TRACE);
#ifdef TIF_SYSCALL_EMU
	clear_tsk_thread_flag(p, TIF_SYSCALL_EMU);
#endif
	clear_all_latency_tracing(p);

	/* ok, now we should be set up.. */
	p->pid = pid_nr(pid);
	if (clone_flags & CLONE_THREAD) {
		p->exit_signal = -1;
		p->group_leader = current->group_leader;
		p->tgid = current->tgid;
	} else {
		if (clone_flags & CLONE_PARENT)
			p->exit_signal = current->group_leader->exit_signal;
		else
			p->exit_signal = (clone_flags & CSIGNAL);
		p->group_leader = p;
		p->tgid = p->pid;
	}

	p->nr_dirtied = 0;
	p->nr_dirtied_pause = 128 >> (PAGE_SHIFT - 10);
	p->dirty_paused_when = 0;

	p->pdeath_signal = 0;
	INIT_LIST_HEAD(&p->thread_group);
	p->task_works = NULL;

	cgroup_threadgroup_change_begin(current);
	/*
	 * Ensure that the cgroup subsystem policies allow the new process to be
	 * forked. It should be noted the the new process's css_set can be changed
	 * between here and cgroup_post_fork() if an organisation operation is in
	 * progress.
	 */
	retval = cgroup_can_fork(p);
	if (retval)
		goto bad_fork_cgroup_threadgroup_change_end;

	/*
	 * From this point on we must avoid any synchronous user-space
	 * communication until we take the tasklist-lock. In particular, we do
	 * not want user-space to be able to predict the process start-time by
	 * stalling fork(2) after we recorded the start_time but before it is
	 * visible to the system.
	 */

	p->start_time = ktime_get_ns();
	p->real_start_time = ktime_get_boot_ns();

	/*
	 * Make it visible to the rest of the system, but dont wake it up yet.
	 * Need tasklist lock for parent etc handling!
	 */
	write_lock_irq(&tasklist_lock);

	/* CLONE_PARENT re-uses the old parent */
	if (clone_flags & (CLONE_PARENT|CLONE_THREAD)) {
		p->real_parent = current->real_parent;
		p->parent_exec_id = current->parent_exec_id;
	} else {
		p->real_parent = current;
		p->parent_exec_id = current->self_exec_id;
	}

	klp_copy_process(p);

	spin_lock(&current->sighand->siglock);

	/*
	 * Copy seccomp details explicitly here, in case they were changed
	 * before holding sighand lock.
	 */
	copy_seccomp(p);

	rseq_fork(p, clone_flags);

	/* Don't start children in a dying pid namespace */
	if (unlikely(!(ns_of_pid(pid)->pid_allocated & PIDNS_ADDING))) {
		retval = -ENOMEM;
		goto bad_fork_cancel_cgroup;
	}

	/* Let kill terminate clone/fork in the middle */
	if (fatal_signal_pending(current)) {
		retval = -EINTR;
		goto bad_fork_cancel_cgroup;
	}


	init_task_pid_links(p);
	if (likely(p->pid)) {
		ptrace_init_task(p, (clone_flags & CLONE_PTRACE) || trace);

		init_task_pid(p, PIDTYPE_PID, pid);
		if (thread_group_leader(p)) {
			init_task_pid(p, PIDTYPE_TGID, pid);
			init_task_pid(p, PIDTYPE_PGID, task_pgrp(current));
			init_task_pid(p, PIDTYPE_SID, task_session(current));

			if (is_child_reaper(pid)) {
				ns_of_pid(pid)->child_reaper = p;
				p->signal->flags |= SIGNAL_UNKILLABLE;
			}
			p->signal->shared_pending.signal = delayed.signal;
			p->signal->tty = tty_kref_get(current->signal->tty);
			/*
			 * Inherit has_child_subreaper flag under the same
			 * tasklist_lock with adding child to the process tree
			 * for propagate_has_child_subreaper optimization.
			 */
			p->signal->has_child_subreaper = p->real_parent->signal->has_child_subreaper ||
							 p->real_parent->signal->is_child_subreaper;
			list_add_tail(&p->sibling, &p->real_parent->children);
			list_add_tail_rcu(&p->tasks, &init_task.tasks);
			attach_pid(p, PIDTYPE_TGID);
			attach_pid(p, PIDTYPE_PGID);
			attach_pid(p, PIDTYPE_SID);
			__this_cpu_inc(process_counts);
		} else {
			current->signal->nr_threads++;
			atomic_inc(&current->signal->live);
			atomic_inc(&current->signal->sigcnt);
			task_join_group_stop(p);
			list_add_tail_rcu(&p->thread_group,
					  &p->group_leader->thread_group);
			list_add_tail_rcu(&p->thread_node,
					  &p->signal->thread_head);
		}
		attach_pid(p, PIDTYPE_PID);
		nr_threads++;
	}
	total_forks++;
	hlist_del_init(&delayed.node);
	spin_unlock(&current->sighand->siglock);
	syscall_tracepoint_update(p);
	write_unlock_irq(&tasklist_lock);

	proc_fork_connector(p);
	cgroup_post_fork(p);
	cgroup_threadgroup_change_end(current);
	perf_event_fork(p);

	trace_task_newtask(p, clone_flags);
	uprobe_copy_process(p, clone_flags);

	copy_oom_score_adj(clone_flags, p);

	return p;

bad_fork_cancel_cgroup:
	spin_unlock(&current->sighand->siglock);
	write_unlock_irq(&tasklist_lock);
	cgroup_cancel_fork(p);
bad_fork_cgroup_threadgroup_change_end:
	cgroup_threadgroup_change_end(current);
bad_fork_put_pidfd:
	if (clone_flags & CLONE_PIDFD)
		ksys_close(pidfd);
bad_fork_free_pid:
	if (pid != &init_struct_pid)
		free_pid(pid);
bad_fork_cleanup_thread:
	exit_thread(p);
bad_fork_cleanup_io:
	if (p->io_context)
		exit_io_context(p);
bad_fork_cleanup_namespaces:
	exit_task_namespaces(p);
bad_fork_cleanup_mm:
	if (p->mm) {
		mm_clear_owner(p->mm, p);
		mmput(p->mm);
	}
bad_fork_cleanup_signal:
	if (!(clone_flags & CLONE_THREAD))
		free_signal_struct(p->signal);
bad_fork_cleanup_sighand:
	__cleanup_sighand(p->sighand);
bad_fork_cleanup_fs:
	exit_fs(p); /* blocking */
bad_fork_cleanup_files:
	exit_files(p); /* blocking */
bad_fork_cleanup_semundo:
	exit_sem(p);
bad_fork_cleanup_security:
	security_task_free(p);
bad_fork_cleanup_audit:
	audit_free(p);
bad_fork_cleanup_perf:
	perf_event_free_task(p);
bad_fork_cleanup_policy:
	lockdep_free_task(p);
#ifdef CONFIG_NUMA
	mpol_put(p->mempolicy);
bad_fork_cleanup_threadgroup_lock:
#endif
	delayacct_tsk_free(p);
bad_fork_cleanup_count:
	atomic_dec(&p->cred->user->processes);
	exit_creds(p);
bad_fork_free:
	p->state = TASK_DEAD;
	put_task_stack(p);
	delayed_free_task(p);
fork_out:
	spin_lock_irq(&current->sighand->siglock);
	hlist_del_init(&delayed.node);
	spin_unlock_irq(&current->sighand->siglock);
	return ERR_PTR(retval);
}

static inline void init_idle_pids(struct task_struct *idle)
{
	enum pid_type type;

	for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type) {
		INIT_HLIST_NODE(&idle->pid_links[type]); /* not really needed */
		init_task_pid(idle, type, &init_struct_pid);
	}
}

struct task_struct *fork_idle(int cpu)
{
	struct task_struct *task;
	task = copy_process(CLONE_VM, 0, 0, NULL, NULL, &init_struct_pid, 0, 0,
			    cpu_to_node(cpu));
	if (!IS_ERR(task)) {
		init_idle_pids(task);
		init_idle(task, cpu);
	}

	return task;
}

/*
 *  Ok, this is the main fork-routine.
 *
 * It copies the process, and if successful kick-starts
 * it and waits for it to finish using the VM if required.
 */
long _do_fork(unsigned long clone_flags,
	      unsigned long stack_start,
	      unsigned long stack_size,
	      int __user *parent_tidptr,
	      int __user *child_tidptr,
	      unsigned long tls)
{
	struct completion vfork;
	struct pid *pid;
	struct task_struct *p;
	int trace = 0;
	long nr;

	/*
	 * Determine whether and which event to report to ptracer.  When
	 * called from kernel_thread or CLONE_UNTRACED is explicitly
	 * requested, no event is reported; otherwise, report if the event
	 * for the type of forking is enabled.
	 */
	if (!(clone_flags & CLONE_UNTRACED)) {
		if (clone_flags & CLONE_VFORK)
			trace = PTRACE_EVENT_VFORK;
		else if ((clone_flags & CSIGNAL) != SIGCHLD)
			trace = PTRACE_EVENT_CLONE;
		else
			trace = PTRACE_EVENT_FORK;

		if (likely(!ptrace_event_enabled(current, trace)))
			trace = 0;
	}

	p = copy_process(clone_flags, stack_start, stack_size, parent_tidptr,
			 child_tidptr, NULL, trace, tls, NUMA_NO_NODE);
	add_latent_entropy();

	if (IS_ERR(p))
		return PTR_ERR(p);

	cpufreq_task_times_alloc(p);

	/*
	 * Do this prior waking up the new thread - the thread pointer
	 * might get invalid after that point, if the thread exits quickly.
	 */
	trace_sched_process_fork(current, p);

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);

	if (clone_flags & CLONE_PARENT_SETTID)
		put_user(nr, parent_tidptr);

	if (clone_flags & CLONE_VFORK) {
		p->vfork_done = &vfork;
		init_completion(&vfork);
		get_task_struct(p);
	}

	wake_up_new_task(p);

	/* forking complete and child started to run, tell ptracer */
	if (unlikely(trace))
		ptrace_event_pid(trace, pid);

	if (clone_flags & CLONE_VFORK) {
		if (!wait_for_vfork_done(p, &vfork))
			ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
	}

	put_pid(pid);
	return nr;
}

#ifndef CONFIG_HAVE_COPY_THREAD_TLS
/* For compatibility with architectures that call do_fork directly rather than
 * using the syscall entry points below. */
long do_fork(unsigned long clone_flags,
	      unsigned long stack_start,
	      unsigned long stack_size,
	      int __user *parent_tidptr,
	      int __user *child_tidptr)
{
	return _do_fork(clone_flags, stack_start, stack_size,
			parent_tidptr, child_tidptr, 0);
}
#endif

/*
 * Create a kernel thread.
 */
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	return _do_fork(flags|CLONE_VM|CLONE_UNTRACED, (unsigned long)fn,
		(unsigned long)arg, NULL, NULL, 0);
}

#ifdef __ARCH_WANT_SYS_FORK
SYSCALL_DEFINE0(fork)
{
#ifdef CONFIG_MMU
	return _do_fork(SIGCHLD, 0, 0, NULL, NULL, 0);
#else
	/* can not support in nommu mode */
	return -EINVAL;
#endif
}
#endif

#ifdef __ARCH_WANT_SYS_VFORK
SYSCALL_DEFINE0(vfork)
{
	return _do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, 0,
			0, NULL, NULL, 0);
}
#endif

#ifdef __ARCH_WANT_SYS_CLONE
#ifdef CONFIG_CLONE_BACKWARDS
SYSCALL_DEFINE5(clone, unsigned long, clone_flags, unsigned long, newsp,
		 int __user *, parent_tidptr,
		 unsigned long, tls,
		 int __user *, child_tidptr)
#elif defined(CONFIG_CLONE_BACKWARDS2)
SYSCALL_DEFINE5(clone, unsigned long, newsp, unsigned long, clone_flags,
		 int __user *, parent_tidptr,
		 int __user *, child_tidptr,
		 unsigned long, tls)
#elif defined(CONFIG_CLONE_BACKWARDS3)
SYSCALL_DEFINE6(clone, unsigned long, clone_flags, unsigned long, newsp,
		int, stack_size,
		int __user *, parent_tidptr,
		int __user *, child_tidptr,
		unsigned long, tls)
#else
SYSCALL_DEFINE5(clone, unsigned long, clone_flags, unsigned long, newsp,
		 int __user *, parent_tidptr,
		 int __user *, child_tidptr,
		 unsigned long, tls)
#endif
{
	return _do_fork(clone_flags, newsp, 0, parent_tidptr, child_tidptr, tls);
}
#endif

void walk_process_tree(struct task_struct *top, proc_visitor visitor, void *data)
{
	struct task_struct *leader, *parent, *child;
	int res;

	read_lock(&tasklist_lock);
	leader = top = top->group_leader;
down:
	for_each_thread(leader, parent) {
		list_for_each_entry(child, &parent->children, sibling) {
			res = visitor(child, data);
			if (res) {
				if (res < 0)
					goto out;
				leader = child;
				goto down;
			}
up:
			;
		}
	}

	if (leader != top) {
		child = leader;
		parent = child->real_parent;
		leader = parent->group_leader;
		goto up;
	}
out:
	read_unlock(&tasklist_lock);
}

#ifndef ARCH_MIN_MMSTRUCT_ALIGN
#define ARCH_MIN_MMSTRUCT_ALIGN 0
#endif

static void sighand_ctor(void *data)
{
	struct sighand_struct *sighand = data;

	spin_lock_init(&sighand->siglock);
	init_waitqueue_head(&sighand->signalfd_wqh);
}

void __init proc_caches_init(void)
{
	unsigned int mm_size;

	sighand_cachep = kmem_cache_create("sighand_cache",
			sizeof(struct sighand_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_TYPESAFE_BY_RCU|
			SLAB_ACCOUNT, sighand_ctor);
	signal_cachep = kmem_cache_create("signal_cache",
			sizeof(struct signal_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);
	files_cachep = kmem_cache_create("files_cache",
			sizeof(struct files_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);
	fs_cachep = kmem_cache_create("fs_cache",
			sizeof(struct fs_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);

	/*
	 * The mm_cpumask is located at the end of mm_struct, and is
	 * dynamically sized based on the maximum CPU number this system
	 * can have, taking hotplug into account (nr_cpu_ids).
	 */
	mm_size = sizeof(struct mm_struct) + cpumask_size();

	mm_cachep = kmem_cache_create_usercopy("mm_struct",
			mm_size, ARCH_MIN_MMSTRUCT_ALIGN,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			offsetof(struct mm_struct, saved_auxv),
			sizeof_field(struct mm_struct, saved_auxv),
			NULL);
	vm_area_cachep = KMEM_CACHE(vm_area_struct, SLAB_PANIC|SLAB_ACCOUNT);
	mmap_init();
	nsproxy_cache_init();
}

/*
 * Check constraints on flags passed to the unshare system call.
 */
static int check_unshare_flags(unsigned long unshare_flags)
{
	if (unshare_flags & ~(CLONE_THREAD|CLONE_FS|CLONE_NEWNS|CLONE_SIGHAND|
				CLONE_VM|CLONE_FILES|CLONE_SYSVSEM|
				CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWNET|
				CLONE_NEWUSER|CLONE_NEWPID|CLONE_NEWCGROUP))
		return -EINVAL;
	/*
	 * Not implemented, but pretend it works if there is nothing
	 * to unshare.  Note that unsharing the address space or the
	 * signal handlers also need to unshare the signal queues (aka
	 * CLONE_THREAD).
	 */
	if (unshare_flags & (CLONE_THREAD | CLONE_SIGHAND | CLONE_VM)) {
		if (!thread_group_empty(current))
			return -EINVAL;
	}
	if (unshare_flags & (CLONE_SIGHAND | CLONE_VM)) {
		if (atomic_read(&current->sighand->count) > 1)
			return -EINVAL;
	}
	if (unshare_flags & CLONE_VM) {
		if (!current_is_single_threaded())
			return -EINVAL;
	}

	return 0;
}

/*
 * Unshare the filesystem structure if it is being shared
 */
static int unshare_fs(unsigned long unshare_flags, struct fs_struct **new_fsp)
{
	struct fs_struct *fs = current->fs;

	if (!(unshare_flags & CLONE_FS) || !fs)
		return 0;

	/* don't need lock here; in the worst case we'll do useless copy */
	if (fs->users == 1)
		return 0;

	*new_fsp = copy_fs_struct(fs);
	if (!*new_fsp)
		return -ENOMEM;

	return 0;
}

/*
 * Unshare file descriptor table if it is being shared
 */
static int unshare_fd(unsigned long unshare_flags, struct files_struct **new_fdp)
{
	struct files_struct *fd = current->files;
	int error = 0;

	if ((unshare_flags & CLONE_FILES) &&
	    (fd && atomic_read(&fd->count) > 1)) {
		*new_fdp = dup_fd(fd, &error);
		if (!*new_fdp)
			return error;
	}

	return 0;
}

/*
 * unshare allows a process to 'unshare' part of the process
 * context which was originally shared using clone.  copy_*
 * functions used by do_fork() cannot be used here directly
 * because they modify an inactive task_struct that is being
 * constructed. Here we are modifying the current, active,
 * task_struct.
 */
int ksys_unshare(unsigned long unshare_flags)
{
	struct fs_struct *fs, *new_fs = NULL;
	struct files_struct *fd, *new_fd = NULL;
	struct cred *new_cred = NULL;
	struct nsproxy *new_nsproxy = NULL;
	int do_sysvsem = 0;
	int err;

	/*
	 * If unsharing a user namespace must also unshare the thread group
	 * and unshare the filesystem root and working directories.
	 */
	if (unshare_flags & CLONE_NEWUSER)
		unshare_flags |= CLONE_THREAD | CLONE_FS;
	/*
	 * If unsharing vm, must also unshare signal handlers.
	 */
	if (unshare_flags & CLONE_VM)
		unshare_flags |= CLONE_SIGHAND;
	/*
	 * If unsharing a signal handlers, must also unshare the signal queues.
	 */
	if (unshare_flags & CLONE_SIGHAND)
		unshare_flags |= CLONE_THREAD;
	/*
	 * If unsharing namespace, must also unshare filesystem information.
	 */
	if (unshare_flags & CLONE_NEWNS)
		unshare_flags |= CLONE_FS;

	err = check_unshare_flags(unshare_flags);
	if (err)
		goto bad_unshare_out;
	/*
	 * CLONE_NEWIPC must also detach from the undolist: after switching
	 * to a new ipc namespace, the semaphore arrays from the old
	 * namespace are unreachable.
	 */
	if (unshare_flags & (CLONE_NEWIPC|CLONE_SYSVSEM))
		do_sysvsem = 1;
	err = unshare_fs(unshare_flags, &new_fs);
	if (err)
		goto bad_unshare_out;
	err = unshare_fd(unshare_flags, &new_fd);
	if (err)
		goto bad_unshare_cleanup_fs;
	err = unshare_userns(unshare_flags, &new_cred);
	if (err)
		goto bad_unshare_cleanup_fd;
	err = unshare_nsproxy_namespaces(unshare_flags, &new_nsproxy,
					 new_cred, new_fs);
	if (err)
		goto bad_unshare_cleanup_cred;

	if (new_fs || new_fd || do_sysvsem || new_cred || new_nsproxy) {
		if (do_sysvsem) {
			/*
			 * CLONE_SYSVSEM is equivalent to sys_exit().
			 */
			exit_sem(current);
		}
		if (unshare_flags & CLONE_NEWIPC) {
			/* Orphan segments in old ns (see sem above). */
			exit_shm(current);
			shm_init_task(current);
		}

		if (new_nsproxy)
			switch_task_namespaces(current, new_nsproxy);

		task_lock(current);

		if (new_fs) {
			fs = current->fs;
			spin_lock(&fs->lock);
			current->fs = new_fs;
			if (--fs->users)
				new_fs = NULL;
			else
				new_fs = fs;
			spin_unlock(&fs->lock);
		}

		if (new_fd) {
			fd = current->files;
			current->files = new_fd;
			new_fd = fd;
		}

		task_unlock(current);

		if (new_cred) {
			/* Install the new user namespace */
			commit_creds(new_cred);
			new_cred = NULL;
		}
	}

	perf_event_namespaces(current);

bad_unshare_cleanup_cred:
	if (new_cred)
		put_cred(new_cred);
bad_unshare_cleanup_fd:
	if (new_fd)
		put_files_struct(new_fd);

bad_unshare_cleanup_fs:
	if (new_fs)
		free_fs_struct(new_fs);

bad_unshare_out:
	return err;
}

SYSCALL_DEFINE1(unshare, unsigned long, unshare_flags)
{
	return ksys_unshare(unshare_flags);
}

/*
 *	Helper to unshare the files of the current task.
 *	We don't want to expose copy_files internals to
 *	the exec layer of the kernel.
 */

int unshare_files(struct files_struct **displaced)
{
	struct task_struct *task = current;
	struct files_struct *copy = NULL;
	int error;

	error = unshare_fd(CLONE_FILES, &copy);
	if (error || !copy) {
		*displaced = NULL;
		return error;
	}
	*displaced = task->files;
	task_lock(task);
	task->files = copy;
	task_unlock(task);
	return 0;
}

int sysctl_max_threads(struct ctl_table *table, int write,
		       void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	int ret;
	int threads = max_threads;
	int min = 1;
	int max = MAX_THREADS;

	t = *table;
	t.data = &threads;
	t.extra1 = &min;
	t.extra2 = &max;

	ret = proc_dointvec_minmax(&t, write, buffer, lenp, ppos);
	if (ret || !write)
		return ret;

	max_threads = threads;

	return 0;
}
