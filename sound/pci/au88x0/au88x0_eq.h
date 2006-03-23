#ifndef AU88X0_EQ_H
#define AU88X0_EQ_H

/***************************************************************************
 *            au88x0_eq.h
 *
 *  Definitions and constant data for the Aureal Hardware EQ.
 *
 *  Sun Jun  8 18:23:38 2003
 *  Author: Manuel Jander (mjander@users.sourceforge.net)
 ****************************************************************************/

typedef struct {
	u16 LeftCoefs[50];	//0x4
	u16 RightCoefs[50];	// 0x68
	u16 LeftGains[10];	//0xd0
	u16 RightGains[10];	//0xe4
} auxxEqCoeffSet_t;

typedef struct {
	s32 this04;		/* How many filters for each side (default = 10) */
	s32 this08;		/* inited to cero. Stereo flag? */
} eqhw_t;

typedef struct {
	eqhw_t this04;		/* CHwEq */
	u16 this08;		/* Bad codec flag ? SetBypassGain: bypass gain */
	u16 this0a;
	u16 this0c;		/* SetBypassGain: bypass gain when this28 is not set. */
	u16 this0e;

	s32 this10;		/* How many gains are used for each side (right or left). */
	u16 this14_array[10];	/* SetLeftGainsTarget: Left (and right?) EQ gains  */
	s32 this28;		/* flag related to EQ enabled or not. Gang flag ? */
	s32 this54;		/* SetBypass */
	s32 this58;
	s32 this5c;
	/*0x60 */ auxxEqCoeffSet_t coefset;
	/* 50 u16 word each channel. */
	u16 this130[20];	/* Left and Right gains */
} eqlzr_t;

#endif
