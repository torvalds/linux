// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/fs/9p/error.c
 *
 * Error string handling
 *
 * Plan 9 uses error strings, Unix uses error numbers.  These functions
 * try to help manage that and provide for dynamically adding error
 * mappings.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/errno.h>
#include <net/9p/9p.h>

/**
 * struct errormap - map string errors from Plan 9 to Linux numeric ids
 * @name: string sent over 9P
 * @val: numeric id most closely representing @name
 * @namelen: length of string
 * @list: hash-table list for string lookup
 */
struct errormap {
	char *name;
	int val;

	int namelen;
	struct hlist_node list;
};

#define ERRHASHSZ		32
static struct hlist_head hash_errmap[ERRHASHSZ];

/* FixMe - reduce to a reasonable size */
static struct errormap errmap[] = {
	{"Operation not permitted", EPERM},
	{"wstat prohibited", EPERM},
	{"No such file or directory", ENOENT},
	{"directory entry not found", ENOENT},
	{"file not found", ENOENT},
	{"Interrupted system call", EINTR},
	{"Input/output error", EIO},
	{"No such device or address", ENXIO},
	{"Argument list too long", E2BIG},
	{"Bad file descriptor", EBADF},
	{"Resource temporarily unavailable", EAGAIN},
	{"Cannot allocate memory", ENOMEM},
	{"Permission denied", EACCES},
	{"Bad address", EFAULT},
	{"Block device required", ENOTBLK},
	{"Device or resource busy", EBUSY},
	{"File exists", EEXIST},
	{"Invalid cross-device link", EXDEV},
	{"No such device", ENODEV},
	{"Not a directory", ENOTDIR},
	{"Is a directory", EISDIR},
	{"Invalid argument", EINVAL},
	{"Too many open files in system", ENFILE},
	{"Too many open files", EMFILE},
	{"Text file busy", ETXTBSY},
	{"File too large", EFBIG},
	{"No space left on device", ENOSPC},
	{"Illegal seek", ESPIPE},
	{"Read-only file system", EROFS},
	{"Too many links", EMLINK},
	{"Broken pipe", EPIPE},
	{"Numerical argument out of domain", EDOM},
	{"Numerical result out of range", ERANGE},
	{"Resource deadlock avoided", EDEADLK},
	{"File name too long", ENAMETOOLONG},
	{"No locks available", ENOLCK},
	{"Function not implemented", ENOSYS},
	{"Directory not empty", ENOTEMPTY},
	{"Too many levels of symbolic links", ELOOP},
	{"No message of desired type", ENOMSG},
	{"Identifier removed", EIDRM},
	{"No data available", ENODATA},
	{"Machine is not on the network", ENONET},
	{"Package not installed", ENOPKG},
	{"Object is remote", EREMOTE},
	{"Link has been severed", ENOLINK},
	{"Communication error on send", ECOMM},
	{"Protocol error", EPROTO},
	{"Bad message", EBADMSG},
	{"File descriptor in bad state", EBADFD},
	{"Streams pipe error", ESTRPIPE},
	{"Too many users", EUSERS},
	{"Socket operation on non-socket", ENOTSOCK},
	{"Message too long", EMSGSIZE},
	{"Protocol not available", ENOPROTOOPT},
	{"Protocol not supported", EPROTONOSUPPORT},
	{"Socket type not supported", ESOCKTNOSUPPORT},
	{"Operation not supported", EOPNOTSUPP},
	{"Protocol family not supported", EPFNOSUPPORT},
	{"Network is down", ENETDOWN},
	{"Network is unreachable", ENETUNREACH},
	{"Network dropped connection on reset", ENETRESET},
	{"Software caused connection abort", ECONNABORTED},
	{"Connection reset by peer", ECONNRESET},
	{"No buffer space available", ENOBUFS},
	{"Transport endpoint is already connected", EISCONN},
	{"Transport endpoint is not connected", ENOTCONN},
	{"Cannot send after transport endpoint shutdown", ESHUTDOWN},
	{"Connection timed out", ETIMEDOUT},
	{"Connection refused", ECONNREFUSED},
	{"Host is down", EHOSTDOWN},
	{"No route to host", EHOSTUNREACH},
	{"Operation already in progress", EALREADY},
	{"Operation now in progress", EINPROGRESS},
	{"Is a named type file", EISNAM},
	{"Remote I/O error", EREMOTEIO},
	{"Disk quota exceeded", EDQUOT},
/* errors from fossil, vacfs, and u9fs */
	{"fid unknown or out of range", EBADF},
	{"permission denied", EACCES},
	{"file does not exist", ENOENT},
	{"authentication failed", ECONNREFUSED},
	{"bad offset in directory read", ESPIPE},
	{"bad use of fid", EBADF},
	{"wstat can't convert between files and directories", EPERM},
	{"directory is not empty", ENOTEMPTY},
	{"file exists", EEXIST},
	{"file already exists", EEXIST},
	{"file or directory already exists", EEXIST},
	{"fid already in use", EBADF},
	{"file in use", ETXTBSY},
	{"i/o error", EIO},
	{"file already open for I/O", ETXTBSY},
	{"illegal mode", EINVAL},
	{"illegal name", ENAMETOOLONG},
	{"not a directory", ENOTDIR},
	{"not a member of proposed group", EPERM},
	{"not owner", EACCES},
	{"only owner can change group in wstat", EACCES},
	{"read only file system", EROFS},
	{"no access to special file", EPERM},
	{"i/o count too large", EIO},
	{"unknown group", EINVAL},
	{"unknown user", EINVAL},
	{"bogus wstat buffer", EPROTO},
	{"exclusive use file already open", EAGAIN},
	{"corrupted directory entry", EIO},
	{"corrupted file entry", EIO},
	{"corrupted block label", EIO},
	{"corrupted meta data", EIO},
	{"illegal offset", EINVAL},
	{"illegal path element", ENOENT},
	{"root of file system is corrupted", EIO},
	{"corrupted super block", EIO},
	{"protocol botch", EPROTO},
	{"file system is full", ENOSPC},
	{"file is in use", EAGAIN},
	{"directory entry is not allocated", ENOENT},
	{"file is read only", EROFS},
	{"file has been removed", EIDRM},
	{"only support truncation to zero length", EPERM},
	{"cannot remove root", EPERM},
	{"file too big", EFBIG},
	{"venti i/o error", EIO},
	/* these are not errors */
	{"u9fs rhostsauth: no authentication required", 0},
	{"u9fs authnone: no authentication required", 0},
	{NULL, -1}
};

/**
 * p9_error_init - preload mappings into hash list
 *
 */

int p9_error_init(void)
{
	struct errormap *c;
	int bucket;

	/* initialize hash table */
	for (bucket = 0; bucket < ERRHASHSZ; bucket++)
		INIT_HLIST_HEAD(&hash_errmap[bucket]);

	/* load initial error map into hash table */
	for (c = errmap; c->name != NULL; c++) {
		c->namelen = strlen(c->name);
		bucket = jhash(c->name, c->namelen, 0) % ERRHASHSZ;
		INIT_HLIST_NODE(&c->list);
		hlist_add_head(&c->list, &hash_errmap[bucket]);
	}

	return 1;
}
EXPORT_SYMBOL(p9_error_init);

/**
 * errstr2errno - convert error string to error number
 * @errstr: error string
 * @len: length of error string
 *
 */

int p9_errstr2errno(char *errstr, int len)
{
	int errno;
	struct errormap *c;
	int bucket;

	errno = 0;
	c = NULL;
	bucket = jhash(errstr, len, 0) % ERRHASHSZ;
	hlist_for_each_entry(c, &hash_errmap[bucket], list) {
		if (c->namelen == len && !memcmp(c->name, errstr, len)) {
			errno = c->val;
			break;
		}
	}

	if (errno == 0) {
		/* TODO: if error isn't found, add it dynamically */
		errstr[len] = 0;
		pr_err("%s: server reported unknown error %s\n",
		       __func__, errstr);
		errno = ESERVERFAULT;
	}

	return -errno;
}
EXPORT_SYMBOL(p9_errstr2errno);
