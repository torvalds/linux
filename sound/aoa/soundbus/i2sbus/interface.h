/*
 * i2sbus driver -- interface register definitions
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 */
#ifndef __I2SBUS_INTERFACE_H
#define __I2SBUS_INTERFACE_H

/* i2s bus control registers, at least what we know about them */

#define __PAD(m,n) u8 __pad##m[n]
#define _PAD(line, n) __PAD(line, n)
#define PAD(n) _PAD(__LINE__, (n))
struct i2s_interface_regs {
	__le32 intr_ctl;	/* 0x00 */
	PAD(12);
	__le32 serial_format;	/* 0x10 */
	PAD(12);
	__le32 codec_msg_out;	/* 0x20 */
	PAD(12);
	__le32 codec_msg_in;	/* 0x30 */
	PAD(12);
	__le32 frame_count;	/* 0x40 */
	PAD(12);
	__le32 frame_match;	/* 0x50 */
	PAD(12);
	__le32 data_word_sizes;	/* 0x60 */
	PAD(12);
	__le32 peak_level_sel;	/* 0x70 */
	PAD(12);
	__le32 peak_level_in0;	/* 0x80 */
	PAD(12);
	__le32 peak_level_in1;	/* 0x90 */
	PAD(12);
	/* total size: 0x100 bytes */
}  __attribute__((__packed__));

/* interrupt register is just a bitfield with
 * interrupt enable and pending bits */
#define I2S_REG_INTR_CTL		0x00
#	define I2S_INT_FRAME_COUNT		(1<<31)
#	define I2S_PENDING_FRAME_COUNT		(1<<30)
#	define I2S_INT_MESSAGE_FLAG		(1<<29)
#	define I2S_PENDING_MESSAGE_FLAG		(1<<28)
#	define I2S_INT_NEW_PEAK			(1<<27)
#	define I2S_PENDING_NEW_PEAK		(1<<26)
#	define I2S_INT_CLOCKS_STOPPED		(1<<25)
#	define I2S_PENDING_CLOCKS_STOPPED	(1<<24)
#	define I2S_INT_EXTERNAL_SYNC_ERROR	(1<<23)
#	define I2S_PENDING_EXTERNAL_SYNC_ERROR	(1<<22)
#	define I2S_INT_EXTERNAL_SYNC_OK		(1<<21)
#	define I2S_PENDING_EXTERNAL_SYNC_OK	(1<<20)
#	define I2S_INT_NEW_SAMPLE_RATE		(1<<19)
#	define I2S_PENDING_NEW_SAMPLE_RATE	(1<<18)
#	define I2S_INT_STATUS_FLAG		(1<<17)
#	define I2S_PENDING_STATUS_FLAG		(1<<16)

/* serial format register is more interesting :)
 * It contains:
 *  - clock source
 *  - MClk divisor
 *  - SClk divisor
 *  - SClk master flag
 *  - serial format (sony, i2s 64x, i2s 32x, dav, silabs)
 *  - external sample frequency interrupt (don't understand)
 *  - external sample frequency
 */
#define I2S_REG_SERIAL_FORMAT		0x10
/* clock source. You get either 18.432, 45.1584 or 49.1520 MHz */
#	define I2S_SF_CLOCK_SOURCE_SHIFT	30
#	define I2S_SF_CLOCK_SOURCE_MASK		(3<<I2S_SF_CLOCK_SOURCE_SHIFT)
#	define I2S_SF_CLOCK_SOURCE_18MHz	(0<<I2S_SF_CLOCK_SOURCE_SHIFT)
#	define I2S_SF_CLOCK_SOURCE_45MHz	(1<<I2S_SF_CLOCK_SOURCE_SHIFT)
#	define I2S_SF_CLOCK_SOURCE_49MHz	(2<<I2S_SF_CLOCK_SOURCE_SHIFT)
/* also, let's define the exact clock speeds here, in Hz */
#define I2S_CLOCK_SPEED_18MHz	18432000
#define I2S_CLOCK_SPEED_45MHz	45158400
#define I2S_CLOCK_SPEED_49MHz	49152000
/* MClk is the clock that drives the codec, usually called its 'system clock'.
 * It is derived by taking only every 'divisor' tick of the clock.
 */
#	define I2S_SF_MCLKDIV_SHIFT		24
#	define I2S_SF_MCLKDIV_MASK		(0x1F<<I2S_SF_MCLKDIV_SHIFT)
#	define I2S_SF_MCLKDIV_1			(0x14<<I2S_SF_MCLKDIV_SHIFT)
#	define I2S_SF_MCLKDIV_3			(0x13<<I2S_SF_MCLKDIV_SHIFT)
#	define I2S_SF_MCLKDIV_5			(0x12<<I2S_SF_MCLKDIV_SHIFT)
#	define I2S_SF_MCLKDIV_14		(0x0E<<I2S_SF_MCLKDIV_SHIFT)
#	define I2S_SF_MCLKDIV_OTHER(div)	(((div/2-1)<<I2S_SF_MCLKDIV_SHIFT)&I2S_SF_MCLKDIV_MASK)
static inline int i2s_sf_mclkdiv(int div, int *out)
{
	int d;

	switch(div) {
	case 1: *out |= I2S_SF_MCLKDIV_1; return 0;
	case 3: *out |= I2S_SF_MCLKDIV_3; return 0;
	case 5: *out |= I2S_SF_MCLKDIV_5; return 0;
	case 14: *out |= I2S_SF_MCLKDIV_14; return 0;
	default:
		if (div%2) return -1;
		d = div/2-1;
		if (d == 0x14 || d == 0x13 || d == 0x12 || d == 0x0E)
			return -1;
		*out |= I2S_SF_MCLKDIV_OTHER(div);
		return 0;
	}
}
/* SClk is the clock that drives the i2s wire bus. Note that it is
 * derived from the MClk above by taking only every 'divisor' tick
 * of MClk.
 */
#	define I2S_SF_SCLKDIV_SHIFT		20
#	define I2S_SF_SCLKDIV_MASK		(0xF<<I2S_SF_SCLKDIV_SHIFT)
#	define I2S_SF_SCLKDIV_1			(8<<I2S_SF_SCLKDIV_SHIFT)
#	define I2S_SF_SCLKDIV_3			(9<<I2S_SF_SCLKDIV_SHIFT)
#	define I2S_SF_SCLKDIV_OTHER(div)	(((div/2-1)<<I2S_SF_SCLKDIV_SHIFT)&I2S_SF_SCLKDIV_MASK)
static inline int i2s_sf_sclkdiv(int div, int *out)
{
	int d;

	switch(div) {
	case 1: *out |= I2S_SF_SCLKDIV_1; return 0;
	case 3: *out |= I2S_SF_SCLKDIV_3; return 0;
	default:
		if (div%2) return -1;
		d = div/2-1;
		if (d == 8 || d == 9) return -1;
		*out |= I2S_SF_SCLKDIV_OTHER(div);
		return 0;
	}
}
#	define I2S_SF_SCLK_MASTER		(1<<19)
/* serial format is the way the data is put to the i2s wire bus */
#	define I2S_SF_SERIAL_FORMAT_SHIFT	16
#	define I2S_SF_SERIAL_FORMAT_MASK	(7<<I2S_SF_SERIAL_FORMAT_SHIFT)
#	define I2S_SF_SERIAL_FORMAT_SONY	(0<<I2S_SF_SERIAL_FORMAT_SHIFT)
#	define I2S_SF_SERIAL_FORMAT_I2S_64X	(1<<I2S_SF_SERIAL_FORMAT_SHIFT)
#	define I2S_SF_SERIAL_FORMAT_I2S_32X	(2<<I2S_SF_SERIAL_FORMAT_SHIFT)
#	define I2S_SF_SERIAL_FORMAT_I2S_DAV	(4<<I2S_SF_SERIAL_FORMAT_SHIFT)
#	define I2S_SF_SERIAL_FORMAT_I2S_SILABS	(5<<I2S_SF_SERIAL_FORMAT_SHIFT)
/* unknown */
#	define I2S_SF_EXT_SAMPLE_FREQ_INT_SHIFT	12
#	define I2S_SF_EXT_SAMPLE_FREQ_INT_MASK	(0xF<<I2S_SF_SAMPLE_FREQ_INT_SHIFT)
/* probably gives external frequency? */
#	define I2S_SF_EXT_SAMPLE_FREQ_MASK	0xFFF

/* used to send codec messages, but how isn't clear */
#define I2S_REG_CODEC_MSG_OUT		0x20

/* used to receive codec messages, but how isn't clear */
#define I2S_REG_CODEC_MSG_IN		0x30

/* frame count reg isn't clear to me yet, but probably useful */
#define I2S_REG_FRAME_COUNT		0x40

/* program to some value, and get interrupt if frame count reaches it */
#define I2S_REG_FRAME_MATCH		0x50

/* this register describes how the bus transfers data */
#define I2S_REG_DATA_WORD_SIZES		0x60
/* number of interleaved input channels */
#	define I2S_DWS_NUM_CHANNELS_IN_SHIFT	24
#	define I2S_DWS_NUM_CHANNELS_IN_MASK	(0x1F<<I2S_DWS_NUM_CHANNELS_IN_SHIFT)
/* word size of input data */
#	define I2S_DWS_DATA_IN_SIZE_SHIFT	16
#	define I2S_DWS_DATA_IN_16BIT		(0<<I2S_DWS_DATA_IN_SIZE_SHIFT)
#	define I2S_DWS_DATA_IN_24BIT		(3<<I2S_DWS_DATA_IN_SIZE_SHIFT)
/* number of interleaved output channels */
#	define I2S_DWS_NUM_CHANNELS_OUT_SHIFT	8
#	define I2S_DWS_NUM_CHANNELS_OUT_MASK	(0x1F<<I2S_DWS_NUM_CHANNELS_OUT_SHIFT)
/* word size of output data */
#	define I2S_DWS_DATA_OUT_SIZE_SHIFT	0
#	define I2S_DWS_DATA_OUT_16BIT		(0<<I2S_DWS_DATA_OUT_SIZE_SHIFT)
#	define I2S_DWS_DATA_OUT_24BIT		(3<<I2S_DWS_DATA_OUT_SIZE_SHIFT)


/* unknown */
#define I2S_REG_PEAK_LEVEL_SEL		0x70

/* unknown */
#define I2S_REG_PEAK_LEVEL_IN0		0x80

/* unknown */
#define I2S_REG_PEAK_LEVEL_IN1		0x90

#endif /* __I2SBUS_INTERFACE_H */
