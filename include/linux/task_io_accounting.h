/*
 * task_io_accounting: a structure which is used for recording a single task's
 * IO statistics.
 *
 * Don't include this header file directly - it is designed to be dragged in via
 * sched.h.
 *
 * Blame Andrew Morton for all this.
 */

struct task_io_accounting {
#ifdef CONFIG_TASK_XACCT
	/* bytes read */
	u64 rchar;
	/*  bytes written */
	u64 wchar;
	/* # of read syscalls */
	u64 syscr;
	/* # of write syscalls */
	u64 syscw;
	/* # of fsync syscalls */
	u64 syscfs;
#endif /* CONFIG_TASK_XACCT */

#ifdef CONFIG_TASK_IO_ACCOUNTING
	/*
	 * The number of bytes which this task has caused to be read from
	 * storage.
	 */
	u64 read_bytes;

	/*
	 * The number of bytes which this task has caused, or shall cause to be
	 * written to disk.
	 */
	u64 write_bytes;

	/*
	 * A task can cause "negative" IO too.  If this task truncates some
	 * dirty pagecache, some IO which another task has been accounted for
	 * (in its write_bytes) will not be happening.  We _could_ just
	 * subtract that from the truncating task's write_bytes, but there is
	 * information loss in doing that.
	 */
	u64 cancelled_write_bytes;
#endif /* CONFIG_TASK_IO_ACCOUNTING */
};
