/*
 * include/net/9p/9p.h
 *
 * 9P protocol definitions.
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
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

#ifndef NET_9P_H
#define NET_9P_H

#ifdef CONFIG_NET_9P_DEBUG

#define P9_DEBUG_ERROR		(1<<0)
#define P9_DEBUG_9P	        (1<<2)
#define P9_DEBUG_VFS	        (1<<3)
#define P9_DEBUG_CONV		(1<<4)
#define P9_DEBUG_MUX		(1<<5)
#define P9_DEBUG_TRANS		(1<<6)
#define P9_DEBUG_SLABS	      	(1<<7)
#define P9_DEBUG_FCALL		(1<<8)

extern unsigned int p9_debug_level;

#define P9_DPRINTK(level, format, arg...) \
do {  \
	if ((p9_debug_level & level) == level) \
		printk(KERN_NOTICE "-- %s (%d): " \
		format , __FUNCTION__, current->pid , ## arg); \
} while (0)

#define PRINT_FCALL_ERROR(s, fcall) P9_DPRINTK(P9_DEBUG_ERROR,   \
	"%s: %.*s\n", s, fcall?fcall->params.rerror.error.len:0, \
	fcall?fcall->params.rerror.error.str:"");

#else
#define P9_DPRINTK(level, format, arg...)  do { } while (0)
#define PRINT_FCALL_ERROR(s, fcall) do { } while (0)
#endif

#define P9_EPRINTK(level, format, arg...) \
do { \
	printk(level "9p: %s (%d): " \
		format , __FUNCTION__, current->pid , ## arg); \
} while (0)


/* Message Types */
enum {
	P9_TVERSION = 100,
	P9_RVERSION,
	P9_TAUTH = 102,
	P9_RAUTH,
	P9_TATTACH = 104,
	P9_RATTACH,
	P9_TERROR = 106,
	P9_RERROR,
	P9_TFLUSH = 108,
	P9_RFLUSH,
	P9_TWALK = 110,
	P9_RWALK,
	P9_TOPEN = 112,
	P9_ROPEN,
	P9_TCREATE = 114,
	P9_RCREATE,
	P9_TREAD = 116,
	P9_RREAD,
	P9_TWRITE = 118,
	P9_RWRITE,
	P9_TCLUNK = 120,
	P9_RCLUNK,
	P9_TREMOVE = 122,
	P9_RREMOVE,
	P9_TSTAT = 124,
	P9_RSTAT,
	P9_TWSTAT = 126,
	P9_RWSTAT,
};

/* open modes */
enum {
	P9_OREAD = 0x00,
	P9_OWRITE = 0x01,
	P9_ORDWR = 0x02,
	P9_OEXEC = 0x03,
	P9_OEXCL = 0x04,
	P9_OTRUNC = 0x10,
	P9_OREXEC = 0x20,
	P9_ORCLOSE = 0x40,
	P9_OAPPEND = 0x80,
};

/* permissions */
enum {
	P9_DMDIR = 0x80000000,
	P9_DMAPPEND = 0x40000000,
	P9_DMEXCL = 0x20000000,
	P9_DMMOUNT = 0x10000000,
	P9_DMAUTH = 0x08000000,
	P9_DMTMP = 0x04000000,
	P9_DMSYMLINK = 0x02000000,
	P9_DMLINK = 0x01000000,
	/* 9P2000.u extensions */
	P9_DMDEVICE = 0x00800000,
	P9_DMNAMEDPIPE = 0x00200000,
	P9_DMSOCKET = 0x00100000,
	P9_DMSETUID = 0x00080000,
	P9_DMSETGID = 0x00040000,
};

/* qid.types */
enum {
	P9_QTDIR = 0x80,
	P9_QTAPPEND = 0x40,
	P9_QTEXCL = 0x20,
	P9_QTMOUNT = 0x10,
	P9_QTAUTH = 0x08,
	P9_QTTMP = 0x04,
	P9_QTSYMLINK = 0x02,
	P9_QTLINK = 0x01,
	P9_QTFILE = 0x00,
};

#define P9_NOTAG	(u16)(~0)
#define P9_NOFID	(u32)(~0)
#define P9_MAXWELEM	16

/* ample room for Twrite/Rread header */
#define P9_IOHDRSZ	24

struct p9_str {
	u16 len;
	char *str;
};

/* qids are the unique ID for a file (like an inode */
struct p9_qid {
	u8 type;
	u32 version;
	u64 path;
};

/* Plan 9 file metadata (stat) structure */
struct p9_stat {
	u16 size;
	u16 type;
	u32 dev;
	struct p9_qid qid;
	u32 mode;
	u32 atime;
	u32 mtime;
	u64 length;
	struct p9_str name;
	struct p9_str uid;
	struct p9_str gid;
	struct p9_str muid;
	struct p9_str extension;	/* 9p2000.u extensions */
	u32 n_uid;			/* 9p2000.u extensions */
	u32 n_gid;			/* 9p2000.u extensions */
	u32 n_muid;			/* 9p2000.u extensions */
};

/* file metadata (stat) structure used to create Twstat message
   The is similar to p9_stat, but the strings don't point to
   the same memory block and should be freed separately
*/
struct p9_wstat {
	u16 size;
	u16 type;
	u32 dev;
	struct p9_qid qid;
	u32 mode;
	u32 atime;
	u32 mtime;
	u64 length;
	char *name;
	char *uid;
	char *gid;
	char *muid;
	char *extension;	/* 9p2000.u extensions */
	u32 n_uid;		/* 9p2000.u extensions */
	u32 n_gid;		/* 9p2000.u extensions */
	u32 n_muid;		/* 9p2000.u extensions */
};

/* Structures for Protocol Operations */
struct p9_tversion {
	u32 msize;
	struct p9_str version;
};

struct p9_rversion {
	u32 msize;
	struct p9_str version;
};

struct p9_tauth {
	u32 afid;
	struct p9_str uname;
	struct p9_str aname;
	u32 n_uname;		/* 9P2000.u extensions */
};

struct p9_rauth {
	struct p9_qid qid;
};

struct p9_rerror {
	struct p9_str error;
	u32 errno;		/* 9p2000.u extension */
};

struct p9_tflush {
	u16 oldtag;
};

struct p9_rflush {
};

struct p9_tattach {
	u32 fid;
	u32 afid;
	struct p9_str uname;
	struct p9_str aname;
	u32 n_uname;		/* 9P2000.u extensions */
};

struct p9_rattach {
	struct p9_qid qid;
};

struct p9_twalk {
	u32 fid;
	u32 newfid;
	u16 nwname;
	struct p9_str wnames[16];
};

struct p9_rwalk {
	u16 nwqid;
	struct p9_qid wqids[16];
};

struct p9_topen {
	u32 fid;
	u8 mode;
};

struct p9_ropen {
	struct p9_qid qid;
	u32 iounit;
};

struct p9_tcreate {
	u32 fid;
	struct p9_str name;
	u32 perm;
	u8 mode;
	struct p9_str extension;
};

struct p9_rcreate {
	struct p9_qid qid;
	u32 iounit;
};

struct p9_tread {
	u32 fid;
	u64 offset;
	u32 count;
};

struct p9_rread {
	u32 count;
	u8 *data;
};

struct p9_twrite {
	u32 fid;
	u64 offset;
	u32 count;
	u8 *data;
};

struct p9_rwrite {
	u32 count;
};

struct p9_tclunk {
	u32 fid;
};

struct p9_rclunk {
};

struct p9_tremove {
	u32 fid;
};

struct p9_rremove {
};

struct p9_tstat {
	u32 fid;
};

struct p9_rstat {
	struct p9_stat stat;
};

struct p9_twstat {
	u32 fid;
	struct p9_stat stat;
};

struct p9_rwstat {
};

/*
  * fcall is the primary packet structure
  *
  */

struct p9_fcall {
	u32 size;
	u8 id;
	u16 tag;
	void *sdata;

	union {
		struct p9_tversion tversion;
		struct p9_rversion rversion;
		struct p9_tauth tauth;
		struct p9_rauth rauth;
		struct p9_rerror rerror;
		struct p9_tflush tflush;
		struct p9_rflush rflush;
		struct p9_tattach tattach;
		struct p9_rattach rattach;
		struct p9_twalk twalk;
		struct p9_rwalk rwalk;
		struct p9_topen topen;
		struct p9_ropen ropen;
		struct p9_tcreate tcreate;
		struct p9_rcreate rcreate;
		struct p9_tread tread;
		struct p9_rread rread;
		struct p9_twrite twrite;
		struct p9_rwrite rwrite;
		struct p9_tclunk tclunk;
		struct p9_rclunk rclunk;
		struct p9_tremove tremove;
		struct p9_rremove rremove;
		struct p9_tstat tstat;
		struct p9_rstat rstat;
		struct p9_twstat twstat;
		struct p9_rwstat rwstat;
	} params;
};

struct p9_idpool;

int p9_deserialize_stat(void *buf, u32 buflen, struct p9_stat *stat,
	int dotu);
int p9_deserialize_fcall(void *buf, u32 buflen, struct p9_fcall *fc, int dotu);
void p9_set_tag(struct p9_fcall *fc, u16 tag);
struct p9_fcall *p9_create_tversion(u32 msize, char *version);
struct p9_fcall *p9_create_tattach(u32 fid, u32 afid, char *uname,
	char *aname, u32 n_uname, int dotu);
struct p9_fcall *p9_create_tauth(u32 afid, char *uname, char *aname,
	u32 n_uname, int dotu);
struct p9_fcall *p9_create_tflush(u16 oldtag);
struct p9_fcall *p9_create_twalk(u32 fid, u32 newfid, u16 nwname,
	char **wnames);
struct p9_fcall *p9_create_topen(u32 fid, u8 mode);
struct p9_fcall *p9_create_tcreate(u32 fid, char *name, u32 perm, u8 mode,
	char *extension, int dotu);
struct p9_fcall *p9_create_tread(u32 fid, u64 offset, u32 count);
struct p9_fcall *p9_create_twrite(u32 fid, u64 offset, u32 count,
	const char *data);
struct p9_fcall *p9_create_twrite_u(u32 fid, u64 offset, u32 count,
	const char __user *data);
struct p9_fcall *p9_create_tclunk(u32 fid);
struct p9_fcall *p9_create_tremove(u32 fid);
struct p9_fcall *p9_create_tstat(u32 fid);
struct p9_fcall *p9_create_twstat(u32 fid, struct p9_wstat *wstat,
	int dotu);

int p9_printfcall(char *buf, int buflen, struct p9_fcall *fc, int dotu);
int p9_errstr2errno(char *errstr, int len);

struct p9_idpool *p9_idpool_create(void);
void p9_idpool_destroy(struct p9_idpool *);
int p9_idpool_get(struct p9_idpool *p);
void p9_idpool_put(int id, struct p9_idpool *p);
int p9_idpool_check(int id, struct p9_idpool *p);

int p9_error_init(void);
int p9_errstr2errno(char *, int);
#endif /* NET_9P_H */
