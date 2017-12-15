#ifndef __TI_SYSC_DATA_H__
#define __TI_SYSC_DATA_H__

/**
 * struct sysc_regbits - TI OCP_SYSCONFIG register field offsets
 * @midle_shift: Offset of the midle bit
 * @clkact_shift: Offset of the clockactivity bit
 * @sidle_shift: Offset of the sidle bit
 * @enwkup_shift: Offset of the enawakeup bit
 * @srst_shift: Offset of the softreset bit
 * @autoidle_shift: Offset of the autoidle bit
 * @dmadisable_shift: Offset of the dmadisable bit
 * @emufree_shift; Offset of the emufree bit
 *
 * Note that 0 is a valid shift, and for ti-sysc.c -ENODEV can be used if a
 * feature is not available.
 */
struct sysc_regbits {
	s8 midle_shift;
	s8 clkact_shift;
	s8 sidle_shift;
	s8 enwkup_shift;
	s8 srst_shift;
	s8 autoidle_shift;
	s8 dmadisable_shift;
	s8 emufree_shift;
};

#endif	/* __TI_SYSC_DATA_H__ */
