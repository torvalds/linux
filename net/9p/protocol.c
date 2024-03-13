// SPDX-License-Identifier: GPL-2.0-only
/*
 * 9P Protocol Support Code
 *
 *  Copyright (C) 2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *
 *  Base on code from Anthony Liguori <aliguori@us.ibm.com>
 *  Copyright (C) 2008 by IBM, Corp.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/uio.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include "protocol.h"

#include <trace/events/9p.h>

/* len[2] text[len] */
#define P9_STRLEN(s) \
	(2 + min_t(size_t, s ? strlen(s) : 0, USHRT_MAX))

/**
 * p9_msg_buf_size - Returns a buffer size sufficiently large to hold the
 * intended 9p message.
 * @c: client
 * @type: message type
 * @fmt: format template for assembling request message
 * (see p9pdu_vwritef)
 * @ap: variable arguments to be fed to passed format template
 * (see p9pdu_vwritef)
 *
 * Note: Even for response types (P9_R*) the format template and variable
 * arguments must always be for the originating request type (P9_T*).
 */
size_t p9_msg_buf_size(struct p9_client *c, enum p9_msg_t type,
			const char *fmt, va_list ap)
{
	/* size[4] type[1] tag[2] */
	const int hdr = 4 + 1 + 2;
	/* ename[s] errno[4] */
	const int rerror_size = hdr + P9_ERRMAX + 4;
	/* ecode[4] */
	const int rlerror_size = hdr + 4;
	const int err_size =
		c->proto_version == p9_proto_2000L ? rlerror_size : rerror_size;

	static_assert(NAME_MAX <= 4*1024, "p9_msg_buf_size() currently assumes "
				  "a max. allowed directory entry name length of 4k");

	switch (type) {

	/* message types not used at all */
	case P9_TERROR:
	case P9_TLERROR:
	case P9_TAUTH:
	case P9_RAUTH:
		BUG();

	/* variable length & potentially large message types */
	case P9_TATTACH:
		BUG_ON(strcmp("ddss?u", fmt));
		va_arg(ap, int32_t);
		va_arg(ap, int32_t);
		{
			const char *uname = va_arg(ap, const char *);
			const char *aname = va_arg(ap, const char *);
			/* fid[4] afid[4] uname[s] aname[s] n_uname[4] */
			return hdr + 4 + 4 + P9_STRLEN(uname) + P9_STRLEN(aname) + 4;
		}
	case P9_TWALK:
		BUG_ON(strcmp("ddT", fmt));
		va_arg(ap, int32_t);
		va_arg(ap, int32_t);
		{
			uint i, nwname = va_arg(ap, int);
			size_t wname_all;
			const char **wnames = va_arg(ap, const char **);
			for (i = 0, wname_all = 0; i < nwname; ++i) {
				wname_all += P9_STRLEN(wnames[i]);
			}
			/* fid[4] newfid[4] nwname[2] nwname*(wname[s]) */
			return hdr + 4 + 4 + 2 + wname_all;
		}
	case P9_RWALK:
		BUG_ON(strcmp("ddT", fmt));
		va_arg(ap, int32_t);
		va_arg(ap, int32_t);
		{
			uint nwname = va_arg(ap, int);
			/* nwqid[2] nwqid*(wqid[13]) */
			return max_t(size_t, hdr + 2 + nwname * 13, err_size);
		}
	case P9_TCREATE:
		BUG_ON(strcmp("dsdb?s", fmt));
		va_arg(ap, int32_t);
		{
			const char *name = va_arg(ap, const char *);
			if (c->proto_version == p9_proto_legacy) {
				/* fid[4] name[s] perm[4] mode[1] */
				return hdr + 4 + P9_STRLEN(name) + 4 + 1;
			} else {
				va_arg(ap, int32_t);
				va_arg(ap, int);
				{
					const char *ext = va_arg(ap, const char *);
					/* fid[4] name[s] perm[4] mode[1] extension[s] */
					return hdr + 4 + P9_STRLEN(name) + 4 + 1 + P9_STRLEN(ext);
				}
			}
		}
	case P9_TLCREATE:
		BUG_ON(strcmp("dsddg", fmt));
		va_arg(ap, int32_t);
		{
			const char *name = va_arg(ap, const char *);
			/* fid[4] name[s] flags[4] mode[4] gid[4] */
			return hdr + 4 + P9_STRLEN(name) + 4 + 4 + 4;
		}
	case P9_RREAD:
	case P9_RREADDIR:
		BUG_ON(strcmp("dqd", fmt));
		va_arg(ap, int32_t);
		va_arg(ap, int64_t);
		{
			const int32_t count = va_arg(ap, int32_t);
			/* count[4] data[count] */
			return max_t(size_t, hdr + 4 + count, err_size);
		}
	case P9_TWRITE:
		BUG_ON(strcmp("dqV", fmt));
		va_arg(ap, int32_t);
		va_arg(ap, int64_t);
		{
			const int32_t count = va_arg(ap, int32_t);
			/* fid[4] offset[8] count[4] data[count] */
			return hdr + 4 + 8 + 4 + count;
		}
	case P9_TRENAMEAT:
		BUG_ON(strcmp("dsds", fmt));
		va_arg(ap, int32_t);
		{
			const char *oldname, *newname;
			oldname = va_arg(ap, const char *);
			va_arg(ap, int32_t);
			newname = va_arg(ap, const char *);
			/* olddirfid[4] oldname[s] newdirfid[4] newname[s] */
			return hdr + 4 + P9_STRLEN(oldname) + 4 + P9_STRLEN(newname);
		}
	case P9_TSYMLINK:
		BUG_ON(strcmp("dssg", fmt));
		va_arg(ap, int32_t);
		{
			const char *name = va_arg(ap, const char *);
			const char *symtgt = va_arg(ap, const char *);
			/* fid[4] name[s] symtgt[s] gid[4] */
			return hdr + 4 + P9_STRLEN(name) + P9_STRLEN(symtgt) + 4;
		}

	case P9_RERROR:
		return rerror_size;
	case P9_RLERROR:
		return rlerror_size;

	/* small message types */
	case P9_TWSTAT:
	case P9_RSTAT:
	case P9_RREADLINK:
	case P9_TXATTRWALK:
	case P9_TXATTRCREATE:
	case P9_TLINK:
	case P9_TMKDIR:
	case P9_TMKNOD:
	case P9_TRENAME:
	case P9_TUNLINKAT:
	case P9_TLOCK:
		return 8 * 1024;

	/* tiny message types */
	default:
		return 4 * 1024;

	}
}

static int
p9pdu_writef(struct p9_fcall *pdu, int proto_version, const char *fmt, ...);

void p9stat_free(struct p9_wstat *stbuf)
{
	kfree(stbuf->name);
	stbuf->name = NULL;
	kfree(stbuf->uid);
	stbuf->uid = NULL;
	kfree(stbuf->gid);
	stbuf->gid = NULL;
	kfree(stbuf->muid);
	stbuf->muid = NULL;
	kfree(stbuf->extension);
	stbuf->extension = NULL;
}
EXPORT_SYMBOL(p9stat_free);

size_t pdu_read(struct p9_fcall *pdu, void *data, size_t size)
{
	size_t len = min(pdu->size - pdu->offset, size);

	memcpy(data, &pdu->sdata[pdu->offset], len);
	pdu->offset += len;
	return size - len;
}

static size_t pdu_write(struct p9_fcall *pdu, const void *data, size_t size)
{
	size_t len = min(pdu->capacity - pdu->size, size);

	memcpy(&pdu->sdata[pdu->size], data, len);
	pdu->size += len;
	return size - len;
}

static size_t
pdu_write_u(struct p9_fcall *pdu, struct iov_iter *from, size_t size)
{
	size_t len = min(pdu->capacity - pdu->size, size);

	if (!copy_from_iter_full(&pdu->sdata[pdu->size], len, from))
		len = 0;

	pdu->size += len;
	return size - len;
}

/*	b - int8_t
 *	w - int16_t
 *	d - int32_t
 *	q - int64_t
 *	s - string
 *	u - numeric uid
 *	g - numeric gid
 *	S - stat
 *	Q - qid
 *	D - data blob (int32_t size followed by void *, results are not freed)
 *	T - array of strings (int16_t count, followed by strings)
 *	R - array of qids (int16_t count, followed by qids)
 *	A - stat for 9p2000.L (p9_stat_dotl)
 *	? - if optional = 1, continue parsing
 */

static int
p9pdu_vreadf(struct p9_fcall *pdu, int proto_version, const char *fmt,
	     va_list ap)
{
	const char *ptr;
	int errcode = 0;

	for (ptr = fmt; *ptr; ptr++) {
		switch (*ptr) {
		case 'b':{
				int8_t *val = va_arg(ap, int8_t *);
				if (pdu_read(pdu, val, sizeof(*val))) {
					errcode = -EFAULT;
					break;
				}
			}
			break;
		case 'w':{
				int16_t *val = va_arg(ap, int16_t *);
				__le16 le_val;
				if (pdu_read(pdu, &le_val, sizeof(le_val))) {
					errcode = -EFAULT;
					break;
				}
				*val = le16_to_cpu(le_val);
			}
			break;
		case 'd':{
				int32_t *val = va_arg(ap, int32_t *);
				__le32 le_val;
				if (pdu_read(pdu, &le_val, sizeof(le_val))) {
					errcode = -EFAULT;
					break;
				}
				*val = le32_to_cpu(le_val);
			}
			break;
		case 'q':{
				int64_t *val = va_arg(ap, int64_t *);
				__le64 le_val;
				if (pdu_read(pdu, &le_val, sizeof(le_val))) {
					errcode = -EFAULT;
					break;
				}
				*val = le64_to_cpu(le_val);
			}
			break;
		case 's':{
				char **sptr = va_arg(ap, char **);
				uint16_t len;

				errcode = p9pdu_readf(pdu, proto_version,
								"w", &len);
				if (errcode)
					break;

				*sptr = kmalloc(len + 1, GFP_NOFS);
				if (*sptr == NULL) {
					errcode = -ENOMEM;
					break;
				}
				if (pdu_read(pdu, *sptr, len)) {
					errcode = -EFAULT;
					kfree(*sptr);
					*sptr = NULL;
				} else
					(*sptr)[len] = 0;
			}
			break;
		case 'u': {
				kuid_t *uid = va_arg(ap, kuid_t *);
				__le32 le_val;
				if (pdu_read(pdu, &le_val, sizeof(le_val))) {
					errcode = -EFAULT;
					break;
				}
				*uid = make_kuid(&init_user_ns,
						 le32_to_cpu(le_val));
			} break;
		case 'g': {
				kgid_t *gid = va_arg(ap, kgid_t *);
				__le32 le_val;
				if (pdu_read(pdu, &le_val, sizeof(le_val))) {
					errcode = -EFAULT;
					break;
				}
				*gid = make_kgid(&init_user_ns,
						 le32_to_cpu(le_val));
			} break;
		case 'Q':{
				struct p9_qid *qid =
				    va_arg(ap, struct p9_qid *);

				errcode = p9pdu_readf(pdu, proto_version, "bdq",
						      &qid->type, &qid->version,
						      &qid->path);
			}
			break;
		case 'S':{
				struct p9_wstat *stbuf =
				    va_arg(ap, struct p9_wstat *);

				memset(stbuf, 0, sizeof(struct p9_wstat));
				stbuf->n_uid = stbuf->n_muid = INVALID_UID;
				stbuf->n_gid = INVALID_GID;

				errcode =
				    p9pdu_readf(pdu, proto_version,
						"wwdQdddqssss?sugu",
						&stbuf->size, &stbuf->type,
						&stbuf->dev, &stbuf->qid,
						&stbuf->mode, &stbuf->atime,
						&stbuf->mtime, &stbuf->length,
						&stbuf->name, &stbuf->uid,
						&stbuf->gid, &stbuf->muid,
						&stbuf->extension,
						&stbuf->n_uid, &stbuf->n_gid,
						&stbuf->n_muid);
				if (errcode)
					p9stat_free(stbuf);
			}
			break;
		case 'D':{
				uint32_t *count = va_arg(ap, uint32_t *);
				void **data = va_arg(ap, void **);

				errcode =
				    p9pdu_readf(pdu, proto_version, "d", count);
				if (!errcode) {
					*count =
					    min_t(uint32_t, *count,
						  pdu->size - pdu->offset);
					*data = &pdu->sdata[pdu->offset];
				}
			}
			break;
		case 'T':{
				uint16_t *nwname = va_arg(ap, uint16_t *);
				char ***wnames = va_arg(ap, char ***);

				errcode = p9pdu_readf(pdu, proto_version,
								"w", nwname);
				if (!errcode) {
					*wnames =
					    kmalloc_array(*nwname,
							  sizeof(char *),
							  GFP_NOFS);
					if (!*wnames)
						errcode = -ENOMEM;
				}

				if (!errcode) {
					int i;

					for (i = 0; i < *nwname; i++) {
						errcode =
						    p9pdu_readf(pdu,
								proto_version,
								"s",
								&(*wnames)[i]);
						if (errcode)
							break;
					}
				}

				if (errcode) {
					if (*wnames) {
						int i;

						for (i = 0; i < *nwname; i++)
							kfree((*wnames)[i]);
					}
					kfree(*wnames);
					*wnames = NULL;
				}
			}
			break;
		case 'R':{
				uint16_t *nwqid = va_arg(ap, uint16_t *);
				struct p9_qid **wqids =
				    va_arg(ap, struct p9_qid **);

				*wqids = NULL;

				errcode =
				    p9pdu_readf(pdu, proto_version, "w", nwqid);
				if (!errcode) {
					*wqids =
					    kmalloc_array(*nwqid,
							  sizeof(struct p9_qid),
							  GFP_NOFS);
					if (*wqids == NULL)
						errcode = -ENOMEM;
				}

				if (!errcode) {
					int i;

					for (i = 0; i < *nwqid; i++) {
						errcode =
						    p9pdu_readf(pdu,
								proto_version,
								"Q",
								&(*wqids)[i]);
						if (errcode)
							break;
					}
				}

				if (errcode) {
					kfree(*wqids);
					*wqids = NULL;
				}
			}
			break;
		case 'A': {
				struct p9_stat_dotl *stbuf =
				    va_arg(ap, struct p9_stat_dotl *);

				memset(stbuf, 0, sizeof(struct p9_stat_dotl));
				errcode =
				    p9pdu_readf(pdu, proto_version,
					"qQdugqqqqqqqqqqqqqqq",
					&stbuf->st_result_mask,
					&stbuf->qid,
					&stbuf->st_mode,
					&stbuf->st_uid, &stbuf->st_gid,
					&stbuf->st_nlink,
					&stbuf->st_rdev, &stbuf->st_size,
					&stbuf->st_blksize, &stbuf->st_blocks,
					&stbuf->st_atime_sec,
					&stbuf->st_atime_nsec,
					&stbuf->st_mtime_sec,
					&stbuf->st_mtime_nsec,
					&stbuf->st_ctime_sec,
					&stbuf->st_ctime_nsec,
					&stbuf->st_btime_sec,
					&stbuf->st_btime_nsec,
					&stbuf->st_gen,
					&stbuf->st_data_version);
			}
			break;
		case '?':
			if ((proto_version != p9_proto_2000u) &&
				(proto_version != p9_proto_2000L))
				return 0;
			break;
		default:
			BUG();
			break;
		}

		if (errcode)
			break;
	}

	return errcode;
}

int
p9pdu_vwritef(struct p9_fcall *pdu, int proto_version, const char *fmt,
	va_list ap)
{
	const char *ptr;
	int errcode = 0;

	for (ptr = fmt; *ptr; ptr++) {
		switch (*ptr) {
		case 'b':{
				int8_t val = va_arg(ap, int);
				if (pdu_write(pdu, &val, sizeof(val)))
					errcode = -EFAULT;
			}
			break;
		case 'w':{
				__le16 val = cpu_to_le16(va_arg(ap, int));
				if (pdu_write(pdu, &val, sizeof(val)))
					errcode = -EFAULT;
			}
			break;
		case 'd':{
				__le32 val = cpu_to_le32(va_arg(ap, int32_t));
				if (pdu_write(pdu, &val, sizeof(val)))
					errcode = -EFAULT;
			}
			break;
		case 'q':{
				__le64 val = cpu_to_le64(va_arg(ap, int64_t));
				if (pdu_write(pdu, &val, sizeof(val)))
					errcode = -EFAULT;
			}
			break;
		case 's':{
				const char *sptr = va_arg(ap, const char *);
				uint16_t len = 0;
				if (sptr)
					len = min_t(size_t, strlen(sptr),
								USHRT_MAX);

				errcode = p9pdu_writef(pdu, proto_version,
								"w", len);
				if (!errcode && pdu_write(pdu, sptr, len))
					errcode = -EFAULT;
			}
			break;
		case 'u': {
				kuid_t uid = va_arg(ap, kuid_t);
				__le32 val = cpu_to_le32(
						from_kuid(&init_user_ns, uid));
				if (pdu_write(pdu, &val, sizeof(val)))
					errcode = -EFAULT;
			} break;
		case 'g': {
				kgid_t gid = va_arg(ap, kgid_t);
				__le32 val = cpu_to_le32(
						from_kgid(&init_user_ns, gid));
				if (pdu_write(pdu, &val, sizeof(val)))
					errcode = -EFAULT;
			} break;
		case 'Q':{
				const struct p9_qid *qid =
				    va_arg(ap, const struct p9_qid *);
				errcode =
				    p9pdu_writef(pdu, proto_version, "bdq",
						 qid->type, qid->version,
						 qid->path);
			} break;
		case 'S':{
				const struct p9_wstat *stbuf =
				    va_arg(ap, const struct p9_wstat *);
				errcode =
				    p9pdu_writef(pdu, proto_version,
						 "wwdQdddqssss?sugu",
						 stbuf->size, stbuf->type,
						 stbuf->dev, &stbuf->qid,
						 stbuf->mode, stbuf->atime,
						 stbuf->mtime, stbuf->length,
						 stbuf->name, stbuf->uid,
						 stbuf->gid, stbuf->muid,
						 stbuf->extension, stbuf->n_uid,
						 stbuf->n_gid, stbuf->n_muid);
			} break;
		case 'V':{
				uint32_t count = va_arg(ap, uint32_t);
				struct iov_iter *from =
						va_arg(ap, struct iov_iter *);
				errcode = p9pdu_writef(pdu, proto_version, "d",
									count);
				if (!errcode && pdu_write_u(pdu, from, count))
					errcode = -EFAULT;
			}
			break;
		case 'T':{
				uint16_t nwname = va_arg(ap, int);
				const char **wnames = va_arg(ap, const char **);

				errcode = p9pdu_writef(pdu, proto_version, "w",
									nwname);
				if (!errcode) {
					int i;

					for (i = 0; i < nwname; i++) {
						errcode =
						    p9pdu_writef(pdu,
								proto_version,
								 "s",
								 wnames[i]);
						if (errcode)
							break;
					}
				}
			}
			break;
		case 'R':{
				uint16_t nwqid = va_arg(ap, int);
				struct p9_qid *wqids =
				    va_arg(ap, struct p9_qid *);

				errcode = p9pdu_writef(pdu, proto_version, "w",
									nwqid);
				if (!errcode) {
					int i;

					for (i = 0; i < nwqid; i++) {
						errcode =
						    p9pdu_writef(pdu,
								proto_version,
								 "Q",
								 &wqids[i]);
						if (errcode)
							break;
					}
				}
			}
			break;
		case 'I':{
				struct p9_iattr_dotl *p9attr = va_arg(ap,
							struct p9_iattr_dotl *);

				errcode = p9pdu_writef(pdu, proto_version,
							"ddugqqqqq",
							p9attr->valid,
							p9attr->mode,
							p9attr->uid,
							p9attr->gid,
							p9attr->size,
							p9attr->atime_sec,
							p9attr->atime_nsec,
							p9attr->mtime_sec,
							p9attr->mtime_nsec);
			}
			break;
		case '?':
			if ((proto_version != p9_proto_2000u) &&
				(proto_version != p9_proto_2000L))
				return 0;
			break;
		default:
			BUG();
			break;
		}

		if (errcode)
			break;
	}

	return errcode;
}

int p9pdu_readf(struct p9_fcall *pdu, int proto_version, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = p9pdu_vreadf(pdu, proto_version, fmt, ap);
	va_end(ap);

	return ret;
}

static int
p9pdu_writef(struct p9_fcall *pdu, int proto_version, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = p9pdu_vwritef(pdu, proto_version, fmt, ap);
	va_end(ap);

	return ret;
}

int p9stat_read(struct p9_client *clnt, char *buf, int len, struct p9_wstat *st)
{
	struct p9_fcall fake_pdu;
	int ret;

	fake_pdu.size = len;
	fake_pdu.capacity = len;
	fake_pdu.sdata = buf;
	fake_pdu.offset = 0;

	ret = p9pdu_readf(&fake_pdu, clnt->proto_version, "S", st);
	if (ret) {
		p9_debug(P9_DEBUG_9P, "<<< p9stat_read failed: %d\n", ret);
		trace_9p_protocol_dump(clnt, &fake_pdu);
		return ret;
	}

	return fake_pdu.offset;
}
EXPORT_SYMBOL(p9stat_read);

int p9pdu_prepare(struct p9_fcall *pdu, int16_t tag, int8_t type)
{
	pdu->id = type;
	return p9pdu_writef(pdu, 0, "dbw", 0, type, tag);
}

int p9pdu_finalize(struct p9_client *clnt, struct p9_fcall *pdu)
{
	int size = pdu->size;
	int err;

	pdu->size = 0;
	err = p9pdu_writef(pdu, 0, "d", size);
	pdu->size = size;

	trace_9p_protocol_dump(clnt, pdu);
	p9_debug(P9_DEBUG_9P, ">>> size=%d type: %d tag: %d\n",
		 pdu->size, pdu->id, pdu->tag);

	return err;
}

void p9pdu_reset(struct p9_fcall *pdu)
{
	pdu->offset = 0;
	pdu->size = 0;
}

int p9dirent_read(struct p9_client *clnt, char *buf, int len,
		  struct p9_dirent *dirent)
{
	struct p9_fcall fake_pdu;
	int ret;
	char *nameptr;

	fake_pdu.size = len;
	fake_pdu.capacity = len;
	fake_pdu.sdata = buf;
	fake_pdu.offset = 0;

	ret = p9pdu_readf(&fake_pdu, clnt->proto_version, "Qqbs", &dirent->qid,
			  &dirent->d_off, &dirent->d_type, &nameptr);
	if (ret) {
		p9_debug(P9_DEBUG_9P, "<<< p9dirent_read failed: %d\n", ret);
		trace_9p_protocol_dump(clnt, &fake_pdu);
		return ret;
	}

	ret = strscpy(dirent->d_name, nameptr, sizeof(dirent->d_name));
	if (ret < 0) {
		p9_debug(P9_DEBUG_ERROR,
			 "On the wire dirent name too long: %s\n",
			 nameptr);
		kfree(nameptr);
		return ret;
	}
	kfree(nameptr);

	return fake_pdu.offset;
}
EXPORT_SYMBOL(p9dirent_read);
