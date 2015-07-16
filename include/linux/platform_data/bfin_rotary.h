/*
 * board initialization should put one of these structures into platform_data
 * and place the bfin-rotary onto platform_bus named "bfin-rotary".
 *
 * Copyright 2008-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BFIN_ROTARY_H
#define _BFIN_ROTARY_H

/* mode bitmasks */
#define ROT_QUAD_ENC	CNTMODE_QUADENC	/* quadrature/grey code encoder mode */
#define ROT_BIN_ENC	CNTMODE_BINENC	/* binary encoder mode */
#define ROT_UD_CNT	CNTMODE_UDCNT	/* rotary counter mode */
#define ROT_DIR_CNT	CNTMODE_DIRCNT	/* direction counter mode */

#define ROT_DEBE	DEBE		/* Debounce Enable */

#define ROT_CDGINV	CDGINV		/* CDG Pin Polarity Invert */
#define ROT_CUDINV	CUDINV		/* CUD Pin Polarity Invert */
#define ROT_CZMINV	CZMINV		/* CZM Pin Polarity Invert */

struct bfin_rotary_platform_data {
	/* set rotary UP KEY_### or BTN_### in case you prefer
	 * bfin-rotary to send EV_KEY otherwise set 0
	 */
	unsigned int rotary_up_key;
	/* set rotary DOWN KEY_### or BTN_### in case you prefer
	 * bfin-rotary to send EV_KEY otherwise set 0
	 */
	unsigned int rotary_down_key;
	/* set rotary BUTTON KEY_### or BTN_### */
	unsigned int rotary_button_key;
	/* set rotary Relative Axis REL_### in case you prefer
	 * bfin-rotary to send EV_REL otherwise set 0
	 */
	unsigned int rotary_rel_code;
	unsigned short debounce;	/* 0..17 */
	unsigned short mode;
	unsigned short pm_wakeup;
	unsigned short *pin_list;
};

/* CNT_CONFIG bitmasks */
#define CNTE		(1 << 0)	/* Counter Enable */
#define DEBE		(1 << 1)	/* Debounce Enable */
#define CDGINV		(1 << 4)	/* CDG Pin Polarity Invert */
#define CUDINV		(1 << 5)	/* CUD Pin Polarity Invert */
#define CZMINV		(1 << 6)	/* CZM Pin Polarity Invert */
#define CNTMODE_SHIFT	8
#define CNTMODE		(0x7 << CNTMODE_SHIFT)	/* Counter Operating Mode */
#define ZMZC		(1 << 1)	/* CZM Zeroes Counter Enable */
#define BNDMODE_SHIFT	12
#define BNDMODE		(0x3 << BNDMODE_SHIFT)	/* Boundary register Mode */
#define INPDIS		(1 << 15)	/* CUG and CDG Input Disable */

#define CNTMODE_QUADENC	(0 << CNTMODE_SHIFT)	/* quadrature encoder mode */
#define CNTMODE_BINENC	(1 << CNTMODE_SHIFT)	/* binary encoder mode */
#define CNTMODE_UDCNT	(2 << CNTMODE_SHIFT)	/* up/down counter mode */
#define CNTMODE_DIRCNT	(4 << CNTMODE_SHIFT)	/* direction counter mode */
#define CNTMODE_DIRTMR	(5 << CNTMODE_SHIFT)	/* direction timer mode */

#define BNDMODE_COMP	(0 << BNDMODE_SHIFT)	/* boundary compare mode */
#define BNDMODE_ZERO	(1 << BNDMODE_SHIFT)	/* boundary compare and zero mode */
#define BNDMODE_CAPT	(2 << BNDMODE_SHIFT)	/* boundary capture mode */
#define BNDMODE_AEXT	(3 << BNDMODE_SHIFT)	/* boundary auto-extend mode */

/* CNT_IMASK bitmasks */
#define ICIE		(1 << 0)	/* Illegal Gray/Binary Code Interrupt Enable */
#define UCIE		(1 << 1)	/* Up count Interrupt Enable */
#define DCIE		(1 << 2)	/* Down count Interrupt Enable */
#define MINCIE		(1 << 3)	/* Min Count Interrupt Enable */
#define MAXCIE		(1 << 4)	/* Max Count Interrupt Enable */
#define COV31IE		(1 << 5)	/* Bit 31 Overflow Interrupt Enable */
#define COV15IE		(1 << 6)	/* Bit 15 Overflow Interrupt Enable */
#define CZEROIE		(1 << 7)	/* Count to Zero Interrupt Enable */
#define CZMIE		(1 << 8)	/* CZM Pin Interrupt Enable */
#define CZMEIE		(1 << 9)	/* CZM Error Interrupt Enable */
#define CZMZIE		(1 << 10)	/* CZM Zeroes Counter Interrupt Enable */

/* CNT_STATUS bitmasks */
#define ICII		(1 << 0)	/* Illegal Gray/Binary Code Interrupt Identifier */
#define UCII		(1 << 1)	/* Up count Interrupt Identifier */
#define DCII		(1 << 2)	/* Down count Interrupt Identifier */
#define MINCII		(1 << 3)	/* Min Count Interrupt Identifier */
#define MAXCII		(1 << 4)	/* Max Count Interrupt Identifier */
#define COV31II		(1 << 5)	/* Bit 31 Overflow Interrupt Identifier */
#define COV15II		(1 << 6)	/* Bit 15 Overflow Interrupt Identifier */
#define CZEROII		(1 << 7)	/* Count to Zero Interrupt Identifier */
#define CZMII		(1 << 8)	/* CZM Pin Interrupt Identifier */
#define CZMEII		(1 << 9)	/* CZM Error Interrupt Identifier */
#define CZMZII		(1 << 10)	/* CZM Zeroes Counter Interrupt Identifier */

/* CNT_COMMAND bitmasks */
#define W1LCNT		0xf		/* Load Counter Register */
#define W1LMIN		0xf0		/* Load Min Register */
#define W1LMAX		0xf00		/* Load Max Register */
#define W1ZMONCE	(1 << 12)	/* Enable CZM Clear Counter Once */

#define W1LCNT_ZERO	(1 << 0)	/* write 1 to load CNT_COUNTER with zero */
#define W1LCNT_MIN	(1 << 2)	/* write 1 to load CNT_COUNTER from CNT_MIN */
#define W1LCNT_MAX	(1 << 3)	/* write 1 to load CNT_COUNTER from CNT_MAX */

#define W1LMIN_ZERO	(1 << 4)	/* write 1 to load CNT_MIN with zero */
#define W1LMIN_CNT	(1 << 5)	/* write 1 to load CNT_MIN from CNT_COUNTER */
#define W1LMIN_MAX	(1 << 7)	/* write 1 to load CNT_MIN from CNT_MAX */

#define W1LMAX_ZERO	(1 << 8)	/* write 1 to load CNT_MAX with zero */
#define W1LMAX_CNT	(1 << 9)	/* write 1 to load CNT_MAX from CNT_COUNTER */
#define W1LMAX_MIN	(1 << 10)	/* write 1 to load CNT_MAX from CNT_MIN */

/* CNT_DEBOUNCE bitmasks */
#define DPRESCALE	0xf		/* Load Counter Register */

#endif
