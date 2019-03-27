/*
 * $FreeBSD$
 *
 * Copyright (c) 2011, 2012, 2013, 2015, 2016, Juniper Networks, Inc.
 * All rights reserved.
 *
 * Originally derived from:
 *	$NetBSD: kern_verifiedexec.c,v 1.7 2003/11/18 13:13:03 martin Exp $
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#include "opt_mac.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/vnode.h>

#include "mac_veriexec.h"
#include "mac_veriexec_internal.h"

/**
 * @brief per-device meta-data storage
 */
struct veriexec_dev_list {
	dev_t fsid;	/**< file system identifier of the mount point */
	LIST_HEAD(filehead, mac_veriexec_file_info) file_head;
	    /**< list of per-file meta-data information */
	LIST_ENTRY(veriexec_dev_list) entries;
	    /**< next entries in the device list */
};

typedef LIST_HEAD(veriexec_devhead, veriexec_dev_list) veriexec_devhead_t;

/**
 * @brief Mutex to protect the meta-data store lists
 */
struct mtx ve_mutex;

/**
 * @brief Executables meta-data storage
 *
 * This is used to store the fingerprints for potentially-executable files.
 */
veriexec_devhead_t veriexec_dev_head;

/**
 * @brief Plain file meta-data storage
 *
 * This is used for files that are not allowed to be executed, but should
 * have fingerprint validation available.
 */
veriexec_devhead_t veriexec_file_dev_head;

/**
 * @internal
 * @brief Search the @p head meta-data list for the specified file identifier
 *     @p fileid in the file system identified by @p fsid
 *
 * If meta-data exists for file system identified by @p fsid, it has a
 * fingerprint list, and @p found_dev is not @c NULL then store true in the
 * location pointed to by @p found_dev
 *
 * @param head		meta-data list to search
 * @param fsid		file system identifier to look for
 * @param fileid	file to look for
 * @param gen		generation of file
 * @param found_dev	indicator that an entry for the file system was found
 *
 * @return A pointer to the meta-data inforation if meta-data exists for
 *     the specified file identifier, otherwise @c NULL
 */
static struct mac_veriexec_file_info *
get_veriexec_file(struct veriexec_devhead *head, dev_t fsid, long fileid,
    unsigned long gen, int *found_dev)
{
	struct veriexec_dev_list *lp;
	struct mac_veriexec_file_info *ip, *tip;

	ip = NULL;

	/* Initialize the value found_dev, if non-NULL */
	if (found_dev != NULL)
		*found_dev = 0;

	VERIEXEC_DEBUG(3, ("searching for file %ju.%lu on device %ju,"
	    " files=%d\n", (uintmax_t)fileid, gen, (uintmax_t)fsid,
	    (head == &veriexec_file_dev_head)));

	/* Get a lock to access the list */
	mtx_lock(&ve_mutex);

	/* First, look for the file system */
	for (lp = LIST_FIRST(head); lp != NULL; lp = LIST_NEXT(lp, entries))
		if (lp->fsid == fsid)
			break;

	/* We found the file system in the list */
	if (lp != NULL) {
		VERIEXEC_DEBUG(3, ("found matching dev number %ju\n",
		    (uintmax_t)lp->fsid));

		/* If found_dev is non-NULL, store true there */
		if (found_dev != NULL)
			*found_dev = 1;

		/* Next, look for the meta-data information for the file */
		LIST_FOREACH_SAFE(ip, &(lp->file_head), entries, tip) {
			if (ip->fileid == fileid) {
				if (ip->gen == gen)
					break;
				/* we need to garbage collect */
				LIST_REMOVE(ip, entries);
				free(ip, M_VERIEXEC);
			}
		}
	}

	/* Release the lock we obtained earlier */
	mtx_unlock(&ve_mutex);

	/* Return the meta-data information we found, if anything */
	return (ip);
}

/**
 * @internal
 * @brief Search the meta-data store for information on the specified file.
 *
 * @param fsid		file system identifier to look for
 * @param fileid	file to look for
 * @param gen		generation of file
 * @param found_dev	indicator that an entry for the file system was found
 * @param check_files	if 1, check the files list first, otherwise check the
 * 			exectuables list first
 *
 * @return A pointer to the meta-data inforation if meta-data exists for
 *     the specified file identifier, otherwise @c NULL
 */
static struct mac_veriexec_file_info *
find_veriexec_file(dev_t fsid, long fileid, unsigned long gen, int *found_dev,
    int check_files)
{
	struct veriexec_devhead *search[3];
	struct mac_veriexec_file_info *ip;
	int x;

	/* Determine the order of the lists to search */
	if (check_files) {
		search[0] = &veriexec_file_dev_head;
		search[1] = &veriexec_dev_head;
	} else {
		search[0] = &veriexec_dev_head;
		search[1] = &veriexec_file_dev_head;
	}
	search[2] = NULL;

	VERIEXEC_DEBUG(3, ("%s: searching for dev %ju, file %lu\n",
	    __func__, (uintmax_t)fsid, fileid));

	/* Search for the specified file */
	for (ip = NULL, x = 0; ip == NULL && search[x]; x++)
		ip = get_veriexec_file(search[x], fsid, fileid, gen, found_dev);

	return (ip);
}

/**
 * @internal
 * @brief Display the fingerprint for each entry in the device list
 *
 * @param sbp		sbuf to write output to
 * @param lp		pointer to device list
 */
static void
mac_veriexec_print_db_dev_list(struct sbuf *sbp, struct veriexec_dev_list *lp)
{
	struct mac_veriexec_file_info *ip;

#define FPB(i) (ip->fingerprint[i])
	for (ip = LIST_FIRST(&(lp->file_head)); ip != NULL;
	    ip = LIST_NEXT(ip, entries))
		sbuf_printf(sbp, "  %ld: %u %ld [%02x %02x %02x %02x %02x "
		    "%02x %02x %02x...]\n", ip->fileid, ip->flags, ip->gen,
		    FPB(0), FPB(1), FPB(2), FPB(3), FPB(4), FPB(5), FPB(6),
		    FPB(7));
}

/**
 * @internal
 * @brief Display the device list
 *
 * @param sbp		sbuf to write output to
 * @param head		pointer to head of the device list
 */
static void
mac_veriexec_print_db_head(struct sbuf *sbp, struct veriexec_devhead *head)
{
	struct veriexec_dev_list *lp;

	for (lp = LIST_FIRST(head); lp != NULL; lp = LIST_NEXT(lp, entries)) {
		sbuf_printf(sbp, " FS id: %ju\n", (uintmax_t)lp->fsid);
		mac_veriexec_print_db_dev_list(sbp, lp);
	}

}

/**
 * @internal
 * @brief Generate human-readable output for the current fingerprint database
 *
 * @param sbp	sbuf to write output to
 */
void
mac_veriexec_metadata_print_db(struct sbuf *sbp)
{
	struct {
		struct veriexec_devhead *h;
		const char *name;
	} fpdbs[] = {
		{ &veriexec_file_dev_head, "regular files" },
		{ &veriexec_dev_head, "executable files" },
	};
	int i;

	mtx_lock(&ve_mutex);
	for (i = 0; i < sizeof(fpdbs)/sizeof(fpdbs[0]); i++) {
		sbuf_printf(sbp, "%s fingerprint db:\n", fpdbs[i].name);
		mac_veriexec_print_db_head(sbp, fpdbs[i].h);
	}
	mtx_unlock(&ve_mutex);
}
/**
 * @brief Determine if the meta-data store has an entry for the specified file.
 *
 * @param fsid		file system identifier to look for
 * @param fileid	file to look for
 * @param gen		generation of file
 *
 * @return 1 if there is an entry in the meta-data store, 0 otherwise.
 */
int
mac_veriexec_metadata_has_file(dev_t fsid, long fileid, unsigned long gen)
{

	return (find_veriexec_file(fsid, fileid, gen, NULL,
	    VERIEXEC_FILES_FIRST) != NULL);
}

/**
 * @brief Search the list of devices looking for the one given, in order to
 *     release the resources used by it.
 *
 * If found, free all file entries for it, and remove it from the list.
 *
 * @note Called with @a ve_mutex held
 *
 * @param fsid		file system identifier to look for
 * @param head		meta-data list to search
 *
 * @return 0 if the device entry was freed, otherwise an error code
 */
static int
free_veriexec_dev(dev_t fsid, struct veriexec_devhead *head)
{
	struct veriexec_dev_list *lp;
	struct mac_veriexec_file_info *ip, *nip;

	/* Look for the file system */
	for (lp = LIST_FIRST(head); lp != NULL;
	     lp = LIST_NEXT(lp, entries))
		if (lp->fsid == fsid) break;

	/* If lp is NULL, we did not find it */
	if (lp == NULL)
		return ENOENT;

	/* Unhook lp, before we free it and its content */
	LIST_REMOVE(lp, entries);

	/* Release the lock */
	mtx_unlock(&ve_mutex);

	/* Free the file entries in the list */
	for (ip = LIST_FIRST(&(lp->file_head)); ip != NULL; ip = nip) {
		nip = LIST_NEXT(ip, entries);
		LIST_REMOVE(ip, entries);
		free(ip, M_VERIEXEC);
	}

	/* Free the meta-data entry for the device */
	free(lp, M_VERIEXEC);

	/* Re-acquire the lock */
	mtx_lock(&ve_mutex);
	return 0;
}

/**
 * @brief Search the list of devices looking for the one given.
 *
 * If it is not in the list then add it.
 *
 * @note Called with @a ve_mutex held
 *
 * @param fsid		file system identifier to look for
 * @param head		meta-data list to search
 *
 * @return A pointer to the meta-data entry for the device, if found or added,
 *     otherwise @c NULL
 */
static struct veriexec_dev_list *
find_veriexec_dev(dev_t fsid, struct veriexec_devhead *head)
{
	struct veriexec_dev_list *lp;
	struct veriexec_dev_list *np = NULL;

search:
	/* Look for the file system */
	for (lp = LIST_FIRST(head); lp != NULL;
	     lp = LIST_NEXT(lp, entries))
		if (lp->fsid == fsid) break;

	if (lp == NULL) {
		if (np == NULL) {
			/*
			 * If pointer is null then entry not there,
			 * add a new one, first try to malloc while
			 * we hold mutex - should work most of the time.
			 */
			np = malloc(sizeof(struct veriexec_dev_list),
			    M_VERIEXEC, M_NOWAIT);
			if (np == NULL) {
				/*
				 * So much for that plan, dop the mutex
				 * and repeat...
				 */
				mtx_unlock(&ve_mutex);
				np = malloc(sizeof(struct veriexec_dev_list),
				    M_VERIEXEC, M_WAITOK);
				mtx_lock(&ve_mutex);
				/*
				 * Repeat the seach, in case someone
				 * added this while we slept.
				 */
				goto search;
			}
		}
		if (np) {
			/* Add the entry to the list */
			lp = np;
			LIST_INIT(&(lp->file_head));
			lp->fsid = fsid;
			LIST_INSERT_HEAD(head, lp, entries);
		}
	} else if (np) {
		/*
		 * Someone else did it while we slept.
		 */
		mtx_unlock(&ve_mutex);
		free(np, M_VERIEXEC);
		mtx_lock(&ve_mutex);
	}

	return (lp);
}

/**
 * @brief When a device is unmounted, we want to toss the signatures recorded
 *     against it.
 *
 * We are being called from unmount() with the root vnode just before it is
 * freed.
 *
 * @param fsid		file system identifier to look for
 * @param td		calling thread
 *
 * @return 0 on success, otherwise an error code.
 */
int
mac_veriexec_metadata_unmounted(dev_t fsid, struct thread *td)
{
    int error;

    /*
     * The device can have entries on both lists.
     */
    mtx_lock(&ve_mutex);
    error = free_veriexec_dev(fsid, &veriexec_dev_head);
    if (error && error != ENOENT) {
	    mtx_unlock(&ve_mutex);
	    return error;
    }
    error = free_veriexec_dev(fsid, &veriexec_file_dev_head);
    mtx_unlock(&ve_mutex);
    if (error && error != ENOENT) {
	    return error;
    }
    return 0;
}

/**
 * @brief Return the flags assigned to the file identified by file system
 * 	  identifier @p fsid and file identifier @p fileid.
 *
 * @param fsid		file system identifier
 * @param fileid	file identifier within the file system
 * @param gen		generation of file
 * @param flags		pointer to location to store the flags
 * @param check_files	if 1, check the files list first, otherwise check the
 * 			exectuables list first
 *
 * @return 0 on success, otherwise an error code.
 */
int
mac_veriexec_metadata_get_file_flags(dev_t fsid, long fileid, unsigned long gen,
    int *flags, int check_files)
{
	struct mac_veriexec_file_info *ip;
	int found_dev;

	ip = find_veriexec_file(fsid, fileid, gen, &found_dev, check_files);
	if (ip == NULL)
		return (ENOENT);

	*flags = ip->flags;
	return (0);
}

/**
 * @brief get the files for the specified process
 *
 * @param cred		credentials to use
 * @param p		process to get the flags for
 * @param flags		where to store the flags
 * @param check_files	if 1, check the files list first, otherwise check the
 * 			exectuables list first
 *
 * @return 0 if the process has an entry in the meta-data store, otherwise an
 *     error code
 */
int
mac_veriexec_metadata_get_executable_flags(struct ucred *cred, struct proc *p,
    int *flags, int check_files)
{
	struct vnode *proc_vn;
	struct vattr vap;
	int error;

	/* Get the text vnode for the process */
	proc_vn = p->p_textvp;
	if (proc_vn == NULL)
		return EINVAL;

	/* Get vnode attributes */
	error = VOP_GETATTR(proc_vn, &vap, cred);
	if (error)
		return error;

	error = mac_veriexec_metadata_get_file_flags(vap.va_fsid,
	    vap.va_fileid, vap.va_gen, flags,
	    (check_files == VERIEXEC_FILES_FIRST));

	return (error);
}

/**
 * @brief Ensure the fingerprint status for the vnode @p vp is assigned to its
 *     MAC label.
 *
 * @param vp		vnode to check
 * @param vap		vnode attributes to use
 * @param td		calling thread
 * @param check_files	if 1, check the files list first, otherwise check the
 * 			exectuables list first
 *
 * @return 0 on success, otherwise an error code.
 */
int
mac_veriexec_metadata_fetch_fingerprint_status(struct vnode *vp,
    struct vattr *vap, struct thread *td, int check_files)
{
	unsigned char digest[MAXFINGERPRINTLEN];
	struct mac_veriexec_file_info *ip;
	int error, found_dev;
	fingerprint_status_t status;

	error = 0;
	ip = NULL;

	status = mac_veriexec_get_fingerprint_status(vp);
	if (status == FINGERPRINT_INVALID || status == FINGERPRINT_NODEV) {
		found_dev = 0;
		ip = find_veriexec_file(vap->va_fsid, vap->va_fileid,
		    vap->va_gen, &found_dev, check_files);
		if (ip == NULL) {
			status = (found_dev) ? FINGERPRINT_NOENTRY :
			    FINGERPRINT_NODEV;
			VERIEXEC_DEBUG(3,
			    ("fingerprint status is %d for dev %ju, file "
			    "%ju.%lu\n", status, (uintmax_t)vap->va_fsid,
			    (uintmax_t)vap->va_fileid, vap->va_gen));
		} else {
			/*
			 * evaluate and compare fingerprint
			 */
			error = mac_veriexec_fingerprint_check_vnode(vp, ip,
			    td, vap->va_size, digest);
			switch (error) {
			case 0:
				/* Process flags */
				if ((ip->flags & VERIEXEC_INDIRECT))
					status = FINGERPRINT_INDIRECT;
				else if ((ip->flags & VERIEXEC_FILE))
					status = FINGERPRINT_FILE;
				else
					status = FINGERPRINT_VALID;
				VERIEXEC_DEBUG(2,
				    ("%sfingerprint matches for dev %ju, file "
				    "%ju.%lu\n",
				     (status == FINGERPRINT_INDIRECT) ?
				     "indirect " :
				     (status == FINGERPRINT_FILE) ?
				     "file " : "", (uintmax_t)vap->va_fsid,
				     (uintmax_t)vap->va_fileid, vap->va_gen));
				break;

			case EAUTH:
#ifdef VERIFIED_EXEC_DEBUG_VERBOSE
				{
					char have[MAXFINGERPRINTLEN * 2 + 1];
					char want[MAXFINGERPRINTLEN * 2 + 1];
					int i, len;

					len = ip->ops->digest_len;
					for (i = 0; i < len; i++) {
						sprintf(&want[i * 2], "%02x",
						    ip->fingerprint[i]);
						sprintf(&have[i * 2], "%02x",
						    digest[i]);
					}
					log(LOG_ERR, MAC_VERIEXEC_FULLNAME
					    ": fingerprint for dev %ju, file "
					    "%ju.%lu %s != %s\n",
					    (uintmax_t)vap->va_fsid,
					    (uintmax_t)vap->va_fileid,
					    vap->va_gen,
					    have, want);
				}
#endif
				status = FINGERPRINT_NOMATCH;
				break;
			default:
				VERIEXEC_DEBUG(2,
				    ("fingerprint status error %d\n", error));
				break;
			}
		}
		mac_veriexec_set_fingerprint_status(vp, status);
	}
	return (error);
}

/**
 * Add a file and its fingerprint to the list of files attached
 * to the device @p fsid.
 *
 * Only add the entry if it is not already on the list.
 *
 * @note Called with @a ve_mutex held
 *
 * @param file_dev	if 1, the entry should be added on the file list,
 * 			otherwise it should be added on the executable list
 * @param fsid		file system identifier of device
 * @param fileid	file to add
 * @param gen		generation of file
 * @param fingerprint	fingerprint to add to the store
 * @param flags		flags to set in the store
 * @param fp_type	digest type
 * @param override	if 1, override any values already stored
 *
 * @return 0 on success, otherwise an error code.
 */
int
mac_veriexec_metadata_add_file(int file_dev, dev_t fsid, long fileid,
    unsigned long gen, unsigned char fingerprint[MAXFINGERPRINTLEN],
    int flags, const char *fp_type, int override)
{
	struct mac_veriexec_fpops *fpops;
	struct veriexec_dev_list *lp;
	struct veriexec_devhead *head;
	struct mac_veriexec_file_info *ip;
	struct mac_veriexec_file_info *np = NULL;

	/* Look up the device entry */
	if (file_dev)
		head = &veriexec_file_dev_head;
	else
		head = &veriexec_dev_head;
	lp = find_veriexec_dev(fsid, head);

	/* Look up the fingerprint operations for the digest type */
	fpops = mac_veriexec_fingerprint_lookup_ops(fp_type);
	if (fpops == NULL)
		return (EOPNOTSUPP);

search:
	for (ip = LIST_FIRST(&(lp->file_head)); ip != NULL;
	     ip = LIST_NEXT(ip, entries)) {
		  /* check for a dupe file in the list, skip if an entry
		   * exists for this file except for when the flags contains
		   * VERIEXEC_INDIRECT, always set the flags when it is so
		   * we don't get a hole caused by conflicting flags on
		   * hardlinked files.  XXX maybe we should validate
		   * fingerprint is same and complain if it is not...
		   */
		if (ip->fileid == fileid && ip->gen == gen) {
			if (override) {
				/*
				 * for a signed load we allow overrides,
				 * otherwise fingerpints needed for pkg loads
				 * can fail (the files are on temp device).
				 */
				ip->flags = flags;
				ip->ops = fpops;
				memcpy(ip->fingerprint, fingerprint,
				    fpops->digest_len);
			} else if ((flags & (VERIEXEC_INDIRECT|VERIEXEC_FILE)))
				ip->flags |= flags;

			if (np) {
				/* unlikely but... we don't need it now. */
				mtx_unlock(&ve_mutex);
				free(np, M_VERIEXEC);
				mtx_lock(&ve_mutex);
			}
			return (0);
		}
	}

	/*
	 * We may have been past here before...
	 */
	if (np == NULL) {
		/*
		 * We first try with mutex held and nowait.
		 */
		np = malloc(sizeof(struct mac_veriexec_file_info), M_VERIEXEC,
		    M_NOWAIT);
		if (np == NULL) {
			/*
			 * It was worth a try, now
			 * drop mutex while we malloc.
			 */
			mtx_unlock(&ve_mutex);
			np = malloc(sizeof(struct mac_veriexec_file_info),
			    M_VERIEXEC, M_WAITOK);
			mtx_lock(&ve_mutex);
			/*
			 * We now have to repeat our search!
			 */
			goto search;
		}
	}

	/* Set up the meta-data entry */
	ip = np;
	ip->flags = flags;
	ip->ops = fpops;
	ip->fileid = fileid;
	ip->gen = gen;
	memcpy(ip->fingerprint, fingerprint, fpops->digest_len);

	VERIEXEC_DEBUG(3, ("add file %ju.%lu (files=%d)\n",
	    (uintmax_t)ip->fileid,
	    ip->gen, file_dev));

	/* Add the entry to the list */
	LIST_INSERT_HEAD(&(lp->file_head), ip, entries);
#ifdef DEBUG_VERIEXEC_FINGERPRINT
	{
		off_t offset;

		printf("Stored %s fingerprint:\n", fp_type);
		for (offset = 0; offset < fpops->digest_len; offset++)
			printf("%02x", fingerprint[offset]);
		printf("\n");
	}
#endif
	return (0);
}

/**
 * @brief Intialize the meta-data store
 */
void
mac_veriexec_metadata_init(void)
{

	mtx_init(&ve_mutex, "veriexec lock", NULL, MTX_DEF);
	LIST_INIT(&veriexec_dev_head);
	LIST_INIT(&veriexec_file_dev_head);
}
