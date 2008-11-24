/*
 * include/net/9p/client.h
 *
 * 9P Client Definitions
 *
 *  Copyright (C) 2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2007 by Latchesar Ionkov <lucho@ionkov.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#ifndef NET_9P_CLIENT_H
#define NET_9P_CLIENT_H

/* Number of requests per row */
#define P9_ROW_MAXTAG 255

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
	Disconnected,
	Hung,
};

/**
 * enum p9_req_status_t - virtio request status
 * @REQ_STATUS_IDLE: request slot unused
 * @REQ_STATUS_ALLOC: request has been allocated but not sent
 * @REQ_STATUS_UNSENT: request waiting to be sent
 * @REQ_STATUS_SENT: request sent to server
 * @REQ_STATUS_FLSH: a flush has been sent for this request
 * @REQ_STATUS_RCVD: response received from server
 * @REQ_STATUS_FLSHD: request has been flushed
 * @REQ_STATUS_ERROR: request encountered an error on the client side
 *
 * The @REQ_STATUS_IDLE state is used to mark a request slot as unused
 * but use is actually tracked by the idpool structure which handles tag
 * id allocation.
 *
 */

enum p9_req_status_t {
	REQ_STATUS_IDLE,
	REQ_STATUS_ALLOC,
	REQ_STATUS_UNSENT,
	REQ_STATUS_SENT,
	REQ_STATUS_FLSH,
	REQ_STATUS_RCVD,
	REQ_STATUS_FLSHD,
	REQ_STATUS_ERROR,
};

/**
 * struct p9_req_t - request slots
 * @status: status of this request slot
 * @t_err: transport error
 * @flush_tag: tag of request being flushed (for flush requests)
 * @wq: wait_queue for the client to block on for this request
 * @tc: the request fcall structure
 * @rc: the response fcall structure
 * @aux: transport specific data (provided for trans_fd migration)
 * @req_list: link for higher level objects to chain requests
 *
 * Transport use an array to track outstanding requests
 * instead of a list.  While this may incurr overhead during initial
 * allocation or expansion, it makes request lookup much easier as the
 * tag id is a index into an array.  (We use tag+1 so that we can accomodate
 * the -1 tag for the T_VERSION request).
 * This also has the nice effect of only having to allocate wait_queues
 * once, instead of constantly allocating and freeing them.  Its possible
 * other resources could benefit from this scheme as well.
 *
 */

struct p9_req_t {
	int status;
	int t_err;
	u16 flush_tag;
	wait_queue_head_t *wq;
	struct p9_fcall *tc;
	struct p9_fcall *rc;
	void *aux;

	struct list_head req_list;
};

/**
 * struct p9_client - per client instance state
 * @lock: protect @fidlist
 * @msize: maximum data size negotiated by protocol
 * @dotu: extension flags negotiated by protocol
 * @trans_mod: module API instantiated with this client
 * @trans: tranport instance state and API
 * @conn: connection state information used by trans_fd
 * @fidpool: fid handle accounting for session
 * @fidlist: List of active fid handles
 * @tagpool - transaction id accounting for session
 * @reqs - 2D array of requests
 * @max_tag - current maximum tag id allocated
 *
 * The client structure is used to keep track of various per-client
 * state that has been instantiated.
 * In order to minimize per-transaction overhead we use a
 * simple array to lookup requests instead of a hash table
 * or linked list.  In order to support larger number of
 * transactions, we make this a 2D array, allocating new rows
 * when we need to grow the total number of the transactions.
 *
 * Each row is 256 requests and we'll support up to 256 rows for
 * a total of 64k concurrent requests per session.
 *
 * Bugs: duplicated data and potentially unnecessary elements.
 */

struct p9_client {
	spinlock_t lock; /* protect client structure */
	int msize;
	unsigned char dotu;
	struct p9_trans_module *trans_mod;
	enum p9_trans_status status;
	void *trans;
	struct p9_conn *conn;

	struct p9_idpool *fidpool;
	struct list_head fidlist;

	struct p9_idpool *tagpool;
	struct p9_req_t *reqs[P9_ROW_MAXTAG];
	int max_tag;
};

/**
 * struct p9_fid - file system entity handle
 * @clnt: back pointer to instantiating &p9_client
 * @fid: numeric identifier for this handle
 * @mode: current mode of this fid (enum?)
 * @qid: the &p9_qid server identifier this handle points to
 * @iounit: the server reported maximum transaction size for this file
 * @uid: the numeric uid of the local user who owns this handle
 * @aux: transport specific information (unused?)
 * @rdir_fpos: tracks offset of file position when reading directory contents
 * @flist: per-client-instance fid tracking
 * @dlist: per-dentry fid tracking
 *
 * TODO: This needs lots of explanation.
 */

struct p9_fid {
	struct p9_client *clnt;
	u32 fid;
	int mode;
	struct p9_qid qid;
	u32 iounit;
	uid_t uid;
	void *aux;

	int rdir_fpos;
	struct list_head flist;
	struct list_head dlist;	/* list of all fids attached to a dentry */
};

int p9_client_version(struct p9_client *);
struct p9_client *p9_client_create(const char *dev_name, char *options);
void p9_client_destroy(struct p9_client *clnt);
void p9_client_disconnect(struct p9_client *clnt);
struct p9_fid *p9_client_attach(struct p9_client *clnt, struct p9_fid *afid,
					char *uname, u32 n_uname, char *aname);
struct p9_fid *p9_client_auth(struct p9_client *clnt, char *uname,
						u32 n_uname, char *aname);
struct p9_fid *p9_client_walk(struct p9_fid *oldfid, int nwname, char **wnames,
								int clone);
int p9_client_open(struct p9_fid *fid, int mode);
int p9_client_fcreate(struct p9_fid *fid, char *name, u32 perm, int mode,
							char *extension);
int p9_client_clunk(struct p9_fid *fid);
int p9_client_remove(struct p9_fid *fid);
int p9_client_read(struct p9_fid *fid, char *data, char __user *udata,
							u64 offset, u32 count);
int p9_client_write(struct p9_fid *fid, char *data, const char __user *udata,
							u64 offset, u32 count);
struct p9_wstat *p9_client_stat(struct p9_fid *fid);
int p9_client_wstat(struct p9_fid *fid, struct p9_wstat *wst);

struct p9_req_t *p9_tag_lookup(struct p9_client *, u16);
void p9_client_cb(struct p9_client *c, struct p9_req_t *req);

int p9_parse_header(struct p9_fcall *, int32_t *, int8_t *, int16_t *, int);
int p9stat_read(char *, int, struct p9_wstat *, int);
void p9stat_free(struct p9_wstat *);


#endif /* NET_9P_CLIENT_H */
