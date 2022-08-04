/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/net/9p/client.h
 *
 * 9P Client Definitions
 *
 *  Copyright (C) 2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2007 by Latchesar Ionkov <lucho@ionkov.net>
 */

#ifndef NET_9P_CLIENT_H
#define NET_9P_CLIENT_H

#include <linux/utsname.h>
#include <linux/idr.h>

/* Number of requests per row */
#define P9_ROW_MAXTAG 255

/** enum p9_proto_versions - 9P protocol versions
 * @p9_proto_legacy: 9P Legacy mode, pre-9P2000.u
 * @p9_proto_2000u: 9P2000.u extension
 * @p9_proto_2000L: 9P2000.L extension
 */

enum p9_proto_versions{
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
	struct kref refcount;
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
struct p9_client *p9_client_create(const char *dev_name, char *options);
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
int p9_client_readdir(struct p9_fid *fid, char *data, u32 count, u64 offset);
int p9dirent_read(struct p9_client *clnt, char *buf, int len,
		  struct p9_dirent *dirent);
struct p9_wstat *p9_client_stat(struct p9_fid *fid);
int p9_client_wstat(struct p9_fid *fid, struct p9_wstat *wst);
int p9_client_setattr(struct p9_fid *fid, struct p9_iattr_dotl *attr);

struct p9_stat_dotl *p9_client_getattr_dotl(struct p9_fid *fid,
							u64 request_mask);

int p9_client_mknod_dotl(struct p9_fid *oldfid, const char *name, int mode,
			dev_t rdev, kgid_t gid, struct p9_qid *);
int p9_client_mkdir_dotl(struct p9_fid *fid, const char *name, int mode,
				kgid_t gid, struct p9_qid *);
int p9_client_lock_dotl(struct p9_fid *fid, struct p9_flock *flock, u8 *status);
int p9_client_getlock_dotl(struct p9_fid *fid, struct p9_getlock *fl);
void p9_fcall_fini(struct p9_fcall *fc);
struct p9_req_t *p9_tag_lookup(struct p9_client *, u16);

static inline void p9_req_get(struct p9_req_t *r)
{
	kref_get(&r->refcount);
}

static inline int p9_req_try_get(struct p9_req_t *r)
{
	return kref_get_unless_zero(&r->refcount);
}

int p9_req_put(struct p9_req_t *r);

void p9_client_cb(struct p9_client *c, struct p9_req_t *req, int status);

int p9_parse_header(struct p9_fcall *, int32_t *, int8_t *, int16_t *, int);
int p9stat_read(struct p9_client *, char *, int, struct p9_wstat *);
void p9stat_free(struct p9_wstat *);

int p9_is_proto_dotu(struct p9_client *clnt);
int p9_is_proto_dotl(struct p9_client *clnt);
struct p9_fid *p9_client_xattrwalk(struct p9_fid *, const char *, u64 *);
int p9_client_xattrcreate(struct p9_fid *, const char *, u64, int);
int p9_client_readlink(struct p9_fid *fid, char **target);

int p9_client_init(void);
void p9_client_exit(void);

#endif /* NET_9P_CLIENT_H */
