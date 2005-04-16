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
	u16 LeftGains[20];	//0xd0
	u16 RightGains[20];	//0xe4
} auxxEqCoeffSet_t;

typedef struct {
	unsigned int *this00;	/*CAsp4HwIO */
	long this04;		/* How many filters for each side (default = 10) */
	long this08;		/* inited to cero. Stereo flag? */
} eqhw_t;

typedef struct {
	unsigned int *this00;	/*CAsp4Core */
	eqhw_t this04;		/* CHwEq */
	short this08;		/* Bad codec flag ? SetBypassGain: bypass gain */
	short this0a;
	short this0c;		/* SetBypassGain: bypass gain when this28 is not set. */
	short this0e;

	long this10;		/* How many gains are used for each side (right or left). */
	u16 this14[32];		/* SetLeftGainsTarget: Left (and right?) EQ gains  */
	long this24;
	long this28;		/* flag related to EQ enabled or not. Gang flag ? */
	long this54;		/* SetBypass */
	long this58;
	long this5c;
	/*0x60 */ auxxEqCoeffSet_t coefset;
	/* 50 u16 word each channel. */
	u16 this130[20];	/* Left and Right gains */
} eqlzr_t;

#endif
