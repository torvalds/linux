// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/ceph/mdsmap.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/decode.h>

#include "super.h"

#define CEPH_MDS_IS_READY(i, ignore_laggy) \
	(m->m_info[i].state > 0 && (ignore_laggy ? true : !m->m_info[i].laggy))

static int __mdsmap_get_random_mds(struct ceph_mdsmap *m, bool ignore_laggy)
{
	int n = 0;
	int i, j;

	/*
	 * special case for one mds, no matter it is laggy or
	 * not we have no choice
	 */
	if (1 == m->m_num_mds && m->m_info[0].state > 0)
		return 0;

	/* count */
	for (i = 0; i < m->m_num_mds; i++)
		if (CEPH_MDS_IS_READY(i, ignore_laggy))
			n++;
	if (n == 0)
		return -1;

	/* pick */
	n = prandom_u32() % n;
	for (j = 0, i = 0; i < m->m_num_mds; i++) {
		if (CEPH_MDS_IS_READY(i, ignore_laggy))
			j++;
		if (j > n)
			break;
	}

	return i;
}

/*
 * choose a random mds that is "up" (i.e. has a state > 0), or -1.
 */
int ceph_mdsmap_get_random_mds(struct ceph_mdsmap *m)
{
	int mds;

	mds = __mdsmap_get_random_mds(m, false);
	if (mds == m->m_num_mds || mds == -1)
		mds = __mdsmap_get_random_mds(m, true);

	return mds == m->m_num_mds ? -1 : mds;
}

#define __decode_and_drop_type(p, end, type, bad)		\
	do {							\
		if (*p + sizeof(type) > end)			\
			goto bad;				\
		*p += sizeof(type);				\
	} while (0)

#define __decode_and_drop_set(p, end, type, bad)		\
	do {							\
		u32 n;						\
		size_t need;					\
		ceph_decode_32_safe(p, end, n, bad);		\
		need = sizeof(type) * n;			\
		ceph_decode_need(p, end, need, bad);		\
		*p += need;					\
	} while (0)

#define __decode_and_drop_map(p, end, ktype, vtype, bad)	\
	do {							\
		u32 n;						\
		size_t need;					\
		ceph_decode_32_safe(p, end, n, bad);		\
		need = (sizeof(ktype) + sizeof(vtype)) * n;	\
		ceph_decode_need(p, end, need, bad);		\
		*p += need;					\
	} while (0)


static int __decode_and_drop_compat_set(void **p, void* end)
{
	int i;
	/* compat, ro_compat, incompat*/
	for (i = 0; i < 3; i++) {
		u32 n;
		ceph_decode_need(p, end, sizeof(u64) + sizeof(u32), bad);
		/* mask */
		*p += sizeof(u64);
		/* names (map<u64, string>) */
		n = ceph_decode_32(p);
		while (n-- > 0) {
			u32 len;
			ceph_decode_need(p, end, sizeof(u64) + sizeof(u32),
					 bad);
			*p += sizeof(u64);
			len = ceph_decode_32(p);
			ceph_decode_need(p, end, len, bad);
			*p += len;
		}
	}
	return 0;
bad:
	return -1;
}

/*
 * Decode an MDS map
 *
 * Ignore any fields we don't care about (there are quite a few of
 * them).
 */
struct ceph_mdsmap *ceph_mdsmap_decode(void **p, void *end)
{
	struct ceph_mdsmap *m;
	const void *start = *p;
	int i, j, n;
	int err;
	u8 mdsmap_v, mdsmap_cv;
	u16 mdsmap_ev;
	u32 possible_max_rank;

	m = kzalloc(sizeof(*m), GFP_NOFS);
	if (!m)
		return ERR_PTR(-ENOMEM);

	ceph_decode_need(p, end, 1 + 1, bad);
	mdsmap_v = ceph_decode_8(p);
	mdsmap_cv = ceph_decode_8(p);
	if (mdsmap_v >= 4) {
	       u32 mdsmap_len;
	       ceph_decode_32_safe(p, end, mdsmap_len, bad);
	       if (end < *p + mdsmap_len)
		       goto bad;
	       end = *p + mdsmap_len;
	}

	ceph_decode_need(p, end, 8*sizeof(u32) + sizeof(u64), bad);
	m->m_epoch = ceph_decode_32(p);
	m->m_client_epoch = ceph_decode_32(p);
	m->m_last_failure = ceph_decode_32(p);
	m->m_root = ceph_decode_32(p);
	m->m_session_timeout = ceph_decode_32(p);
	m->m_session_autoclose = ceph_decode_32(p);
	m->m_max_file_size = ceph_decode_64(p);
	m->m_max_mds = ceph_decode_32(p);

	/*
	 * pick out the active nodes as the m_num_mds, the m_num_mds
	 * maybe larger than m_max_mds when decreasing the max_mds in
	 * cluster side, in other case it should less than or equal
	 * to m_max_mds.
	 */
	m->m_num_mds = n = ceph_decode_32(p);
	m->m_num_active_mds = m->m_num_mds;

	/*
	 * the possible max rank, it maybe larger than the m->m_num_mds,
	 * for example if the mds_max == 2 in the cluster, when the MDS(0)
	 * was laggy and being replaced by a new MDS, we will temporarily
	 * receive a new mds map with n_num_mds == 1 and the active MDS(1),
	 * and the mds rank >= m->m_num_mds.
	 */
	possible_max_rank = max((u32)m->m_num_mds, m->m_max_mds);

	m->m_info = kcalloc(m->m_num_mds, sizeof(*m->m_info), GFP_NOFS);
	if (!m->m_info)
		goto nomem;

	/* pick out active nodes from mds_info (state > 0) */
	for (i = 0; i < n; i++) {
		u64 global_id;
		u32 namelen;
		s32 mds, inc, state;
		u64 state_seq;
		u8 info_v;
		void *info_end = NULL;
		struct ceph_entity_addr addr;
		u32 num_export_targets;
		void *pexport_targets = NULL;
		struct ceph_timespec laggy_since;
		struct ceph_mds_info *info;
		bool laggy;

		ceph_decode_need(p, end, sizeof(u64) + 1, bad);
		global_id = ceph_decode_64(p);
		info_v= ceph_decode_8(p);
		if (info_v >= 4) {
			u32 info_len;
			u8 info_cv;
			ceph_decode_need(p, end, 1 + sizeof(u32), bad);
			info_cv = ceph_decode_8(p);
			info_len = ceph_decode_32(p);
			info_end = *p + info_len;
			if (info_end > end)
				goto bad;
		}

		ceph_decode_need(p, end, sizeof(u64) + sizeof(u32), bad);
		*p += sizeof(u64);
		namelen = ceph_decode_32(p);  /* skip mds name */
		*p += namelen;

		ceph_decode_need(p, end,
				 4*sizeof(u32) + sizeof(u64) +
				 sizeof(addr) + sizeof(struct ceph_timespec),
				 bad);
		mds = ceph_decode_32(p);
		inc = ceph_decode_32(p);
		state = ceph_decode_32(p);
		state_seq = ceph_decode_64(p);
		err = ceph_decode_entity_addr(p, end, &addr);
		if (err)
			goto corrupt;
		ceph_decode_copy(p, &laggy_since, sizeof(laggy_since));
		laggy = laggy_since.tv_sec != 0 || laggy_since.tv_nsec != 0;
		*p += sizeof(u32);
		ceph_decode_32_safe(p, end, namelen, bad);
		*p += namelen;
		if (info_v >= 2) {
			ceph_decode_32_safe(p, end, num_export_targets, bad);
			pexport_targets = *p;
			*p += num_export_targets * sizeof(u32);
		} else {
			num_export_targets = 0;
		}

		if (info_end && *p != info_end) {
			if (*p > info_end)
				goto bad;
			*p = info_end;
		}

		dout("mdsmap_decode %d/%d %lld mds%d.%d %s %s%s\n",
		     i+1, n, global_id, mds, inc,
		     ceph_pr_addr(&addr),
		     ceph_mds_state_name(state),
		     laggy ? "(laggy)" : "");

		if (mds < 0 || mds >= possible_max_rank) {
			pr_warn("mdsmap_decode got incorrect mds(%d)\n", mds);
			continue;
		}

		if (state <= 0) {
			pr_warn("mdsmap_decode got incorrect state(%s)\n",
				ceph_mds_state_name(state));
			continue;
		}

		info = &m->m_info[mds];
		info->global_id = global_id;
		info->state = state;
		info->addr = addr;
		info->laggy = laggy;
		info->num_export_targets = num_export_targets;
		if (num_export_targets) {
			info->export_targets = kcalloc(num_export_targets,
						       sizeof(u32), GFP_NOFS);
			if (!info->export_targets)
				goto nomem;
			for (j = 0; j < num_export_targets; j++)
				info->export_targets[j] =
				       ceph_decode_32(&pexport_targets);
		} else {
			info->export_targets = NULL;
		}
	}

	/* pg_pools */
	ceph_decode_32_safe(p, end, n, bad);
	m->m_num_data_pg_pools = n;
	m->m_data_pg_pools = kcalloc(n, sizeof(u64), GFP_NOFS);
	if (!m->m_data_pg_pools)
		goto nomem;
	ceph_decode_need(p, end, sizeof(u64)*(n+1), bad);
	for (i = 0; i < n; i++)
		m->m_data_pg_pools[i] = ceph_decode_64(p);
	m->m_cas_pg_pool = ceph_decode_64(p);
	m->m_enabled = m->m_epoch > 1;

	mdsmap_ev = 1;
	if (mdsmap_v >= 2) {
		ceph_decode_16_safe(p, end, mdsmap_ev, bad_ext);
	}
	if (mdsmap_ev >= 3) {
		if (__decode_and_drop_compat_set(p, end) < 0)
			goto bad_ext;
	}
	/* metadata_pool */
	if (mdsmap_ev < 5) {
		__decode_and_drop_type(p, end, u32, bad_ext);
	} else {
		__decode_and_drop_type(p, end, u64, bad_ext);
	}

	/* created + modified + tableserver */
	__decode_and_drop_type(p, end, struct ceph_timespec, bad_ext);
	__decode_and_drop_type(p, end, struct ceph_timespec, bad_ext);
	__decode_and_drop_type(p, end, u32, bad_ext);

	/* in */
	{
		int num_laggy = 0;
		ceph_decode_32_safe(p, end, n, bad_ext);
		ceph_decode_need(p, end, sizeof(u32) * n, bad_ext);

		for (i = 0; i < n; i++) {
			s32 mds = ceph_decode_32(p);
			if (mds >= 0 && mds < m->m_num_mds) {
				if (m->m_info[mds].laggy)
					num_laggy++;
			}
		}
		m->m_num_laggy = num_laggy;

		if (n > m->m_num_mds) {
			void *new_m_info = krealloc(m->m_info,
						    n * sizeof(*m->m_info),
						    GFP_NOFS | __GFP_ZERO);
			if (!new_m_info)
				goto nomem;
			m->m_info = new_m_info;
		}
		m->m_num_mds = n;
	}

	/* inc */
	__decode_and_drop_map(p, end, u32, u32, bad_ext);
	/* up */
	__decode_and_drop_map(p, end, u32, u64, bad_ext);
	/* failed */
	__decode_and_drop_set(p, end, u32, bad_ext);
	/* stopped */
	__decode_and_drop_set(p, end, u32, bad_ext);

	if (mdsmap_ev >= 4) {
		/* last_failure_osd_epoch */
		__decode_and_drop_type(p, end, u32, bad_ext);
	}
	if (mdsmap_ev >= 6) {
		/* ever_allowed_snaps */
		__decode_and_drop_type(p, end, u8, bad_ext);
		/* explicitly_allowed_snaps */
		__decode_and_drop_type(p, end, u8, bad_ext);
	}
	if (mdsmap_ev >= 7) {
		/* inline_data_enabled */
		__decode_and_drop_type(p, end, u8, bad_ext);
	}
	if (mdsmap_ev >= 8) {
		u32 name_len;
		/* enabled */
		ceph_decode_8_safe(p, end, m->m_enabled, bad_ext);
		ceph_decode_32_safe(p, end, name_len, bad_ext);
		ceph_decode_need(p, end, name_len, bad_ext);
		*p += name_len;
	}
	/* damaged */
	if (mdsmap_ev >= 9) {
		size_t need;
		ceph_decode_32_safe(p, end, n, bad_ext);
		need = sizeof(u32) * n;
		ceph_decode_need(p, end, need, bad_ext);
		*p += need;
		m->m_damaged = n > 0;
	} else {
		m->m_damaged = false;
	}
bad_ext:
	dout("mdsmap_decode m_enabled: %d, m_damaged: %d, m_num_laggy: %d\n",
	     !!m->m_enabled, !!m->m_damaged, m->m_num_laggy);
	*p = end;
	dout("mdsmap_decode success epoch %u\n", m->m_epoch);
	return m;
nomem:
	err = -ENOMEM;
	goto out_err;
corrupt:
	pr_err("corrupt mdsmap\n");
	print_hex_dump(KERN_DEBUG, "mdsmap: ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       start, end - start, true);
out_err:
	ceph_mdsmap_destroy(m);
	return ERR_PTR(err);
bad:
	err = -EINVAL;
	goto corrupt;
}

void ceph_mdsmap_destroy(struct ceph_mdsmap *m)
{
	int i;

	for (i = 0; i < m->m_num_mds; i++)
		kfree(m->m_info[i].export_targets);
	kfree(m->m_info);
	kfree(m->m_data_pg_pools);
	kfree(m);
}

bool ceph_mdsmap_is_cluster_available(struct ceph_mdsmap *m)
{
	int i, nr_active = 0;
	if (!m->m_enabled)
		return false;
	if (m->m_damaged)
		return false;
	if (m->m_num_laggy == m->m_num_active_mds)
		return false;
	for (i = 0; i < m->m_num_mds; i++) {
		if (m->m_info[i].state == CEPH_MDS_STATE_ACTIVE)
			nr_active++;
	}
	return nr_active > 0;
}
