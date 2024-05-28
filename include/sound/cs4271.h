/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for CS4271 ASoC codec driver
 *
 * Copyright (c) 2010 Alexander Sverdlin <subaparts@yandex.ru>
 */

#ifndef __CS4271_H
#define __CS4271_H

struct cs4271_platform_data {
	bool amutec_eq_bmutec;	/* flag to enable AMUTEC=BMUTEC */

	/*
	 * The CS4271 requires its LRCLK and MCLK to be stable before its RESET
	 * line is de-asserted. That also means that clocks cannot be changed
	 * without putting the chip back into hardware reset, which also requires
	 * a complete re-initialization of all registers.
	 *
	 * One (undocumented) workaround is to assert and de-assert the PDN bit
	 * in the MODE2 register. This workaround can be enabled with the
	 * following flag.
	 *
	 * Note that this is not needed in case the clocks are stable
	 * throughout the entire runtime of the codec.
	 */
	bool enable_soft_reset;
};

#endif /* __CS4271_H */
