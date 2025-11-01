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

static __always_inline void vdso_write_seq_begin(struct vdso_clock *vc)
{
	/*
	 * WRITE_ONCE() is required otherwise the compiler can validly tear
	 * updates to vc->seq and it is possible that the value seen by the
	 * reader is inconsistent.
	 */
	WRITE_ONCE(vc->seq, vc->seq + 1);
}

static __always_inline void vdso_write_seq_end(struct vdso_clock *vc)
{
	/*
	 * WRITE_ONCE() is required otherwise the compiler can validly tear
	 * updates to vc->seq and it is possible that the value seen by the
	 * reader is inconsistent.
	 */
	WRITE_ONCE(vc->seq, vc->seq + 1);
}

static __always_inline void vdso_write_begin_clock(struct vdso_clock *vc)
{
	vdso_write_seq_begin(vc);
	/* Ensure the sequence invalidation is visible before data is modified */
	smp_wmb();
}

static __always_inline void vdso_write_end_clock(struct vdso_clock *vc)
{
	/* Ensure the data update is visible before the sequence is set valid again */
	smp_wmb();
	vdso_write_seq_end(vc);
}

static __always_inline void vdso_write_begin(struct vdso_time_data *vd)
{
	struct vdso_clock *vc = vd->clock_data;

	vdso_write_seq_begin(&vc[CS_HRES_COARSE]);
	vdso_write_seq_begin(&vc[CS_RAW]);
	/* Ensure the sequence invalidation is visible before data is modified */
	smp_wmb();
}

static __always_inline void vdso_write_end(struct vdso_time_data *vd)
{
	struct vdso_clock *vc = vd->clock_data;

	/* Ensure the data update is visible before the sequence is set valid again */
	smp_wmb();
	vdso_write_seq_end(&vc[CS_HRES_COARSE]);
	vdso_write_seq_end(&vc[CS_RAW]);
}

#endif /* !__ASSEMBLY__ */

#endif /* __VDSO_HELPERS_H */
