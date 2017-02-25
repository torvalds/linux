/*
 *  inode.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *  Modified 1998 Wolfram Pienkoss for NLS
 *  Modified 2000 Ben Harris, University of Cambridge for NFS NS meta-info
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>

#include <linux/uaccess.h>
#include <asm/byteorder.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/vfs.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/namei.h>

#include <net/sock.h>

#include "ncp_fs.h"
#include "getopt.h"

#define NCP_DEFAULT_FILE_MODE 0600
#define NCP_DEFAULT_DIR_MODE 0700
#define NCP_DEFAULT_TIME_OUT 10
#define NCP_DEFAULT_RETRY_COUNT 20

static void ncp_evict_inode(struct inode *);
static void ncp_put_super(struct super_block *);
static int  ncp_statfs(struct dentry *, struct kstatfs *);
static int  ncp_show_options(struct seq_file *, struct dentry *);

static struct kmem_cache * ncp_inode_cachep;

static struct inode *ncp_alloc_inode(struct super_block *sb)
{
	struct ncp_inode_info *ei;
	ei = (struct ncp_inode_info *)kmem_cache_alloc(ncp_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void ncp_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(ncp_inode_cachep, NCP_FINFO(inode));
}

static void ncp_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, ncp_i_callback);
}

static void init_once(void *foo)
{
	struct ncp_inode_info *ei = (struct ncp_inode_info *) foo;

	mutex_init(&ei->open_mutex);
	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	ncp_inode_cachep = kmem_cache_create("ncp_inode_cache",
					     sizeof(struct ncp_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (ncp_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ncp_inode_cachep);
}

static int ncp_remount(struct super_block *sb, int *flags, char* data)
{
	sync_filesystem(sb);
	*flags |= MS_NODIRATIME;
	return 0;
}

static const struct super_operations ncp_sops =
{
	.alloc_inode	= ncp_alloc_inode,
	.destroy_inode	= ncp_destroy_inode,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= ncp_evict_inode,
	.put_super	= ncp_put_super,
	.statfs		= ncp_statfs,
	.remount_fs	= ncp_remount,
	.show_options	= ncp_show_options,
};

/*
 * Fill in the ncpfs-specific information in the inode.
 */
static void ncp_update_dirent(struct inode *inode, struct ncp_entry_info *nwinfo)
{
	NCP_FINFO(inode)->DosDirNum = nwinfo->i.DosDirNum;
	NCP_FINFO(inode)->dirEntNum = nwinfo->i.dirEntNum;
	NCP_FINFO(inode)->volNumber = nwinfo->volume;
}

void ncp_update_inode(struct inode *inode, struct ncp_entry_info *nwinfo)
{
	ncp_update_dirent(inode, nwinfo);
	NCP_FINFO(inode)->nwattr = nwinfo->i.attributes;
	NCP_FINFO(inode)->access = nwinfo->access;
	memcpy(NCP_FINFO(inode)->file_handle, nwinfo->file_handle,
			sizeof(nwinfo->file_handle));
	ncp_dbg(1, "updated %s, volnum=%d, dirent=%u\n",
		nwinfo->i.entryName, NCP_FINFO(inode)->volNumber,
		NCP_FINFO(inode)->dirEntNum);
}

static void ncp_update_dates(struct inode *inode, struct nw_info_struct *nwi)
{
	/* NFS namespace mode overrides others if it's set. */
	ncp_dbg(1, "(%s) nfs.mode=0%o\n", nwi->entryName, nwi->nfs.mode);
	if (nwi->nfs.mode) {
		/* XXX Security? */
		inode->i_mode = nwi->nfs.mode;
	}

	inode->i_blocks = (i_size_read(inode) + NCP_BLOCK_SIZE - 1) >> NCP_BLOCK_SHIFT;

	inode->i_mtime.tv_sec = ncp_date_dos2unix(nwi->modifyTime, nwi->modifyDate);
	inode->i_ctime.tv_sec = ncp_date_dos2unix(nwi->creationTime, nwi->creationDate);
	inode->i_atime.tv_sec = ncp_date_dos2unix(0, nwi->lastAccessDate);
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
}

static void ncp_update_attrs(struct inode *inode, struct ncp_entry_info *nwinfo)
{
	struct nw_info_struct *nwi = &nwinfo->i;
	struct ncp_server *server = NCP_SERVER(inode);

	if (nwi->attributes & aDIR) {
		inode->i_mode = server->m.dir_mode;
		/* for directories dataStreamSize seems to be some
		   Object ID ??? */
		i_size_write(inode, NCP_BLOCK_SIZE);
	} else {
		u32 size;

		inode->i_mode = server->m.file_mode;
		size = le32_to_cpu(nwi->dataStreamSize);
		i_size_write(inode, size);
#ifdef CONFIG_NCPFS_EXTRAS
		if ((server->m.flags & (NCP_MOUNT_EXTRAS|NCP_MOUNT_SYMLINKS)) 
		 && (nwi->attributes & aSHARED)) {
			switch (nwi->attributes & (aHIDDEN|aSYSTEM)) {
				case aHIDDEN:
					if (server->m.flags & NCP_MOUNT_SYMLINKS) {
						if (/* (size >= NCP_MIN_SYMLINK_SIZE)
						 && */ (size <= NCP_MAX_SYMLINK_SIZE)) {
							inode->i_mode = (inode->i_mode & ~S_IFMT) | S_IFLNK;
							NCP_FINFO(inode)->flags |= NCPI_KLUDGE_SYMLINK;
							break;
						}
					}
					/* FALLTHROUGH */
				case 0:
					if (server->m.flags & NCP_MOUNT_EXTRAS)
						inode->i_mode |= S_IRUGO;
					break;
				case aSYSTEM:
					if (server->m.flags & NCP_MOUNT_EXTRAS)
						inode->i_mode |= (inode->i_mode >> 2) & S_IXUGO;
					break;
				/* case aSYSTEM|aHIDDEN: */
				default:
					/* reserved combination */
					break;
			}
		}
#endif
	}
	if (nwi->attributes & aRONLY) inode->i_mode &= ~S_IWUGO;
}

void ncp_update_inode2(struct inode* inode, struct ncp_entry_info *nwinfo)
{
	NCP_FINFO(inode)->flags = 0;
	if (!atomic_read(&NCP_FINFO(inode)->opened)) {
		NCP_FINFO(inode)->nwattr = nwinfo->i.attributes;
		ncp_update_attrs(inode, nwinfo);
	}

	ncp_update_dates(inode, &nwinfo->i);
	ncp_update_dirent(inode, nwinfo);
}

/*
 * Fill in the inode based on the ncp_entry_info structure.  Used only for brand new inodes.
 */
static void ncp_set_attr(struct inode *inode, struct ncp_entry_info *nwinfo)
{
	struct ncp_server *server = NCP_SERVER(inode);

	NCP_FINFO(inode)->flags = 0;
	
	ncp_update_attrs(inode, nwinfo);

	ncp_dbg(2, "inode->i_mode = %u\n", inode->i_mode);

	set_nlink(inode, 1);
	inode->i_uid = server->m.uid;
	inode->i_gid = server->m.gid;

	ncp_update_dates(inode, &nwinfo->i);
	ncp_update_inode(inode, nwinfo);
}

#if defined(CONFIG_NCPFS_EXTRAS) || defined(CONFIG_NCPFS_NFS_NS)
static const struct inode_operations ncp_symlink_inode_operations = {
	.get_link	= page_get_link,
	.setattr	= ncp_notify_change,
};
#endif

/*
 * Get a new inode.
 */
struct inode * 
ncp_iget(struct super_block *sb, struct ncp_entry_info *info)
{
	struct inode *inode;

	if (info == NULL) {
		pr_err("%s: info is NULL\n", __func__);
		return NULL;
	}

	inode = new_inode(sb);
	if (inode) {
		atomic_set(&NCP_FINFO(inode)->opened, info->opened);

		inode->i_ino = info->ino;
		ncp_set_attr(inode, info);
		if (S_ISREG(inode->i_mode)) {
			inode->i_op = &ncp_file_inode_operations;
			inode->i_fop = &ncp_file_operations;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_op = &ncp_dir_inode_operations;
			inode->i_fop = &ncp_dir_operations;
#ifdef CONFIG_NCPFS_NFS_NS
		} else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) || S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
			init_special_inode(inode, inode->i_mode,
				new_decode_dev(info->i.nfs.rdev));
#endif
#if defined(CONFIG_NCPFS_EXTRAS) || defined(CONFIG_NCPFS_NFS_NS)
		} else if (S_ISLNK(inode->i_mode)) {
			inode->i_op = &ncp_symlink_inode_operations;
			inode_nohighmem(inode);
			inode->i_data.a_ops = &ncp_symlink_aops;
#endif
		} else {
			make_bad_inode(inode);
		}
		insert_inode_hash(inode);
	} else
		pr_err("%s: iget failed!\n", __func__);
	return inode;
}

static void
ncp_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	if (S_ISDIR(inode->i_mode)) {
		ncp_dbg(2, "put directory %ld\n", inode->i_ino);
	}

	if (ncp_make_closed(inode) != 0) {
		/* We can't do anything but complain. */
		pr_err("%s: could not close\n", __func__);
	}
}

static void ncp_stop_tasks(struct ncp_server *server) {
	struct sock* sk = server->ncp_sock->sk;

	lock_sock(sk);
	sk->sk_error_report = server->error_report;
	sk->sk_data_ready   = server->data_ready;
	sk->sk_write_space  = server->write_space;
	release_sock(sk);
	del_timer_sync(&server->timeout_tm);

	flush_work(&server->rcv.tq);
	if (sk->sk_socket->type == SOCK_STREAM)
		flush_work(&server->tx.tq);
	else
		flush_work(&server->timeout_tq);
}

static int  ncp_show_options(struct seq_file *seq, struct dentry *root)
{
	struct ncp_server *server = NCP_SBP(root->d_sb);
	unsigned int tmp;

	if (!uid_eq(server->m.uid, GLOBAL_ROOT_UID))
		seq_printf(seq, ",uid=%u",
			   from_kuid_munged(&init_user_ns, server->m.uid));
	if (!gid_eq(server->m.gid, GLOBAL_ROOT_GID))
		seq_printf(seq, ",gid=%u",
			   from_kgid_munged(&init_user_ns, server->m.gid));
	if (!uid_eq(server->m.mounted_uid, GLOBAL_ROOT_UID))
		seq_printf(seq, ",owner=%u",
			   from_kuid_munged(&init_user_ns, server->m.mounted_uid));
	tmp = server->m.file_mode & S_IALLUGO;
	if (tmp != NCP_DEFAULT_FILE_MODE)
		seq_printf(seq, ",mode=0%o", tmp);
	tmp = server->m.dir_mode & S_IALLUGO;
	if (tmp != NCP_DEFAULT_DIR_MODE)
		seq_printf(seq, ",dirmode=0%o", tmp);
	if (server->m.time_out != NCP_DEFAULT_TIME_OUT * HZ / 100) {
		tmp = server->m.time_out * 100 / HZ;
		seq_printf(seq, ",timeout=%u", tmp);
	}
	if (server->m.retry_count != NCP_DEFAULT_RETRY_COUNT)
		seq_printf(seq, ",retry=%u", server->m.retry_count);
	if (server->m.flags != 0)
		seq_printf(seq, ",flags=%lu", server->m.flags);
	if (server->m.wdog_pid != NULL)
		seq_printf(seq, ",wdogpid=%u", pid_vnr(server->m.wdog_pid));

	return 0;
}

static const struct ncp_option ncp_opts[] = {
	{ "uid",	OPT_INT,	'u' },
	{ "gid",	OPT_INT,	'g' },
	{ "owner",	OPT_INT,	'o' },
	{ "mode",	OPT_INT,	'm' },
	{ "dirmode",	OPT_INT,	'd' },
	{ "timeout",	OPT_INT,	't' },
	{ "retry",	OPT_INT,	'r' },
	{ "flags",	OPT_INT,	'f' },
	{ "wdogpid",	OPT_INT,	'w' },
	{ "ncpfd",	OPT_INT,	'n' },
	{ "infofd",	OPT_INT,	'i' },	/* v5 */
	{ "version",	OPT_INT,	'v' },
	{ NULL,		0,		0 } };

static int ncp_parse_options(struct ncp_mount_data_kernel *data, char *options) {
	int optval;
	char *optarg;
	unsigned long optint;
	int version = 0;
	int ret;

	data->flags = 0;
	data->int_flags = 0;
	data->mounted_uid = GLOBAL_ROOT_UID;
	data->wdog_pid = NULL;
	data->ncp_fd = ~0;
	data->time_out = NCP_DEFAULT_TIME_OUT;
	data->retry_count = NCP_DEFAULT_RETRY_COUNT;
	data->uid = GLOBAL_ROOT_UID;
	data->gid = GLOBAL_ROOT_GID;
	data->file_mode = NCP_DEFAULT_FILE_MODE;
	data->dir_mode = NCP_DEFAULT_DIR_MODE;
	data->info_fd = -1;
	data->mounted_vol[0] = 0;
	
	while ((optval = ncp_getopt("ncpfs", &options, ncp_opts, NULL, &optarg, &optint)) != 0) {
		ret = optval;
		if (ret < 0)
			goto err;
		switch (optval) {
			case 'u':
				data->uid = make_kuid(current_user_ns(), optint);
				if (!uid_valid(data->uid)) {
					ret = -EINVAL;
					goto err;
				}
				break;
			case 'g':
				data->gid = make_kgid(current_user_ns(), optint);
				if (!gid_valid(data->gid)) {
					ret = -EINVAL;
					goto err;
				}
				break;
			case 'o':
				data->mounted_uid = make_kuid(current_user_ns(), optint);
				if (!uid_valid(data->mounted_uid)) {
					ret = -EINVAL;
					goto err;
				}
				break;
			case 'm':
				data->file_mode = optint;
				break;
			case 'd':
				data->dir_mode = optint;
				break;
			case 't':
				data->time_out = optint;
				break;
			case 'r':
				data->retry_count = optint;
				break;
			case 'f':
				data->flags = optint;
				break;
			case 'w':
				data->wdog_pid = find_get_pid(optint);
				break;
			case 'n':
				data->ncp_fd = optint;
				break;
			case 'i':
				data->info_fd = optint;
				break;
			case 'v':
				ret = -ECHRNG;
				if (optint < NCP_MOUNT_VERSION_V4)
					goto err;
				if (optint > NCP_MOUNT_VERSION_V5)
					goto err;
				version = optint;
				break;
			
		}
	}
	return 0;
err:
	put_pid(data->wdog_pid);
	data->wdog_pid = NULL;
	return ret;
}

static int ncp_fill_super(struct super_block *sb, void *raw_data, int silent)
{
	struct ncp_mount_data_kernel data;
	struct ncp_server *server;
	struct inode *root_inode;
	struct socket *sock;
	int error;
	int default_bufsize;
#ifdef CONFIG_NCPFS_PACKET_SIGNING
	int options;
#endif
	struct ncp_entry_info finfo;

	memset(&data, 0, sizeof(data));
	server = kzalloc(sizeof(struct ncp_server), GFP_KERNEL);
	if (!server)
		return -ENOMEM;
	sb->s_fs_info = server;

	error = -EFAULT;
	if (raw_data == NULL)
		goto out;
	switch (*(int*)raw_data) {
		case NCP_MOUNT_VERSION:
			{
				struct ncp_mount_data* md = (struct ncp_mount_data*)raw_data;

				data.flags = md->flags;
				data.int_flags = NCP_IMOUNT_LOGGEDIN_POSSIBLE;
				data.mounted_uid = make_kuid(current_user_ns(), md->mounted_uid);
				data.wdog_pid = find_get_pid(md->wdog_pid);
				data.ncp_fd = md->ncp_fd;
				data.time_out = md->time_out;
				data.retry_count = md->retry_count;
				data.uid = make_kuid(current_user_ns(), md->uid);
				data.gid = make_kgid(current_user_ns(), md->gid);
				data.file_mode = md->file_mode;
				data.dir_mode = md->dir_mode;
				data.info_fd = -1;
				memcpy(data.mounted_vol, md->mounted_vol,
					NCP_VOLNAME_LEN+1);
			}
			break;
		case NCP_MOUNT_VERSION_V4:
			{
				struct ncp_mount_data_v4* md = (struct ncp_mount_data_v4*)raw_data;

				data.flags = md->flags;
				data.mounted_uid = make_kuid(current_user_ns(), md->mounted_uid);
				data.wdog_pid = find_get_pid(md->wdog_pid);
				data.ncp_fd = md->ncp_fd;
				data.time_out = md->time_out;
				data.retry_count = md->retry_count;
				data.uid = make_kuid(current_user_ns(), md->uid);
				data.gid = make_kgid(current_user_ns(), md->gid);
				data.file_mode = md->file_mode;
				data.dir_mode = md->dir_mode;
				data.info_fd = -1;
			}
			break;
		default:
			error = -ECHRNG;
			if (memcmp(raw_data, "vers", 4) == 0) {
				error = ncp_parse_options(&data, raw_data);
			}
			if (error)
				goto out;
			break;
	}
	error = -EINVAL;
	if (!uid_valid(data.mounted_uid) || !uid_valid(data.uid) ||
	    !gid_valid(data.gid))
		goto out;
	sock = sockfd_lookup(data.ncp_fd, &error);
	if (!sock)
		goto out;

	if (sock->type == SOCK_STREAM)
		default_bufsize = 0xF000;
	else
		default_bufsize = 1024;

	sb->s_flags |= MS_NODIRATIME;	/* probably even noatime */
	sb->s_maxbytes = 0xFFFFFFFFU;
	sb->s_blocksize = 1024;	/* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = NCP_SUPER_MAGIC;
	sb->s_op = &ncp_sops;
	sb->s_d_op = &ncp_dentry_operations;
	sb->s_bdi = &server->bdi;

	server = NCP_SBP(sb);
	memset(server, 0, sizeof(*server));

	error = bdi_setup_and_register(&server->bdi, "ncpfs");
	if (error)
		goto out_fput;

	server->ncp_sock = sock;
	
	if (data.info_fd != -1) {
		struct socket *info_sock = sockfd_lookup(data.info_fd, &error);
		if (!info_sock)
			goto out_bdi;
		server->info_sock = info_sock;
		error = -EBADFD;
		if (info_sock->type != SOCK_STREAM)
			goto out_fput2;
	}

/*	server->lock = 0;	*/
	mutex_init(&server->mutex);
	server->packet = NULL;
/*	server->buffer_size = 0;	*/
/*	server->conn_status = 0;	*/
/*	server->root_dentry = NULL;	*/
/*	server->root_setuped = 0;	*/
	mutex_init(&server->root_setup_lock);
#ifdef CONFIG_NCPFS_PACKET_SIGNING
/*	server->sign_wanted = 0;	*/
/*	server->sign_active = 0;	*/
#endif
	init_rwsem(&server->auth_rwsem);
	server->auth.auth_type = NCP_AUTH_NONE;
/*	server->auth.object_name_len = 0;	*/
/*	server->auth.object_name = NULL;	*/
/*	server->auth.object_type = 0;		*/
/*	server->priv.len = 0;			*/
/*	server->priv.data = NULL;		*/

	server->m = data;
	/* Although anything producing this is buggy, it happens
	   now because of PATH_MAX changes.. */
	if (server->m.time_out < 1) {
		server->m.time_out = 10;
		pr_info("You need to recompile your ncpfs utils..\n");
	}
	server->m.time_out = server->m.time_out * HZ / 100;
	server->m.file_mode = (server->m.file_mode & S_IRWXUGO) | S_IFREG;
	server->m.dir_mode = (server->m.dir_mode & S_IRWXUGO) | S_IFDIR;

#ifdef CONFIG_NCPFS_NLS
	/* load the default NLS charsets */
	server->nls_vol = load_nls_default();
	server->nls_io = load_nls_default();
#endif /* CONFIG_NCPFS_NLS */

	atomic_set(&server->dentry_ttl, 0);	/* no caching */

	INIT_LIST_HEAD(&server->tx.requests);
	mutex_init(&server->rcv.creq_mutex);
	server->tx.creq		= NULL;
	server->rcv.creq	= NULL;

	init_timer(&server->timeout_tm);
#undef NCP_PACKET_SIZE
#define NCP_PACKET_SIZE 131072
	error = -ENOMEM;
	server->packet_size = NCP_PACKET_SIZE;
	server->packet = vmalloc(NCP_PACKET_SIZE);
	if (server->packet == NULL)
		goto out_nls;
	server->txbuf = vmalloc(NCP_PACKET_SIZE);
	if (server->txbuf == NULL)
		goto out_packet;
	server->rxbuf = vmalloc(NCP_PACKET_SIZE);
	if (server->rxbuf == NULL)
		goto out_txbuf;

	lock_sock(sock->sk);
	server->data_ready	= sock->sk->sk_data_ready;
	server->write_space	= sock->sk->sk_write_space;
	server->error_report	= sock->sk->sk_error_report;
	sock->sk->sk_user_data	= server;
	sock->sk->sk_data_ready	  = ncp_tcp_data_ready;
	sock->sk->sk_error_report = ncp_tcp_error_report;
	if (sock->type == SOCK_STREAM) {
		server->rcv.ptr = (unsigned char*)&server->rcv.buf;
		server->rcv.len = 10;
		server->rcv.state = 0;
		INIT_WORK(&server->rcv.tq, ncp_tcp_rcv_proc);
		INIT_WORK(&server->tx.tq, ncp_tcp_tx_proc);
		sock->sk->sk_write_space = ncp_tcp_write_space;
	} else {
		INIT_WORK(&server->rcv.tq, ncpdgram_rcv_proc);
		INIT_WORK(&server->timeout_tq, ncpdgram_timeout_proc);
		server->timeout_tm.data = (unsigned long)server;
		server->timeout_tm.function = ncpdgram_timeout_call;
	}
	release_sock(sock->sk);

	ncp_lock_server(server);
	error = ncp_connect(server);
	ncp_unlock_server(server);
	if (error < 0)
		goto out_rxbuf;
	ncp_dbg(1, "NCP_SBP(sb) = %p\n", NCP_SBP(sb));

	error = -EMSGSIZE;	/* -EREMOTESIDEINCOMPATIBLE */
#ifdef CONFIG_NCPFS_PACKET_SIGNING
	if (ncp_negotiate_size_and_options(server, default_bufsize,
		NCP_DEFAULT_OPTIONS, &(server->buffer_size), &options) == 0)
	{
		if (options != NCP_DEFAULT_OPTIONS)
		{
			if (ncp_negotiate_size_and_options(server, 
				default_bufsize,
				options & 2, 
				&(server->buffer_size), &options) != 0)
				
			{
				goto out_disconnect;
			}
		}
		ncp_lock_server(server);
		if (options & 2)
			server->sign_wanted = 1;
		ncp_unlock_server(server);
	}
	else 
#endif	/* CONFIG_NCPFS_PACKET_SIGNING */
	if (ncp_negotiate_buffersize(server, default_bufsize,
  				     &(server->buffer_size)) != 0)
		goto out_disconnect;
	ncp_dbg(1, "bufsize = %d\n", server->buffer_size);

	memset(&finfo, 0, sizeof(finfo));
	finfo.i.attributes	= aDIR;
	finfo.i.dataStreamSize	= 0;	/* ignored */
	finfo.i.dirEntNum	= 0;
	finfo.i.DosDirNum	= 0;
#ifdef CONFIG_NCPFS_SMALLDOS
	finfo.i.NSCreator	= NW_NS_DOS;
#endif
	finfo.volume		= NCP_NUMBER_OF_VOLUMES;
	/* set dates of mountpoint to Jan 1, 1986; 00:00 */
	finfo.i.creationTime	= finfo.i.modifyTime
				= cpu_to_le16(0x0000);
	finfo.i.creationDate	= finfo.i.modifyDate
				= finfo.i.lastAccessDate
				= cpu_to_le16(0x0C21);
	finfo.i.nameLen		= 0;
	finfo.i.entryName[0]	= '\0';

	finfo.opened		= 0;
	finfo.ino		= 2;	/* tradition */

	server->name_space[finfo.volume] = NW_NS_DOS;

	error = -ENOMEM;
        root_inode = ncp_iget(sb, &finfo);
        if (!root_inode)
		goto out_disconnect;
	ncp_dbg(1, "root vol=%d\n", NCP_FINFO(root_inode)->volNumber);
	sb->s_root = d_make_root(root_inode);
        if (!sb->s_root)
		goto out_disconnect;
	return 0;

out_disconnect:
	ncp_lock_server(server);
	ncp_disconnect(server);
	ncp_unlock_server(server);
out_rxbuf:
	ncp_stop_tasks(server);
	vfree(server->rxbuf);
out_txbuf:
	vfree(server->txbuf);
out_packet:
	vfree(server->packet);
out_nls:
#ifdef CONFIG_NCPFS_NLS
	unload_nls(server->nls_io);
	unload_nls(server->nls_vol);
#endif
	mutex_destroy(&server->rcv.creq_mutex);
	mutex_destroy(&server->root_setup_lock);
	mutex_destroy(&server->mutex);
out_fput2:
	if (server->info_sock)
		sockfd_put(server->info_sock);
out_bdi:
	bdi_destroy(&server->bdi);
out_fput:
	sockfd_put(sock);
out:
	put_pid(data.wdog_pid);
	sb->s_fs_info = NULL;
	kfree(server);
	return error;
}

static void delayed_free(struct rcu_head *p)
{
	struct ncp_server *server = container_of(p, struct ncp_server, rcu);
#ifdef CONFIG_NCPFS_NLS
	/* unload the NLS charsets */
	unload_nls(server->nls_vol);
	unload_nls(server->nls_io);
#endif /* CONFIG_NCPFS_NLS */
	kfree(server);
}

static void ncp_put_super(struct super_block *sb)
{
	struct ncp_server *server = NCP_SBP(sb);

	ncp_lock_server(server);
	ncp_disconnect(server);
	ncp_unlock_server(server);

	ncp_stop_tasks(server);

	mutex_destroy(&server->rcv.creq_mutex);
	mutex_destroy(&server->root_setup_lock);
	mutex_destroy(&server->mutex);

	if (server->info_sock)
		sockfd_put(server->info_sock);
	sockfd_put(server->ncp_sock);
	kill_pid(server->m.wdog_pid, SIGTERM, 1);
	put_pid(server->m.wdog_pid);

	bdi_destroy(&server->bdi);
	kfree(server->priv.data);
	kfree(server->auth.object_name);
	vfree(server->rxbuf);
	vfree(server->txbuf);
	vfree(server->packet);
	call_rcu(&server->rcu, delayed_free);
}

static int ncp_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct dentry* d;
	struct inode* i;
	struct ncp_inode_info* ni;
	struct ncp_server* s;
	struct ncp_volume_info vi;
	struct super_block *sb = dentry->d_sb;
	int err;
	__u8 dh;
	
	d = sb->s_root;
	if (!d) {
		goto dflt;
	}
	i = d_inode(d);
	if (!i) {
		goto dflt;
	}
	ni = NCP_FINFO(i);
	if (!ni) {
		goto dflt;
	}
	s = NCP_SBP(sb);
	if (!s) {
		goto dflt;
	}
	if (!s->m.mounted_vol[0]) {
		goto dflt;
	}

	err = ncp_dirhandle_alloc(s, ni->volNumber, ni->DosDirNum, &dh);
	if (err) {
		goto dflt;
	}
	err = ncp_get_directory_info(s, dh, &vi);
	ncp_dirhandle_free(s, dh);
	if (err) {
		goto dflt;
	}
	buf->f_type = NCP_SUPER_MAGIC;
	buf->f_bsize = vi.sectors_per_block * 512;
	buf->f_blocks = vi.total_blocks;
	buf->f_bfree = vi.free_blocks;
	buf->f_bavail = vi.free_blocks;
	buf->f_files = vi.total_dir_entries;
	buf->f_ffree = vi.available_dir_entries;
	buf->f_namelen = 12;
	return 0;

	/* We cannot say how much disk space is left on a mounted
	   NetWare Server, because free space is distributed over
	   volumes, and the current user might have disk quotas. So
	   free space is not that simple to determine. Our decision
	   here is to err conservatively. */

dflt:;
	buf->f_type = NCP_SUPER_MAGIC;
	buf->f_bsize = NCP_BLOCK_SIZE;
	buf->f_blocks = 0;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_namelen = 12;
	return 0;
}

int ncp_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int result = 0;
	__le32 info_mask;
	struct nw_modify_dos_info info;
	struct ncp_server *server;

	result = -EIO;

	server = NCP_SERVER(inode);
	if (!server)	/* How this could happen? */
		goto out;

	result = -EPERM;
	if (IS_DEADDIR(d_inode(dentry)))
		goto out;

	/* ageing the dentry to force validation */
	ncp_age_dentry(server, dentry);

	result = setattr_prepare(dentry, attr);
	if (result < 0)
		goto out;

	result = -EPERM;
	if ((attr->ia_valid & ATTR_UID) && !uid_eq(attr->ia_uid, server->m.uid))
		goto out;

	if ((attr->ia_valid & ATTR_GID) && !gid_eq(attr->ia_gid, server->m.gid))
		goto out;

	if (((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode &
	      ~(S_IFREG | S_IFDIR | S_IRWXUGO))))
		goto out;

	info_mask = 0;
	memset(&info, 0, sizeof(info));

#if 1 
        if ((attr->ia_valid & ATTR_MODE) != 0)
        {
		umode_t newmode = attr->ia_mode;

		info_mask |= DM_ATTRIBUTES;

                if (S_ISDIR(inode->i_mode)) {
                	newmode &= server->m.dir_mode;
		} else {
#ifdef CONFIG_NCPFS_EXTRAS			
			if (server->m.flags & NCP_MOUNT_EXTRAS) {
				/* any non-default execute bit set */
				if (newmode & ~server->m.file_mode & S_IXUGO)
					info.attributes |= aSHARED | aSYSTEM;
				/* read for group/world and not in default file_mode */
				else if (newmode & ~server->m.file_mode & S_IRUGO)
					info.attributes |= aSHARED;
			} else
#endif
				newmode &= server->m.file_mode;			
                }
                if (newmode & S_IWUGO)
                	info.attributes &= ~(aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
                else
			info.attributes |=  (aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);

#ifdef CONFIG_NCPFS_NFS_NS
		if (ncp_is_nfs_extras(server, NCP_FINFO(inode)->volNumber)) {
			result = ncp_modify_nfs_info(server,
						     NCP_FINFO(inode)->volNumber,
						     NCP_FINFO(inode)->dirEntNum,
						     attr->ia_mode, 0);
			if (result != 0)
				goto out;
			info.attributes &= ~(aSHARED | aSYSTEM);
			{
				/* mark partial success */
				struct iattr tmpattr;
				
				tmpattr.ia_valid = ATTR_MODE;
				tmpattr.ia_mode = attr->ia_mode;

				setattr_copy(inode, &tmpattr);
				mark_inode_dirty(inode);
			}
		}
#endif
        }
#endif

	/* Do SIZE before attributes, otherwise mtime together with size does not work...
	 */
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		int written;

		ncp_dbg(1, "trying to change size to %llu\n", attr->ia_size);

		if ((result = ncp_make_open(inode, O_WRONLY)) < 0) {
			result = -EACCES;
			goto out;
		}
		ncp_write_kernel(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle,
			  attr->ia_size, 0, "", &written);

		/* According to ndir, the changes only take effect after
		   closing the file */
		ncp_inode_close(inode);
		result = ncp_make_closed(inode);
		if (result)
			goto out;

		if (attr->ia_size != i_size_read(inode)) {
			truncate_setsize(inode, attr->ia_size);
			mark_inode_dirty(inode);
		}
	}
	if ((attr->ia_valid & ATTR_CTIME) != 0) {
		info_mask |= (DM_CREATE_TIME | DM_CREATE_DATE);
		ncp_date_unix2dos(attr->ia_ctime.tv_sec,
			     &info.creationTime, &info.creationDate);
	}
	if ((attr->ia_valid & ATTR_MTIME) != 0) {
		info_mask |= (DM_MODIFY_TIME | DM_MODIFY_DATE);
		ncp_date_unix2dos(attr->ia_mtime.tv_sec,
				  &info.modifyTime, &info.modifyDate);
	}
	if ((attr->ia_valid & ATTR_ATIME) != 0) {
		__le16 dummy;
		info_mask |= (DM_LAST_ACCESS_DATE);
		ncp_date_unix2dos(attr->ia_atime.tv_sec,
				  &dummy, &info.lastAccessDate);
	}
	if (info_mask != 0) {
		result = ncp_modify_file_or_subdir_dos_info(NCP_SERVER(inode),
				      inode, info_mask, &info);
		if (result != 0) {
			if (info_mask == (DM_CREATE_TIME | DM_CREATE_DATE)) {
				/* NetWare seems not to allow this. I
				   do not know why. So, just tell the
				   user everything went fine. This is
				   a terrible hack, but I do not know
				   how to do this correctly. */
				result = 0;
			} else
				goto out;
		}
#ifdef CONFIG_NCPFS_STRONG		
		if ((!result) && (info_mask & DM_ATTRIBUTES))
			NCP_FINFO(inode)->nwattr = info.attributes;
#endif
	}
	if (result)
		goto out;

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);

out:
	if (result > 0)
		result = -EACCES;
	return result;
}

static struct dentry *ncp_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, ncp_fill_super);
}

static struct file_system_type ncp_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ncpfs",
	.mount		= ncp_mount,
	.kill_sb	= kill_anon_super,
	.fs_flags	= FS_BINARY_MOUNTDATA,
};
MODULE_ALIAS_FS("ncpfs");

static int __init init_ncp_fs(void)
{
	int err;
	ncp_dbg(1, "called\n");

	err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&ncp_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_ncp_fs(void)
{
	ncp_dbg(1, "called\n");
	unregister_filesystem(&ncp_fs_type);
	destroy_inodecache();
}

module_init(init_ncp_fs)
module_exit(exit_ncp_fs)
MODULE_LICENSE("GPL");
