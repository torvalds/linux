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

#include <linux/ceph/types.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/msgpool.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/osd_client.h>
#include <linux/ceph/ceph_fs.h>

/*
 * mount options
 */
#define CEPH_OPT_FSID             (1<<0)
#define CEPH_OPT_NOSHARE          (1<<1) /* don't share client with other sbs */
#define CEPH_OPT_MYIP             (1<<2) /* specified my ip */
#define CEPH_OPT_NOCRC            (1<<3) /* no data crc on writes */
#define CEPH_OPT_NOMSGAUTH	  (1<<4) /* not require cephx message signature */
#define CEPH_OPT_TCP_NODELAY	  (1<<5) /* TCP_NODELAY on TCP sockets */

#define CEPH_OPT_DEFAULT   (CEPH_OPT_TCP_NODELAY)

#define ceph_set_opt(client, opt) \
	(client)->options->flags |= CEPH_OPT_##opt;
#define ceph_test_opt(client, opt) \
	(!!((client)->options->flags & CEPH_OPT_##opt))

struct ceph_options {
	int flags;
	struct ceph_fsid fsid;
	struct ceph_entity_addr my_addr;
	int mount_timeout;
	int osd_idle_ttl;
	int osd_keepalive_timeout;

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
#define CEPH_MOUNT_TIMEOUT_DEFAULT  60
#define CEPH_OSD_KEEPALIVE_DEFAULT  5
#define CEPH_OSD_IDLE_TTL_DEFAULT    60

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

/*
 * subtract jiffies
 */
static inline unsigned long time_sub(unsigned long a, unsigned long b)
{
	BUG_ON(time_after(b, a));
	return (long)a - (long)b;
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
	atomic_t nref;
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
	return ((off+len+PAGE_CACHE_SIZE-1) >> PAGE_CACHE_SHIFT) -
		(off >> PAGE_CACHE_SHIFT);
}

extern struct kmem_cache *ceph_inode_cachep;
extern struct kmem_cache *ceph_cap_cachep;
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
extern struct ceph_client *ceph_create_client(struct ceph_options *opt,
					      void *private,
					      u64 supported_features,
					      u64 required_features);
extern u64 ceph_client_id(struct ceph_client *client);
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
