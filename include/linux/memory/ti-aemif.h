/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __MEMORY_TI_AEMIF_H
#define __MEMORY_TI_AEMIF_H

/**
 * struct aemif_cs_timings: structure to hold CS timing configuration
 * values are expressed in number of clock cycles - 1
 * @ta: minimum turn around time
 * @rhold: read hold width
 * @rstrobe: read strobe width
 * @rsetup: read setup width
 * @whold: write hold width
 * @wstrobe: write strobe width
 * @wsetup: write setup width
 */
struct aemif_cs_timings {
	u32	ta;
	u32	rhold;
	u32	rstrobe;
	u32	rsetup;
	u32	whold;
	u32	wstrobe;
	u32	wsetup;
};

struct aemif_device;

int aemif_set_cs_timings(struct aemif_device *aemif, u8 cs, struct aemif_cs_timings *timings);
int aemif_check_cs_timings(struct aemif_cs_timings *timings);

#endif // __MEMORY_TI_AEMIF_H
