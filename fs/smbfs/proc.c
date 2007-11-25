/*
 *  proc.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 *  Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/types.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/dcache.h>
#include <linux/dirent.h>
#include <linux/nls.h>
#include <linux/smp_lock.h>
#include <linux/net.h>
#include <linux/vfs.h>
#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/smb_mount.h>

#include <net/sock.h>

#include <asm/string.h>
#include <asm/div64.h>

#include "smb_debug.h"
#include "proto.h"
#include "request.h"


/* Features. Undefine if they cause problems, this should perhaps be a
   config option. */
#define SMBFS_POSIX_UNLINK 1

/* Allow smb_retry to be interrupted. */
#define SMB_RETRY_INTR

#define SMB_VWV(packet)  ((packet) + SMB_HEADER_LEN)
#define SMB_CMD(packet)  (*(packet+8))
#define SMB_WCT(packet)  (*(packet+SMB_HEADER_LEN - 1))

#define SMB_DIRINFO_SIZE 43
#define SMB_STATUS_SIZE  21

#define SMB_ST_BLKSIZE	(PAGE_SIZE)
#define SMB_ST_BLKSHIFT	(PAGE_SHIFT)

static struct smb_ops smb_ops_core;
static struct smb_ops smb_ops_os2;
static struct smb_ops smb_ops_win95;
static struct smb_ops smb_ops_winNT;
static struct smb_ops smb_ops_unix;
static struct smb_ops smb_ops_null;

static void
smb_init_dirent(struct smb_sb_info *server, struct smb_fattr *fattr);
static void
smb_finish_dirent(struct smb_sb_info *server, struct smb_fattr *fattr);
static int
smb_proc_getattr_core(struct smb_sb_info *server, struct dentry *dir,
		      struct smb_fattr *fattr);
static int
smb_proc_getattr_ff(struct smb_sb_info *server, struct dentry *dentry,
		    struct smb_fattr *fattr);
static int
smb_proc_setattr_core(struct smb_sb_info *server, struct dentry *dentry,
		      u16 attr);
static int
smb_proc_setattr_ext(struct smb_sb_info *server,
		     struct inode *inode, struct smb_fattr *fattr);
static int
smb_proc_query_cifsunix(struct smb_sb_info *server);
static void
install_ops(struct smb_ops *dst, struct smb_ops *src);


static void
str_upper(char *name, int len)
{
	while (len--)
	{
		if (*name >= 'a' && *name <= 'z')
			*name -= ('a' - 'A');
		name++;
	}
}

#if 0
static void
str_lower(char *name, int len)
{
	while (len--)
	{
		if (*name >= 'A' && *name <= 'Z')
			*name += ('a' - 'A');
		name++;
	}
}
#endif

/* reverse a string inline. This is used by the dircache walking routines */
static void reverse_string(char *buf, int len)
{
	char c;
	char *end = buf+len-1;

	while(buf < end) {
		c = *buf;
		*(buf++) = *end;
		*(end--) = c;
	}
}

/* no conversion, just a wrapper for memcpy. */
static int convert_memcpy(unsigned char *output, int olen,
			  const unsigned char *input, int ilen,
			  struct nls_table *nls_from,
			  struct nls_table *nls_to)
{
	if (olen < ilen)
		return -ENAMETOOLONG;
	memcpy(output, input, ilen);
	return ilen;
}

static inline int write_char(unsigned char ch, char *output, int olen)
{
	if (olen < 4)
		return -ENAMETOOLONG;
	sprintf(output, ":x%02x", ch);
	return 4;
}

static inline int write_unichar(wchar_t ch, char *output, int olen)
{
	if (olen < 5)
		return -ENAMETOOLONG;
	sprintf(output, ":%04x", ch);
	return 5;
}

/* convert from one "codepage" to another (possibly being utf8). */
static int convert_cp(unsigned char *output, int olen,
		      const unsigned char *input, int ilen,
		      struct nls_table *nls_from,
		      struct nls_table *nls_to)
{
	int len = 0;
	int n;
	wchar_t ch;

	while (ilen > 0) {
		/* convert by changing to unicode and back to the new cp */
		n = nls_from->char2uni(input, ilen, &ch);
		if (n == -EINVAL) {
			ilen--;
			n = write_char(*input++, output, olen);
			if (n < 0)
				goto fail;
			output += n;
			olen -= n;
			len += n;
			continue;
		} else if (n < 0)
			goto fail;
		input += n;
		ilen -= n;

		n = nls_to->uni2char(ch, output, olen);
		if (n == -EINVAL)
			n = write_unichar(ch, output, olen);
		if (n < 0)
			goto fail;
		output += n;
		olen -= n;

		len += n;
	}
	return len;
fail:
	return n;
}

/* ----------------------------------------------------------- */

/*
 * nls_unicode
 *
 * This encodes/decodes little endian unicode format
 */

static int uni2char(wchar_t uni, unsigned char *out, int boundlen)
{
	if (boundlen < 2)
		return -EINVAL;
	*out++ = uni & 0xff;
	*out++ = uni >> 8;
	return 2;
}

static int char2uni(const unsigned char *rawstring, int boundlen, wchar_t *uni)
{
	if (boundlen < 2)
		return -EINVAL;
	*uni = (rawstring[1] << 8) | rawstring[0];
	return 2;
}

static struct nls_table unicode_table = {
	.charset	= "unicode",
	.uni2char	= uni2char,
	.char2uni	= char2uni,
};

/* ----------------------------------------------------------- */

static int setcodepage(struct nls_table **p, char *name)
{
	struct nls_table *nls;

	if (!name || !*name) {
		nls = NULL;
	} else if ( (nls = load_nls(name)) == NULL) {
		printk (KERN_ERR "smbfs: failed to load nls '%s'\n", name);
		return -EINVAL;
	}

	/* if already set, unload the previous one. */
	if (*p && *p != &unicode_table)
		unload_nls(*p);
	*p = nls;

	return 0;
}

/* Handles all changes to codepage settings. */
int smb_setcodepage(struct smb_sb_info *server, struct smb_nls_codepage *cp)
{
	int n = 0;

	smb_lock_server(server);

	/* Don't load any nls_* at all, if no remote is requested */
	if (!*cp->remote_name)
		goto out;

	/* local */
	n = setcodepage(&server->local_nls, cp->local_name);
	if (n != 0)
		goto out;

	/* remote */
	if (!strcmp(cp->remote_name, "unicode")) {
		server->remote_nls = &unicode_table;
	} else {
		n = setcodepage(&server->remote_nls, cp->remote_name);
		if (n != 0)
			setcodepage(&server->local_nls, NULL);
	}

out:
	if (server->local_nls != NULL && server->remote_nls != NULL)
		server->ops->convert = convert_cp;
	else
		server->ops->convert = convert_memcpy;

	smb_unlock_server(server);
	return n;
}


/*****************************************************************************/
/*                                                                           */
/*  Encoding/Decoding section                                                */
/*                                                                           */
/*****************************************************************************/

static __u8 *
smb_encode_smb_length(__u8 * p, __u32 len)
{
	*p = 0;
	*(p+1) = 0;
	*(p+2) = (len & 0xFF00) >> 8;
	*(p+3) = (len & 0xFF);
	if (len > 0xFFFF)
	{
		*(p+1) = 1;
	}
	return p + 4;
}

/*
 * smb_build_path: build the path to entry and name storing it in buf.
 * The path returned will have the trailing '\0'.
 */
static int smb_build_path(struct smb_sb_info *server, unsigned char *buf,
			  int maxlen,
			  struct dentry *entry, struct qstr *name)
{
	unsigned char *path = buf;
	int len;
	int unicode = (server->mnt->flags & SMB_MOUNT_UNICODE) != 0;

	if (maxlen < (2<<unicode))
		return -ENAMETOOLONG;

	if (maxlen > SMB_MAXPATHLEN + 1)
		maxlen = SMB_MAXPATHLEN + 1;

	if (entry == NULL)
		goto test_name_and_out;

	/*
	 * If IS_ROOT, we have to do no walking at all.
	 */
	if (IS_ROOT(entry) && !name) {
		*path++ = '\\';
		if (unicode) *path++ = '\0';
		*path++ = '\0';
		if (unicode) *path++ = '\0';
		return path-buf;
	}

	/*
	 * Build the path string walking the tree backward from end to ROOT
	 * and store it in reversed order [see reverse_string()]
	 */
	dget(entry);
	spin_lock(&entry->d_lock);
	while (!IS_ROOT(entry)) {
		struct dentry *parent;

		if (maxlen < (3<<unicode)) {
			spin_unlock(&entry->d_lock);
			dput(entry);
			return -ENAMETOOLONG;
		}

		len = server->ops->convert(path, maxlen-2, 
				      entry->d_name.name, entry->d_name.len,
				      server->local_nls, server->remote_nls);
		if (len < 0) {
			spin_unlock(&entry->d_lock);
			dput(entry);
			return len;
		}
		reverse_string(path, len);
		path += len;
		if (unicode) {
			/* Note: reverse order */
			*path++ = '\0';
			maxlen--;
		}
		*path++ = '\\';
		maxlen -= len+1;

		parent = entry->d_parent;
		dget(parent);
		spin_unlock(&entry->d_lock);
		dput(entry);
		entry = parent;
		spin_lock(&entry->d_lock);
	}
	spin_unlock(&entry->d_lock);
	dput(entry);
	reverse_string(buf, path-buf);

	/* maxlen has space for at least one char */
test_name_and_out:
	if (name) {
		if (maxlen < (3<<unicode))
			return -ENAMETOOLONG;
		*path++ = '\\';
		if (unicode) {
			*path++ = '\0';
			maxlen--;
		}
		len = server->ops->convert(path, maxlen-2, 
				      name->name, name->len,
				      server->local_nls, server->remote_nls);
		if (len < 0)
			return len;
		path += len;
		maxlen -= len+1;
	}
	/* maxlen has space for at least one char */
	*path++ = '\0';
	if (unicode) *path++ = '\0';
	return path-buf;
}

static int smb_encode_path(struct smb_sb_info *server, char *buf, int maxlen,
			   struct dentry *dir, struct qstr *name)
{
	int result;

	result = smb_build_path(server, buf, maxlen, dir, name);
	if (result < 0)
		goto out;
	if (server->opt.protocol <= SMB_PROTOCOL_COREPLUS)
		str_upper(buf, result);
out:
	return result;
}

/* encode_path for non-trans2 request SMBs */
static int smb_simple_encode_path(struct smb_request *req, char **p,
				  struct dentry * entry, struct qstr * name)
{
	struct smb_sb_info *server = req->rq_server;
	char *s = *p;
	int res;
	int maxlen = ((char *)req->rq_buffer + req->rq_bufsize) - s;
	int unicode = (server->mnt->flags & SMB_MOUNT_UNICODE);

	if (!maxlen)
		return -ENAMETOOLONG;
	*s++ = 4;	/* ASCII data format */

	/*
	 * SMB Unicode strings must be 16bit aligned relative the start of the
	 * packet. If they are not they must be padded with 0.
	 */
	if (unicode) {
		int align = s - (char *)req->rq_buffer;
		if (!(align & 1)) {
			*s++ = '\0';
			maxlen--;
		}
	}

	res = smb_encode_path(server, s, maxlen-1, entry, name);
	if (res < 0)
		return res;
	*p = s + res;
	return 0;
}

/* The following are taken directly from msdos-fs */

/* Linear day numbers of the respective 1sts in non-leap years. */

static int day_n[] =
{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0, 0};
		  /* JanFebMarApr May Jun Jul Aug Sep Oct Nov Dec */


static time_t
utc2local(struct smb_sb_info *server, time_t time)
{
	return time - server->opt.serverzone*60;
}

static time_t
local2utc(struct smb_sb_info *server, time_t time)
{
	return time + server->opt.serverzone*60;
}

/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */

static time_t
date_dos2unix(struct smb_sb_info *server, __u16 date, __u16 time)
{
	int month, year;
	time_t secs;

	/* first subtract and mask after that... Otherwise, if
	   date == 0, bad things happen */
	month = ((date >> 5) - 1) & 15;
	year = date >> 9;
	secs = (time & 31) * 2 + 60 * ((time >> 5) & 63) + (time >> 11) * 3600 + 86400 *
	    ((date & 31) - 1 + day_n[month] + (year / 4) + year * 365 - ((year & 3) == 0 &&
						   month < 2 ? 1 : 0) + 3653);
	/* days since 1.1.70 plus 80's leap day */
	return local2utc(server, secs);
}


/* Convert linear UNIX date to a MS-DOS time/date pair. */

static void
date_unix2dos(struct smb_sb_info *server,
	      int unix_date, __u16 *date, __u16 *time)
{
	int day, year, nl_day, month;

	unix_date = utc2local(server, unix_date);
	if (unix_date < 315532800)
		unix_date = 315532800;

	*time = (unix_date % 60) / 2 +
		(((unix_date / 60) % 60) << 5) +
		(((unix_date / 3600) % 24) << 11);

	day = unix_date / 86400 - 3652;
	year = day / 365;
	if ((year + 3) / 4 + 365 * year > day)
		year--;
	day -= (year + 3) / 4 + 365 * year;
	if (day == 59 && !(year & 3)) {
		nl_day = day;
		month = 2;
	} else {
		nl_day = (year & 3) || day <= 59 ? day : day - 1;
		for (month = 0; month < 12; month++)
			if (day_n[month] > nl_day)
				break;
	}
	*date = nl_day - day_n[month - 1] + 1 + (month << 5) + (year << 9);
}

/* The following are taken from fs/ntfs/util.c */

#define NTFS_TIME_OFFSET ((u64)(369*365 + 89) * 24 * 3600 * 10000000)

/*
 * Convert the NT UTC (based 1601-01-01, in hundred nanosecond units)
 * into Unix UTC (based 1970-01-01, in seconds).
 */
static struct timespec
smb_ntutc2unixutc(u64 ntutc)
{
	struct timespec ts;
	/* FIXME: what about the timezone difference? */
	/* Subtract the NTFS time offset, then convert to 1s intervals. */
	u64 t = ntutc - NTFS_TIME_OFFSET;
	ts.tv_nsec = do_div(t, 10000000) * 100;
	ts.tv_sec = t; 
	return ts;
}

/* Convert the Unix UTC into NT time */
static u64
smb_unixutc2ntutc(struct timespec ts)
{
	/* Note: timezone conversion is probably wrong. */
	/* return ((u64)utc2local(server, t)) * 10000000 + NTFS_TIME_OFFSET; */
	return ((u64)ts.tv_sec) * 10000000 + ts.tv_nsec/100 + NTFS_TIME_OFFSET;
}

#define MAX_FILE_MODE	6
static mode_t file_mode[] = {
	S_IFREG, S_IFDIR, S_IFLNK, S_IFCHR, S_IFBLK, S_IFIFO, S_IFSOCK
};

static int smb_filetype_to_mode(u32 filetype)
{
	if (filetype > MAX_FILE_MODE) {
		PARANOIA("Filetype out of range: %d\n", filetype);
		return S_IFREG;
	}
	return file_mode[filetype];
}

static u32 smb_filetype_from_mode(int mode)
{
	if (S_ISREG(mode))
		return UNIX_TYPE_FILE;
	if (S_ISDIR(mode))
		return UNIX_TYPE_DIR;
	if (S_ISLNK(mode))
		return UNIX_TYPE_SYMLINK;
	if (S_ISCHR(mode))
		return UNIX_TYPE_CHARDEV;
	if (S_ISBLK(mode))
		return UNIX_TYPE_BLKDEV;
	if (S_ISFIFO(mode))
		return UNIX_TYPE_FIFO;
	if (S_ISSOCK(mode))
		return UNIX_TYPE_SOCKET;
	return UNIX_TYPE_UNKNOWN;
}


/*****************************************************************************/
/*                                                                           */
/*  Support section.                                                         */
/*                                                                           */
/*****************************************************************************/

__u32
smb_len(__u8 * p)
{
	return ((*(p+1) & 0x1) << 16L) | (*(p+2) << 8L) | *(p+3);
}

static __u16
smb_bcc(__u8 * packet)
{
	int pos = SMB_HEADER_LEN + SMB_WCT(packet) * sizeof(__u16);
	return WVAL(packet, pos);
}

/* smb_valid_packet: We check if packet fulfills the basic
   requirements of a smb packet */

static int
smb_valid_packet(__u8 * packet)
{
	return (packet[4] == 0xff
		&& packet[5] == 'S'
		&& packet[6] == 'M'
		&& packet[7] == 'B'
		&& (smb_len(packet) + 4 == SMB_HEADER_LEN
		    + SMB_WCT(packet) * 2 + smb_bcc(packet)));
}

/* smb_verify: We check if we got the answer we expected, and if we
   got enough data. If bcc == -1, we don't care. */

static int
smb_verify(__u8 * packet, int command, int wct, int bcc)
{
	if (SMB_CMD(packet) != command)
		goto bad_command;
	if (SMB_WCT(packet) < wct)
		goto bad_wct;
	if (bcc != -1 && smb_bcc(packet) < bcc)
		goto bad_bcc;
	return 0;

bad_command:
	printk(KERN_ERR "smb_verify: command=%x, SMB_CMD=%x??\n",
	       command, SMB_CMD(packet));
	goto fail;
bad_wct:
	printk(KERN_ERR "smb_verify: command=%x, wct=%d, SMB_WCT=%d??\n",
	       command, wct, SMB_WCT(packet));
	goto fail;
bad_bcc:
	printk(KERN_ERR "smb_verify: command=%x, bcc=%d, SMB_BCC=%d??\n",
	       command, bcc, smb_bcc(packet));
fail:
	return -EIO;
}

/*
 * Returns the maximum read or write size for the "payload". Making all of the
 * packet fit within the negotiated max_xmit size.
 *
 * N.B. Since this value is usually computed before locking the server,
 * the server's packet size must never be decreased!
 */
static inline int
smb_get_xmitsize(struct smb_sb_info *server, int overhead)
{
	return server->opt.max_xmit - overhead;
}

/*
 * Calculate the maximum read size
 */
int
smb_get_rsize(struct smb_sb_info *server)
{
	/* readX has 12 parameters, read has 5 */
	int overhead = SMB_HEADER_LEN + 12 * sizeof(__u16) + 2 + 1 + 2;
	int size = smb_get_xmitsize(server, overhead);

	VERBOSE("xmit=%d, size=%d\n", server->opt.max_xmit, size);

	return size;
}

/*
 * Calculate the maximum write size
 */
int
smb_get_wsize(struct smb_sb_info *server)
{
	/* writeX has 14 parameters, write has 5 */
	int overhead = SMB_HEADER_LEN + 14 * sizeof(__u16) + 2 + 1 + 2;
	int size = smb_get_xmitsize(server, overhead);

	VERBOSE("xmit=%d, size=%d\n", server->opt.max_xmit, size);

	return size;
}

/*
 * Convert SMB error codes to -E... errno values.
 */
int
smb_errno(struct smb_request *req)
{
	int errcls = req->rq_rcls;
	int error  = req->rq_err;
	char *class = "Unknown";

	VERBOSE("errcls %d  code %d  from command 0x%x\n",
		errcls, error, SMB_CMD(req->rq_header));

	if (errcls == ERRDOS) {
		switch (error) {
		case ERRbadfunc:
			return -EINVAL;
		case ERRbadfile:
		case ERRbadpath:
			return -ENOENT;
		case ERRnofids:
			return -EMFILE;
		case ERRnoaccess:
			return -EACCES;
		case ERRbadfid:
			return -EBADF;
		case ERRbadmcb:
			return -EREMOTEIO;
		case ERRnomem:
			return -ENOMEM;
		case ERRbadmem:
			return -EFAULT;
		case ERRbadenv:
		case ERRbadformat:
			return -EREMOTEIO;
		case ERRbadaccess:
			return -EACCES;
		case ERRbaddata:
			return -E2BIG;
		case ERRbaddrive:
			return -ENXIO;
		case ERRremcd:
			return -EREMOTEIO;
		case ERRdiffdevice:
			return -EXDEV;
		case ERRnofiles:
			return -ENOENT;
		case ERRbadshare:
			return -ETXTBSY;
		case ERRlock:
			return -EDEADLK;
		case ERRfilexists:
			return -EEXIST;
		case ERROR_INVALID_PARAMETER:
			return -EINVAL;
		case ERROR_DISK_FULL:
			return -ENOSPC;
		case ERROR_INVALID_NAME:
			return -ENOENT;
		case ERROR_DIR_NOT_EMPTY:
			return -ENOTEMPTY;
		case ERROR_NOT_LOCKED:
                       return -ENOLCK;
		case ERROR_ALREADY_EXISTS:
			return -EEXIST;
		default:
			class = "ERRDOS";
			goto err_unknown;
		}
	} else if (errcls == ERRSRV) {
		switch (error) {
		/* N.B. This is wrong ... EIO ? */
		case ERRerror:
			return -ENFILE;
		case ERRbadpw:
			return -EINVAL;
		case ERRbadtype:
		case ERRtimeout:
			return -EIO;
		case ERRaccess:
			return -EACCES;
		/*
		 * This is a fatal error, as it means the "tree ID"
		 * for this connection is no longer valid. We map
		 * to a special error code and get a new connection.
		 */
		case ERRinvnid:
			return -EBADSLT;
		default:
			class = "ERRSRV";
			goto err_unknown;
		}
	} else if (errcls == ERRHRD) {
		switch (error) {
		case ERRnowrite:
			return -EROFS;
		case ERRbadunit:
			return -ENODEV;
		case ERRnotready:
			return -EUCLEAN;
		case ERRbadcmd:
		case ERRdata:
			return -EIO;
		case ERRbadreq:
			return -ERANGE;
		case ERRbadshare:
			return -ETXTBSY;
		case ERRlock:
			return -EDEADLK;
		case ERRdiskfull:
			return -ENOSPC;
		default:
			class = "ERRHRD";
			goto err_unknown;
		}
	} else if (errcls == ERRCMD) {
		class = "ERRCMD";
	} else if (errcls == SUCCESS) {
		return 0;	/* This is the only valid 0 return */
	}

err_unknown:
	printk(KERN_ERR "smb_errno: class %s, code %d from command 0x%x\n",
	       class, error, SMB_CMD(req->rq_header));
	return -EIO;
}

/* smb_request_ok: We expect the server to be locked. Then we do the
   request and check the answer completely. When smb_request_ok
   returns 0, you can be quite sure that everything went well. When
   the answer is <=0, the returned number is a valid unix errno. */

static int
smb_request_ok(struct smb_request *req, int command, int wct, int bcc)
{
	int result;

	req->rq_resp_wct = wct;
	req->rq_resp_bcc = bcc;

	result = smb_add_request(req);
	if (result != 0) {
		DEBUG1("smb_request failed\n");
		goto out;
	}

	if (smb_valid_packet(req->rq_header) != 0) {
		PARANOIA("invalid packet!\n");
		goto out;
	}

	result = smb_verify(req->rq_header, command, wct, bcc);

out:
	return result;
}

/*
 * This implements the NEWCONN ioctl. It installs the server pid,
 * sets server->state to CONN_VALID, and wakes up the waiting process.
 */
int
smb_newconn(struct smb_sb_info *server, struct smb_conn_opt *opt)
{
	struct file *filp;
	struct sock *sk;
	int error;

	VERBOSE("fd=%d, pid=%d\n", opt->fd, current->pid);

	smb_lock_server(server);

	/*
	 * Make sure we don't already have a valid connection ...
	 */
	error = -EINVAL;
	if (server->state == CONN_VALID)
		goto out;

	error = -EACCES;
	if (current->uid != server->mnt->mounted_uid && 
	    !capable(CAP_SYS_ADMIN))
		goto out;

	error = -EBADF;
	filp = fget(opt->fd);
	if (!filp)
		goto out;
	if (!smb_valid_socket(filp->f_path.dentry->d_inode))
		goto out_putf;

	server->sock_file = filp;
	server->conn_pid = get_pid(task_pid(current));
	server->opt = *opt;
	server->generation += 1;
	server->state = CONN_VALID;
	error = 0;

	if (server->conn_error) {
		/*
		 * conn_error is the returncode we originally decided to
		 * drop the old connection on. This message should be positive
		 * and not make people ask questions on why smbfs is printing
		 * error messages ...
		 */
		printk(KERN_INFO "SMB connection re-established (%d)\n",
		       server->conn_error);
		server->conn_error = 0;
	}

	/*
	 * Store the server in sock user_data (Only used by sunrpc)
	 */
	sk = SOCKET_I(filp->f_path.dentry->d_inode)->sk;
	sk->sk_user_data = server;

	/* chain into the data_ready callback */
	server->data_ready = xchg(&sk->sk_data_ready, smb_data_ready);

	/* check if we have an old smbmount that uses seconds for the 
	   serverzone */
	if (server->opt.serverzone > 12*60 || server->opt.serverzone < -12*60)
		server->opt.serverzone /= 60;

	/* now that we have an established connection we can detect the server
	   type and enable bug workarounds */
	if (server->opt.protocol < SMB_PROTOCOL_LANMAN2)
		install_ops(server->ops, &smb_ops_core);
	else if (server->opt.protocol == SMB_PROTOCOL_LANMAN2)
		install_ops(server->ops, &smb_ops_os2);
	else if (server->opt.protocol == SMB_PROTOCOL_NT1 &&
		 (server->opt.max_xmit < 0x1000) &&
		 !(server->opt.capabilities & SMB_CAP_NT_SMBS)) {
		/* FIXME: can we kill the WIN95 flag now? */
		server->mnt->flags |= SMB_MOUNT_WIN95;
		VERBOSE("detected WIN95 server\n");
		install_ops(server->ops, &smb_ops_win95);
	} else {
		/*
		 * Samba has max_xmit 65535
		 * NT4spX has max_xmit 4536 (or something like that)
		 * win2k has ...
		 */
		VERBOSE("detected NT1 (Samba, NT4/5) server\n");
		install_ops(server->ops, &smb_ops_winNT);
	}

	/* FIXME: the win9x code wants to modify these ... (seek/trunc bug) */
	if (server->mnt->flags & SMB_MOUNT_OLDATTR) {
		server->ops->getattr = smb_proc_getattr_core;
	} else if (server->mnt->flags & SMB_MOUNT_DIRATTR) {
		server->ops->getattr = smb_proc_getattr_ff;
	}

	/* Decode server capabilities */
	if (server->opt.capabilities & SMB_CAP_LARGE_FILES) {
		/* Should be ok to set this now, as no one can access the
		   mount until the connection has been established. */
		SB_of(server)->s_maxbytes = ~0ULL >> 1;
		VERBOSE("LFS enabled\n");
	}
	if (server->opt.capabilities & SMB_CAP_UNICODE) {
		server->mnt->flags |= SMB_MOUNT_UNICODE;
		VERBOSE("Unicode enabled\n");
	} else {
		server->mnt->flags &= ~SMB_MOUNT_UNICODE;
	}
#if 0
	/* flags we may test for other patches ... */
	if (server->opt.capabilities & SMB_CAP_LARGE_READX) {
		VERBOSE("Large reads enabled\n");
	}
	if (server->opt.capabilities & SMB_CAP_LARGE_WRITEX) {
		VERBOSE("Large writes enabled\n");
	}
#endif
	if (server->opt.capabilities & SMB_CAP_UNIX) {
		struct inode *inode;
		VERBOSE("Using UNIX CIFS extensions\n");
		install_ops(server->ops, &smb_ops_unix);
		inode = SB_of(server)->s_root->d_inode;
		if (inode)
			inode->i_op = &smb_dir_inode_operations_unix;
	}

	VERBOSE("protocol=%d, max_xmit=%d, pid=%d capabilities=0x%x\n",
		server->opt.protocol, server->opt.max_xmit,
		pid_nr(server->conn_pid), server->opt.capabilities);

	/* FIXME: this really should be done by smbmount. */
	if (server->opt.max_xmit > SMB_MAX_PACKET_SIZE) {
		server->opt.max_xmit = SMB_MAX_PACKET_SIZE;
	}

	smb_unlock_server(server);
	smbiod_wake_up();
	if (server->opt.capabilities & SMB_CAP_UNIX)
		smb_proc_query_cifsunix(server);

	server->conn_complete++;
	wake_up_interruptible_all(&server->conn_wq);
	return error;

out:
	smb_unlock_server(server);
	smbiod_wake_up();
	return error;

out_putf:
	fput(filp);
	goto out;
}

/* smb_setup_header: We completely set up the packet. You only have to
   insert the command-specific fields */

__u8 *
smb_setup_header(struct smb_request *req, __u8 command, __u16 wct, __u16 bcc)
{
	__u32 xmit_len = SMB_HEADER_LEN + wct * sizeof(__u16) + bcc + 2;
	__u8 *p = req->rq_header;
	struct smb_sb_info *server = req->rq_server;

	p = smb_encode_smb_length(p, xmit_len - 4);

	*p++ = 0xff;
	*p++ = 'S';
	*p++ = 'M';
	*p++ = 'B';
	*p++ = command;

	memset(p, '\0', 19);
	p += 19;
	p += 8;

	if (server->opt.protocol > SMB_PROTOCOL_CORE) {
		int flags = SMB_FLAGS_CASELESS_PATHNAMES;
		int flags2 = SMB_FLAGS2_LONG_PATH_COMPONENTS |
			SMB_FLAGS2_EXTENDED_ATTRIBUTES;	/* EA? not really ... */

		*(req->rq_header + smb_flg) = flags;
		if (server->mnt->flags & SMB_MOUNT_UNICODE)
			flags2 |= SMB_FLAGS2_UNICODE_STRINGS;
		WSET(req->rq_header, smb_flg2, flags2);
	}
	*p++ = wct;		/* wct */
	p += 2 * wct;
	WSET(p, 0, bcc);

	/* Include the header in the data to send */
	req->rq_iovlen = 1;
	req->rq_iov[0].iov_base = req->rq_header;
	req->rq_iov[0].iov_len  = xmit_len - bcc;

	return req->rq_buffer;
}

static void
smb_setup_bcc(struct smb_request *req, __u8 *p)
{
	u16 bcc = p - req->rq_buffer;
	u8 *pbcc = req->rq_header + SMB_HEADER_LEN + 2*SMB_WCT(req->rq_header);

	WSET(pbcc, 0, bcc);

	smb_encode_smb_length(req->rq_header, SMB_HEADER_LEN + 
			      2*SMB_WCT(req->rq_header) - 2 + bcc);

	/* Include the "bytes" in the data to send */
	req->rq_iovlen = 2;
	req->rq_iov[1].iov_base = req->rq_buffer;
	req->rq_iov[1].iov_len  = bcc;
}

static int
smb_proc_seek(struct smb_sb_info *server, __u16 fileid,
	      __u16 mode, off_t offset)
{
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	smb_setup_header(req, SMBlseek, 4, 0);
	WSET(req->rq_header, smb_vwv0, fileid);
	WSET(req->rq_header, smb_vwv1, mode);
	DSET(req->rq_header, smb_vwv2, offset);
	req->rq_flags |= SMB_REQ_NORETRY;

	result = smb_request_ok(req, SMBlseek, 2, 0);
	if (result < 0) {
		result = 0;
		goto out_free;
	}

	result = DVAL(req->rq_header, smb_vwv0);
out_free:
	smb_rput(req);
out:
	return result;
}

static int
smb_proc_open(struct smb_sb_info *server, struct dentry *dentry, int wish)
{
	struct inode *ino = dentry->d_inode;
	struct smb_inode_info *ei = SMB_I(ino);
	int mode, read_write = 0x42, read_only = 0x40;
	int res;
	char *p;
	struct smb_request *req;

	/*
	 * Attempt to open r/w, unless there are no write privileges.
	 */
	mode = read_write;
	if (!(ino->i_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
		mode = read_only;
#if 0
	/* FIXME: why is this code not in? below we fix it so that a caller
	   wanting RO doesn't get RW. smb_revalidate_inode does some 
	   optimization based on access mode. tail -f needs it to be correct.

	   We must open rw since we don't do the open if called a second time
	   with different 'wish'. Is that not supported by smb servers? */
	if (!(wish & (O_WRONLY | O_RDWR)))
		mode = read_only;
#endif

	res = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

      retry:
	p = smb_setup_header(req, SMBopen, 2, 0);
	WSET(req->rq_header, smb_vwv0, mode);
	WSET(req->rq_header, smb_vwv1, aSYSTEM | aHIDDEN | aDIR);
	res = smb_simple_encode_path(req, &p, dentry, NULL);
	if (res < 0)
		goto out_free;
	smb_setup_bcc(req, p);

	res = smb_request_ok(req, SMBopen, 7, 0);
	if (res != 0) {
		if (mode == read_write &&
		    (res == -EACCES || res == -ETXTBSY || res == -EROFS))
		{
			VERBOSE("%s/%s R/W failed, error=%d, retrying R/O\n",
				DENTRY_PATH(dentry), res);
			mode = read_only;
			req->rq_flags = 0;
			goto retry;
		}
		goto out_free;
	}
	/* We should now have data in vwv[0..6]. */

	ei->fileid = WVAL(req->rq_header, smb_vwv0);
	ei->attr   = WVAL(req->rq_header, smb_vwv1);
	/* smb_vwv2 has mtime */
	/* smb_vwv4 has size  */
	ei->access = (WVAL(req->rq_header, smb_vwv6) & SMB_ACCMASK);
	ei->open = server->generation;

out_free:
	smb_rput(req);
out:
	return res;
}

/*
 * Make sure the file is open, and check that the access
 * is compatible with the desired access.
 */
int
smb_open(struct dentry *dentry, int wish)
{
	struct inode *inode = dentry->d_inode;
	int result;
	__u16 access;

	result = -ENOENT;
	if (!inode) {
		printk(KERN_ERR "smb_open: no inode for dentry %s/%s\n",
		       DENTRY_PATH(dentry));
		goto out;
	}

	if (!smb_is_open(inode)) {
		struct smb_sb_info *server = server_from_inode(inode);
		result = 0;
		if (!smb_is_open(inode))
			result = smb_proc_open(server, dentry, wish);
		if (result)
			goto out;
		/*
		 * A successful open means the path is still valid ...
		 */
		smb_renew_times(dentry);
	}

	/*
	 * Check whether the access is compatible with the desired mode.
	 */
	result = 0;
	access = SMB_I(inode)->access;
	if (access != wish && access != SMB_O_RDWR) {
		PARANOIA("%s/%s access denied, access=%x, wish=%x\n",
			 DENTRY_PATH(dentry), access, wish);
		result = -EACCES;
	}
out:
	return result;
}

static int 
smb_proc_close(struct smb_sb_info *server, __u16 fileid, __u32 mtime)
{
	struct smb_request *req;
	int result = -ENOMEM;

	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	smb_setup_header(req, SMBclose, 3, 0);
	WSET(req->rq_header, smb_vwv0, fileid);
	DSET(req->rq_header, smb_vwv1, utc2local(server, mtime));
	req->rq_flags |= SMB_REQ_NORETRY;
	result = smb_request_ok(req, SMBclose, 0, 0);

	smb_rput(req);
out:
	return result;
}

/*
 * Win NT 4.0 has an apparent bug in that it fails to update the
 * modify time when writing to a file. As a workaround, we update
 * both modify and access time locally, and post the times to the
 * server when closing the file.
 */
static int 
smb_proc_close_inode(struct smb_sb_info *server, struct inode * ino)
{
	struct smb_inode_info *ei = SMB_I(ino);
	int result = 0;
	if (smb_is_open(ino))
	{
		/*
		 * We clear the open flag in advance, in case another
 		 * process observes the value while we block below.
		 */
		ei->open = 0;

		/*
		 * Kludge alert: SMB timestamps are accurate only to
		 * two seconds ... round the times to avoid needless
		 * cache invalidations!
		 */
		if (ino->i_mtime.tv_sec & 1) { 
			ino->i_mtime.tv_sec--;
			ino->i_mtime.tv_nsec = 0; 
		}
		if (ino->i_atime.tv_sec & 1) {
			ino->i_atime.tv_sec--;
			ino->i_atime.tv_nsec = 0;
		}
		/*
		 * If the file is open with write permissions,
		 * update the time stamps to sync mtime and atime.
		 */
		if ((server->opt.capabilities & SMB_CAP_UNIX) == 0 &&
		    (server->opt.protocol >= SMB_PROTOCOL_LANMAN2) &&
		    !(ei->access == SMB_O_RDONLY))
		{
			struct smb_fattr fattr;
			smb_get_inode_attr(ino, &fattr);
			smb_proc_setattr_ext(server, ino, &fattr);
		}

		result = smb_proc_close(server, ei->fileid, ino->i_mtime.tv_sec);
		/*
		 * Force a revalidation after closing ... some servers
		 * don't post the size until the file has been closed.
		 */
		if (server->opt.protocol < SMB_PROTOCOL_NT1)
			ei->oldmtime = 0;
		ei->closed = jiffies;
	}
	return result;
}

int
smb_close(struct inode *ino)
{
	int result = 0;

	if (smb_is_open(ino)) {
		struct smb_sb_info *server = server_from_inode(ino);
		result = smb_proc_close_inode(server, ino);
	}
	return result;
}

/*
 * This is used to close a file following a failed instantiate.
 * Since we don't have an inode, we can't use any of the above.
 */
int
smb_close_fileid(struct dentry *dentry, __u16 fileid)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	int result;

	result = smb_proc_close(server, fileid, get_seconds());
	return result;
}

/* In smb_proc_read and smb_proc_write we do not retry, because the
   file-id would not be valid after a reconnection. */

static void
smb_proc_read_data(struct smb_request *req)
{
	req->rq_iov[0].iov_base = req->rq_buffer;
	req->rq_iov[0].iov_len  = 3;

	req->rq_iov[1].iov_base = req->rq_page;
	req->rq_iov[1].iov_len  = req->rq_rsize;
	req->rq_iovlen = 2;

	req->rq_rlen = smb_len(req->rq_header) + 4 - req->rq_bytes_recvd;
}

static int
smb_proc_read(struct inode *inode, loff_t offset, int count, char *data)
{
	struct smb_sb_info *server = server_from_inode(inode);
	__u16 returned_count, data_len;
	unsigned char *buf;
	int result;
	struct smb_request *req;
	u8 rbuf[4];

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	smb_setup_header(req, SMBread, 5, 0);
	buf = req->rq_header;
	WSET(buf, smb_vwv0, SMB_I(inode)->fileid);
	WSET(buf, smb_vwv1, count);
	DSET(buf, smb_vwv2, offset);
	WSET(buf, smb_vwv4, 0);

	req->rq_page = data;
	req->rq_rsize = count;
	req->rq_callback = smb_proc_read_data;
	req->rq_buffer = rbuf;
	req->rq_flags |= SMB_REQ_NORETRY | SMB_REQ_STATIC;

	result = smb_request_ok(req, SMBread, 5, -1);
	if (result < 0)
		goto out_free;
	returned_count = WVAL(req->rq_header, smb_vwv0);

	data_len = WVAL(rbuf, 1);

	if (returned_count != data_len) {
		printk(KERN_NOTICE "smb_proc_read: returned != data_len\n");
		printk(KERN_NOTICE "smb_proc_read: ret_c=%d, data_len=%d\n",
		       returned_count, data_len);
	}
	result = data_len;

out_free:
	smb_rput(req);
out:
	VERBOSE("ino=%ld, fileid=%d, count=%d, result=%d\n",
		inode->i_ino, SMB_I(inode)->fileid, count, result);
	return result;
}

static int
smb_proc_write(struct inode *inode, loff_t offset, int count, const char *data)
{
	struct smb_sb_info *server = server_from_inode(inode);
	int result;
	u16 fileid = SMB_I(inode)->fileid;
	u8 buf[4];
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	VERBOSE("ino=%ld, fileid=%d, count=%d@%Ld\n",
		inode->i_ino, fileid, count, offset);

	smb_setup_header(req, SMBwrite, 5, count + 3);
	WSET(req->rq_header, smb_vwv0, fileid);
	WSET(req->rq_header, smb_vwv1, count);
	DSET(req->rq_header, smb_vwv2, offset);
	WSET(req->rq_header, smb_vwv4, 0);

	buf[0] = 1;
	WSET(buf, 1, count);	/* yes, again ... */
	req->rq_iov[1].iov_base = buf;
	req->rq_iov[1].iov_len = 3;
	req->rq_iov[2].iov_base = (char *) data;
	req->rq_iov[2].iov_len = count;
	req->rq_iovlen = 3;
	req->rq_flags |= SMB_REQ_NORETRY;

	result = smb_request_ok(req, SMBwrite, 1, 0);
	if (result >= 0)
		result = WVAL(req->rq_header, smb_vwv0);

	smb_rput(req);
out:
	return result;
}

/*
 * In smb_proc_readX and smb_proc_writeX we do not retry, because the
 * file-id would not be valid after a reconnection.
 */

#define SMB_READX_MAX_PAD      64
static void
smb_proc_readX_data(struct smb_request *req)
{
	/* header length, excluding the netbios length (-4) */
	int hdrlen = SMB_HEADER_LEN + req->rq_resp_wct*2 - 2;
	int data_off = WVAL(req->rq_header, smb_vwv6);

	/*
	 * Some genius made the padding to the data bytes arbitrary.
	 * So we must first calculate the amount of padding used by the server.
	 */
	data_off -= hdrlen;
	if (data_off > SMB_READX_MAX_PAD || data_off < 0) {
		PARANOIA("offset is larger than SMB_READX_MAX_PAD or negative!\n");
		PARANOIA("%d > %d || %d < 0\n", data_off, SMB_READX_MAX_PAD, data_off);
		req->rq_rlen = req->rq_bufsize + 1;
		return;
	}
	req->rq_iov[0].iov_base = req->rq_buffer;
	req->rq_iov[0].iov_len  = data_off;

	req->rq_iov[1].iov_base = req->rq_page;
	req->rq_iov[1].iov_len  = req->rq_rsize;
	req->rq_iovlen = 2;

	req->rq_rlen = smb_len(req->rq_header) + 4 - req->rq_bytes_recvd;
}

static int
smb_proc_readX(struct inode *inode, loff_t offset, int count, char *data)
{
	struct smb_sb_info *server = server_from_inode(inode);
	unsigned char *buf;
	int result;
	struct smb_request *req;
	static char pad[SMB_READX_MAX_PAD];

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	smb_setup_header(req, SMBreadX, 12, 0);
	buf = req->rq_header;
	WSET(buf, smb_vwv0, 0x00ff);
	WSET(buf, smb_vwv1, 0);
	WSET(buf, smb_vwv2, SMB_I(inode)->fileid);
	DSET(buf, smb_vwv3, (u32)offset);               /* low 32 bits */
	WSET(buf, smb_vwv5, count);
	WSET(buf, smb_vwv6, 0);
	DSET(buf, smb_vwv7, 0);
	WSET(buf, smb_vwv9, 0);
	DSET(buf, smb_vwv10, (u32)(offset >> 32));      /* high 32 bits */
	WSET(buf, smb_vwv11, 0);

	req->rq_page = data;
	req->rq_rsize = count;
	req->rq_callback = smb_proc_readX_data;
	req->rq_buffer = pad;
	req->rq_bufsize = SMB_READX_MAX_PAD;
	req->rq_flags |= SMB_REQ_STATIC | SMB_REQ_NORETRY;

	result = smb_request_ok(req, SMBreadX, 12, -1);
	if (result < 0)
		goto out_free;
	result = WVAL(req->rq_header, smb_vwv5);

out_free:
	smb_rput(req);
out:
	VERBOSE("ino=%ld, fileid=%d, count=%d, result=%d\n",
		inode->i_ino, SMB_I(inode)->fileid, count, result);
	return result;
}

static int
smb_proc_writeX(struct inode *inode, loff_t offset, int count, const char *data)
{
	struct smb_sb_info *server = server_from_inode(inode);
	int result;
	u8 *p;
	static u8 pad[4];
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	VERBOSE("ino=%ld, fileid=%d, count=%d@%Ld\n",
		inode->i_ino, SMB_I(inode)->fileid, count, offset);

	p = smb_setup_header(req, SMBwriteX, 14, count + 1);
	WSET(req->rq_header, smb_vwv0, 0x00ff);
	WSET(req->rq_header, smb_vwv1, 0);
	WSET(req->rq_header, smb_vwv2, SMB_I(inode)->fileid);
	DSET(req->rq_header, smb_vwv3, (u32)offset);	/* low 32 bits */
	DSET(req->rq_header, smb_vwv5, 0);
	WSET(req->rq_header, smb_vwv7, 0);		/* write mode */
	WSET(req->rq_header, smb_vwv8, 0);
	WSET(req->rq_header, smb_vwv9, 0);
	WSET(req->rq_header, smb_vwv10, count);		/* data length */
	WSET(req->rq_header, smb_vwv11, smb_vwv12 + 2 + 1);
	DSET(req->rq_header, smb_vwv12, (u32)(offset >> 32));

	req->rq_iov[1].iov_base = pad;
	req->rq_iov[1].iov_len = 1;
	req->rq_iov[2].iov_base = (char *) data;
	req->rq_iov[2].iov_len = count;
	req->rq_iovlen = 3;
	req->rq_flags |= SMB_REQ_NORETRY;

	result = smb_request_ok(req, SMBwriteX, 6, 0);
 	if (result >= 0)
		result = WVAL(req->rq_header, smb_vwv2);

	smb_rput(req);
out:
	return result;
}

int
smb_proc_create(struct dentry *dentry, __u16 attr, time_t ctime, __u16 *fileid)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	char *p;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

	p = smb_setup_header(req, SMBcreate, 3, 0);
	WSET(req->rq_header, smb_vwv0, attr);
	DSET(req->rq_header, smb_vwv1, utc2local(server, ctime));
	result = smb_simple_encode_path(req, &p, dentry, NULL);
	if (result < 0)
		goto out_free;
	smb_setup_bcc(req, p);

	result = smb_request_ok(req, SMBcreate, 1, 0);
	if (result < 0)
		goto out_free;

	*fileid = WVAL(req->rq_header, smb_vwv0);
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

int
smb_proc_mv(struct dentry *old_dentry, struct dentry *new_dentry)
{
	struct smb_sb_info *server = server_from_dentry(old_dentry);
	char *p;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

	p = smb_setup_header(req, SMBmv, 1, 0);
	WSET(req->rq_header, smb_vwv0, aSYSTEM | aHIDDEN | aDIR);
	result = smb_simple_encode_path(req, &p, old_dentry, NULL);
	if (result < 0)
		goto out_free;
	result = smb_simple_encode_path(req, &p, new_dentry, NULL);
	if (result < 0)
		goto out_free;
	smb_setup_bcc(req, p);

	if ((result = smb_request_ok(req, SMBmv, 0, 0)) < 0)
		goto out_free;
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

/*
 * Code common to mkdir and rmdir.
 */
static int
smb_proc_generic_command(struct dentry *dentry, __u8 command)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	char *p;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

	p = smb_setup_header(req, command, 0, 0);
	result = smb_simple_encode_path(req, &p, dentry, NULL);
	if (result < 0)
		goto out_free;
	smb_setup_bcc(req, p);

	result = smb_request_ok(req, command, 0, 0);
	if (result < 0)
		goto out_free;
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

int
smb_proc_mkdir(struct dentry *dentry)
{
	return smb_proc_generic_command(dentry, SMBmkdir);
}

int
smb_proc_rmdir(struct dentry *dentry)
{
	return smb_proc_generic_command(dentry, SMBrmdir);
}

#if SMBFS_POSIX_UNLINK
/*
 * Removes readonly attribute from a file. Used by unlink to give posix
 * semantics.
 */
static int
smb_set_rw(struct dentry *dentry,struct smb_sb_info *server)
{
	int result;
	struct smb_fattr fattr;

	/* FIXME: cifsUE should allow removing a readonly file. */

	/* first get current attribute */
	smb_init_dirent(server, &fattr);
	result = server->ops->getattr(server, dentry, &fattr);
	smb_finish_dirent(server, &fattr);
	if (result < 0)
		return result;

	/* if RONLY attribute is set, remove it */
	if (fattr.attr & aRONLY) {  /* read only attribute is set */
		fattr.attr &= ~aRONLY;
		result = smb_proc_setattr_core(server, dentry, fattr.attr);
	}
	return result;
}
#endif

int
smb_proc_unlink(struct dentry *dentry)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	int flag = 0;
	char *p;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

      retry:
	p = smb_setup_header(req, SMBunlink, 1, 0);
	WSET(req->rq_header, smb_vwv0, aSYSTEM | aHIDDEN);
	result = smb_simple_encode_path(req, &p, dentry, NULL);
	if (result < 0)
		goto out_free;
	smb_setup_bcc(req, p);

	if ((result = smb_request_ok(req, SMBunlink, 0, 0)) < 0) {
#if SMBFS_POSIX_UNLINK
		if (result == -EACCES && !flag) {
			/* Posix semantics is for the read-only state
			   of a file to be ignored in unlink(). In the
			   SMB world a unlink() is refused on a
			   read-only file. To make things easier for
			   unix users we try to override the files
			   permission if the unlink fails with the
			   right error.
			   This introduces a race condition that could
			   lead to a file being written by someone who
			   shouldn't have access, but as far as I can
			   tell that is unavoidable */

			/* remove RONLY attribute and try again */
			result = smb_set_rw(dentry,server);
			if (result == 0) {
				flag = 1;
				req->rq_flags = 0;
				goto retry;
			}
		}
#endif
		goto out_free;
	}
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

int
smb_proc_flush(struct smb_sb_info *server, __u16 fileid)
{
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	smb_setup_header(req, SMBflush, 1, 0);
	WSET(req->rq_header, smb_vwv0, fileid);
	req->rq_flags |= SMB_REQ_NORETRY;
	result = smb_request_ok(req, SMBflush, 0, 0);

	smb_rput(req);
out:
	return result;
}

static int
smb_proc_trunc32(struct inode *inode, loff_t length)
{
	/*
	 * Writing 0bytes is old-SMB magic for truncating files.
	 * MAX_NON_LFS should prevent this from being called with a too
	 * large offset.
	 */
	return smb_proc_write(inode, length, 0, NULL);
}

static int
smb_proc_trunc64(struct inode *inode, loff_t length)
{
	struct smb_sb_info *server = server_from_inode(inode);
	int result;
	char *param;
	char *data;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 14)))
		goto out;

	param = req->rq_buffer;
	data = req->rq_buffer + 6;

	/* FIXME: must we also set allocation size? winNT seems to do that */
	WSET(param, 0, SMB_I(inode)->fileid);
	WSET(param, 2, SMB_SET_FILE_END_OF_FILE_INFO);
	WSET(param, 4, 0);
	LSET(data, 0, length);

	req->rq_trans2_command = TRANSACT2_SETFILEINFO;
	req->rq_ldata = 8;
	req->rq_data  = data;
	req->rq_lparm = 6;
	req->rq_parm  = param;
	req->rq_flags |= SMB_REQ_NORETRY;
	result = smb_add_request(req);
	if (result < 0)
		goto out_free;

	result = 0;
	if (req->rq_rcls != 0)
		result = smb_errno(req);

out_free:
	smb_rput(req);
out:
	return result;
}

static int
smb_proc_trunc95(struct inode *inode, loff_t length)
{
	struct smb_sb_info *server = server_from_inode(inode);
	int result = smb_proc_trunc32(inode, length);
 
	/*
	 * win9x doesn't appear to update the size immediately.
	 * It will return the old file size after the truncate,
	 * confusing smbfs. So we force an update.
	 *
	 * FIXME: is this still necessary?
	 */
	smb_proc_flush(server, SMB_I(inode)->fileid);
	return result;
}

static void
smb_init_dirent(struct smb_sb_info *server, struct smb_fattr *fattr)
{
	memset(fattr, 0, sizeof(*fattr));

	fattr->f_nlink = 1;
	fattr->f_uid = server->mnt->uid;
	fattr->f_gid = server->mnt->gid;
	fattr->f_unix = 0;
}

static void
smb_finish_dirent(struct smb_sb_info *server, struct smb_fattr *fattr)
{
	if (fattr->f_unix)
		return;

	fattr->f_mode = server->mnt->file_mode;
	if (fattr->attr & aDIR) {
		fattr->f_mode = server->mnt->dir_mode;
		fattr->f_size = SMB_ST_BLKSIZE;
	}
	/* Check the read-only flag */
	if (fattr->attr & aRONLY)
		fattr->f_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);

	/* How many 512 byte blocks do we need for this file? */
	fattr->f_blocks = 0;
	if (fattr->f_size != 0)
		fattr->f_blocks = 1 + ((fattr->f_size-1) >> 9);
	return;
}

void
smb_init_root_dirent(struct smb_sb_info *server, struct smb_fattr *fattr,
		     struct super_block *sb)
{
	smb_init_dirent(server, fattr);
	fattr->attr = aDIR;
	fattr->f_ino = 2; /* traditional root inode number */
	fattr->f_mtime = current_fs_time(sb);
	smb_finish_dirent(server, fattr);
}

/*
 * Decode a dirent for old protocols
 *
 * qname is filled with the decoded, and possibly translated, name.
 * fattr receives decoded attributes
 *
 * Bugs Noted:
 * (1) Pathworks servers may pad the name with extra spaces.
 */
static char *
smb_decode_short_dirent(struct smb_sb_info *server, char *p,
			struct qstr *qname, struct smb_fattr *fattr,
			unsigned char *name_buf)
{
	int len;

	/*
	 * SMB doesn't have a concept of inode numbers ...
	 */
	smb_init_dirent(server, fattr);
	fattr->f_ino = 0;	/* FIXME: do we need this? */

	p += SMB_STATUS_SIZE;	/* reserved (search_status) */
	fattr->attr = *p;
	fattr->f_mtime.tv_sec = date_dos2unix(server, WVAL(p, 3), WVAL(p, 1));
	fattr->f_mtime.tv_nsec = 0;
	fattr->f_size = DVAL(p, 5);
	fattr->f_ctime = fattr->f_mtime;
	fattr->f_atime = fattr->f_mtime;
	qname->name = p + 9;
	len = strnlen(qname->name, 12);

	/*
	 * Trim trailing blanks for Pathworks servers
	 */
	while (len > 2 && qname->name[len-1] == ' ')
		len--;

	smb_finish_dirent(server, fattr);

#if 0
	/* FIXME: These only work for ascii chars, and recent smbmount doesn't
	   allow the flag to be set anyway. It kills const. Remove? */
	switch (server->opt.case_handling) {
	case SMB_CASE_UPPER:
		str_upper(entry->name, len);
		break;
	case SMB_CASE_LOWER:
		str_lower(entry->name, len);
		break;
	default:
		break;
	}
#endif

	qname->len = 0;
	len = server->ops->convert(name_buf, SMB_MAXNAMELEN,
				   qname->name, len,
				   server->remote_nls, server->local_nls);
	if (len > 0) {
		qname->len = len;
		qname->name = name_buf;
		DEBUG1("len=%d, name=%.*s\n",qname->len,qname->len,qname->name);
	}

	return p + 22;
}

/*
 * This routine is used to read in directory entries from the network.
 * Note that it is for short directory name seeks, i.e.: protocol <
 * SMB_PROTOCOL_LANMAN2
 */
static int
smb_proc_readdir_short(struct file *filp, void *dirent, filldir_t filldir,
		       struct smb_cache_control *ctl)
{
	struct dentry *dir = filp->f_path.dentry;
	struct smb_sb_info *server = server_from_dentry(dir);
	struct qstr qname;
	struct smb_fattr fattr;
	char *p;
	int result;
	int i, first, entries_seen, entries;
	int entries_asked = (server->opt.max_xmit - 100) / SMB_DIRINFO_SIZE;
	__u16 bcc;
	__u16 count;
	char status[SMB_STATUS_SIZE];
	static struct qstr mask = {
		.name	= "*.*",
		.len	= 3,
	};
	unsigned char *last_status;
	struct smb_request *req;
	unsigned char *name_buf;

	VERBOSE("%s/%s\n", DENTRY_PATH(dir));

	lock_kernel();

	result = -ENOMEM;
	if (! (name_buf = kmalloc(SMB_MAXNAMELEN, GFP_KERNEL)))
		goto out;

	first = 1;
	entries = 0;
	entries_seen = 2; /* implicit . and .. */

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, server->opt.max_xmit)))
		goto out_name;

	while (1) {
		p = smb_setup_header(req, SMBsearch, 2, 0);
		WSET(req->rq_header, smb_vwv0, entries_asked);
		WSET(req->rq_header, smb_vwv1, aDIR);
		if (first == 1) {
			result = smb_simple_encode_path(req, &p, dir, &mask);
			if (result < 0)
				goto out_free;
			if (p + 3 > (char *)req->rq_buffer + req->rq_bufsize) {
				result = -ENAMETOOLONG;
				goto out_free;
			}
			*p++ = 5;
			WSET(p, 0, 0);
			p += 2;
			first = 0;
		} else {
			if (p + 5 + SMB_STATUS_SIZE >
			    (char *)req->rq_buffer + req->rq_bufsize) {
				result = -ENAMETOOLONG;
				goto out_free;
			}
				
			*p++ = 4;
			*p++ = 0;
			*p++ = 5;
			WSET(p, 0, SMB_STATUS_SIZE);
			p += 2;
			memcpy(p, status, SMB_STATUS_SIZE);
			p += SMB_STATUS_SIZE;
		}

		smb_setup_bcc(req, p);

		result = smb_request_ok(req, SMBsearch, 1, -1);
		if (result < 0) {
			if ((req->rq_rcls == ERRDOS) && 
			    (req->rq_err  == ERRnofiles))
				break;
			goto out_free;
		}
		count = WVAL(req->rq_header, smb_vwv0);
		if (count <= 0)
			break;

		result = -EIO;
		bcc = smb_bcc(req->rq_header);
		if (bcc != count * SMB_DIRINFO_SIZE + 3)
			goto out_free;
		p = req->rq_buffer + 3;


		/* Make sure the response fits in the buffer. Fixed sized 
		   entries means we don't have to check in the decode loop. */

		last_status = req->rq_buffer + 3 + (count-1) * SMB_DIRINFO_SIZE;

		if (last_status + SMB_DIRINFO_SIZE >=
		    req->rq_buffer + req->rq_bufsize) {
			printk(KERN_ERR "smb_proc_readdir_short: "
			       "last dir entry outside buffer! "
			       "%d@%p  %d@%p\n", SMB_DIRINFO_SIZE, last_status,
			       req->rq_bufsize, req->rq_buffer);
			goto out_free;
		}

		/* Read the last entry into the status field. */
		memcpy(status, last_status, SMB_STATUS_SIZE);


		/* Now we are ready to parse smb directory entries. */

		for (i = 0; i < count; i++) {
			p = smb_decode_short_dirent(server, p, 
						    &qname, &fattr, name_buf);
			if (qname.len == 0)
				continue;

			if (entries_seen == 2 && qname.name[0] == '.') {
				if (qname.len == 1)
					continue;
				if (qname.name[1] == '.' && qname.len == 2)
					continue;
			}
			if (!smb_fill_cache(filp, dirent, filldir, ctl, 
					    &qname, &fattr))
				;	/* stop reading? */
			entries_seen++;
		}
	}
	result = entries;

out_free:
	smb_rput(req);
out_name:
	kfree(name_buf);
out:
	unlock_kernel();
	return result;
}

static void smb_decode_unix_basic(struct smb_fattr *fattr, struct smb_sb_info *server, char *p)
{
	u64 size, disk_bytes;

	/* FIXME: verify nls support. all is sent as utf8? */

	fattr->f_unix = 1;
	fattr->f_mode = 0;

	/* FIXME: use the uniqueID from the remote instead? */
	/* 0 L file size in bytes */
	/* 8 L file size on disk in bytes (block count) */
	/* 40 L uid */
	/* 48 L gid */
	/* 56 W file type */
	/* 60 L devmajor */
	/* 68 L devminor */
	/* 76 L unique ID (inode) */
	/* 84 L permissions */
	/* 92 L link count */

	size = LVAL(p, 0);
	disk_bytes = LVAL(p, 8);

	/*
	 * Some samba versions round up on-disk byte usage
	 * to 1MB boundaries, making it useless. When seeing
	 * that, use the size instead.
	 */
	if (!(disk_bytes & 0xfffff))
		disk_bytes = size+511;

	fattr->f_size = size;
	fattr->f_blocks = disk_bytes >> 9;
	fattr->f_ctime = smb_ntutc2unixutc(LVAL(p, 16));
	fattr->f_atime = smb_ntutc2unixutc(LVAL(p, 24));
	fattr->f_mtime = smb_ntutc2unixutc(LVAL(p, 32));

	if (server->mnt->flags & SMB_MOUNT_UID)
		fattr->f_uid = server->mnt->uid;
	else
		fattr->f_uid = LVAL(p, 40);

	if (server->mnt->flags & SMB_MOUNT_GID)
		fattr->f_gid = server->mnt->gid;
	else
		fattr->f_gid = LVAL(p, 48);

	fattr->f_mode |= smb_filetype_to_mode(WVAL(p, 56));

	if (S_ISBLK(fattr->f_mode) || S_ISCHR(fattr->f_mode)) {
		__u64 major = LVAL(p, 60);
		__u64 minor = LVAL(p, 68);

		fattr->f_rdev = MKDEV(major & 0xffffffff, minor & 0xffffffff);
		if (MAJOR(fattr->f_rdev) != (major & 0xffffffff) ||
	    	MINOR(fattr->f_rdev) != (minor & 0xffffffff))
			fattr->f_rdev = 0;
	}

	fattr->f_mode |= LVAL(p, 84);

	if ( (server->mnt->flags & SMB_MOUNT_DMODE) &&
	     (S_ISDIR(fattr->f_mode)) )
		fattr->f_mode = (server->mnt->dir_mode & S_IRWXUGO) | S_IFDIR;
	else if ( (server->mnt->flags & SMB_MOUNT_FMODE) &&
	          !(S_ISDIR(fattr->f_mode)) )
		fattr->f_mode = (server->mnt->file_mode & S_IRWXUGO) |
				(fattr->f_mode & S_IFMT);

}

/*
 * Interpret a long filename structure using the specified info level:
 *   level 1 for anything below NT1 protocol
 *   level 260 for NT1 protocol
 *
 * qname is filled with the decoded, and possibly translated, name
 * fattr receives decoded attributes.
 *
 * Bugs Noted:
 * (1) Win NT 4.0 appends a null byte to names and counts it in the length!
 */
static char *
smb_decode_long_dirent(struct smb_sb_info *server, char *p, int level,
		       struct qstr *qname, struct smb_fattr *fattr,
		       unsigned char *name_buf)
{
	char *result;
	unsigned int len = 0;
	int n;
	__u16 date, time;
	int unicode = (server->mnt->flags & SMB_MOUNT_UNICODE);

	/*
	 * SMB doesn't have a concept of inode numbers ...
	 */
	smb_init_dirent(server, fattr);
	fattr->f_ino = 0;	/* FIXME: do we need this? */

	switch (level) {
	case 1:
		len = *((unsigned char *) p + 22);
		qname->name = p + 23;
		result = p + 24 + len;

		date = WVAL(p, 0);
		time = WVAL(p, 2);
		fattr->f_ctime.tv_sec = date_dos2unix(server, date, time);
		fattr->f_ctime.tv_nsec = 0;

		date = WVAL(p, 4);
		time = WVAL(p, 6);
		fattr->f_atime.tv_sec = date_dos2unix(server, date, time);
		fattr->f_atime.tv_nsec = 0;

		date = WVAL(p, 8);
		time = WVAL(p, 10);
		fattr->f_mtime.tv_sec = date_dos2unix(server, date, time);
		fattr->f_mtime.tv_nsec = 0;
		fattr->f_size = DVAL(p, 12);
		/* ULONG allocation size */
		fattr->attr = WVAL(p, 20);

		VERBOSE("info 1 at %p, len=%d, name=%.*s\n",
			p, len, len, qname->name);
		break;
	case 260:
		result = p + WVAL(p, 0);
		len = DVAL(p, 60);
		if (len > 255) len = 255;
		/* NT4 null terminates, unless we are using unicode ... */
		qname->name = p + 94;
		if (!unicode && len && qname->name[len-1] == '\0')
			len--;

		fattr->f_ctime = smb_ntutc2unixutc(LVAL(p, 8));
		fattr->f_atime = smb_ntutc2unixutc(LVAL(p, 16));
		fattr->f_mtime = smb_ntutc2unixutc(LVAL(p, 24));
		/* change time (32) */
		fattr->f_size = LVAL(p, 40);
		/* alloc size (48) */
		fattr->attr = DVAL(p, 56);

		VERBOSE("info 260 at %p, len=%d, name=%.*s\n",
			p, len, len, qname->name);
		break;
	case SMB_FIND_FILE_UNIX:
		result = p + WVAL(p, 0);
		qname->name = p + 108;

		len = strlen(qname->name);
		/* FIXME: should we check the length?? */

		p += 8;
		smb_decode_unix_basic(fattr, server, p);
		VERBOSE("info SMB_FIND_FILE_UNIX at %p, len=%d, name=%.*s\n",
			p, len, len, qname->name);
		break;
	default:
		PARANOIA("Unknown info level %d\n", level);
		result = p + WVAL(p, 0);
		goto out;
	}

	smb_finish_dirent(server, fattr);

#if 0
	/* FIXME: These only work for ascii chars, and recent smbmount doesn't
	   allow the flag to be set anyway. Remove? */
	switch (server->opt.case_handling) {
	case SMB_CASE_UPPER:
		str_upper(qname->name, len);
		break;
	case SMB_CASE_LOWER:
		str_lower(qname->name, len);
		break;
	default:
		break;
	}
#endif

	qname->len = 0;
	n = server->ops->convert(name_buf, SMB_MAXNAMELEN,
				 qname->name, len,
				 server->remote_nls, server->local_nls);
	if (n > 0) {
		qname->len = n;
		qname->name = name_buf;
	}

out:
	return result;
}

/* findfirst/findnext flags */
#define SMB_CLOSE_AFTER_FIRST (1<<0)
#define SMB_CLOSE_IF_END (1<<1)
#define SMB_REQUIRE_RESUME_KEY (1<<2)
#define SMB_CONTINUE_BIT (1<<3)

/*
 * Note: samba-2.0.7 (at least) has a very similar routine, cli_list, in
 * source/libsmb/clilist.c. When looking for smb bugs in the readdir code,
 * go there for advise.
 *
 * Bugs Noted:
 * (1) When using Info Level 1 Win NT 4.0 truncates directory listings 
 * for certain patterns of names and/or lengths. The breakage pattern
 * is completely reproducible and can be toggled by the creation of a
 * single file. (E.g. echo hi >foo breaks, rm -f foo works.)
 */
static int
smb_proc_readdir_long(struct file *filp, void *dirent, filldir_t filldir,
		      struct smb_cache_control *ctl)
{
	struct dentry *dir = filp->f_path.dentry;
	struct smb_sb_info *server = server_from_dentry(dir);
	struct qstr qname;
	struct smb_fattr fattr;

	unsigned char *p, *lastname;
	char *mask, *param;
	__u16 command;
	int first, entries_seen;

	/* Both NT and OS/2 accept info level 1 (but see note below). */
	int info_level = 260;
	const int max_matches = 512;

	unsigned int ff_searchcount = 0;
	unsigned int ff_eos = 0;
	unsigned int ff_lastname = 0;
	unsigned int ff_dir_handle = 0;
	unsigned int loop_count = 0;
	unsigned int mask_len, i;
	int result;
	struct smb_request *req;
	unsigned char *name_buf;
	static struct qstr star = {
		.name	= "*",
		.len	= 1,
	};

	lock_kernel();

	/*
	 * We always prefer unix style. Use info level 1 for older
	 * servers that don't do 260.
	 */
	if (server->opt.capabilities & SMB_CAP_UNIX)
		info_level = SMB_FIND_FILE_UNIX;
	else if (server->opt.protocol < SMB_PROTOCOL_NT1)
		info_level = 1;

	result = -ENOMEM;
	if (! (name_buf = kmalloc(SMB_MAXNAMELEN+2, GFP_KERNEL)))
		goto out;
	if (! (req = smb_alloc_request(server, server->opt.max_xmit)))
		goto out_name;
	param = req->rq_buffer;

	/*
	 * Encode the initial path
	 */
	mask = param + 12;

	result = smb_encode_path(server, mask, SMB_MAXPATHLEN+1, dir, &star);
	if (result <= 0)
		goto out_free;
	mask_len = result - 1;	/* mask_len is strlen, not #bytes */
	result = 0;
	first = 1;
	VERBOSE("starting mask_len=%d, mask=%s\n", mask_len, mask);

	entries_seen = 2;
	ff_eos = 0;

	while (ff_eos == 0) {
		loop_count += 1;
		if (loop_count > 10) {
			printk(KERN_WARNING "smb_proc_readdir_long: "
			       "Looping in FIND_NEXT??\n");
			result = -EIO;
			break;
		}

		if (first != 0) {
			command = TRANSACT2_FINDFIRST;
			WSET(param, 0, aSYSTEM | aHIDDEN | aDIR);
			WSET(param, 2, max_matches);	/* max count */
			WSET(param, 4, SMB_CLOSE_IF_END);
			WSET(param, 6, info_level);
			DSET(param, 8, 0);
		} else {
			command = TRANSACT2_FINDNEXT;

			VERBOSE("handle=0x%X, lastname=%d, mask=%.*s\n",
				ff_dir_handle, ff_lastname, mask_len, mask);

			WSET(param, 0, ff_dir_handle);	/* search handle */
			WSET(param, 2, max_matches);	/* max count */
			WSET(param, 4, info_level);
			DSET(param, 6, 0);
			WSET(param, 10, SMB_CONTINUE_BIT|SMB_CLOSE_IF_END);
		}

		req->rq_trans2_command = command;
		req->rq_ldata = 0;
		req->rq_data  = NULL;
		req->rq_lparm = 12 + mask_len + 1;
		req->rq_parm  = param;
		req->rq_flags = 0;
		result = smb_add_request(req);
		if (result < 0) {
			PARANOIA("error=%d, breaking\n", result);
			break;
		}

		if (req->rq_rcls == ERRSRV && req->rq_err == ERRerror) {
			/* a damn Win95 bug - sometimes it clags if you 
			   ask it too fast */
			schedule_timeout_interruptible(msecs_to_jiffies(200));
			continue;
                }

		if (req->rq_rcls != 0) {
			result = smb_errno(req);
			PARANOIA("name=%s, result=%d, rcls=%d, err=%d\n",
				 mask, result, req->rq_rcls, req->rq_err);
			break;
		}

		/* parse out some important return info */
		if (first != 0) {
			ff_dir_handle = WVAL(req->rq_parm, 0);
			ff_searchcount = WVAL(req->rq_parm, 2);
			ff_eos = WVAL(req->rq_parm, 4);
			ff_lastname = WVAL(req->rq_parm, 8);
		} else {
			ff_searchcount = WVAL(req->rq_parm, 0);
			ff_eos = WVAL(req->rq_parm, 2);
			ff_lastname = WVAL(req->rq_parm, 6);
		}

		if (ff_searchcount == 0)
			break;

		/* Now we are ready to parse smb directory entries. */

		/* point to the data bytes */
		p = req->rq_data;
		for (i = 0; i < ff_searchcount; i++) {
			/* make sure we stay within the buffer */
			if (p >= req->rq_data + req->rq_ldata) {
				printk(KERN_ERR "smb_proc_readdir_long: "
				       "dirent pointer outside buffer! "
				       "%p  %d@%p\n",
				       p, req->rq_ldata, req->rq_data);
				result = -EIO; /* always a comm. error? */
				goto out_free;
			}

			p = smb_decode_long_dirent(server, p, info_level,
						   &qname, &fattr, name_buf);

			/* ignore . and .. from the server */
			if (entries_seen == 2 && qname.name[0] == '.') {
				if (qname.len == 1)
					continue;
				if (qname.name[1] == '.' && qname.len == 2)
					continue;
			}

			if (!smb_fill_cache(filp, dirent, filldir, ctl, 
					    &qname, &fattr))
				;	/* stop reading? */
			entries_seen++;
		}

		VERBOSE("received %d entries, eos=%d\n", ff_searchcount,ff_eos);

		/*
		 * We might need the lastname for continuations.
		 *
		 * Note that some servers (win95?) point to the filename and
		 * others (NT4, Samba using NT1) to the dir entry. We assume
		 * here that those who do not point to a filename do not need
		 * this info to continue the listing.
		 *
		 * OS/2 needs this and talks infolevel 1.
		 * NetApps want lastname with infolevel 260.
		 * win2k want lastname with infolevel 260, and points to
		 *       the record not to the name.
		 * Samba+CifsUnixExt doesn't need lastname.
		 *
		 * Both are happy if we return the data they point to. So we do.
		 * (FIXME: above is not true with win2k)
		 */
		mask_len = 0;
		if (info_level != SMB_FIND_FILE_UNIX &&
		    ff_lastname > 0 && ff_lastname < req->rq_ldata) {
			lastname = req->rq_data + ff_lastname;

			switch (info_level) {
			case 260:
				mask_len = req->rq_ldata - ff_lastname;
				break;
			case 1:
				/* lastname points to a length byte */
				mask_len = *lastname++;
				if (ff_lastname + 1 + mask_len > req->rq_ldata)
					mask_len = req->rq_ldata - ff_lastname - 1;
				break;
			}

			/*
			 * Update the mask string for the next message.
			 */
			if (mask_len > 255)
				mask_len = 255;
			if (mask_len)
				strncpy(mask, lastname, mask_len);
		}
		mask_len = strnlen(mask, mask_len);
		VERBOSE("new mask, len=%d@%d of %d, mask=%.*s\n",
			mask_len, ff_lastname, req->rq_ldata, mask_len, mask);

		first = 0;
		loop_count = 0;
	}

out_free:
	smb_rput(req);
out_name:
	kfree(name_buf);
out:
	unlock_kernel();
	return result;
}

/*
 * This version uses the trans2 TRANSACT2_FINDFIRST message 
 * to get the attribute data.
 *
 * Bugs Noted:
 */
static int
smb_proc_getattr_ff(struct smb_sb_info *server, struct dentry *dentry,
			struct smb_fattr *fattr)
{
	char *param, *mask;
	__u16 date, time;
	int mask_len, result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;
	param = req->rq_buffer;
	mask = param + 12;

	mask_len = smb_encode_path(server, mask, SMB_MAXPATHLEN+1, dentry,NULL);
	if (mask_len < 0) {
		result = mask_len;
		goto out_free;
	}
	VERBOSE("name=%s, len=%d\n", mask, mask_len);
	WSET(param, 0, aSYSTEM | aHIDDEN | aDIR);
	WSET(param, 2, 1);	/* max count */
	WSET(param, 4, 1);	/* close after this call */
	WSET(param, 6, 1);	/* info_level */
	DSET(param, 8, 0);

	req->rq_trans2_command = TRANSACT2_FINDFIRST;
	req->rq_ldata = 0;
	req->rq_data  = NULL;
	req->rq_lparm = 12 + mask_len;
	req->rq_parm  = param;
	req->rq_flags = 0;
	result = smb_add_request(req);
	if (result < 0)
		goto out_free;
	if (req->rq_rcls != 0) {
		result = smb_errno(req);
#ifdef SMBFS_PARANOIA
		if (result != -ENOENT)
			PARANOIA("error for %s, rcls=%d, err=%d\n",
				 mask, req->rq_rcls, req->rq_err);
#endif
		goto out_free;
	}
	/* Make sure we got enough data ... */
	result = -EINVAL;
	if (req->rq_ldata < 22 || WVAL(req->rq_parm, 2) != 1) {
		PARANOIA("bad result for %s, len=%d, count=%d\n",
			 mask, req->rq_ldata, WVAL(req->rq_parm, 2));
		goto out_free;
	}

	/*
	 * Decode the response into the fattr ...
	 */
	date = WVAL(req->rq_data, 0);
	time = WVAL(req->rq_data, 2);
	fattr->f_ctime.tv_sec = date_dos2unix(server, date, time);
	fattr->f_ctime.tv_nsec = 0;

	date = WVAL(req->rq_data, 4);
	time = WVAL(req->rq_data, 6);
	fattr->f_atime.tv_sec = date_dos2unix(server, date, time);
	fattr->f_atime.tv_nsec = 0;

	date = WVAL(req->rq_data, 8);
	time = WVAL(req->rq_data, 10);
	fattr->f_mtime.tv_sec = date_dos2unix(server, date, time);
	fattr->f_mtime.tv_nsec = 0;
	VERBOSE("name=%s, date=%x, time=%x, mtime=%ld\n",
		mask, date, time, fattr->f_mtime.tv_sec);
	fattr->f_size = DVAL(req->rq_data, 12);
	/* ULONG allocation size */
	fattr->attr = WVAL(req->rq_data, 20);
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

static int
smb_proc_getattr_core(struct smb_sb_info *server, struct dentry *dir,
		      struct smb_fattr *fattr)
{
	int result;
	char *p;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

	p = smb_setup_header(req, SMBgetatr, 0, 0);
	result = smb_simple_encode_path(req, &p, dir, NULL);
	if (result < 0)
 		goto out_free;
	smb_setup_bcc(req, p);

	if ((result = smb_request_ok(req, SMBgetatr, 10, 0)) < 0)
		goto out_free;
	fattr->attr    = WVAL(req->rq_header, smb_vwv0);
	fattr->f_mtime.tv_sec = local2utc(server, DVAL(req->rq_header, smb_vwv1));
	fattr->f_mtime.tv_nsec = 0;
	fattr->f_size  = DVAL(req->rq_header, smb_vwv3);
	fattr->f_ctime = fattr->f_mtime; 
	fattr->f_atime = fattr->f_mtime; 
#ifdef SMBFS_DEBUG_TIMESTAMP
	printk("getattr_core: %s/%s, mtime=%ld\n",
	       DENTRY_PATH(dir), fattr->f_mtime);
#endif
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

/*
 * Bugs Noted:
 * (1) Win 95 swaps the date and time fields in the standard info level.
 */
static int
smb_proc_getattr_trans2(struct smb_sb_info *server, struct dentry *dir,
			struct smb_request *req, int infolevel)
{
	char *p, *param;
	int result;

	param = req->rq_buffer;
	WSET(param, 0, infolevel);
	DSET(param, 2, 0);
	result = smb_encode_path(server, param+6, SMB_MAXPATHLEN+1, dir, NULL);
	if (result < 0)
		goto out;
	p = param + 6 + result;

	req->rq_trans2_command = TRANSACT2_QPATHINFO;
	req->rq_ldata = 0;
	req->rq_data  = NULL;
	req->rq_lparm = p - param;
	req->rq_parm  = param;
	req->rq_flags = 0;
	result = smb_add_request(req);
	if (result < 0)
		goto out;
	if (req->rq_rcls != 0) {
		VERBOSE("for %s: result=%d, rcls=%d, err=%d\n",
			&param[6], result, req->rq_rcls, req->rq_err);
		result = smb_errno(req);
		goto out;
	}
	result = -ENOENT;
	if (req->rq_ldata < 22) {
		PARANOIA("not enough data for %s, len=%d\n",
			 &param[6], req->rq_ldata);
		goto out;
	}

	result = 0;
out:
	return result;
}

static int
smb_proc_getattr_trans2_std(struct smb_sb_info *server, struct dentry *dir,
			    struct smb_fattr *attr)
{
	u16 date, time;
	int off_date = 0, off_time = 2;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

	result = smb_proc_getattr_trans2(server, dir, req, SMB_INFO_STANDARD);
	if (result < 0)
		goto out_free;

	/*
	 * Kludge alert: Win 95 swaps the date and time field,
	 * contrary to the CIFS docs and Win NT practice.
	 */
	if (server->mnt->flags & SMB_MOUNT_WIN95) {
		off_date = 2;
		off_time = 0;
	}
	date = WVAL(req->rq_data, off_date);
	time = WVAL(req->rq_data, off_time);
	attr->f_ctime.tv_sec = date_dos2unix(server, date, time);
	attr->f_ctime.tv_nsec = 0;

	date = WVAL(req->rq_data, 4 + off_date);
	time = WVAL(req->rq_data, 4 + off_time);
	attr->f_atime.tv_sec = date_dos2unix(server, date, time);
	attr->f_atime.tv_nsec = 0;

	date = WVAL(req->rq_data, 8 + off_date);
	time = WVAL(req->rq_data, 8 + off_time);
	attr->f_mtime.tv_sec = date_dos2unix(server, date, time);
	attr->f_mtime.tv_nsec = 0;
#ifdef SMBFS_DEBUG_TIMESTAMP
	printk(KERN_DEBUG "getattr_trans2: %s/%s, date=%x, time=%x, mtime=%ld\n",
	       DENTRY_PATH(dir), date, time, attr->f_mtime);
#endif
	attr->f_size = DVAL(req->rq_data, 12);
	attr->attr = WVAL(req->rq_data, 20);

out_free:
	smb_rput(req);
out:
	return result;
}

static int
smb_proc_getattr_trans2_all(struct smb_sb_info *server, struct dentry *dir,
			    struct smb_fattr *attr)
{
	struct smb_request *req;
	int result;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

	result = smb_proc_getattr_trans2(server, dir, req,
					 SMB_QUERY_FILE_ALL_INFO);
	if (result < 0)
		goto out_free;

	attr->f_ctime = smb_ntutc2unixutc(LVAL(req->rq_data, 0));
	attr->f_atime = smb_ntutc2unixutc(LVAL(req->rq_data, 8));
	attr->f_mtime = smb_ntutc2unixutc(LVAL(req->rq_data, 16));
	/* change (24) */
	attr->attr = WVAL(req->rq_data, 32);
	/* pad? (34) */
	/* allocated size (40) */
	attr->f_size = LVAL(req->rq_data, 48);

out_free:
	smb_rput(req);
out:
	return result;
}

static int
smb_proc_getattr_unix(struct smb_sb_info *server, struct dentry *dir,
		      struct smb_fattr *attr)
{
	struct smb_request *req;
	int result;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

	result = smb_proc_getattr_trans2(server, dir, req,
					 SMB_QUERY_FILE_UNIX_BASIC);
	if (result < 0)
		goto out_free;

	smb_decode_unix_basic(attr, server, req->rq_data);

out_free:
	smb_rput(req);
out:
	return result;
}

static int
smb_proc_getattr_95(struct smb_sb_info *server, struct dentry *dir,
		    struct smb_fattr *attr)
{
	struct inode *inode = dir->d_inode;
	int result;

	/* FIXME: why not use the "all" version? */
	result = smb_proc_getattr_trans2_std(server, dir, attr);
	if (result < 0)
		goto out;

	/*
	 * None of the getattr versions here can make win9x return the right
	 * filesize if there are changes made to an open file.
	 * A seek-to-end does return the right size, but we only need to do
	 * that on files we have written.
	 */
	if (inode && SMB_I(inode)->flags & SMB_F_LOCALWRITE &&
	    smb_is_open(inode))
	{
		__u16 fileid = SMB_I(inode)->fileid;
		attr->f_size = smb_proc_seek(server, fileid, 2, 0);
	}

out:
	return result;
}

static int
smb_proc_ops_wait(struct smb_sb_info *server)
{
	int result;

	result = wait_event_interruptible_timeout(server->conn_wq,
				server->conn_complete, 30*HZ);

	if (!result || signal_pending(current))
		return -EIO;

	return 0;
}

static int
smb_proc_getattr_null(struct smb_sb_info *server, struct dentry *dir,
			  struct smb_fattr *fattr)
{
	int result;

	if (smb_proc_ops_wait(server) < 0)
		return -EIO;

	smb_init_dirent(server, fattr);
	result = server->ops->getattr(server, dir, fattr);
	smb_finish_dirent(server, fattr);

	return result;
}

static int
smb_proc_readdir_null(struct file *filp, void *dirent, filldir_t filldir,
		      struct smb_cache_control *ctl)
{
	struct smb_sb_info *server = server_from_dentry(filp->f_path.dentry);

	if (smb_proc_ops_wait(server) < 0)
		return -EIO;

	return server->ops->readdir(filp, dirent, filldir, ctl);
}

int
smb_proc_getattr(struct dentry *dir, struct smb_fattr *fattr)
{
	struct smb_sb_info *server = server_from_dentry(dir);
	int result;

	smb_init_dirent(server, fattr);
	result = server->ops->getattr(server, dir, fattr);
	smb_finish_dirent(server, fattr);

	return result;
}


/*
 * Because of bugs in the core protocol, we use this only to set
 * attributes. See smb_proc_settime() below for timestamp handling.
 *
 * Bugs Noted:
 * (1) If mtime is non-zero, both Win 3.1 and Win 95 fail
 * with an undocumented error (ERRDOS code 50). Setting
 * mtime to 0 allows the attributes to be set.
 * (2) The extra parameters following the name string aren't
 * in the CIFS docs, but seem to be necessary for operation.
 */
static int
smb_proc_setattr_core(struct smb_sb_info *server, struct dentry *dentry,
		      __u16 attr)
{
	char *p;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;

	p = smb_setup_header(req, SMBsetatr, 8, 0);
	WSET(req->rq_header, smb_vwv0, attr);
	DSET(req->rq_header, smb_vwv1, 0); /* mtime */
	WSET(req->rq_header, smb_vwv3, 0); /* reserved values */
	WSET(req->rq_header, smb_vwv4, 0);
	WSET(req->rq_header, smb_vwv5, 0);
	WSET(req->rq_header, smb_vwv6, 0);
	WSET(req->rq_header, smb_vwv7, 0);
	result = smb_simple_encode_path(req, &p, dentry, NULL);
	if (result < 0)
		goto out_free;
	if (p + 2 > (char *)req->rq_buffer + req->rq_bufsize) {
		result = -ENAMETOOLONG;
		goto out_free;
	}
	*p++ = 4;
	*p++ = 0;
	smb_setup_bcc(req, p);

	result = smb_request_ok(req, SMBsetatr, 0, 0);
	if (result < 0)
		goto out_free;
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

/*
 * Because of bugs in the trans2 setattr messages, we must set
 * attributes and timestamps separately. The core SMBsetatr
 * message seems to be the only reliable way to set attributes.
 */
int
smb_proc_setattr(struct dentry *dir, struct smb_fattr *fattr)
{
	struct smb_sb_info *server = server_from_dentry(dir);
	int result;

	VERBOSE("setting %s/%s, open=%d\n", 
		DENTRY_PATH(dir), smb_is_open(dir->d_inode));
	result = smb_proc_setattr_core(server, dir, fattr->attr);
	return result;
}

/*
 * Sets the timestamps for an file open with write permissions.
 */
static int
smb_proc_setattr_ext(struct smb_sb_info *server,
		      struct inode *inode, struct smb_fattr *fattr)
{
	__u16 date, time;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	smb_setup_header(req, SMBsetattrE, 7, 0);
	WSET(req->rq_header, smb_vwv0, SMB_I(inode)->fileid);
	/* We don't change the creation time */
	WSET(req->rq_header, smb_vwv1, 0);
	WSET(req->rq_header, smb_vwv2, 0);
	date_unix2dos(server, fattr->f_atime.tv_sec, &date, &time);
	WSET(req->rq_header, smb_vwv3, date);
	WSET(req->rq_header, smb_vwv4, time);
	date_unix2dos(server, fattr->f_mtime.tv_sec, &date, &time);
	WSET(req->rq_header, smb_vwv5, date);
	WSET(req->rq_header, smb_vwv6, time);
#ifdef SMBFS_DEBUG_TIMESTAMP
	printk(KERN_DEBUG "smb_proc_setattr_ext: date=%d, time=%d, mtime=%ld\n",
	       date, time, fattr->f_mtime);
#endif

	req->rq_flags |= SMB_REQ_NORETRY;
	result = smb_request_ok(req, SMBsetattrE, 0, 0);
	if (result < 0)
		goto out_free;
	result = 0;
out_free:
	smb_rput(req);
out:
	return result;
}

/*
 * Bugs Noted:
 * (1) The TRANSACT2_SETPATHINFO message under Win NT 4.0 doesn't
 * set the file's attribute flags.
 */
static int
smb_proc_setattr_trans2(struct smb_sb_info *server,
			struct dentry *dir, struct smb_fattr *fattr)
{
	__u16 date, time;
	char *p, *param;
	int result;
	char data[26];
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;
	param = req->rq_buffer;

	WSET(param, 0, 1);	/* Info level SMB_INFO_STANDARD */
	DSET(param, 2, 0);
	result = smb_encode_path(server, param+6, SMB_MAXPATHLEN+1, dir, NULL);
	if (result < 0)
		goto out_free;
	p = param + 6 + result;

	WSET(data, 0, 0); /* creation time */
	WSET(data, 2, 0);
	date_unix2dos(server, fattr->f_atime.tv_sec, &date, &time);
	WSET(data, 4, date);
	WSET(data, 6, time);
	date_unix2dos(server, fattr->f_mtime.tv_sec, &date, &time);
	WSET(data, 8, date);
	WSET(data, 10, time);
#ifdef SMBFS_DEBUG_TIMESTAMP
	printk(KERN_DEBUG "setattr_trans2: %s/%s, date=%x, time=%x, mtime=%ld\n", 
	       DENTRY_PATH(dir), date, time, fattr->f_mtime);
#endif
	DSET(data, 12, 0); /* size */
	DSET(data, 16, 0); /* blksize */
	WSET(data, 20, 0); /* attr */
	DSET(data, 22, 0); /* ULONG EA size */

	req->rq_trans2_command = TRANSACT2_SETPATHINFO;
	req->rq_ldata = 26;
	req->rq_data  = data;
	req->rq_lparm = p - param;
	req->rq_parm  = param;
	req->rq_flags = 0;
	result = smb_add_request(req);
	if (result < 0)
		goto out_free;
	result = 0;
	if (req->rq_rcls != 0)
		result = smb_errno(req);

out_free:
	smb_rput(req);
out:
	return result;
}

/*
 * ATTR_MODE      0x001
 * ATTR_UID       0x002
 * ATTR_GID       0x004
 * ATTR_SIZE      0x008
 * ATTR_ATIME     0x010
 * ATTR_MTIME     0x020
 * ATTR_CTIME     0x040
 * ATTR_ATIME_SET 0x080
 * ATTR_MTIME_SET 0x100
 * ATTR_FORCE     0x200	
 * ATTR_ATTR_FLAG 0x400
 *
 * major/minor should only be set by mknod.
 */
int
smb_proc_setattr_unix(struct dentry *d, struct iattr *attr,
		      unsigned int major, unsigned int minor)
{
	struct smb_sb_info *server = server_from_dentry(d);
	u64 nttime;
	char *p, *param;
	int result;
	char data[100];
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;
	param = req->rq_buffer;

	DEBUG1("valid flags = 0x%04x\n", attr->ia_valid);

	WSET(param, 0, SMB_SET_FILE_UNIX_BASIC);
	DSET(param, 2, 0);
	result = smb_encode_path(server, param+6, SMB_MAXPATHLEN+1, d, NULL);
	if (result < 0)
		goto out_free;
	p = param + 6 + result;

	/* 0 L file size in bytes */
	/* 8 L file size on disk in bytes (block count) */
	/* 40 L uid */
	/* 48 L gid */
	/* 56 W file type enum */
	/* 60 L devmajor */
	/* 68 L devminor */
	/* 76 L unique ID (inode) */
	/* 84 L permissions */
	/* 92 L link count */
	LSET(data, 0, SMB_SIZE_NO_CHANGE);
	LSET(data, 8, SMB_SIZE_NO_CHANGE);
	LSET(data, 16, SMB_TIME_NO_CHANGE);
	LSET(data, 24, SMB_TIME_NO_CHANGE);
	LSET(data, 32, SMB_TIME_NO_CHANGE);
	LSET(data, 40, SMB_UID_NO_CHANGE);
	LSET(data, 48, SMB_GID_NO_CHANGE);
	DSET(data, 56, smb_filetype_from_mode(attr->ia_mode));
	LSET(data, 60, major);
	LSET(data, 68, minor);
	LSET(data, 76, 0);
	LSET(data, 84, SMB_MODE_NO_CHANGE);
	LSET(data, 92, 0);

	if (attr->ia_valid & ATTR_SIZE) {
		LSET(data, 0, attr->ia_size);
		LSET(data, 8, 0); /* can't set anyway */
	}

	/*
	 * FIXME: check the conversion function it the correct one
	 *
	 * we can't set ctime but we might as well pass this to the server
	 * and let it ignore it.
	 */
	if (attr->ia_valid & ATTR_CTIME) {
		nttime = smb_unixutc2ntutc(attr->ia_ctime);
		LSET(data, 16, nttime);
	}
	if (attr->ia_valid & ATTR_ATIME) {
		nttime = smb_unixutc2ntutc(attr->ia_atime);
		LSET(data, 24, nttime);
	}
	if (attr->ia_valid & ATTR_MTIME) {
		nttime = smb_unixutc2ntutc(attr->ia_mtime);
		LSET(data, 32, nttime);
	}
	
	if (attr->ia_valid & ATTR_UID) {
		LSET(data, 40, attr->ia_uid);
	}
	if (attr->ia_valid & ATTR_GID) {
		LSET(data, 48, attr->ia_gid); 
	}
	
	if (attr->ia_valid & ATTR_MODE) {
		LSET(data, 84, attr->ia_mode);
	}

	req->rq_trans2_command = TRANSACT2_SETPATHINFO;
	req->rq_ldata = 100;
	req->rq_data  = data;
	req->rq_lparm = p - param;
	req->rq_parm  = param;
	req->rq_flags = 0;
	result = smb_add_request(req);

out_free:
	smb_rput(req);
out:
	return result;
}


/*
 * Set the modify and access timestamps for a file.
 *
 * Incredibly enough, in all of SMB there is no message to allow
 * setting both attributes and timestamps at once. 
 *
 * Bugs Noted:
 * (1) Win 95 doesn't support the TRANSACT2_SETFILEINFO message 
 * with info level 1 (INFO_STANDARD).
 * (2) Win 95 seems not to support setting directory timestamps.
 * (3) Under the core protocol apparently the only way to set the
 * timestamp is to open and close the file.
 */
int
smb_proc_settime(struct dentry *dentry, struct smb_fattr *fattr)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	struct inode *inode = dentry->d_inode;
	int result;

	VERBOSE("setting %s/%s, open=%d\n",
		DENTRY_PATH(dentry), smb_is_open(inode));

	/* setting the time on a Win95 server fails (tridge) */
	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2 && 
	    !(server->mnt->flags & SMB_MOUNT_WIN95)) {
		if (smb_is_open(inode) && SMB_I(inode)->access != SMB_O_RDONLY)
			result = smb_proc_setattr_ext(server, inode, fattr);
		else
			result = smb_proc_setattr_trans2(server, dentry, fattr);
	} else {
		/*
		 * Fail silently on directories ... timestamp can't be set?
		 */
		result = 0;
		if (S_ISREG(inode->i_mode)) {
			/*
			 * Set the mtime by opening and closing the file.
			 * Note that the file is opened read-only, but this
			 * still allows us to set the date (tridge)
			 */
			result = -EACCES;
			if (!smb_is_open(inode))
				smb_proc_open(server, dentry, SMB_O_RDONLY);
			if (smb_is_open(inode)) {
				inode->i_mtime = fattr->f_mtime;
				result = smb_proc_close_inode(server, inode);
			}
		}
	}

	return result;
}

int
smb_proc_dskattr(struct dentry *dentry, struct kstatfs *attr)
{
	struct smb_sb_info *server = SMB_SB(dentry->d_sb);
	int result;
	char *p;
	long unit;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 0)))
		goto out;

	smb_setup_header(req, SMBdskattr, 0, 0);
	if ((result = smb_request_ok(req, SMBdskattr, 5, 0)) < 0)
		goto out_free;
	p = SMB_VWV(req->rq_header);
	unit = (WVAL(p, 2) * WVAL(p, 4)) >> SMB_ST_BLKSHIFT;
	attr->f_blocks = WVAL(p, 0) * unit;
	attr->f_bsize  = SMB_ST_BLKSIZE;
	attr->f_bavail = attr->f_bfree = WVAL(p, 6) * unit;
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

int
smb_proc_read_link(struct smb_sb_info *server, struct dentry *d,
		   char *buffer, int len)
{
	char *p, *param;
	int result;
	struct smb_request *req;

	DEBUG1("readlink of %s/%s\n", DENTRY_PATH(d));

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;
	param = req->rq_buffer;

	WSET(param, 0, SMB_QUERY_FILE_UNIX_LINK);
	DSET(param, 2, 0);
	result = smb_encode_path(server, param+6, SMB_MAXPATHLEN+1, d, NULL);
	if (result < 0)
		goto out_free;
	p = param + 6 + result;

	req->rq_trans2_command = TRANSACT2_QPATHINFO;
	req->rq_ldata = 0;
	req->rq_data  = NULL;
	req->rq_lparm = p - param;
	req->rq_parm  = param;
	req->rq_flags = 0;
	result = smb_add_request(req);
	if (result < 0)
		goto out_free;
	DEBUG1("for %s: result=%d, rcls=%d, err=%d\n",
		&param[6], result, req->rq_rcls, req->rq_err);

	/* copy data up to the \0 or buffer length */
	result = len;
	if (req->rq_ldata < len)
		result = req->rq_ldata;
	strncpy(buffer, req->rq_data, result);

out_free:
	smb_rput(req);
out:
	return result;
}


/*
 * Create a symlink object called dentry which points to oldpath.
 * Samba does not permit dangling links but returns a suitable error message.
 */
int
smb_proc_symlink(struct smb_sb_info *server, struct dentry *d,
		 const char *oldpath)
{
	char *p, *param;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;
	param = req->rq_buffer;

	WSET(param, 0, SMB_SET_FILE_UNIX_LINK);
	DSET(param, 2, 0);
	result = smb_encode_path(server, param + 6, SMB_MAXPATHLEN+1, d, NULL);
	if (result < 0)
		goto out_free;
	p = param + 6 + result;

	req->rq_trans2_command = TRANSACT2_SETPATHINFO;
	req->rq_ldata = strlen(oldpath) + 1;
	req->rq_data  = (char *) oldpath;
	req->rq_lparm = p - param;
	req->rq_parm  = param;
	req->rq_flags = 0;
	result = smb_add_request(req);
	if (result < 0)
		goto out_free;

	DEBUG1("for %s: result=%d, rcls=%d, err=%d\n",
		&param[6], result, req->rq_rcls, req->rq_err);
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

/*
 * Create a hard link object called new_dentry which points to dentry.
 */
int
smb_proc_link(struct smb_sb_info *server, struct dentry *dentry,
	      struct dentry *new_dentry)
{
	char *p, *param;
	int result;
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, PAGE_SIZE)))
		goto out;
	param = req->rq_buffer;

	WSET(param, 0, SMB_SET_FILE_UNIX_HLINK);
	DSET(param, 2, 0);
	result = smb_encode_path(server, param + 6, SMB_MAXPATHLEN+1,
				 new_dentry, NULL);
	if (result < 0)
		goto out_free;
	p = param + 6 + result;

	/* Grr, pointless separation of parameters and data ... */
	req->rq_data = p;
	req->rq_ldata = smb_encode_path(server, p, SMB_MAXPATHLEN+1,
					dentry, NULL);

	req->rq_trans2_command = TRANSACT2_SETPATHINFO;
	req->rq_lparm = p - param;
	req->rq_parm  = param;
	req->rq_flags = 0;
	result = smb_add_request(req);
	if (result < 0)
		goto out_free;

	DEBUG1("for %s: result=%d, rcls=%d, err=%d\n",
	       &param[6], result, req->rq_rcls, req->rq_err);
	result = 0;

out_free:
	smb_rput(req);
out:
	return result;
}

static int
smb_proc_query_cifsunix(struct smb_sb_info *server)
{
	int result;
	int major, minor;
	u64 caps;
	char param[2];
	struct smb_request *req;

	result = -ENOMEM;
	if (! (req = smb_alloc_request(server, 100)))
		goto out;

	WSET(param, 0, SMB_QUERY_CIFS_UNIX_INFO);

	req->rq_trans2_command = TRANSACT2_QFSINFO;
	req->rq_ldata = 0;
	req->rq_data  = NULL;
	req->rq_lparm = 2;
	req->rq_parm  = param;
	req->rq_flags = 0;
	result = smb_add_request(req);
	if (result < 0)
		goto out_free;

	if (req->rq_ldata < 12) {
		PARANOIA("Not enough data\n");
		goto out_free;
	}
	major = WVAL(req->rq_data, 0);
	minor = WVAL(req->rq_data, 2);

	DEBUG1("Server implements CIFS Extensions for UNIX systems v%d.%d\n",
	       major, minor);
	/* FIXME: verify that we are ok with this major/minor? */

	caps = LVAL(req->rq_data, 4);
	DEBUG1("Server capabilities 0x%016llx\n", caps);

out_free:
	smb_rput(req);
out:
	return result;
}


static void
install_ops(struct smb_ops *dst, struct smb_ops *src)
{
	memcpy(dst, src, sizeof(void *) * SMB_OPS_NUM_STATIC);
}

/* < LANMAN2 */
static struct smb_ops smb_ops_core =
{
	.read		= smb_proc_read,
	.write		= smb_proc_write,
	.readdir	= smb_proc_readdir_short,
	.getattr	= smb_proc_getattr_core,
	.truncate	= smb_proc_trunc32,
};

/* LANMAN2, OS/2, others? */
static struct smb_ops smb_ops_os2 =
{
	.read		= smb_proc_read,
	.write		= smb_proc_write,
	.readdir	= smb_proc_readdir_long,
	.getattr	= smb_proc_getattr_trans2_std,
	.truncate	= smb_proc_trunc32,
};

/* Win95, and possibly some NetApp versions too */
static struct smb_ops smb_ops_win95 =
{
	.read		= smb_proc_read,    /* does not support 12word readX */
	.write		= smb_proc_write,
	.readdir	= smb_proc_readdir_long,
	.getattr	= smb_proc_getattr_95,
	.truncate	= smb_proc_trunc95,
};

/* Samba, NT4 and NT5 */
static struct smb_ops smb_ops_winNT =
{
	.read		= smb_proc_readX,
	.write		= smb_proc_writeX,
	.readdir	= smb_proc_readdir_long,
	.getattr	= smb_proc_getattr_trans2_all,
	.truncate	= smb_proc_trunc64,
};

/* Samba w/ unix extensions. Others? */
static struct smb_ops smb_ops_unix =
{
	.read		= smb_proc_readX,
	.write		= smb_proc_writeX,
	.readdir	= smb_proc_readdir_long,
	.getattr	= smb_proc_getattr_unix,
	/* FIXME: core/ext/time setattr needs to be cleaned up! */
	/* .setattr	= smb_proc_setattr_unix, */
	.truncate	= smb_proc_trunc64,
};

/* Place holder until real ops are in place */
static struct smb_ops smb_ops_null =
{
	.readdir	= smb_proc_readdir_null,
	.getattr	= smb_proc_getattr_null,
};

void smb_install_null_ops(struct smb_ops *ops)
{
	install_ops(ops, &smb_ops_null);
}
