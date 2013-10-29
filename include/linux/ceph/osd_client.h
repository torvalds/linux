#ifndef _FS_CEPH_OSD_CLIENT_H
#define _FS_CEPH_OSD_CLIENT_H

#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/mempool.h>
#include <linux/rbtree.h>

#include <linux/ceph/types.h>
#include <linux/ceph/osdmap.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/auth.h>

/* 
 * Maximum object name size 
 * (must be at least as big as RBD_MAX_MD_NAME_LEN -- currently 100) 
 */
#define MAX_OBJ_NAME_SIZE 100

struct ceph_msg;
struct ceph_snap_context;
struct ceph_osd_request;
struct ceph_osd_client;
struct ceph_authorizer;
struct ceph_pagelist;

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
	struct list_head o_linger_requests;
	struct list_head o_osd_lru;
	struct ceph_auth_handshake o_auth;
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
	struct list_head r_linger_item;
	struct list_head r_linger_osd;
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
	int		  r_linger;

	struct ceph_osd_client *r_osdc;
	struct kref       r_kref;
	bool              r_mempool;
	struct completion r_completion, r_safe_completion;
	ceph_osdc_callback_t r_callback, r_safe_callback;
	struct ceph_eversion r_reassert_version;
	struct list_head  r_unsafe_item;

	struct inode *r_inode;         	      /* for use by callbacks */
	void *r_priv;			      /* ditto */

	char              r_oid[MAX_OBJ_NAME_SIZE];          /* object name */
	int               r_oid_len;
	unsigned long     r_stamp;            /* send OR check time */

	struct ceph_file_layout r_file_layout;
	struct ceph_snap_context *r_snapc;    /* snap context for writes */
	unsigned          r_num_pages;        /* size of page array (follows) */
	unsigned          r_page_alignment;   /* io offset in first page */
	struct page     **r_pages;            /* pages for data payload */
	int               r_pages_from_pool;
	int               r_own_pages;        /* if true, i own page list */
#ifdef CONFIG_BLOCK
	struct bio       *r_bio;	      /* instead of pages */
#endif

	struct ceph_pagelist *r_trail;	      /* trailing part of the data */
};

struct ceph_osd_event {
	u64 cookie;
	int one_shot;
	struct ceph_osd_client *osdc;
	void (*cb)(u64, u64, u8, void *);
	void *data;
	struct rb_node node;
	struct list_head osd_node;
	struct kref kref;
	struct completion completion;
};

struct ceph_osd_event_work {
	struct work_struct work;
	struct ceph_osd_event *event;
        u64 ver;
        u64 notify_id;
        u8 opcode;
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
	struct list_head       req_lru;	      /* in-flight lru */
	struct list_head       req_unsent;    /* unsent/need-resend queue */
	struct list_head       req_notarget;  /* map to no osd */
	struct list_head       req_linger;    /* lingering requests */
	int                    num_requests;
	struct delayed_work    timeout_work;
	struct delayed_work    osds_timeout_work;
#ifdef CONFIG_DEBUG_FS
	struct dentry 	       *debugfs_file;
#endif

	mempool_t              *req_mempool;

	struct ceph_msgpool	msgpool_op;
	struct ceph_msgpool	msgpool_op_reply;

	spinlock_t		event_lock;
	struct rb_root		event_tree;
	u64			event_count;

	struct workqueue_struct	*notify_wq;
};

struct ceph_osd_req_op {
	u16 op;           /* CEPH_OSD_OP_* */
	u32 flags;        /* CEPH_OSD_FLAG_* */
	union {
		struct {
			u64 offset, length;
			u64 truncate_size;
			u32 truncate_seq;
		} extent;
		struct {
			const char *name;
			u32 name_len;
			const char  *val;
			u32 value_len;
			__u8 cmp_op;       /* CEPH_OSD_CMPXATTR_OP_* */
			__u8 cmp_mode;     /* CEPH_OSD_CMPXATTR_MODE_* */
		} xattr;
		struct {
			const char *class_name;
			__u8 class_len;
			const char *method_name;
			__u8 method_len;
			__u8 argc;
			const char *indata;
			u32 indata_len;
		} cls;
		struct {
			u64 cookie, count;
		} pgls;
	        struct {
		        u64 snapid;
	        } snap;
		struct {
			u64 cookie;
			u64 ver;
			__u8 flag;
			u32 prot_ver;
			u32 timeout;
		} watch;
	};
	u32 payload_len;
};

extern int ceph_osdc_init(struct ceph_osd_client *osdc,
			  struct ceph_client *client);
extern void ceph_osdc_stop(struct ceph_osd_client *osdc);

extern void ceph_osdc_handle_reply(struct ceph_osd_client *osdc,
				   struct ceph_msg *msg);
extern void ceph_osdc_handle_map(struct ceph_osd_client *osdc,
				 struct ceph_msg *msg);

extern int ceph_calc_raw_layout(struct ceph_osd_client *osdc,
			struct ceph_file_layout *layout,
			u64 snapid,
			u64 off, u64 *plen, u64 *bno,
			struct ceph_osd_request *req,
			struct ceph_osd_req_op *op);

extern struct ceph_osd_request *ceph_osdc_alloc_request(struct ceph_osd_client *osdc,
					       int flags,
					       struct ceph_snap_context *snapc,
					       struct ceph_osd_req_op *ops,
					       bool use_mempool,
					       gfp_t gfp_flags,
					       struct page **pages,
					       struct bio *bio);

extern void ceph_osdc_build_request(struct ceph_osd_request *req,
				    u64 off, u64 *plen,
				    struct ceph_osd_req_op *src_ops,
				    struct ceph_snap_context *snapc,
				    struct timespec *mtime,
				    const char *oid,
				    int oid_len);

extern struct ceph_osd_request *ceph_osdc_new_request(struct ceph_osd_client *,
				      struct ceph_file_layout *layout,
				      struct ceph_vino vino,
				      u64 offset, u64 *len, int op, int flags,
				      struct ceph_snap_context *snapc,
				      int do_sync, u32 truncate_seq,
				      u64 truncate_size,
				      struct timespec *mtime,
				      bool use_mempool, int num_reply,
				      int page_align);

extern void ceph_osdc_set_request_linger(struct ceph_osd_client *osdc,
					 struct ceph_osd_request *req);
extern void ceph_osdc_unregister_linger_request(struct ceph_osd_client *osdc,
						struct ceph_osd_request *req);

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
			       struct page **pages, int nr_pages,
			       int page_align);

extern int ceph_osdc_writepages(struct ceph_osd_client *osdc,
				struct ceph_vino vino,
				struct ceph_file_layout *layout,
				struct ceph_snap_context *sc,
				u64 off, u64 len,
				u32 truncate_seq, u64 truncate_size,
				struct timespec *mtime,
				struct page **pages, int nr_pages,
				int flags, int do_sync, bool nofail);

/* watch/notify events */
extern int ceph_osdc_create_event(struct ceph_osd_client *osdc,
				  void (*event_cb)(u64, u64, u8, void *),
				  int one_shot, void *data,
				  struct ceph_osd_event **pevent);
extern void ceph_osdc_cancel_event(struct ceph_osd_event *event);
extern int ceph_osdc_wait_event(struct ceph_osd_event *event,
				unsigned long timeout);
extern void ceph_osdc_put_event(struct ceph_osd_event *event);
#endif

