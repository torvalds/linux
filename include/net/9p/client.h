/*
 * include/net/9p/client.h
 *
 * 9P Client Definitions
 *
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

struct p9_client {
	spinlock_t lock; /* protect client structure */
	int msize;
	unsigned char dotu;
	struct p9_trans *trans;
	struct p9_conn *conn;

	struct p9_idpool *fidpool;
	struct list_head fidlist;
};

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

struct p9_client *p9_client_create(struct p9_trans *trans, int msize,
								int dotu);
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
