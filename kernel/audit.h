/* SPDX-License-Identifier: GPL-2.0-or-later */
/* audit -- definition of audit_context structure and supporting types
 *
 * Copyright 2003-2004 Red Hat, Inc.
 * Copyright 2005 Hewlett-Packard Development Company, L.P.
 * Copyright 2005 IBM Corporation
 */

#ifndef _KERNEL_AUDIT_H_
#define _KERNEL_AUDIT_H_

#include <linux/fs.h>
#include <linux/audit.h>
#include <linux/skbuff.h>
#include <uapi/linux/mqueue.h>
#include <linux/tty.h>
#include <uapi/linux/openat2.h> // struct open_how

/* AUDIT_NAMES is the number of slots we reserve in the audit_context
 * for saving names from getname().  If we get more names we will allocate
 * a name dynamically and also add those to the list anchored by names_list. */
#define AUDIT_NAMES	5

/* At task start time, the audit_state is set in the audit_context using
   a per-task filter.  At syscall entry, the audit_state is augmented by
   the syscall filter. */
enum audit_state {
	AUDIT_STATE_DISABLED,	/* Do not create per-task audit_context.
				 * No syscall-specific audit records can
				 * be generated. */
	AUDIT_STATE_BUILD,	/* Create the per-task audit_context,
				 * and fill it in at syscall
				 * entry time.  This makes a full
				 * syscall record available if some
				 * other part of the kernel decides it
				 * should be recorded. */
	AUDIT_STATE_RECORD	/* Create the per-task audit_context,
				 * always fill it in at syscall entry
				 * time, and always write out the audit
				 * record at syscall exit time.  */
};

/* Rule lists */
struct audit_watch;
struct audit_fsnotify_mark;
struct audit_tree;
struct audit_chunk;

struct audit_entry {
	struct list_head	list;
	struct rcu_head		rcu;
	struct audit_krule	rule;
};

struct audit_cap_data {
	kernel_cap_t		permitted;
	kernel_cap_t		inheritable;
	union {
		unsigned int	fE;		/* effective bit of file cap */
		kernel_cap_t	effective;	/* effective set of process */
	};
	kernel_cap_t		ambient;
	kuid_t			rootid;
};

/* When fs/namei.c:getname() is called, we store the pointer in name and bump
 * the refcnt in the associated filename struct.
 *
 * Further, in fs/namei.c:path_lookup() we store the inode and device.
 */
struct audit_names {
	struct list_head	list;		/* audit_context->names_list */

	struct filename		*name;
	int			name_len;	/* number of chars to log */
	bool			hidden;		/* don't log this record */

	unsigned long		ino;
	dev_t			dev;
	umode_t			mode;
	kuid_t			uid;
	kgid_t			gid;
	dev_t			rdev;
	u32			osid;
	struct audit_cap_data	fcap;
	unsigned int		fcap_ver;
	unsigned char		type;		/* record type */
	/*
	 * This was an allocated audit_names and not from the array of
	 * names allocated in the task audit context.  Thus this name
	 * should be freed on syscall exit.
	 */
	bool			should_free;
};

struct audit_proctitle {
	int	len;	/* length of the cmdline field. */
	char	*value;	/* the cmdline field */
};

/* The per-task audit context. */
struct audit_context {
	int		    dummy;	/* must be the first element */
	enum {
		AUDIT_CTX_UNUSED,	/* audit_context is currently unused */
		AUDIT_CTX_SYSCALL,	/* in use by syscall */
		AUDIT_CTX_URING,	/* in use by io_uring */
	} context;
	enum audit_state    state, current_state;
	unsigned int	    serial;     /* serial number for record */
	int		    major;      /* syscall number */
	int		    uring_op;   /* uring operation */
	struct timespec64   ctime;      /* time of syscall entry */
	unsigned long	    argv[4];    /* syscall arguments */
	long		    return_code;/* syscall return code */
	u64		    prio;
	int		    return_valid; /* return code is valid */
	/*
	 * The names_list is the list of all audit_names collected during this
	 * syscall.  The first AUDIT_NAMES entries in the names_list will
	 * actually be from the preallocated_names array for performance
	 * reasons.  Except during allocation they should never be referenced
	 * through the preallocated_names array and should only be found/used
	 * by running the names_list.
	 */
	struct audit_names  preallocated_names[AUDIT_NAMES];
	int		    name_count; /* total records in names_list */
	struct list_head    names_list;	/* struct audit_names->list anchor */
	char		    *filterkey;	/* key for rule that triggered record */
	struct path	    pwd;
	struct audit_aux_data *aux;
	struct audit_aux_data *aux_pids;
	struct sockaddr_storage *sockaddr;
	size_t sockaddr_len;
				/* Save things to print about task_struct */
	pid_t		    ppid;
	kuid_t		    uid, euid, suid, fsuid;
	kgid_t		    gid, egid, sgid, fsgid;
	unsigned long	    personality;
	int		    arch;

	pid_t		    target_pid;
	kuid_t		    target_auid;
	kuid_t		    target_uid;
	unsigned int	    target_sessionid;
	u32		    target_sid;
	char		    target_comm[TASK_COMM_LEN];

	struct audit_tree_refs *trees, *first_trees;
	struct list_head killed_trees;
	int tree_count;

	int type;
	union {
		struct {
			int nargs;
			long args[6];
		} socketcall;
		struct {
			kuid_t			uid;
			kgid_t			gid;
			umode_t			mode;
			u32			osid;
			int			has_perm;
			uid_t			perm_uid;
			gid_t			perm_gid;
			umode_t			perm_mode;
			unsigned long		qbytes;
		} ipc;
		struct {
			mqd_t			mqdes;
			struct mq_attr		mqstat;
		} mq_getsetattr;
		struct {
			mqd_t			mqdes;
			int			sigev_signo;
		} mq_notify;
		struct {
			mqd_t			mqdes;
			size_t			msg_len;
			unsigned int		msg_prio;
			struct timespec64	abs_timeout;
		} mq_sendrecv;
		struct {
			int			oflag;
			umode_t			mode;
			struct mq_attr		attr;
		} mq_open;
		struct {
			pid_t			pid;
			struct audit_cap_data	cap;
		} capset;
		struct {
			int			fd;
			int			flags;
		} mmap;
		struct open_how openat2;
		struct {
			int			argc;
		} execve;
		struct {
			char			*name;
		} module;
		struct {
			struct audit_ntp_data	ntp_data;
			struct timespec64	tk_injoffset;
		} time;
	};
	int fds[2];
	struct audit_proctitle proctitle;
};

extern bool audit_ever_enabled;

extern void audit_log_session_info(struct audit_buffer *ab);

extern int auditd_test_task(struct task_struct *task);

#define AUDIT_INODE_BUCKETS	32
extern struct list_head audit_inode_hash[AUDIT_INODE_BUCKETS];

static inline int audit_hash_ino(u32 ino)
{
	return (ino & (AUDIT_INODE_BUCKETS-1));
}

/* Indicates that audit should log the full pathname. */
#define AUDIT_NAME_FULL -1

extern int audit_match_class(int class, unsigned syscall);
extern int audit_comparator(const u32 left, const u32 op, const u32 right);
extern int audit_uid_comparator(kuid_t left, u32 op, kuid_t right);
extern int audit_gid_comparator(kgid_t left, u32 op, kgid_t right);
extern int parent_len(const char *path);
extern int audit_compare_dname_path(const struct qstr *dname, const char *path, int plen);
extern struct sk_buff *audit_make_reply(int seq, int type, int done, int multi,
					const void *payload, int size);
extern void		    audit_panic(const char *message);

struct audit_netlink_list {
	__u32 portid;
	struct net *net;
	struct sk_buff_head q;
};

int audit_send_list_thread(void *_dest);

extern struct mutex audit_filter_mutex;
extern int audit_del_rule(struct audit_entry *entry);
extern void audit_free_rule_rcu(struct rcu_head *head);
extern struct list_head audit_filter_list[];

extern struct audit_entry *audit_dupe_rule(struct audit_krule *old);

extern void audit_log_d_path_exe(struct audit_buffer *ab,
				 struct mm_struct *mm);

extern struct tty_struct *audit_get_tty(void);
extern void audit_put_tty(struct tty_struct *tty);

/* audit watch/mark/tree functions */
extern unsigned int audit_serial(void);
#ifdef CONFIG_AUDITSYSCALL
extern int auditsc_get_stamp(struct audit_context *ctx,
			      struct timespec64 *t, unsigned int *serial);

extern void audit_put_watch(struct audit_watch *watch);
extern void audit_get_watch(struct audit_watch *watch);
extern int audit_to_watch(struct audit_krule *krule, char *path, int len,
			  u32 op);
extern int audit_add_watch(struct audit_krule *krule, struct list_head **list);
extern void audit_remove_watch_rule(struct audit_krule *krule);
extern char *audit_watch_path(struct audit_watch *watch);
extern int audit_watch_compare(struct audit_watch *watch, unsigned long ino,
			       dev_t dev);

extern struct audit_fsnotify_mark *audit_alloc_mark(struct audit_krule *krule,
						    char *pathname, int len);
extern char *audit_mark_path(struct audit_fsnotify_mark *mark);
extern void audit_remove_mark(struct audit_fsnotify_mark *audit_mark);
extern void audit_remove_mark_rule(struct audit_krule *krule);
extern int audit_mark_compare(struct audit_fsnotify_mark *mark,
			      unsigned long ino, dev_t dev);
extern int audit_dupe_exe(struct audit_krule *new, struct audit_krule *old);
extern int audit_exe_compare(struct task_struct *tsk,
			     struct audit_fsnotify_mark *mark);

extern struct audit_chunk *audit_tree_lookup(const struct inode *inode);
extern void audit_put_chunk(struct audit_chunk *chunk);
extern bool audit_tree_match(struct audit_chunk *chunk,
			     struct audit_tree *tree);
extern int audit_make_tree(struct audit_krule *rule, char *pathname, u32 op);
extern int audit_add_tree_rule(struct audit_krule *rule);
extern int audit_remove_tree_rule(struct audit_krule *rule);
extern void audit_trim_trees(void);
extern int audit_tag_tree(char *old, char *new);
extern const char *audit_tree_path(struct audit_tree *tree);
extern void audit_put_tree(struct audit_tree *tree);
extern void audit_kill_trees(struct audit_context *context);

extern int audit_signal_info_syscall(struct task_struct *t);
extern void audit_filter_inodes(struct task_struct *tsk,
				struct audit_context *ctx);
extern struct list_head *audit_killed_trees(void);
#else /* CONFIG_AUDITSYSCALL */
#define auditsc_get_stamp(c, t, s) 0
#define audit_put_watch(w) do { } while (0)
#define audit_get_watch(w) do { } while (0)
#define audit_to_watch(k, p, l, o) (-EINVAL)
#define audit_add_watch(k, l) (-EINVAL)
#define audit_remove_watch_rule(k) BUG()
#define audit_watch_path(w) ""
#define audit_watch_compare(w, i, d) 0

#define audit_alloc_mark(k, p, l) (ERR_PTR(-EINVAL))
#define audit_mark_path(m) ""
#define audit_remove_mark(m) do { } while (0)
#define audit_remove_mark_rule(k) do { } while (0)
#define audit_mark_compare(m, i, d) 0
#define audit_exe_compare(t, m) (-EINVAL)
#define audit_dupe_exe(n, o) (-EINVAL)

#define audit_remove_tree_rule(rule) BUG()
#define audit_add_tree_rule(rule) -EINVAL
#define audit_make_tree(rule, str, op) -EINVAL
#define audit_trim_trees() do { } while (0)
#define audit_put_tree(tree) do { } while (0)
#define audit_tag_tree(old, new) -EINVAL
#define audit_tree_path(rule) ""	/* never called */
#define audit_kill_trees(context) BUG()

static inline int audit_signal_info_syscall(struct task_struct *t)
{
	return 0;
}

#define audit_filter_inodes(t, c) do { } while (0)
#endif /* CONFIG_AUDITSYSCALL */

extern char *audit_unpack_string(void **bufp, size_t *remain, size_t len);

extern int audit_filter(int msgtype, unsigned int listtype);

extern void audit_ctl_lock(void);
extern void audit_ctl_unlock(void);

#endif
