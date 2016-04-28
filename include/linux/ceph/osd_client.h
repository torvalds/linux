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

struct ceph_msg;
struct ceph_snap_context;
struct ceph_osd_request;
struct ceph_osd_client;

/*
 * completion callback for async writepages
 */
typedef void (*ceph_osdc_callback_t)(struct ceph_osd_request *);
typedef void (*ceph_osdc_unsafe_callback_t)(struct ceph_osd_request *, bool);

#define CEPH_HOMELESS_OSD	-1

/* a given osd we're communicating with */
struct ceph_osd {
	atomic_t o_ref;
	struct ceph_osd_client *o_osdc;
	int o_osd;
	int o_incarnation;
	struct rb_node o_node;
	struct ceph_connection o_con;
	struct rb_root o_requests;
	struct rb_root o_linger_requests;
	struct list_head o_osd_lru;
	struct ceph_auth_handshake o_auth;
	unsigned long lru_ttl;
	struct list_head o_keepalive_item;
	struct mutex lock;
};

#define CEPH_OSD_SLAB_OPS	2
#define CEPH_OSD_MAX_OPS	16

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
	u32 flags;        /* CEPH_OSD_OP_FLAG_* */
	u32 indata_len;   /* request */
	u32 outdata_len;  /* reply */
	s32 rval;

	union {
		struct ceph_osd_data raw_data_in;
		struct {
			u64 offset, length;
			u64 truncate_size;
			u32 truncate_seq;
			struct ceph_osd_data osd_data;
		} extent;
		struct {
			u32 name_len;
			u32 value_len;
			__u8 cmp_op;       /* CEPH_OSD_CMPXATTR_OP_* */
			__u8 cmp_mode;     /* CEPH_OSD_CMPXATTR_MODE_* */
			struct ceph_osd_data osd_data;
		} xattr;
		struct {
			const char *class_name;
			const char *method_name;
			struct ceph_osd_data request_info;
			struct ceph_osd_data request_data;
			struct ceph_osd_data response_data;
			__u8 class_len;
			__u8 method_len;
			u32 indata_len;
		} cls;
		struct {
			u64 cookie;
			__u8 op;           /* CEPH_OSD_WATCH_OP_ */
			u32 gen;
		} watch;
		struct {
			struct ceph_osd_data request_data;
		} notify_ack;
		struct {
			u64 cookie;
			struct ceph_osd_data request_data;
			struct ceph_osd_data response_data;
		} notify;
		struct {
			u64 expected_object_size;
			u64 expected_write_size;
		} alloc_hint;
	};
};

struct ceph_osd_request_target {
	struct ceph_object_id base_oid;
	struct ceph_object_locator base_oloc;
	struct ceph_object_id target_oid;
	struct ceph_object_locator target_oloc;

	struct ceph_pg pgid;
	u32 pg_num;
	u32 pg_num_mask;
	struct ceph_osds acting;
	struct ceph_osds up;
	int size;
	int min_size;
	bool sort_bitwise;

	unsigned int flags;                /* CEPH_OSD_FLAG_* */
	bool paused;

	int osd;
};

/* an in-flight request */
struct ceph_osd_request {
	u64             r_tid;              /* unique for this client */
	struct rb_node  r_node;
	struct rb_node  r_mc_node;          /* map check */
	struct ceph_osd *r_osd;

	struct ceph_osd_request_target r_t;
#define r_base_oid	r_t.base_oid
#define r_base_oloc	r_t.base_oloc
#define r_flags		r_t.flags

	struct ceph_msg  *r_request, *r_reply;
	u32               r_sent;      /* >0 if r_request is sending/sent */

	/* request osd ops array  */
	unsigned int		r_num_ops;

	int               r_result;
	bool              r_got_reply;

	struct ceph_osd_client *r_osdc;
	struct kref       r_kref;
	bool              r_mempool;
	struct completion r_completion;
	struct completion r_safe_completion;  /* fsync waiter */
	ceph_osdc_callback_t r_callback;
	ceph_osdc_unsafe_callback_t r_unsafe_callback;
	struct list_head  r_unsafe_item;

	struct inode *r_inode;         	      /* for use by callbacks */
	void *r_priv;			      /* ditto */

	/* set by submitter */
	u64 r_snapid;                         /* for reads, CEPH_NOSNAP o/w */
	struct ceph_snap_context *r_snapc;    /* for writes */
	struct timespec r_mtime;              /* ditto */
	u64 r_data_offset;                    /* ditto */
	bool r_linger;                        /* don't resend on failure */

	/* internal */
	unsigned long r_stamp;                /* jiffies, send or check time */
	int r_attempts;
	struct ceph_eversion r_replay_version; /* aka reassert_version */
	u32 r_last_force_resend;
	u32 r_map_dne_bound;

	struct ceph_osd_req_op r_ops[];
};

struct ceph_request_redirect {
	struct ceph_object_locator oloc;
};

typedef void (*rados_watchcb2_t)(void *arg, u64 notify_id, u64 cookie,
				 u64 notifier_id, void *data, size_t data_len);
typedef void (*rados_watcherrcb_t)(void *arg, u64 cookie, int err);

struct ceph_osd_linger_request {
	struct ceph_osd_client *osdc;
	u64 linger_id;
	bool committed;
	bool is_watch;                  /* watch or notify */

	struct ceph_osd *osd;
	struct ceph_osd_request *reg_req;
	struct ceph_osd_request *ping_req;
	unsigned long ping_sent;
	unsigned long watch_valid_thru;
	struct list_head pending_lworks;

	struct ceph_osd_request_target t;
	u32 last_force_resend;
	u32 map_dne_bound;

	struct timespec mtime;

	struct kref kref;
	struct mutex lock;
	struct rb_node node;            /* osd */
	struct rb_node osdc_node;       /* osdc */
	struct rb_node mc_node;         /* map check */
	struct list_head scan_item;

	struct completion reg_commit_wait;
	struct completion notify_finish_wait;
	int reg_commit_error;
	int notify_finish_error;
	int last_error;

	u32 register_gen;
	u64 notify_id;

	rados_watchcb2_t wcb;
	rados_watcherrcb_t errcb;
	void *data;

	struct page ***preply_pages;
	size_t *preply_len;
};

struct ceph_osd_client {
	struct ceph_client     *client;

	struct ceph_osdmap     *osdmap;       /* current map */
	struct rw_semaphore    lock;

	struct rb_root         osds;          /* osds */
	struct list_head       osd_lru;       /* idle osds */
	spinlock_t             osd_lru_lock;
	struct ceph_osd        homeless_osd;
	atomic64_t             last_tid;      /* tid of last request */
	u64                    last_linger_id;
	struct rb_root         linger_requests; /* lingering requests */
	struct rb_root         map_checks;
	struct rb_root         linger_map_checks;
	atomic_t               num_requests;
	atomic_t               num_homeless;
	struct delayed_work    timeout_work;
	struct delayed_work    osds_timeout_work;
#ifdef CONFIG_DEBUG_FS
	struct dentry 	       *debugfs_file;
#endif

	mempool_t              *req_mempool;

	struct ceph_msgpool	msgpool_op;
	struct ceph_msgpool	msgpool_op_reply;

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
			    unsigned int which, u16 opcode, u32 flags);

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
extern void osd_req_op_extent_dup_last(struct ceph_osd_request *osd_req,
				       unsigned int which, u64 offset_inc);

extern struct ceph_osd_data *osd_req_op_extent_osd_data(
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
extern int osd_req_op_xattr_init(struct ceph_osd_request *osd_req, unsigned int which,
				 u16 opcode, const char *name, const void *value,
				 size_t size, u8 cmp_op, u8 cmp_mode);
extern void osd_req_op_alloc_hint_init(struct ceph_osd_request *osd_req,
				       unsigned int which,
				       u64 expected_object_size,
				       u64 expected_write_size);

extern struct ceph_osd_request *ceph_osdc_alloc_request(struct ceph_osd_client *osdc,
					       struct ceph_snap_context *snapc,
					       unsigned int num_ops,
					       bool use_mempool,
					       gfp_t gfp_flags);
int ceph_osdc_alloc_messages(struct ceph_osd_request *req, gfp_t gfp);

extern struct ceph_osd_request *ceph_osdc_new_request(struct ceph_osd_client *,
				      struct ceph_file_layout *layout,
				      struct ceph_vino vino,
				      u64 offset, u64 *len,
				      unsigned int which, int num_ops,
				      int opcode, int flags,
				      struct ceph_snap_context *snapc,
				      u32 truncate_seq, u64 truncate_size,
				      bool use_mempool);

extern void ceph_osdc_get_request(struct ceph_osd_request *req);
extern void ceph_osdc_put_request(struct ceph_osd_request *req);

extern int ceph_osdc_start_request(struct ceph_osd_client *osdc,
				   struct ceph_osd_request *req,
				   bool nofail);
extern void ceph_osdc_cancel_request(struct ceph_osd_request *req);
extern int ceph_osdc_wait_request(struct ceph_osd_client *osdc,
				  struct ceph_osd_request *req);
extern void ceph_osdc_sync(struct ceph_osd_client *osdc);

extern void ceph_osdc_flush_notifies(struct ceph_osd_client *osdc);

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

/* watch/notify */
struct ceph_osd_linger_request *
ceph_osdc_watch(struct ceph_osd_client *osdc,
		struct ceph_object_id *oid,
		struct ceph_object_locator *oloc,
		rados_watchcb2_t wcb,
		rados_watcherrcb_t errcb,
		void *data);
int ceph_osdc_unwatch(struct ceph_osd_client *osdc,
		      struct ceph_osd_linger_request *lreq);

int ceph_osdc_notify_ack(struct ceph_osd_client *osdc,
			 struct ceph_object_id *oid,
			 struct ceph_object_locator *oloc,
			 u64 notify_id,
			 u64 cookie,
			 void *payload,
			 size_t payload_len);
int ceph_osdc_notify(struct ceph_osd_client *osdc,
		     struct ceph_object_id *oid,
		     struct ceph_object_locator *oloc,
		     void *payload,
		     size_t payload_len,
		     u32 timeout,
		     struct page ***preply_pages,
		     size_t *preply_len);
int ceph_osdc_watch_check(struct ceph_osd_client *osdc,
			  struct ceph_osd_linger_request *lreq);
#endif

