/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * hdlc.h  --  General purpose ISDN HDLC decoder.
 *
 * Implementation of a HDLC decoder/encoder in software.
 * Necessary because some ISDN devices don't have HDLC
 * controllers.
 *
 * Copyright (C)
 *	2009	Karsten Keil		<keil@b1-systems.de>
 *	2002	Wolfgang Mües		<wolfgang@iksw-muees.de>
 *	2001	Frode Isaksen		<fisaksen@bewan.com>
 *	2001	Kai Germaschewski	<kai.germaschewski@gmx.de>
 */

#ifndef __ISDNHDLC_H__
#define __ISDNHDLC_H__

struct isdnhdlc_vars {
	int bit_shift;
	int hdlc_bits1;
	int data_bits;
	int ffbit_shift;	/* encoding only */
	int state;
	int dstpos;

	u16 crc;

	u8 cbin;
	u8 shift_reg;
	u8 ffvalue;

	/* set if transferring data */
	u32 data_received:1;
	/* set if D channel (send idle instead of flags) */
	u32 dchannel:1;
	/* set if 56K adaptation */
	u32 do_adapt56:1;
	/* set if in closing phase (need to send CRC + flag) */
	u32 do_closing:1;
	/* set if data is bitreverse */
	u32 do_bitreverse:1;
};

/* Feature Flags */
#define HDLC_56KBIT	0x01
#define HDLC_DCHANNEL	0x02
#define HDLC_BITREVERSE	0x04

/*
  The return value from isdnhdlc_decode is
  the frame length, 0 if no complete frame was decoded,
  or a negative error number
*/
#define HDLC_FRAMING_ERROR     1
#define HDLC_CRC_ERROR         2
#define HDLC_LENGTH_ERROR      3

extern void	isdnhdlc_rcv_init(struct isdnhdlc_vars *hdlc, u32 features);

extern int	isdnhdlc_decode(struct isdnhdlc_vars *hdlc, const u8 *src,
			int slen, int *count, u8 *dst, int dsize);

extern void	isdnhdlc_out_init(struct isdnhdlc_vars *hdlc, u32 features);

extern int	isdnhdlc_encode(struct isdnhdlc_vars *hdlc, const u8 *src,
			u16 slen, int *count, u8 *dst, int dsize);

#endif /* __ISDNHDLC_H__ */
