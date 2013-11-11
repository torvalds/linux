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
#include <linux/ceph/pagelist.h>

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

/*
 * completion callback for async writepages
 */
typedef void (*ceph_osdc_callback_t)(struct ceph_osd_request *,
				     struct ceph_msg *);
typedef void (*ceph_osdc_unsafe_callback_t)(struct ceph_osd_request *, bool);

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


#define CEPH_OSD_MAX_OP	2

enum ceph_osd_data_type {
	CEPH_OSD_DATA_TYPE_NONE = 0,
	CEPH_OSD_DATA_TYPE_PAGES,
	CEPH_OSD_DATA_TYPE_PAGELIST,
#ifdef CONFIG_BLOCK
	CEPH_OSD_DATA_TYPE_BIO,
#endif /* CONFIG_BLOCK */
};

struct ceph_osd_data {
	enum ceph_osd_data_type	type;
	union {
		struct {
			struct page	**pages;
			u64		length;
			u32		alignment;
			bool		pages_from_pool;
			bool		own_pages;
		};
		struct ceph_pagelist	*pagelist;
#ifdef CONFIG_BLOCK
		struct {
			struct bio	*bio;		/* list of bios */
			size_t		bio_length;	/* total in list */
		};
#endif /* CONFIG_BLOCK */
	};
};

struct ceph_osd_req_op {
	u16 op;           /* CEPH_OSD_OP_* */
	u32 payload_len;
	union {
		struct ceph_osd_data raw_data_in;
		struct {
			u64 offset, length;
			u64 truncate_size;
			u32 truncate_seq;
			struct ceph_osd_data osd_data;
		} extent;
		struct {
			const char *class_name;
			const char *method_name;
			struct ceph_osd_data request_info;
			struct ceph_osd_data request_data;
			struct ceph_osd_data response_data;
			__u8 class_len;
			__u8 method_len;
			__u8 argc;
		} cls;
		struct {
			u64 cookie;
			u64 ver;
			u32 prot_ver;
			u32 timeout;
			__u8 flag;
		} watch;
	};
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

	struct ceph_msg  *r_request, *r_reply;
	int               r_flags;     /* any additional flags for the osd */
	u32               r_sent;      /* >0 if r_request is sending/sent */

	/* request osd ops array  */
	unsigned int		r_num_ops;
	struct ceph_osd_req_op	r_ops[CEPH_OSD_MAX_OP];

	/* these are updated on each send */
	__le32           *r_request_osdmap_epoch;
	__le32           *r_request_flags;
	__le64           *r_request_pool;
	void             *r_request_pgid;
	__le32           *r_request_attempts;
	struct ceph_eversion *r_request_reassert_version;

	int               r_result;
	int               r_reply_op_len[CEPH_OSD_MAX_OP];
	s32               r_reply_op_result[CEPH_OSD_MAX_OP];
	int               r_got_reply;
	int		  r_linger;
	int		  r_completed;

	struct ceph_osd_client *r_osdc;
	struct kref       r_kref;
	bool              r_mempool;
	struct completion r_completion, r_safe_completion;
	ceph_osdc_callback_t r_callback;
	ceph_osdc_unsafe_callback_t r_unsafe_callback;
	struct ceph_eversion r_reassert_version;
	struct list_head  r_unsafe_item;

	struct inode *r_inode;         	      /* for use by callbacks */
	void *r_priv;			      /* ditto */

	char              r_oid[MAX_OBJ_NAME_SIZE];          /* object name */
	int               r_oid_len;
	u64               r_snapid;
	unsigned long     r_stamp;            /* send OR check time */

	struct ceph_file_layout r_file_layout;
	struct ceph_snap_context *r_snapc;    /* snap context for writes */
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

extern int ceph_osdc_setup(void);
extern void ceph_osdc_cleanup(void);

extern int ceph_osdc_init(struct ceph_osd_client *osdc,
			  struct ceph_client *client);
extern void ceph_osdc_stop(struct ceph_osd_client *osdc);

extern void ceph_osdc_handle_reply(struct ceph_osd_client *osdc,
				   struct ceph_msg *msg);
extern void ceph_osdc_handle_map(struct ceph_osd_client *osdc,
				 struct ceph_msg *msg);

extern void osd_req_op_init(struct ceph_osd_request *osd_req,
					unsigned int which, u16 opcode);

extern void osd_req_op_raw_data_in_pages(struct ceph_osd_request *,
					unsigned int which,
					struct page **pages, u64 length,
					u32 alignment, bool pages_from_pool,
					bool own_pages);

extern void osd_req_op_extent_init(struct ceph_osd_request *osd_req,
					unsigned int which, u16 opcode,
					u64 offset, u64 length,
					u64 truncate_size, u32 truncate_seq);
extern void osd_req_op_extent_update(struct ceph_osd_request *osd_req,
					unsigned int which, u64 length);

extern struct ceph_osd_data *osd_req_op_extent_osd_data(
					struct ceph_osd_request *osd_req,
					unsigned int which);
extern struct ceph_osd_data *osd_req_op_cls_response_data(
					struct ceph_osd_request *osd_req,
					unsigned int which);

extern void osd_req_op_extent_osd_data_pages(struct ceph_osd_request *,
					unsigned int which,
					struct page **pages, u64 length,
					u32 alignment, bool pages_from_pool,
					bool own_pages);
extern void osd_req_op_extent_osd_data_pagelist(struct ceph_osd_request *,
					unsigned int which,
					struct ceph_pagelist *pagelist);
#ifdef CONFIG_BLOCK
extern void osd_req_op_extent_osd_data_bio(struct ceph_osd_request *,
					unsigned int which,
					struct bio *bio, size_t bio_length);
#endif /* CONFIG_BLOCK */

extern void osd_req_op_cls_request_data_pagelist(struct ceph_osd_request *,
					unsigned int which,
					struct ceph_pagelist *pagelist);
extern void osd_req_op_cls_request_data_pages(struct ceph_osd_request *,
					unsigned int which,
					struct page **pages, u64 length,
					u32 alignment, bool pages_from_pool,
					bool own_pages);
extern void osd_req_op_cls_response_data_pages(struct ceph_osd_request *,
					unsigned int which,
					struct page **pages, u64 length,
					u32 alignment, bool pages_from_pool,
					bool own_pages);

extern void osd_req_op_cls_init(struct ceph_osd_request *osd_req,
					unsigned int which, u16 opcode,
					const char *class, const char *method);
extern void osd_req_op_watch_init(struct ceph_osd_request *osd_req,
					unsigned int which, u16 opcode,
					u64 cookie, u64 version, int flag);

extern struct ceph_osd_request *ceph_osdc_alloc_request(struct ceph_osd_client *osdc,
					       struct ceph_snap_context *snapc,
					       unsigned int num_ops,
					       bool use_mempool,
					       gfp_t gfp_flags);

extern void ceph_osdc_build_request(struct ceph_osd_request *req, u64 off,
				    struct ceph_snap_context *snapc,
				    u64 snap_id,
				    struct timespec *mtime);

extern struct ceph_osd_request *ceph_osdc_new_request(struct ceph_osd_client *,
				      struct ceph_file_layout *layout,
				      struct ceph_vino vino,
				      u64 offset, u64 *len,
				      int num_ops, int opcode, int flags,
				      struct ceph_snap_context *snapc,
				      u32 truncate_seq, u64 truncate_size,
				      bool use_mempool);

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
				struct page **pages, int nr_pages);

/* watch/notify events */
extern int ceph_osdc_create_event(struct ceph_osd_client *osdc,
				  void (*event_cb)(u64, u64, u8, void *),
				  void *data, struct ceph_osd_event **pevent);
extern void ceph_osdc_cancel_event(struct ceph_osd_event *event);
extern void ceph_osdc_put_event(struct ceph_osd_event *event);
#endif

