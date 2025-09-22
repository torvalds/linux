/*	$OpenBSD: ntfs_subr.c,v 1.53 2025/01/13 13:58:41 claudio Exp $	*/
/*	$NetBSD: ntfs_subr.c,v 1.4 2003/04/10 21:37:32 jdolecek Exp $	*/

/*-
 * Copyright (c) 1998, 1999 Semen Ustimenko (semenu@FreeBSD.org)
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	Id: ntfs_subr.c,v 1.4 1999/05/12 09:43:01 semenu Exp
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>
#include <sys/specdev.h>

/* #define NTFS_DEBUG 1 */
#include <ntfs/ntfs.h>
#include <ntfs/ntfsmount.h>
#include <ntfs/ntfs_inode.h>
#include <ntfs/ntfs_vfsops.h>
#include <ntfs/ntfs_subr.h>
#include <ntfs/ntfs_compr.h>
#include <ntfs/ntfs_ihash.h>

#if defined(NTFS_DEBUG)
int ntfs_debug = NTFS_DEBUG;
#endif

/* Local struct used in ntfs_ntlookupfile() */
struct ntfs_lookup_ctx {
	u_int32_t	aoff;
	u_int32_t	rdsize;
	cn_t		cn;
	struct ntfs_lookup_ctx *prev;
};

int ntfs_ntlookupattr(struct ntfsmount *, const char *, int, int *, char **);
int ntfs_findvattr(struct ntfsmount *, struct ntnode *, struct ntvattr **, struct ntvattr **, u_int32_t, const char *, size_t, cn_t);
int ntfs_uastricmp(struct ntfsmount *, const wchar *, size_t, const char *, size_t);
int ntfs_uastrcmp(struct ntfsmount *, const wchar *, size_t, const char *, size_t);

/* table for mapping Unicode chars into uppercase; it's filled upon first
 * ntfs mount, freed upon last ntfs umount */
static wchar *ntfs_toupper_tab;
#define NTFS_U28(ch)		((((ch) & 0xE0) == 0) ? '_' : (ch) & 0xFF)
#define NTFS_TOUPPER(ch)	(ntfs_toupper_tab[(unsigned char)(ch)])
struct rwlock ntfs_toupper_lock = RWLOCK_INITIALIZER("ntfs_toupper");
static signed int ntfs_toupper_usecount;

/* support macro for ntfs_ntvattrget() */
#define NTFS_AALPCMP(aalp,type,name,namelen) (				\
  (aalp->al_type == type) && (aalp->al_namelen == namelen) &&		\
  !ntfs_uastrcmp(ntmp, aalp->al_name,aalp->al_namelen,name,namelen) )

/*
 * 
 */
int
ntfs_ntvattrrele(struct ntvattr *vap)
{
	DPRINTF("ntfs_ntvattrrele: ino: %u, type: 0x%x\n",
	    vap->va_ip->i_number, vap->va_type);

	ntfs_ntrele(vap->va_ip);

	return (0);
}

/*
 * find the attribute in the ntnode
 */
int
ntfs_findvattr(struct ntfsmount *ntmp, struct ntnode *ip,
    struct ntvattr **lvapp, struct ntvattr **vapp, u_int32_t type,
    const char *name, size_t namelen, cn_t vcn)
{
	int error;
	struct ntvattr *vap;

	if((ip->i_flag & IN_LOADED) == 0) {
		DPRINTF("ntfs_findvattr: node not loaded, ino: %u\n",
		    ip->i_number);
		error = ntfs_loadntnode(ntmp,ip);
		if (error) {
			printf("ntfs_findvattr: FAILED TO LOAD INO: %d\n",
			       ip->i_number);
			return (error);
		}
	} else {
		/* Update LRU loaded list. */
		TAILQ_REMOVE(&ntmp->ntm_ntnodeq, ip, i_loaded);
		TAILQ_INSERT_HEAD(&ntmp->ntm_ntnodeq, ip, i_loaded);
	}

	*lvapp = NULL;
	*vapp = NULL;
	LIST_FOREACH(vap, &ip->i_valist, va_list) {
		DDPRINTF("ntfs_findvattr: type: 0x%x, vcn: %llu - %llu\n",
		    vap->va_type, vap->va_vcnstart, vap->va_vcnend);
		if ((vap->va_type == type) &&
		    (vap->va_vcnstart <= vcn) && (vap->va_vcnend >= vcn) &&
		    (vap->va_namelen == namelen) &&
		    (strncmp(name, vap->va_name, namelen) == 0)) {
			*vapp = vap;
			ntfs_ntref(vap->va_ip);
			return (0);
		}
		if (vap->va_type == NTFS_A_ATTRLIST)
			*lvapp = vap;
	}

	return (-1);
}

/*
 * Search attribute specified in ntnode (load ntnode if necessary).
 * If not found but ATTR_A_ATTRLIST present, read it in and search through.
 * VOP_VGET node needed, and lookup through its ntnode (load if necessary).
 *
 * ntnode should be locked
 */
int
ntfs_ntvattrget(struct ntfsmount *ntmp, struct ntnode *ip, u_int32_t type,
    const char *name, cn_t vcn, struct ntvattr **vapp)
{
	struct ntvattr *lvap = NULL;
	struct attr_attrlist *aalp;
	struct attr_attrlist *nextaalp;
	struct vnode   *newvp;
	struct ntnode  *newip;
	caddr_t         alpool;
	size_t		namelen, len;
	int             error;

	*vapp = NULL;

	if (name) {
		DPRINTF("ntfs_ntvattrget: ino: %u, type: 0x%x, name: %s, "
		    "vcn: %llu\n", ip->i_number, type, name, vcn);
		namelen = strlen(name);
	} else {
		DPRINTF("ntfs_ntvattrget: ino: %u, type: 0x%x, vcn: %llu\n",
		    ip->i_number, type, vcn);
		name = "";
		namelen = 0;
	}

	error = ntfs_findvattr(ntmp, ip, &lvap, vapp, type, name, namelen, vcn);
	if (error >= 0)
		return (error);

	if (!lvap) {
		DPRINTF("ntfs_ntvattrget: UNEXISTED ATTRIBUTE: ino: %u, "
		    "type: 0x%x, name: %s, vcn: %llu\n", ip->i_number, type,
		    name, vcn);
		return (ENOENT);
	}
	/* Scan $ATTRIBUTE_LIST for requested attribute */
	len = lvap->va_datalen;
	alpool = malloc(len, M_TEMP, M_WAITOK);
	error = ntfs_readntvattr_plain(ntmp, ip, lvap, 0, len, alpool, &len,
			NULL);
	if (error)
		goto out;

	aalp = (struct attr_attrlist *) alpool;
	nextaalp = NULL;

	for(; len > 0; aalp = nextaalp) {
		DPRINTF("ntfs_ntvattrget: attrlist: ino: %u, attr: 0x%x, "
		    "vcn: %llu\n", aalp->al_inumber, aalp->al_type,
		    aalp->al_vcnstart);

		if (len > aalp->reclen) {
			nextaalp = NTFS_NEXTREC(aalp, struct attr_attrlist *);
		} else {
			nextaalp = NULL;
		}
		len -= aalp->reclen;

		if (!NTFS_AALPCMP(aalp, type, name, namelen) ||
		    (nextaalp && (nextaalp->al_vcnstart <= vcn) &&
		     NTFS_AALPCMP(nextaalp, type, name, namelen)))
			continue;

		DPRINTF("ntfs_ntvattrget: attribute in ino: %u\n",
		    aalp->al_inumber);

		/* this is not a main record, so we can't use just plain
		   vget() */
		error = ntfs_vgetex(ntmp->ntm_mountp, aalp->al_inumber,
				NTFS_A_DATA, NULL, LK_EXCLUSIVE,
				VG_EXT, &newvp);
		if (error) {
			printf("ntfs_ntvattrget: CAN'T VGET INO: %d\n",
			       aalp->al_inumber);
			goto out;
		}
		newip = VTONT(newvp);
		/* XXX have to lock ntnode */
		error = ntfs_findvattr(ntmp, newip, &lvap, vapp,
				type, name, namelen, vcn);
		vput(newvp);
		if (error == 0)
			goto out;
		printf("ntfs_ntvattrget: ATTRLIST ERROR.\n");
		break;
	}
	error = ENOENT;

	DPRINTF("ntfs_ntvattrget: UNEXISTED ATTRIBUTE: ino: %u, type: 0x%x, "
	    "name: %.*s, vcn: %llu\n", ip->i_number, type,
	    (unsigned int)namelen, name, vcn);
out:
	free(alpool, M_TEMP, 0);
	return (error);
}

/*
 * Read ntnode from disk, make ntvattr list.
 *
 * ntnode should be locked
 */
int
ntfs_loadntnode(struct ntfsmount *ntmp, struct ntnode *ip)
{
	struct ntnode	*oip;
	struct ntvattr	*vap;
	struct filerec	*mfrp;
	struct attr	*ap;
	daddr_t		bn;
 	int		error,off;
 
 	DPRINTF("ntfs_loadntnode: loading ino: %u\n", ip->i_number);
 
	KASSERT((ip->i_flag & IN_LOADED) == 0);

	if (ntmp->ntm_ntnodes >= LOADED_NTNODE_HI) {
		oip = TAILQ_LAST(&ntmp->ntm_ntnodeq, ntnodeq);
		TAILQ_REMOVE(&ntmp->ntm_ntnodeq, oip, i_loaded);
		ntmp->ntm_ntnodes--;

		DPRINTF("ntfs_loadntnode: unloading ino: %u\n", oip->i_number);

		KASSERT((oip->i_flag & IN_LOADED));
		oip->i_flag &= ~IN_LOADED;
		while ((vap = LIST_FIRST(&oip->i_valist)) != NULL) {
			LIST_REMOVE(vap, va_list);
			ntfs_freentvattr(vap);
		}
	}

 	mfrp = malloc(ntfs_bntob(ntmp->ntm_bpmftrec), M_TEMP, M_WAITOK);
 
	if (ip->i_number < NTFS_SYSNODESNUM) {
		struct buf     *bp;

		DPRINTF("ntfs_loadntnode: read system node\n");

		bn = ntfs_cntobn(ntmp->ntm_mftcn) +
			ntmp->ntm_bpmftrec * ip->i_number;

		error = bread(ntmp->ntm_devvp, bn,
		    ntfs_bntob(ntmp->ntm_bpmftrec), &bp);
		if (error) {
			printf("ntfs_loadntnode: BREAD FAILED\n");
			brelse(bp);
			goto out;
		}
		memcpy(mfrp, bp->b_data, ntfs_bntob(ntmp->ntm_bpmftrec));
		brelse(bp);
	} else {
		struct vnode   *vp;

		vp = ntmp->ntm_sysvn[NTFS_MFTINO];
		error = ntfs_readattr(ntmp, VTONT(vp), NTFS_A_DATA, NULL,
			       ip->i_number * ntfs_bntob(ntmp->ntm_bpmftrec),
			       ntfs_bntob(ntmp->ntm_bpmftrec), mfrp, NULL);
		if (error) {
			printf("ntfs_loadntnode: ntfs_readattr failed\n");
			goto out;
		}
	}

	/* Check if magic and fixups are correct */
	error = ntfs_procfixups(ntmp, NTFS_FILEMAGIC, (caddr_t)mfrp,
				ntfs_bntob(ntmp->ntm_bpmftrec));
	if (error) {
		printf("ntfs_loadntnode: BAD MFT RECORD %d\n",
		       (u_int32_t) ip->i_number);
		goto out;
	}

	DPRINTF("ntfs_loadntnode: load attrs for ino: %u\n", ip->i_number);
	off = mfrp->fr_attroff;
	ap = (struct attr *) ((caddr_t)mfrp + off);

	LIST_INIT(&ip->i_valist);
	
	while (ap->a_hdr.a_type != -1) {
		error = ntfs_attrtontvattr(ntmp, &vap, ap);
		if (error)
			break;
		vap->va_ip = ip;

		LIST_INSERT_HEAD(&ip->i_valist, vap, va_list);

		off += ap->a_hdr.reclen;
		ap = (struct attr *) ((caddr_t)mfrp + off);
	}
	if (error) {
		printf("ntfs_loadntnode: failed to load attr ino: %d\n",
		       ip->i_number);
		goto out;
	}

	ip->i_mainrec = mfrp->fr_mainrec;
	ip->i_nlink = mfrp->fr_nlink;
	ip->i_frflag = mfrp->fr_flags;

	ip->i_flag |= IN_LOADED;

	/* Add to loaded list. */
	TAILQ_INSERT_HEAD(&ntmp->ntm_ntnodeq, ip, i_loaded);
	ntmp->ntm_ntnodes++;

out:
	free(mfrp, M_TEMP, 0);
	return (error);
}

/*
 * Routine locks ntnode and increase usecount, just opposite of
 * ntfs_ntput().
 */
int
ntfs_ntget(struct ntnode *ip)
{
	DPRINTF("ntfs_ntget: get ntnode %u: %p, usecount: %d\n",
	    ip->i_number, ip, ip->i_usecount);

	ip->i_usecount++;

	rw_enter_write(&ip->i_lock);

	return 0;
}

/*
 * Routine search ntnode in hash, if found: lock, inc usecount and return.
 * If not in hash allocate structure for ntnode, prefill it, lock,
 * inc count and return.
 *
 * ntnode returned locked
 */
int
ntfs_ntlookup(struct ntfsmount *ntmp, ntfsino_t ino, struct ntnode **ipp)
{
	struct ntnode  *ip;

	DPRINTF("ntfs_ntlookup: looking for ntnode %u\n", ino);

 retry:
	if ((ip = ntfs_nthashlookup(ntmp->ntm_dev, ino)) != NULL) {
		ntfs_ntget(ip);
		DPRINTF("ntfs_ntlookup: ntnode %u: %p, usecount: %d\n",
		    ino, ip, ip->i_usecount);
		*ipp = ip;
		return (0);
	}

	ip = malloc(sizeof(*ip), M_NTFSNTNODE, M_WAITOK | M_ZERO);
	DDPRINTF("ntfs_ntlookup: allocating ntnode: %u: %p\n", ino, ip);

	/* Generic initialization */
	ip->i_devvp = ntmp->ntm_devvp;
	ip->i_dev = ntmp->ntm_dev;
	ip->i_number = ino;
	ip->i_mp = ntmp;

	LIST_INIT(&ip->i_fnlist);
	LIST_INIT(&ip->i_valist);
	vref(ip->i_devvp);

	/* init lock and lock the newborn ntnode */
	rw_init(&ip->i_lock, "ntnode");
	ntfs_ntget(ip);

	if (ntfs_nthashins(ip) != 0) {
		ntfs_ntput(ip);
		goto retry;
	}

	*ipp = ip;

	DPRINTF("ntfs_ntlookup: ntnode %u: %p, usecount: %d\n",
	    ino, ip, ip->i_usecount);

	return (0);
}

/*
 * Decrement usecount of ntnode and unlock it, if usecount reach zero,
 * deallocate ntnode.
 *
 * ntnode should be locked on entry, and unlocked on return.
 */
void
ntfs_ntput(struct ntnode *ip)
{
	struct ntfsmount *ntmp = ip->i_mp;
	struct ntvattr *vap;

	DPRINTF("ntfs_ntput: rele ntnode %u: %p, usecount: %d\n",
	    ip->i_number, ip, ip->i_usecount);

	ip->i_usecount--;

#ifdef DIAGNOSTIC
	if (ip->i_usecount < 0) {
		panic("ntfs_ntput: ino: %d usecount: %d ",
		      ip->i_number,ip->i_usecount);
	}
#endif

	if (ip->i_usecount > 0) {
		rw_exit_write(&ip->i_lock);
		return;
	}

	DPRINTF("ntfs_ntput: deallocating ntnode: %u\n", ip->i_number);

	if (LIST_FIRST(&ip->i_fnlist))
		panic("ntfs_ntput: ntnode has fnodes");

	ntfs_nthashrem(ip);

	/* Remove from loaded list. */
	if (ip->i_flag & IN_LOADED) {
		TAILQ_REMOVE(&ntmp->ntm_ntnodeq, ip, i_loaded);
		ntmp->ntm_ntnodes--;
	}

	while ((vap = LIST_FIRST(&ip->i_valist)) != NULL) {
		LIST_REMOVE(vap, va_list);
		ntfs_freentvattr(vap);
	}

	vrele(ip->i_devvp);
	free(ip, M_NTFSNTNODE, 0);
}

/*
 * increment usecount of ntnode 
 */
void
ntfs_ntref(struct ntnode *ip)
{
	ip->i_usecount++;

	DPRINTF("ntfs_ntref: ino %u, usecount: %d\n",
	    ip->i_number, ip->i_usecount);
}

/*
 * Decrement usecount of ntnode.
 */
void
ntfs_ntrele(struct ntnode *ip)
{
	DPRINTF("ntfs_ntrele: rele ntnode %u: %p, usecount: %d\n",
	    ip->i_number, ip, ip->i_usecount);

	ip->i_usecount--;

	if (ip->i_usecount < 0)
		panic("ntfs_ntrele: ino: %d usecount: %d ",
		      ip->i_number,ip->i_usecount);
}

/*
 * Deallocate all memory allocated for ntvattr
 */
void
ntfs_freentvattr(struct ntvattr *vap)
{
	if (vap->va_flag & NTFS_AF_INRUN) {
		if (vap->va_vruncn)
			free(vap->va_vruncn, M_NTFSRUN, 0);
		if (vap->va_vruncl)
			free(vap->va_vruncl, M_NTFSRUN, 0);
	} else {
		if (vap->va_datap)
			free(vap->va_datap, M_NTFSRDATA, 0);
	}
	free(vap, M_NTFSNTVATTR, 0);
}

/*
 * Convert disk image of attribute into ntvattr structure,
 * runs are expanded also.
 */
int
ntfs_attrtontvattr(struct ntfsmount *ntmp, struct ntvattr **rvapp,
    struct attr *rap)
{
	int             error, i;
	struct ntvattr *vap;

	error = 0;
	*rvapp = NULL;

	vap = malloc(sizeof(*vap), M_NTFSNTVATTR, M_WAITOK | M_ZERO);
	vap->va_ip = NULL;
	vap->va_flag = rap->a_hdr.a_flag;
	vap->va_type = rap->a_hdr.a_type;
	vap->va_compression = rap->a_hdr.a_compression;
	vap->va_index = rap->a_hdr.a_index;

	DDPRINTF("type: 0x%x, index: %u", vap->va_type, vap->va_index);

	vap->va_namelen = rap->a_hdr.a_namelen;
	if (rap->a_hdr.a_namelen) {
		wchar *unp = (wchar *) ((caddr_t) rap + rap->a_hdr.a_nameoff);
		DDPRINTF(", name:[");
		for (i = 0; i < vap->va_namelen; i++) {
			vap->va_name[i] = unp[i];
			DDPRINTF("%c", vap->va_name[i]);
		}
		DDPRINTF("]");
	}
	if (vap->va_flag & NTFS_AF_INRUN) {
		DDPRINTF(", nonres.");
		vap->va_datalen = rap->a_nr.a_datalen;
		vap->va_allocated = rap->a_nr.a_allocated;
		vap->va_vcnstart = rap->a_nr.a_vcnstart;
		vap->va_vcnend = rap->a_nr.a_vcnend;
		vap->va_compressalg = rap->a_nr.a_compressalg;
		error = ntfs_runtovrun(&(vap->va_vruncn), &(vap->va_vruncl),
				       &(vap->va_vruncnt),
				       (caddr_t) rap + rap->a_nr.a_dataoff);
	} else {
		vap->va_compressalg = 0;
		DDPRINTF(", res.");
		vap->va_datalen = rap->a_r.a_datalen;
		vap->va_allocated = rap->a_r.a_datalen;
		vap->va_vcnstart = 0;
		vap->va_vcnend = ntfs_btocn(vap->va_allocated);
		vap->va_datap = malloc(vap->va_datalen, M_NTFSRDATA, M_WAITOK);
		memcpy(vap->va_datap, (caddr_t) rap + rap->a_r.a_dataoff,
		       rap->a_r.a_datalen);
	}
	DDPRINTF(", len: %llu", vap->va_datalen);

	if (error)
		free(vap, M_NTFSNTVATTR, 0);
	else
		*rvapp = vap;

	DDPRINTF("\n");

	return (error);
}

/*
 * Expand run into more utilizable and more memory eating format.
 */
int
ntfs_runtovrun(cn_t **rcnp, cn_t **rclp, u_long *rcntp, u_int8_t *run)
{
	u_int32_t       off;
	u_int32_t       sz, i;
	cn_t           *cn;
	cn_t           *cl;
	u_long		cnt;
	cn_t		prev;
	cn_t		tmp;

	off = 0;
	cnt = 0;
	while (run[off]) {
		off += (run[off] & 0xF) + ((run[off] >> 4) & 0xF) + 1;
		cnt++;
	}
	cn = mallocarray(cnt, sizeof(cn_t), M_NTFSRUN, M_WAITOK);
	cl = mallocarray(cnt, sizeof(cn_t), M_NTFSRUN, M_WAITOK);

	off = 0;
	cnt = 0;
	prev = 0;
	while (run[off]) {

		sz = run[off++];
		cl[cnt] = 0;

		for (i = 0; i < (sz & 0xF); i++)
			cl[cnt] += (u_int32_t) run[off++] << (i << 3);

		sz >>= 4;
		if (run[off + sz - 1] & 0x80) {
			tmp = ((u_int64_t) - 1) << (sz << 3);
			for (i = 0; i < sz; i++)
				tmp |= (u_int64_t) run[off++] << (i << 3);
		} else {
			tmp = 0;
			for (i = 0; i < sz; i++)
				tmp |= (u_int64_t) run[off++] << (i << 3);
		}
		if (tmp)
			prev = cn[cnt] = prev + tmp;
		else
			cn[cnt] = tmp;

		cnt++;
	}
	*rcnp = cn;
	*rclp = cl;
	*rcntp = cnt;
	return (0);
}

/*
 * Compare unicode and ascii string case insens.
 */
int
ntfs_uastricmp(struct ntfsmount *ntmp, const wchar *ustr, size_t ustrlen,
    const char *astr, size_t astrlen)
{
	size_t  i;
	int             res;
	const char *astrend = astr + astrlen;

	for (i = 0; i < ustrlen && astr < astrend; i++) {
		res = (*ntmp->ntm_wcmp)(NTFS_TOUPPER(ustr[i]),
				NTFS_TOUPPER((*ntmp->ntm_wget)(&astr)) );
		if (res)
			return res;
	}

	if (i == ustrlen && astr == astrend)
		return 0;
	else if (i == ustrlen)
		return -1;
	else
		return 1;
}

/*
 * Compare unicode and ascii string case sens.
 */
int
ntfs_uastrcmp(struct ntfsmount *ntmp, const wchar *ustr, size_t ustrlen,
    const char *astr, size_t astrlen)
{
	size_t             i;
	int             res;
	const char *astrend = astr + astrlen;

	for (i = 0; (i < ustrlen) && (astr < astrend); i++) {
		res = (*ntmp->ntm_wcmp)(ustr[i], (*ntmp->ntm_wget)(&astr));
		if (res)
			return res;
	}

	if (i == ustrlen && astr == astrend)
		return 0;
	else if (i == ustrlen)
		return -1;
	else
		return 1;
}

/* 
 * Search fnode in ntnode, if not found allocate and preinitialize.
 *
 * ntnode should be locked on entry.
 */
int
ntfs_fget(struct ntfsmount *ntmp, struct ntnode *ip, int attrtype,
    char *attrname, struct fnode **fpp)
{
	struct fnode *fp;

	DPRINTF("ntfs_fget: ino: %u, attrtype: 0x%x, attrname: %s\n",
	    ip->i_number, attrtype, attrname ? attrname : "");
	*fpp = NULL;
	LIST_FOREACH(fp, &ip->i_fnlist, f_fnlist) {
		DPRINTF("ntfs_fget: fnode: attrtype: %u, attrname: %s\n",
		    fp->f_attrtype, fp->f_attrname ? fp->f_attrname : "");

		if ((attrtype == fp->f_attrtype) && 
		    ((!attrname && !fp->f_attrname) ||
		     (attrname && fp->f_attrname &&
		      !strcmp(attrname,fp->f_attrname)))){
			DPRINTF("ntfs_fget: found existed: %p\n", fp);
			*fpp = fp;
		}
	}

	if (*fpp)
		return (0);

	fp = malloc(sizeof(*fp), M_NTFSFNODE, M_WAITOK | M_ZERO);
	DPRINTF("ntfs_fget: allocating fnode: %p\n", fp);

	fp->f_ip = ip;
	fp->f_attrname = attrname;
	if (fp->f_attrname) fp->f_flag |= FN_AATTRNAME;
	fp->f_attrtype = attrtype;

	ntfs_ntref(ip);

	LIST_INSERT_HEAD(&ip->i_fnlist, fp, f_fnlist);

	*fpp = fp;

	return (0);
}

/*
 * Deallocate fnode, remove it from ntnode's fnode list.
 *
 * ntnode should be locked.
 */
void
ntfs_frele(struct fnode *fp)
{
	struct ntnode *ip = FTONT(fp);

	DPRINTF("ntfs_frele: fnode: %p for %u: %p\n", fp, ip->i_number, ip);

	DPRINTF("ntfs_frele: deallocating fnode\n");
	LIST_REMOVE(fp,f_fnlist);
	if (fp->f_flag & FN_AATTRNAME)
		free(fp->f_attrname, M_TEMP, 0);
	if (fp->f_dirblbuf)
		free(fp->f_dirblbuf, M_NTFSDIR, 0);
	free(fp, M_NTFSFNODE, 0);
	ntfs_ntrele(ip);
}

/*
 * Lookup attribute name in format: [[:$ATTR_TYPE]:$ATTR_NAME], 
 * $ATTR_TYPE is searched in attrdefs read from $AttrDefs.
 * If $ATTR_TYPE not specified, ATTR_A_DATA assumed.
 */
int
ntfs_ntlookupattr(struct ntfsmount *ntmp, const char *name, int namelen,
    int *attrtype, char **attrname)
{
	const char *sys;
	size_t syslen, i;
	struct ntvattrdef *adp;

	if (namelen == 0)
		return (0);

	if (name[0] == '$') {
		sys = name;
		for (syslen = 0; syslen < namelen; syslen++) {
			if(sys[syslen] == ':') {
				name++;
				namelen--;
				break;
			}
		}
		name += syslen;
		namelen -= syslen;

		adp = ntmp->ntm_ad;
		for (i = 0; i < ntmp->ntm_adnum; i++, adp++){
			if (syslen != adp->ad_namelen || 
			    strncmp(sys, adp->ad_name, syslen) != 0)
				continue;

			*attrtype = adp->ad_type;
			goto out;
		}
		return (ENOENT);
	}

    out:
	if (namelen) {
		*attrname = malloc(namelen + 1, M_TEMP, M_WAITOK);
		memcpy(*attrname, name, namelen);
		(*attrname)[namelen] = '\0';
		*attrtype = NTFS_A_DATA;
	}

	return (0);
}

/*
 * Lookup specified node for filename, matching cnp, return fnode filled.
 */
int
ntfs_ntlookupfile(struct ntfsmount *ntmp, struct vnode *vp,
    struct componentname *cnp, struct vnode **vpp)
{
	struct fnode   *fp = VTOF(vp);
	struct ntnode  *ip = FTONT(fp);
	struct ntvattr *vap = NULL;	/* Root attribute */
	cn_t            cn = 0;	/* VCN in current attribute */
	caddr_t         rdbuf = NULL;	/* Buffer to read directory's blocks */
	u_int32_t       blsize;
	u_int32_t       rdsize;	/* Length of data to read from current block */
	struct attr_indexentry *iep;
	int             error, res, anamelen, fnamelen;
	const char     *fname,*aname;
	u_int32_t       aoff;
	int attrtype = NTFS_A_DATA;
	char *attrname = NULL;
	struct fnode   *nfp;
	struct vnode   *nvp;
	enum vtype	f_type;
	int fullscan = 0;
	struct ntfs_lookup_ctx *lookup_ctx = NULL, *tctx;

	error = ntfs_ntget(ip);
	if (error)
		return (error);

	error = ntfs_ntvattrget(ntmp, ip, NTFS_A_INDXROOT, "$I30", 0, &vap);
	if (error || (vap->va_flag & NTFS_AF_INRUN)) {
		error = ENOTDIR;
		goto fail;
	}

	/*
	 * Divide file name into: foofilefoofilefoofile[:attrspec]
	 * Store like this:       fname:fnamelen       [aname:anamelen]
	 */
	fname = cnp->cn_nameptr;
	aname = NULL;
	anamelen = 0;
	for (fnamelen = 0; fnamelen < cnp->cn_namelen; fnamelen++)
		if(fname[fnamelen] == ':') {
			aname = fname + fnamelen + 1;
			anamelen = cnp->cn_namelen - fnamelen - 1;
			DPRINTF("ntfs_ntlookupfile: %s (%d), attr: %s (%d)\n",
			    fname, fnamelen, aname, anamelen);
			break;
		}

	blsize = vap->va_a_iroot->ir_size;
	DPRINTF("ntfs_ntlookupfile: blksz: %u\n", blsize);

	rdbuf = malloc(blsize, M_TEMP, M_WAITOK);

    loop:
	rdsize = vap->va_datalen;
	DPRINTF("ntfs_ntlookupfile: rdsz: %u\n", rdsize);

	error = ntfs_readattr(ntmp, ip, NTFS_A_INDXROOT, "$I30",
			       0, rdsize, rdbuf, NULL);
	if (error)
		goto fail;

	aoff = sizeof(struct attr_indexroot);

	do {
		iep = (struct attr_indexentry *) (rdbuf + aoff);

		for (; !(iep->ie_flag & NTFS_IEFLAG_LAST) && (rdsize > aoff);
			aoff += iep->reclen,
			iep = (struct attr_indexentry *) (rdbuf + aoff))
		{
			DDPRINTF("scan: %u, %u\n", iep->ie_number,
			    iep->ie_fnametype);
 
			/* check the name - the case-insensitive check
			 * has to come first, to break from this for loop
			 * if needed, so we can dive correctly */
			res = ntfs_uastricmp(ntmp, iep->ie_fname,
				iep->ie_fnamelen, fname, fnamelen);
			if (!fullscan) {
				if (res > 0) break;
				if (res < 0) continue;
			}

			if (iep->ie_fnametype == 0 ||
			    !(ntmp->ntm_flag & NTFS_MFLAG_CASEINS))
			{
				res = ntfs_uastrcmp(ntmp, iep->ie_fname,
					iep->ie_fnamelen, fname, fnamelen);
				if (res != 0 && !fullscan) continue;
			}

			/* if we perform full scan, the file does not match
			 * and this is subnode, dive */
			if (fullscan && res != 0) {
			    if (iep->ie_flag & NTFS_IEFLAG_SUBNODE) {
				tctx = malloc(sizeof(struct ntfs_lookup_ctx),
					M_TEMP, M_WAITOK);
				tctx->aoff	= aoff + iep->reclen;
				tctx->rdsize	= rdsize;
				tctx->cn	= cn;
				tctx->prev	= lookup_ctx;
				lookup_ctx = tctx;
				break;
			    } else
				continue;
			}

			if (aname) {
				error = ntfs_ntlookupattr(ntmp,
					aname, anamelen,
					&attrtype, &attrname);
				if (error)
					goto fail;
			}

			/* Check if we've found ourselves */
			if ((iep->ie_number == ip->i_number) &&
			    (attrtype == fp->f_attrtype) &&
			    ((!attrname && !fp->f_attrname) ||
			     (attrname && fp->f_attrname &&
			      !strcmp(attrname, fp->f_attrname))))
			{
				vref(vp);
				*vpp = vp;
				error = 0;
				goto fail;
			}

			/* free the buffer returned by ntfs_ntlookupattr() */
			if (attrname) {
				free(attrname, M_TEMP, 0);
				attrname = NULL;
			}

			/* vget node, but don't load it */
			error = ntfs_vgetex(ntmp->ntm_mountp,
				   iep->ie_number, attrtype, attrname,
				   LK_EXCLUSIVE, VG_DONTLOADIN | VG_DONTVALIDFN,
				   &nvp);
			if (error)
				goto fail;

			nfp = VTOF(nvp);

			if (nfp->f_flag & FN_VALID) {
				*vpp = nvp;
				goto fail;
			}

			nfp->f_fflag = iep->ie_fflag;
			nfp->f_pnumber = iep->ie_fpnumber;
			nfp->f_times = iep->ie_ftimes;

			if((nfp->f_fflag & NTFS_FFLAG_DIR) &&
			   (nfp->f_attrtype == NTFS_A_DATA) &&
			   (nfp->f_attrname == NULL))
				f_type = VDIR;	
			else
				f_type = VREG;	

			nvp->v_type = f_type;

			if ((nfp->f_attrtype == NTFS_A_DATA) &&
			    (nfp->f_attrname == NULL))
			{
				/* Opening default attribute */
				nfp->f_size = iep->ie_fsize;
				nfp->f_allocated = iep->ie_fallocated;
				nfp->f_flag |= FN_PRELOADED;
			} else {
				error = ntfs_filesize(ntmp, nfp,
					    &nfp->f_size, &nfp->f_allocated);
				if (error) {
					vput(nvp);
					goto fail;
				}
			}

			nfp->f_flag &= ~FN_VALID;
			*vpp = nvp;
			goto fail;
		}

		/* Dive if possible */
		if (iep->ie_flag & NTFS_IEFLAG_SUBNODE) {
			DPRINTF("ntfs_ntlookupfile: diving\n");

			cn = *(cn_t *) (rdbuf + aoff +
					iep->reclen - sizeof(cn_t));
			rdsize = blsize;

			error = ntfs_readattr(ntmp, ip, NTFS_A_INDX, "$I30",
					ntfs_cntob(cn), rdsize, rdbuf, NULL);
			if (error)
				goto fail;

			error = ntfs_procfixups(ntmp, NTFS_INDXMAGIC,
						rdbuf, rdsize);
			if (error)
				goto fail;

			aoff = (((struct attr_indexalloc *) rdbuf)->ia_hdrsize +
				0x18);
		} else if (fullscan && lookup_ctx) {
			cn = lookup_ctx->cn;
			aoff = lookup_ctx->aoff;
			rdsize = lookup_ctx->rdsize;

			error = ntfs_readattr(ntmp, ip,
				(cn == 0) ? NTFS_A_INDXROOT : NTFS_A_INDX,
				"$I30", ntfs_cntob(cn), rdsize, rdbuf, NULL);
			if (error)
				goto fail;
			
			if (cn != 0) {
				error = ntfs_procfixups(ntmp, NTFS_INDXMAGIC,
						rdbuf, rdsize);
				if (error)
					goto fail;
			}

			tctx = lookup_ctx;
			lookup_ctx = lookup_ctx->prev;
			free(tctx, M_TEMP, 0);
		} else {
			DPRINTF("ntfs_ntlookupfile: nowhere to dive :-(\n");
			error = ENOENT;
			break;
		}
	} while (1);

	if (error == ENOENT) {
		/* perform full scan if no entry was found */
		if (!fullscan) {
			fullscan = 1;
			cn = 0;		/* need zero, used by lookup_ctx */

			DDPRINTF("ntfs_ntlookupfile: fullscan performed for: %.*s\n",
			    (unsigned int)fnamelen, fname);
			goto loop;
		}

		if ((cnp->cn_flags & ISLASTCN) &&
		    (cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME))
			error = EJUSTRETURN;
	}

	DPRINTF("finish\n");

fail:
	if (vap)
		ntfs_ntvattrrele(vap);
	if (rdbuf)
		free(rdbuf, M_TEMP, 0);
	if (attrname)
		free(attrname, M_TEMP, 0);
	if (lookup_ctx) {
		while(lookup_ctx) {
			tctx = lookup_ctx;
			lookup_ctx = lookup_ctx->prev;
			free(tctx, M_TEMP, 0);
		}
	}
	ntfs_ntput(ip);
	return (error);
}

/*
 * Check if name type is permitted to show.
 */
int
ntfs_isnamepermitted(struct ntfsmount *ntmp, struct attr_indexentry *iep)
{
	if (ntmp->ntm_flag & NTFS_MFLAG_ALLNAMES)
		return 1;

	switch (iep->ie_fnametype) {
	case 2:
		DDPRINTF("ntfs_isnamepermitted: skipped DOS name\n");
		return 0;
	case 0: case 1: case 3:
		return 1;
	default:
		printf("ntfs_isnamepermitted: " \
		       "WARNING! Unknown file name type: %d\n",
		       iep->ie_fnametype);
		break;
	}
	return 0;
}

/*
 * Read ntfs dir like stream of attr_indexentry, not like btree of them.
 * This is done by scanning $BITMAP:$I30 for busy clusters and reading them.
 * Of course $INDEX_ROOT:$I30 is read before. Last read values are stored in
 * fnode, so we can skip toward record number num almost immediately.
 * Anyway this is rather slow routine. The problem is that we don't know
 * how many records are there in $INDEX_ALLOCATION:$I30 block.
 */
int
ntfs_ntreaddir(struct ntfsmount *ntmp, struct fnode *fp, u_int32_t num,
    struct attr_indexentry **riepp, struct proc *p)
{
	struct ntnode  *ip = FTONT(fp);
	struct ntvattr *vap = NULL;	/* IndexRoot attribute */
	struct ntvattr *bmvap = NULL;	/* BitMap attribute */
	struct ntvattr *iavap = NULL;	/* IndexAllocation attribute */
	caddr_t         rdbuf;		/* Buffer to read directory's blocks  */
	u_int8_t       *bmp = NULL;	/* Bitmap */
	u_int32_t       blsize;		/* Index allocation size (2048) */
	u_int32_t       rdsize;		/* Length of data to read */
	u_int32_t       attrnum;	/* Current attribute type */
	u_int32_t       cpbl = 1;	/* Clusters per directory block */
	u_int32_t       blnum;
	struct attr_indexentry *iep;
	int             error = ENOENT;
	u_int32_t       aoff, cnum;

	DPRINTF("ntfs_ntreaddir: read ino: %u, num: %u\n", ip->i_number, num);
	error = ntfs_ntget(ip);
	if (error)
		return (error);

	error = ntfs_ntvattrget(ntmp, ip, NTFS_A_INDXROOT, "$I30", 0, &vap);
	if (error) {
		error = ENOTDIR;
		goto fail;
	}

	if (fp->f_dirblbuf == NULL) {
		fp->f_dirblsz = vap->va_a_iroot->ir_size;
		fp->f_dirblbuf = malloc(MAX(vap->va_datalen,fp->f_dirblsz),
		    M_NTFSDIR, M_WAITOK);
	}

	blsize = fp->f_dirblsz;
	rdbuf = fp->f_dirblbuf;

	DPRINTF("ntfs_ntreaddir: rdbuf: %p, blsize: %u\n", rdbuf, blsize);

	if (vap->va_a_iroot->ir_flag & NTFS_IRFLAG_INDXALLOC) {
		error = ntfs_ntvattrget(ntmp, ip, NTFS_A_INDXBITMAP, "$I30",
					0, &bmvap);
		if (error) {
			error = ENOTDIR;
			goto fail;
		}
		bmp = malloc(bmvap->va_datalen, M_TEMP, M_WAITOK);
		error = ntfs_readattr(ntmp, ip, NTFS_A_INDXBITMAP, "$I30", 0,
				       bmvap->va_datalen, bmp, NULL);
		if (error)
			goto fail;

		error = ntfs_ntvattrget(ntmp, ip, NTFS_A_INDX, "$I30",
					0, &iavap);
		if (error) {
			error = ENOTDIR;
			goto fail;
		}
		cpbl = ntfs_btocn(blsize + ntfs_cntob(1) - 1);
		DPRINTF("ntfs_ntreaddir: indexalloc: %llu, cpbl: %u\n",
		    iavap->va_datalen, cpbl);
	} else {
		DPRINTF("ntfs_ntreadidir: w/o BitMap and IndexAllocation\n");
		iavap = bmvap = NULL;
		bmp = NULL;
	}

	/* Try use previous values */
	if ((fp->f_lastdnum < num) && (fp->f_lastdnum != 0)) {
		attrnum = fp->f_lastdattr;
		aoff = fp->f_lastdoff;
		blnum = fp->f_lastdblnum;
		cnum = fp->f_lastdnum;
	} else {
		attrnum = NTFS_A_INDXROOT;
		aoff = sizeof(struct attr_indexroot);
		blnum = 0;
		cnum = 0;
	}

	do {
		DPRINTF("ntfs_ntreaddir: scan: 0x%x, %u, %u, %u, %u\n",
		    attrnum, blnum, cnum, num, aoff);
		rdsize = (attrnum == NTFS_A_INDXROOT) ? vap->va_datalen : blsize;
		error = ntfs_readattr(ntmp, ip, attrnum, "$I30",
				ntfs_cntob(blnum * cpbl), rdsize, rdbuf, NULL);
		if (error)
			goto fail;

		if (attrnum == NTFS_A_INDX) {
			error = ntfs_procfixups(ntmp, NTFS_INDXMAGIC,
						rdbuf, rdsize);
			if (error)
				goto fail;
		}
		if (aoff == 0)
			aoff = (attrnum == NTFS_A_INDX) ?
				(0x18 + ((struct attr_indexalloc *) rdbuf)->ia_hdrsize) :
				sizeof(struct attr_indexroot);

		iep = (struct attr_indexentry *) (rdbuf + aoff);
		for (; !(iep->ie_flag & NTFS_IEFLAG_LAST) && (rdsize > aoff);
			aoff += iep->reclen,
			iep = (struct attr_indexentry *) (rdbuf + aoff))
		{
			if (!ntfs_isnamepermitted(ntmp, iep)) continue;

			if (cnum >= num) {
				fp->f_lastdnum = cnum;
				fp->f_lastdoff = aoff;
				fp->f_lastdblnum = blnum;
				fp->f_lastdattr = attrnum;

				*riepp = iep;

				error = 0;
				goto fail;
			}
			cnum++;
		}

		if (iavap) {
			if (attrnum == NTFS_A_INDXROOT)
				blnum = 0;
			else
				blnum++;

			while (ntfs_cntob(blnum * cpbl) < iavap->va_datalen) {
				if (bmp[blnum >> 3] & (1 << (blnum & 7)))
					break;
				blnum++;
			}

			attrnum = NTFS_A_INDX;
			aoff = 0;
			if (ntfs_cntob(blnum * cpbl) >= iavap->va_datalen)
				break;
			DPRINTF("ntfs_ntreaddir: blnum: %u\n", blnum);
		}
	} while (iavap);

	*riepp = NULL;
	fp->f_lastdnum = 0;

fail:
	if (vap)
		ntfs_ntvattrrele(vap);
	if (bmvap)
		ntfs_ntvattrrele(bmvap);
	if (iavap)
		ntfs_ntvattrrele(iavap);
	if (bmp)
		free(bmp, M_TEMP, 0);
	ntfs_ntput(ip);

	return (error);
}

/*
 * Convert NTFS times that are in 100 ns units and begins from
 * 1601 Jan 1 into unix times.
 */
struct timespec
ntfs_nttimetounix(u_int64_t nt)
{
	struct timespec t;

	/* Windows NT times are in 100 ns and from 1601 Jan 1 */
	t.tv_nsec = (nt % (1000 * 1000 * 10)) * 100;
	t.tv_sec = nt / (1000 * 1000 * 10) -
		369LL * 365LL * 24LL * 60LL * 60LL -
		89LL * 1LL * 24LL * 60LL * 60LL;
	return (t);
}

/*
 * Get file sizes from corresponding attribute. 
 * 
 * ntnode under fnode should be locked.
 */
int
ntfs_filesize(struct ntfsmount *ntmp, struct fnode *fp, u_int64_t *size,
    u_int64_t *bytes)
{
	struct ntvattr *vap;
	struct ntnode *ip = FTONT(fp);
	u_int64_t       sz, bn;
	int             error;

	DPRINTF("ntfs_filesize: ino: %u\n", ip->i_number);

	error = ntfs_ntvattrget(ntmp, ip,
		fp->f_attrtype, fp->f_attrname, 0, &vap);
	if (error)
		return (error);

	bn = vap->va_allocated;
	sz = vap->va_datalen;

	DPRINTF("ntfs_filesize: %llu bytes (%llu bytes allocated)\n", sz, bn);

	if (size)
		*size = sz;
	if (bytes)
		*bytes = bn;

	ntfs_ntvattrrele(vap);

	return (0);
}

/*
 * This is one of the read routines.
 *
 * ntnode should be locked.
 */
int
ntfs_readntvattr_plain(struct ntfsmount *ntmp, struct ntnode *ip,
    struct ntvattr *vap, off_t roff, size_t rsize, void *rdata, size_t *initp,
    struct uio *uio)
{
	int             error = 0;
	off_t		off;

	*initp = 0;
	if (vap->va_flag & NTFS_AF_INRUN) {
		int             cnt;
		cn_t            ccn, ccl, cn, cl;
		caddr_t         data = rdata;
		struct buf     *bp;
		size_t          left, tocopy;

		DDPRINTF("ntfs_readntvattr_plain: data in run: %lu chains\n",
		    vap->va_vruncnt);

		off = roff;
		left = rsize;
		ccl = 0;
		ccn = 0;
		cnt = 0;
		while (left && (cnt < vap->va_vruncnt)) {
			ccn = vap->va_vruncn[cnt];
			ccl = vap->va_vruncl[cnt];

			DDPRINTF("ntfs_readntvattr_plain: left %zu, "
			    "cn: 0x%llx, cl: %llu, off: %lld\n",
			    left, ccn, ccl, off);

			if (ntfs_cntob(ccl) < off) {
				off -= ntfs_cntob(ccl);
				cnt++;
				continue;
			}
			if (ccn || ip->i_number == NTFS_BOOTINO) {
				ccl -= ntfs_btocn(off);
				cn = ccn + ntfs_btocn(off);
				off = ntfs_btocnoff(off);

				while (left && ccl) {
					/*
					 * Always read single clusters at a
					 * time - we need to avoid reading
					 * differently-sized blocks at the
					 * same disk offsets to avoid
					 * confusing the buffer cache.
					 */
					tocopy = MIN(left,
					    ntfs_cntob(1) - off);
					cl = ntfs_btocl(tocopy + off);
					KASSERT(cl == 1 &&
					    tocopy <= ntfs_cntob(1));

					DDPRINTF("ntfs_readntvattr_plain: "
					    "read: cn: 0x%llx cl: %llu, "
					    "off: %lld, len: %zu, "
					    "left: %zu\n",
					    cn, cl, off, tocopy, left);
					error = bread(ntmp->ntm_devvp,
						      ntfs_cntobn(cn),
						      ntfs_cntob(cl),
						      &bp);
					if (error) {
						brelse(bp);
						return (error);
					}
					if (uio) {
						error = uiomove(bp->b_data + off,
							tocopy, uio);
						if (error != 0)
							break;
					} else {
						memcpy(data, bp->b_data + off,
							tocopy);
					}
					brelse(bp);
					data = data + tocopy;
					*initp += tocopy;
					off = 0;
					left -= tocopy;
					cn += cl;
					ccl -= cl;
				}
			} else {
				tocopy = MIN(left, ntfs_cntob(ccl) - off);
				DDPRINTF("ntfs_readntvattr_plain: hole: "
				    "ccn: 0x%llx ccl: %llu, off: %lld, "
				    "len: %zu, left: %zu\n",
				    ccn, ccl, off, tocopy, left);
				left -= tocopy;
				off = 0;
				if (uio) {
					size_t remains = tocopy;
					for(; remains; remains--) {
						error = uiomove("", 1, uio);
						if (error != 0)
							break;
					}
				} else 
					bzero(data, tocopy);
				data = data + tocopy;
			}
			cnt++;
			if (error != 0)
				break;
		}
		if (left && error == 0) {
			printf("ntfs_readntvattr_plain: POSSIBLE RUN ERROR\n");
			error = E2BIG;
		}
	} else {
		DDPRINTF("ntfs_readnvattr_plain: data is in mft record\n");
		if (uio) 
			error = uiomove(vap->va_datap + roff, rsize, uio);
		else
			memcpy(rdata, vap->va_datap + roff, rsize);
		*initp += rsize;
	}

	return (error);
}

/*
 * This is one of read routines.
 */
int
ntfs_readattr_plain(struct ntfsmount *ntmp, struct ntnode *ip,
    u_int32_t attrnum, char *attrname, off_t roff, size_t rsize, void *rdata,
    size_t *initp, struct uio *uio)
{
	size_t          init;
	int             error = 0;
	off_t           off = roff;
	size_t		left = rsize, toread;
	caddr_t         data = rdata;
	struct ntvattr *vap;
	*initp = 0;

	while (left) {
		error = ntfs_ntvattrget(ntmp, ip, attrnum, attrname,
					ntfs_btocn(off), &vap);
		if (error)
			return (error);
		toread = MIN(left, ntfs_cntob(vap->va_vcnend + 1) - off);
		DDPRINTF("ntfs_readattr_plain: o: %lld, s: %zu "
		    "(%llu - %llu)\n", off, toread,
		    vap->va_vcnstart, vap->va_vcnend);
		error = ntfs_readntvattr_plain(ntmp, ip, vap,
					 off - ntfs_cntob(vap->va_vcnstart),
					 toread, data, &init, uio);
		if (error) {
			printf("ntfs_readattr_plain: ntfs_readntvattr_plain "
			    "failed: o: %lld, s: %zu\n", off, toread);
			printf("ntfs_readattr_plain: attrib: %llu - %llu\n",
			       vap->va_vcnstart, vap->va_vcnend);
			ntfs_ntvattrrele(vap);
			break;
		}
		ntfs_ntvattrrele(vap);
		left -= toread;
		off += toread;
		data = data + toread;
		*initp += init;
	}

	return (error);
}

/*
 * This is one of read routines.
 */
int
ntfs_readattr(struct ntfsmount *ntmp, struct ntnode *ip, u_int32_t attrnum,
    char *attrname, off_t roff, size_t rsize, void *rdata, struct uio *uio)
{
	int             error = 0;
	struct ntvattr *vap;
	size_t          init;

	DDPRINTF("ntfs_readattr: reading %u: 0x%x, from %lld size %zu bytes\n",
	    ip->i_number, attrnum, roff, rsize);

	error = ntfs_ntvattrget(ntmp, ip, attrnum, attrname, 0, &vap);
	if (error)
		return (error);

	if ((roff > vap->va_datalen) ||
	    (roff + rsize > vap->va_datalen)) {
		printf("ntfs_readattr: offset too big: %lld (%lld) > %llu\n",
		    roff, roff + rsize, vap->va_datalen);
		ntfs_ntvattrrele(vap);
		return (E2BIG);
	}
	if (vap->va_compression && vap->va_compressalg) {
		u_int8_t       *cup;
		u_int8_t       *uup;
		off_t           off = roff;
		caddr_t         data = rdata;
		cn_t            cn;
		size_t		left = rsize, tocopy;

		DDPRINTF("ntfs_ntreadattr: compression: %u\n",
		    vap->va_compressalg);

		cup = malloc(ntfs_cntob(NTFS_COMPUNIT_CL), M_NTFSDECOMP,
		    M_WAITOK);
		uup = malloc(ntfs_cntob(NTFS_COMPUNIT_CL), M_NTFSDECOMP,
		    M_WAITOK);

		cn = (ntfs_btocn(roff)) & (~(NTFS_COMPUNIT_CL - 1));
		off = roff - ntfs_cntob(cn);

		while (left) {
			error = ntfs_readattr_plain(ntmp, ip, attrnum,
						  attrname, ntfs_cntob(cn),
					          ntfs_cntob(NTFS_COMPUNIT_CL),
						  cup, &init, NULL);
			if (error)
				break;

			tocopy = MIN(left, ntfs_cntob(NTFS_COMPUNIT_CL) - off);

			if (init == ntfs_cntob(NTFS_COMPUNIT_CL)) {
				if (uio)
					error = uiomove(cup + off, tocopy, uio);
				else
					memcpy(data, cup + off, tocopy);
			} else if (init == 0) {
				if (uio) {
					size_t remains = tocopy;
					for(; remains; remains--) {
						error = uiomove("", 1, uio);
						if (error != 0)
							break;
					}
				}
				else
					bzero(data, tocopy);
			} else {
				error = ntfs_uncompunit(ntmp, uup, cup);
				if (error)
					break;
				if (uio)
					error = uiomove(uup + off, tocopy, uio);
				else
					memcpy(data, uup + off, tocopy);
			}
			if (error)
				break;

			left -= tocopy;
			data = data + tocopy;
			off += tocopy - ntfs_cntob(NTFS_COMPUNIT_CL);
			cn += NTFS_COMPUNIT_CL;
		}

		free(uup, M_NTFSDECOMP, 0);
		free(cup, M_NTFSDECOMP, 0);
	} else
		error = ntfs_readattr_plain(ntmp, ip, attrnum, attrname,
					     roff, rsize, rdata, &init, uio);
	ntfs_ntvattrrele(vap);
	return (error);
}

#if UNUSED_CODE
int
ntfs_parserun(cn_t *cn, cn_t *cl, u_int8_t *run, u_long len, u_long *off)
{
	u_int8_t        sz;
	int             i;

	if (NULL == run) {
		printf("ntfs_parsetun: run == NULL\n");
		return (EINVAL);
	}
	sz = run[(*off)++];
	if (0 == sz) {
		printf("ntfs_parserun: trying to go out of run\n");
		return (E2BIG);
	}
	*cl = 0;
	if ((sz & 0xF) > 8 || (*off) + (sz & 0xF) > len) {
		printf("ntfs_parserun: " \
		       "bad run: length too big: sz: 0x%02x (%ld < %ld + sz)\n",
		       sz, len, *off);
		return (EINVAL);
	}
	for (i = 0; i < (sz & 0xF); i++)
		*cl += (u_int32_t) run[(*off)++] << (i << 3);

	sz >>= 4;
	if ((sz & 0xF) > 8 || (*off) + (sz & 0xF) > len) {
		printf("ntfs_parserun: " \
		       "bad run: length too big: sz: 0x%02x (%ld < %ld + sz)\n",
		       sz, len, *off);
		return (EINVAL);
	}
	for (i = 0; i < (sz & 0xF); i++)
		*cn += (u_int32_t) run[(*off)++] << (i << 3);

	return (0);
}
#endif

/*
 * Process fixup routine on given buffer.
 */
int
ntfs_procfixups(struct ntfsmount *ntmp, u_int32_t magic, caddr_t buf,
    size_t len)
{
	struct fixuphdr *fhp = (struct fixuphdr *) buf;
	int             i;
	u_int16_t       fixup;
	u_int16_t      *fxp;
	u_int16_t      *cfxp;

	if (fhp->fh_magic != magic) {
		printf("ntfs_procfixups: magic doesn't match: %08x != %08x\n",
		       fhp->fh_magic, magic);
		return (EINVAL);
	}
	if ((fhp->fh_fnum - 1) * ntmp->ntm_bps != len) {
		printf("ntfs_procfixups: " \
		       "bad fixups number: %d for %ld bytes block\n", 
		       fhp->fh_fnum, (long)len);	/* XXX printf kludge */
		return (EINVAL);
	}
	if (fhp->fh_foff >= ntmp->ntm_spc * ntmp->ntm_mftrecsz * ntmp->ntm_bps) {
		printf("ntfs_procfixups: invalid offset: %x", fhp->fh_foff);
		return (EINVAL);
	}
	fxp = (u_int16_t *) (buf + fhp->fh_foff);
	cfxp = (u_int16_t *) (buf + ntmp->ntm_bps - 2);
	fixup = *fxp++;
	for (i = 1; i < fhp->fh_fnum; i++, fxp++) {
		if (*cfxp != fixup) {
			printf("ntfs_procfixups: fixup %d doesn't match\n", i);
			return (EINVAL);
		}
		*cfxp = *fxp;
		cfxp = (u_int16_t *)((caddr_t)cfxp + ntmp->ntm_bps);
	}
	return (0);
}

#if UNUSED_CODE
int
ntfs_runtocn(cn_t *cn, struct ntfsmount *ntmp, u_int8_t *run, u_long len,
    cn_t vcn)
{
	cn_t            ccn = 0;
	cn_t            ccl = 0;
	u_long          off = 0;
	int             error = 0;

#if NTFS_DEBUG
	int             i;
	printf("ntfs_runtocn: run: %p, %ld bytes, vcn:%ld\n",
		run, len, (u_long) vcn);
	printf("ntfs_runtocn: run: ");
	for (i = 0; i < len; i++)
		printf("0x%02x ", run[i]);
	printf("\n");
#endif

	if (NULL == run) {
		printf("ntfs_runtocn: run == NULL\n");
		return (EINVAL);
	}
	do {
		if (run[off] == 0) {
			printf("ntfs_runtocn: vcn too big\n");
			return (E2BIG);
		}
		vcn -= ccl;
		error = ntfs_parserun(&ccn, &ccl, run, len, &off);
		if (error) {
			printf("ntfs_runtocn: ntfs_parserun failed\n");
			return (error);
		}
	} while (ccl <= vcn);
	*cn = ccn + vcn;
	return (0);
}
#endif

/*
 * if the ntfs_toupper_tab[] is filled already, just raise use count;
 * otherwise read the data from the filesystem we are currently mounting
 */
int
ntfs_toupper_use(struct mount *mp, struct ntfsmount *ntmp, struct proc *p)
{
	int error = 0;
	struct vnode *vp;

	/* get exclusive access */
	rw_enter_write(&ntfs_toupper_lock);

	/* only read the translation data from a file if it hasn't been
	 * read already */
	if (ntfs_toupper_tab)
		goto out;

	/*
	 * Read in Unicode lowercase -> uppercase translation file.
	 * XXX for now, just the first 256 entries are used anyway,
	 * so don't bother reading more
	 */
	ntfs_toupper_tab = malloc(256 * 256 * sizeof(wchar), M_NTFSRDATA,
	    M_WAITOK);

	if ((error = VFS_VGET(mp, NTFS_UPCASEINO, &vp)))
		goto out;
	error = ntfs_readattr(ntmp, VTONT(vp), NTFS_A_DATA, NULL,
			0, 256*256*sizeof(wchar), (char *) ntfs_toupper_tab,
			NULL);
	vput(vp);

    out:
	ntfs_toupper_usecount++;
	rw_exit_write(&ntfs_toupper_lock);
	return (error);
}

/*
 * lower the use count and if it reaches zero, free the memory
 * tied by toupper table
 */
void
ntfs_toupper_unuse(struct proc *p)
{
	/* get exclusive access */
	rw_enter_write(&ntfs_toupper_lock);

	ntfs_toupper_usecount--;
	if (ntfs_toupper_usecount == 0) {
		free(ntfs_toupper_tab, M_NTFSRDATA, 0);
		ntfs_toupper_tab = NULL;
	}
#ifdef DIAGNOSTIC
	else if (ntfs_toupper_usecount < 0) {
		panic("ntfs_toupper_unuse(): use count negative: %d",
			ntfs_toupper_usecount);
	}
#endif
	
	/* release the lock */
	rw_exit_write(&ntfs_toupper_lock);
}
