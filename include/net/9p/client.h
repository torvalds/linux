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
 *
 * The client structure is used to keep track of various per-client
 * state that has been instantiated.
 *
 * Bugs: duplicated data and potentially unnecessary elements.
 */

struct p9_client {
	spinlock_t lock; /* protect client structure */
	int msize;
	unsigned char dotu;
	struct p9_trans_module *trans_mod;
	struct p9_trans *trans;
	struct p9_conn *conn;

	struct p9_idpool *fidpool;
	struct list_head fidlist;
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
 * @rdir_pos: (unused?)
 * @rdir_fcall: holds response of last directory read request
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
	int rdir_pos;
	struct p9_fcall *rdir_fcall;
	struct list_head flist;
	struct list_head dlist;	/* list of all fids attached to a dentry */
};

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
int p9_client_read(struct p9_fid *fid, char *data, u64 offset, u32 count);
int p9_client_readn(struct p9_fid *fid, char *data, u64 offset, u32 count);
int p9_client_write(struct p9_fid *fid, char *data, u64 offset, u32 count);
int p9_client_uread(struct p9_fid *fid, char __user *data, u64 offset,
								u32 count);
int p9_client_uwrite(struct p9_fid *fid, const char __user *data, u64 offset,
								u32 count);
struct p9_stat *p9_client_stat(struct p9_fid *fid);
int p9_client_wstat(struct p9_fid *fid, struct p9_wstat *wst);
struct p9_stat *p9_client_dirread(struct p9_fid *fid, u64 offset);

#endif /* NET_9P_CLIENT_H */
