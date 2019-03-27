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

#include <sys/zfs_context.h>
#include <sys/dbuf.h>
#include <sys/dmu_objset.h>

/*
 * Calculate the index of the arc header for the state, disabled by default.
 */
int zfs_dbuf_state_index = 0;

/*
 * ==========================================================================
 * Dbuf Hash Read Routines
 * ==========================================================================
 */
typedef struct dbuf_stats_t {
	kmutex_t		lock;
	kstat_t			*kstat;
	dbuf_hash_table_t	*hash;
	int			idx;
} dbuf_stats_t;

static dbuf_stats_t dbuf_stats_hash_table;

static int
dbuf_stats_hash_table_headers(char *buf, size_t size)
{
	size = snprintf(buf, size - 1,
	    "%-88s | %-124s | %s\n"
	    "%-16s %-8s %-8s %-8s %-8s %-8s %-8s %-5s %-5s %5s | "
	    "%-5s %-5s %-6s %-8s %-6s %-8s %-12s "
	    "%-6s %-6s %-6s %-6s %-6s %-8s %-8s %-8s %-5s | "
	    "%-6s %-6s %-8s %-8s %-6s %-6s %-5s %-8s %-8s\n",
	    "dbuf", "arcbuf", "dnode", "pool", "objset", "object", "level",
	    "blkid", "offset", "dbsize", "meta", "state", "dbholds", "list",
	    "atype", "index", "flags", "count", "asize", "access", "mru", "gmru",
	    "mfu", "gmfu", "l2", "l2_dattr", "l2_asize", "l2_comp", "aholds",
	    "dtype", "btype", "data_bs", "meta_bs", "bsize",
	    "lvls", "dholds", "blocks", "dsize");
        buf[size] = '\0';

	return (0);
}

int
__dbuf_stats_hash_table_data(char *buf, size_t size, dmu_buf_impl_t *db)
{
	arc_buf_info_t abi = { 0 };
	dmu_object_info_t doi = { 0 };
	dnode_t *dn = DB_DNODE(db);

	if (db->db_buf)
		arc_buf_info(db->db_buf, &abi, zfs_dbuf_state_index);

	if (dn)
		__dmu_object_info_from_dnode(dn, &doi);

	size = snprintf(buf, size - 1,
	    "%-16s %-8llu %-8lld %-8lld %-8lld %-8llu %-8llu %-5d %-5d %-5lu | "
	    "%-5d %-5d %-6lld 0x%-6x %-6lu %-8llu %-12llu "
	    "%-6lu %-6lu %-6lu %-6lu %-6lu %-8llu %-8llu %-8d %-5lu | "
	    "%-6d %-6d %-8lu %-8lu %-6llu %-6lu %-5lu %-8llu %-8llu\n",
	    /* dmu_buf_impl_t */
	    spa_name(dn->dn_objset->os_spa),
	    (u_longlong_t)dmu_objset_id(db->db_objset),
	    (longlong_t)db->db.db_object,
	    (longlong_t)db->db_level,
	    (longlong_t)db->db_blkid,
	    (u_longlong_t)db->db.db_offset,
	    (u_longlong_t)db->db.db_size,
	    !!dbuf_is_metadata(db),
	    db->db_state,
	    (ulong_t)refcount_count(&db->db_holds),
	    /* arc_buf_info_t */
	    abi.abi_state_type,
	    abi.abi_state_contents,
	    (longlong_t)abi.abi_state_index,
	    abi.abi_flags,
	    (ulong_t)abi.abi_bufcnt,
	    (u_longlong_t)abi.abi_size,
	    (u_longlong_t)abi.abi_access,
	    (ulong_t)abi.abi_mru_hits,
	    (ulong_t)abi.abi_mru_ghost_hits,
	    (ulong_t)abi.abi_mfu_hits,
	    (ulong_t)abi.abi_mfu_ghost_hits,
	    (ulong_t)abi.abi_l2arc_hits,
	    (u_longlong_t)abi.abi_l2arc_dattr,
	    (u_longlong_t)abi.abi_l2arc_asize,
	    abi.abi_l2arc_compress,
	    (ulong_t)abi.abi_holds,
	    /* dmu_object_info_t */
	    doi.doi_type,
	    doi.doi_bonus_type,
	    (ulong_t)doi.doi_data_block_size,
	    (ulong_t)doi.doi_metadata_block_size,
	    (u_longlong_t)doi.doi_bonus_size,
	    (ulong_t)doi.doi_indirection,
	    (ulong_t)refcount_count(&dn->dn_holds),
	    (u_longlong_t)doi.doi_fill_count,
	    (u_longlong_t)doi.doi_max_offset);
        buf[size] = '\0';

	return (size);
}

static int
dbuf_stats_hash_table_data(char *buf, size_t size, void *data)
{
	dbuf_stats_t *dsh = (dbuf_stats_t *)data;
	dbuf_hash_table_t *h = dsh->hash;
	dmu_buf_impl_t *db;
	int length, error = 0;

	ASSERT3S(dsh->idx, >=, 0);
	ASSERT3S(dsh->idx, <=, h->hash_table_mask);
	memset(buf, 0, size);

	mutex_enter(DBUF_HASH_MUTEX(h, dsh->idx));
	for (db = h->hash_table[dsh->idx]; db != NULL; db = db->db_hash_next) {
		/*
		 * Returning ENOMEM will cause the data and header functions
		 * to be called with a larger scratch buffers.
		 */
		if (size < 512) {
			error = ENOMEM;
			break;
		}

		mutex_enter(&db->db_mtx);
		mutex_exit(DBUF_HASH_MUTEX(h, dsh->idx));

		length = __dbuf_stats_hash_table_data(buf, size, db);
		buf += length;
		size -= length;

		mutex_exit(&db->db_mtx);
		mutex_enter(DBUF_HASH_MUTEX(h, dsh->idx));
	}
	mutex_exit(DBUF_HASH_MUTEX(h, dsh->idx));

	return (error);
}

static void *
dbuf_stats_hash_table_addr(kstat_t *ksp, off_t n)
{
	dbuf_stats_t *dsh = ksp->ks_private;

        ASSERT(MUTEX_HELD(&dsh->lock));

	if (n <= dsh->hash->hash_table_mask) {
		dsh->idx = n;
		return (dsh);
	}

	return (NULL);
}

#ifndef __FreeBSD__
/*
 * XXX The FreeBSD SPL is missing support for KSTAT_TYPE_RAW
 * we can enable this as soon as that's implemented. See the
 * lindebugfs module for similar callback semantics.
 */
static void
dbuf_stats_hash_table_init(dbuf_hash_table_t *hash)
{
	dbuf_stats_t *dsh = &dbuf_stats_hash_table;
	kstat_t *ksp;

	mutex_init(&dsh->lock, NULL, MUTEX_DEFAULT, NULL);
	dsh->hash = hash;

	ksp = kstat_create("zfs", 0, "dbufs", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);
	dsh->kstat = ksp;

	if (ksp) {
		ksp->ks_lock = &dsh->lock;
		ksp->ks_ndata = UINT32_MAX;
		ksp->ks_private = dsh;
		kstat_set_raw_ops(ksp, dbuf_stats_hash_table_headers,
		    dbuf_stats_hash_table_data, dbuf_stats_hash_table_addr);
		kstat_install(ksp);
	}
}

static void
dbuf_stats_hash_table_destroy(void)
{
	dbuf_stats_t *dsh = &dbuf_stats_hash_table;
	kstat_t *ksp;

	ksp = dsh->kstat;
	if (ksp)
		kstat_delete(ksp);

	mutex_destroy(&dsh->lock);
}
#else
static void
dbuf_stats_hash_table_init(dbuf_hash_table_t *hash)
{
}

static void
dbuf_stats_hash_table_destroy(void)
{
}
#endif

void
dbuf_stats_init(dbuf_hash_table_t *hash)
{
	dbuf_stats_hash_table_init(hash);
}

void
dbuf_stats_destroy(void)
{
	dbuf_stats_hash_table_destroy();
}
