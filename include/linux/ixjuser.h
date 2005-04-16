#ifndef __LINUX_IXJUSER_H
#define __LINUX_IXJUSER_H

/******************************************************************************
 *
 *    ixjuser.h
 *
 * Device Driver for Quicknet Technologies, Inc.'s Telephony cards
 * including the Internet PhoneJACK, Internet PhoneJACK Lite,
 * Internet PhoneJACK PCI, Internet LineJACK, Internet PhoneCARD and
 * SmartCABLE
 *
 *    (c) Copyright 1999-2001  Quicknet Technologies, Inc.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version
 *    2 of the License, or (at your option) any later version.
 *
 * Author:          Ed Okerson, <eokerson@quicknet.net>
 *    
 * Contributors:    Greg Herlein, <gherlein@quicknet.net>
 *                  David W. Erhart, <derhart@quicknet.net>
 *                  John Sellers, <jsellers@quicknet.net>
 *                  Mike Preston, <mpreston@quicknet.net>
 *
 * More information about the hardware related to this driver can be found
 * at our website:    http://www.quicknet.net
 *
 * Fixes:
 *
 * IN NO EVENT SHALL QUICKNET TECHNOLOGIES, INC. BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF QUICKNET
 * TECHNOLOGIES, INC.HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * QUICKNET TECHNOLOGIES, INC. SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND QUICKNET TECHNOLOGIES, INC. HAS NO OBLIGATION 
 * TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *****************************************************************************/

static char ixjuser_h_rcsid[] = "$Id: ixjuser.h,v 4.1 2001/08/05 00:17:37 craigs Exp $";

#include <linux/telephony.h>


/******************************************************************************
*
* IOCTL's used for the Quicknet Telephony Cards
*
* If you use the IXJCTL_TESTRAM command, the card must be power cycled to
* reset the SRAM values before futher use.
*
******************************************************************************/

#define IXJCTL_DSP_RESET 		_IO  ('q', 0xC0)

#define IXJCTL_RING                     PHONE_RING
#define IXJCTL_HOOKSTATE                PHONE_HOOKSTATE
#define IXJCTL_MAXRINGS			PHONE_MAXRINGS
#define IXJCTL_RING_CADENCE		PHONE_RING_CADENCE
#define IXJCTL_RING_START		PHONE_RING_START
#define IXJCTL_RING_STOP		PHONE_RING_STOP

#define IXJCTL_CARDTYPE			_IOR ('q', 0xC1, int)
#define IXJCTL_SERIAL			_IOR ('q', 0xC2, int)
#define IXJCTL_DSP_TYPE                 _IOR ('q', 0xC3, int)
#define IXJCTL_DSP_VERSION              _IOR ('q', 0xC4, int)
#define IXJCTL_VERSION              	_IOR ('q', 0xDA, char *)
#define IXJCTL_DSP_IDLE			_IO  ('q', 0xC5)
#define IXJCTL_TESTRAM			_IO  ('q', 0xC6)

/******************************************************************************
*
* This group of IOCTLs deal with the record settings of the DSP
*
* The IXJCTL_REC_DEPTH command sets the internal buffer depth of the DSP.
* Setting a lower depth reduces latency, but increases the demand of the
* application to service the driver without frame loss.  The DSP has 480
* bytes of physical buffer memory for the record channel so the true
* maximum limit is determined by how many frames will fit in the buffer.
*
* 1 uncompressed (480 byte) 16-bit linear frame.
* 2 uncompressed (240 byte) 8-bit A-law/mu-law frames.
* 15 TrueSpeech 8.5 frames.
* 20 TrueSpeech 6.3,5.3,4.8 or 4.1 frames.
*
* The default in the driver is currently set to 2 frames.
*
* The IXJCTL_REC_VOLUME and IXJCTL_PLAY_VOLUME commands both use a Q8
* number as a parameter, 0x100 scales the signal by 1.0, 0x200 scales the
* signal by 2.0, 0x80 scales the signal by 0.5.  No protection is given
* against over-scaling, if the multiplication factor times the input
* signal exceeds 16 bits, overflow distortion will occur.  The default
* setting is 0x100 (1.0).
*
* The IXJCTL_REC_LEVEL returns the average signal level (not r.m.s.) on
* the most recently recorded frame as a 16 bit value.
******************************************************************************/

#define IXJCTL_REC_CODEC                PHONE_REC_CODEC
#define IXJCTL_REC_START                PHONE_REC_START
#define IXJCTL_REC_STOP                 PHONE_REC_STOP
#define IXJCTL_REC_DEPTH		PHONE_REC_DEPTH
#define IXJCTL_FRAME			PHONE_FRAME
#define IXJCTL_REC_VOLUME		PHONE_REC_VOLUME
#define IXJCTL_REC_LEVEL		PHONE_REC_LEVEL

typedef enum {
	f300_640 = 4, f300_500, f1100, f350, f400, f480, f440, f620, f20_50,
	f133_200, f300, f300_420, f330, f300_425, f330_440, f340, f350_400,
	f350_440, f350_450, f360, f380_420, f392, f400_425, f400_440, f400_450,
	f420, f425, f425_450, f425_475, f435, f440_450, f440_480, f445, f450,
	f452, f475, f480_620, f494, f500, f520, f523, f525, f540_660, f587,
	f590, f600, f660, f700, f740, f750, f750_1450, f770, f800, f816, f850,
	f857_1645, f900, f900_1300, f935_1215, f941_1477, f942, f950, f950_1400,
	f975, f1000, f1020, f1050, f1100_1750, f1140, f1200, f1209, f1330, f1336,
	lf1366, f1380, f1400, f1477, f1600, f1633_1638, f1800, f1860
} IXJ_FILTER_FREQ;

typedef struct {
	unsigned int filter;
	IXJ_FILTER_FREQ freq;
	char enable;
} IXJ_FILTER;

typedef struct {
	char enable;
	char en_filter;
	unsigned int filter;
	unsigned int on1;
	unsigned int off1;
	unsigned int on2;
	unsigned int off2;
	unsigned int on3;
	unsigned int off3;
} IXJ_FILTER_CADENCE;

#define IXJCTL_SET_FILTER		_IOW ('q', 0xC7, IXJ_FILTER *)
#define IXJCTL_SET_FILTER_RAW		_IOW ('q', 0xDD, IXJ_FILTER_RAW *)
#define IXJCTL_GET_FILTER_HIST		_IOW ('q', 0xC8, int)
#define IXJCTL_FILTER_CADENCE		_IOW ('q', 0xD6, IXJ_FILTER_CADENCE *)
#define IXJCTL_PLAY_CID			_IO  ('q', 0xD7)
/******************************************************************************
*
* This IOCTL allows you to reassign values in the tone index table.  The
* tone table has 32 entries (0 - 31), but the driver only allows entries
* 13 - 27 to be modified, entry 0 is reserved for silence and 1 - 12 are
* the standard DTMF digits and 28 - 31 are the DTMF tones for A, B, C & D.
* The positions used internally for Call Progress Tones are as follows:
*    Dial Tone   - 25
*    Ring Back   - 26
*    Busy Signal - 27
*
* The freq values are calculated as:
* freq = cos(2 * PI * frequency / 8000)
*
* The most commonly needed values are already calculated and listed in the
* enum IXJ_TONE_FREQ.  Each tone index can have two frequencies with
* different gains, if you are only using a single frequency set the unused
* one to 0.
*
* The gain values range from 0 to 15 indicating +6dB to -24dB in 2dB
* increments.
*
******************************************************************************/

typedef enum {
	hz20 = 0x7ffa,
	hz50 = 0x7fe5,
	hz133 = 0x7f4c,
	hz200 = 0x7e6b,
	hz261 = 0x7d50,		/* .63 C1  */
	hz277 = 0x7cfa,		/* .18 CS1 */
	hz293 = 0x7c9f,		/* .66 D1  */
	hz300 = 0x7c75,
	hz311 = 0x7c32,		/* .13 DS1 */
	hz329 = 0x7bbf,		/* .63 E1  */
	hz330 = 0x7bb8,
	hz340 = 0x7b75,
	hz349 = 0x7b37,		/* .23 F1  */
	hz350 = 0x7b30,
	hz360 = 0x7ae9,
	hz369 = 0x7aa8,		/* .99 FS1 */
	hz380 = 0x7a56,
	hz392 = 0x79fa,		/* .00 G1  */
	hz400 = 0x79bb,
	hz415 = 0x7941,		/* .30 GS1 */
	hz420 = 0x7918,
	hz425 = 0x78ee,
	hz435 = 0x7899,
	hz440 = 0x786d,		/* .00 A1  */
	hz445 = 0x7842,
	hz450 = 0x7815,
	hz452 = 0x7803,
	hz466 = 0x7784,		/* .16 AS1 */
	hz475 = 0x7731,
	hz480 = 0x7701,
	hz493 = 0x7685,		/* .88 B1  */
	hz494 = 0x767b,
	hz500 = 0x7640,
	hz520 = 0x7578,
	hz523 = 0x7559,		/* .25 C2  */
	hz525 = 0x7544,
	hz540 = 0x74a7,
	hz554 = 0x7411,		/* .37 CS2 */
	hz587 = 0x72a1,		/* .33 D2  */
	hz590 = 0x727f,
	hz600 = 0x720b,
	hz620 = 0x711e,
	hz622 = 0x7106,		/* .25 DS2 */
	hz659 = 0x6f3b,		/* .26 E2  */
	hz660 = 0x6f2e,
	hz698 = 0x6d3d,		/* .46 F2  */
	hz700 = 0x6d22,
	hz739 = 0x6b09,		/* .99 FS2 */
	hz740 = 0x6afa,
	hz750 = 0x6a6c,
	hz770 = 0x694b,
	hz783 = 0x688b,		/* .99 G2  */
	hz800 = 0x678d,
	hz816 = 0x6698,
	hz830 = 0x65bf,		/* .61 GS2 */
	hz850 = 0x6484,
	hz857 = 0x6414,
	hz880 = 0x629f,		/* .00 A2  */
	hz900 = 0x6154,
	hz932 = 0x5f35,		/* .33 AS2 */
	hz935 = 0x5f01,
	hz941 = 0x5e9a,
	hz942 = 0x5e88,
	hz950 = 0x5dfd,
	hz975 = 0x5c44,
	hz1000 = 0x5a81,
	hz1020 = 0x5912,
	hz1050 = 0x56e2,
	hz1100 = 0x5320,
	hz1140 = 0x5007,
	hz1200 = 0x4b3b,
	hz1209 = 0x4a80,
	hz1215 = 0x4a02,
	hz1250 = 0x471c,
	hz1300 = 0x42e0,
	hz1330 = 0x4049,
	hz1336 = 0x3fc4,
	hz1366 = 0x3d22,
	hz1380 = 0x3be4,
	hz1400 = 0x3a1b,
	hz1450 = 0x3596,
	hz1477 = 0x331c,
	hz1500 = 0x30fb,
	hz1600 = 0x278d,
	hz1633 = 0x2462,
	hz1638 = 0x23e7,
	hz1645 = 0x233a,
	hz1750 = 0x18f8,
	hz1800 = 0x1405,
	hz1860 = 0xe0b,
	hz2100 = 0xf5f6,
	hz2130 = 0xf2f5,
	hz2450 = 0xd3b3,
	hz2750 = 0xb8e4
} IXJ_FREQ;

typedef enum {
	C1 = hz261,
	CS1 = hz277,
	D1 = hz293,
	DS1 = hz311,
	E1 = hz329,
	F1 = hz349,
	FS1 = hz369,
	G1 = hz392,
	GS1 = hz415,
	A1 = hz440,
	AS1 = hz466,
	B1 = hz493,
	C2 = hz523,
	CS2 = hz554,
	D2 = hz587,
	DS2 = hz622,
	E2 = hz659,
	F2 = hz698,
	FS2 = hz739,
	G2 = hz783,
	GS2 = hz830,
	A2 = hz880,
	AS2 = hz932,
} IXJ_NOTE;

typedef struct {
	int tone_index;
	int freq0;
	int gain0;
	int freq1;
	int gain1;
} IXJ_TONE;

#define IXJCTL_INIT_TONE		_IOW ('q', 0xC9, IXJ_TONE *)

/******************************************************************************
*
* The IXJCTL_TONE_CADENCE ioctl defines tone sequences used for various
* Call Progress Tones (CPT).  This is accomplished by setting up an array of
* IXJ_CADENCE_ELEMENT structures that sequentially define the states of
* the tone sequence.  The tone_on_time and tone_off time are in
* 250 microsecond intervals.  A pointer to this array is passed to the
* driver as the ce element of an IXJ_CADENCE structure.  The elements_used
* must be set to the number of IXJ_CADENCE_ELEMENTS in the array.  The
* termination variable defines what to do at the end of a cadence, the
* options are to play the cadence once and stop, to repeat the last
* element of the cadence indefinitely, or to repeat the entire cadence
* indefinitely.  The ce variable is a pointer to the array of IXJ_TONE
* structures.  If the freq0 variable is non-zero, the tone table contents
* for the tone_index are updated to the frequencies and gains defined.  It
* should be noted that DTMF tones cannot be reassigned, so if DTMF tone
* table indexs are used in a cadence the frequency and gain variables will
* be ignored.
*
* If the array elements contain frequency parameters the driver will
* initialize the needed tone table elements and begin playing the tone,
* there is no preset limit on the number of elements in the cadence.  If
* there is more than one frequency used in the cadence, sequential elements
* of different frequencies MUST use different tone table indexes.  Only one
* cadence can be played at a time.  It is possible to build complex
* cadences with multiple frequencies using 2 tone table indexes by
* alternating between them.
*
******************************************************************************/

typedef struct {
	int index;
	int tone_on_time;
	int tone_off_time;
	int freq0;
	int gain0;
	int freq1;
	int gain1;
} IXJ_CADENCE_ELEMENT;

typedef enum {
	PLAY_ONCE,
	REPEAT_LAST_ELEMENT,
	REPEAT_ALL
} IXJ_CADENCE_TERM;

typedef struct {
	int elements_used;
	IXJ_CADENCE_TERM termination;
	IXJ_CADENCE_ELEMENT __user *ce;
} IXJ_CADENCE;

#define IXJCTL_TONE_CADENCE		_IOW ('q', 0xCA, IXJ_CADENCE *)
/******************************************************************************
*
* This group of IOCTLs deal with the playback settings of the DSP
*
******************************************************************************/

#define IXJCTL_PLAY_CODEC               PHONE_PLAY_CODEC
#define IXJCTL_PLAY_START               PHONE_PLAY_START
#define IXJCTL_PLAY_STOP                PHONE_PLAY_STOP
#define IXJCTL_PLAY_DEPTH		PHONE_PLAY_DEPTH
#define IXJCTL_PLAY_VOLUME		PHONE_PLAY_VOLUME
#define IXJCTL_PLAY_LEVEL		PHONE_PLAY_LEVEL

/******************************************************************************
*
* This group of IOCTLs deal with the Acoustic Echo Cancellation settings
* of the DSP
*
* Issuing the IXJCTL_AEC_START command with a value of AEC_OFF has the
* same effect as IXJCTL_AEC_STOP.  This is to simplify slider bar
* controls.  IXJCTL_AEC_GET_LEVEL returns the current setting of the AEC.
******************************************************************************/
#define IXJCTL_AEC_START		_IOW ('q', 0xCB, int)
#define IXJCTL_AEC_STOP			_IO  ('q', 0xCC)
#define IXJCTL_AEC_GET_LEVEL		_IO  ('q', 0xCD)

#define AEC_OFF   0
#define AEC_LOW   1
#define AEC_MED   2
#define AEC_HIGH  3
#define AEC_AUTO  4
#define AEC_AGC   5
/******************************************************************************
*
* Call Progress Tones, DTMF, etc.
* IXJCTL_DTMF_OOB determines if DTMF signaling is sent as Out-Of-Band
* only.  If you pass a 1, DTMF is suppressed from the audio stream.
* Tone on and off times are in 250 microsecond intervals so
* ioctl(ixj1, IXJCTL_SET_TONE_ON_TIME, 360);
* will set the tone on time of board ixj1 to 360 * 250us = 90ms
* the default values of tone on and off times is 840 or 210ms
******************************************************************************/

#define IXJCTL_DTMF_READY		PHONE_DTMF_READY
#define IXJCTL_GET_DTMF                 PHONE_GET_DTMF
#define IXJCTL_GET_DTMF_ASCII           PHONE_GET_DTMF_ASCII
#define IXJCTL_DTMF_OOB			PHONE_DTMF_OOB
#define IXJCTL_EXCEPTION		PHONE_EXCEPTION
#define IXJCTL_PLAY_TONE		PHONE_PLAY_TONE
#define IXJCTL_SET_TONE_ON_TIME		PHONE_SET_TONE_ON_TIME
#define IXJCTL_SET_TONE_OFF_TIME	PHONE_SET_TONE_OFF_TIME
#define IXJCTL_GET_TONE_ON_TIME		PHONE_GET_TONE_ON_TIME
#define IXJCTL_GET_TONE_OFF_TIME	PHONE_GET_TONE_OFF_TIME
#define IXJCTL_GET_TONE_STATE		PHONE_GET_TONE_STATE
#define IXJCTL_BUSY			PHONE_BUSY
#define IXJCTL_RINGBACK			PHONE_RINGBACK
#define IXJCTL_DIALTONE			PHONE_DIALTONE
#define IXJCTL_CPT_STOP			PHONE_CPT_STOP

/******************************************************************************
* LineJACK specific IOCTLs
*
* The lsb 4 bits of the LED argument represent the state of each of the 4
* LED's on the LineJACK
******************************************************************************/

#define IXJCTL_SET_LED			_IOW ('q', 0xCE, int)
#define IXJCTL_MIXER			_IOW ('q', 0xCF, int)

/******************************************************************************
* 
* The master volume controls use attenuation with 32 levels from 0 to -62dB
* with steps of 2dB each, the defines should be OR'ed together then sent
* as the parameter to the mixer command to change the mixer settings.
* 
******************************************************************************/
#define MIXER_MASTER_L		0x0000
#define MIXER_MASTER_R		0x0100
#define ATT00DB			0x00
#define ATT02DB			0x01
#define ATT04DB			0x02
#define ATT06DB			0x03
#define ATT08DB			0x04
#define ATT10DB			0x05
#define ATT12DB			0x06
#define ATT14DB			0x07
#define ATT16DB			0x08
#define ATT18DB			0x09
#define ATT20DB			0x0A
#define ATT22DB			0x0B
#define ATT24DB			0x0C
#define ATT26DB			0x0D
#define ATT28DB			0x0E
#define ATT30DB			0x0F
#define ATT32DB			0x10
#define ATT34DB			0x11
#define ATT36DB			0x12
#define ATT38DB			0x13
#define ATT40DB			0x14
#define ATT42DB			0x15
#define ATT44DB			0x16
#define ATT46DB			0x17
#define ATT48DB			0x18
#define ATT50DB			0x19
#define ATT52DB			0x1A
#define ATT54DB			0x1B
#define ATT56DB			0x1C
#define ATT58DB			0x1D
#define ATT60DB			0x1E
#define ATT62DB			0x1F
#define MASTER_MUTE		0x80

/******************************************************************************
* 
* The input volume controls use gain with 32 levels from +12dB to -50dB
* with steps of 2dB each, the defines should be OR'ed together then sent
* as the parameter to the mixer command to change the mixer settings.
* 
******************************************************************************/
#define MIXER_PORT_CD_L		0x0600
#define MIXER_PORT_CD_R		0x0700
#define MIXER_PORT_LINE_IN_L	0x0800
#define MIXER_PORT_LINE_IN_R	0x0900
#define MIXER_PORT_POTS_REC	0x0C00
#define MIXER_PORT_MIC		0x0E00

#define GAIN12DB		0x00
#define GAIN10DB		0x01
#define GAIN08DB		0x02
#define GAIN06DB		0x03
#define GAIN04DB		0x04
#define GAIN02DB		0x05
#define GAIN00DB		0x06
#define GAIN_02DB		0x07
#define GAIN_04DB		0x08
#define GAIN_06DB		0x09
#define GAIN_08DB		0x0A
#define GAIN_10DB		0x0B
#define GAIN_12DB		0x0C
#define GAIN_14DB		0x0D
#define GAIN_16DB		0x0E
#define GAIN_18DB		0x0F
#define GAIN_20DB		0x10
#define GAIN_22DB		0x11
#define GAIN_24DB		0x12
#define GAIN_26DB		0x13
#define GAIN_28DB		0x14
#define GAIN_30DB		0x15
#define GAIN_32DB		0x16
#define GAIN_34DB		0x17
#define GAIN_36DB		0x18
#define GAIN_38DB		0x19
#define GAIN_40DB		0x1A
#define GAIN_42DB		0x1B
#define GAIN_44DB		0x1C
#define GAIN_46DB		0x1D
#define GAIN_48DB		0x1E
#define GAIN_50DB		0x1F
#define INPUT_MUTE		0x80

/******************************************************************************
* 
* The POTS volume control use attenuation with 8 levels from 0dB to -28dB
* with steps of 4dB each, the defines should be OR'ed together then sent
* as the parameter to the mixer command to change the mixer settings.
* 
******************************************************************************/
#define MIXER_PORT_POTS_PLAY	0x0F00

#define POTS_ATT_00DB		0x00
#define POTS_ATT_04DB		0x01
#define POTS_ATT_08DB		0x02
#define POTS_ATT_12DB		0x03
#define POTS_ATT_16DB		0x04
#define POTS_ATT_20DB		0x05
#define POTS_ATT_24DB		0x06
#define POTS_ATT_28DB		0x07
#define POTS_MUTE		0x80

/******************************************************************************
* 
* The DAA controls the interface to the PSTN port.  The driver loads the
* US coefficients by default, so if you live in a different country you
* need to load the set for your countries phone system.
* 
******************************************************************************/
#define IXJCTL_DAA_COEFF_SET		_IOW ('q', 0xD0, int)

#define DAA_US 		1	/*PITA 8kHz */
#define DAA_UK 		2	/*ISAR34 8kHz */
#define DAA_FRANCE 	3	/* */
#define DAA_GERMANY	4
#define DAA_AUSTRALIA	5
#define DAA_JAPAN	6

/******************************************************************************
* 
* Use IXJCTL_PORT to set or query the port the card is set to.  If the
* argument is set to PORT_QUERY, the return value of the ioctl will
* indicate which port is currently in use, otherwise it will change the
* port.
* 
******************************************************************************/
#define IXJCTL_PORT			_IOW ('q', 0xD1, int)

#define PORT_QUERY	0
#define PORT_POTS	1
#define PORT_PSTN	2
#define PORT_SPEAKER	3
#define PORT_HANDSET	4

#define IXJCTL_PSTN_SET_STATE		PHONE_PSTN_SET_STATE
#define IXJCTL_PSTN_GET_STATE		PHONE_PSTN_GET_STATE

#define PSTN_ON_HOOK	0
#define PSTN_RINGING	1
#define PSTN_OFF_HOOK	2
#define PSTN_PULSE_DIAL	3

/******************************************************************************
* 
* The DAA Analog GAIN sets 2 parameters at one time, the receive gain (AGRR), 
* and the transmit gain (AGX).  OR together the components and pass them
* as the parameter to IXJCTL_DAA_AGAIN.  The default setting is both at 0dB.
* 
******************************************************************************/
#define IXJCTL_DAA_AGAIN		_IOW ('q', 0xD2, int)

#define AGRR00DB	0x00	/* Analog gain in receive direction 0dB */
#define AGRR3_5DB	0x10	/* Analog gain in receive direction 3.5dB */
#define AGRR06DB	0x30	/* Analog gain in receive direction 6dB */

#define AGX00DB		0x00	/* Analog gain in transmit direction 0dB */
#define AGX_6DB		0x04	/* Analog gain in transmit direction -6dB */
#define AGX3_5DB	0x08	/* Analog gain in transmit direction 3.5dB */
#define AGX_2_5B	0x0C	/* Analog gain in transmit direction -2.5dB */

#define IXJCTL_PSTN_LINETEST		_IO  ('q', 0xD3)

#define IXJCTL_CID			_IOR ('q', 0xD4, PHONE_CID *)
#define IXJCTL_VMWI			_IOR ('q', 0xD8, int)
#define IXJCTL_CIDCW			_IOW ('q', 0xD9, PHONE_CID *)
/******************************************************************************
* 
* The wink duration is tunable with this ioctl.  The default wink duration  
* is 320ms.  You do not need to use this ioctl if you do not require a
* different wink duration.
* 
******************************************************************************/
#define IXJCTL_WINK_DURATION		PHONE_WINK_DURATION

/******************************************************************************
* 
* This ioctl will connect the POTS port to the PSTN port on the LineJACK
* In order for this to work properly the port selection should be set to
* the PSTN port with IXJCTL_PORT prior to calling this ioctl.  This will
* enable conference calls between PSTN callers and network callers.
* Passing a 1 to this ioctl enables the POTS<->PSTN connection while
* passing a 0 turns it back off.
* 
******************************************************************************/
#define IXJCTL_POTS_PSTN		_IOW ('q', 0xD5, int)

/******************************************************************************
*
* IOCTLs added by request.
*
* IXJCTL_HZ sets the value your Linux kernel uses for HZ as defined in
*           /usr/include/asm/param.h, this determines the fundamental
*           frequency of the clock ticks on your Linux system.  The kernel
*           must be rebuilt if you change this value, also all modules you
*           use (except this one) must be recompiled.  The default value
*           is 100, and you only need to use this IOCTL if you use some
*           other value.
*
*
* IXJCTL_RATE sets the number of times per second that the driver polls
*             the DSP.  This value cannot be larger than HZ.  By
*             increasing both of these values, you may be able to reduce
*             latency because the max hang time that can exist between the
*             driver and the DSP will be reduced.
*
******************************************************************************/

#define IXJCTL_HZ                       _IOW ('q', 0xE0, int)
#define IXJCTL_RATE                     _IOW ('q', 0xE1, int)
#define IXJCTL_FRAMES_READ		_IOR ('q', 0xE2, unsigned long)
#define IXJCTL_FRAMES_WRITTEN		_IOR ('q', 0xE3, unsigned long)
#define IXJCTL_READ_WAIT		_IOR ('q', 0xE4, unsigned long)
#define IXJCTL_WRITE_WAIT		_IOR ('q', 0xE5, unsigned long)
#define IXJCTL_DRYBUFFER_READ		_IOR ('q', 0xE6, unsigned long)
#define IXJCTL_DRYBUFFER_CLEAR		_IO  ('q', 0xE7)
#define IXJCTL_DTMF_PRESCALE		_IOW ('q', 0xE8, int)

/******************************************************************************
*
* This ioctl allows the user application to control what events the driver
* will send signals for, and what signals it will send for which event.
* By default, if signaling is enabled, all events will send SIGIO when
* they occur.  To disable signals for an event set the signal to 0.
*
******************************************************************************/
typedef enum {
	SIG_DTMF_READY,
	SIG_HOOKSTATE,
	SIG_FLASH,
	SIG_PSTN_RING,
	SIG_CALLER_ID,
	SIG_PSTN_WINK,
	SIG_F0, SIG_F1, SIG_F2, SIG_F3,
	SIG_FC0, SIG_FC1, SIG_FC2, SIG_FC3,
	SIG_READ_READY = 33,
	SIG_WRITE_READY = 34
} IXJ_SIGEVENT;

typedef struct {
	unsigned int event;
	int signal;
} IXJ_SIGDEF;

#define IXJCTL_SIGCTL			_IOW ('q', 0xE9, IXJ_SIGDEF *)

/******************************************************************************
*
* These ioctls allow the user application to change the gain in the 
* Smart Cable of the Internet Phone Card.  Sending -1 as a value will cause
* return value to be the current setting.  Valid values to set are 0x00 - 0x1F
*
* 11111 = +12 dB
* 10111 =   0 dB
* 00000 = -34.5 dB
*
* IXJCTL_SC_RXG sets the Receive gain
* IXJCTL_SC_TXG sets the Transmit gain
*
******************************************************************************/
#define IXJCTL_SC_RXG			_IOW ('q', 0xEA, int)
#define IXJCTL_SC_TXG			_IOW ('q', 0xEB, int)

/******************************************************************************
*
* The intercom IOCTL's short the output from one card to the input of the
* other and vice versa (actually done in the DSP read function).  It is only
* necessary to execute the IOCTL on one card, but it is necessary to have
* both devices open to be able to detect hook switch changes.  The record
* codec and rate of each card must match the playback codec and rate of
* the other card for this to work properly.
*
******************************************************************************/

#define IXJCTL_INTERCOM_START 		_IOW ('q', 0xFD, int)
#define IXJCTL_INTERCOM_STOP  		_IOW ('q', 0xFE, int)

/******************************************************************************
 *
 * new structure for accessing raw filter information
 *
 ******************************************************************************/

typedef struct {
	unsigned int filter;
	char enable;
	unsigned int coeff[19];
} IXJ_FILTER_RAW;

#endif
