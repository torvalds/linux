/*
 * i2sbus driver -- bus register definitions
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 */
#ifndef __I2SBUS_CONTROLREGS_H
#define __I2SBUS_CONTROLREGS_H

/* i2s control registers, at least what we know about them */

#define __PAD(m,n) u8 __pad##m[n]
#define _PAD(line, n) __PAD(line, n)
#define PAD(n) _PAD(__LINE__, (n))
struct i2s_control_regs {
	PAD(0x38);
	__le32 fcr0;		/* 0x38 (unknown) */
	__le32 cell_control;	/* 0x3c (fcr1) */
	__le32 fcr2;		/* 0x40 (unknown) */
	__le32 fcr3;		/* 0x44 (fcr3) */
	__le32 clock_control;	/* 0x48 (unknown) */
	PAD(4);
	/* total size: 0x50 bytes */
}  __attribute__((__packed__));

#define CTRL_CLOCK_CELL_0_ENABLE	(1<<10)
#define CTRL_CLOCK_CLOCK_0_ENABLE	(1<<12)
#define CTRL_CLOCK_SWRESET_0		(1<<11)
#define CTRL_CLOCK_INTF_0_ENABLE	(1<<13)

#define CTRL_CLOCK_CELL_1_ENABLE	(1<<17)
#define CTRL_CLOCK_CLOCK_1_ENABLE	(1<<18)
#define CTRL_CLOCK_SWRESET_1		(1<<19)
#define CTRL_CLOCK_INTF_1_ENABLE	(1<<20)

#endif /* __I2SBUS_CONTROLREGS_H */
