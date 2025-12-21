/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 9P Client Definitions
 *
 *  Copyright (C) 2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2007 by Latchesar Ionkov <lucho@ionkov.net>
 */

#ifndef NET_9P_CLIENT_H
#define NET_9P_CLIENT_H

#include <linux/utsname.h>
#include <linux/idr.h>
#include <linux/tracepoint-defs.h>

/* Number of requests per row */
#define P9_ROW_MAXTAG 255

/* DEFAULT MSIZE = 32 pages worth of payload + P9_HDRSZ +
 * room for write (16 extra) or read (11 extra) operands.
 */

#define DEFAULT_MSIZE ((128 * 1024) + P9_IOHDRSZ)

/** enum p9_proto_versions - 9P protocol versions
 * @p9_proto_legacy: 9P Legacy mode, pre-9P2000.u
 * @p9_proto_2000u: 9P2000.u extension
 * @p9_proto_2000L: 9P2000.L extension
 */

enum p9_proto_versions {
	p9_proto_legacy,
	p9_proto_2000u,
	p9_proto_2000L,
};


/**
 * enum p9_trans_status - different states of underlying transports
 * @Connected: transport is connected and healthy
 * @Disconnected: transport has been disconnected
 * @Hung: transport is connected by wedged
 *
 * This enumeration details the various states a transport
 * instatiation can be in.
 */

enum p9_trans_status {
	Connected,
	BeginDisconnect,
	Disconnected,
	Hung,
};

/**
 * enum p9_req_status_t - status of a request
 * @REQ_STATUS_ALLOC: request has been allocated but not sent
 * @REQ_STATUS_UNSENT: request waiting to be sent
 * @REQ_STATUS_SENT: request sent to server
 * @REQ_STATUS_RCVD: response received from server
 * @REQ_STATUS_FLSHD: request has been flushed
 * @REQ_STATUS_ERROR: request encountered an error on the client side
 */

enum p9_req_status_t {
	REQ_STATUS_ALLOC,
	REQ_STATUS_UNSENT,
	REQ_STATUS_SENT,
	REQ_STATUS_RCVD,
	REQ_STATUS_FLSHD,
	REQ_STATUS_ERROR,
};

/**
 * struct p9_req_t - request slots
 * @status: status of this request slot
 * @t_err: transport error
 * @wq: wait_queue for the client to block on for this request
 * @tc: the request fcall structure
 * @rc: the response fcall structure
 * @req_list: link for higher level objects to chain requests
 */
struct p9_req_t {
	int status;
	int t_err;
	refcount_t refcount;
	wait_queue_head_t wq;
	struct p9_fcall tc;
	struct p9_fcall rc;
	struct list_head req_list;
};

/**
 * struct p9_client - per client instance state
 * @lock: protect @fids and @reqs
 * @msize: maximum data size negotiated by protocol
 * @proto_version: 9P protocol version to use
 * @trans_mod: module API instantiated with this client
 * @status: connection state
 * @trans: tranport instance state and API
 * @fids: All active FID handles
 * @reqs: All active requests.
 * @name: node name used as client id
 *
 * The client structure is used to keep track of various per-client
 * state that has been instantiated.
 */
struct p9_client {
	spinlock_t lock;
	unsigned int msize;
	unsigned char proto_version;
	struct p9_trans_module *trans_mod;
	enum p9_trans_status status;
	void *trans;
	struct kmem_cache *fcall_cache;

	union {
		struct {
			int rfd;
			int wfd;
		} fd;
		struct {
			u16 port;
			bool privport;

		} tcp;
	} trans_opts;

	struct idr fids;
	struct idr reqs;

	char name[__NEW_UTS_LEN + 1];
};

/**
 * struct p9_fd_opts - holds client options during parsing
 * @msize: maximum data size negotiated by protocol
 * @prot-Oversion: 9P protocol version to use
 * @trans_mod: module API instantiated with this client
 *
 * These parsed options get transferred into client in
 * apply_client_options()
 */
struct p9_client_opts {
	unsigned int msize;
	unsigned char proto_version;
	struct p9_trans_module *trans_mod;
};

/**
 * struct p9_fd_opts - per-transport options for fd transport
 * @rfd: file descriptor for reading (trans=fd)
 * @wfd: file descriptor for writing (trans=fd)
 * @port: port to connect to (trans=tcp)
 * @privport: port is privileged
 */
struct p9_fd_opts {
	int rfd;
	int wfd;
	u16 port;
	bool privport;
};

/**
 * struct p9_rdma_opts - Collection of mount options for rdma transport
 * @port: port of connection
 * @privport: Whether a privileged port may be used
 * @sq_depth: The requested depth of the SQ. This really doesn't need
 * to be any deeper than the number of threads used in the client
 * @rq_depth: The depth of the RQ. Should be greater than or equal to SQ depth
 * @timeout: Time to wait in msecs for CM events
 */
struct p9_rdma_opts {
	short port;
	bool privport;
	int sq_depth;
	int rq_depth;
	long timeout;
};

/**
 * struct p9_session_opts - holds parsed options for v9fs_session_info
 * @flags: session options of type &p9_session_flags
 * @nodev: set to 1 to disable device mapping
 * @debug: debug level
 * @afid: authentication handle
 * @cache: cache mode of type &p9_cache_bits
 * @cachetag: the tag of the cache associated with this session
 * @uname: string user name to mount hierarchy as
 * @aname: mount specifier for remote hierarchy
 * @dfltuid: default numeric userid to mount hierarchy as
 * @dfltgid: default numeric groupid to mount hierarchy as
 * @uid: if %V9FS_ACCESS_SINGLE, the numeric uid which mounted the hierarchy
 * @session_lock_timeout: retry interval for blocking locks
 *
 * This strucure holds options which are parsed and will be transferred
 * to the v9fs_session_info structure when mounted, and therefore largely
 * duplicates struct v9fs_session_info.
 */
struct p9_session_opts {
	unsigned int flags;
	unsigned char nodev;
	unsigned short debug;
	unsigned int afid;
	unsigned int cache;
#ifdef CONFIG_9P_FSCACHE
	char *cachetag;
#endif
	char *uname;
	char *aname;
	kuid_t dfltuid;
	kgid_t dfltgid;
	kuid_t uid;
	long session_lock_timeout;
};

/* Used by mount API to store parsed mount options */
struct v9fs_context {
	struct p9_client_opts	client_opts;
	struct p9_fd_opts	fd_opts;
	struct p9_rdma_opts	rdma_opts;
	struct p9_session_opts	session_opts;
};

/**
 * struct p9_fid - file system entity handle
 * @clnt: back pointer to instantiating &p9_client
 * @fid: numeric identifier for this handle
 * @mode: current mode of this fid (enum?)
 * @qid: the &p9_qid server identifier this handle points to
 * @iounit: the server reported maximum transaction size for this file
 * @uid: the numeric uid of the local user who owns this handle
 * @rdir: readdir accounting structure (allocated on demand)
 * @dlist: per-dentry fid tracking
 *
 * TODO: This needs lots of explanation.
 */
enum fid_source {
	FID_FROM_OTHER,
	FID_FROM_INODE,
	FID_FROM_DENTRY,
};

struct p9_fid {
	struct p9_client *clnt;
	u32 fid;
	refcount_t count;
	int mode;
	struct p9_qid qid;
	u32 iounit;
	kuid_t uid;

	void *rdir;

	struct hlist_node dlist;	/* list of all fids attached to a dentry */
	struct hlist_node ilist;
};

/**
 * struct p9_dirent - directory entry structure
 * @qid: The p9 server qid for this dirent
 * @d_off: offset to the next dirent
 * @d_type: type of file
 * @d_name: file name
 */

struct p9_dirent {
	struct p9_qid qid;
	u64 d_off;
	unsigned char d_type;
	char d_name[256];
};

struct iov_iter;

int p9_show_client_options(struct seq_file *m, struct p9_client *clnt);
int p9_client_statfs(struct p9_fid *fid, struct p9_rstatfs *sb);
int p9_client_rename(struct p9_fid *fid, struct p9_fid *newdirfid,
		     const char *name);
int p9_client_renameat(struct p9_fid *olddirfid, const char *old_name,
		       struct p9_fid *newdirfid, const char *new_name);
struct p9_client *p9_client_create(struct fs_context *fc);
void p9_client_destroy(struct p9_client *clnt);
void p9_client_disconnect(struct p9_client *clnt);
void p9_client_begin_disconnect(struct p9_client *clnt);
struct p9_fid *p9_client_attach(struct p9_client *clnt, struct p9_fid *afid,
				const char *uname, kuid_t n_uname, const char *aname);
struct p9_fid *p9_client_walk(struct p9_fid *oldfid, uint16_t nwname,
		const unsigned char * const *wnames, int clone);
int p9_client_open(struct p9_fid *fid, int mode);
int p9_client_fcreate(struct p9_fid *fid, const char *name, u32 perm, int mode,
							char *extension);
int p9_client_link(struct p9_fid *fid, struct p9_fid *oldfid, const char *newname);
int p9_client_symlink(struct p9_fid *fid, const char *name, const char *symname,
		kgid_t gid, struct p9_qid *qid);
int p9_client_create_dotl(struct p9_fid *ofid, const char *name, u32 flags, u32 mode,
		kgid_t gid, struct p9_qid *qid);
int p9_client_clunk(struct p9_fid *fid);
int p9_client_fsync(struct p9_fid *fid, int datasync);
int p9_client_remove(struct p9_fid *fid);
int p9_client_unlinkat(struct p9_fid *dfid, const char *name, int flags);
int p9_client_read(struct p9_fid *fid, u64 offset, struct iov_iter *to, int *err);
int p9_client_read_once(struct p9_fid *fid, u64 offset, struct iov_iter *to,
		int *err);
int p9_client_write(struct p9_fid *fid, u64 offset, struct iov_iter *from, int *err);
struct netfs_io_subrequest;
void p9_client_write_subreq(struct netfs_io_subrequest *subreq);
int p9_client_readdir(struct p9_fid *fid, char *data, u32 count, u64 offset);
int p9dirent_read(struct p9_client *clnt, char *buf, int len,
		  struct p9_dirent *dirent);
struct p9_wstat *p9_client_stat(struct p9_fid *fid);
int p9_client_wstat(struct p9_fid *fid, struct p9_wstat *wst);
int p9_client_setattr(struct p9_fid *fid, struct p9_iattr_dotl *attr);

struct p9_stat_dotl *p9_client_getattr_dotl(struct p9_fid *fid,
							u64 request_mask);

int p9_client_mknod_dotl(struct p9_fid *oldfid, const char *name, int mode,
			dev_t rdev, kgid_t gid, struct p9_qid *qid);
int p9_client_mkdir_dotl(struct p9_fid *fid, const char *name, int mode,
				kgid_t gid, struct p9_qid *qid);
int p9_client_lock_dotl(struct p9_fid *fid, struct p9_flock *flock, u8 *status);
int p9_client_getlock_dotl(struct p9_fid *fid, struct p9_getlock *fl);
void p9_fcall_fini(struct p9_fcall *fc);
struct p9_req_t *p9_tag_lookup(struct p9_client *c, u16 tag);

static inline void p9_req_get(struct p9_req_t *r)
{
	refcount_inc(&r->refcount);
}

static inline int p9_req_try_get(struct p9_req_t *r)
{
	return refcount_inc_not_zero(&r->refcount);
}

int p9_req_put(struct p9_client *c, struct p9_req_t *r);

/* We cannot have the real tracepoints in header files,
 * use a wrapper function */
DECLARE_TRACEPOINT(9p_fid_ref);
void do_trace_9p_fid_get(struct p9_fid *fid);
void do_trace_9p_fid_put(struct p9_fid *fid);

/* fid reference counting helpers:
 *  - fids used for any length of time should always be referenced through
 *    p9_fid_get(), and released with p9_fid_put()
 *  - v9fs_fid_lookup() or similar will automatically call get for you
 *    and also require a put
 *  - the *_fid_add() helpers will stash the fid in the inode,
 *    at which point it is the responsibility of evict_inode()
 *    to call the put
 *  - the last put will automatically send a clunk to the server
 */
static inline struct p9_fid *p9_fid_get(struct p9_fid *fid)
{
	if (tracepoint_enabled(9p_fid_ref))
		do_trace_9p_fid_get(fid);

	refcount_inc(&fid->count);

	return fid;
}

static inline int p9_fid_put(struct p9_fid *fid)
{
	if (!fid || IS_ERR(fid))
		return 0;

	if (tracepoint_enabled(9p_fid_ref))
		do_trace_9p_fid_put(fid);

	if (!refcount_dec_and_test(&fid->count))
		return 0;

	return p9_client_clunk(fid);
}

void p9_client_cb(struct p9_client *c, struct p9_req_t *req, int status);

int p9_parse_header(struct p9_fcall *pdu, int32_t *size, int8_t *type,
		    int16_t *tag, int rewind);
int p9stat_read(struct p9_client *clnt, char *buf, int len,
		struct p9_wstat *st);
void p9stat_free(struct p9_wstat *stbuf);

int p9_is_proto_dotu(struct p9_client *clnt);
int p9_is_proto_dotl(struct p9_client *clnt);
struct p9_fid *p9_client_xattrwalk(struct p9_fid *file_fid,
				   const char *attr_name, u64 *attr_size);
int p9_client_xattrcreate(struct p9_fid *fid, const char *name,
			  u64 attr_size, int flags);
int p9_client_readlink(struct p9_fid *fid, char **target);

int p9_client_init(void);
void p9_client_exit(void);

#endif /* NET_9P_CLIENT_H */
