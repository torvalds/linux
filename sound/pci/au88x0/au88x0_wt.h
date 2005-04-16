/***************************************************************************
 *           WT register offsets.
 *
 *  Wed Oct 22 13:50:20 2003
 *  Copyright  2003  mjander
 *  mjander@users.sourceforge.org
 ****************************************************************************/
#ifndef _AU88X0_WT_H
#define _AU88X0_WT_H

/* WT channels are grouped in banks. Each bank has 0x20 channels. */
/* Bank register address boundary is 0x8000 */

#define NR_WT_PB 0x20

/* WT bank base register (as dword address). */
#define WT_BAR(x) (((x)&0xffe0)<<0x8)
#define WT_BANK(x) (x>>5)
/* WT Bank registers */
#define WT_CTRL(bank)	(((((bank)&1)<<0xd) + 0x00)<<2)	/* 0x0000 */
#define WT_SRAMP(bank)	(((((bank)&1)<<0xd) + 0x01)<<2)	/* 0x0004 */
#define WT_DSREG(bank)	(((((bank)&1)<<0xd) + 0x02)<<2)	/* 0x0008 */
#define WT_MRAMP(bank)	(((((bank)&1)<<0xd) + 0x03)<<2)	/* 0x000c */
#define WT_GMODE(bank)	(((((bank)&1)<<0xd) + 0x04)<<2)	/* 0x0010 */
#define WT_ARAMP(bank)	(((((bank)&1)<<0xd) + 0x05)<<2)	/* 0x0014 */
/* WT Voice registers */
#define WT_STEREO(voice)	((WT_BAR(voice)+ 0x20 +(((voice)&0x1f)>>1))<<2)	/* 0x0080 */
#define WT_MUTE(voice)		((WT_BAR(voice)+ 0x40 +((voice)&0x1f))<<2)	/* 0x0100 */
#define WT_RUN(voice)		((WT_BAR(voice)+ 0x60 +((voice)&0x1f))<<2)	/* 0x0180 */
/* Some kind of parameters. */
/* PARM0, PARM1 : Filter (0xFF000000), SampleRate (0x0000FFFF) */
/* PARM2, PARM3 : Still unknown */
#define WT_PARM(x,y)	(((WT_BAR(x))+ 0x80 +(((x)&0x1f)<<2)+(y))<<2)	/* 0x0200 */
#define WT_DELAY(x,y)	(((WT_BAR(x))+ 0x100 +(((x)&0x1f)<<2)+(y))<<2)	/* 0x0400 */

/* Numeric indexes used by SetReg() and GetReg() */
#if 0
enum {
	run = 0,		/* 0  W 1:run 0:stop */
	parm0,			/* 1  W filter, samplerate */
	parm1,			/* 2  W filter, samplerate */
	parm2,			/* 3  W  */
	parm3,			/* 4  RW volume. This value is calculated using floating point ops. */
	sramp,			/* 5  W */
	mute,			/* 6  W 1:mute, 0:unmute */
	gmode,			/* 7  RO Looks like only bit0 is used. */
	aramp,			/* 8  W */
	mramp,			/* 9  W */
	ctrl,			/* a  W */
	delay,			/* b  W All 4 values are written at once with same value. */
	dsreg,			/* c  (R)W */
} wt_reg;
#endif

typedef struct {
	unsigned int parm0;	/* this_1E4 */
	unsigned int parm1;	/* this_1E8 */
	unsigned int parm2;	/* this_1EC */
	unsigned int parm3;	/* this_1F0 */
	unsigned int this_1D0;
} wt_voice_t;

#endif				/* _AU88X0_WT_H */

/* End of file */
