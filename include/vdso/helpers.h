/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_HELPERS_H
#define __VDSO_HELPERS_H

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>

static __always_inline u32 vdso_read_begin(const struct vdso_data *vd)
{
	u32 seq;

	while ((seq = READ_ONCE(vd->seq)) & 1)
		cpu_relax();

	smp_rmb();
	return seq;
}

static __always_inline u32 vdso_read_retry(const struct vdso_data *vd,
					   u32 start)
{
	u32 seq;

	smp_rmb();
	seq = READ_ONCE(vd->seq);
	return seq != start;
}

static __always_inline void vdso_write_begin(struct vdso_data *vd)
{
	/*
	 * WRITE_ONCE it is required otherwise the compiler can validly tear
	 * updates to vd[x].seq and it is possible that the value seen by the
	 * reader it is inconsistent.
	 */
	WRITE_ONCE(vd[CS_HRES_COARSE].seq, vd[CS_HRES_COARSE].seq + 1);
	WRITE_ONCE(vd[CS_RAW].seq, vd[CS_RAW].seq + 1);
	smp_wmb();
}

static __always_inline void vdso_write_end(struct vdso_data *vd)
{
	smp_wmb();
	/*
	 * WRITE_ONCE it is required otherwise the compiler can validly tear
	 * updates to vd[x].seq and it is possible that the value seen by the
	 * reader it is inconsistent.
	 */
	WRITE_ONCE(vd[CS_HRES_COARSE].seq, vd[CS_HRES_COARSE].seq + 1);
	WRITE_ONCE(vd[CS_RAW].seq, vd[CS_RAW].seq + 1);
}

#endif /* !__ASSEMBLY__ */

#endif /* __VDSO_HELPERS_H */
