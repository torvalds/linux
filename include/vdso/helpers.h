/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_HELPERS_H
#define __VDSO_HELPERS_H

#ifndef __ASSEMBLY__

#include <asm/barrier.h>
#include <vdso/datapage.h>

static __always_inline u32 vdso_read_begin(const struct vdso_clock *vc)
{
	u32 seq;

	while (unlikely((seq = READ_ONCE(vc->seq)) & 1))
		cpu_relax();

	smp_rmb();
	return seq;
}

static __always_inline u32 vdso_read_retry(const struct vdso_clock *vc,
					   u32 start)
{
	u32 seq;

	smp_rmb();
	seq = READ_ONCE(vc->seq);
	return seq != start;
}

static __always_inline void vdso_write_begin(struct vdso_time_data *vd)
{
	struct vdso_clock *vc = vd->clock_data;

	/*
	 * WRITE_ONCE() is required otherwise the compiler can validly tear
	 * updates to vd[x].seq and it is possible that the value seen by the
	 * reader is inconsistent.
	 */
	WRITE_ONCE(vc[CS_HRES_COARSE].seq, vc[CS_HRES_COARSE].seq + 1);
	WRITE_ONCE(vc[CS_RAW].seq, vc[CS_RAW].seq + 1);
	smp_wmb();
}

static __always_inline void vdso_write_end(struct vdso_time_data *vd)
{
	struct vdso_clock *vc = vd->clock_data;

	smp_wmb();
	/*
	 * WRITE_ONCE() is required otherwise the compiler can validly tear
	 * updates to vd[x].seq and it is possible that the value seen by the
	 * reader is inconsistent.
	 */
	WRITE_ONCE(vc[CS_HRES_COARSE].seq, vc[CS_HRES_COARSE].seq + 1);
	WRITE_ONCE(vc[CS_RAW].seq, vc[CS_RAW].seq + 1);
}

#endif /* !__ASSEMBLY__ */

#endif /* __VDSO_HELPERS_H */
