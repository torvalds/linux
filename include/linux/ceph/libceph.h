#ifndef _FS_CEPH_LIBCEPH_H
#define _FS_CEPH_LIBCEPH_H

#include <linux/ceph/ceph_debug.h>

#include <asm/unaligned.h>
#include <linux/backing-dev.h>
#include <linux/completion.h>
#include <linux/exportfs.h>
#include <linux/bug.h>
#include <linux/fs.h>
#include <linux/mempool.h>
#include <linux/pagemap.h>
#include <linux/wait.h>
#include <linux/writeback.h>
#include <linux/slab.h>
#include <linux/refcount.h>

#include <linux/ceph/types.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/msgpool.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/osd_client.h>
#include <linux/ceph/ceph_fs.h>
#include <linux/ceph/string_table.h>

/*
 * mount options
 */
#define CEPH_OPT_FSID             (1<<0)
#define CEPH_OPT_NOSHARE          (1<<1) /* don't share client with other sbs */
#define CEPH_OPT_MYIP             (1<<2) /* specified my ip */
#define CEPH_OPT_NOCRC            (1<<3) /* no data crc on writes */
#define CEPH_OPT_NOMSGAUTH	  (1<<4) /* don't require msg signing feat */
#define CEPH_OPT_TCP_NODELAY	  (1<<5) /* TCP_NODELAY on TCP sockets */
#define CEPH_OPT_NOMSGSIGN	  (1<<6) /* don't sign msgs */

#define CEPH_OPT_DEFAULT   (CEPH_OPT_TCP_NODELAY)

#define ceph_set_opt(client, opt) \
	(client)->options->flags |= CEPH_OPT_##opt;
#define ceph_test_opt(client, opt) \
	(!!((client)->options->flags & CEPH_OPT_##opt))

struct ceph_options {
	int flags;
	struct ceph_fsid fsid;
	struct ceph_entity_addr my_addr;
	unsigned long mount_timeout;		/* jiffies */
	unsigned long osd_idle_ttl;		/* jiffies */
	unsigned long osd_keepalive_timeout;	/* jiffies */
	unsigned long osd_request_timeout;	/* jiffies */

	/*
	 * any type that can't be simply compared or doesn't need need
	 * to be compared should go beyond this point,
	 * ceph_compare_options() should be updated accordingly
	 */

	struct ceph_entity_addr *mon_addr; /* should be the first
					      pointer type of args */
	int num_mon;
	char *name;
	struct ceph_crypto_key *key;
};

/*
 * defaults
 */
#define CEPH_MOUNT_TIMEOUT_DEFAULT	msecs_to_jiffies(60 * 1000)
#define CEPH_OSD_KEEPALIVE_DEFAULT	msecs_to_jiffies(5 * 1000)
#define CEPH_OSD_IDLE_TTL_DEFAULT	msecs_to_jiffies(60 * 1000)
#define CEPH_OSD_REQUEST_TIMEOUT_DEFAULT 0  /* no timeout */

#define CEPH_MONC_HUNT_INTERVAL		msecs_to_jiffies(3 * 1000)
#define CEPH_MONC_PING_INTERVAL		msecs_to_jiffies(10 * 1000)
#define CEPH_MONC_PING_TIMEOUT		msecs_to_jiffies(30 * 1000)
#define CEPH_MONC_HUNT_BACKOFF		2
#define CEPH_MONC_HUNT_MAX_MULT		10

#define CEPH_MSG_MAX_FRONT_LEN	(16*1024*1024)
#define CEPH_MSG_MAX_MIDDLE_LEN	(16*1024*1024)
#define CEPH_MSG_MAX_DATA_LEN	(16*1024*1024)

#define CEPH_AUTH_NAME_DEFAULT   "guest"

/*
 * Delay telling the MDS we no longer want caps, in case we reopen
 * the file.  Delay a minimum amount of time, even if we send a cap
 * message for some other reason.  Otherwise, take the oppotunity to
 * update the mds to avoid sending another message later.
 */
#define CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT      5  /* cap release delay */
#define CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT     60  /* cap release delay */

#define CEPH_CAP_RELEASE_SAFETY_DEFAULT        (CEPH_CAPS_PER_RELEASE * 4)

/* mount state */
enum {
	CEPH_MOUNT_MOUNTING,
	CEPH_MOUNT_MOUNTED,
	CEPH_MOUNT_UNMOUNTING,
	CEPH_MOUNT_UNMOUNTED,
	CEPH_MOUNT_SHUTDOWN,
};

static inline unsigned long ceph_timeout_jiffies(unsigned long timeout)
{
	return timeout ?: MAX_SCHEDULE_TIMEOUT;
}

struct ceph_mds_client;

/*
 * per client state
 *
 * possibly shared by multiple mount points, if they are
 * mounting the same ceph filesystem/cluster.
 */
struct ceph_client {
	struct ceph_fsid fsid;
	bool have_fsid;

	void *private;

	struct ceph_options *options;

	struct mutex mount_mutex;      /* serialize mount attempts */
	wait_queue_head_t auth_wq;
	int auth_err;

	int (*extra_mon_dispatch)(struct ceph_client *, struct ceph_msg *);

	u64 supported_features;
	u64 required_features;

	struct ceph_messenger msgr;   /* messenger instance */
	struct ceph_mon_client monc;
	struct ceph_osd_client osdc;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dir;
	struct dentry *debugfs_monmap;
	struct dentry *debugfs_osdmap;
	struct dentry *debugfs_options;
#endif
};

#define from_msgr(ms)	container_of(ms, struct ceph_client, msgr)


/*
 * snapshots
 */

/*
 * A "snap context" is the set of existing snapshots when we
 * write data.  It is used by the OSD to guide its COW behavior.
 *
 * The ceph_snap_context is refcounted, and attached to each dirty
 * page, indicating which context the dirty data belonged when it was
 * dirtied.
 */
struct ceph_snap_context {
	refcount_t nref;
	u64 seq;
	u32 num_snaps;
	u64 snaps[];
};

extern struct ceph_snap_context *ceph_create_snap_context(u32 snap_count,
					gfp_t gfp_flags);
extern struct ceph_snap_context *ceph_get_snap_context(
					struct ceph_snap_context *sc);
extern void ceph_put_snap_context(struct ceph_snap_context *sc);

/*
 * calculate the number of pages a given length and offset map onto,
 * if we align the data.
 */
static inline int calc_pages_for(u64 off, u64 len)
{
	return ((off+len+PAGE_SIZE-1) >> PAGE_SHIFT) -
		(off >> PAGE_SHIFT);
}

#define RB_BYVAL(a)      (a)
#define RB_BYPTR(a)      (&(a))
#define RB_CMP3WAY(a, b) ((a) < (b) ? -1 : (a) > (b))

#define DEFINE_RB_INSDEL_FUNCS2(name, type, keyfld, cmpexp, keyexp, nodefld) \
static void insert_##name(struct rb_root *root, type *t)		\
{									\
	struct rb_node **n = &root->rb_node;				\
	struct rb_node *parent = NULL;					\
									\
	BUG_ON(!RB_EMPTY_NODE(&t->nodefld));				\
									\
	while (*n) {							\
		type *cur = rb_entry(*n, type, nodefld);		\
		int cmp;						\
									\
		parent = *n;						\
		cmp = cmpexp(keyexp(t->keyfld), keyexp(cur->keyfld));	\
		if (cmp < 0)						\
			n = &(*n)->rb_left;				\
		else if (cmp > 0)					\
			n = &(*n)->rb_right;				\
		else							\
			BUG();						\
	}								\
									\
	rb_link_node(&t->nodefld, parent, n);				\
	rb_insert_color(&t->nodefld, root);				\
}									\
static void erase_##name(struct rb_root *root, type *t)			\
{									\
	BUG_ON(RB_EMPTY_NODE(&t->nodefld));				\
	rb_erase(&t->nodefld, root);					\
	RB_CLEAR_NODE(&t->nodefld);					\
}

/*
 * @lookup_param_type is a parameter and not constructed from (@type,
 * @keyfld) with typeof() because adding const is too unwieldy.
 */
#define DEFINE_RB_LOOKUP_FUNC2(name, type, keyfld, cmpexp, keyexp,	\
			       lookup_param_type, nodefld)		\
static type *lookup_##name(struct rb_root *root, lookup_param_type key)	\
{									\
	struct rb_node *n = root->rb_node;				\
									\
	while (n) {							\
		type *cur = rb_entry(n, type, nodefld);			\
		int cmp;						\
									\
		cmp = cmpexp(key, keyexp(cur->keyfld));			\
		if (cmp < 0)						\
			n = n->rb_left;					\
		else if (cmp > 0)					\
			n = n->rb_right;				\
		else							\
			return cur;					\
	}								\
									\
	return NULL;							\
}

#define DEFINE_RB_FUNCS2(name, type, keyfld, cmpexp, keyexp,		\
			 lookup_param_type, nodefld)			\
DEFINE_RB_INSDEL_FUNCS2(name, type, keyfld, cmpexp, keyexp, nodefld)	\
DEFINE_RB_LOOKUP_FUNC2(name, type, keyfld, cmpexp, keyexp,		\
		       lookup_param_type, nodefld)

/*
 * Shorthands for integer keys.
 */
#define DEFINE_RB_INSDEL_FUNCS(name, type, keyfld, nodefld)		\
DEFINE_RB_INSDEL_FUNCS2(name, type, keyfld, RB_CMP3WAY, RB_BYVAL, nodefld)

#define DEFINE_RB_LOOKUP_FUNC(name, type, keyfld, nodefld)		\
extern type __lookup_##name##_key;					\
DEFINE_RB_LOOKUP_FUNC2(name, type, keyfld, RB_CMP3WAY, RB_BYVAL,	\
		       typeof(__lookup_##name##_key.keyfld), nodefld)

#define DEFINE_RB_FUNCS(name, type, keyfld, nodefld)			\
DEFINE_RB_INSDEL_FUNCS(name, type, keyfld, nodefld)			\
DEFINE_RB_LOOKUP_FUNC(name, type, keyfld, nodefld)

extern struct kmem_cache *ceph_inode_cachep;
extern struct kmem_cache *ceph_cap_cachep;
extern struct kmem_cache *ceph_cap_flush_cachep;
extern struct kmem_cache *ceph_dentry_cachep;
extern struct kmem_cache *ceph_file_cachep;

/* ceph_common.c */
extern bool libceph_compatible(void *data);

extern const char *ceph_msg_type_name(int type);
extern int ceph_check_fsid(struct ceph_client *client, struct ceph_fsid *fsid);
extern void *ceph_kvmalloc(size_t size, gfp_t flags);

extern struct ceph_options *ceph_parse_options(char *options,
			      const char *dev_name, const char *dev_name_end,
			      int (*parse_extra_token)(char *c, void *private),
			      void *private);
int ceph_print_client_options(struct seq_file *m, struct ceph_client *client);
extern void ceph_destroy_options(struct ceph_options *opt);
extern int ceph_compare_options(struct ceph_options *new_opt,
				struct ceph_client *client);
struct ceph_client *ceph_create_client(struct ceph_options *opt, void *private);
struct ceph_entity_addr *ceph_client_addr(struct ceph_client *client);
u64 ceph_client_gid(struct ceph_client *client);
extern void ceph_destroy_client(struct ceph_client *client);
extern int __ceph_open_session(struct ceph_client *client,
			       unsigned long started);
extern int ceph_open_session(struct ceph_client *client);

/* pagevec.c */
extern void ceph_release_page_vector(struct page **pages, int num_pages);

extern struct page **ceph_get_direct_page_vector(const void __user *data,
						 int num_pages,
						 bool write_page);
extern void ceph_put_page_vector(struct page **pages, int num_pages,
				 bool dirty);
extern struct page **ceph_alloc_page_vector(int num_pages, gfp_t flags);
extern int ceph_copy_user_to_page_vector(struct page **pages,
					 const void __user *data,
					 loff_t off, size_t len);
extern void ceph_copy_to_page_vector(struct page **pages,
				    const void *data,
				    loff_t off, size_t len);
extern void ceph_copy_from_page_vector(struct page **pages,
				    void *data,
				    loff_t off, size_t len);
extern void ceph_zero_page_vector_range(int off, int len, struct page **pages);


#endif /* _FS_CEPH_SUPER_H */
