/*
 * linux/fs/lockd/svcsubs.c
 *
 * Various support routines for the NLM server.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/in.h>
#include <linux/mutex.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/export.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/share.h>
#include <linux/lockd/sm_inter.h>

#define NLMDBG_FACILITY		NLMDBG_SVCSUBS


/*
 * Global file hash table
 */
#define FILE_HASH_BITS		5
#define FILE_NRHASH		(1<<FILE_HASH_BITS)
static struct nlm_file *	nlm_files[FILE_NRHASH];
static DEFINE_MUTEX(nlm_file_mutex);

#ifdef NFSD_DEBUG
static inline void nlm_debug_print_fh(char *msg, struct nfs_fh *f)
{
	u32 *fhp = (u32*)f->data;

	/* print the first 32 bytes of the fh */
	dprintk("lockd: %s (%08x %08x %08x %08x %08x %08x %08x %08x)\n",
		msg, fhp[0], fhp[1], fhp[2], fhp[3],
		fhp[4], fhp[5], fhp[6], fhp[7]);
}

static inline void nlm_debug_print_file(char *msg, struct nlm_file *file)
{
	struct inode *inode = file->f_file->f_dentry->d_inode;

	dprintk("lockd: %s %s/%ld\n",
		msg, inode->i_sb->s_id, inode->i_ino);
}
#else
static inline void nlm_debug_print_fh(char *msg, struct nfs_fh *f)
{
	return;
}

static inline void nlm_debug_print_file(char *msg, struct nlm_file *file)
{
	return;
}
#endif

static inline unsigned int file_hash(struct nfs_fh *f)
{
	unsigned int tmp=0;
	int i;
	for (i=0; i<NFS2_FHSIZE;i++)
		tmp += f->data[i];
	return tmp & (FILE_NRHASH - 1);
}

/*
 * Lookup file info. If it doesn't exist, create a file info struct
 * and open a (VFS) file for the given inode.
 *
 * FIXME:
 * Note that we open the file O_RDONLY even when creating write locks.
 * This is not quite right, but for now, we assume the client performs
 * the proper R/W checking.
 */
u32
nlm_lookup_file(struct svc_rqst *rqstp, struct nlm_file **result,
					struct nfs_fh *f)
{
	struct nlm_file	*file;
	unsigned int	hash;
	u32		nfserr;

	nlm_debug_print_fh("nlm_file_lookup", f);

	hash = file_hash(f);

	/* Lock file table */
	mutex_lock(&nlm_file_mutex);

	for (file = nlm_files[hash]; file; file = file->f_next)
		if (!nfs_compare_fh(&file->f_handle, f))
			goto found;

	nlm_debug_print_fh("creating file for", f);

	nfserr = nlm_lck_denied_nolocks;
	file = (struct nlm_file *) kmalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		goto out_unlock;

	memset(file, 0, sizeof(*file));
	memcpy(&file->f_handle, f, sizeof(struct nfs_fh));
	file->f_hash = hash;
	init_MUTEX(&file->f_sema);

	/* Open the file. Note that this must not sleep for too long, else
	 * we would lock up lockd:-) So no NFS re-exports, folks.
	 *
	 * We have to make sure we have the right credential to open
	 * the file.
	 */
	if ((nfserr = nlmsvc_ops->fopen(rqstp, f, &file->f_file)) != 0) {
		dprintk("lockd: open failed (nfserr %d)\n", ntohl(nfserr));
		goto out_free;
	}

	file->f_next = nlm_files[hash];
	nlm_files[hash] = file;

found:
	dprintk("lockd: found file %p (count %d)\n", file, file->f_count);
	*result = file;
	file->f_count++;
	nfserr = 0;

out_unlock:
	mutex_unlock(&nlm_file_mutex);
	return nfserr;

out_free:
	kfree(file);
#ifdef CONFIG_LOCKD_V4
	if (nfserr == 1)
		nfserr = nlm4_stale_fh;
	else
#endif
	nfserr = nlm_lck_denied;
	goto out_unlock;
}

/*
 * Delete a file after having released all locks, blocks and shares
 */
static inline void
nlm_delete_file(struct nlm_file *file)
{
	struct nlm_file	**fp, *f;

	nlm_debug_print_file("closing file", file);

	fp = nlm_files + file->f_hash;
	while ((f = *fp) != NULL) {
		if (f == file) {
			*fp = file->f_next;
			nlmsvc_ops->fclose(file->f_file);
			kfree(file);
			return;
		}
		fp = &f->f_next;
	}

	printk(KERN_WARNING "lockd: attempt to release unknown file!\n");
}

/*
 * Loop over all locks on the given file and perform the specified
 * action.
 */
static int
nlm_traverse_locks(struct nlm_host *host, struct nlm_file *file, int action)
{
	struct inode	 *inode = nlmsvc_file_inode(file);
	struct file_lock *fl;
	struct nlm_host	 *lockhost;

again:
	file->f_locks = 0;
	for (fl = inode->i_flock; fl; fl = fl->fl_next) {
		if (fl->fl_lmops != &nlmsvc_lock_operations)
			continue;

		/* update current lock count */
		file->f_locks++;
		lockhost = (struct nlm_host *) fl->fl_owner;
		if (action == NLM_ACT_MARK)
			lockhost->h_inuse = 1;
		else if (action == NLM_ACT_CHECK)
			return 1;
		else if (action == NLM_ACT_UNLOCK) {
			struct file_lock lock = *fl;

			if (host && lockhost != host)
				continue;

			lock.fl_type  = F_UNLCK;
			lock.fl_start = 0;
			lock.fl_end   = OFFSET_MAX;
			if (posix_lock_file(file->f_file, &lock) < 0) {
				printk("lockd: unlock failure in %s:%d\n",
						__FILE__, __LINE__);
				return 1;
			}
			goto again;
		}
	}

	return 0;
}

/*
 * Operate on a single file
 */
static inline int
nlm_inspect_file(struct nlm_host *host, struct nlm_file *file, int action)
{
	if (action == NLM_ACT_CHECK) {
		/* Fast path for mark and sweep garbage collection */
		if (file->f_count || file->f_blocks || file->f_shares)
			return 1;
	} else {
		nlmsvc_traverse_blocks(host, file, action);
		nlmsvc_traverse_shares(host, file, action);
	}
	return nlm_traverse_locks(host, file, action);
}

/*
 * Loop over all files in the file table.
 */
static int
nlm_traverse_files(struct nlm_host *host, int action)
{
	struct nlm_file	*file, **fp;
	int		i;

	mutex_lock(&nlm_file_mutex);
	for (i = 0; i < FILE_NRHASH; i++) {
		fp = nlm_files + i;
		while ((file = *fp) != NULL) {
			/* Traverse locks, blocks and shares of this file
			 * and update file->f_locks count */
			if (nlm_inspect_file(host, file, action)) {
				mutex_unlock(&nlm_file_mutex);
				return 1;
			}

			/* No more references to this file. Let go of it. */
			if (!file->f_blocks && !file->f_locks
			 && !file->f_shares && !file->f_count) {
				*fp = file->f_next;
				nlmsvc_ops->fclose(file->f_file);
				kfree(file);
			} else {
				fp = &file->f_next;
			}
		}
	}
	mutex_unlock(&nlm_file_mutex);
	return 0;
}

/*
 * Release file. If there are no more remote locks on this file,
 * close it and free the handle.
 *
 * Note that we can't do proper reference counting without major
 * contortions because the code in fs/locks.c creates, deletes and
 * splits locks without notification. Our only way is to walk the
 * entire lock list each time we remove a lock.
 */
void
nlm_release_file(struct nlm_file *file)
{
	dprintk("lockd: nlm_release_file(%p, ct = %d)\n",
				file, file->f_count);

	/* Lock file table */
	mutex_lock(&nlm_file_mutex);

	/* If there are no more locks etc, delete the file */
	if(--file->f_count == 0) {
		if(!nlm_inspect_file(NULL, file, NLM_ACT_CHECK))
			nlm_delete_file(file);
	}

	mutex_unlock(&nlm_file_mutex);
}

/*
 * Mark all hosts that still hold resources
 */
void
nlmsvc_mark_resources(void)
{
	dprintk("lockd: nlmsvc_mark_resources\n");

	nlm_traverse_files(NULL, NLM_ACT_MARK);
}

/*
 * Release all resources held by the given client
 */
void
nlmsvc_free_host_resources(struct nlm_host *host)
{
	dprintk("lockd: nlmsvc_free_host_resources\n");

	if (nlm_traverse_files(host, NLM_ACT_UNLOCK))
		printk(KERN_WARNING
			"lockd: couldn't remove all locks held by %s",
			host->h_name);
}

/*
 * delete all hosts structs for clients
 */
void
nlmsvc_invalidate_all(void)
{
	struct nlm_host *host;
	while ((host = nlm_find_client()) != NULL) {
		nlmsvc_free_host_resources(host);
		host->h_expires = 0;
		host->h_killed = 1;
		nlm_release_host(host);
	}
}
