/*
 *	opl3_hw.h	- Definitions of the OPL-3 registers
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 *	The OPL-3 mode is switched on by writing 0x01, to the offset 5
 *	of the right side.
 *
 *	Another special register at the right side is at offset 4. It contains
 *	a bit mask defining which voices are used as 4 OP voices.
 *
 *	The percussive mode is implemented in the left side only.
 *
 *	With the above exceptions the both sides can be operated independently.
 *	
 *	A 4 OP voice can be created by setting the corresponding
 *	bit at offset 4 of the right side.
 *
 *	For example setting the rightmost bit (0x01) changes the
 *	first voice on the right side to the 4 OP mode. The fourth
 *	voice is made inaccessible.
 *
 *	If a voice is set to the 2 OP mode, it works like 2 OP modes
 *	of the original YM3812 (AdLib). In addition the voice can 
 *	be connected the left, right or both stereo channels. It can
 *	even be left unconnected. This works with 4 OP voices also.
 *
 *	The stereo connection bits are located in the FEEDBACK_CONNECTION
 *	register of the voice (0xC0-0xC8). In 4 OP voices these bits are
 *	in the second half of the voice.
 */

/*
 *	Register numbers for the global registers
 */

#define TEST_REGISTER				0x01
#define   ENABLE_WAVE_SELECT		0x20

#define TIMER1_REGISTER				0x02
#define TIMER2_REGISTER				0x03
#define TIMER_CONTROL_REGISTER			0x04	/* Left side */
#define   IRQ_RESET			0x80
#define   TIMER1_MASK			0x40
#define   TIMER2_MASK			0x20
#define   TIMER1_START			0x01
#define   TIMER2_START			0x02

#define CONNECTION_SELECT_REGISTER		0x04	/* Right side */
#define   RIGHT_4OP_0			0x01
#define   RIGHT_4OP_1			0x02
#define   RIGHT_4OP_2			0x04
#define   LEFT_4OP_0			0x08
#define   LEFT_4OP_1			0x10
#define   LEFT_4OP_2			0x20

#define OPL3_MODE_REGISTER			0x05	/* Right side */
#define   OPL3_ENABLE			0x01
#define   OPL4_ENABLE			0x02

#define KBD_SPLIT_REGISTER			0x08	/* Left side */
#define   COMPOSITE_SINE_WAVE_MODE	0x80		/* Don't use with OPL-3? */
#define   KEYBOARD_SPLIT		0x40

#define PERCOSSION_REGISTER			0xbd	/* Left side only */
#define   TREMOLO_DEPTH			0x80
#define   VIBRATO_DEPTH			0x40
#define	  PERCOSSION_ENABLE		0x20
#define   BASSDRUM_ON			0x10
#define   SNAREDRUM_ON			0x08
#define   TOMTOM_ON			0x04
#define   CYMBAL_ON			0x02
#define   HIHAT_ON			0x01

/*
 *	Offsets to the register banks for operators. To get the
 *	register number just add the operator offset to the bank offset
 *
 *	AM/VIB/EG/KSR/Multiple (0x20 to 0x35)
 */
#define AM_VIB					0x20
#define   TREMOLO_ON			0x80
#define   VIBRATO_ON			0x40
#define   SUSTAIN_ON			0x20
#define   KSR				0x10 	/* Key scaling rate */
#define   MULTIPLE_MASK		0x0f	/* Frequency multiplier */

 /*
  *	KSL/Total level (0x40 to 0x55)
  */
#define KSL_LEVEL				0x40
#define   KSL_MASK			0xc0	/* Envelope scaling bits */
#define   TOTAL_LEVEL_MASK		0x3f	/* Strength (volume) of OP */

/*
 *	Attack / Decay rate (0x60 to 0x75)
 */
#define ATTACK_DECAY				0x60
#define   ATTACK_MASK			0xf0
#define   DECAY_MASK			0x0f

/*
 * Sustain level / Release rate (0x80 to 0x95)
 */
#define SUSTAIN_RELEASE				0x80
#define   SUSTAIN_MASK			0xf0
#define   RELEASE_MASK			0x0f

/*
 * Wave select (0xE0 to 0xF5)
 */
#define WAVE_SELECT			0xe0

/*
 *	Offsets to the register banks for voices. Just add to the
 *	voice number to get the register number.
 *
 *	F-Number low bits (0xA0 to 0xA8).
 */
#define FNUM_LOW				0xa0

/*
 *	F-number high bits / Key on / Block (octave) (0xB0 to 0xB8)
 */
#define KEYON_BLOCK					0xb0
#define	  KEYON_BIT				0x20
#define	  BLOCKNUM_MASK				0x1c
#define   FNUM_HIGH_MASK			0x03

/*
 *	Feedback / Connection (0xc0 to 0xc8)
 *
 *	These registers have two new bits when the OPL-3 mode
 *	is selected. These bits controls connecting the voice
 *	to the stereo channels. For 4 OP voices this bit is
 *	defined in the second half of the voice (add 3 to the
 *	register offset).
 *
 *	For 4 OP voices the connection bit is used in the
 *	both halves (gives 4 ways to connect the operators).
 */
#define FEEDBACK_CONNECTION				0xc0
#define   FEEDBACK_MASK				0x0e	/* Valid just for 1st OP of a voice */
#define   CONNECTION_BIT			0x01
/*
 *	In the 4 OP mode there is four possible configurations how the
 *	operators can be connected together (in 2 OP modes there is just
 *	AM or FM). The 4 OP connection mode is defined by the rightmost
 *	bit of the FEEDBACK_CONNECTION (0xC0-0xC8) on the both halves.
 *
 *	First half	Second half	Mode
 *
 *					 +---+
 *					 v   |
 *	0		0		>+-1-+--2--3--4-->
 *
 *
 *					
 *					 +---+
 *					 |   |
 *	0		1		>+-1-+--2-+
 *						  |->
 *					>--3----4-+
 *					
 *					 +---+
 *					 |   |
 *	1		0		>+-1-+-----+
 *						   |->
 *					>--2--3--4-+
 *
 *					 +---+
 *					 |   |
 *	1		1		>+-1-+--+
 *						|
 *					>--2--3-+->
 *						|
 *					>--4----+
 */
#define   STEREO_BITS				0x30	/* OPL-3 only */
#define     VOICE_TO_LEFT		0x10
#define     VOICE_TO_RIGHT		0x20

/*
 * 	Definition table for the physical voices
 */

struct physical_voice_info {
		unsigned char voice_num;
		unsigned char voice_mode; /* 0=unavailable, 2=2 OP, 4=4 OP */
		unsigned short ioaddr; /* I/O port (left or right side) */
		unsigned char op[4]; /* Operator offsets */
	};

/*
 *	There is 18 possible 2 OP voices
 *	(9 in the left and 9 in the right).
 *	The first OP is the modulator and 2nd is the carrier.
 *
 *	The first three voices in the both sides may be connected
 *	with another voice to a 4 OP voice. For example voice 0
 *	can be connected with voice 3. The operators of voice 3 are
 *	used as operators 3 and 4 of the new 4 OP voice.
 *	In this case the 2 OP voice number 0 is the 'first half' and
 *	voice 3 is the second.
 */

#define USE_LEFT	0
#define USE_RIGHT	1

static struct physical_voice_info pv_map[18] =
{
/*       No Mode Side		OP1	OP2	OP3   OP4	*/
/*	---------------------------------------------------	*/
	{ 0,  2, USE_LEFT,	{0x00,	0x03,	0x08, 0x0b}},
	{ 1,  2, USE_LEFT,	{0x01,	0x04,	0x09, 0x0c}},
	{ 2,  2, USE_LEFT,	{0x02,	0x05,	0x0a, 0x0d}},

	{ 3,  2, USE_LEFT,	{0x08,	0x0b,	0x00, 0x00}},
	{ 4,  2, USE_LEFT,	{0x09,	0x0c,	0x00, 0x00}},
	{ 5,  2, USE_LEFT,	{0x0a,	0x0d,	0x00, 0x00}},

	{ 6,  2, USE_LEFT,	{0x10,	0x13,	0x00, 0x00}}, /* Used by percussive voices */
	{ 7,  2, USE_LEFT,	{0x11,	0x14,	0x00, 0x00}}, /* if the percussive mode */
	{ 8,  2, USE_LEFT,	{0x12,	0x15,	0x00, 0x00}}, /* is selected */

	{ 0,  2, USE_RIGHT,	{0x00,	0x03,	0x08, 0x0b}},
	{ 1,  2, USE_RIGHT,	{0x01,	0x04,	0x09, 0x0c}},
	{ 2,  2, USE_RIGHT,	{0x02,	0x05,	0x0a, 0x0d}},

	{ 3,  2, USE_RIGHT,	{0x08,	0x0b,	0x00, 0x00}},
	{ 4,  2, USE_RIGHT,	{0x09,	0x0c,	0x00, 0x00}},
	{ 5,  2, USE_RIGHT,	{0x0a,	0x0d,	0x00, 0x00}},

	{ 6,  2, USE_RIGHT,	{0x10,	0x13,	0x00, 0x00}},
	{ 7,  2, USE_RIGHT,	{0x11,	0x14,	0x00, 0x00}},
	{ 8,  2, USE_RIGHT,	{0x12,	0x15,	0x00, 0x00}}
};
/*
 *	DMA buffer calls
 */
