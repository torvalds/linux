// SPDX-License-Identifier: GPL-2.0-only
/*
 * Syscall interface to knfsd.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/ctype.h>
#include <linux/fs_context.h>

#include <linux/sunrpc/svcsock.h>
#include <linux/lockd/lockd.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/sunrpc/svc.h>
#include <linux/module.h>
#include <linux/fsnotify.h>
#include <linux/nfslocalio.h>

#include "idmap.h"
#include "nfsd.h"
#include "cache.h"
#include "state.h"
#include "netns.h"
#include "pnfs.h"
#include "filecache.h"
#include "trace.h"
#include "netlink.h"

/*
 *	We have a single directory with several nodes in it.
 */
enum {
	NFSD_Root = 1,
	NFSD_List,
	NFSD_Export_Stats,
	NFSD_Export_features,
	NFSD_Fh,
	NFSD_FO_UnlockIP,
	NFSD_FO_UnlockFS,
	NFSD_Threads,
	NFSD_Pool_Threads,
	NFSD_Pool_Stats,
	NFSD_Reply_Cache_Stats,
	NFSD_Versions,
	NFSD_Ports,
	NFSD_MaxBlkSize,
	NFSD_Filecache,
	NFSD_Leasetime,
	NFSD_Gracetime,
	NFSD_RecoveryDir,
	NFSD_V4EndGrace,
	NFSD_MaxReserved
};

/*
 * write() for these nodes.
 */
static ssize_t write_filehandle(struct file *file, char *buf, size_t size);
static ssize_t write_unlock_ip(struct file *file, char *buf, size_t size);
static ssize_t write_unlock_fs(struct file *file, char *buf, size_t size);
static ssize_t write_threads(struct file *file, char *buf, size_t size);
static ssize_t write_pool_threads(struct file *file, char *buf, size_t size);
static ssize_t write_versions(struct file *file, char *buf, size_t size);
static ssize_t write_ports(struct file *file, char *buf, size_t size);
static ssize_t write_maxblksize(struct file *file, char *buf, size_t size);
#ifdef CONFIG_NFSD_V4
static ssize_t write_leasetime(struct file *file, char *buf, size_t size);
static ssize_t write_gracetime(struct file *file, char *buf, size_t size);
#ifdef CONFIG_NFSD_LEGACY_CLIENT_TRACKING
static ssize_t write_recoverydir(struct file *file, char *buf, size_t size);
#endif
static ssize_t write_v4_end_grace(struct file *file, char *buf, size_t size);
#endif

static ssize_t (*const write_op[])(struct file *, char *, size_t) = {
	[NFSD_Fh] = write_filehandle,
	[NFSD_FO_UnlockIP] = write_unlock_ip,
	[NFSD_FO_UnlockFS] = write_unlock_fs,
	[NFSD_Threads] = write_threads,
	[NFSD_Pool_Threads] = write_pool_threads,
	[NFSD_Versions] = write_versions,
	[NFSD_Ports] = write_ports,
	[NFSD_MaxBlkSize] = write_maxblksize,
#ifdef CONFIG_NFSD_V4
	[NFSD_Leasetime] = write_leasetime,
	[NFSD_Gracetime] = write_gracetime,
#ifdef CONFIG_NFSD_LEGACY_CLIENT_TRACKING
	[NFSD_RecoveryDir] = write_recoverydir,
#endif
	[NFSD_V4EndGrace] = write_v4_end_grace,
#endif
};

static ssize_t nfsctl_transaction_write(struct file *file, const char __user *buf, size_t size, loff_t *pos)
{
	ino_t ino =  file_inode(file)->i_ino;
	char *data;
	ssize_t rv;

	if (ino >= ARRAY_SIZE(write_op) || !write_op[ino])
		return -EINVAL;

	data = simple_transaction_get(file, buf, size);
	if (IS_ERR(data))
		return PTR_ERR(data);

	rv = write_op[ino](file, data, size);
	if (rv < 0)
		return rv;

	simple_transaction_set(file, rv);
	return size;
}

static ssize_t nfsctl_transaction_read(struct file *file, char __user *buf, size_t size, loff_t *pos)
{
	if (! file->private_data) {
		/* An attempt to read a transaction file without writing
		 * causes a 0-byte write so that the file can return
		 * state information
		 */
		ssize_t rv = nfsctl_transaction_write(file, buf, 0, pos);
		if (rv < 0)
			return rv;
	}
	return simple_transaction_read(file, buf, size, pos);
}

static const struct file_operations transaction_ops = {
	.write		= nfsctl_transaction_write,
	.read		= nfsctl_transaction_read,
	.release	= simple_transaction_release,
	.llseek		= default_llseek,
};

static int exports_net_open(struct net *net, struct file *file)
{
	int err;
	struct seq_file *seq;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	err = seq_open(file, &nfs_exports_op);
	if (err)
		return err;

	seq = file->private_data;
	seq->private = nn->svc_export_cache;
	return 0;
}

static int exports_nfsd_open(struct inode *inode, struct file *file)
{
	return exports_net_open(inode->i_sb->s_fs_info, file);
}

static const struct file_operations exports_nfsd_operations = {
	.open		= exports_nfsd_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int export_features_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%x 0x%x\n", NFSEXP_ALLFLAGS, NFSEXP_SECINFO_FLAGS);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(export_features);

static int nfsd_pool_stats_open(struct inode *inode, struct file *file)
{
	struct nfsd_net *nn = net_generic(inode->i_sb->s_fs_info, nfsd_net_id);

	return svc_pool_stats_open(&nn->nfsd_info, file);
}

static const struct file_operations pool_stats_operations = {
	.open		= nfsd_pool_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

DEFINE_SHOW_ATTRIBUTE(nfsd_reply_cache_stats);

DEFINE_SHOW_ATTRIBUTE(nfsd_file_cache_stats);

/*----------------------------------------------------------------------------*/
/*
 * payload - write methods
 */

static inline struct net *netns(struct file *file)
{
	return file_inode(file)->i_sb->s_fs_info;
}

/*
 * write_unlock_ip - Release all locks used by a client
 *
 * Experimental.
 *
 * Input:
 *			buf:	'\n'-terminated C string containing a
 *				presentation format IP address
 *			size:	length of C string in @buf
 * Output:
 *	On success:	returns zero if all specified locks were released;
 *			returns one if one or more locks were not released
 *	On error:	return code is negative errno value
 */
static ssize_t write_unlock_ip(struct file *file, char *buf, size_t size)
{
	struct sockaddr_storage address;
	struct sockaddr *sap = (struct sockaddr *)&address;
	size_t salen = sizeof(address);
	char *fo_path;
	struct net *net = netns(file);

	/* sanity check */
	if (size == 0)
		return -EINVAL;

	if (buf[size-1] != '\n')
		return -EINVAL;

	fo_path = buf;
	if (qword_get(&buf, fo_path, size) < 0)
		return -EINVAL;

	if (rpc_pton(net, fo_path, size, sap, salen) == 0)
		return -EINVAL;

	trace_nfsd_ctl_unlock_ip(net, buf);
	return nlmsvc_unlock_all_by_ip(sap);
}

/*
 * write_unlock_fs - Release all locks on a local file system
 *
 * Experimental.
 *
 * Input:
 *			buf:	'\n'-terminated C string containing the
 *				absolute pathname of a local file system
 *			size:	length of C string in @buf
 * Output:
 *	On success:	returns zero if all specified locks were released;
 *			returns one if one or more locks were not released
 *	On error:	return code is negative errno value
 */
static ssize_t write_unlock_fs(struct file *file, char *buf, size_t size)
{
	struct path path;
	char *fo_path;
	int error;

	/* sanity check */
	if (size == 0)
		return -EINVAL;

	if (buf[size-1] != '\n')
		return -EINVAL;

	fo_path = buf;
	if (qword_get(&buf, fo_path, size) < 0)
		return -EINVAL;
	trace_nfsd_ctl_unlock_fs(netns(file), fo_path);
	error = kern_path(fo_path, 0, &path);
	if (error)
		return error;

	/*
	 * XXX: Needs better sanity checking.  Otherwise we could end up
	 * releasing locks on the wrong file system.
	 *
	 * For example:
	 * 1.  Does the path refer to a directory?
	 * 2.  Is that directory a mount point, or
	 * 3.  Is that directory the root of an exported file system?
	 */
	error = nlmsvc_unlock_all_by_sb(path.dentry->d_sb);
	nfsd4_revoke_states(netns(file), path.dentry->d_sb);

	path_put(&path);
	return error;
}

/*
 * write_filehandle - Get a variable-length NFS file handle by path
 *
 * On input, the buffer contains a '\n'-terminated C string comprised of
 * three alphanumeric words separated by whitespace.  The string may
 * contain escape sequences.
 *
 * Input:
 *			buf:
 *				domain:		client domain name
 *				path:		export pathname
 *				maxsize:	numeric maximum size of
 *						@buf
 *			size:	length of C string in @buf
 * Output:
 *	On success:	passed-in buffer filled with '\n'-terminated C
 *			string containing a ASCII hex text version
 *			of the NFS file handle;
 *			return code is the size in bytes of the string
 *	On error:	return code is negative errno value
 */
static ssize_t write_filehandle(struct file *file, char *buf, size_t size)
{
	char *dname, *path;
	int maxsize;
	char *mesg = buf;
	int len;
	struct auth_domain *dom;
	struct knfsd_fh fh;

	if (size == 0)
		return -EINVAL;

	if (buf[size-1] != '\n')
		return -EINVAL;
	buf[size-1] = 0;

	dname = mesg;
	len = qword_get(&mesg, dname, size);
	if (len <= 0)
		return -EINVAL;

	path = dname+len+1;
	len = qword_get(&mesg, path, size);
	if (len <= 0)
		return -EINVAL;

	len = get_int(&mesg, &maxsize);
	if (len)
		return len;

	if (maxsize < NFS_FHSIZE)
		return -EINVAL;
	maxsize = min(maxsize, NFS3_FHSIZE);

	if (qword_get(&mesg, mesg, size) > 0)
		return -EINVAL;

	trace_nfsd_ctl_filehandle(netns(file), dname, path, maxsize);

	/* we have all the words, they are in buf.. */
	dom = unix_domain_find(dname);
	if (!dom)
		return -ENOMEM;

	len = exp_rootfh(netns(file), dom, path, &fh, maxsize);
	auth_domain_put(dom);
	if (len)
		return len;

	mesg = buf;
	len = SIMPLE_TRANSACTION_LIMIT;
	qword_addhex(&mesg, &len, fh.fh_raw, fh.fh_size);
	mesg[-1] = '\n';
	return mesg - buf;
}

/*
 * write_threads - Start NFSD, or report the current number of running threads
 *
 * Input:
 *			buf:		ignored
 *			size:		zero
 * Output:
 *	On success:	passed-in buffer filled with '\n'-terminated C
 *			string numeric value representing the number of
 *			running NFSD threads;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero
 *
 * OR
 *
 * Input:
 *			buf:		C string containing an unsigned
 *					integer value representing the
 *					number of NFSD threads to start
 *			size:		non-zero length of C string in @buf
 * Output:
 *	On success:	NFS service is started;
 *			passed-in buffer filled with '\n'-terminated C
 *			string numeric value representing the number of
 *			running NFSD threads;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero or a negative errno value
 */
static ssize_t write_threads(struct file *file, char *buf, size_t size)
{
	char *mesg = buf;
	int rv;
	struct net *net = netns(file);

	if (size > 0) {
		int newthreads;
		rv = get_int(&mesg, &newthreads);
		if (rv)
			return rv;
		if (newthreads < 0)
			return -EINVAL;
		trace_nfsd_ctl_threads(net, newthreads);
		mutex_lock(&nfsd_mutex);
		rv = nfsd_svc(1, &newthreads, net, file->f_cred, NULL);
		mutex_unlock(&nfsd_mutex);
		if (rv < 0)
			return rv;
	} else
		rv = nfsd_nrthreads(net);

	return scnprintf(buf, SIMPLE_TRANSACTION_LIMIT, "%d\n", rv);
}

/*
 * write_pool_threads - Set or report the current number of threads per pool
 *
 * Input:
 *			buf:		ignored
 *			size:		zero
 *
 * OR
 *
 * Input:
 *			buf:		C string containing whitespace-
 *					separated unsigned integer values
 *					representing the number of NFSD
 *					threads to start in each pool
 *			size:		non-zero length of C string in @buf
 * Output:
 *	On success:	passed-in buffer filled with '\n'-terminated C
 *			string containing integer values representing the
 *			number of NFSD threads in each pool;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero or a negative errno value
 */
static ssize_t write_pool_threads(struct file *file, char *buf, size_t size)
{
	/* if size > 0, look for an array of number of threads per node
	 * and apply them  then write out number of threads per node as reply
	 */
	char *mesg = buf;
	int i;
	int rv;
	int len;
	int npools;
	int *nthreads;
	struct net *net = netns(file);

	mutex_lock(&nfsd_mutex);
	npools = nfsd_nrpools(net);
	if (npools == 0) {
		/*
		 * NFS is shut down.  The admin can start it by
		 * writing to the threads file but NOT the pool_threads
		 * file, sorry.  Report zero threads.
		 */
		mutex_unlock(&nfsd_mutex);
		strcpy(buf, "0\n");
		return strlen(buf);
	}

	nthreads = kcalloc(npools, sizeof(int), GFP_KERNEL);
	rv = -ENOMEM;
	if (nthreads == NULL)
		goto out_free;

	if (size > 0) {
		for (i = 0; i < npools; i++) {
			rv = get_int(&mesg, &nthreads[i]);
			if (rv == -ENOENT)
				break;		/* fewer numbers than pools */
			if (rv)
				goto out_free;	/* syntax error */
			rv = -EINVAL;
			if (nthreads[i] < 0)
				goto out_free;
			trace_nfsd_ctl_pool_threads(net, i, nthreads[i]);
		}

		/*
		 * There must always be a thread in pool 0; the admin
		 * can't shut down NFS completely using pool_threads.
		 */
		if (nthreads[0] == 0)
			nthreads[0] = 1;

		rv = nfsd_set_nrthreads(i, nthreads, net);
		if (rv)
			goto out_free;
	}

	rv = nfsd_get_nrthreads(npools, nthreads, net);
	if (rv)
		goto out_free;

	mesg = buf;
	size = SIMPLE_TRANSACTION_LIMIT;
	for (i = 0; i < npools && size > 0; i++) {
		snprintf(mesg, size, "%d%c", nthreads[i], (i == npools-1 ? '\n' : ' '));
		len = strlen(mesg);
		size -= len;
		mesg += len;
	}
	rv = mesg - buf;
out_free:
	kfree(nthreads);
	mutex_unlock(&nfsd_mutex);
	return rv;
}

static ssize_t
nfsd_print_version_support(struct nfsd_net *nn, char *buf, int remaining,
		const char *sep, unsigned vers, int minor)
{
	const char *format = minor < 0 ? "%s%c%u" : "%s%c%u.%u";
	bool supported = !!nfsd_vers(nn, vers, NFSD_TEST);

	if (vers == 4 && minor >= 0 &&
	    !nfsd_minorversion(nn, minor, NFSD_TEST))
		supported = false;
	if (minor == 0 && supported)
		/*
		 * special case for backward compatability.
		 * +4.0 is never reported, it is implied by
		 * +4, unless -4.0 is present.
		 */
		return 0;
	return snprintf(buf, remaining, format, sep,
			supported ? '+' : '-', vers, minor);
}

static ssize_t __write_versions(struct file *file, char *buf, size_t size)
{
	char *mesg = buf;
	char *vers, *minorp, sign;
	int len, num, remaining;
	ssize_t tlen = 0;
	char *sep;
	struct nfsd_net *nn = net_generic(netns(file), nfsd_net_id);

	if (size > 0) {
		if (nn->nfsd_serv)
			/* Cannot change versions without updating
			 * nn->nfsd_serv->sv_xdrsize, and reallocing
			 * rq_argp and rq_resp
			 */
			return -EBUSY;
		if (buf[size-1] != '\n')
			return -EINVAL;
		buf[size-1] = 0;
		trace_nfsd_ctl_version(netns(file), buf);

		vers = mesg;
		len = qword_get(&mesg, vers, size);
		if (len <= 0) return -EINVAL;
		do {
			enum vers_op cmd;
			unsigned minor;
			sign = *vers;
			if (sign == '+' || sign == '-')
				num = simple_strtol((vers+1), &minorp, 0);
			else
				num = simple_strtol(vers, &minorp, 0);
			if (*minorp == '.') {
				if (num != 4)
					return -EINVAL;
				if (kstrtouint(minorp+1, 0, &minor) < 0)
					return -EINVAL;
			}

			cmd = sign == '-' ? NFSD_CLEAR : NFSD_SET;
			switch(num) {
#ifdef CONFIG_NFSD_V2
			case 2:
#endif
			case 3:
				nfsd_vers(nn, num, cmd);
				break;
			case 4:
				if (*minorp == '.') {
					if (nfsd_minorversion(nn, minor, cmd) < 0)
						return -EINVAL;
				} else if ((cmd == NFSD_SET) != nfsd_vers(nn, num, NFSD_TEST)) {
					/*
					 * Either we have +4 and no minors are enabled,
					 * or we have -4 and at least one minor is enabled.
					 * In either case, propagate 'cmd' to all minors.
					 */
					minor = 0;
					while (nfsd_minorversion(nn, minor, cmd) >= 0)
						minor++;
				}
				break;
			default:
				/* Ignore requests to disable non-existent versions */
				if (cmd == NFSD_SET)
					return -EINVAL;
			}
			vers += len + 1;
		} while ((len = qword_get(&mesg, vers, size)) > 0);
		/* If all get turned off, turn them back on, as
		 * having no versions is BAD
		 */
		nfsd_reset_versions(nn);
	}

	/* Now write current state into reply buffer */
	sep = "";
	remaining = SIMPLE_TRANSACTION_LIMIT;
	for (num=2 ; num <= 4 ; num++) {
		int minor;
		if (!nfsd_vers(nn, num, NFSD_AVAIL))
			continue;

		minor = -1;
		do {
			len = nfsd_print_version_support(nn, buf, remaining,
					sep, num, minor);
			if (len >= remaining)
				goto out;
			remaining -= len;
			buf += len;
			tlen += len;
			minor++;
			if (len)
				sep = " ";
		} while (num == 4 && minor <= NFSD_SUPPORTED_MINOR_VERSION);
	}
out:
	len = snprintf(buf, remaining, "\n");
	if (len >= remaining)
		return -EINVAL;
	return tlen + len;
}

/*
 * write_versions - Set or report the available NFS protocol versions
 *
 * Input:
 *			buf:		ignored
 *			size:		zero
 * Output:
 *	On success:	passed-in buffer filled with '\n'-terminated C
 *			string containing positive or negative integer
 *			values representing the current status of each
 *			protocol version;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero or a negative errno value
 *
 * OR
 *
 * Input:
 *			buf:		C string containing whitespace-
 *					separated positive or negative
 *					integer values representing NFS
 *					protocol versions to enable ("+n")
 *					or disable ("-n")
 *			size:		non-zero length of C string in @buf
 * Output:
 *	On success:	status of zero or more protocol versions has
 *			been updated; passed-in buffer filled with
 *			'\n'-terminated C string containing positive
 *			or negative integer values representing the
 *			current status of each protocol version;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero or a negative errno value
 */
static ssize_t write_versions(struct file *file, char *buf, size_t size)
{
	ssize_t rv;

	mutex_lock(&nfsd_mutex);
	rv = __write_versions(file, buf, size);
	mutex_unlock(&nfsd_mutex);
	return rv;
}

/*
 * Zero-length write.  Return a list of NFSD's current listener
 * transports.
 */
static ssize_t __write_ports_names(char *buf, struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	if (nn->nfsd_serv == NULL)
		return 0;
	return svc_xprt_names(nn->nfsd_serv, buf, SIMPLE_TRANSACTION_LIMIT);
}

/*
 * A single 'fd' number was written, in which case it must be for
 * a socket of a supported family/protocol, and we use it as an
 * nfsd listener.
 */
static ssize_t __write_ports_addfd(char *buf, struct net *net, const struct cred *cred)
{
	char *mesg = buf;
	int fd, err;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct svc_serv *serv;

	err = get_int(&mesg, &fd);
	if (err != 0 || fd < 0)
		return -EINVAL;
	trace_nfsd_ctl_ports_addfd(net, fd);

	err = nfsd_create_serv(net);
	if (err != 0)
		return err;

	serv = nn->nfsd_serv;
	err = svc_addsock(serv, net, fd, buf, SIMPLE_TRANSACTION_LIMIT, cred);

	if (!serv->sv_nrthreads && list_empty(&nn->nfsd_serv->sv_permsocks))
		nfsd_destroy_serv(net);

	return err;
}

/*
 * A transport listener is added by writing its transport name and
 * a port number.
 */
static ssize_t __write_ports_addxprt(char *buf, struct net *net, const struct cred *cred)
{
	char transport[16];
	struct svc_xprt *xprt;
	int port, err;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct svc_serv *serv;

	if (sscanf(buf, "%15s %5u", transport, &port) != 2)
		return -EINVAL;

	if (port < 1 || port > USHRT_MAX)
		return -EINVAL;
	trace_nfsd_ctl_ports_addxprt(net, transport, port);

	err = nfsd_create_serv(net);
	if (err != 0)
		return err;

	serv = nn->nfsd_serv;
	err = svc_xprt_create(serv, transport, net,
			      PF_INET, port, SVC_SOCK_ANONYMOUS, cred);
	if (err < 0)
		goto out_err;

	err = svc_xprt_create(serv, transport, net,
			      PF_INET6, port, SVC_SOCK_ANONYMOUS, cred);
	if (err < 0 && err != -EAFNOSUPPORT)
		goto out_close;

	return 0;
out_close:
	xprt = svc_find_xprt(serv, transport, net, PF_INET, port);
	if (xprt != NULL) {
		svc_xprt_close(xprt);
		svc_xprt_put(xprt);
	}
out_err:
	if (!serv->sv_nrthreads && list_empty(&nn->nfsd_serv->sv_permsocks))
		nfsd_destroy_serv(net);

	return err;
}

static ssize_t __write_ports(struct file *file, char *buf, size_t size,
			     struct net *net)
{
	if (size == 0)
		return __write_ports_names(buf, net);

	if (isdigit(buf[0]))
		return __write_ports_addfd(buf, net, file->f_cred);

	if (isalpha(buf[0]))
		return __write_ports_addxprt(buf, net, file->f_cred);

	return -EINVAL;
}

/*
 * write_ports - Pass a socket file descriptor or transport name to listen on
 *
 * Input:
 *			buf:		ignored
 *			size:		zero
 * Output:
 *	On success:	passed-in buffer filled with a '\n'-terminated C
 *			string containing a whitespace-separated list of
 *			named NFSD listeners;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero or a negative errno value
 *
 * OR
 *
 * Input:
 *			buf:		C string containing an unsigned
 *					integer value representing a bound
 *					but unconnected socket that is to be
 *					used as an NFSD listener; listen(3)
 *					must be called for a SOCK_STREAM
 *					socket, otherwise it is ignored
 *			size:		non-zero length of C string in @buf
 * Output:
 *	On success:	NFS service is started;
 *			passed-in buffer filled with a '\n'-terminated C
 *			string containing a unique alphanumeric name of
 *			the listener;
 *			return code is the size in bytes of the string
 *	On error:	return code is a negative errno value
 *
 * OR
 *
 * Input:
 *			buf:		C string containing a transport
 *					name and an unsigned integer value
 *					representing the port to listen on,
 *					separated by whitespace
 *			size:		non-zero length of C string in @buf
 * Output:
 *	On success:	returns zero; NFS service is started
 *	On error:	return code is a negative errno value
 */
static ssize_t write_ports(struct file *file, char *buf, size_t size)
{
	ssize_t rv;

	mutex_lock(&nfsd_mutex);
	rv = __write_ports(file, buf, size, netns(file));
	mutex_unlock(&nfsd_mutex);
	return rv;
}


int nfsd_max_blksize;

/*
 * write_maxblksize - Set or report the current NFS blksize
 *
 * Input:
 *			buf:		ignored
 *			size:		zero
 *
 * OR
 *
 * Input:
 *			buf:		C string containing an unsigned
 *					integer value representing the new
 *					NFS blksize
 *			size:		non-zero length of C string in @buf
 * Output:
 *	On success:	passed-in buffer filled with '\n'-terminated C string
 *			containing numeric value of the current NFS blksize
 *			setting;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero or a negative errno value
 */
static ssize_t write_maxblksize(struct file *file, char *buf, size_t size)
{
	char *mesg = buf;
	struct nfsd_net *nn = net_generic(netns(file), nfsd_net_id);

	if (size > 0) {
		int bsize;
		int rv = get_int(&mesg, &bsize);
		if (rv)
			return rv;
		trace_nfsd_ctl_maxblksize(netns(file), bsize);

		/* force bsize into allowed range and
		 * required alignment.
		 */
		bsize = max_t(int, bsize, 1024);
		bsize = min_t(int, bsize, NFSSVC_MAXBLKSIZE);
		bsize &= ~(1024-1);
		mutex_lock(&nfsd_mutex);
		if (nn->nfsd_serv) {
			mutex_unlock(&nfsd_mutex);
			return -EBUSY;
		}
		nfsd_max_blksize = bsize;
		mutex_unlock(&nfsd_mutex);
	}

	return scnprintf(buf, SIMPLE_TRANSACTION_LIMIT, "%d\n",
							nfsd_max_blksize);
}

#ifdef CONFIG_NFSD_V4
static ssize_t __nfsd4_write_time(struct file *file, char *buf, size_t size,
				  time64_t *time, struct nfsd_net *nn)
{
	struct dentry *dentry = file_dentry(file);
	char *mesg = buf;
	int rv, i;

	if (size > 0) {
		if (nn->nfsd_serv)
			return -EBUSY;
		rv = get_int(&mesg, &i);
		if (rv)
			return rv;
		trace_nfsd_ctl_time(netns(file), dentry->d_name.name,
				    dentry->d_name.len, i);

		/*
		 * Some sanity checking.  We don't have a reason for
		 * these particular numbers, but problems with the
		 * extremes are:
		 *	- Too short: the briefest network outage may
		 *	  cause clients to lose all their locks.  Also,
		 *	  the frequent polling may be wasteful.
		 *	- Too long: do you really want reboot recovery
		 *	  to take more than an hour?  Or to make other
		 *	  clients wait an hour before being able to
		 *	  revoke a dead client's locks?
		 */
		if (i < 10 || i > 3600)
			return -EINVAL;
		*time = i;
	}

	return scnprintf(buf, SIMPLE_TRANSACTION_LIMIT, "%lld\n", *time);
}

static ssize_t nfsd4_write_time(struct file *file, char *buf, size_t size,
				time64_t *time, struct nfsd_net *nn)
{
	ssize_t rv;

	mutex_lock(&nfsd_mutex);
	rv = __nfsd4_write_time(file, buf, size, time, nn);
	mutex_unlock(&nfsd_mutex);
	return rv;
}

/*
 * write_leasetime - Set or report the current NFSv4 lease time
 *
 * Input:
 *			buf:		ignored
 *			size:		zero
 *
 * OR
 *
 * Input:
 *			buf:		C string containing an unsigned
 *					integer value representing the new
 *					NFSv4 lease expiry time
 *			size:		non-zero length of C string in @buf
 * Output:
 *	On success:	passed-in buffer filled with '\n'-terminated C
 *			string containing unsigned integer value of the
 *			current lease expiry time;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero or a negative errno value
 */
static ssize_t write_leasetime(struct file *file, char *buf, size_t size)
{
	struct nfsd_net *nn = net_generic(netns(file), nfsd_net_id);
	return nfsd4_write_time(file, buf, size, &nn->nfsd4_lease, nn);
}

/*
 * write_gracetime - Set or report current NFSv4 grace period time
 *
 * As above, but sets the time of the NFSv4 grace period.
 *
 * Note this should never be set to less than the *previous*
 * lease-period time, but we don't try to enforce this.  (In the common
 * case (a new boot), we don't know what the previous lease time was
 * anyway.)
 */
static ssize_t write_gracetime(struct file *file, char *buf, size_t size)
{
	struct nfsd_net *nn = net_generic(netns(file), nfsd_net_id);
	return nfsd4_write_time(file, buf, size, &nn->nfsd4_grace, nn);
}

#ifdef CONFIG_NFSD_LEGACY_CLIENT_TRACKING
static ssize_t __write_recoverydir(struct file *file, char *buf, size_t size,
				   struct nfsd_net *nn)
{
	char *mesg = buf;
	char *recdir;
	int len, status;

	if (size > 0) {
		if (nn->nfsd_serv)
			return -EBUSY;
		if (size > PATH_MAX || buf[size-1] != '\n')
			return -EINVAL;
		buf[size-1] = 0;

		recdir = mesg;
		len = qword_get(&mesg, recdir, size);
		if (len <= 0)
			return -EINVAL;
		trace_nfsd_ctl_recoverydir(netns(file), recdir);

		status = nfs4_reset_recoverydir(recdir);
		if (status)
			return status;
	}

	return scnprintf(buf, SIMPLE_TRANSACTION_LIMIT, "%s\n",
							nfs4_recoverydir());
}

/*
 * write_recoverydir - Set or report the pathname of the recovery directory
 *
 * Input:
 *			buf:		ignored
 *			size:		zero
 *
 * OR
 *
 * Input:
 *			buf:		C string containing the pathname
 *					of the directory on a local file
 *					system containing permanent NFSv4
 *					recovery data
 *			size:		non-zero length of C string in @buf
 * Output:
 *	On success:	passed-in buffer filled with '\n'-terminated C string
 *			containing the current recovery pathname setting;
 *			return code is the size in bytes of the string
 *	On error:	return code is zero or a negative errno value
 */
static ssize_t write_recoverydir(struct file *file, char *buf, size_t size)
{
	ssize_t rv;
	struct nfsd_net *nn = net_generic(netns(file), nfsd_net_id);

	mutex_lock(&nfsd_mutex);
	rv = __write_recoverydir(file, buf, size, nn);
	mutex_unlock(&nfsd_mutex);
	return rv;
}
#endif

/*
 * write_v4_end_grace - release grace period for nfsd's v4.x lock manager
 *
 * Input:
 *			buf:		ignored
 *			size:		zero
 * OR
 *
 * Input:
 *			buf:		any value
 *			size:		non-zero length of C string in @buf
 * Output:
 *			passed-in buffer filled with "Y" or "N" with a newline
 *			and NULL-terminated C string. This indicates whether
 *			the grace period has ended in the current net
 *			namespace. Return code is the size in bytes of the
 *			string. Writing a string that starts with 'Y', 'y', or
 *			'1' to the file will end the grace period for nfsd's v4
 *			lock manager.
 */
static ssize_t write_v4_end_grace(struct file *file, char *buf, size_t size)
{
	struct nfsd_net *nn = net_generic(netns(file), nfsd_net_id);

	if (size > 0) {
		switch(buf[0]) {
		case 'Y':
		case 'y':
		case '1':
			if (!nn->nfsd_serv)
				return -EBUSY;
			trace_nfsd_end_grace(netns(file));
			nfsd4_end_grace(nn);
			break;
		default:
			return -EINVAL;
		}
	}

	return scnprintf(buf, SIMPLE_TRANSACTION_LIMIT, "%c\n",
			 nn->grace_ended ? 'Y' : 'N');
}

#endif

/*----------------------------------------------------------------------------*/
/*
 *	populating the filesystem.
 */

/* Basically copying rpc_get_inode. */
static struct inode *nfsd_get_inode(struct super_block *sb, umode_t mode)
{
	struct inode *inode = new_inode(sb);
	if (!inode)
		return NULL;
	/* Following advice from simple_fill_super documentation: */
	inode->i_ino = iunique(sb, NFSD_MaxReserved);
	inode->i_mode = mode;
	simple_inode_init_ts(inode);
	switch (mode & S_IFMT) {
	case S_IFDIR:
		inode->i_fop = &simple_dir_operations;
		inode->i_op = &simple_dir_inode_operations;
		inc_nlink(inode);
		break;
	case S_IFLNK:
		inode->i_op = &simple_symlink_inode_operations;
		break;
	default:
		break;
	}
	return inode;
}

static int __nfsd_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode, struct nfsdfs_client *ncl)
{
	struct inode *inode;

	inode = nfsd_get_inode(dir->i_sb, mode);
	if (!inode)
		return -ENOMEM;
	if (ncl) {
		inode->i_private = ncl;
		kref_get(&ncl->cl_ref);
	}
	d_add(dentry, inode);
	inc_nlink(dir);
	fsnotify_mkdir(dir, dentry);
	return 0;
}

static struct dentry *nfsd_mkdir(struct dentry *parent, struct nfsdfs_client *ncl, char *name)
{
	struct inode *dir = parent->d_inode;
	struct dentry *dentry;
	int ret = -ENOMEM;

	inode_lock(dir);
	dentry = d_alloc_name(parent, name);
	if (!dentry)
		goto out_err;
	ret = __nfsd_mkdir(d_inode(parent), dentry, S_IFDIR | 0600, ncl);
	if (ret)
		goto out_err;
out:
	inode_unlock(dir);
	return dentry;
out_err:
	dput(dentry);
	dentry = ERR_PTR(ret);
	goto out;
}

#if IS_ENABLED(CONFIG_SUNRPC_GSS)
static int __nfsd_symlink(struct inode *dir, struct dentry *dentry,
			  umode_t mode, const char *content)
{
	struct inode *inode;

	inode = nfsd_get_inode(dir->i_sb, mode);
	if (!inode)
		return -ENOMEM;

	inode->i_link = (char *)content;
	inode->i_size = strlen(content);

	d_add(dentry, inode);
	inc_nlink(dir);
	fsnotify_create(dir, dentry);
	return 0;
}

/*
 * @content is assumed to be a NUL-terminated string that lives
 * longer than the symlink itself.
 */
static void _nfsd_symlink(struct dentry *parent, const char *name,
			  const char *content)
{
	struct inode *dir = parent->d_inode;
	struct dentry *dentry;
	int ret;

	inode_lock(dir);
	dentry = d_alloc_name(parent, name);
	if (!dentry)
		goto out;
	ret = __nfsd_symlink(d_inode(parent), dentry, S_IFLNK | 0777, content);
	if (ret)
		dput(dentry);
out:
	inode_unlock(dir);
}
#else
static inline void _nfsd_symlink(struct dentry *parent, const char *name,
				 const char *content)
{
}

#endif

static void clear_ncl(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct nfsdfs_client *ncl = inode->i_private;

	spin_lock(&inode->i_lock);
	inode->i_private = NULL;
	spin_unlock(&inode->i_lock);
	kref_put(&ncl->cl_ref, ncl->cl_release);
}

struct nfsdfs_client *get_nfsdfs_client(struct inode *inode)
{
	struct nfsdfs_client *nc;

	spin_lock(&inode->i_lock);
	nc = inode->i_private;
	if (nc)
		kref_get(&nc->cl_ref);
	spin_unlock(&inode->i_lock);
	return nc;
}

/* XXX: cut'n'paste from simple_fill_super; figure out if we could share
 * code instead. */
static  int nfsdfs_create_files(struct dentry *root,
				const struct tree_descr *files,
				struct nfsdfs_client *ncl,
				struct dentry **fdentries)
{
	struct inode *dir = d_inode(root);
	struct inode *inode;
	struct dentry *dentry;
	int i;

	inode_lock(dir);
	for (i = 0; files->name && files->name[0]; i++, files++) {
		dentry = d_alloc_name(root, files->name);
		if (!dentry)
			goto out;
		inode = nfsd_get_inode(d_inode(root)->i_sb,
					S_IFREG | files->mode);
		if (!inode) {
			dput(dentry);
			goto out;
		}
		kref_get(&ncl->cl_ref);
		inode->i_fop = files->ops;
		inode->i_private = ncl;
		d_add(dentry, inode);
		fsnotify_create(dir, dentry);
		if (fdentries)
			fdentries[i] = dentry;
	}
	inode_unlock(dir);
	return 0;
out:
	inode_unlock(dir);
	return -ENOMEM;
}

/* on success, returns positive number unique to that client. */
struct dentry *nfsd_client_mkdir(struct nfsd_net *nn,
				 struct nfsdfs_client *ncl, u32 id,
				 const struct tree_descr *files,
				 struct dentry **fdentries)
{
	struct dentry *dentry;
	char name[11];
	int ret;

	sprintf(name, "%u", id);

	dentry = nfsd_mkdir(nn->nfsd_client_dir, ncl, name);
	if (IS_ERR(dentry)) /* XXX: tossing errors? */
		return NULL;
	ret = nfsdfs_create_files(dentry, files, ncl, fdentries);
	if (ret) {
		nfsd_client_rmdir(dentry);
		return NULL;
	}
	return dentry;
}

/* Taken from __rpc_rmdir: */
void nfsd_client_rmdir(struct dentry *dentry)
{
	simple_recursive_removal(dentry, clear_ncl);
}

static int nfsd_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
							nfsd_net_id);
	struct dentry *dentry;
	int ret;

	static const struct tree_descr nfsd_files[] = {
		[NFSD_List] = {"exports", &exports_nfsd_operations, S_IRUGO},
		/* Per-export io stats use same ops as exports file */
		[NFSD_Export_Stats] = {"export_stats", &exports_nfsd_operations, S_IRUGO},
		[NFSD_Export_features] = {"export_features",
					&export_features_fops, S_IRUGO},
		[NFSD_FO_UnlockIP] = {"unlock_ip",
					&transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_FO_UnlockFS] = {"unlock_filesystem",
					&transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Fh] = {"filehandle", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Threads] = {"threads", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Pool_Threads] = {"pool_threads", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Pool_Stats] = {"pool_stats", &pool_stats_operations, S_IRUGO},
		[NFSD_Reply_Cache_Stats] = {"reply_cache_stats",
					&nfsd_reply_cache_stats_fops, S_IRUGO},
		[NFSD_Versions] = {"versions", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Ports] = {"portlist", &transaction_ops, S_IWUSR|S_IRUGO},
		[NFSD_MaxBlkSize] = {"max_block_size", &transaction_ops, S_IWUSR|S_IRUGO},
		[NFSD_Filecache] = {"filecache", &nfsd_file_cache_stats_fops, S_IRUGO},
#ifdef CONFIG_NFSD_V4
		[NFSD_Leasetime] = {"nfsv4leasetime", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Gracetime] = {"nfsv4gracetime", &transaction_ops, S_IWUSR|S_IRUSR},
#ifdef CONFIG_NFSD_LEGACY_CLIENT_TRACKING
		[NFSD_RecoveryDir] = {"nfsv4recoverydir", &transaction_ops, S_IWUSR|S_IRUSR},
#endif
		[NFSD_V4EndGrace] = {"v4_end_grace", &transaction_ops, S_IWUSR|S_IRUGO},
#endif
		/* last one */ {""}
	};

	ret = simple_fill_super(sb, 0x6e667364, nfsd_files);
	if (ret)
		return ret;
	_nfsd_symlink(sb->s_root, "supported_krb5_enctypes",
		      "/proc/net/rpc/gss_krb5_enctypes");
	dentry = nfsd_mkdir(sb->s_root, NULL, "clients");
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	nn->nfsd_client_dir = dentry;
	return 0;
}

static int nfsd_fs_get_tree(struct fs_context *fc)
{
	return get_tree_keyed(fc, nfsd_fill_super, get_net(fc->net_ns));
}

static void nfsd_fs_free_fc(struct fs_context *fc)
{
	if (fc->s_fs_info)
		put_net(fc->s_fs_info);
}

static const struct fs_context_operations nfsd_fs_context_ops = {
	.free		= nfsd_fs_free_fc,
	.get_tree	= nfsd_fs_get_tree,
};

static int nfsd_init_fs_context(struct fs_context *fc)
{
	put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(fc->net_ns->user_ns);
	fc->ops = &nfsd_fs_context_ops;
	return 0;
}

static void nfsd_umount(struct super_block *sb)
{
	struct net *net = sb->s_fs_info;

	nfsd_shutdown_threads(net);

	kill_litter_super(sb);
	put_net(net);
}

static struct file_system_type nfsd_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfsd",
	.init_fs_context = nfsd_init_fs_context,
	.kill_sb	= nfsd_umount,
};
MODULE_ALIAS_FS("nfsd");

#ifdef CONFIG_PROC_FS

static int exports_proc_open(struct inode *inode, struct file *file)
{
	return exports_net_open(current->nsproxy->net_ns, file);
}

static const struct proc_ops exports_proc_ops = {
	.proc_open	= exports_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static int create_proc_exports_entry(void)
{
	struct proc_dir_entry *entry;

	entry = proc_mkdir("fs/nfs", NULL);
	if (!entry)
		return -ENOMEM;
	entry = proc_create("exports", 0, entry, &exports_proc_ops);
	if (!entry) {
		remove_proc_entry("fs/nfs", NULL);
		return -ENOMEM;
	}
	return 0;
}
#else /* CONFIG_PROC_FS */
static int create_proc_exports_entry(void)
{
	return 0;
}
#endif

unsigned int nfsd_net_id;

static int nfsd_genl_rpc_status_compose_msg(struct sk_buff *skb,
					    struct netlink_callback *cb,
					    struct nfsd_genl_rqstp *rqstp)
{
	void *hdr;
	u32 i;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &nfsd_nl_family, 0, NFSD_CMD_RPC_STATUS_GET);
	if (!hdr)
		return -ENOBUFS;

	if (nla_put_be32(skb, NFSD_A_RPC_STATUS_XID, rqstp->rq_xid) ||
	    nla_put_u32(skb, NFSD_A_RPC_STATUS_FLAGS, rqstp->rq_flags) ||
	    nla_put_u32(skb, NFSD_A_RPC_STATUS_PROG, rqstp->rq_prog) ||
	    nla_put_u32(skb, NFSD_A_RPC_STATUS_PROC, rqstp->rq_proc) ||
	    nla_put_u8(skb, NFSD_A_RPC_STATUS_VERSION, rqstp->rq_vers) ||
	    nla_put_s64(skb, NFSD_A_RPC_STATUS_SERVICE_TIME,
			ktime_to_us(rqstp->rq_stime),
			NFSD_A_RPC_STATUS_PAD))
		return -ENOBUFS;

	switch (rqstp->rq_saddr.sa_family) {
	case AF_INET: {
		const struct sockaddr_in *s_in, *d_in;

		s_in = (const struct sockaddr_in *)&rqstp->rq_saddr;
		d_in = (const struct sockaddr_in *)&rqstp->rq_daddr;
		if (nla_put_in_addr(skb, NFSD_A_RPC_STATUS_SADDR4,
				    s_in->sin_addr.s_addr) ||
		    nla_put_in_addr(skb, NFSD_A_RPC_STATUS_DADDR4,
				    d_in->sin_addr.s_addr) ||
		    nla_put_be16(skb, NFSD_A_RPC_STATUS_SPORT,
				 s_in->sin_port) ||
		    nla_put_be16(skb, NFSD_A_RPC_STATUS_DPORT,
				 d_in->sin_port))
			return -ENOBUFS;
		break;
	}
	case AF_INET6: {
		const struct sockaddr_in6 *s_in, *d_in;

		s_in = (const struct sockaddr_in6 *)&rqstp->rq_saddr;
		d_in = (const struct sockaddr_in6 *)&rqstp->rq_daddr;
		if (nla_put_in6_addr(skb, NFSD_A_RPC_STATUS_SADDR6,
				     &s_in->sin6_addr) ||
		    nla_put_in6_addr(skb, NFSD_A_RPC_STATUS_DADDR6,
				     &d_in->sin6_addr) ||
		    nla_put_be16(skb, NFSD_A_RPC_STATUS_SPORT,
				 s_in->sin6_port) ||
		    nla_put_be16(skb, NFSD_A_RPC_STATUS_DPORT,
				 d_in->sin6_port))
			return -ENOBUFS;
		break;
	}
	}

	for (i = 0; i < rqstp->rq_opcnt; i++)
		if (nla_put_u32(skb, NFSD_A_RPC_STATUS_COMPOUND_OPS,
				rqstp->rq_opnum[i]))
			return -ENOBUFS;

	genlmsg_end(skb, hdr);
	return 0;
}

/**
 * nfsd_nl_rpc_status_get_dumpit - Handle rpc_status_get dumpit
 * @skb: reply buffer
 * @cb: netlink metadata and command arguments
 *
 * Returns the size of the reply or a negative errno.
 */
int nfsd_nl_rpc_status_get_dumpit(struct sk_buff *skb,
				  struct netlink_callback *cb)
{
	int i, ret, rqstp_index = 0;
	struct nfsd_net *nn;

	mutex_lock(&nfsd_mutex);

	nn = net_generic(sock_net(skb->sk), nfsd_net_id);
	if (!nn->nfsd_serv) {
		ret = -ENODEV;
		goto out_unlock;
	}

	rcu_read_lock();

	for (i = 0; i < nn->nfsd_serv->sv_nrpools; i++) {
		struct svc_rqst *rqstp;

		if (i < cb->args[0]) /* already consumed */
			continue;

		rqstp_index = 0;
		list_for_each_entry_rcu(rqstp,
				&nn->nfsd_serv->sv_pools[i].sp_all_threads,
				rq_all) {
			struct nfsd_genl_rqstp genl_rqstp;
			unsigned int status_counter;

			if (rqstp_index++ < cb->args[1]) /* already consumed */
				continue;
			/*
			 * Acquire rq_status_counter before parsing the rqst
			 * fields. rq_status_counter is set to an odd value in
			 * order to notify the consumers the rqstp fields are
			 * meaningful.
			 */
			status_counter =
				smp_load_acquire(&rqstp->rq_status_counter);
			if (!(status_counter & 1))
				continue;

			genl_rqstp.rq_xid = rqstp->rq_xid;
			genl_rqstp.rq_flags = rqstp->rq_flags;
			genl_rqstp.rq_vers = rqstp->rq_vers;
			genl_rqstp.rq_prog = rqstp->rq_prog;
			genl_rqstp.rq_proc = rqstp->rq_proc;
			genl_rqstp.rq_stime = rqstp->rq_stime;
			genl_rqstp.rq_opcnt = 0;
			memcpy(&genl_rqstp.rq_daddr, svc_daddr(rqstp),
			       sizeof(struct sockaddr));
			memcpy(&genl_rqstp.rq_saddr, svc_addr(rqstp),
			       sizeof(struct sockaddr));

#ifdef CONFIG_NFSD_V4
			if (rqstp->rq_vers == NFS4_VERSION &&
			    rqstp->rq_proc == NFSPROC4_COMPOUND) {
				/* NFSv4 compound */
				struct nfsd4_compoundargs *args;
				int j;

				args = rqstp->rq_argp;
				genl_rqstp.rq_opcnt = args->opcnt;
				for (j = 0; j < genl_rqstp.rq_opcnt; j++)
					genl_rqstp.rq_opnum[j] =
						args->ops[j].opnum;
			}
#endif /* CONFIG_NFSD_V4 */

			/*
			 * Acquire rq_status_counter before reporting the rqst
			 * fields to the user.
			 */
			if (smp_load_acquire(&rqstp->rq_status_counter) !=
			    status_counter)
				continue;

			ret = nfsd_genl_rpc_status_compose_msg(skb, cb,
							       &genl_rqstp);
			if (ret)
				goto out;
		}
	}

	cb->args[0] = i;
	cb->args[1] = rqstp_index;
	ret = skb->len;
out:
	rcu_read_unlock();
out_unlock:
	mutex_unlock(&nfsd_mutex);

	return ret;
}

/**
 * nfsd_nl_threads_set_doit - set the number of running threads
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int nfsd_nl_threads_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	int *nthreads, nrpools = 0, i, ret = -EOPNOTSUPP, rem;
	struct net *net = genl_info_net(info);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	const struct nlattr *attr;
	const char *scope = NULL;

	if (GENL_REQ_ATTR_CHECK(info, NFSD_A_SERVER_THREADS))
		return -EINVAL;

	/* count number of SERVER_THREADS values */
	nlmsg_for_each_attr(attr, info->nlhdr, GENL_HDRLEN, rem) {
		if (nla_type(attr) == NFSD_A_SERVER_THREADS)
			nrpools++;
	}

	mutex_lock(&nfsd_mutex);

	nthreads = kcalloc(nrpools, sizeof(int), GFP_KERNEL);
	if (!nthreads) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	i = 0;
	nlmsg_for_each_attr(attr, info->nlhdr, GENL_HDRLEN, rem) {
		if (nla_type(attr) == NFSD_A_SERVER_THREADS) {
			nthreads[i++] = nla_get_u32(attr);
			if (i >= nrpools)
				break;
		}
	}

	if (info->attrs[NFSD_A_SERVER_GRACETIME] ||
	    info->attrs[NFSD_A_SERVER_LEASETIME] ||
	    info->attrs[NFSD_A_SERVER_SCOPE]) {
		ret = -EBUSY;
		if (nn->nfsd_serv && nn->nfsd_serv->sv_nrthreads)
			goto out_unlock;

		ret = -EINVAL;
		attr = info->attrs[NFSD_A_SERVER_GRACETIME];
		if (attr) {
			u32 gracetime = nla_get_u32(attr);

			if (gracetime < 10 || gracetime > 3600)
				goto out_unlock;

			nn->nfsd4_grace = gracetime;
		}

		attr = info->attrs[NFSD_A_SERVER_LEASETIME];
		if (attr) {
			u32 leasetime = nla_get_u32(attr);

			if (leasetime < 10 || leasetime > 3600)
				goto out_unlock;

			nn->nfsd4_lease = leasetime;
		}

		attr = info->attrs[NFSD_A_SERVER_SCOPE];
		if (attr)
			scope = nla_data(attr);
	}

	ret = nfsd_svc(nrpools, nthreads, net, get_current_cred(), scope);
	if (ret > 0)
		ret = 0;
out_unlock:
	mutex_unlock(&nfsd_mutex);
	kfree(nthreads);
	return ret;
}

/**
 * nfsd_nl_threads_get_doit - get the number of running threads
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int nfsd_nl_threads_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	void *hdr;
	int err;

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_iput(skb, info);
	if (!hdr) {
		err = -EMSGSIZE;
		goto err_free_msg;
	}

	mutex_lock(&nfsd_mutex);

	err = nla_put_u32(skb, NFSD_A_SERVER_GRACETIME,
			  nn->nfsd4_grace) ||
	      nla_put_u32(skb, NFSD_A_SERVER_LEASETIME,
			  nn->nfsd4_lease) ||
	      nla_put_string(skb, NFSD_A_SERVER_SCOPE,
			  nn->nfsd_name);
	if (err)
		goto err_unlock;

	if (nn->nfsd_serv) {
		int i;

		for (i = 0; i < nfsd_nrpools(net); ++i) {
			struct svc_pool *sp = &nn->nfsd_serv->sv_pools[i];

			err = nla_put_u32(skb, NFSD_A_SERVER_THREADS,
					  sp->sp_nrthreads);
			if (err)
				goto err_unlock;
		}
	} else {
		err = nla_put_u32(skb, NFSD_A_SERVER_THREADS, 0);
		if (err)
			goto err_unlock;
	}

	mutex_unlock(&nfsd_mutex);

	genlmsg_end(skb, hdr);

	return genlmsg_reply(skb, info);

err_unlock:
	mutex_unlock(&nfsd_mutex);
err_free_msg:
	nlmsg_free(skb);

	return err;
}

/**
 * nfsd_nl_version_set_doit - set the nfs enabled versions
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int nfsd_nl_version_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	const struct nlattr *attr;
	struct nfsd_net *nn;
	int i, rem;

	if (GENL_REQ_ATTR_CHECK(info, NFSD_A_SERVER_PROTO_VERSION))
		return -EINVAL;

	mutex_lock(&nfsd_mutex);

	nn = net_generic(genl_info_net(info), nfsd_net_id);
	if (nn->nfsd_serv) {
		mutex_unlock(&nfsd_mutex);
		return -EBUSY;
	}

	/* clear current supported versions. */
	nfsd_vers(nn, 2, NFSD_CLEAR);
	nfsd_vers(nn, 3, NFSD_CLEAR);
	for (i = 0; i <= NFSD_SUPPORTED_MINOR_VERSION; i++)
		nfsd_minorversion(nn, i, NFSD_CLEAR);

	nlmsg_for_each_attr(attr, info->nlhdr, GENL_HDRLEN, rem) {
		struct nlattr *tb[NFSD_A_VERSION_MAX + 1];
		u32 major, minor = 0;
		bool enabled;

		if (nla_type(attr) != NFSD_A_SERVER_PROTO_VERSION)
			continue;

		if (nla_parse_nested(tb, NFSD_A_VERSION_MAX, attr,
				     nfsd_version_nl_policy, info->extack) < 0)
			continue;

		if (!tb[NFSD_A_VERSION_MAJOR])
			continue;

		major = nla_get_u32(tb[NFSD_A_VERSION_MAJOR]);
		if (tb[NFSD_A_VERSION_MINOR])
			minor = nla_get_u32(tb[NFSD_A_VERSION_MINOR]);

		enabled = nla_get_flag(tb[NFSD_A_VERSION_ENABLED]);

		switch (major) {
		case 4:
			nfsd_minorversion(nn, minor, enabled ? NFSD_SET : NFSD_CLEAR);
			break;
		case 3:
		case 2:
			if (!minor)
				nfsd_vers(nn, major, enabled ? NFSD_SET : NFSD_CLEAR);
			break;
		default:
			break;
		}
	}

	mutex_unlock(&nfsd_mutex);

	return 0;
}

/**
 * nfsd_nl_version_get_doit - get the enabled status for all supported nfs versions
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int nfsd_nl_version_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nfsd_net *nn;
	int i, err;
	void *hdr;

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_iput(skb, info);
	if (!hdr) {
		err = -EMSGSIZE;
		goto err_free_msg;
	}

	mutex_lock(&nfsd_mutex);
	nn = net_generic(genl_info_net(info), nfsd_net_id);

	for (i = 2; i <= 4; i++) {
		int j;

		for (j = 0; j <= NFSD_SUPPORTED_MINOR_VERSION; j++) {
			struct nlattr *attr;

			/* Don't record any versions the kernel doesn't have
			 * compiled in
			 */
			if (!nfsd_support_version(i))
				continue;

			/* NFSv{2,3} does not support minor numbers */
			if (i < 4 && j)
				continue;

			attr = nla_nest_start(skb,
					      NFSD_A_SERVER_PROTO_VERSION);
			if (!attr) {
				err = -EINVAL;
				goto err_nfsd_unlock;
			}

			if (nla_put_u32(skb, NFSD_A_VERSION_MAJOR, i) ||
			    nla_put_u32(skb, NFSD_A_VERSION_MINOR, j)) {
				err = -EINVAL;
				goto err_nfsd_unlock;
			}

			/* Set the enabled flag if the version is enabled */
			if (nfsd_vers(nn, i, NFSD_TEST) &&
			    (i < 4 || nfsd_minorversion(nn, j, NFSD_TEST)) &&
			    nla_put_flag(skb, NFSD_A_VERSION_ENABLED)) {
				err = -EINVAL;
				goto err_nfsd_unlock;
			}

			nla_nest_end(skb, attr);
		}
	}

	mutex_unlock(&nfsd_mutex);
	genlmsg_end(skb, hdr);

	return genlmsg_reply(skb, info);

err_nfsd_unlock:
	mutex_unlock(&nfsd_mutex);
err_free_msg:
	nlmsg_free(skb);

	return err;
}

/**
 * nfsd_nl_listener_set_doit - set the nfs running sockets
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int nfsd_nl_listener_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct svc_xprt *xprt, *tmp;
	const struct nlattr *attr;
	struct svc_serv *serv;
	LIST_HEAD(permsocks);
	struct nfsd_net *nn;
	bool delete = false;
	int err, rem;

	mutex_lock(&nfsd_mutex);

	err = nfsd_create_serv(net);
	if (err) {
		mutex_unlock(&nfsd_mutex);
		return err;
	}

	nn = net_generic(net, nfsd_net_id);
	serv = nn->nfsd_serv;

	spin_lock_bh(&serv->sv_lock);

	/* Move all of the old listener sockets to a temp list */
	list_splice_init(&serv->sv_permsocks, &permsocks);

	/*
	 * Walk the list of server_socks from userland and move any that match
	 * back to sv_permsocks
	 */
	nlmsg_for_each_attr(attr, info->nlhdr, GENL_HDRLEN, rem) {
		struct nlattr *tb[NFSD_A_SOCK_MAX + 1];
		const char *xcl_name;
		struct sockaddr *sa;

		if (nla_type(attr) != NFSD_A_SERVER_SOCK_ADDR)
			continue;

		if (nla_parse_nested(tb, NFSD_A_SOCK_MAX, attr,
				     nfsd_sock_nl_policy, info->extack) < 0)
			continue;

		if (!tb[NFSD_A_SOCK_ADDR] || !tb[NFSD_A_SOCK_TRANSPORT_NAME])
			continue;

		if (nla_len(tb[NFSD_A_SOCK_ADDR]) < sizeof(*sa))
			continue;

		xcl_name = nla_data(tb[NFSD_A_SOCK_TRANSPORT_NAME]);
		sa = nla_data(tb[NFSD_A_SOCK_ADDR]);

		/* Put back any matching sockets */
		list_for_each_entry_safe(xprt, tmp, &permsocks, xpt_list) {
			/* This shouldn't be possible */
			if (WARN_ON_ONCE(xprt->xpt_net != net)) {
				list_move(&xprt->xpt_list, &serv->sv_permsocks);
				continue;
			}

			/* If everything matches, put it back */
			if (!strcmp(xprt->xpt_class->xcl_name, xcl_name) &&
			    rpc_cmp_addr_port(sa, (struct sockaddr *)&xprt->xpt_local)) {
				list_move(&xprt->xpt_list, &serv->sv_permsocks);
				break;
			}
		}
	}

	/*
	 * If there are listener transports remaining on the permsocks list,
	 * it means we were asked to remove a listener.
	 */
	if (!list_empty(&permsocks)) {
		list_splice_init(&permsocks, &serv->sv_permsocks);
		delete = true;
	}
	spin_unlock_bh(&serv->sv_lock);

	/* Do not remove listeners while there are active threads. */
	if (serv->sv_nrthreads) {
		err = -EBUSY;
		goto out_unlock_mtx;
	}

	/*
	 * Since we can't delete an arbitrary llist entry, destroy the
	 * remaining listeners and recreate the list.
	 */
	if (delete)
		svc_xprt_destroy_all(serv, net);

	/* walk list of addrs again, open any that still don't exist */
	nlmsg_for_each_attr(attr, info->nlhdr, GENL_HDRLEN, rem) {
		struct nlattr *tb[NFSD_A_SOCK_MAX + 1];
		const char *xcl_name;
		struct sockaddr *sa;
		int ret;

		if (nla_type(attr) != NFSD_A_SERVER_SOCK_ADDR)
			continue;

		if (nla_parse_nested(tb, NFSD_A_SOCK_MAX, attr,
				     nfsd_sock_nl_policy, info->extack) < 0)
			continue;

		if (!tb[NFSD_A_SOCK_ADDR] || !tb[NFSD_A_SOCK_TRANSPORT_NAME])
			continue;

		if (nla_len(tb[NFSD_A_SOCK_ADDR]) < sizeof(*sa))
			continue;

		xcl_name = nla_data(tb[NFSD_A_SOCK_TRANSPORT_NAME]);
		sa = nla_data(tb[NFSD_A_SOCK_ADDR]);

		xprt = svc_find_listener(serv, xcl_name, net, sa);
		if (xprt) {
			if (delete)
				WARN_ONCE(1, "Transport type=%s already exists\n",
					  xcl_name);
			svc_xprt_put(xprt);
			continue;
		}

		ret = svc_xprt_create_from_sa(serv, xcl_name, net, sa, 0,
					      get_current_cred());
		/* always save the latest error */
		if (ret < 0)
			err = ret;
	}

	if (!serv->sv_nrthreads && list_empty(&nn->nfsd_serv->sv_permsocks))
		nfsd_destroy_serv(net);

out_unlock_mtx:
	mutex_unlock(&nfsd_mutex);

	return err;
}

/**
 * nfsd_nl_listener_get_doit - get the nfs running listeners
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int nfsd_nl_listener_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct svc_xprt *xprt;
	struct svc_serv *serv;
	struct nfsd_net *nn;
	void *hdr;
	int err;

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_iput(skb, info);
	if (!hdr) {
		err = -EMSGSIZE;
		goto err_free_msg;
	}

	mutex_lock(&nfsd_mutex);
	nn = net_generic(genl_info_net(info), nfsd_net_id);

	/* no nfs server? Just send empty socket list */
	if (!nn->nfsd_serv)
		goto out_unlock_mtx;

	serv = nn->nfsd_serv;
	spin_lock_bh(&serv->sv_lock);
	list_for_each_entry(xprt, &serv->sv_permsocks, xpt_list) {
		struct nlattr *attr;

		attr = nla_nest_start(skb, NFSD_A_SERVER_SOCK_ADDR);
		if (!attr) {
			err = -EINVAL;
			goto err_serv_unlock;
		}

		if (nla_put_string(skb, NFSD_A_SOCK_TRANSPORT_NAME,
				   xprt->xpt_class->xcl_name) ||
		    nla_put(skb, NFSD_A_SOCK_ADDR,
			    sizeof(struct sockaddr_storage),
			    &xprt->xpt_local)) {
			err = -EINVAL;
			goto err_serv_unlock;
		}

		nla_nest_end(skb, attr);
	}
	spin_unlock_bh(&serv->sv_lock);
out_unlock_mtx:
	mutex_unlock(&nfsd_mutex);
	genlmsg_end(skb, hdr);

	return genlmsg_reply(skb, info);

err_serv_unlock:
	spin_unlock_bh(&serv->sv_lock);
	mutex_unlock(&nfsd_mutex);
err_free_msg:
	nlmsg_free(skb);

	return err;
}

/**
 * nfsd_nl_pool_mode_set_doit - set the number of running threads
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int nfsd_nl_pool_mode_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	const struct nlattr *attr;

	if (GENL_REQ_ATTR_CHECK(info, NFSD_A_POOL_MODE_MODE))
		return -EINVAL;

	attr = info->attrs[NFSD_A_POOL_MODE_MODE];
	return sunrpc_set_pool_mode(nla_data(attr));
}

/**
 * nfsd_nl_pool_mode_get_doit - get info about pool_mode
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int nfsd_nl_pool_mode_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	char buf[16];
	void *hdr;
	int err;

	if (sunrpc_get_pool_mode(buf, ARRAY_SIZE(buf)) >= ARRAY_SIZE(buf))
		return -ERANGE;

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	err = -EMSGSIZE;
	hdr = genlmsg_iput(skb, info);
	if (!hdr)
		goto err_free_msg;

	err = nla_put_string(skb, NFSD_A_POOL_MODE_MODE, buf) |
	      nla_put_u32(skb, NFSD_A_POOL_MODE_NPOOLS, nfsd_nrpools(net));
	if (err)
		goto err_free_msg;

	genlmsg_end(skb, hdr);
	return genlmsg_reply(skb, info);

err_free_msg:
	nlmsg_free(skb);
	return err;
}

/**
 * nfsd_net_init - Prepare the nfsd_net portion of a new net namespace
 * @net: a freshly-created network namespace
 *
 * This information stays around as long as the network namespace is
 * alive whether or not there is an NFSD instance running in the
 * namespace.
 *
 * Returns zero on success, or a negative errno otherwise.
 */
static __net_init int nfsd_net_init(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	int retval;
	int i;

	retval = nfsd_export_init(net);
	if (retval)
		goto out_export_error;
	retval = nfsd_idmap_init(net);
	if (retval)
		goto out_idmap_error;
	retval = percpu_counter_init_many(nn->counter, 0, GFP_KERNEL,
					  NFSD_STATS_COUNTERS_NUM);
	if (retval)
		goto out_repcache_error;

	memset(&nn->nfsd_svcstats, 0, sizeof(nn->nfsd_svcstats));
	nn->nfsd_svcstats.program = &nfsd_programs[0];
	if (!nfsd_proc_stat_init(net)) {
		retval = -ENOMEM;
		goto out_proc_error;
	}

	for (i = 0; i < sizeof(nn->nfsd_versions); i++)
		nn->nfsd_versions[i] = nfsd_support_version(i);
	for (i = 0; i < sizeof(nn->nfsd4_minorversions); i++)
		nn->nfsd4_minorversions[i] = nfsd_support_version(4);
	nn->nfsd_info.mutex = &nfsd_mutex;
	nn->nfsd_serv = NULL;
	nfsd4_init_leases_net(nn);
	get_random_bytes(&nn->siphash_key, sizeof(nn->siphash_key));
	seqlock_init(&nn->writeverf_lock);
#if IS_ENABLED(CONFIG_NFS_LOCALIO)
	spin_lock_init(&nn->local_clients_lock);
	INIT_LIST_HEAD(&nn->local_clients);
#endif
	return 0;

out_proc_error:
	percpu_counter_destroy_many(nn->counter, NFSD_STATS_COUNTERS_NUM);
out_repcache_error:
	nfsd_idmap_shutdown(net);
out_idmap_error:
	nfsd_export_shutdown(net);
out_export_error:
	return retval;
}

#if IS_ENABLED(CONFIG_NFS_LOCALIO)
/**
 * nfsd_net_pre_exit - Disconnect localio clients from net namespace
 * @net: a network namespace that is about to be destroyed
 *
 * This invalidates ->net pointers held by localio clients
 * while they can still safely access nn->counter.
 */
static __net_exit void nfsd_net_pre_exit(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	nfs_localio_invalidate_clients(&nn->local_clients,
				       &nn->local_clients_lock);
}
#endif

/**
 * nfsd_net_exit - Release the nfsd_net portion of a net namespace
 * @net: a network namespace that is about to be destroyed
 *
 */
static __net_exit void nfsd_net_exit(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	nfsd_proc_stat_shutdown(net);
	percpu_counter_destroy_many(nn->counter, NFSD_STATS_COUNTERS_NUM);
	nfsd_idmap_shutdown(net);
	nfsd_export_shutdown(net);
}

static struct pernet_operations nfsd_net_ops = {
	.init = nfsd_net_init,
#if IS_ENABLED(CONFIG_NFS_LOCALIO)
	.pre_exit = nfsd_net_pre_exit,
#endif
	.exit = nfsd_net_exit,
	.id   = &nfsd_net_id,
	.size = sizeof(struct nfsd_net),
};

static int __init init_nfsd(void)
{
	int retval;

	nfsd_debugfs_init();

	retval = nfsd4_init_slabs();
	if (retval)
		return retval;
	retval = nfsd4_init_pnfs();
	if (retval)
		goto out_free_slabs;
	retval = nfsd_drc_slab_create();
	if (retval)
		goto out_free_pnfs;
	nfsd_lockd_init();	/* lockd->nfsd callbacks */
	retval = register_pernet_subsys(&nfsd_net_ops);
	if (retval < 0)
		goto out_free_lockd;
	retval = register_cld_notifier();
	if (retval)
		goto out_free_subsys;
	retval = nfsd4_create_laundry_wq();
	if (retval)
		goto out_free_cld;
	retval = register_filesystem(&nfsd_fs_type);
	if (retval)
		goto out_free_nfsd4;
	retval = genl_register_family(&nfsd_nl_family);
	if (retval)
		goto out_free_filesystem;
	retval = create_proc_exports_entry();
	if (retval)
		goto out_free_all;
	nfsd_localio_ops_init();

	return 0;
out_free_all:
	genl_unregister_family(&nfsd_nl_family);
out_free_filesystem:
	unregister_filesystem(&nfsd_fs_type);
out_free_nfsd4:
	nfsd4_destroy_laundry_wq();
out_free_cld:
	unregister_cld_notifier();
out_free_subsys:
	unregister_pernet_subsys(&nfsd_net_ops);
out_free_lockd:
	nfsd_lockd_shutdown();
	nfsd_drc_slab_free();
out_free_pnfs:
	nfsd4_exit_pnfs();
out_free_slabs:
	nfsd4_free_slabs();
	nfsd_debugfs_exit();
	return retval;
}

static void __exit exit_nfsd(void)
{
	remove_proc_entry("fs/nfs/exports", NULL);
	remove_proc_entry("fs/nfs", NULL);
	genl_unregister_family(&nfsd_nl_family);
	unregister_filesystem(&nfsd_fs_type);
	nfsd4_destroy_laundry_wq();
	unregister_cld_notifier();
	unregister_pernet_subsys(&nfsd_net_ops);
	nfsd_drc_slab_free();
	nfsd_lockd_shutdown();
	nfsd4_free_slabs();
	nfsd4_exit_pnfs();
	nfsd_debugfs_exit();
}

MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_DESCRIPTION("In-kernel NFS server");
MODULE_LICENSE("GPL");
module_init(init_nfsd)
module_exit(exit_nfsd)
