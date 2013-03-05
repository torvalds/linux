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


/*
 * choose a random mds that is "up" (i.e. has a state > 0), or -1.
 */
int ceph_mdsmap_get_random_mds(struct ceph_mdsmap *m)
{
	int n = 0;
	int i;
	char r;

	/* count */
	for (i = 0; i < m->m_max_mds; i++)
		if (m->m_info[i].state > 0)
			n++;
	if (n == 0)
		return -1;

	/* pick */
	get_random_bytes(&r, 1);
	n = r % n;
	i = 0;
	for (i = 0; n > 0; i++, n--)
		while (m->m_info[i].state <= 0)
			i++;

	return i;
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
	int err = -EINVAL;
	u16 version;

	m = kzalloc(sizeof(*m), GFP_NOFS);
	if (m == NULL)
		return ERR_PTR(-ENOMEM);

	ceph_decode_16_safe(p, end, version, bad);
	if (version > 3) {
		pr_warning("got mdsmap version %d > 3, failing", version);
		goto bad;
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

	m->m_info = kcalloc(m->m_max_mds, sizeof(*m->m_info), GFP_NOFS);
	if (m->m_info == NULL)
		goto badmem;

	/* pick out active nodes from mds_info (state > 0) */
	n = ceph_decode_32(p);
	for (i = 0; i < n; i++) {
		u64 global_id;
		u32 namelen;
		s32 mds, inc, state;
		u64 state_seq;
		u8 infoversion;
		struct ceph_entity_addr addr;
		u32 num_export_targets;
		void *pexport_targets = NULL;
		struct ceph_timespec laggy_since;

		ceph_decode_need(p, end, sizeof(u64)*2 + 1 + sizeof(u32), bad);
		global_id = ceph_decode_64(p);
		infoversion = ceph_decode_8(p);
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
		ceph_decode_copy(p, &addr, sizeof(addr));
		ceph_decode_addr(&addr);
		ceph_decode_copy(p, &laggy_since, sizeof(laggy_since));
		*p += sizeof(u32);
		ceph_decode_32_safe(p, end, namelen, bad);
		*p += namelen;
		if (infoversion >= 2) {
			ceph_decode_32_safe(p, end, num_export_targets, bad);
			pexport_targets = *p;
			*p += num_export_targets * sizeof(u32);
		} else {
			num_export_targets = 0;
		}

		dout("mdsmap_decode %d/%d %lld mds%d.%d %s %s\n",
		     i+1, n, global_id, mds, inc,
		     ceph_pr_addr(&addr.in_addr),
		     ceph_mds_state_name(state));
		if (mds >= 0 && mds < m->m_max_mds && state > 0) {
			m->m_info[mds].global_id = global_id;
			m->m_info[mds].state = state;
			m->m_info[mds].addr = addr;
			m->m_info[mds].laggy =
				(laggy_since.tv_sec != 0 ||
				 laggy_since.tv_nsec != 0);
			m->m_info[mds].num_export_targets = num_export_targets;
			if (num_export_targets) {
				m->m_info[mds].export_targets =
					kcalloc(num_export_targets, sizeof(u32),
						GFP_NOFS);
				for (j = 0; j < num_export_targets; j++)
					m->m_info[mds].export_targets[j] =
					       ceph_decode_32(&pexport_targets);
			} else {
				m->m_info[mds].export_targets = NULL;
			}
		}
	}

	/* pg_pools */
	ceph_decode_32_safe(p, end, n, bad);
	m->m_num_data_pg_pools = n;
	m->m_data_pg_pools = kcalloc(n, sizeof(u64), GFP_NOFS);
	if (!m->m_data_pg_pools)
		goto badmem;
	ceph_decode_need(p, end, sizeof(u64)*(n+1), bad);
	for (i = 0; i < n; i++)
		m->m_data_pg_pools[i] = ceph_decode_64(p);
	m->m_cas_pg_pool = ceph_decode_64(p);

	/* ok, we don't care about the rest. */
	dout("mdsmap_decode success epoch %u\n", m->m_epoch);
	return m;

badmem:
	err = -ENOMEM;
bad:
	pr_err("corrupt mdsmap\n");
	print_hex_dump(KERN_DEBUG, "mdsmap: ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       start, end - start, true);
	ceph_mdsmap_destroy(m);
	return ERR_PTR(-EINVAL);
}

void ceph_mdsmap_destroy(struct ceph_mdsmap *m)
{
	int i;

	for (i = 0; i < m->m_max_mds; i++)
		kfree(m->m_info[i].export_targets);
	kfree(m->m_info);
	kfree(m->m_data_pg_pools);
	kfree(m);
}
