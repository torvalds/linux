/*
 * net/9p/conv.c
 *
 * 9P protocol conversion functions
 *
 *  Copyright (C) 2004, 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/idr.h>
#include <linux/uaccess.h>
#include <net/9p/9p.h>

/*
 * Buffer to help with string parsing
 */
struct cbuf {
	unsigned char *sp;
	unsigned char *p;
	unsigned char *ep;
};

static inline void buf_init(struct cbuf *buf, void *data, int datalen)
{
	buf->sp = buf->p = data;
	buf->ep = data + datalen;
}

static inline int buf_check_overflow(struct cbuf *buf)
{
	return buf->p > buf->ep;
}

static int buf_check_size(struct cbuf *buf, int len)
{
	if (buf->p + len > buf->ep) {
		if (buf->p < buf->ep) {
			P9_EPRINTK(KERN_ERR,
				"buffer overflow: want %d has %d\n", len,
				(int)(buf->ep - buf->p));
			dump_stack();
			buf->p = buf->ep + 1;
		}

		return 0;
	}

	return 1;
}

static void *buf_alloc(struct cbuf *buf, int len)
{
	void *ret = NULL;

	if (buf_check_size(buf, len)) {
		ret = buf->p;
		buf->p += len;
	}

	return ret;
}

static void buf_put_int8(struct cbuf *buf, u8 val)
{
	if (buf_check_size(buf, 1)) {
		buf->p[0] = val;
		buf->p++;
	}
}

static void buf_put_int16(struct cbuf *buf, u16 val)
{
	if (buf_check_size(buf, 2)) {
		*(__le16 *) buf->p = cpu_to_le16(val);
		buf->p += 2;
	}
}

static void buf_put_int32(struct cbuf *buf, u32 val)
{
	if (buf_check_size(buf, 4)) {
		*(__le32 *)buf->p = cpu_to_le32(val);
		buf->p += 4;
	}
}

static void buf_put_int64(struct cbuf *buf, u64 val)
{
	if (buf_check_size(buf, 8)) {
		*(__le64 *)buf->p = cpu_to_le64(val);
		buf->p += 8;
	}
}

static char *buf_put_stringn(struct cbuf *buf, const char *s, u16 slen)
{
	char *ret;

	ret = NULL;
	if (buf_check_size(buf, slen + 2)) {
		buf_put_int16(buf, slen);
		ret = buf->p;
		memcpy(buf->p, s, slen);
		buf->p += slen;
	}

	return ret;
}

static u8 buf_get_int8(struct cbuf *buf)
{
	u8 ret = 0;

	if (buf_check_size(buf, 1)) {
		ret = buf->p[0];
		buf->p++;
	}

	return ret;
}

static u16 buf_get_int16(struct cbuf *buf)
{
	u16 ret = 0;

	if (buf_check_size(buf, 2)) {
		ret = le16_to_cpu(*(__le16 *)buf->p);
		buf->p += 2;
	}

	return ret;
}

static u32 buf_get_int32(struct cbuf *buf)
{
	u32 ret = 0;

	if (buf_check_size(buf, 4)) {
		ret = le32_to_cpu(*(__le32 *)buf->p);
		buf->p += 4;
	}

	return ret;
}

static u64 buf_get_int64(struct cbuf *buf)
{
	u64 ret = 0;

	if (buf_check_size(buf, 8)) {
		ret = le64_to_cpu(*(__le64 *)buf->p);
		buf->p += 8;
	}

	return ret;
}

static void buf_get_str(struct cbuf *buf, struct p9_str *vstr)
{
	vstr->len = buf_get_int16(buf);
	if (!buf_check_overflow(buf) && buf_check_size(buf, vstr->len)) {
		vstr->str = buf->p;
		buf->p += vstr->len;
	} else {
		vstr->len = 0;
		vstr->str = NULL;
	}
}

static void buf_get_qid(struct cbuf *bufp, struct p9_qid *qid)
{
	qid->type = buf_get_int8(bufp);
	qid->version = buf_get_int32(bufp);
	qid->path = buf_get_int64(bufp);
}

/**
 * p9_size_wstat - calculate the size of a variable length stat struct
 * @wstat: metadata (stat) structure
 * @dotu: non-zero if 9P2000.u
 *
 */

static int p9_size_wstat(struct p9_wstat *wstat, int dotu)
{
	int size = 0;

	if (wstat == NULL) {
		P9_EPRINTK(KERN_ERR, "p9_size_stat: got a NULL stat pointer\n");
		return 0;
	}

	size =			/* 2 + *//* size[2] */
	    2 +			/* type[2] */
	    4 +			/* dev[4] */
	    1 +			/* qid.type[1] */
	    4 +			/* qid.vers[4] */
	    8 +			/* qid.path[8] */
	    4 +			/* mode[4] */
	    4 +			/* atime[4] */
	    4 +			/* mtime[4] */
	    8 +			/* length[8] */
	    8;			/* minimum sum of string lengths */

	if (wstat->name)
		size += strlen(wstat->name);
	if (wstat->uid)
		size += strlen(wstat->uid);
	if (wstat->gid)
		size += strlen(wstat->gid);
	if (wstat->muid)
		size += strlen(wstat->muid);

	if (dotu) {
		size += 4 +	/* n_uid[4] */
		    4 +		/* n_gid[4] */
		    4 +		/* n_muid[4] */
		    2;		/* string length of extension[4] */
		if (wstat->extension)
			size += strlen(wstat->extension);
	}

	return size;
}

/**
 * buf_get_stat - safely decode a recieved metadata (stat) structure
 * @bufp: buffer to deserialize
 * @stat: metadata (stat) structure
 * @dotu: non-zero if 9P2000.u
 *
 */

static void
buf_get_stat(struct cbuf *bufp, struct p9_stat *stat, int dotu)
{
	stat->size = buf_get_int16(bufp);
	stat->type = buf_get_int16(bufp);
	stat->dev = buf_get_int32(bufp);
	stat->qid.type = buf_get_int8(bufp);
	stat->qid.version = buf_get_int32(bufp);
	stat->qid.path = buf_get_int64(bufp);
	stat->mode = buf_get_int32(bufp);
	stat->atime = buf_get_int32(bufp);
	stat->mtime = buf_get_int32(bufp);
	stat->length = buf_get_int64(bufp);
	buf_get_str(bufp, &stat->name);
	buf_get_str(bufp, &stat->uid);
	buf_get_str(bufp, &stat->gid);
	buf_get_str(bufp, &stat->muid);

	if (dotu) {
		buf_get_str(bufp, &stat->extension);
		stat->n_uid = buf_get_int32(bufp);
		stat->n_gid = buf_get_int32(bufp);
		stat->n_muid = buf_get_int32(bufp);
	}
}

/**
 * p9_deserialize_stat - decode a received metadata structure
 * @buf: buffer to deserialize
 * @buflen: length of received buffer
 * @stat: metadata structure to decode into
 * @dotu: non-zero if 9P2000.u
 *
 * Note: stat will point to the buf region.
 */

int
p9_deserialize_stat(void *buf, u32 buflen, struct p9_stat *stat,
		int dotu)
{
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	unsigned char *p;

	buf_init(bufp, buf, buflen);
	p = bufp->p;
	buf_get_stat(bufp, stat, dotu);

	if (buf_check_overflow(bufp))
		return 0;
	else
		return bufp->p - p;
}
EXPORT_SYMBOL(p9_deserialize_stat);

/**
 * deserialize_fcall - unmarshal a response
 * @buf: recieved buffer
 * @buflen: length of received buffer
 * @rcall: fcall structure to populate
 * @rcalllen: length of fcall structure to populate
 * @dotu: non-zero if 9P2000.u
 *
 */

int
p9_deserialize_fcall(void *buf, u32 buflen, struct p9_fcall *rcall,
		       int dotu)
{

	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	int i = 0;

	buf_init(bufp, buf, buflen);

	rcall->size = buf_get_int32(bufp);
	rcall->id = buf_get_int8(bufp);
	rcall->tag = buf_get_int16(bufp);

	P9_DPRINTK(P9_DEBUG_CONV, "size %d id %d tag %d\n", rcall->size,
							rcall->id, rcall->tag);

	switch (rcall->id) {
	default:
		P9_EPRINTK(KERN_ERR, "unknown message type: %d\n", rcall->id);
		return -EPROTO;
	case P9_RVERSION:
		rcall->params.rversion.msize = buf_get_int32(bufp);
		buf_get_str(bufp, &rcall->params.rversion.version);
		break;
	case P9_RFLUSH:
		break;
	case P9_RATTACH:
		rcall->params.rattach.qid.type = buf_get_int8(bufp);
		rcall->params.rattach.qid.version = buf_get_int32(bufp);
		rcall->params.rattach.qid.path = buf_get_int64(bufp);
		break;
	case P9_RWALK:
		rcall->params.rwalk.nwqid = buf_get_int16(bufp);
		if (rcall->params.rwalk.nwqid > P9_MAXWELEM) {
			P9_EPRINTK(KERN_ERR,
					"Rwalk with more than %d qids: %d\n",
					P9_MAXWELEM, rcall->params.rwalk.nwqid);
			return -EPROTO;
		}

		for (i = 0; i < rcall->params.rwalk.nwqid; i++)
			buf_get_qid(bufp, &rcall->params.rwalk.wqids[i]);
		break;
	case P9_ROPEN:
		buf_get_qid(bufp, &rcall->params.ropen.qid);
		rcall->params.ropen.iounit = buf_get_int32(bufp);
		break;
	case P9_RCREATE:
		buf_get_qid(bufp, &rcall->params.rcreate.qid);
		rcall->params.rcreate.iounit = buf_get_int32(bufp);
		break;
	case P9_RREAD:
		rcall->params.rread.count = buf_get_int32(bufp);
		rcall->params.rread.data = bufp->p;
		buf_check_size(bufp, rcall->params.rread.count);
		break;
	case P9_RWRITE:
		rcall->params.rwrite.count = buf_get_int32(bufp);
		break;
	case P9_RCLUNK:
		break;
	case P9_RREMOVE:
		break;
	case P9_RSTAT:
		buf_get_int16(bufp);
		buf_get_stat(bufp, &rcall->params.rstat.stat, dotu);
		break;
	case P9_RWSTAT:
		break;
	case P9_RERROR:
		buf_get_str(bufp, &rcall->params.rerror.error);
		if (dotu)
			rcall->params.rerror.errno = buf_get_int16(bufp);
		break;
	}

	if (buf_check_overflow(bufp)) {
		P9_DPRINTK(P9_DEBUG_ERROR, "buffer overflow\n");
		return -EIO;
	}

	return bufp->p - bufp->sp;
}
EXPORT_SYMBOL(p9_deserialize_fcall);

static inline void p9_put_int8(struct cbuf *bufp, u8 val, u8 * p)
{
	*p = val;
	buf_put_int8(bufp, val);
}

static inline void p9_put_int16(struct cbuf *bufp, u16 val, u16 * p)
{
	*p = val;
	buf_put_int16(bufp, val);
}

static inline void p9_put_int32(struct cbuf *bufp, u32 val, u32 * p)
{
	*p = val;
	buf_put_int32(bufp, val);
}

static inline void p9_put_int64(struct cbuf *bufp, u64 val, u64 * p)
{
	*p = val;
	buf_put_int64(bufp, val);
}

static void
p9_put_str(struct cbuf *bufp, char *data, struct p9_str *str)
{
	int len;
	char *s;

	if (data)
		len = strlen(data);
	else
		len = 0;

	s = buf_put_stringn(bufp, data, len);
	if (str) {
		str->len = len;
		str->str = s;
	}
}

static int
p9_put_data(struct cbuf *bufp, const char *data, int count,
		   unsigned char **pdata)
{
	*pdata = buf_alloc(bufp, count);
	memmove(*pdata, data, count);
	return count;
}

static int
p9_put_user_data(struct cbuf *bufp, const char __user *data, int count,
		   unsigned char **pdata)
{
	*pdata = buf_alloc(bufp, count);
	return copy_from_user(*pdata, data, count);
}

static void
p9_put_wstat(struct cbuf *bufp, struct p9_wstat *wstat,
	       struct p9_stat *stat, int statsz, int dotu)
{
	p9_put_int16(bufp, statsz, &stat->size);
	p9_put_int16(bufp, wstat->type, &stat->type);
	p9_put_int32(bufp, wstat->dev, &stat->dev);
	p9_put_int8(bufp, wstat->qid.type, &stat->qid.type);
	p9_put_int32(bufp, wstat->qid.version, &stat->qid.version);
	p9_put_int64(bufp, wstat->qid.path, &stat->qid.path);
	p9_put_int32(bufp, wstat->mode, &stat->mode);
	p9_put_int32(bufp, wstat->atime, &stat->atime);
	p9_put_int32(bufp, wstat->mtime, &stat->mtime);
	p9_put_int64(bufp, wstat->length, &stat->length);

	p9_put_str(bufp, wstat->name, &stat->name);
	p9_put_str(bufp, wstat->uid, &stat->uid);
	p9_put_str(bufp, wstat->gid, &stat->gid);
	p9_put_str(bufp, wstat->muid, &stat->muid);

	if (dotu) {
		p9_put_str(bufp, wstat->extension, &stat->extension);
		p9_put_int32(bufp, wstat->n_uid, &stat->n_uid);
		p9_put_int32(bufp, wstat->n_gid, &stat->n_gid);
		p9_put_int32(bufp, wstat->n_muid, &stat->n_muid);
	}
}

static struct p9_fcall *
p9_create_common(struct cbuf *bufp, u32 size, u8 id)
{
	struct p9_fcall *fc;

	size += 4 + 1 + 2;	/* size[4] id[1] tag[2] */
	fc = kmalloc(sizeof(struct p9_fcall) + size, GFP_KERNEL);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	fc->sdata = (char *)fc + sizeof(*fc);

	buf_init(bufp, (char *)fc->sdata, size);
	p9_put_int32(bufp, size, &fc->size);
	p9_put_int8(bufp, id, &fc->id);
	p9_put_int16(bufp, P9_NOTAG, &fc->tag);

	return fc;
}

/**
 * p9_set_tag - set the tag field of an &p9_fcall structure
 * @fc: fcall structure to set tag within
 * @tag: tag id to set
 */

void p9_set_tag(struct p9_fcall *fc, u16 tag)
{
	fc->tag = tag;
	*(__le16 *) (fc->sdata + 5) = cpu_to_le16(tag);
}
EXPORT_SYMBOL(p9_set_tag);

/**
 * p9_create_tversion - allocates and creates a T_VERSION request
 * @msize: requested maximum data size
 * @version: version string to negotiate
 *
 */
struct p9_fcall *p9_create_tversion(u32 msize, char *version)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 2 + strlen(version);	/* msize[4] version[s] */
	fc = p9_create_common(bufp, size, P9_TVERSION);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, msize, &fc->params.tversion.msize);
	p9_put_str(bufp, version, &fc->params.tversion.version);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tversion);

/**
 * p9_create_tauth - allocates and creates a T_AUTH request
 * @afid: handle to use for authentication protocol
 * @uname: user name attempting to authenticate
 * @aname: mount specifier for remote server
 * @n_uname: numeric id for user attempting to authneticate
 * @dotu: 9P2000.u extension flag
 *
 */

struct p9_fcall *p9_create_tauth(u32 afid, char *uname, char *aname,
	u32 n_uname, int dotu)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	/* afid[4] uname[s] aname[s] */
	size = 4 + 2 + 2;
	if (uname)
		size += strlen(uname);

	if (aname)
		size += strlen(aname);

	if (dotu)
		size += 4;	/* n_uname */

	fc = p9_create_common(bufp, size, P9_TAUTH);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, afid, &fc->params.tauth.afid);
	p9_put_str(bufp, uname, &fc->params.tauth.uname);
	p9_put_str(bufp, aname, &fc->params.tauth.aname);
	if (dotu)
		p9_put_int32(bufp, n_uname, &fc->params.tauth.n_uname);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tauth);

/**
 * p9_create_tattach - allocates and creates a T_ATTACH request
 * @fid: handle to use for the new mount point
 * @afid: handle to use for authentication protocol
 * @uname: user name attempting to attach
 * @aname: mount specifier for remote server
 * @n_uname: numeric id for user attempting to attach
 * @n_uname: numeric id for user attempting to attach
 * @dotu: 9P2000.u extension flag
 *
 */

struct p9_fcall *
p9_create_tattach(u32 fid, u32 afid, char *uname, char *aname,
	u32 n_uname, int dotu)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	/* fid[4] afid[4] uname[s] aname[s] */
	size = 4 + 4 + 2 + 2;
	if (uname)
		size += strlen(uname);

	if (aname)
		size += strlen(aname);

	if (dotu)
		size += 4;	/* n_uname */

	fc = p9_create_common(bufp, size, P9_TATTACH);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.tattach.fid);
	p9_put_int32(bufp, afid, &fc->params.tattach.afid);
	p9_put_str(bufp, uname, &fc->params.tattach.uname);
	p9_put_str(bufp, aname, &fc->params.tattach.aname);
	if (dotu)
		p9_put_int32(bufp, n_uname, &fc->params.tattach.n_uname);

error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tattach);

/**
 * p9_create_tflush - allocates and creates a T_FLUSH request
 * @oldtag: tag id for the transaction we are attempting to cancel
 *
 */

struct p9_fcall *p9_create_tflush(u16 oldtag)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 2;		/* oldtag[2] */
	fc = p9_create_common(bufp, size, P9_TFLUSH);
	if (IS_ERR(fc))
		goto error;

	p9_put_int16(bufp, oldtag, &fc->params.tflush.oldtag);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tflush);

/**
 * p9_create_twalk - allocates and creates a T_FLUSH request
 * @fid: handle we are traversing from
 * @newfid: a new handle for this transaction
 * @nwname: number of path elements to traverse
 * @wnames: array of path elements
 *
 */

struct p9_fcall *p9_create_twalk(u32 fid, u32 newfid, u16 nwname,
				     char **wnames)
{
	int i, size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	if (nwname > P9_MAXWELEM) {
		P9_DPRINTK(P9_DEBUG_ERROR, "nwname > %d\n", P9_MAXWELEM);
		return NULL;
	}

	size = 4 + 4 + 2;	/* fid[4] newfid[4] nwname[2] ... */
	for (i = 0; i < nwname; i++) {
		size += 2 + strlen(wnames[i]);	/* wname[s] */
	}

	fc = p9_create_common(bufp, size, P9_TWALK);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.twalk.fid);
	p9_put_int32(bufp, newfid, &fc->params.twalk.newfid);
	p9_put_int16(bufp, nwname, &fc->params.twalk.nwname);
	for (i = 0; i < nwname; i++) {
		p9_put_str(bufp, wnames[i], &fc->params.twalk.wnames[i]);
	}

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_twalk);

/**
 * p9_create_topen - allocates and creates a T_OPEN request
 * @fid: handle we are trying to open
 * @mode: what mode we are trying to open the file in
 *
 */

struct p9_fcall *p9_create_topen(u32 fid, u8 mode)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 1;		/* fid[4] mode[1] */
	fc = p9_create_common(bufp, size, P9_TOPEN);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.topen.fid);
	p9_put_int8(bufp, mode, &fc->params.topen.mode);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_topen);

/**
 * p9_create_tcreate - allocates and creates a T_CREATE request
 * @fid: handle of directory we are trying to create in
 * @name: name of the file we are trying to create
 * @perm: permissions for the file we are trying to create
 * @mode: what mode we are trying to open the file in
 * @extension: 9p2000.u extension string (for special files)
 * @dotu: 9p2000.u enabled flag
 *
 * Note: Plan 9 create semantics include opening the resulting file
 * which is why mode is included.
 */

struct p9_fcall *p9_create_tcreate(u32 fid, char *name, u32 perm, u8 mode,
	char *extension, int dotu)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	/* fid[4] name[s] perm[4] mode[1] */
	size = 4 + 2 + strlen(name) + 4 + 1;
	if (dotu) {
		size += 2 +			/* extension[s] */
		    (extension == NULL ? 0 : strlen(extension));
	}

	fc = p9_create_common(bufp, size, P9_TCREATE);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.tcreate.fid);
	p9_put_str(bufp, name, &fc->params.tcreate.name);
	p9_put_int32(bufp, perm, &fc->params.tcreate.perm);
	p9_put_int8(bufp, mode, &fc->params.tcreate.mode);
	if (dotu)
		p9_put_str(bufp, extension, &fc->params.tcreate.extension);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tcreate);

/**
 * p9_create_tread - allocates and creates a T_READ request
 * @fid: handle of the file we are trying to read
 * @offset: offset to start reading from
 * @count: how many bytes to read
 */

struct p9_fcall *p9_create_tread(u32 fid, u64 offset, u32 count)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 8 + 4;	/* fid[4] offset[8] count[4] */
	fc = p9_create_common(bufp, size, P9_TREAD);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.tread.fid);
	p9_put_int64(bufp, offset, &fc->params.tread.offset);
	p9_put_int32(bufp, count, &fc->params.tread.count);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tread);

/**
 * p9_create_twrite - allocates and creates a T_WRITE request from the kernel
 * @fid: handle of the file we are trying to write
 * @offset: offset to start writing at
 * @count: how many bytes to write
 * @data: data to write
 *
 * This function will create a requst with data buffers from the kernel
 * such as the page cache.
 */

struct p9_fcall *p9_create_twrite(u32 fid, u64 offset, u32 count,
				      const char *data)
{
	int size, err;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	/* fid[4] offset[8] count[4] data[count] */
	size = 4 + 8 + 4 + count;
	fc = p9_create_common(bufp, size, P9_TWRITE);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.twrite.fid);
	p9_put_int64(bufp, offset, &fc->params.twrite.offset);
	p9_put_int32(bufp, count, &fc->params.twrite.count);
	err = p9_put_data(bufp, data, count, &fc->params.twrite.data);
	if (err) {
		kfree(fc);
		fc = ERR_PTR(err);
		goto error;
	}

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_twrite);

/**
 * p9_create_twrite_u - allocates and creates a T_WRITE request from userspace
 * @fid: handle of the file we are trying to write
 * @offset: offset to start writing at
 * @count: how many bytes to write
 * @data: data to write
 *
 * This function will create a request with data buffers from userspace
 */

struct p9_fcall *p9_create_twrite_u(u32 fid, u64 offset, u32 count,
				      const char __user *data)
{
	int size, err;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	/* fid[4] offset[8] count[4] data[count] */
	size = 4 + 8 + 4 + count;
	fc = p9_create_common(bufp, size, P9_TWRITE);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.twrite.fid);
	p9_put_int64(bufp, offset, &fc->params.twrite.offset);
	p9_put_int32(bufp, count, &fc->params.twrite.count);
	err = p9_put_user_data(bufp, data, count, &fc->params.twrite.data);
	if (err) {
		kfree(fc);
		fc = ERR_PTR(err);
		goto error;
	}

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_twrite_u);

/**
 * p9_create_tclunk - allocate a request to forget about a file handle
 * @fid: handle of the file we closing or forgetting about
 *
 * clunk is used both to close open files and to discard transient handles
 * which may be created during meta-data operations and hierarchy traversal.
 */

struct p9_fcall *p9_create_tclunk(u32 fid)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4;		/* fid[4] */
	fc = p9_create_common(bufp, size, P9_TCLUNK);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.tclunk.fid);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tclunk);

/**
 * p9_create_tremove - allocate and create a request to remove a file
 * @fid: handle of the file or directory we are removing
 *
 */

struct p9_fcall *p9_create_tremove(u32 fid)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4;		/* fid[4] */
	fc = p9_create_common(bufp, size, P9_TREMOVE);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.tremove.fid);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tremove);

/**
 * p9_create_tstat - allocate and populate a request for attributes
 * @fid: handle of the file or directory we are trying to get the attributes of
 *
 */

struct p9_fcall *p9_create_tstat(u32 fid)
{
	int size;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4;		/* fid[4] */
	fc = p9_create_common(bufp, size, P9_TSTAT);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.tstat.fid);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_tstat);

/**
 * p9_create_tstat - allocate and populate a request to change attributes
 * @fid: handle of the file or directory we are trying to change
 * @wstat: &p9_stat structure with attributes we wish to set
 * @dotu: 9p2000.u enabled flag
 *
 */

struct p9_fcall *p9_create_twstat(u32 fid, struct p9_wstat *wstat,
				      int dotu)
{
	int size, statsz;
	struct p9_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	statsz = p9_size_wstat(wstat, dotu);
	size = 4 + 2 + 2 + statsz;	/* fid[4] stat[n] */
	fc = p9_create_common(bufp, size, P9_TWSTAT);
	if (IS_ERR(fc))
		goto error;

	p9_put_int32(bufp, fid, &fc->params.twstat.fid);
	buf_put_int16(bufp, statsz + 2);
	p9_put_wstat(bufp, wstat, &fc->params.twstat.stat, statsz, dotu);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
error:
	return fc;
}
EXPORT_SYMBOL(p9_create_twstat);

