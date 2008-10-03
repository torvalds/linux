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

/**
 * enum p9_debug_flags - bits for mount time debug parameter
 * @P9_DEBUG_ERROR: more verbose error messages including original error string
 * @P9_DEBUG_9P: 9P protocol tracing
 * @P9_DEBUG_VFS: VFS API tracing
 * @P9_DEBUG_CONV: protocol conversion tracing
 * @P9_DEBUG_MUX: trace management of concurrent transactions
 * @P9_DEBUG_TRANS: transport tracing
 * @P9_DEBUG_SLABS: memory management tracing
 * @P9_DEBUG_FCALL: verbose dump of protocol messages
 *
 * These flags are passed at mount time to turn on various levels of
 * verbosity and tracing which will be output to the system logs.
 */

enum p9_debug_flags {
	P9_DEBUG_ERROR = 	(1<<0),
	P9_DEBUG_9P = 		(1<<2),
	P9_DEBUG_VFS =		(1<<3),
	P9_DEBUG_CONV =		(1<<4),
	P9_DEBUG_MUX =		(1<<5),
	P9_DEBUG_TRANS =	(1<<6),
	P9_DEBUG_SLABS =      	(1<<7),
	P9_DEBUG_FCALL =	(1<<8),
};

extern unsigned int p9_debug_level;

#define P9_DPRINTK(level, format, arg...) \
do {  \
	if ((p9_debug_level & level) == level) \
		printk(KERN_NOTICE "-- %s (%d): " \
		format , __FUNCTION__, task_pid_nr(current) , ## arg); \
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
		format , __FUNCTION__, task_pid_nr(current), ## arg); \
} while (0)

/**
 * enum p9_msg_t - 9P message types
 * @P9_TVERSION: version handshake request
 * @P9_RVERSION: version handshake response
 * @P9_TAUTH: request to establish authentication channel
 * @P9_RAUTH: response with authentication information
 * @P9_TATTACH: establish user access to file service
 * @P9_RATTACH: response with top level handle to file hierarchy
 * @P9_TERROR: not used
 * @P9_RERROR: response for any failed request
 * @P9_TFLUSH: request to abort a previous request
 * @P9_RFLUSH: response when previous request has been cancelled
 * @P9_TWALK: descend a directory hierarchy
 * @P9_RWALK: response with new handle for position within hierarchy
 * @P9_TOPEN: prepare a handle for I/O on an existing file
 * @P9_ROPEN: response with file access information
 * @P9_TCREATE: prepare a handle for I/O on a new file
 * @P9_RCREATE: response with file access information
 * @P9_TREAD: request to transfer data from a file or directory
 * @P9_RREAD: response with data requested
 * @P9_TWRITE: reuqest to transfer data to a file
 * @P9_RWRITE: response with out much data was transfered to file
 * @P9_TCLUNK: forget about a handle to an entity within the file system
 * @P9_RCLUNK: response when server has forgotten about the handle
 * @P9_TREMOVE: request to remove an entity from the hierarchy
 * @P9_RREMOVE: response when server has removed the entity
 * @P9_TSTAT: request file entity attributes
 * @P9_RSTAT: response with file entity attributes
 * @P9_TWSTAT: request to update file entity attributes
 * @P9_RWSTAT: response when file entity attributes are updated
 *
 * There are 14 basic operations in 9P2000, paired as
 * requests and responses.  The one special case is ERROR
 * as there is no @P9_TERROR request for clients to transmit to
 * the server, but the server may respond to any other request
 * with an @P9_RERROR.
 *
 * See Also: http://plan9.bell-labs.com/sys/man/5/INDEX.html
 */

enum p9_msg_t {
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

/**
 * enum p9_open_mode_t - 9P open modes
 * @P9_OREAD: open file for reading only
 * @P9_OWRITE: open file for writing only
 * @P9_ORDWR: open file for reading or writing
 * @P9_OEXEC: open file for execution
 * @P9_OTRUNC: truncate file to zero-length before opening it
 * @P9_OREXEC: close the file when an exec(2) system call is made
 * @P9_ORCLOSE: remove the file when the file is closed
 * @P9_OAPPEND: open the file and seek to the end
 * @P9_OEXCL: only create a file, do not open it
 *
 * 9P open modes differ slightly from Posix standard modes.
 * In particular, there are extra modes which specify different
 * semantic behaviors than may be available on standard Posix
 * systems.  For example, @P9_OREXEC and @P9_ORCLOSE are modes that
 * most likely will not be issued from the Linux VFS client, but may
 * be supported by servers.
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/open
 */

enum p9_open_mode_t {
	P9_OREAD = 0x00,
	P9_OWRITE = 0x01,
	P9_ORDWR = 0x02,
	P9_OEXEC = 0x03,
	P9_OTRUNC = 0x10,
	P9_OREXEC = 0x20,
	P9_ORCLOSE = 0x40,
	P9_OAPPEND = 0x80,
	P9_OEXCL = 0x1000,
};

/**
 * enum p9_perm_t - 9P permissions
 * @P9_DMDIR: mode bite for directories
 * @P9_DMAPPEND: mode bit for is append-only
 * @P9_DMEXCL: mode bit for excluse use (only one open handle allowed)
 * @P9_DMMOUNT: mode bite for mount points
 * @P9_DMAUTH: mode bit for authentication file
 * @P9_DMTMP: mode bit for non-backed-up files
 * @P9_DMSYMLINK: mode bit for symbolic links (9P2000.u)
 * @P9_DMLINK: mode bit for hard-link (9P2000.u)
 * @P9_DMDEVICE: mode bit for device files (9P2000.u)
 * @P9_DMNAMEDPIPE: mode bit for named pipe (9P2000.u)
 * @P9_DMSOCKET: mode bit for socket (9P2000.u)
 * @P9_DMSETUID: mode bit for setuid (9P2000.u)
 * @P9_DMSETGID: mode bit for setgid (9P2000.u)
 * @P9_DMSETVTX: mode bit for sticky bit (9P2000.u)
 *
 * 9P permissions differ slightly from Posix standard modes.
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/stat
 */
enum p9_perm_t {
	P9_DMDIR = 0x80000000,
	P9_DMAPPEND = 0x40000000,
	P9_DMEXCL = 0x20000000,
	P9_DMMOUNT = 0x10000000,
	P9_DMAUTH = 0x08000000,
	P9_DMTMP = 0x04000000,
/* 9P2000.u extensions */
	P9_DMSYMLINK = 0x02000000,
	P9_DMLINK = 0x01000000,
	P9_DMDEVICE = 0x00800000,
	P9_DMNAMEDPIPE = 0x00200000,
	P9_DMSOCKET = 0x00100000,
	P9_DMSETUID = 0x00080000,
	P9_DMSETGID = 0x00040000,
	P9_DMSETVTX = 0x00010000,
};

/**
 * enum p9_qid_t - QID types
 * @P9_QTDIR: directory
 * @P9_QTAPPEND: append-only
 * @P9_QTEXCL: excluse use (only one open handle allowed)
 * @P9_QTMOUNT: mount points
 * @P9_QTAUTH: authentication file
 * @P9_QTTMP: non-backed-up files
 * @P9_QTSYMLINK: symbolic links (9P2000.u)
 * @P9_QTLINK: hard-link (9P2000.u)
 * @P9_QTFILE: normal files
 *
 * QID types are a subset of permissions - they are primarily
 * used to differentiate semantics for a file system entity via
 * a jump-table.  Their value is also the most signifigant 16 bits
 * of the permission_t
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/stat
 */
enum p9_qid_t {
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

/* 9P Magic Numbers */
#define P9_NOTAG	(u16)(~0)
#define P9_NOFID	(u32)(~0)
#define P9_MAXWELEM	16

/* ample room for Twrite/Rread header */
#define P9_IOHDRSZ	24

/**
 * struct p9_str - length prefixed string type
 * @len: length of the string
 * @str: the string
 *
 * The protocol uses length prefixed strings for all
 * string data, so we replicate that for our internal
 * string members.
 */

struct p9_str {
	u16 len;
	char *str;
};

/**
 * struct p9_qid - file system entity information
 * @type: 8-bit type &p9_qid_t
 * @version: 16-bit monotonically incrementing version number
 * @path: 64-bit per-server-unique ID for a file system element
 *
 * qids are identifiers used by 9P servers to track file system
 * entities.  The type is used to differentiate semantics for operations
 * on the entity (ie. read means something different on a directory than
 * on a file).  The path provides a server unique index for an entity
 * (roughly analogous to an inode number), while the version is updated
 * every time a file is modified and can be used to maintain cache
 * coherency between clients and serves.
 * Servers will often differentiate purely synthetic entities by setting
 * their version to 0, signaling that they should never be cached and
 * should be accessed synchronously.
 *
 * See Also://plan9.bell-labs.com/magic/man2html/2/stat
 */

struct p9_qid {
	u8 type;
	u32 version;
	u64 path;
};

/**
 * struct p9_stat - file system metadata information
 * @size: length prefix for this stat structure instance
 * @type: the type of the server (equivilent to a major number)
 * @dev: the sub-type of the server (equivilent to a minor number)
 * @qid: unique id from the server of type &p9_qid
 * @mode: Plan 9 format permissions of type &p9_perm_t
 * @atime: Last access/read time
 * @mtime: Last modify/write time
 * @length: file length
 * @name: last element of path (aka filename) in type &p9_str
 * @uid: owner name in type &p9_str
 * @gid: group owner in type &p9_str
 * @muid: last modifier in type &p9_str
 * @extension: area used to encode extended UNIX support in type &p9_str
 * @n_uid: numeric user id of owner (part of 9p2000.u extension)
 * @n_gid: numeric group id (part of 9p2000.u extension)
 * @n_muid: numeric user id of laster modifier (part of 9p2000.u extension)
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/stat
 */

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

/*
 * file metadata (stat) structure used to create Twstat message
 * The is identical to &p9_stat, but the strings don't point to
 * the same memory block and should be freed separately
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/stat
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

/**
 * struct p9_fcall - primary packet structure
 * @size: prefixed length of the structure
 * @id: protocol operating identifier of type &p9_msg_t
 * @tag: transaction id of the request
 * @sdata: payload
 * @params: per-operation parameters
 *
 * &p9_fcall represents the structure for all 9P RPC
 * transactions.  Requests are packaged into fcalls, and reponses
 * must be extracted from them.
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/fcall
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
int p9_trans_fd_init(void);
void p9_trans_fd_exit(void);
#endif /* NET_9P_H */
