/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sysctl.h: General linux system control interface
 *
 * Begun 24 March 1995, Stephen Tweedie
 *
 ****************************************************************
 ****************************************************************
 **
 **  WARNING:
 **  The values in this file are exported to user space via 
 **  the sysctl() binary interface.  Do *NOT* change the
 **  numbering of any existing values here, and do not change
 **  any numbers within any one set of values.  If you have to
 **  redefine an existing interface, use a new number for it.
 **  The kernel will then return -ENOTDIR to any application using
 **  the old binary interface.
 **
 ****************************************************************
 ****************************************************************
 */
#ifndef _LINUX_SYSCTL_H
#define _LINUX_SYSCTL_H

#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/wait.h>
#include <linux/rbtree.h>
#include <linux/uidgid.h>
#include <uapi/linux/sysctl.h>

/* For the /proc/sys support */
struct completion;
struct ctl_table;
struct nsproxy;
struct ctl_table_root;
struct ctl_table_header;
struct ctl_dir;

/* Keep the same order as in fs/proc/proc_sysctl.c */
#define SYSCTL_ZERO			((void *)&sysctl_vals[0])
#define SYSCTL_ONE			((void *)&sysctl_vals[1])
#define SYSCTL_TWO			((void *)&sysctl_vals[2])
#define SYSCTL_THREE			((void *)&sysctl_vals[3])
#define SYSCTL_FOUR			((void *)&sysctl_vals[4])
#define SYSCTL_ONE_HUNDRED		((void *)&sysctl_vals[5])
#define SYSCTL_TWO_HUNDRED		((void *)&sysctl_vals[6])
#define SYSCTL_ONE_THOUSAND		((void *)&sysctl_vals[7])
#define SYSCTL_THREE_THOUSAND		((void *)&sysctl_vals[8])
#define SYSCTL_INT_MAX			((void *)&sysctl_vals[9])

/* this is needed for the proc_dointvec_minmax for [fs_]overflow UID and GID */
#define SYSCTL_MAXOLDUID		((void *)&sysctl_vals[10])
#define SYSCTL_NEG_ONE			((void *)&sysctl_vals[11])

extern const int sysctl_vals[];

#define SYSCTL_LONG_ZERO	((void *)&sysctl_long_vals[0])
#define SYSCTL_LONG_ONE		((void *)&sysctl_long_vals[1])
#define SYSCTL_LONG_MAX		((void *)&sysctl_long_vals[2])

extern const unsigned long sysctl_long_vals[];

typedef int proc_handler(const struct ctl_table *ctl, int write, void *buffer,
		size_t *lenp, loff_t *ppos);

int proc_dostring(const struct ctl_table *, int, void *, size_t *, loff_t *);
int proc_dobool(const struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos);
int proc_dointvec(const struct ctl_table *, int, void *, size_t *, loff_t *);
int proc_douintvec(const struct ctl_table *, int, void *, size_t *, loff_t *);
int proc_dointvec_minmax(const struct ctl_table *, int, void *, size_t *, loff_t *);
int proc_douintvec_minmax(const struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos);
int proc_dou8vec_minmax(const struct ctl_table *table, int write, void *buffer,
			size_t *lenp, loff_t *ppos);
int proc_dointvec_jiffies(const struct ctl_table *, int, void *, size_t *, loff_t *);
int proc_dointvec_ms_jiffies_minmax(const struct ctl_table *table, int write,
		void *buffer, size_t *lenp, loff_t *ppos);
int proc_dointvec_userhz_jiffies(const struct ctl_table *, int, void *, size_t *,
		loff_t *);
int proc_dointvec_ms_jiffies(const struct ctl_table *, int, void *, size_t *,
		loff_t *);
int proc_doulongvec_minmax(const struct ctl_table *, int, void *, size_t *, loff_t *);
int proc_doulongvec_ms_jiffies_minmax(const struct ctl_table *table, int, void *,
		size_t *, loff_t *);
int proc_do_large_bitmap(const struct ctl_table *, int, void *, size_t *, loff_t *);
int proc_do_static_key(const struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos);

/*
 * Register a set of sysctl names by calling register_sysctl
 * with an initialised array of struct ctl_table's.
 *
 * sysctl names can be mirrored automatically under /proc/sys.  The
 * procname supplied controls /proc naming.
 *
 * The table's mode will be honoured for proc-fs access.
 *
 * Leaf nodes in the sysctl tree will be represented by a single file
 * under /proc; non-leaf nodes will be represented by directories.  A
 * null procname disables /proc mirroring at this node.
 *
 * The data and maxlen fields of the ctl_table
 * struct enable minimal validation of the values being written to be
 * performed, and the mode field allows minimal authentication.
 * 
 * There must be a proc_handler routine for any terminal nodes
 * mirrored under /proc/sys (non-terminals are handled by a built-in
 * directory handler).  Several default handlers are available to
 * cover common cases.
 */

/* Support for userspace poll() to watch for changes */
struct ctl_table_poll {
	atomic_t event;
	wait_queue_head_t wait;
};

static inline void *proc_sys_poll_event(struct ctl_table_poll *poll)
{
	return (void *)(unsigned long)atomic_read(&poll->event);
}

#define __CTL_TABLE_POLL_INITIALIZER(name) {				\
	.event = ATOMIC_INIT(0),					\
	.wait = __WAIT_QUEUE_HEAD_INITIALIZER(name.wait) }

#define DEFINE_CTL_TABLE_POLL(name)					\
	struct ctl_table_poll name = __CTL_TABLE_POLL_INITIALIZER(name)

/* A sysctl table is an array of struct ctl_table: */
struct ctl_table {
	const char *procname;		/* Text ID for /proc/sys */
	void *data;
	int maxlen;
	umode_t mode;
	proc_handler *proc_handler;	/* Callback for text formatting */
	struct ctl_table_poll *poll;
	void *extra1;
	void *extra2;
} __randomize_layout;

struct ctl_node {
	struct rb_node node;
	struct ctl_table_header *header;
};

/**
 * struct ctl_table_header - maintains dynamic lists of struct ctl_table trees
 * @ctl_table: pointer to the first element in ctl_table array
 * @ctl_table_size: number of elements pointed by @ctl_table
 * @used: The entry will never be touched when equal to 0.
 * @count: Upped every time something is added to @inodes and downed every time
 *         something is removed from inodes
 * @nreg: When nreg drops to 0 the ctl_table_header will be unregistered.
 * @rcu: Delays the freeing of the inode. Introduced with "unfuck proc_sysctl ->d_compare()"
 *
 */
struct ctl_table_header {
	union {
		struct {
			const struct ctl_table *ctl_table;
			int ctl_table_size;
			int used;
			int count;
			int nreg;
		};
		struct rcu_head rcu;
	};
	struct completion *unregistering;
	const struct ctl_table *ctl_table_arg;
	struct ctl_table_root *root;
	struct ctl_table_set *set;
	struct ctl_dir *parent;
	struct ctl_node *node;
	struct hlist_head inodes; /* head for proc_inode->sysctl_inodes */
	/**
	 * enum type - Enumeration to differentiate between ctl target types
	 * @SYSCTL_TABLE_TYPE_DEFAULT: ctl target with no special considerations
	 * @SYSCTL_TABLE_TYPE_PERMANENTLY_EMPTY: Used to identify a permanently
	 *                                       empty directory target to serve
	 *                                       as mount point.
	 */
	enum {
		SYSCTL_TABLE_TYPE_DEFAULT,
		SYSCTL_TABLE_TYPE_PERMANENTLY_EMPTY,
	} type;
};

struct ctl_dir {
	/* Header must be at the start of ctl_dir */
	struct ctl_table_header header;
	struct rb_root root;
};

struct ctl_table_set {
	int (*is_seen)(struct ctl_table_set *);
	struct ctl_dir dir;
};

struct ctl_table_root {
	struct ctl_table_set default_set;
	struct ctl_table_set *(*lookup)(struct ctl_table_root *root);
	void (*set_ownership)(struct ctl_table_header *head,
			      kuid_t *uid, kgid_t *gid);
	int (*permissions)(struct ctl_table_header *head, const struct ctl_table *table);
};

#define register_sysctl(path, table)	\
	register_sysctl_sz(path, table, ARRAY_SIZE(table))

#ifdef CONFIG_SYSCTL

void proc_sys_poll_notify(struct ctl_table_poll *poll);

extern void setup_sysctl_set(struct ctl_table_set *p,
	struct ctl_table_root *root,
	int (*is_seen)(struct ctl_table_set *));
extern void retire_sysctl_set(struct ctl_table_set *set);

struct ctl_table_header *__register_sysctl_table(
	struct ctl_table_set *set,
	const char *path, const struct ctl_table *table, size_t table_size);
struct ctl_table_header *register_sysctl_sz(const char *path, const struct ctl_table *table,
					    size_t table_size);
void unregister_sysctl_table(struct ctl_table_header * table);

extern int sysctl_init_bases(void);
extern void __register_sysctl_init(const char *path, const struct ctl_table *table,
				 const char *table_name, size_t table_size);
#define register_sysctl_init(path, table)	\
	__register_sysctl_init(path, table, #table, ARRAY_SIZE(table))
extern struct ctl_table_header *register_sysctl_mount_point(const char *path);

void do_sysctl_args(void);
bool sysctl_is_alias(char *param);
int do_proc_douintvec(const struct ctl_table *table, int write,
		      void *buffer, size_t *lenp, loff_t *ppos,
		      int (*conv)(unsigned long *lvalp,
				  unsigned int *valp,
				  int write, void *data),
		      void *data);

extern int unaligned_enabled;
extern int no_unaligned_warning;

#else /* CONFIG_SYSCTL */

static inline void register_sysctl_init(const char *path, const struct ctl_table *table)
{
}

static inline struct ctl_table_header *register_sysctl_mount_point(const char *path)
{
	return NULL;
}

static inline struct ctl_table_header *register_sysctl_sz(const char *path,
							  const struct ctl_table *table,
							  size_t table_size)
{
	return NULL;
}

static inline void unregister_sysctl_table(struct ctl_table_header * table)
{
}

static inline void setup_sysctl_set(struct ctl_table_set *p,
	struct ctl_table_root *root,
	int (*is_seen)(struct ctl_table_set *))
{
}

static inline void do_sysctl_args(void)
{
}

static inline bool sysctl_is_alias(char *param)
{
	return false;
}
#endif /* CONFIG_SYSCTL */

#endif /* _LINUX_SYSCTL_H */
