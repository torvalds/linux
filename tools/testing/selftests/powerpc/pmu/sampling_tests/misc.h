/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 * Copyright 2022, Madhavan Srinivasan, IBM Corp.
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include "../event.h"

#define POWER10 0x80
#define POWER9  0x4e
#define PERF_POWER9_MASK        0x7f8ffffffffffff
#define PERF_POWER10_MASK       0x7ffffffffffffff

#define MMCR0_FC56      0x00000010UL /* freeze counters 5 and 6 */
#define MMCR0_PMCCEXT   0x00000200UL /* PMCCEXT control */
#define MMCR1_RSQ       0x200000000000ULL /* radix scope qual field */
#define BHRB_DISABLE    0x2000000000ULL /* MMCRA BHRB DISABLE bit */

extern int ev_mask_pmcxsel, ev_shift_pmcxsel;
extern int ev_mask_marked, ev_shift_marked;
extern int ev_mask_comb, ev_shift_comb;
extern int ev_mask_unit, ev_shift_unit;
extern int ev_mask_pmc, ev_shift_pmc;
extern int ev_mask_cache, ev_shift_cache;
extern int ev_mask_sample, ev_shift_sample;
extern int ev_mask_thd_sel, ev_shift_thd_sel;
extern int ev_mask_thd_start, ev_shift_thd_start;
extern int ev_mask_thd_stop, ev_shift_thd_stop;
extern int ev_mask_thd_cmp, ev_shift_thd_cmp;
extern int ev_mask_sm, ev_shift_sm;
extern int ev_mask_rsq, ev_shift_rsq;
extern int ev_mask_l2l3, ev_shift_l2l3;
extern int ev_mask_mmcr3_src, ev_shift_mmcr3_src;
extern int pvr;
extern u64 platform_extended_mask;
extern int check_pvr_for_sampling_tests(void);

/*
 * Event code field extraction macro.
 * Raw event code is combination of multiple
 * fields. Macro to extract individual fields
 *
 * x - Raw event code value
 * y - Field to extract
 */
#define EV_CODE_EXTRACT(x, y)   \
	((x >> ev_shift_##y) & ev_mask_##y)

void *event_sample_buf_mmap(int fd, int mmap_pages);
void *__event_read_samples(void *sample_buff, size_t *size, u64 *sample_count);
int collect_samples(void *sample_buff);
u64 *get_intr_regs(struct event *event, void *sample_buff);
u64 get_reg_value(u64 *intr_regs, char *register_name);

static inline int get_mmcr0_fc56(u64 mmcr0, int pmc)
{
	return (mmcr0 & MMCR0_FC56);
}

static inline int get_mmcr0_pmccext(u64 mmcr0, int pmc)
{
	return (mmcr0 & MMCR0_PMCCEXT);
}

static inline int get_mmcr0_pmao(u64 mmcr0, int pmc)
{
	return ((mmcr0 >> 7) & 0x1);
}

static inline int get_mmcr0_cc56run(u64 mmcr0, int pmc)
{
	return ((mmcr0 >> 8) & 0x1);
}

static inline int get_mmcr0_pmcjce(u64 mmcr0, int pmc)
{
	return ((mmcr0 >> 14) & 0x1);
}

static inline int get_mmcr0_pmc1ce(u64 mmcr0, int pmc)
{
	return ((mmcr0 >> 15) & 0x1);
}

static inline int get_mmcr0_pmae(u64 mmcr0, int pmc)
{
	return ((mmcr0 >> 27) & 0x1);
}

static inline int get_mmcr1_pmcxsel(u64 mmcr1, int pmc)
{
	return ((mmcr1 >> ((24 - (((pmc) - 1) * 8))) & 0xff));
}

static inline int get_mmcr1_unit(u64 mmcr1, int pmc)
{
	return ((mmcr1 >> ((60 - (4 * ((pmc) - 1))))) & 0xf);
}

static inline int get_mmcr1_comb(u64 mmcr1, int pmc)
{
	return ((mmcr1 >> (38 - ((pmc - 1) * 2))) & 0x3);
}

static inline int get_mmcr1_cache(u64 mmcr1, int pmc)
{
	return ((mmcr1 >> 46) & 0x3);
}

static inline int get_mmcr1_rsq(u64 mmcr1, int pmc)
{
	return mmcr1 & MMCR1_RSQ;
}

static inline int get_mmcr2_fcs(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (63 - (((pmc) - 1) * 9)))) >> (63 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_fcp(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (62 - (((pmc) - 1) * 9)))) >> (62 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_fcpc(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (61 - (((pmc) - 1) * 9)))) >> (61 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_fcm1(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (60 - (((pmc) - 1) * 9)))) >> (60 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_fcm0(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (59 - (((pmc) - 1) * 9)))) >> (59 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_fcwait(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (58 - (((pmc) - 1) * 9)))) >> (58 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_fch(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (57 - (((pmc) - 1) * 9)))) >> (57 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_fcti(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (56 - (((pmc) - 1) * 9)))) >> (56 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_fcta(u64 mmcr2, int pmc)
{
	return ((mmcr2 & (1ull << (55 - (((pmc) - 1) * 9)))) >> (55 - (((pmc) - 1) * 9)));
}

static inline int get_mmcr2_l2l3(u64 mmcr2, int pmc)
{
	if (pvr == POWER10)
		return ((mmcr2 & 0xf8) >> 3);
	return 0;
}

static inline int get_mmcr3_src(u64 mmcr3, int pmc)
{
	if (pvr != POWER10)
		return 0;
	return ((mmcr3 >> ((49 - (15 * ((pmc) - 1))))) & 0x7fff);
}

static inline int get_mmcra_thd_cmp(u64 mmcra, int pmc)
{
	if (pvr == POWER10)
		return ((mmcra >> 45) & 0x7ff);
	return ((mmcra >> 45) & 0x3ff);
}

static inline int get_mmcra_sm(u64 mmcra, int pmc)
{
	return ((mmcra >> 42) & 0x3);
}

static inline int get_mmcra_bhrb_disable(u64 mmcra, int pmc)
{
	if (pvr == POWER10)
		return mmcra & BHRB_DISABLE;
	return 0;
}

static inline int get_mmcra_ifm(u64 mmcra, int pmc)
{
	return ((mmcra >> 30) & 0x3);
}

static inline int get_mmcra_thd_sel(u64 mmcra, int pmc)
{
	return ((mmcra >> 16) & 0x7);
}

static inline int get_mmcra_thd_start(u64 mmcra, int pmc)
{
	return ((mmcra >> 12) & 0xf);
}

static inline int get_mmcra_thd_stop(u64 mmcra, int pmc)
{
	return ((mmcra >> 8) & 0xf);
}

static inline int get_mmcra_rand_samp_elig(u64 mmcra, int pmc)
{
	return ((mmcra >> 4) & 0x7);
}

static inline int get_mmcra_sample_mode(u64 mmcra, int pmc)
{
	return ((mmcra >> 1) & 0x3);
}

static inline int get_mmcra_marked(u64 mmcra, int pmc)
{
	return mmcra & 0x1;
}
