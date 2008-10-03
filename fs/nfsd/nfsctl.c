/*
 * linux/fs/nfsd/nfsctl.c
 *
 * Syscall interface to knfsd.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>

#include <linux/linkage.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/inet.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/ctype.h>

#include <linux/nfs.h>
#include <linux/nfsd_idmap.h>
#include <linux/lockd/bind.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/lockd/lockd.h>

#include <asm/uaccess.h>
#include <net/ipv6.h>

/*
 *	We have a single directory with 9 nodes in it.
 */
enum {
	NFSD_Root = 1,
	NFSD_Svc,
	NFSD_Add,
	NFSD_Del,
	NFSD_Export,
	NFSD_Unexport,
	NFSD_Getfd,
	NFSD_Getfs,
	NFSD_List,
	NFSD_Fh,
	NFSD_FO_UnlockIP,
	NFSD_FO_UnlockFS,
	NFSD_Threads,
	NFSD_Pool_Threads,
	NFSD_Versions,
	NFSD_Ports,
	NFSD_MaxBlkSize,
	/*
	 * The below MUST come last.  Otherwise we leave a hole in nfsd_files[]
	 * with !CONFIG_NFSD_V4 and simple_fill_super() goes oops
	 */
#ifdef CONFIG_NFSD_V4
	NFSD_Leasetime,
	NFSD_RecoveryDir,
#endif
};

/*
 * write() for these nodes.
 */
static ssize_t write_svc(struct file *file, char *buf, size_t size);
static ssize_t write_add(struct file *file, char *buf, size_t size);
static ssize_t write_del(struct file *file, char *buf, size_t size);
static ssize_t write_export(struct file *file, char *buf, size_t size);
static ssize_t write_unexport(struct file *file, char *buf, size_t size);
static ssize_t write_getfd(struct file *file, char *buf, size_t size);
static ssize_t write_getfs(struct file *file, char *buf, size_t size);
static ssize_t write_filehandle(struct file *file, char *buf, size_t size);
static ssize_t write_threads(struct file *file, char *buf, size_t size);
static ssize_t write_pool_threads(struct file *file, char *buf, size_t size);
static ssize_t write_versions(struct file *file, char *buf, size_t size);
static ssize_t write_ports(struct file *file, char *buf, size_t size);
static ssize_t write_maxblksize(struct file *file, char *buf, size_t size);
#ifdef CONFIG_NFSD_V4
static ssize_t write_leasetime(struct file *file, char *buf, size_t size);
static ssize_t write_recoverydir(struct file *file, char *buf, size_t size);
#endif

static ssize_t failover_unlock_ip(struct file *file, char *buf, size_t size);
static ssize_t failover_unlock_fs(struct file *file, char *buf, size_t size);

static ssize_t (*write_op[])(struct file *, char *, size_t) = {
	[NFSD_Svc] = write_svc,
	[NFSD_Add] = write_add,
	[NFSD_Del] = write_del,
	[NFSD_Export] = write_export,
	[NFSD_Unexport] = write_unexport,
	[NFSD_Getfd] = write_getfd,
	[NFSD_Getfs] = write_getfs,
	[NFSD_Fh] = write_filehandle,
	[NFSD_FO_UnlockIP] = failover_unlock_ip,
	[NFSD_FO_UnlockFS] = failover_unlock_fs,
	[NFSD_Threads] = write_threads,
	[NFSD_Pool_Threads] = write_pool_threads,
	[NFSD_Versions] = write_versions,
	[NFSD_Ports] = write_ports,
	[NFSD_MaxBlkSize] = write_maxblksize,
#ifdef CONFIG_NFSD_V4
	[NFSD_Leasetime] = write_leasetime,
	[NFSD_RecoveryDir] = write_recoverydir,
#endif
};

static ssize_t nfsctl_transaction_write(struct file *file, const char __user *buf, size_t size, loff_t *pos)
{
	ino_t ino =  file->f_path.dentry->d_inode->i_ino;
	char *data;
	ssize_t rv;

	if (ino >= ARRAY_SIZE(write_op) || !write_op[ino])
		return -EINVAL;

	data = simple_transaction_get(file, buf, size);
	if (IS_ERR(data))
		return PTR_ERR(data);

	rv =  write_op[ino](file, data, size);
	if (rv >= 0) {
		simple_transaction_set(file, rv);
		rv = size;
	}
	return rv;
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
};

static int exports_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nfs_exports_op);
}

static const struct file_operations exports_operations = {
	.open		= exports_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
	.owner		= THIS_MODULE,
};

/*----------------------------------------------------------------------------*/
/*
 * payload - write methods
 * If the method has a response, the response should be put in buf,
 * and the length returned.  Otherwise return 0 or and -error.
 */

static ssize_t write_svc(struct file *file, char *buf, size_t size)
{
	struct nfsctl_svc *data;
	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_svc*) buf;
	return nfsd_svc(data->svc_port, data->svc_nthreads);
}

static ssize_t write_add(struct file *file, char *buf, size_t size)
{
	struct nfsctl_client *data;
	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_client *)buf;
	return exp_addclient(data);
}

static ssize_t write_del(struct file *file, char *buf, size_t size)
{
	struct nfsctl_client *data;
	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_client *)buf;
	return exp_delclient(data);
}

static ssize_t write_export(struct file *file, char *buf, size_t size)
{
	struct nfsctl_export *data;
	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_export*)buf;
	return exp_export(data);
}

static ssize_t write_unexport(struct file *file, char *buf, size_t size)
{
	struct nfsctl_export *data;

	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_export*)buf;
	return exp_unexport(data);
}

static ssize_t write_getfs(struct file *file, char *buf, size_t size)
{
	struct nfsctl_fsparm *data;
	struct sockaddr_in *sin;
	struct auth_domain *clp;
	int err = 0;
	struct knfsd_fh *res;
	struct in6_addr in6;

	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_fsparm*)buf;
	err = -EPROTONOSUPPORT;
	if (data->gd_addr.sa_family != AF_INET)
		goto out;
	sin = (struct sockaddr_in *)&data->gd_addr;
	if (data->gd_maxlen > NFS3_FHSIZE)
		data->gd_maxlen = NFS3_FHSIZE;

	res = (struct knfsd_fh*)buf;

	exp_readlock();

	ipv6_addr_set_v4mapped(sin->sin_addr.s_addr, &in6);

	clp = auth_unix_lookup(&in6);
	if (!clp)
		err = -EPERM;
	else {
		err = exp_rootfh(clp, data->gd_path, res, data->gd_maxlen);
		auth_domain_put(clp);
	}
	exp_readunlock();
	if (err == 0)
		err = res->fh_size + offsetof(struct knfsd_fh, fh_base);
 out:
	return err;
}

static ssize_t write_getfd(struct file *file, char *buf, size_t size)
{
	struct nfsctl_fdparm *data;
	struct sockaddr_in *sin;
	struct auth_domain *clp;
	int err = 0;
	struct knfsd_fh fh;
	char *res;
	struct in6_addr in6;

	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_fdparm*)buf;
	err = -EPROTONOSUPPORT;
	if (data->gd_addr.sa_family != AF_INET)
		goto out;
	err = -EINVAL;
	if (data->gd_version < 2 || data->gd_version > NFSSVC_MAXVERS)
		goto out;

	res = buf;
	sin = (struct sockaddr_in *)&data->gd_addr;
	exp_readlock();

	ipv6_addr_set_v4mapped(sin->sin_addr.s_addr, &in6);

	clp = auth_unix_lookup(&in6);
	if (!clp)
		err = -EPERM;
	else {
		err = exp_rootfh(clp, data->gd_path, &fh, NFS_FHSIZE);
		auth_domain_put(clp);
	}
	exp_readunlock();

	if (err == 0) {
		memset(res,0, NFS_FHSIZE);
		memcpy(res, &fh.fh_base, fh.fh_size);
		err = NFS_FHSIZE;
	}
 out:
	return err;
}

static ssize_t failover_unlock_ip(struct file *file, char *buf, size_t size)
{
	struct sockaddr_in sin = {
		.sin_family	= AF_INET,
	};
	int b1, b2, b3, b4;
	char c;
	char *fo_path;

	/* sanity check */
	if (size == 0)
		return -EINVAL;

	if (buf[size-1] != '\n')
		return -EINVAL;

	fo_path = buf;
	if (qword_get(&buf, fo_path, size) < 0)
		return -EINVAL;

	/* get ipv4 address */
	if (sscanf(fo_path, NIPQUAD_FMT "%c", &b1, &b2, &b3, &b4, &c) != 4)
		return -EINVAL;
	if (b1 > 255 || b2 > 255 || b3 > 255 || b4 > 255)
		return -EINVAL;
	sin.sin_addr.s_addr = htonl((b1 << 24) | (b2 << 16) | (b3 << 8) | b4);

	return nlmsvc_unlock_all_by_ip((struct sockaddr *)&sin);
}

static ssize_t failover_unlock_fs(struct file *file, char *buf, size_t size)
{
	struct nameidata nd;
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

	error = path_lookup(fo_path, 0, &nd);
	if (error)
		return error;

	error = nlmsvc_unlock_all_by_sb(nd.path.mnt->mnt_sb);

	path_put(&nd.path);
	return error;
}

static ssize_t write_filehandle(struct file *file, char *buf, size_t size)
{
	/* request is:
	 *   domain path maxsize
	 * response is
	 *   filehandle
	 *
	 * qword quoting is used, so filehandle will be \x....
	 */
	char *dname, *path;
	int uninitialized_var(maxsize);
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
	if (len <= 0) return -EINVAL;
	
	path = dname+len+1;
	len = qword_get(&mesg, path, size);
	if (len <= 0) return -EINVAL;

	len = get_int(&mesg, &maxsize);
	if (len)
		return len;

	if (maxsize < NFS_FHSIZE)
		return -EINVAL;
	if (maxsize > NFS3_FHSIZE)
		maxsize = NFS3_FHSIZE;

	if (qword_get(&mesg, mesg, size)>0)
		return -EINVAL;

	/* we have all the words, they are in buf.. */
	dom = unix_domain_find(dname);
	if (!dom)
		return -ENOMEM;

	len = exp_rootfh(dom, path, &fh,  maxsize);
	auth_domain_put(dom);
	if (len)
		return len;
	
	mesg = buf; len = SIMPLE_TRANSACTION_LIMIT;
	qword_addhex(&mesg, &len, (char*)&fh.fh_base, fh.fh_size);
	mesg[-1] = '\n';
	return mesg - buf;	
}

static ssize_t write_threads(struct file *file, char *buf, size_t size)
{
	/* if size > 0, look for a number of threads and call nfsd_svc
	 * then write out number of threads as reply
	 */
	char *mesg = buf;
	int rv;
	if (size > 0) {
		int newthreads;
		rv = get_int(&mesg, &newthreads);
		if (rv)
			return rv;
		if (newthreads <0)
			return -EINVAL;
		rv = nfsd_svc(2049, newthreads);
		if (rv)
			return rv;
	}
	sprintf(buf, "%d\n", nfsd_nrthreads());
	return strlen(buf);
}

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

	mutex_lock(&nfsd_mutex);
	npools = nfsd_nrpools();
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
		}
		rv = nfsd_set_nrthreads(i, nthreads);
		if (rv)
			goto out_free;
	}

	rv = nfsd_get_nrthreads(npools, nthreads);
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

	mutex_unlock(&nfsd_mutex);
	return (mesg-buf);

out_free:
	kfree(nthreads);
	mutex_unlock(&nfsd_mutex);
	return rv;
}

static ssize_t __write_versions(struct file *file, char *buf, size_t size)
{
	/*
	 * Format:
	 *   [-/+]vers [-/+]vers ...
	 */
	char *mesg = buf;
	char *vers, sign;
	int len, num;
	ssize_t tlen = 0;
	char *sep;

	if (size>0) {
		if (nfsd_serv)
			/* Cannot change versions without updating
			 * nfsd_serv->sv_xdrsize, and reallocing
			 * rq_argp and rq_resp
			 */
			return -EBUSY;
		if (buf[size-1] != '\n')
			return -EINVAL;
		buf[size-1] = 0;

		vers = mesg;
		len = qword_get(&mesg, vers, size);
		if (len <= 0) return -EINVAL;
		do {
			sign = *vers;
			if (sign == '+' || sign == '-')
				num = simple_strtol((vers+1), NULL, 0);
			else
				num = simple_strtol(vers, NULL, 0);
			switch(num) {
			case 2:
			case 3:
			case 4:
				nfsd_vers(num, sign == '-' ? NFSD_CLEAR : NFSD_SET);
				break;
			default:
				return -EINVAL;
			}
			vers += len + 1;
			tlen += len;
		} while ((len = qword_get(&mesg, vers, size)) > 0);
		/* If all get turned off, turn them back on, as
		 * having no versions is BAD
		 */
		nfsd_reset_versions();
	}
	/* Now write current state into reply buffer */
	len = 0;
	sep = "";
	for (num=2 ; num <= 4 ; num++)
		if (nfsd_vers(num, NFSD_AVAIL)) {
			len += sprintf(buf+len, "%s%c%d", sep,
				       nfsd_vers(num, NFSD_TEST)?'+':'-',
				       num);
			sep = " ";
		}
	len += sprintf(buf+len, "\n");
	return len;
}

static ssize_t write_versions(struct file *file, char *buf, size_t size)
{
	ssize_t rv;

	mutex_lock(&nfsd_mutex);
	rv = __write_versions(file, buf, size);
	mutex_unlock(&nfsd_mutex);
	return rv;
}

static ssize_t __write_ports(struct file *file, char *buf, size_t size)
{
	if (size == 0) {
		int len = 0;

		if (nfsd_serv)
			len = svc_xprt_names(nfsd_serv, buf, 0);
		return len;
	}
	/* Either a single 'fd' number is written, in which
	 * case it must be for a socket of a supported family/protocol,
	 * and we use it as an nfsd socket, or
	 * A '-' followed by the 'name' of a socket in which case
	 * we close the socket.
	 */
	if (isdigit(buf[0])) {
		char *mesg = buf;
		int fd;
		int err;
		err = get_int(&mesg, &fd);
		if (err)
			return -EINVAL;
		if (fd < 0)
			return -EINVAL;
		err = nfsd_create_serv();
		if (!err) {
			err = svc_addsock(nfsd_serv, fd, buf);
			if (err >= 0) {
				err = lockd_up();
				if (err < 0)
					svc_sock_names(buf+strlen(buf)+1, nfsd_serv, buf);
			}
			/* Decrease the count, but don't shutdown the
			 * the service
			 */
			nfsd_serv->sv_nrthreads--;
		}
		return err < 0 ? err : 0;
	}
	if (buf[0] == '-' && isdigit(buf[1])) {
		char *toclose = kstrdup(buf+1, GFP_KERNEL);
		int len = 0;
		if (!toclose)
			return -ENOMEM;
		if (nfsd_serv)
			len = svc_sock_names(buf, nfsd_serv, toclose);
		if (len >= 0)
			lockd_down();
		kfree(toclose);
		return len;
	}
	/*
	 * Add a transport listener by writing it's transport name
	 */
	if (isalpha(buf[0])) {
		int err;
		char transport[16];
		int port;
		if (sscanf(buf, "%15s %4d", transport, &port) == 2) {
			err = nfsd_create_serv();
			if (!err) {
				err = svc_create_xprt(nfsd_serv,
						      transport, port,
						      SVC_SOCK_ANONYMOUS);
				if (err == -ENOENT)
					/* Give a reasonable perror msg for
					 * bad transport string */
					err = -EPROTONOSUPPORT;
			}
			return err < 0 ? err : 0;
		}
	}
	/*
	 * Remove a transport by writing it's transport name and port number
	 */
	if (buf[0] == '-' && isalpha(buf[1])) {
		struct svc_xprt *xprt;
		int err = -EINVAL;
		char transport[16];
		int port;
		if (sscanf(&buf[1], "%15s %4d", transport, &port) == 2) {
			if (port == 0)
				return -EINVAL;
			if (nfsd_serv) {
				xprt = svc_find_xprt(nfsd_serv, transport,
						     AF_UNSPEC, port);
				if (xprt) {
					svc_close_xprt(xprt);
					svc_xprt_put(xprt);
					err = 0;
				} else
					err = -ENOTCONN;
			}
			return err < 0 ? err : 0;
		}
	}
	return -EINVAL;
}

static ssize_t write_ports(struct file *file, char *buf, size_t size)
{
	ssize_t rv;

	mutex_lock(&nfsd_mutex);
	rv = __write_ports(file, buf, size);
	mutex_unlock(&nfsd_mutex);
	return rv;
}


int nfsd_max_blksize;

static ssize_t write_maxblksize(struct file *file, char *buf, size_t size)
{
	char *mesg = buf;
	if (size > 0) {
		int bsize;
		int rv = get_int(&mesg, &bsize);
		if (rv)
			return rv;
		/* force bsize into allowed range and
		 * required alignment.
		 */
		if (bsize < 1024)
			bsize = 1024;
		if (bsize > NFSSVC_MAXBLKSIZE)
			bsize = NFSSVC_MAXBLKSIZE;
		bsize &= ~(1024-1);
		mutex_lock(&nfsd_mutex);
		if (nfsd_serv && nfsd_serv->sv_nrthreads) {
			mutex_unlock(&nfsd_mutex);
			return -EBUSY;
		}
		nfsd_max_blksize = bsize;
		mutex_unlock(&nfsd_mutex);
	}
	return sprintf(buf, "%d\n", nfsd_max_blksize);
}

#ifdef CONFIG_NFSD_V4
extern time_t nfs4_leasetime(void);

static ssize_t __write_leasetime(struct file *file, char *buf, size_t size)
{
	/* if size > 10 seconds, call
	 * nfs4_reset_lease() then write out the new lease (seconds) as reply
	 */
	char *mesg = buf;
	int rv, lease;

	if (size > 0) {
		if (nfsd_serv)
			return -EBUSY;
		rv = get_int(&mesg, &lease);
		if (rv)
			return rv;
		if (lease < 10 || lease > 3600)
			return -EINVAL;
		nfs4_reset_lease(lease);
	}
	sprintf(buf, "%ld\n", nfs4_lease_time());
	return strlen(buf);
}

static ssize_t write_leasetime(struct file *file, char *buf, size_t size)
{
	ssize_t rv;

	mutex_lock(&nfsd_mutex);
	rv = __write_leasetime(file, buf, size);
	mutex_unlock(&nfsd_mutex);
	return rv;
}

extern char *nfs4_recoverydir(void);

static ssize_t __write_recoverydir(struct file *file, char *buf, size_t size)
{
	char *mesg = buf;
	char *recdir;
	int len, status;

	if (size > 0) {
		if (nfsd_serv)
			return -EBUSY;
		if (size > PATH_MAX || buf[size-1] != '\n')
			return -EINVAL;
		buf[size-1] = 0;

		recdir = mesg;
		len = qword_get(&mesg, recdir, size);
		if (len <= 0)
			return -EINVAL;

		status = nfs4_reset_recoverydir(recdir);
	}
	sprintf(buf, "%s\n", nfs4_recoverydir());
	return strlen(buf);
}

static ssize_t write_recoverydir(struct file *file, char *buf, size_t size)
{
	ssize_t rv;

	mutex_lock(&nfsd_mutex);
	rv = __write_recoverydir(file, buf, size);
	mutex_unlock(&nfsd_mutex);
	return rv;
}

#endif

/*----------------------------------------------------------------------------*/
/*
 *	populating the filesystem.
 */

static int nfsd_fill_super(struct super_block * sb, void * data, int silent)
{
	static struct tree_descr nfsd_files[] = {
		[NFSD_Svc] = {".svc", &transaction_ops, S_IWUSR},
		[NFSD_Add] = {".add", &transaction_ops, S_IWUSR},
		[NFSD_Del] = {".del", &transaction_ops, S_IWUSR},
		[NFSD_Export] = {".export", &transaction_ops, S_IWUSR},
		[NFSD_Unexport] = {".unexport", &transaction_ops, S_IWUSR},
		[NFSD_Getfd] = {".getfd", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Getfs] = {".getfs", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_List] = {"exports", &exports_operations, S_IRUGO},
		[NFSD_FO_UnlockIP] = {"unlock_ip",
					&transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_FO_UnlockFS] = {"unlock_filesystem",
					&transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Fh] = {"filehandle", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Threads] = {"threads", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Pool_Threads] = {"pool_threads", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Versions] = {"versions", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Ports] = {"portlist", &transaction_ops, S_IWUSR|S_IRUGO},
		[NFSD_MaxBlkSize] = {"max_block_size", &transaction_ops, S_IWUSR|S_IRUGO},
#ifdef CONFIG_NFSD_V4
		[NFSD_Leasetime] = {"nfsv4leasetime", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_RecoveryDir] = {"nfsv4recoverydir", &transaction_ops, S_IWUSR|S_IRUSR},
#endif
		/* last one */ {""}
	};
	return simple_fill_super(sb, 0x6e667364, nfsd_files);
}

static int nfsd_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, nfsd_fill_super, mnt);
}

static struct file_system_type nfsd_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfsd",
	.get_sb		= nfsd_get_sb,
	.kill_sb	= kill_litter_super,
};

#ifdef CONFIG_PROC_FS
static int create_proc_exports_entry(void)
{
	struct proc_dir_entry *entry;

	entry = proc_mkdir("fs/nfs", NULL);
	if (!entry)
		return -ENOMEM;
	entry = proc_create("exports", 0, entry, &exports_operations);
	if (!entry)
		return -ENOMEM;
	return 0;
}
#else /* CONFIG_PROC_FS */
static int create_proc_exports_entry(void)
{
	return 0;
}
#endif

static int __init init_nfsd(void)
{
	int retval;
	printk(KERN_INFO "Installing knfsd (copyright (C) 1996 okir@monad.swb.de).\n");

	retval = nfs4_state_init(); /* nfs4 locking state */
	if (retval)
		return retval;
	nfsd_stat_init();	/* Statistics */
	retval = nfsd_reply_cache_init();
	if (retval)
		goto out_free_stat;
	retval = nfsd_export_init();
	if (retval)
		goto out_free_cache;
	nfsd_lockd_init();	/* lockd->nfsd callbacks */
	retval = nfsd_idmap_init();
	if (retval)
		goto out_free_lockd;
	retval = create_proc_exports_entry();
	if (retval)
		goto out_free_idmap;
	retval = register_filesystem(&nfsd_fs_type);
	if (retval)
		goto out_free_all;
	return 0;
out_free_all:
	remove_proc_entry("fs/nfs/exports", NULL);
	remove_proc_entry("fs/nfs", NULL);
out_free_idmap:
	nfsd_idmap_shutdown();
out_free_lockd:
	nfsd_lockd_shutdown();
	nfsd_export_shutdown();
out_free_cache:
	nfsd_reply_cache_shutdown();
out_free_stat:
	nfsd_stat_shutdown();
	nfsd4_free_slabs();
	return retval;
}

static void __exit exit_nfsd(void)
{
	nfsd_export_shutdown();
	nfsd_reply_cache_shutdown();
	remove_proc_entry("fs/nfs/exports", NULL);
	remove_proc_entry("fs/nfs", NULL);
	nfsd_stat_shutdown();
	nfsd_lockd_shutdown();
	nfsd_idmap_shutdown();
	nfsd4_free_slabs();
	unregister_filesystem(&nfsd_fs_type);
}

MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");
module_init(init_nfsd)
module_exit(exit_nfsd)
