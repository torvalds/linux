/*
 * memfd_create system call and file sealing support
 *
 * Code was originally included in shmem.c, and broken out to facilitate
 * use by hugetlbfs as well as tmpfs.
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/khugepaged.h>
#include <linux/syscalls.h>
#include <linux/hugetlb.h>
#include <linux/shmem_fs.h>
#include <linux/memfd.h>
#include <linux/pid_namespace.h>
#include <uapi/linux/memfd.h>

/*
 * We need a tag: a new tag would expand every xa_node by 8 bytes,
 * so reuse a tag which we firmly believe is never set or cleared on tmpfs
 * or hugetlbfs because they are memory only filesystems.
 */
#define MEMFD_TAG_PINNED        PAGECACHE_TAG_TOWRITE
#define LAST_SCAN               4       /* about 150ms max */

static void memfd_tag_pins(struct xa_state *xas)
{
	struct page *page;
	int latency = 0;
	int cache_count;

	lru_add_drain();

	xas_lock_irq(xas);
	xas_for_each(xas, page, ULONG_MAX) {
		cache_count = 1;
		if (!xa_is_value(page) &&
		    PageTransHuge(page) && !PageHuge(page))
			cache_count = HPAGE_PMD_NR;

		if (!xa_is_value(page) &&
		    page_count(page) - total_mapcount(page) != cache_count)
			xas_set_mark(xas, MEMFD_TAG_PINNED);
		if (cache_count != 1)
			xas_set(xas, page->index + cache_count);

		latency += cache_count;
		if (latency < XA_CHECK_SCHED)
			continue;
		latency = 0;

		xas_pause(xas);
		xas_unlock_irq(xas);
		cond_resched();
		xas_lock_irq(xas);
	}
	xas_unlock_irq(xas);
}

/*
 * Setting SEAL_WRITE requires us to verify there's no pending writer. However,
 * via get_user_pages(), drivers might have some pending I/O without any active
 * user-space mappings (eg., direct-IO, AIO). Therefore, we look at all pages
 * and see whether it has an elevated ref-count. If so, we tag them and wait for
 * them to be dropped.
 * The caller must guarantee that no new user will acquire writable references
 * to those pages to avoid races.
 */
static int memfd_wait_for_pins(struct address_space *mapping)
{
	XA_STATE(xas, &mapping->i_pages, 0);
	struct page *page;
	int error, scan;

	memfd_tag_pins(&xas);

	error = 0;
	for (scan = 0; scan <= LAST_SCAN; scan++) {
		int latency = 0;
		int cache_count;

		if (!xas_marked(&xas, MEMFD_TAG_PINNED))
			break;

		if (!scan)
			lru_add_drain_all();
		else if (schedule_timeout_killable((HZ << scan) / 200))
			scan = LAST_SCAN;

		xas_set(&xas, 0);
		xas_lock_irq(&xas);
		xas_for_each_marked(&xas, page, ULONG_MAX, MEMFD_TAG_PINNED) {
			bool clear = true;

			cache_count = 1;
			if (!xa_is_value(page) &&
			    PageTransHuge(page) && !PageHuge(page))
				cache_count = HPAGE_PMD_NR;

			if (!xa_is_value(page) && cache_count !=
			    page_count(page) - total_mapcount(page)) {
				/*
				 * On the last scan, we clean up all those tags
				 * we inserted; but make a note that we still
				 * found pages pinned.
				 */
				if (scan == LAST_SCAN)
					error = -EBUSY;
				else
					clear = false;
			}
			if (clear)
				xas_clear_mark(&xas, MEMFD_TAG_PINNED);

			latency += cache_count;
			if (latency < XA_CHECK_SCHED)
				continue;
			latency = 0;

			xas_pause(&xas);
			xas_unlock_irq(&xas);
			cond_resched();
			xas_lock_irq(&xas);
		}
		xas_unlock_irq(&xas);
	}

	return error;
}

static unsigned int *memfd_file_seals_ptr(struct file *file)
{
	if (shmem_file(file))
		return &SHMEM_I(file_inode(file))->seals;

#ifdef CONFIG_HUGETLBFS
	if (is_file_hugepages(file))
		return &HUGETLBFS_I(file_inode(file))->seals;
#endif

	return NULL;
}

#define F_ALL_SEALS (F_SEAL_SEAL | \
		     F_SEAL_EXEC | \
		     F_SEAL_SHRINK | \
		     F_SEAL_GROW | \
		     F_SEAL_WRITE | \
		     F_SEAL_FUTURE_WRITE)

static int memfd_add_seals(struct file *file, unsigned int seals)
{
	struct inode *inode = file_inode(file);
	unsigned int *file_seals;
	int error;

	/*
	 * SEALING
	 * Sealing allows multiple parties to share a tmpfs or hugetlbfs file
	 * but restrict access to a specific subset of file operations. Seals
	 * can only be added, but never removed. This way, mutually untrusted
	 * parties can share common memory regions with a well-defined policy.
	 * A malicious peer can thus never perform unwanted operations on a
	 * shared object.
	 *
	 * Seals are only supported on special tmpfs or hugetlbfs files and
	 * always affect the whole underlying inode. Once a seal is set, it
	 * may prevent some kinds of access to the file. Currently, the
	 * following seals are defined:
	 *   SEAL_SEAL: Prevent further seals from being set on this file
	 *   SEAL_SHRINK: Prevent the file from shrinking
	 *   SEAL_GROW: Prevent the file from growing
	 *   SEAL_WRITE: Prevent write access to the file
	 *   SEAL_EXEC: Prevent modification of the exec bits in the file mode
	 *
	 * As we don't require any trust relationship between two parties, we
	 * must prevent seals from being removed. Therefore, sealing a file
	 * only adds a given set of seals to the file, it never touches
	 * existing seals. Furthermore, the "setting seals"-operation can be
	 * sealed itself, which basically prevents any further seal from being
	 * added.
	 *
	 * Semantics of sealing are only defined on volatile files. Only
	 * anonymous tmpfs and hugetlbfs files support sealing. More
	 * importantly, seals are never written to disk. Therefore, there's
	 * no plan to support it on other file types.
	 */

	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;
	if (seals & ~(unsigned int)F_ALL_SEALS)
		return -EINVAL;

	inode_lock(inode);

	file_seals = memfd_file_seals_ptr(file);
	if (!file_seals) {
		error = -EINVAL;
		goto unlock;
	}

	if (*file_seals & F_SEAL_SEAL) {
		error = -EPERM;
		goto unlock;
	}

	if ((seals & F_SEAL_WRITE) && !(*file_seals & F_SEAL_WRITE)) {
		error = mapping_deny_writable(file->f_mapping);
		if (error)
			goto unlock;

		error = memfd_wait_for_pins(file->f_mapping);
		if (error) {
			mapping_allow_writable(file->f_mapping);
			goto unlock;
		}
	}

	/*
	 * SEAL_EXEC implys SEAL_WRITE, making W^X from the start.
	 */
	if (seals & F_SEAL_EXEC && inode->i_mode & 0111)
		seals |= F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_WRITE|F_SEAL_FUTURE_WRITE;

	*file_seals |= seals;
	error = 0;

unlock:
	inode_unlock(inode);
	return error;
}

static int memfd_get_seals(struct file *file)
{
	unsigned int *seals = memfd_file_seals_ptr(file);

	return seals ? *seals : -EINVAL;
}

long memfd_fcntl(struct file *file, unsigned int cmd, unsigned int arg)
{
	long error;

	switch (cmd) {
	case F_ADD_SEALS:
		error = memfd_add_seals(file, arg);
		break;
	case F_GET_SEALS:
		error = memfd_get_seals(file);
		break;
	default:
		error = -EINVAL;
		break;
	}

	return error;
}

#define MFD_NAME_PREFIX "memfd:"
#define MFD_NAME_PREFIX_LEN (sizeof(MFD_NAME_PREFIX) - 1)
#define MFD_NAME_MAX_LEN (NAME_MAX - MFD_NAME_PREFIX_LEN)

#define MFD_ALL_FLAGS (MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_HUGETLB | MFD_NOEXEC_SEAL | MFD_EXEC)

static int check_sysctl_memfd_noexec(unsigned int *flags)
{
#ifdef CONFIG_SYSCTL
	struct pid_namespace *ns = task_active_pid_ns(current);
	int sysctl = pidns_memfd_noexec_scope(ns);

	if (!(*flags & (MFD_EXEC | MFD_NOEXEC_SEAL))) {
		if (sysctl >= MEMFD_NOEXEC_SCOPE_NOEXEC_SEAL)
			*flags |= MFD_NOEXEC_SEAL;
		else
			*flags |= MFD_EXEC;
	}

	if (!(*flags & MFD_NOEXEC_SEAL) && sysctl >= MEMFD_NOEXEC_SCOPE_NOEXEC_ENFORCED) {
		pr_err_ratelimited(
			"%s[%d]: memfd_create() requires MFD_NOEXEC_SEAL with vm.memfd_noexec=%d\n",
			current->comm, task_pid_nr(current), sysctl);
		return -EACCES;
	}
#endif
	return 0;
}

SYSCALL_DEFINE2(memfd_create,
		const char __user *, uname,
		unsigned int, flags)
{
	unsigned int *file_seals;
	struct file *file;
	int fd, error;
	char *name;
	long len;

	if (!(flags & MFD_HUGETLB)) {
		if (flags & ~(unsigned int)MFD_ALL_FLAGS)
			return -EINVAL;
	} else {
		/* Allow huge page size encoding in flags. */
		if (flags & ~(unsigned int)(MFD_ALL_FLAGS |
				(MFD_HUGE_MASK << MFD_HUGE_SHIFT)))
			return -EINVAL;
	}

	/* Invalid if both EXEC and NOEXEC_SEAL are set.*/
	if ((flags & MFD_EXEC) && (flags & MFD_NOEXEC_SEAL))
		return -EINVAL;

	if (!(flags & (MFD_EXEC | MFD_NOEXEC_SEAL))) {
		pr_warn_once(
			"%s[%d]: memfd_create() called without MFD_EXEC or MFD_NOEXEC_SEAL set\n",
			current->comm, task_pid_nr(current));
	}

	error = check_sysctl_memfd_noexec(&flags);
	if (error < 0)
		return error;

	/* length includes terminating zero */
	len = strnlen_user(uname, MFD_NAME_MAX_LEN + 1);
	if (len <= 0)
		return -EFAULT;
	if (len > MFD_NAME_MAX_LEN + 1)
		return -EINVAL;

	name = kmalloc(len + MFD_NAME_PREFIX_LEN, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	strcpy(name, MFD_NAME_PREFIX);
	if (copy_from_user(&name[MFD_NAME_PREFIX_LEN], uname, len)) {
		error = -EFAULT;
		goto err_name;
	}

	/* terminating-zero may have changed after strnlen_user() returned */
	if (name[len + MFD_NAME_PREFIX_LEN - 1]) {
		error = -EFAULT;
		goto err_name;
	}

	fd = get_unused_fd_flags((flags & MFD_CLOEXEC) ? O_CLOEXEC : 0);
	if (fd < 0) {
		error = fd;
		goto err_name;
	}

	if (flags & MFD_HUGETLB) {
		file = hugetlb_file_setup(name, 0, VM_NORESERVE,
					HUGETLB_ANONHUGE_INODE,
					(flags >> MFD_HUGE_SHIFT) &
					MFD_HUGE_MASK);
	} else
		file = shmem_file_setup(name, 0, VM_NORESERVE);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto err_fd;
	}
	file->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;
	file->f_flags |= O_LARGEFILE;

	if (flags & MFD_NOEXEC_SEAL) {
		struct inode *inode = file_inode(file);

		inode->i_mode &= ~0111;
		file_seals = memfd_file_seals_ptr(file);
		if (file_seals) {
			*file_seals &= ~F_SEAL_SEAL;
			*file_seals |= F_SEAL_EXEC;
		}
	} else if (flags & MFD_ALLOW_SEALING) {
		/* MFD_EXEC and MFD_ALLOW_SEALING are set */
		file_seals = memfd_file_seals_ptr(file);
		if (file_seals)
			*file_seals &= ~F_SEAL_SEAL;
	}

	fd_install(fd, file);
	kfree(name);
	return fd;

err_fd:
	put_unused_fd(fd);
err_name:
	kfree(name);
	return error;
}
