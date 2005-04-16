/* 
 * lib/reed_solomon/encode_rs.c
 *
 * Overview:
 *   Generic Reed Solomon encoder / decoder library
 *   
 * Copyright 2002, Phil Karn, KA9Q
 * May be used under the terms of the GNU General Public License (GPL)
 *
 * Adaption to the kernel by Thomas Gleixner (tglx@linutronix.de)
 *
 * $Id: encode_rs.c,v 1.4 2004/10/22 15:41:47 gleixner Exp $
 *
 */

/* Generic data width independent code which is included by the 
 * wrappers.
 * int encode_rsX (struct rs_control *rs, uintX_t *data, int len, uintY_t *par)
 */
{
	int i, j, pad;
	int nn = rs->nn;
	int nroots = rs->nroots;
	uint16_t *alpha_to = rs->alpha_to;
	uint16_t *index_of = rs->index_of;
	uint16_t *genpoly = rs->genpoly;
	uint16_t fb;
	uint16_t msk = (uint16_t) rs->nn;

	/* Check length parameter for validity */
	pad = nn - nroots - len;
	if (pad < 0 || pad >= nn)
		return -ERANGE;

	for (i = 0; i < len; i++) {
		fb = index_of[((((uint16_t) data[i])^invmsk) & msk) ^ par[0]];
		/* feedback term is non-zero */
		if (fb != nn) {	
			for (j = 1; j < nroots; j++) {
				par[j] ^= alpha_to[rs_modnn(rs, fb + 
							 genpoly[nroots - j])];
			}
		}
		/* Shift */
		memmove(&par[0], &par[1], sizeof(uint16_t) * (nroots - 1));
		if (fb != nn) {
			par[nroots - 1] = alpha_to[rs_modnn(rs, 
							    fb + genpoly[0])];
		} else {
			par[nroots - 1] = 0;
		}
	}
	return 0;
}
