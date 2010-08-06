#ifndef _FS_CEPH_OSD_CLIENT_H
#define _FS_CEPH_OSD_CLIENT_H

#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/mempool.h>
#include <linux/rbtree.h>

#include "types.h"
#include "osdmap.h"
#include "messenger.h"

struct ceph_msg;
struct ceph_snap_context;
struct ceph_osd_request;
struct ceph_osd_client;
struct ceph_authorizer;

/*
 * completion callback for async writepages
 */
typedef void (*ceph_osdc_callback_t)(struct ceph_osd_request *,
				     struct ceph_msg *);

/* a given osd we're communicating with */
struct ceph_osd {
	atomic_t o_ref;
	struct ceph_osd_client *o_osdc;
	int o_osd;
	int o_incarnation;
	struct rb_node o_node;
	struct ceph_connection o_con;
	struct list_head o_requests;
	struct list_head o_osd_lru;
	struct ceph_authorizer *o_authorizer;
	void *o_authorizer_buf, *o_authorizer_reply_buf;
	size_t o_authorizer_buf_len, o_authorizer_reply_buf_len;
	unsigned long lru_ttl;
	int o_marked_for_keepalive;
	struct list_head o_keepalive_item;
};

/* an in-flight request */
struct ceph_osd_request {
	u64             r_tid;              /* unique for this client */
	struct rb_node  r_node;
	struct list_head r_req_lru_item;
	struct list_head r_osd_item;
	struct ceph_osd *r_osd;
	struct ceph_pg   r_pgid;
	int              r_pg_osds[CEPH_PG_MAX_SIZE];
	int              r_num_pg_osds;

	struct ceph_connection *r_con_filling_msg;

	struct ceph_msg  *r_request, *r_reply;
	int               r_result;
	int               r_flags;     /* any additional flags for the osd */
	u32               r_sent;      /* >0 if r_request is sending/sent */
	int               r_got_reply;

	struct ceph_osd_client *r_osdc;
	struct kref       r_kref;
	bool              r_mempool;
	struct completion r_completion, r_safe_completion;
	ceph_osdc_callback_t r_callback, r_safe_callback;
	struct ceph_eversion r_reassert_version;
	struct list_head  r_unsafe_item;

	struct inode *r_inode;         	      /* for use by callbacks */

	char              r_oid[40];          /* object name */
	int               r_oid_len;
	unsigned long     r_stamp;            /* send OR check time */
	bool              r_resend;           /* msg send failed, needs retry */

	struct ceph_file_layout r_file_layout;
	struct ceph_snap_context *r_snapc;    /* snap context for writes */
	unsigned          r_num_pages;        /* size of page array (follows) */
	struct page     **r_pages;            /* pages for data payload */
	int               r_pages_from_pool;
	int               r_own_pages;        /* if true, i own page list */
};

struct ceph_osd_client {
	struct ceph_client     *client;

	struct ceph_osdmap     *osdmap;       /* current map */
	struct rw_semaphore    map_sem;
	struct completion      map_waiters;
	u64                    last_requested_map;

	struct mutex           request_mutex;
	struct rb_root         osds;          /* osds */
	struct list_head       osd_lru;       /* idle osds */
	u64                    timeout_tid;   /* tid of timeout triggering rq */
	u64                    last_tid;      /* tid of last request */
	struct rb_root         requests;      /* pending requests */
	struct list_head       req_lru;	      /* pending requests lru */
	int                    num_requests;
	struct delayed_work    timeout_work;
	struct delayed_work    osds_timeout_work;
#ifdef CONFIG_DEBUG_FS
	struct dentry 	       *debugfs_file;
#endif

	mempool_t              *req_mempool;

	struct ceph_msgpool	msgpool_op;
	struct ceph_msgpool	msgpool_op_reply;
};

extern int ceph_osdc_init(struct ceph_osd_client *osdc,
			  struct ceph_client *client);
extern void ceph_osdc_stop(struct ceph_osd_client *osdc);

extern void ceph_osdc_handle_reply(struct ceph_osd_client *osdc,
				   struct ceph_msg *msg);
extern void ceph_osdc_handle_map(struct ceph_osd_client *osdc,
				 struct ceph_msg *msg);

extern struct ceph_osd_request *ceph_osdc_new_request(struct ceph_osd_client *,
				      struct ceph_file_layout *layout,
				      struct ceph_vino vino,
				      u64 offset, u64 *len, int op, int flags,
				      struct ceph_snap_context *snapc,
				      int do_sync, u32 truncate_seq,
				      u64 truncate_size,
				      struct timespec *mtime,
				      bool use_mempool, int num_reply);

static inline void ceph_osdc_get_request(struct ceph_osd_request *req)
{
	kref_get(&req->r_kref);
}
extern void ceph_osdc_release_request(struct kref *kref);
static inline void ceph_osdc_put_request(struct ceph_osd_request *req)
{
	kref_put(&req->r_kref, ceph_osdc_release_request);
}

extern int ceph_osdc_start_request(struct ceph_osd_client *osdc,
				   struct ceph_osd_request *req,
				   bool nofail);
extern int ceph_osdc_wait_request(struct ceph_osd_client *osdc,
				  struct ceph_osd_request *req);
extern void ceph_osdc_sync(struct ceph_osd_client *osdc);

extern int ceph_osdc_readpages(struct ceph_osd_client *osdc,
			       struct ceph_vino vino,
			       struct ceph_file_layout *layout,
			       u64 off, u64 *plen,
			       u32 truncate_seq, u64 truncate_size,
			       struct page **pages, int nr_pages);

extern int ceph_osdc_writepages(struct ceph_osd_client *osdc,
				struct ceph_vino vino,
				struct ceph_file_layout *layout,
				struct ceph_snap_context *sc,
				u64 off, u64 len,
				u32 truncate_seq, u64 truncate_size,
				struct timespec *mtime,
				struct page **pages, int nr_pages,
				int flags, int do_sync, bool nofail);

#endif

