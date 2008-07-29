/*
 *  net/9p/fcprint.c
 *
 *  Print 9P call.
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <linux/idr.h>
#include <net/9p/9p.h>

#ifdef CONFIG_NET_9P_DEBUG

static int
p9_printqid(char *buf, int buflen, struct p9_qid *q)
{
	int n;
	char b[10];

	n = 0;
	if (q->type & P9_QTDIR)
		b[n++] = 'd';
	if (q->type & P9_QTAPPEND)
		b[n++] = 'a';
	if (q->type & P9_QTAUTH)
		b[n++] = 'A';
	if (q->type & P9_QTEXCL)
		b[n++] = 'l';
	if (q->type & P9_QTTMP)
		b[n++] = 't';
	if (q->type & P9_QTSYMLINK)
		b[n++] = 'L';
	b[n] = '\0';

	return scnprintf(buf, buflen, "(%.16llx %x %s)",
					(long long int) q->path, q->version, b);
}

static int
p9_printperm(char *buf, int buflen, int perm)
{
	int n;
	char b[15];

	n = 0;
	if (perm & P9_DMDIR)
		b[n++] = 'd';
	if (perm & P9_DMAPPEND)
		b[n++] = 'a';
	if (perm & P9_DMAUTH)
		b[n++] = 'A';
	if (perm & P9_DMEXCL)
		b[n++] = 'l';
	if (perm & P9_DMTMP)
		b[n++] = 't';
	if (perm & P9_DMDEVICE)
		b[n++] = 'D';
	if (perm & P9_DMSOCKET)
		b[n++] = 'S';
	if (perm & P9_DMNAMEDPIPE)
		b[n++] = 'P';
	if (perm & P9_DMSYMLINK)
		b[n++] = 'L';
	b[n] = '\0';

	return scnprintf(buf, buflen, "%s%03o", b, perm&077);
}

static int
p9_printstat(char *buf, int buflen, struct p9_stat *st, int extended)
{
	int n;

	n = scnprintf(buf, buflen, "'%.*s' '%.*s'", st->name.len,
		st->name.str, st->uid.len, st->uid.str);
	if (extended)
		n += scnprintf(buf+n, buflen-n, "(%d)", st->n_uid);

	n += scnprintf(buf+n, buflen-n, " '%.*s'", st->gid.len, st->gid.str);
	if (extended)
		n += scnprintf(buf+n, buflen-n, "(%d)", st->n_gid);

	n += scnprintf(buf+n, buflen-n, " '%.*s'", st->muid.len, st->muid.str);
	if (extended)
		n += scnprintf(buf+n, buflen-n, "(%d)", st->n_muid);

	n += scnprintf(buf+n, buflen-n, " q ");
	n += p9_printqid(buf+n, buflen-n, &st->qid);
	n += scnprintf(buf+n, buflen-n, " m ");
	n += p9_printperm(buf+n, buflen-n, st->mode);
	n += scnprintf(buf+n, buflen-n, " at %d mt %d l %lld",
		st->atime, st->mtime, (long long int) st->length);

	if (extended)
		n += scnprintf(buf+n, buflen-n, " ext '%.*s'",
			st->extension.len, st->extension.str);

	return n;
}

static int
p9_dumpdata(char *buf, int buflen, u8 *data, int datalen)
{
	int i, n;

	i = n = 0;
	while (i < datalen) {
		n += scnprintf(buf + n, buflen - n, "%02x", data[i]);
		if (i%4 == 3)
			n += scnprintf(buf + n, buflen - n, " ");
		if (i%32 == 31)
			n += scnprintf(buf + n, buflen - n, "\n");

		i++;
	}
	n += scnprintf(buf + n, buflen - n, "\n");

	return n;
}

static int
p9_printdata(char *buf, int buflen, u8 *data, int datalen)
{
	return p9_dumpdata(buf, buflen, data, datalen < 16?datalen:16);
}

/**
 * p9_printfcall - decode and print a protocol structure into a buffer
 * @buf: buffer to deposit decoded structure into
 * @buflen: available space in buffer
 * @fc: protocol rpc structure of type &p9_fcall
 * @extended: whether or not session is operating with extended protocol
 */

int
p9_printfcall(char *buf, int buflen, struct p9_fcall *fc, int extended)
{
	int i, ret, type, tag;

	if (!fc)
		return scnprintf(buf, buflen, "<NULL>");

	type = fc->id;
	tag = fc->tag;

	ret = 0;
	switch (type) {
	case P9_TVERSION:
		ret += scnprintf(buf+ret, buflen-ret,
				"Tversion tag %u msize %u version '%.*s'", tag,
				fc->params.tversion.msize,
				fc->params.tversion.version.len,
				fc->params.tversion.version.str);
		break;

	case P9_RVERSION:
		ret += scnprintf(buf+ret, buflen-ret,
				"Rversion tag %u msize %u version '%.*s'", tag,
				fc->params.rversion.msize,
				fc->params.rversion.version.len,
				fc->params.rversion.version.str);
		break;

	case P9_TAUTH:
		ret += scnprintf(buf+ret, buflen-ret,
			"Tauth tag %u afid %d uname '%.*s' aname '%.*s'", tag,
			fc->params.tauth.afid, fc->params.tauth.uname.len,
			fc->params.tauth.uname.str, fc->params.tauth.aname.len,
			fc->params.tauth.aname.str);
		break;

	case P9_RAUTH:
		ret += scnprintf(buf+ret, buflen-ret, "Rauth tag %u qid ", tag);
		p9_printqid(buf+ret, buflen-ret, &fc->params.rauth.qid);
		break;

	case P9_TATTACH:
		ret += scnprintf(buf+ret, buflen-ret,
		 "Tattach tag %u fid %d afid %d uname '%.*s' aname '%.*s'", tag,
		 fc->params.tattach.fid, fc->params.tattach.afid,
		 fc->params.tattach.uname.len, fc->params.tattach.uname.str,
		 fc->params.tattach.aname.len, fc->params.tattach.aname.str);
		break;

	case P9_RATTACH:
		ret += scnprintf(buf+ret, buflen-ret, "Rattach tag %u qid ",
									tag);
		p9_printqid(buf+ret, buflen-ret, &fc->params.rattach.qid);
		break;

	case P9_RERROR:
		ret += scnprintf(buf+ret, buflen-ret,
				"Rerror tag %u ename '%.*s'", tag,
				fc->params.rerror.error.len,
				fc->params.rerror.error.str);
		if (extended)
			ret += scnprintf(buf+ret, buflen-ret, " ecode %d\n",
				fc->params.rerror.errno);
		break;

	case P9_TFLUSH:
		ret += scnprintf(buf+ret, buflen-ret, "Tflush tag %u oldtag %u",
			tag, fc->params.tflush.oldtag);
		break;

	case P9_RFLUSH:
		ret += scnprintf(buf+ret, buflen-ret, "Rflush tag %u", tag);
		break;

	case P9_TWALK:
		ret += scnprintf(buf+ret, buflen-ret,
			"Twalk tag %u fid %d newfid %d nwname %d", tag,
			fc->params.twalk.fid, fc->params.twalk.newfid,
			fc->params.twalk.nwname);
		for (i = 0; i < fc->params.twalk.nwname; i++)
			ret += scnprintf(buf+ret, buflen-ret, " '%.*s'",
				fc->params.twalk.wnames[i].len,
				fc->params.twalk.wnames[i].str);
		break;

	case P9_RWALK:
		ret += scnprintf(buf+ret, buflen-ret, "Rwalk tag %u nwqid %d",
			tag, fc->params.rwalk.nwqid);
		for (i = 0; i < fc->params.rwalk.nwqid; i++)
			ret += p9_printqid(buf+ret, buflen-ret,
				&fc->params.rwalk.wqids[i]);
		break;

	case P9_TOPEN:
		ret += scnprintf(buf+ret, buflen-ret,
			"Topen tag %u fid %d mode %d", tag,
			fc->params.topen.fid, fc->params.topen.mode);
		break;

	case P9_ROPEN:
		ret += scnprintf(buf+ret, buflen-ret, "Ropen tag %u", tag);
		ret += p9_printqid(buf+ret, buflen-ret, &fc->params.ropen.qid);
		ret += scnprintf(buf+ret, buflen-ret, " iounit %d",
			fc->params.ropen.iounit);
		break;

	case P9_TCREATE:
		ret += scnprintf(buf+ret, buflen-ret,
			"Tcreate tag %u fid %d name '%.*s' perm ", tag,
			fc->params.tcreate.fid, fc->params.tcreate.name.len,
			fc->params.tcreate.name.str);

		ret += p9_printperm(buf+ret, buflen-ret,
						fc->params.tcreate.perm);
		ret += scnprintf(buf+ret, buflen-ret, " mode %d",
			fc->params.tcreate.mode);
		break;

	case P9_RCREATE:
		ret += scnprintf(buf+ret, buflen-ret, "Rcreate tag %u", tag);
		ret += p9_printqid(buf+ret, buflen-ret,
						&fc->params.rcreate.qid);
		ret += scnprintf(buf+ret, buflen-ret, " iounit %d",
			fc->params.rcreate.iounit);
		break;

	case P9_TREAD:
		ret += scnprintf(buf+ret, buflen-ret,
			"Tread tag %u fid %d offset %lld count %u", tag,
			fc->params.tread.fid,
			(long long int) fc->params.tread.offset,
			fc->params.tread.count);
		break;

	case P9_RREAD:
		ret += scnprintf(buf+ret, buflen-ret,
			"Rread tag %u count %u data ", tag,
			fc->params.rread.count);
		ret += p9_printdata(buf+ret, buflen-ret, fc->params.rread.data,
			fc->params.rread.count);
		break;

	case P9_TWRITE:
		ret += scnprintf(buf+ret, buflen-ret,
			"Twrite tag %u fid %d offset %lld count %u data ",
			tag, fc->params.twrite.fid,
			(long long int) fc->params.twrite.offset,
			fc->params.twrite.count);
		ret += p9_printdata(buf+ret, buflen-ret, fc->params.twrite.data,
			fc->params.twrite.count);
		break;

	case P9_RWRITE:
		ret += scnprintf(buf+ret, buflen-ret, "Rwrite tag %u count %u",
			tag, fc->params.rwrite.count);
		break;

	case P9_TCLUNK:
		ret += scnprintf(buf+ret, buflen-ret, "Tclunk tag %u fid %d",
			tag, fc->params.tclunk.fid);
		break;

	case P9_RCLUNK:
		ret += scnprintf(buf+ret, buflen-ret, "Rclunk tag %u", tag);
		break;

	case P9_TREMOVE:
		ret += scnprintf(buf+ret, buflen-ret, "Tremove tag %u fid %d",
			tag, fc->params.tremove.fid);
		break;

	case P9_RREMOVE:
		ret += scnprintf(buf+ret, buflen-ret, "Rremove tag %u", tag);
		break;

	case P9_TSTAT:
		ret += scnprintf(buf+ret, buflen-ret, "Tstat tag %u fid %d",
			tag, fc->params.tstat.fid);
		break;

	case P9_RSTAT:
		ret += scnprintf(buf+ret, buflen-ret, "Rstat tag %u ", tag);
		ret += p9_printstat(buf+ret, buflen-ret, &fc->params.rstat.stat,
			extended);
		break;

	case P9_TWSTAT:
		ret += scnprintf(buf+ret, buflen-ret, "Twstat tag %u fid %d ",
			tag, fc->params.twstat.fid);
		ret += p9_printstat(buf+ret, buflen-ret,
					&fc->params.twstat.stat, extended);
		break;

	case P9_RWSTAT:
		ret += scnprintf(buf+ret, buflen-ret, "Rwstat tag %u", tag);
		break;

	default:
		ret += scnprintf(buf+ret, buflen-ret, "unknown type %d", type);
		break;
	}

	return ret;
}
#else
int
p9_printfcall(char *buf, int buflen, struct p9_fcall *fc, int extended)
{
	return 0;
}
#endif /* CONFIG_NET_9P_DEBUG */
EXPORT_SYMBOL(p9_printfcall);

