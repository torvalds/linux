/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/avl.h>
#include <sys/zap.h>
#include <sys/refcount.h>
#include <sys/nvpair.h>
#ifdef _KERNEL
#include <sys/kidmap.h>
#include <sys/sid.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#endif
#include <sys/zfs_fuid.h>

/*
 * FUID Domain table(s).
 *
 * The FUID table is stored as a packed nvlist of an array
 * of nvlists which contain an index, domain string and offset
 *
 * During file system initialization the nvlist(s) are read and
 * two AVL trees are created.  One tree is keyed by the index number
 * and the other by the domain string.  Nodes are never removed from
 * trees, but new entries may be added.  If a new entry is added then
 * the zfsvfs->z_fuid_dirty flag is set to true and the caller will then
 * be responsible for calling zfs_fuid_sync() to sync the changes to disk.
 *
 */

#define	FUID_IDX	"fuid_idx"
#define	FUID_DOMAIN	"fuid_domain"
#define	FUID_OFFSET	"fuid_offset"
#define	FUID_NVP_ARRAY	"fuid_nvlist"

typedef struct fuid_domain {
	avl_node_t	f_domnode;
	avl_node_t	f_idxnode;
	ksiddomain_t	*f_ksid;
	uint64_t	f_idx;
} fuid_domain_t;

static char *nulldomain = "";

/*
 * Compare two indexes.
 */
static int
idx_compare(const void *arg1, const void *arg2)
{
	const fuid_domain_t *node1 = (const fuid_domain_t *)arg1;
	const fuid_domain_t *node2 = (const fuid_domain_t *)arg2;

	return (AVL_CMP(node1->f_idx, node2->f_idx));
}

/*
 * Compare two domain strings.
 */
static int
domain_compare(const void *arg1, const void *arg2)
{
	const fuid_domain_t *node1 = (const fuid_domain_t *)arg1;
	const fuid_domain_t *node2 = (const fuid_domain_t *)arg2;
	int val;

	val = strcmp(node1->f_ksid->kd_name, node2->f_ksid->kd_name);

	return (AVL_ISIGN(val));
}

void
zfs_fuid_avl_tree_create(avl_tree_t *idx_tree, avl_tree_t *domain_tree)
{
	avl_create(idx_tree, idx_compare,
	    sizeof (fuid_domain_t), offsetof(fuid_domain_t, f_idxnode));
	avl_create(domain_tree, domain_compare,
	    sizeof (fuid_domain_t), offsetof(fuid_domain_t, f_domnode));
}

/*
 * load initial fuid domain and idx trees.  This function is used by
 * both the kernel and zdb.
 */
uint64_t
zfs_fuid_table_load(objset_t *os, uint64_t fuid_obj, avl_tree_t *idx_tree,
    avl_tree_t *domain_tree)
{
	dmu_buf_t *db;
	uint64_t fuid_size;

	ASSERT(fuid_obj != 0);
	VERIFY(0 == dmu_bonus_hold(os, fuid_obj,
	    FTAG, &db));
	fuid_size = *(uint64_t *)db->db_data;
	dmu_buf_rele(db, FTAG);

	if (fuid_size)  {
		nvlist_t **fuidnvp;
		nvlist_t *nvp = NULL;
		uint_t count;
		char *packed;
		int i;

		packed = kmem_alloc(fuid_size, KM_SLEEP);
		VERIFY(dmu_read(os, fuid_obj, 0,
		    fuid_size, packed, DMU_READ_PREFETCH) == 0);
		VERIFY(nvlist_unpack(packed, fuid_size,
		    &nvp, 0) == 0);
		VERIFY(nvlist_lookup_nvlist_array(nvp, FUID_NVP_ARRAY,
		    &fuidnvp, &count) == 0);

		for (i = 0; i != count; i++) {
			fuid_domain_t *domnode;
			char *domain;
			uint64_t idx;

			VERIFY(nvlist_lookup_string(fuidnvp[i], FUID_DOMAIN,
			    &domain) == 0);
			VERIFY(nvlist_lookup_uint64(fuidnvp[i], FUID_IDX,
			    &idx) == 0);

			domnode = kmem_alloc(sizeof (fuid_domain_t), KM_SLEEP);

			domnode->f_idx = idx;
			domnode->f_ksid = ksid_lookupdomain(domain);
			avl_add(idx_tree, domnode);
			avl_add(domain_tree, domnode);
		}
		nvlist_free(nvp);
		kmem_free(packed, fuid_size);
	}
	return (fuid_size);
}

void
zfs_fuid_table_destroy(avl_tree_t *idx_tree, avl_tree_t *domain_tree)
{
	fuid_domain_t *domnode;
	void *cookie;

	cookie = NULL;
	while (domnode = avl_destroy_nodes(domain_tree, &cookie))
		ksiddomain_rele(domnode->f_ksid);

	avl_destroy(domain_tree);
	cookie = NULL;
	while (domnode = avl_destroy_nodes(idx_tree, &cookie))
		kmem_free(domnode, sizeof (fuid_domain_t));
	avl_destroy(idx_tree);
}

char *
zfs_fuid_idx_domain(avl_tree_t *idx_tree, uint32_t idx)
{
	fuid_domain_t searchnode, *findnode;
	avl_index_t loc;

	searchnode.f_idx = idx;

	findnode = avl_find(idx_tree, &searchnode, &loc);

	return (findnode ? findnode->f_ksid->kd_name : nulldomain);
}

#ifdef _KERNEL
/*
 * Load the fuid table(s) into memory.
 */
static void
zfs_fuid_init(zfsvfs_t *zfsvfs)
{
	rw_enter(&zfsvfs->z_fuid_lock, RW_WRITER);

	if (zfsvfs->z_fuid_loaded) {
		rw_exit(&zfsvfs->z_fuid_lock);
		return;
	}

	zfs_fuid_avl_tree_create(&zfsvfs->z_fuid_idx, &zfsvfs->z_fuid_domain);

	(void) zap_lookup(zfsvfs->z_os, MASTER_NODE_OBJ,
	    ZFS_FUID_TABLES, 8, 1, &zfsvfs->z_fuid_obj);
	if (zfsvfs->z_fuid_obj != 0) {
		zfsvfs->z_fuid_size = zfs_fuid_table_load(zfsvfs->z_os,
		    zfsvfs->z_fuid_obj, &zfsvfs->z_fuid_idx,
		    &zfsvfs->z_fuid_domain);
	}

	zfsvfs->z_fuid_loaded = B_TRUE;
	rw_exit(&zfsvfs->z_fuid_lock);
}

/*
 * sync out AVL trees to persistent storage.
 */
void
zfs_fuid_sync(zfsvfs_t *zfsvfs, dmu_tx_t *tx)
{
	nvlist_t *nvp;
	nvlist_t **fuids;
	size_t nvsize = 0;
	char *packed;
	dmu_buf_t *db;
	fuid_domain_t *domnode;
	int numnodes;
	int i;

	if (!zfsvfs->z_fuid_dirty) {
		return;
	}

	rw_enter(&zfsvfs->z_fuid_lock, RW_WRITER);

	/*
	 * First see if table needs to be created?
	 */
	if (zfsvfs->z_fuid_obj == 0) {
		zfsvfs->z_fuid_obj = dmu_object_alloc(zfsvfs->z_os,
		    DMU_OT_FUID, 1 << 14, DMU_OT_FUID_SIZE,
		    sizeof (uint64_t), tx);
		VERIFY(zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
		    ZFS_FUID_TABLES, sizeof (uint64_t), 1,
		    &zfsvfs->z_fuid_obj, tx) == 0);
	}

	VERIFY(nvlist_alloc(&nvp, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	numnodes = avl_numnodes(&zfsvfs->z_fuid_idx);
	fuids = kmem_alloc(numnodes * sizeof (void *), KM_SLEEP);
	for (i = 0, domnode = avl_first(&zfsvfs->z_fuid_domain); domnode; i++,
	    domnode = AVL_NEXT(&zfsvfs->z_fuid_domain, domnode)) {
		VERIFY(nvlist_alloc(&fuids[i], NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_uint64(fuids[i], FUID_IDX,
		    domnode->f_idx) == 0);
		VERIFY(nvlist_add_uint64(fuids[i], FUID_OFFSET, 0) == 0);
		VERIFY(nvlist_add_string(fuids[i], FUID_DOMAIN,
		    domnode->f_ksid->kd_name) == 0);
	}
	VERIFY(nvlist_add_nvlist_array(nvp, FUID_NVP_ARRAY,
	    fuids, numnodes) == 0);
	for (i = 0; i != numnodes; i++)
		nvlist_free(fuids[i]);
	kmem_free(fuids, numnodes * sizeof (void *));
	VERIFY(nvlist_size(nvp, &nvsize, NV_ENCODE_XDR) == 0);
	packed = kmem_alloc(nvsize, KM_SLEEP);
	VERIFY(nvlist_pack(nvp, &packed, &nvsize,
	    NV_ENCODE_XDR, KM_SLEEP) == 0);
	nvlist_free(nvp);
	zfsvfs->z_fuid_size = nvsize;
	dmu_write(zfsvfs->z_os, zfsvfs->z_fuid_obj, 0,
	    zfsvfs->z_fuid_size, packed, tx);
	kmem_free(packed, zfsvfs->z_fuid_size);
	VERIFY(0 == dmu_bonus_hold(zfsvfs->z_os, zfsvfs->z_fuid_obj,
	    FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	*(uint64_t *)db->db_data = zfsvfs->z_fuid_size;
	dmu_buf_rele(db, FTAG);

	zfsvfs->z_fuid_dirty = B_FALSE;
	rw_exit(&zfsvfs->z_fuid_lock);
}

/*
 * Query domain table for a given domain.
 *
 * If domain isn't found and addok is set, it is added to AVL trees and
 * the zfsvfs->z_fuid_dirty flag will be set to TRUE.  It will then be
 * necessary for the caller or another thread to detect the dirty table
 * and sync out the changes.
 */
int
zfs_fuid_find_by_domain(zfsvfs_t *zfsvfs, const char *domain,
    char **retdomain, boolean_t addok)
{
	fuid_domain_t searchnode, *findnode;
	avl_index_t loc;
	krw_t rw = RW_READER;

	/*
	 * If the dummy "nobody" domain then return an index of 0
	 * to cause the created FUID to be a standard POSIX id
	 * for the user nobody.
	 */
	if (domain[0] == '\0') {
		if (retdomain)
			*retdomain = nulldomain;
		return (0);
	}

	searchnode.f_ksid = ksid_lookupdomain(domain);
	if (retdomain)
		*retdomain = searchnode.f_ksid->kd_name;
	if (!zfsvfs->z_fuid_loaded)
		zfs_fuid_init(zfsvfs);

retry:
	rw_enter(&zfsvfs->z_fuid_lock, rw);
	findnode = avl_find(&zfsvfs->z_fuid_domain, &searchnode, &loc);

	if (findnode) {
		rw_exit(&zfsvfs->z_fuid_lock);
		ksiddomain_rele(searchnode.f_ksid);
		return (findnode->f_idx);
	} else if (addok) {
		fuid_domain_t *domnode;
		uint64_t retidx;

		if (rw == RW_READER && !rw_tryupgrade(&zfsvfs->z_fuid_lock)) {
			rw_exit(&zfsvfs->z_fuid_lock);
			rw = RW_WRITER;
			goto retry;
		}

		domnode = kmem_alloc(sizeof (fuid_domain_t), KM_SLEEP);
		domnode->f_ksid = searchnode.f_ksid;

		retidx = domnode->f_idx = avl_numnodes(&zfsvfs->z_fuid_idx) + 1;

		avl_add(&zfsvfs->z_fuid_domain, domnode);
		avl_add(&zfsvfs->z_fuid_idx, domnode);
		zfsvfs->z_fuid_dirty = B_TRUE;
		rw_exit(&zfsvfs->z_fuid_lock);
		return (retidx);
	} else {
		rw_exit(&zfsvfs->z_fuid_lock);
		return (-1);
	}
}

/*
 * Query domain table by index, returning domain string
 *
 * Returns a pointer from an avl node of the domain string.
 *
 */
const char *
zfs_fuid_find_by_idx(zfsvfs_t *zfsvfs, uint32_t idx)
{
	char *domain;

	if (idx == 0 || !zfsvfs->z_use_fuids)
		return (NULL);

	if (!zfsvfs->z_fuid_loaded)
		zfs_fuid_init(zfsvfs);

	rw_enter(&zfsvfs->z_fuid_lock, RW_READER);

	if (zfsvfs->z_fuid_obj || zfsvfs->z_fuid_dirty)
		domain = zfs_fuid_idx_domain(&zfsvfs->z_fuid_idx, idx);
	else
		domain = nulldomain;
	rw_exit(&zfsvfs->z_fuid_lock);

	ASSERT(domain);
	return (domain);
}

void
zfs_fuid_map_ids(znode_t *zp, cred_t *cr, uid_t *uidp, uid_t *gidp)
{
	*uidp = zfs_fuid_map_id(zp->z_zfsvfs, zp->z_uid, cr, ZFS_OWNER);
	*gidp = zfs_fuid_map_id(zp->z_zfsvfs, zp->z_gid, cr, ZFS_GROUP);
}

uid_t
zfs_fuid_map_id(zfsvfs_t *zfsvfs, uint64_t fuid,
    cred_t *cr, zfs_fuid_type_t type)
{
	uint32_t index = FUID_INDEX(fuid);
	const char *domain;
	uid_t id;

	if (index == 0)
		return (fuid);

	domain = zfs_fuid_find_by_idx(zfsvfs, index);
	ASSERT(domain != NULL);

#ifdef illumos
	if (type == ZFS_OWNER || type == ZFS_ACE_USER) {
		(void) kidmap_getuidbysid(crgetzone(cr), domain,
		    FUID_RID(fuid), &id);
	} else {
		(void) kidmap_getgidbysid(crgetzone(cr), domain,
		    FUID_RID(fuid), &id);
	}
#else
	id = UID_NOBODY;
#endif
	return (id);
}

/*
 * Add a FUID node to the list of fuid's being created for this
 * ACL
 *
 * If ACL has multiple domains, then keep only one copy of each unique
 * domain.
 */
void
zfs_fuid_node_add(zfs_fuid_info_t **fuidpp, const char *domain, uint32_t rid,
    uint64_t idx, uint64_t id, zfs_fuid_type_t type)
{
	zfs_fuid_t *fuid;
	zfs_fuid_domain_t *fuid_domain;
	zfs_fuid_info_t *fuidp;
	uint64_t fuididx;
	boolean_t found = B_FALSE;

	if (*fuidpp == NULL)
		*fuidpp = zfs_fuid_info_alloc();

	fuidp = *fuidpp;
	/*
	 * First find fuid domain index in linked list
	 *
	 * If one isn't found then create an entry.
	 */

	for (fuididx = 1, fuid_domain = list_head(&fuidp->z_domains);
	    fuid_domain; fuid_domain = list_next(&fuidp->z_domains,
	    fuid_domain), fuididx++) {
		if (idx == fuid_domain->z_domidx) {
			found = B_TRUE;
			break;
		}
	}

	if (!found) {
		fuid_domain = kmem_alloc(sizeof (zfs_fuid_domain_t), KM_SLEEP);
		fuid_domain->z_domain = domain;
		fuid_domain->z_domidx = idx;
		list_insert_tail(&fuidp->z_domains, fuid_domain);
		fuidp->z_domain_str_sz += strlen(domain) + 1;
		fuidp->z_domain_cnt++;
	}

	if (type == ZFS_ACE_USER || type == ZFS_ACE_GROUP) {

		/*
		 * Now allocate fuid entry and add it on the end of the list
		 */

		fuid = kmem_alloc(sizeof (zfs_fuid_t), KM_SLEEP);
		fuid->z_id = id;
		fuid->z_domidx = idx;
		fuid->z_logfuid = FUID_ENCODE(fuididx, rid);

		list_insert_tail(&fuidp->z_fuids, fuid);
		fuidp->z_fuid_cnt++;
	} else {
		if (type == ZFS_OWNER)
			fuidp->z_fuid_owner = FUID_ENCODE(fuididx, rid);
		else
			fuidp->z_fuid_group = FUID_ENCODE(fuididx, rid);
	}
}

/*
 * Create a file system FUID, based on information in the users cred
 *
 * If cred contains KSID_OWNER then it should be used to determine
 * the uid otherwise cred's uid will be used. By default cred's gid
 * is used unless it's an ephemeral ID in which case KSID_GROUP will
 * be used if it exists.
 */
uint64_t
zfs_fuid_create_cred(zfsvfs_t *zfsvfs, zfs_fuid_type_t type,
    cred_t *cr, zfs_fuid_info_t **fuidp)
{
	uint64_t	idx;
	ksid_t		*ksid;
	uint32_t	rid;
	char 		*kdomain;
	const char	*domain;
	uid_t		id;

	VERIFY(type == ZFS_OWNER || type == ZFS_GROUP);

	ksid = crgetsid(cr, (type == ZFS_OWNER) ? KSID_OWNER : KSID_GROUP);

	if (!zfsvfs->z_use_fuids || (ksid == NULL)) {
		id = (type == ZFS_OWNER) ? crgetuid(cr) : crgetgid(cr);

		if (IS_EPHEMERAL(id))
			return ((type == ZFS_OWNER) ? UID_NOBODY : GID_NOBODY);

		return ((uint64_t)id);
	}

	/*
	 * ksid is present and FUID is supported
	 */
	id = (type == ZFS_OWNER) ? ksid_getid(ksid) : crgetgid(cr);

	if (!IS_EPHEMERAL(id))
		return ((uint64_t)id);

	if (type == ZFS_GROUP)
		id = ksid_getid(ksid);

	rid = ksid_getrid(ksid);
	domain = ksid_getdomain(ksid);

	idx = zfs_fuid_find_by_domain(zfsvfs, domain, &kdomain, B_TRUE);

	zfs_fuid_node_add(fuidp, kdomain, rid, idx, id, type);

	return (FUID_ENCODE(idx, rid));
}

/*
 * Create a file system FUID for an ACL ace
 * or a chown/chgrp of the file.
 * This is similar to zfs_fuid_create_cred, except that
 * we can't find the domain + rid information in the
 * cred.  Instead we have to query Winchester for the
 * domain and rid.
 *
 * During replay operations the domain+rid information is
 * found in the zfs_fuid_info_t that the replay code has
 * attached to the zfsvfs of the file system.
 */
uint64_t
zfs_fuid_create(zfsvfs_t *zfsvfs, uint64_t id, cred_t *cr,
    zfs_fuid_type_t type, zfs_fuid_info_t **fuidpp)
{
	const char *domain;
	char *kdomain;
	uint32_t fuid_idx = FUID_INDEX(id);
	uint32_t rid;
	idmap_stat status;
	uint64_t idx = 0;
	zfs_fuid_t *zfuid = NULL;
	zfs_fuid_info_t *fuidp = NULL;

	/*
	 * If POSIX ID, or entry is already a FUID then
	 * just return the id
	 *
	 * We may also be handed an already FUID'ized id via
	 * chmod.
	 */

	if (!zfsvfs->z_use_fuids || !IS_EPHEMERAL(id) || fuid_idx != 0)
		return (id);

	if (zfsvfs->z_replay) {
		fuidp = zfsvfs->z_fuid_replay;

		/*
		 * If we are passed an ephemeral id, but no
		 * fuid_info was logged then return NOBODY.
		 * This is most likely a result of idmap service
		 * not being available.
		 */
		if (fuidp == NULL)
			return (UID_NOBODY);

		VERIFY3U(type, >=, ZFS_OWNER);
		VERIFY3U(type, <=, ZFS_ACE_GROUP);

		switch (type) {
		case ZFS_ACE_USER:
		case ZFS_ACE_GROUP:
			zfuid = list_head(&fuidp->z_fuids);
			rid = FUID_RID(zfuid->z_logfuid);
			idx = FUID_INDEX(zfuid->z_logfuid);
			break;
		case ZFS_OWNER:
			rid = FUID_RID(fuidp->z_fuid_owner);
			idx = FUID_INDEX(fuidp->z_fuid_owner);
			break;
		case ZFS_GROUP:
			rid = FUID_RID(fuidp->z_fuid_group);
			idx = FUID_INDEX(fuidp->z_fuid_group);
			break;
		};
		domain = fuidp->z_domain_table[idx - 1];
	} else {
		if (type == ZFS_OWNER || type == ZFS_ACE_USER)
			status = kidmap_getsidbyuid(crgetzone(cr), id,
			    &domain, &rid);
		else
			status = kidmap_getsidbygid(crgetzone(cr), id,
			    &domain, &rid);

		if (status != 0) {
			/*
			 * When returning nobody we will need to
			 * make a dummy fuid table entry for logging
			 * purposes.
			 */
			rid = UID_NOBODY;
			domain = nulldomain;
		}
	}

	idx = zfs_fuid_find_by_domain(zfsvfs, domain, &kdomain, B_TRUE);

	if (!zfsvfs->z_replay)
		zfs_fuid_node_add(fuidpp, kdomain,
		    rid, idx, id, type);
	else if (zfuid != NULL) {
		list_remove(&fuidp->z_fuids, zfuid);
		kmem_free(zfuid, sizeof (zfs_fuid_t));
	}
	return (FUID_ENCODE(idx, rid));
}

void
zfs_fuid_destroy(zfsvfs_t *zfsvfs)
{
	rw_enter(&zfsvfs->z_fuid_lock, RW_WRITER);
	if (!zfsvfs->z_fuid_loaded) {
		rw_exit(&zfsvfs->z_fuid_lock);
		return;
	}
	zfs_fuid_table_destroy(&zfsvfs->z_fuid_idx, &zfsvfs->z_fuid_domain);
	rw_exit(&zfsvfs->z_fuid_lock);
}

/*
 * Allocate zfs_fuid_info for tracking FUIDs created during
 * zfs_mknode, VOP_SETATTR() or VOP_SETSECATTR()
 */
zfs_fuid_info_t *
zfs_fuid_info_alloc(void)
{
	zfs_fuid_info_t *fuidp;

	fuidp = kmem_zalloc(sizeof (zfs_fuid_info_t), KM_SLEEP);
	list_create(&fuidp->z_domains, sizeof (zfs_fuid_domain_t),
	    offsetof(zfs_fuid_domain_t, z_next));
	list_create(&fuidp->z_fuids, sizeof (zfs_fuid_t),
	    offsetof(zfs_fuid_t, z_next));
	return (fuidp);
}

/*
 * Release all memory associated with zfs_fuid_info_t
 */
void
zfs_fuid_info_free(zfs_fuid_info_t *fuidp)
{
	zfs_fuid_t *zfuid;
	zfs_fuid_domain_t *zdomain;

	while ((zfuid = list_head(&fuidp->z_fuids)) != NULL) {
		list_remove(&fuidp->z_fuids, zfuid);
		kmem_free(zfuid, sizeof (zfs_fuid_t));
	}

	if (fuidp->z_domain_table != NULL)
		kmem_free(fuidp->z_domain_table,
		    (sizeof (char **)) * fuidp->z_domain_cnt);

	while ((zdomain = list_head(&fuidp->z_domains)) != NULL) {
		list_remove(&fuidp->z_domains, zdomain);
		kmem_free(zdomain, sizeof (zfs_fuid_domain_t));
	}

	kmem_free(fuidp, sizeof (zfs_fuid_info_t));
}

/*
 * Check to see if id is a groupmember.  If cred
 * has ksid info then sidlist is checked first
 * and if still not found then POSIX groups are checked
 *
 * Will use a straight FUID compare when possible.
 */
boolean_t
zfs_groupmember(zfsvfs_t *zfsvfs, uint64_t id, cred_t *cr)
{
#ifdef illumos
	ksid_t		*ksid = crgetsid(cr, KSID_GROUP);
	ksidlist_t	*ksidlist = crgetsidlist(cr);
#endif
	uid_t		gid;

#ifdef illumos
	if (ksid && ksidlist) {
		int 		i;
		ksid_t		*ksid_groups;
		uint32_t	idx = FUID_INDEX(id);
		uint32_t	rid = FUID_RID(id);

		ksid_groups = ksidlist->ksl_sids;

		for (i = 0; i != ksidlist->ksl_nsid; i++) {
			if (idx == 0) {
				if (id != IDMAP_WK_CREATOR_GROUP_GID &&
				    id == ksid_groups[i].ks_id) {
					return (B_TRUE);
				}
			} else {
				const char *domain;

				domain = zfs_fuid_find_by_idx(zfsvfs, idx);
				ASSERT(domain != NULL);

				if (strcmp(domain,
				    IDMAP_WK_CREATOR_SID_AUTHORITY) == 0)
					return (B_FALSE);

				if ((strcmp(domain,
				    ksid_groups[i].ks_domain->kd_name) == 0) &&
				    rid == ksid_groups[i].ks_rid)
					return (B_TRUE);
			}
		}
	}
#endif	/* illumos */

	/*
	 * Not found in ksidlist, check posix groups
	 */
	gid = zfs_fuid_map_id(zfsvfs, id, cr, ZFS_GROUP);
	return (groupmember(gid, cr));
}

void
zfs_fuid_txhold(zfsvfs_t *zfsvfs, dmu_tx_t *tx)
{
	if (zfsvfs->z_fuid_obj == 0) {
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    FUID_SIZE_ESTIMATE(zfsvfs));
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, FALSE, NULL);
	} else {
		dmu_tx_hold_bonus(tx, zfsvfs->z_fuid_obj);
		dmu_tx_hold_write(tx, zfsvfs->z_fuid_obj, 0,
		    FUID_SIZE_ESTIMATE(zfsvfs));
	}
}
#endif
