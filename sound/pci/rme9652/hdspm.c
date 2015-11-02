/*
 *   ALSA driver for RME Hammerfall DSP MADI audio interface(s)
 *
 *      Copyright (c) 2003 Winfried Ritsch (IEM)
 *      code based on hdsp.c   Paul Davis
 *                             Marcus Andersson
 *                             Thomas Charbonnel
 *      Modified 2006-06-01 for AES32 support by Remy Bruno
 *                                               <remy.bruno@trinnov.com>
 *
 *      Modified 2009-04-13 for proper metering by Florian Faber
 *                                               <faber@faberman.de>
 *
 *      Modified 2009-04-14 for native float support by Florian Faber
 *                                               <faber@faberman.de>
 *
 *      Modified 2009-04-26 fixed bug in rms metering by Florian Faber
 *                                               <faber@faberman.de>
 *
 *      Modified 2009-04-30 added hw serial number support by Florian Faber
 *
 *      Modified 2011-01-14 added S/PDIF input on RayDATs by Adrian Knoth
 *
 *	Modified 2011-01-25 variable period sizes on RayDAT/AIO by Adrian Knoth
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

/* *************    Register Documentation   *******************************************************
 *
 * Work in progress! Documentation is based on the code in this file.
 *
 * --------- HDSPM_controlRegister ---------
 * :7654.3210:7654.3210:7654.3210:7654.3210: bit number per byte
 * :||||.||||:||||.||||:||||.||||:||||.||||:
 * :3322.2222:2222.1111:1111.1100:0000.0000: bit number
 * :1098.7654:3210.9876:5432.1098:7654.3210: 0..31
 * :||||.||||:||||.||||:||||.||||:||||.||||:
 * :8421.8421:8421.8421:8421.8421:8421.8421: hex digit
 * :    .    :    .    :    .    :  x .    :  HDSPM_AudioInterruptEnable \_ setting both bits
 * :    .    :    .    :    .    :    .   x:  HDSPM_Start                /  enables audio IO
 * :    .    :    .    :    .    :   x.    :  HDSPM_ClockModeMaster - 1: Master, 0: Slave
 * :    .    :    .    :    .    :    .210 :  HDSPM_LatencyMask - 3 Bit value for latency
 * :    .    :    .    :    .    :    .    :      0:64, 1:128, 2:256, 3:512,
 * :    .    :    .    :    .    :    .    :      4:1024, 5:2048, 6:4096, 7:8192
 * :x   .    :    .    :    .   x:xx  .    :  HDSPM_FrequencyMask
 * :    .    :    .    :    .    :10  .    :  HDSPM_Frequency1|HDSPM_Frequency0: 1=32K,2=44.1K,3=48K,0=??
 * :    .    :    .    :    .   x:    .    :  <MADI> HDSPM_DoubleSpeed
 * :x   .    :    .    :    .    :    .    :  <MADI> HDSPM_QuadSpeed
 * :    .  3 :    .  10:  2 .    :    .    :  HDSPM_SyncRefMask :
 * :    .    :    .   x:    .    :    .    :  HDSPM_SyncRef0
 * :    .    :    .  x :    .    :    .    :  HDSPM_SyncRef1
 * :    .    :    .    :  x .    :    .    :  <AES32> HDSPM_SyncRef2
 * :    .  x :    .    :    .    :    .    :  <AES32> HDSPM_SyncRef3
 * :    .    :    .  10:    .    :    .    :  <MADI> sync ref: 0:WC, 1:Madi, 2:TCO, 3:SyncIn
 * :    .  3 :    .  10:  2 .    :    .    :  <AES32>  0:WC, 1:AES1 ... 8:AES8, 9: TCO, 10:SyncIn?
 * :    .  x :    .    :    .    :    .    :  <MADIe> HDSPe_FLOAT_FORMAT
 * :    .    :    .    : x  .    :    .    :  <MADI> HDSPM_InputSelect0 : 0=optical,1=coax
 * :    .    :    .    :x   .    :    .    :  <MADI> HDSPM_InputSelect1
 * :    .    :    .x   :    .    :    .    :  <MADI> HDSPM_clr_tms
 * :    .    :    .    :    . x  :    .    :  <MADI> HDSPM_TX_64ch
 * :    .    :    .    :    . x  :    .    :  <AES32> HDSPM_Emphasis
 * :    .    :    .    :    .x   :    .    :  <MADI> HDSPM_AutoInp
 * :    .    :    . x  :    .    :    .    :  <MADI> HDSPM_SMUX
 * :    .    :    .x   :    .    :    .    :  <MADI> HDSPM_clr_tms
 * :    .    :   x.    :    .    :    .    :  <MADI> HDSPM_taxi_reset
 * :    .   x:    .    :    .    :    .    :  <MADI> HDSPM_LineOut
 * :    .   x:    .    :    .    :    .    :  <AES32> ??????????????????
 * :    .    :   x.    :    .    :    .    :  <AES32> HDSPM_WCK48
 * :    .    :    .    :    .x   :    .    :  <AES32> HDSPM_Dolby
 * :    .    : x  .    :    .    :    .    :  HDSPM_Midi0InterruptEnable
 * :    .    :x   .    :    .    :    .    :  HDSPM_Midi1InterruptEnable
 * :    .    :  x .    :    .    :    .    :  HDSPM_Midi2InterruptEnable
 * :    . x  :    .    :    .    :    .    :  <MADI> HDSPM_Midi3InterruptEnable
 * :    . x  :    .    :    .    :    .    :  <AES32> HDSPM_DS_DoubleWire
 * :    .x   :    .    :    .    :    .    :  <AES32> HDSPM_QS_DoubleWire
 * :   x.    :    .    :    .    :    .    :  <AES32> HDSPM_QS_QuadWire
 * :    .    :    .    :    .  x :    .    :  <AES32> HDSPM_Professional
 * : x  .    :    .    :    .    :    .    :  HDSPM_wclk_sel
 * :    .    :    .    :    .    :    .    :
 * :7654.3210:7654.3210:7654.3210:7654.3210: bit number per byte
 * :||||.||||:||||.||||:||||.||||:||||.||||:
 * :3322.2222:2222.1111:1111.1100:0000.0000: bit number
 * :1098.7654:3210.9876:5432.1098:7654.3210: 0..31
 * :||||.||||:||||.||||:||||.||||:||||.||||:
 * :8421.8421:8421.8421:8421.8421:8421.8421:hex digit
 *
 *
 *
 * AIO / RayDAT only
 *
 * ------------ HDSPM_WR_SETTINGS ----------
 * :3322.2222:2222.1111:1111.1100:0000.0000: bit number per byte
 * :1098.7654:3210.9876:5432.1098:7654.3210:
 * :||||.||||:||||.||||:||||.||||:||||.||||: bit number
 * :7654.3210:7654.3210:7654.3210:7654.3210: 0..31
 * :||||.||||:||||.||||:||||.||||:||||.||||:
 * :8421.8421:8421.8421:8421.8421:8421.8421: hex digit
 * :    .    :    .    :    .    :    .   x: HDSPM_c0Master 1: Master, 0: Slave
 * :    .    :    .    :    .    :    .  x : HDSPM_c0_SyncRef0
 * :    .    :    .    :    .    :    . x  : HDSPM_c0_SyncRef1
 * :    .    :    .    :    .    :    .x   : HDSPM_c0_SyncRef2
 * :    .    :    .    :    .    :   x.    : HDSPM_c0_SyncRef3
 * :    .    :    .    :    .    :   3.210 : HDSPM_c0_SyncRefMask:
 * :    .    :    .    :    .    :    .    :  RayDat: 0:WC, 1:AES, 2:SPDIF, 3..6: ADAT1..4,
 * :    .    :    .    :    .    :    .    :          9:TCO, 10:SyncIn
 * :    .    :    .    :    .    :    .    :  AIO: 0:WC, 1:AES, 2: SPDIF, 3: ATAT,
 * :    .    :    .    :    .    :    .    :          9:TCO, 10:SyncIn
 * :    .    :    .    :    .    :    .    :
 * :    .    :    .    :    .    :    .    :
 * :3322.2222:2222.1111:1111.1100:0000.0000: bit number per byte
 * :1098.7654:3210.9876:5432.1098:7654.3210:
 * :||||.||||:||||.||||:||||.||||:||||.||||: bit number
 * :7654.3210:7654.3210:7654.3210:7654.3210: 0..31
 * :||||.||||:||||.||||:||||.||||:||||.||||:
 * :8421.8421:8421.8421:8421.8421:8421.8421: hex digit
 *
 */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/math64.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/asoundef.h>
#include <sound/rawmidi.h>
#include <sound/hwdep.h>
#include <sound/initval.h>

#include <sound/hdspm.h>

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	  /* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	  /* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;/* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for RME HDSPM interface.");

module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for RME HDSPM interface.");

module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable/disable specific HDSPM soundcards.");


MODULE_AUTHOR
(
	"Winfried Ritsch <ritsch_AT_iem.at>, "
	"Paul Davis <paul@linuxaudiosystems.com>, "
	"Marcus Andersson, Thomas Charbonnel <thomas@undata.org>, "
	"Remy Bruno <remy.bruno@trinnov.com>, "
	"Florian Faber <faberman@linuxproaudio.org>, "
	"Adrian Knoth <adi@drcomp.erfurt.thur.de>"
);
MODULE_DESCRIPTION("RME HDSPM");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{RME HDSPM-MADI}}");

/* --- Write registers. ---
  These are defined as byte-offsets from the iobase value.  */

#define HDSPM_WR_SETTINGS             0
#define HDSPM_outputBufferAddress    32
#define HDSPM_inputBufferAddress     36
#define HDSPM_controlRegister	     64
#define HDSPM_interruptConfirmation  96
#define HDSPM_control2Reg	     256  /* not in specs ???????? */
#define HDSPM_freqReg                256  /* for setting arbitrary clock values (DDS feature) */
#define HDSPM_midiDataOut0	     352  /* just believe in old code */
#define HDSPM_midiDataOut1	     356
#define HDSPM_eeprom_wr		     384  /* for AES32 */

/* DMA enable for 64 channels, only Bit 0 is relevant */
#define HDSPM_outputEnableBase       512  /* 512-767  input  DMA */
#define HDSPM_inputEnableBase        768  /* 768-1023 output DMA */

/* 16 page addresses for each of the 64 channels DMA buffer in and out
   (each 64k=16*4k) Buffer must be 4k aligned (which is default i386 ????) */
#define HDSPM_pageAddressBufferOut       8192
#define HDSPM_pageAddressBufferIn        (HDSPM_pageAddressBufferOut+64*16*4)

#define HDSPM_MADI_mixerBase    32768	/* 32768-65535 for 2x64x64 Fader */

#define HDSPM_MATRIX_MIXER_SIZE  8192	/* = 2*64*64 * 4 Byte => 32kB */

/* --- Read registers. ---
   These are defined as byte-offsets from the iobase value */
#define HDSPM_statusRegister    0
/*#define HDSPM_statusRegister2  96 */
/* after RME Windows driver sources, status2 is 4-byte word # 48 = word at
 * offset 192, for AES32 *and* MADI
 * => need to check that offset 192 is working on MADI */
#define HDSPM_statusRegister2  192
#define HDSPM_timecodeRegister 128

/* AIO, RayDAT */
#define HDSPM_RD_STATUS_0 0
#define HDSPM_RD_STATUS_1 64
#define HDSPM_RD_STATUS_2 128
#define HDSPM_RD_STATUS_3 192

#define HDSPM_RD_TCO           256
#define HDSPM_RD_PLL_FREQ      512
#define HDSPM_WR_TCO           128

#define HDSPM_TCO1_TCO_lock			0x00000001
#define HDSPM_TCO1_WCK_Input_Range_LSB		0x00000002
#define HDSPM_TCO1_WCK_Input_Range_MSB		0x00000004
#define HDSPM_TCO1_LTC_Input_valid		0x00000008
#define HDSPM_TCO1_WCK_Input_valid		0x00000010
#define HDSPM_TCO1_Video_Input_Format_NTSC	0x00000020
#define HDSPM_TCO1_Video_Input_Format_PAL	0x00000040

#define HDSPM_TCO1_set_TC			0x00000100
#define HDSPM_TCO1_set_drop_frame_flag		0x00000200
#define HDSPM_TCO1_LTC_Format_LSB		0x00000400
#define HDSPM_TCO1_LTC_Format_MSB		0x00000800

#define HDSPM_TCO2_TC_run			0x00010000
#define HDSPM_TCO2_WCK_IO_ratio_LSB		0x00020000
#define HDSPM_TCO2_WCK_IO_ratio_MSB		0x00040000
#define HDSPM_TCO2_set_num_drop_frames_LSB	0x00080000
#define HDSPM_TCO2_set_num_drop_frames_MSB	0x00100000
#define HDSPM_TCO2_set_jam_sync			0x00200000
#define HDSPM_TCO2_set_flywheel			0x00400000

#define HDSPM_TCO2_set_01_4			0x01000000
#define HDSPM_TCO2_set_pull_down		0x02000000
#define HDSPM_TCO2_set_pull_up			0x04000000
#define HDSPM_TCO2_set_freq			0x08000000
#define HDSPM_TCO2_set_term_75R			0x10000000
#define HDSPM_TCO2_set_input_LSB		0x20000000
#define HDSPM_TCO2_set_input_MSB		0x40000000
#define HDSPM_TCO2_set_freq_from_app		0x80000000


#define HDSPM_midiDataOut0    352
#define HDSPM_midiDataOut1    356
#define HDSPM_midiDataOut2    368

#define HDSPM_midiDataIn0     360
#define HDSPM_midiDataIn1     364
#define HDSPM_midiDataIn2     372
#define HDSPM_midiDataIn3     376

/* status is data bytes in MIDI-FIFO (0-128) */
#define HDSPM_midiStatusOut0  384
#define HDSPM_midiStatusOut1  388
#define HDSPM_midiStatusOut2  400

#define HDSPM_midiStatusIn0   392
#define HDSPM_midiStatusIn1   396
#define HDSPM_midiStatusIn2   404
#define HDSPM_midiStatusIn3   408


/* the meters are regular i/o-mapped registers, but offset
   considerably from the rest. the peak registers are reset
   when read; the least-significant 4 bits are full-scale counters;
   the actual peak value is in the most-significant 24 bits.
*/

#define HDSPM_MADI_INPUT_PEAK		4096
#define HDSPM_MADI_PLAYBACK_PEAK	4352
#define HDSPM_MADI_OUTPUT_PEAK		4608

#define HDSPM_MADI_INPUT_RMS_L		6144
#define HDSPM_MADI_PLAYBACK_RMS_L	6400
#define HDSPM_MADI_OUTPUT_RMS_L		6656

#define HDSPM_MADI_INPUT_RMS_H		7168
#define HDSPM_MADI_PLAYBACK_RMS_H	7424
#define HDSPM_MADI_OUTPUT_RMS_H		7680

/* --- Control Register bits --------- */
#define HDSPM_Start                (1<<0) /* start engine */

#define HDSPM_Latency0             (1<<1) /* buffer size = 2^n */
#define HDSPM_Latency1             (1<<2) /* where n is defined */
#define HDSPM_Latency2             (1<<3) /* by Latency{2,1,0} */

#define HDSPM_ClockModeMaster      (1<<4) /* 1=Master, 0=Autosync */
#define HDSPM_c0Master		0x1    /* Master clock bit in settings
					  register [RayDAT, AIO] */

#define HDSPM_AudioInterruptEnable (1<<5) /* what do you think ? */

#define HDSPM_Frequency0  (1<<6)  /* 0=44.1kHz/88.2kHz 1=48kHz/96kHz */
#define HDSPM_Frequency1  (1<<7)  /* 0=32kHz/64kHz */
#define HDSPM_DoubleSpeed (1<<8)  /* 0=normal speed, 1=double speed */
#define HDSPM_QuadSpeed   (1<<31) /* quad speed bit */

#define HDSPM_Professional (1<<9) /* Professional */ /* AES32 ONLY */
#define HDSPM_TX_64ch     (1<<10) /* Output 64channel MODE=1,
				     56channelMODE=0 */ /* MADI ONLY*/
#define HDSPM_Emphasis    (1<<10) /* Emphasis */ /* AES32 ONLY */

#define HDSPM_AutoInp     (1<<11) /* Auto Input (takeover) == Safe Mode,
                                     0=off, 1=on  */ /* MADI ONLY */
#define HDSPM_Dolby       (1<<11) /* Dolby = "NonAudio" ?? */ /* AES32 ONLY */

#define HDSPM_InputSelect0 (1<<14) /* Input select 0= optical, 1=coax
				    * -- MADI ONLY
				    */
#define HDSPM_InputSelect1 (1<<15) /* should be 0 */

#define HDSPM_SyncRef2     (1<<13)
#define HDSPM_SyncRef3     (1<<25)

#define HDSPM_SMUX         (1<<18) /* Frame ??? */ /* MADI ONY */
#define HDSPM_clr_tms      (1<<19) /* clear track marker, do not use
                                      AES additional bits in
				      lower 5 Audiodatabits ??? */
#define HDSPM_taxi_reset   (1<<20) /* ??? */ /* MADI ONLY ? */
#define HDSPM_WCK48        (1<<20) /* Frame ??? = HDSPM_SMUX */ /* AES32 ONLY */

#define HDSPM_Midi0InterruptEnable 0x0400000
#define HDSPM_Midi1InterruptEnable 0x0800000
#define HDSPM_Midi2InterruptEnable 0x0200000
#define HDSPM_Midi3InterruptEnable 0x4000000

#define HDSPM_LineOut (1<<24) /* Analog Out on channel 63/64 on=1, mute=0 */
#define HDSPe_FLOAT_FORMAT         0x2000000

#define HDSPM_DS_DoubleWire (1<<26) /* AES32 ONLY */
#define HDSPM_QS_DoubleWire (1<<27) /* AES32 ONLY */
#define HDSPM_QS_QuadWire   (1<<28) /* AES32 ONLY */

#define HDSPM_wclk_sel (1<<30)

/* additional control register bits for AIO*/
#define HDSPM_c0_Wck48				0x20 /* also RayDAT */
#define HDSPM_c0_Input0				0x1000
#define HDSPM_c0_Input1				0x2000
#define HDSPM_c0_Spdif_Opt			0x4000
#define HDSPM_c0_Pro				0x8000
#define HDSPM_c0_clr_tms			0x10000
#define HDSPM_c0_AEB1				0x20000
#define HDSPM_c0_AEB2				0x40000
#define HDSPM_c0_LineOut			0x80000
#define HDSPM_c0_AD_GAIN0			0x100000
#define HDSPM_c0_AD_GAIN1			0x200000
#define HDSPM_c0_DA_GAIN0			0x400000
#define HDSPM_c0_DA_GAIN1			0x800000
#define HDSPM_c0_PH_GAIN0			0x1000000
#define HDSPM_c0_PH_GAIN1			0x2000000
#define HDSPM_c0_Sym6db				0x4000000


/* --- bit helper defines */
#define HDSPM_LatencyMask    (HDSPM_Latency0|HDSPM_Latency1|HDSPM_Latency2)
#define HDSPM_FrequencyMask  (HDSPM_Frequency0|HDSPM_Frequency1|\
			      HDSPM_DoubleSpeed|HDSPM_QuadSpeed)
#define HDSPM_InputMask      (HDSPM_InputSelect0|HDSPM_InputSelect1)
#define HDSPM_InputOptical   0
#define HDSPM_InputCoaxial   (HDSPM_InputSelect0)
#define HDSPM_SyncRefMask    (HDSPM_SyncRef0|HDSPM_SyncRef1|\
			      HDSPM_SyncRef2|HDSPM_SyncRef3)

#define HDSPM_c0_SyncRef0      0x2
#define HDSPM_c0_SyncRef1      0x4
#define HDSPM_c0_SyncRef2      0x8
#define HDSPM_c0_SyncRef3      0x10
#define HDSPM_c0_SyncRefMask   (HDSPM_c0_SyncRef0 | HDSPM_c0_SyncRef1 |\
				HDSPM_c0_SyncRef2 | HDSPM_c0_SyncRef3)

#define HDSPM_SYNC_FROM_WORD    0	/* Preferred sync reference */
#define HDSPM_SYNC_FROM_MADI    1	/* choices - used by "pref_sync_ref" */
#define HDSPM_SYNC_FROM_TCO     2
#define HDSPM_SYNC_FROM_SYNC_IN 3

#define HDSPM_Frequency32KHz    HDSPM_Frequency0
#define HDSPM_Frequency44_1KHz  HDSPM_Frequency1
#define HDSPM_Frequency48KHz   (HDSPM_Frequency1|HDSPM_Frequency0)
#define HDSPM_Frequency64KHz   (HDSPM_DoubleSpeed|HDSPM_Frequency0)
#define HDSPM_Frequency88_2KHz (HDSPM_DoubleSpeed|HDSPM_Frequency1)
#define HDSPM_Frequency96KHz   (HDSPM_DoubleSpeed|HDSPM_Frequency1|\
				HDSPM_Frequency0)
#define HDSPM_Frequency128KHz   (HDSPM_QuadSpeed|HDSPM_Frequency0)
#define HDSPM_Frequency176_4KHz   (HDSPM_QuadSpeed|HDSPM_Frequency1)
#define HDSPM_Frequency192KHz   (HDSPM_QuadSpeed|HDSPM_Frequency1|\
				 HDSPM_Frequency0)


/* Synccheck Status */
#define HDSPM_SYNC_CHECK_NO_LOCK 0
#define HDSPM_SYNC_CHECK_LOCK    1
#define HDSPM_SYNC_CHECK_SYNC	 2

/* AutoSync References - used by "autosync_ref" control switch */
#define HDSPM_AUTOSYNC_FROM_WORD      0
#define HDSPM_AUTOSYNC_FROM_MADI      1
#define HDSPM_AUTOSYNC_FROM_TCO       2
#define HDSPM_AUTOSYNC_FROM_SYNC_IN   3
#define HDSPM_AUTOSYNC_FROM_NONE      4

/* Possible sources of MADI input */
#define HDSPM_OPTICAL 0		/* optical   */
#define HDSPM_COAXIAL 1		/* BNC */

#define hdspm_encode_latency(x)       (((x)<<1) & HDSPM_LatencyMask)
#define hdspm_decode_latency(x)       ((((x) & HDSPM_LatencyMask)>>1))

#define hdspm_encode_in(x) (((x)&0x3)<<14)
#define hdspm_decode_in(x) (((x)>>14)&0x3)

/* --- control2 register bits --- */
#define HDSPM_TMS             (1<<0)
#define HDSPM_TCK             (1<<1)
#define HDSPM_TDI             (1<<2)
#define HDSPM_JTAG            (1<<3)
#define HDSPM_PWDN            (1<<4)
#define HDSPM_PROGRAM	      (1<<5)
#define HDSPM_CONFIG_MODE_0   (1<<6)
#define HDSPM_CONFIG_MODE_1   (1<<7)
/*#define HDSPM_VERSION_BIT     (1<<8) not defined any more*/
#define HDSPM_BIGENDIAN_MODE  (1<<9)
#define HDSPM_RD_MULTIPLE     (1<<10)

/* --- Status Register bits --- */ /* MADI ONLY */ /* Bits defined here and
     that do not conflict with specific bits for AES32 seem to be valid also
     for the AES32
 */
#define HDSPM_audioIRQPending    (1<<0)	/* IRQ is high and pending */
#define HDSPM_RX_64ch            (1<<1)	/* Input 64chan. MODE=1, 56chn MODE=0 */
#define HDSPM_AB_int             (1<<2)	/* InputChannel Opt=0, Coax=1
					 * (like inp0)
					 */

#define HDSPM_madiLock           (1<<3)	/* MADI Locked =1, no=0 */
#define HDSPM_madiSync          (1<<18) /* MADI is in sync */

#define HDSPM_tcoLockMadi    0x00000020 /* Optional TCO locked status for HDSPe MADI*/
#define HDSPM_tcoSync    0x10000000 /* Optional TCO sync status for HDSPe MADI and AES32!*/

#define HDSPM_syncInLock 0x00010000 /* Sync In lock status for HDSPe MADI! */
#define HDSPM_syncInSync 0x00020000 /* Sync In sync status for HDSPe MADI! */

#define HDSPM_BufferPositionMask 0x000FFC0 /* Bit 6..15 : h/w buffer pointer */
			/* since 64byte accurate, last 6 bits are not used */



#define HDSPM_DoubleSpeedStatus (1<<19) /* (input) card in double speed */

#define HDSPM_madiFreq0         (1<<22)	/* system freq 0=error */
#define HDSPM_madiFreq1         (1<<23)	/* 1=32, 2=44.1 3=48 */
#define HDSPM_madiFreq2         (1<<24)	/* 4=64, 5=88.2 6=96 */
#define HDSPM_madiFreq3         (1<<25)	/* 7=128, 8=176.4 9=192 */

#define HDSPM_BufferID          (1<<26)	/* (Double)Buffer ID toggles with
					 * Interrupt
					 */
#define HDSPM_tco_detect         0x08000000
#define HDSPM_tcoLockAes         0x20000000 /* Optional TCO locked status for HDSPe AES */

#define HDSPM_s2_tco_detect      0x00000040
#define HDSPM_s2_AEBO_D          0x00000080
#define HDSPM_s2_AEBI_D          0x00000100


#define HDSPM_midi0IRQPending    0x40000000
#define HDSPM_midi1IRQPending    0x80000000
#define HDSPM_midi2IRQPending    0x20000000
#define HDSPM_midi2IRQPendingAES 0x00000020
#define HDSPM_midi3IRQPending    0x00200000

/* --- status bit helpers */
#define HDSPM_madiFreqMask  (HDSPM_madiFreq0|HDSPM_madiFreq1|\
			     HDSPM_madiFreq2|HDSPM_madiFreq3)
#define HDSPM_madiFreq32    (HDSPM_madiFreq0)
#define HDSPM_madiFreq44_1  (HDSPM_madiFreq1)
#define HDSPM_madiFreq48    (HDSPM_madiFreq0|HDSPM_madiFreq1)
#define HDSPM_madiFreq64    (HDSPM_madiFreq2)
#define HDSPM_madiFreq88_2  (HDSPM_madiFreq0|HDSPM_madiFreq2)
#define HDSPM_madiFreq96    (HDSPM_madiFreq1|HDSPM_madiFreq2)
#define HDSPM_madiFreq128   (HDSPM_madiFreq0|HDSPM_madiFreq1|HDSPM_madiFreq2)
#define HDSPM_madiFreq176_4 (HDSPM_madiFreq3)
#define HDSPM_madiFreq192   (HDSPM_madiFreq3|HDSPM_madiFreq0)

/* Status2 Register bits */ /* MADI ONLY */

#define HDSPM_version0 (1<<0)	/* not really defined but I guess */
#define HDSPM_version1 (1<<1)	/* in former cards it was ??? */
#define HDSPM_version2 (1<<2)

#define HDSPM_wcLock (1<<3)	/* Wordclock is detected and locked */
#define HDSPM_wcSync (1<<4)	/* Wordclock is in sync with systemclock */

#define HDSPM_wc_freq0 (1<<5)	/* input freq detected via autosync  */
#define HDSPM_wc_freq1 (1<<6)	/* 001=32, 010==44.1, 011=48, */
#define HDSPM_wc_freq2 (1<<7)	/* 100=64, 101=88.2, 110=96, 111=128 */
#define HDSPM_wc_freq3 0x800	/* 1000=176.4, 1001=192 */

#define HDSPM_SyncRef0 0x10000  /* Sync Reference */
#define HDSPM_SyncRef1 0x20000

#define HDSPM_SelSyncRef0 (1<<8)	/* AutoSync Source */
#define HDSPM_SelSyncRef1 (1<<9)	/* 000=word, 001=MADI, */
#define HDSPM_SelSyncRef2 (1<<10)	/* 111=no valid signal */

#define HDSPM_wc_valid (HDSPM_wcLock|HDSPM_wcSync)

#define HDSPM_wcFreqMask  (HDSPM_wc_freq0|HDSPM_wc_freq1|HDSPM_wc_freq2|\
			    HDSPM_wc_freq3)
#define HDSPM_wcFreq32    (HDSPM_wc_freq0)
#define HDSPM_wcFreq44_1  (HDSPM_wc_freq1)
#define HDSPM_wcFreq48    (HDSPM_wc_freq0|HDSPM_wc_freq1)
#define HDSPM_wcFreq64    (HDSPM_wc_freq2)
#define HDSPM_wcFreq88_2  (HDSPM_wc_freq0|HDSPM_wc_freq2)
#define HDSPM_wcFreq96    (HDSPM_wc_freq1|HDSPM_wc_freq2)
#define HDSPM_wcFreq128   (HDSPM_wc_freq0|HDSPM_wc_freq1|HDSPM_wc_freq2)
#define HDSPM_wcFreq176_4 (HDSPM_wc_freq3)
#define HDSPM_wcFreq192   (HDSPM_wc_freq0|HDSPM_wc_freq3)

#define HDSPM_status1_F_0 0x0400000
#define HDSPM_status1_F_1 0x0800000
#define HDSPM_status1_F_2 0x1000000
#define HDSPM_status1_F_3 0x2000000
#define HDSPM_status1_freqMask (HDSPM_status1_F_0|HDSPM_status1_F_1|HDSPM_status1_F_2|HDSPM_status1_F_3)


#define HDSPM_SelSyncRefMask       (HDSPM_SelSyncRef0|HDSPM_SelSyncRef1|\
				    HDSPM_SelSyncRef2)
#define HDSPM_SelSyncRef_WORD      0
#define HDSPM_SelSyncRef_MADI      (HDSPM_SelSyncRef0)
#define HDSPM_SelSyncRef_TCO       (HDSPM_SelSyncRef1)
#define HDSPM_SelSyncRef_SyncIn    (HDSPM_SelSyncRef0|HDSPM_SelSyncRef1)
#define HDSPM_SelSyncRef_NVALID    (HDSPM_SelSyncRef0|HDSPM_SelSyncRef1|\
				    HDSPM_SelSyncRef2)

/*
   For AES32, bits for status, status2 and timecode are different
*/
/* status */
#define HDSPM_AES32_wcLock	0x0200000
#define HDSPM_AES32_wcSync	0x0100000
#define HDSPM_AES32_wcFreq_bit  22
/* (status >> HDSPM_AES32_wcFreq_bit) & 0xF gives WC frequency (cf function
  HDSPM_bit2freq */
#define HDSPM_AES32_syncref_bit  16
/* (status >> HDSPM_AES32_syncref_bit) & 0xF gives sync source */

#define HDSPM_AES32_AUTOSYNC_FROM_WORD 0
#define HDSPM_AES32_AUTOSYNC_FROM_AES1 1
#define HDSPM_AES32_AUTOSYNC_FROM_AES2 2
#define HDSPM_AES32_AUTOSYNC_FROM_AES3 3
#define HDSPM_AES32_AUTOSYNC_FROM_AES4 4
#define HDSPM_AES32_AUTOSYNC_FROM_AES5 5
#define HDSPM_AES32_AUTOSYNC_FROM_AES6 6
#define HDSPM_AES32_AUTOSYNC_FROM_AES7 7
#define HDSPM_AES32_AUTOSYNC_FROM_AES8 8
#define HDSPM_AES32_AUTOSYNC_FROM_TCO 9
#define HDSPM_AES32_AUTOSYNC_FROM_SYNC_IN 10
#define HDSPM_AES32_AUTOSYNC_FROM_NONE 11

/*  status2 */
/* HDSPM_LockAES_bit is given by HDSPM_LockAES >> (AES# - 1) */
#define HDSPM_LockAES   0x80
#define HDSPM_LockAES1  0x80
#define HDSPM_LockAES2  0x40
#define HDSPM_LockAES3  0x20
#define HDSPM_LockAES4  0x10
#define HDSPM_LockAES5  0x8
#define HDSPM_LockAES6  0x4
#define HDSPM_LockAES7  0x2
#define HDSPM_LockAES8  0x1
/*
   Timecode
   After windows driver sources, bits 4*i to 4*i+3 give the input frequency on
   AES i+1
 bits 3210
      0001  32kHz
      0010  44.1kHz
      0011  48kHz
      0100  64kHz
      0101  88.2kHz
      0110  96kHz
      0111  128kHz
      1000  176.4kHz
      1001  192kHz
  NB: Timecode register doesn't seem to work on AES32 card revision 230
*/

/* Mixer Values */
#define UNITY_GAIN          32768	/* = 65536/2 */
#define MINUS_INFINITY_GAIN 0

/* Number of channels for different Speed Modes */
#define MADI_SS_CHANNELS       64
#define MADI_DS_CHANNELS       32
#define MADI_QS_CHANNELS       16

#define RAYDAT_SS_CHANNELS     36
#define RAYDAT_DS_CHANNELS     20
#define RAYDAT_QS_CHANNELS     12

#define AIO_IN_SS_CHANNELS        14
#define AIO_IN_DS_CHANNELS        10
#define AIO_IN_QS_CHANNELS        8
#define AIO_OUT_SS_CHANNELS        16
#define AIO_OUT_DS_CHANNELS        12
#define AIO_OUT_QS_CHANNELS        10

#define AES32_CHANNELS		16

/* the size of a substream (1 mono data stream) */
#define HDSPM_CHANNEL_BUFFER_SAMPLES  (16*1024)
#define HDSPM_CHANNEL_BUFFER_BYTES    (4*HDSPM_CHANNEL_BUFFER_SAMPLES)

/* the size of the area we need to allocate for DMA transfers. the
   size is the same regardless of the number of channels, and
   also the latency to use.
   for one direction !!!
*/
#define HDSPM_DMA_AREA_BYTES (HDSPM_MAX_CHANNELS * HDSPM_CHANNEL_BUFFER_BYTES)
#define HDSPM_DMA_AREA_KILOBYTES (HDSPM_DMA_AREA_BYTES/1024)

#define HDSPM_RAYDAT_REV	211
#define HDSPM_AIO_REV		212
#define HDSPM_MADIFACE_REV	213

/* speed factor modes */
#define HDSPM_SPEED_SINGLE 0
#define HDSPM_SPEED_DOUBLE 1
#define HDSPM_SPEED_QUAD   2

/* names for speed modes */
static char *hdspm_speed_names[] = { "single", "double", "quad" };

static const char *const texts_autosync_aes_tco[] = { "Word Clock",
					  "AES1", "AES2", "AES3", "AES4",
					  "AES5", "AES6", "AES7", "AES8",
					  "TCO", "Sync In"
};
static const char *const texts_autosync_aes[] = { "Word Clock",
				      "AES1", "AES2", "AES3", "AES4",
				      "AES5", "AES6", "AES7", "AES8",
				      "Sync In"
};
static const char *const texts_autosync_madi_tco[] = { "Word Clock",
					   "MADI", "TCO", "Sync In" };
static const char *const texts_autosync_madi[] = { "Word Clock",
				       "MADI", "Sync In" };

static const char *const texts_autosync_raydat_tco[] = {
	"Word Clock",
	"ADAT 1", "ADAT 2", "ADAT 3", "ADAT 4",
	"AES", "SPDIF", "TCO", "Sync In"
};
static const char *const texts_autosync_raydat[] = {
	"Word Clock",
	"ADAT 1", "ADAT 2", "ADAT 3", "ADAT 4",
	"AES", "SPDIF", "Sync In"
};
static const char *const texts_autosync_aio_tco[] = {
	"Word Clock",
	"ADAT", "AES", "SPDIF", "TCO", "Sync In"
};
static const char *const texts_autosync_aio[] = { "Word Clock",
				      "ADAT", "AES", "SPDIF", "Sync In" };

static const char *const texts_freq[] = {
	"No Lock",
	"32 kHz",
	"44.1 kHz",
	"48 kHz",
	"64 kHz",
	"88.2 kHz",
	"96 kHz",
	"128 kHz",
	"176.4 kHz",
	"192 kHz"
};

static char *texts_ports_madi[] = {
	"MADI.1", "MADI.2", "MADI.3", "MADI.4", "MADI.5", "MADI.6",
	"MADI.7", "MADI.8", "MADI.9", "MADI.10", "MADI.11", "MADI.12",
	"MADI.13", "MADI.14", "MADI.15", "MADI.16", "MADI.17", "MADI.18",
	"MADI.19", "MADI.20", "MADI.21", "MADI.22", "MADI.23", "MADI.24",
	"MADI.25", "MADI.26", "MADI.27", "MADI.28", "MADI.29", "MADI.30",
	"MADI.31", "MADI.32", "MADI.33", "MADI.34", "MADI.35", "MADI.36",
	"MADI.37", "MADI.38", "MADI.39", "MADI.40", "MADI.41", "MADI.42",
	"MADI.43", "MADI.44", "MADI.45", "MADI.46", "MADI.47", "MADI.48",
	"MADI.49", "MADI.50", "MADI.51", "MADI.52", "MADI.53", "MADI.54",
	"MADI.55", "MADI.56", "MADI.57", "MADI.58", "MADI.59", "MADI.60",
	"MADI.61", "MADI.62", "MADI.63", "MADI.64",
};


static char *texts_ports_raydat_ss[] = {
	"ADAT1.1", "ADAT1.2", "ADAT1.3", "ADAT1.4", "ADAT1.5", "ADAT1.6",
	"ADAT1.7", "ADAT1.8", "ADAT2.1", "ADAT2.2", "ADAT2.3", "ADAT2.4",
	"ADAT2.5", "ADAT2.6", "ADAT2.7", "ADAT2.8", "ADAT3.1", "ADAT3.2",
	"ADAT3.3", "ADAT3.4", "ADAT3.5", "ADAT3.6", "ADAT3.7", "ADAT3.8",
	"ADAT4.1", "ADAT4.2", "ADAT4.3", "ADAT4.4", "ADAT4.5", "ADAT4.6",
	"ADAT4.7", "ADAT4.8",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R"
};

static char *texts_ports_raydat_ds[] = {
	"ADAT1.1", "ADAT1.2", "ADAT1.3", "ADAT1.4",
	"ADAT2.1", "ADAT2.2", "ADAT2.3", "ADAT2.4",
	"ADAT3.1", "ADAT3.2", "ADAT3.3", "ADAT3.4",
	"ADAT4.1", "ADAT4.2", "ADAT4.3", "ADAT4.4",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R"
};

static char *texts_ports_raydat_qs[] = {
	"ADAT1.1", "ADAT1.2",
	"ADAT2.1", "ADAT2.2",
	"ADAT3.1", "ADAT3.2",
	"ADAT4.1", "ADAT4.2",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R"
};


static char *texts_ports_aio_in_ss[] = {
	"Analogue.L", "Analogue.R",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R",
	"ADAT.1", "ADAT.2", "ADAT.3", "ADAT.4", "ADAT.5", "ADAT.6",
	"ADAT.7", "ADAT.8",
	"AEB.1", "AEB.2", "AEB.3", "AEB.4"
};

static char *texts_ports_aio_out_ss[] = {
	"Analogue.L", "Analogue.R",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R",
	"ADAT.1", "ADAT.2", "ADAT.3", "ADAT.4", "ADAT.5", "ADAT.6",
	"ADAT.7", "ADAT.8",
	"Phone.L", "Phone.R",
	"AEB.1", "AEB.2", "AEB.3", "AEB.4"
};

static char *texts_ports_aio_in_ds[] = {
	"Analogue.L", "Analogue.R",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R",
	"ADAT.1", "ADAT.2", "ADAT.3", "ADAT.4",
	"AEB.1", "AEB.2", "AEB.3", "AEB.4"
};

static char *texts_ports_aio_out_ds[] = {
	"Analogue.L", "Analogue.R",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R",
	"ADAT.1", "ADAT.2", "ADAT.3", "ADAT.4",
	"Phone.L", "Phone.R",
	"AEB.1", "AEB.2", "AEB.3", "AEB.4"
};

static char *texts_ports_aio_in_qs[] = {
	"Analogue.L", "Analogue.R",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R",
	"ADAT.1", "ADAT.2", "ADAT.3", "ADAT.4",
	"AEB.1", "AEB.2", "AEB.3", "AEB.4"
};

static char *texts_ports_aio_out_qs[] = {
	"Analogue.L", "Analogue.R",
	"AES.L", "AES.R",
	"SPDIF.L", "SPDIF.R",
	"ADAT.1", "ADAT.2", "ADAT.3", "ADAT.4",
	"Phone.L", "Phone.R",
	"AEB.1", "AEB.2", "AEB.3", "AEB.4"
};

static char *texts_ports_aes32[] = {
	"AES.1", "AES.2", "AES.3", "AES.4", "AES.5", "AES.6", "AES.7",
	"AES.8", "AES.9.", "AES.10", "AES.11", "AES.12", "AES.13", "AES.14",
	"AES.15", "AES.16"
};

/* These tables map the ALSA channels 1..N to the channels that we
   need to use in order to find the relevant channel buffer. RME
   refers to this kind of mapping as between "the ADAT channel and
   the DMA channel." We index it using the logical audio channel,
   and the value is the DMA channel (i.e. channel buffer number)
   where the data for that channel can be read/written from/to.
*/

static char channel_map_unity_ss[HDSPM_MAX_CHANNELS] = {
	0, 1, 2, 3, 4, 5, 6, 7,
	8, 9, 10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 30, 31,
	32, 33, 34, 35, 36, 37, 38, 39,
	40, 41, 42, 43, 44, 45, 46, 47,
	48, 49, 50, 51, 52, 53, 54, 55,
	56, 57, 58, 59, 60, 61, 62, 63
};

static char channel_map_raydat_ss[HDSPM_MAX_CHANNELS] = {
	4, 5, 6, 7, 8, 9, 10, 11,	/* ADAT 1 */
	12, 13, 14, 15, 16, 17, 18, 19,	/* ADAT 2 */
	20, 21, 22, 23, 24, 25, 26, 27,	/* ADAT 3 */
	28, 29, 30, 31, 32, 33, 34, 35,	/* ADAT 4 */
	0, 1,			/* AES */
	2, 3,			/* SPDIF */
	-1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
};

static char channel_map_raydat_ds[HDSPM_MAX_CHANNELS] = {
	4, 5, 6, 7,		/* ADAT 1 */
	8, 9, 10, 11,		/* ADAT 2 */
	12, 13, 14, 15,		/* ADAT 3 */
	16, 17, 18, 19,		/* ADAT 4 */
	0, 1,			/* AES */
	2, 3,			/* SPDIF */
	-1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
};

static char channel_map_raydat_qs[HDSPM_MAX_CHANNELS] = {
	4, 5,			/* ADAT 1 */
	6, 7,			/* ADAT 2 */
	8, 9,			/* ADAT 3 */
	10, 11,			/* ADAT 4 */
	0, 1,			/* AES */
	2, 3,			/* SPDIF */
	-1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
};

static char channel_map_aio_in_ss[HDSPM_MAX_CHANNELS] = {
	0, 1,			/* line in */
	8, 9,			/* aes in, */
	10, 11,			/* spdif in */
	12, 13, 14, 15, 16, 17, 18, 19,	/* ADAT in */
	2, 3, 4, 5,		/* AEB */
	-1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
};

static char channel_map_aio_out_ss[HDSPM_MAX_CHANNELS] = {
	0, 1,			/* line out */
	8, 9,			/* aes out */
	10, 11,			/* spdif out */
	12, 13, 14, 15, 16, 17, 18, 19,	/* ADAT out */
	6, 7,			/* phone out */
	2, 3, 4, 5,		/* AEB */
	-1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
};

static char channel_map_aio_in_ds[HDSPM_MAX_CHANNELS] = {
	0, 1,			/* line in */
	8, 9,			/* aes in */
	10, 11,			/* spdif in */
	12, 14, 16, 18,		/* adat in */
	2, 3, 4, 5,		/* AEB */
	-1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1
};

static char channel_map_aio_out_ds[HDSPM_MAX_CHANNELS] = {
	0, 1,			/* line out */
	8, 9,			/* aes out */
	10, 11,			/* spdif out */
	12, 14, 16, 18,		/* adat out */
	6, 7,			/* phone out */
	2, 3, 4, 5,		/* AEB */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1
};

static char channel_map_aio_in_qs[HDSPM_MAX_CHANNELS] = {
	0, 1,			/* line in */
	8, 9,			/* aes in */
	10, 11,			/* spdif in */
	12, 16,			/* adat in */
	2, 3, 4, 5,		/* AEB */
	-1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1
};

static char channel_map_aio_out_qs[HDSPM_MAX_CHANNELS] = {
	0, 1,			/* line out */
	8, 9,			/* aes out */
	10, 11,			/* spdif out */
	12, 16,			/* adat out */
	6, 7,			/* phone out */
	2, 3, 4, 5,		/* AEB */
	-1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1
};

static char channel_map_aes32[HDSPM_MAX_CHANNELS] = {
	0, 1, 2, 3, 4, 5, 6, 7,
	8, 9, 10, 11, 12, 13, 14, 15,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1
};

struct hdspm_midi {
	struct hdspm *hdspm;
	int id;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *input;
	struct snd_rawmidi_substream *output;
	char istimer;		/* timer in use */
	struct timer_list timer;
	spinlock_t lock;
	int pending;
	int dataIn;
	int statusIn;
	int dataOut;
	int statusOut;
	int ie;
	int irq;
};

struct hdspm_tco {
	int input; /* 0: LTC, 1:Video, 2: WC*/
	int framerate; /* 0=24, 1=25, 2=29.97, 3=29.97d, 4=30, 5=30d */
	int wordclock; /* 0=1:1, 1=44.1->48, 2=48->44.1 */
	int samplerate; /* 0=44.1, 1=48, 2= freq from app */
	int pull; /*   0=0, 1=+0.1%, 2=-0.1%, 3=+4%, 4=-4%*/
	int term; /* 0 = off, 1 = on */
};

struct hdspm {
        spinlock_t lock;
	/* only one playback and/or capture stream */
        struct snd_pcm_substream *capture_substream;
        struct snd_pcm_substream *playback_substream;

	char *card_name;	     /* for procinfo */
	unsigned short firmware_rev; /* dont know if relevant (yes if AES32)*/

	uint8_t io_type;

	int monitor_outs;	/* set up monitoring outs init flag */

	u32 control_register;	/* cached value */
	u32 control2_register;	/* cached value */
	u32 settings_register;  /* cached value for AIO / RayDat (sync reference, master/slave) */

	struct hdspm_midi midi[4];
	struct tasklet_struct midi_tasklet;

	size_t period_bytes;
	unsigned char ss_in_channels;
	unsigned char ds_in_channels;
	unsigned char qs_in_channels;
	unsigned char ss_out_channels;
	unsigned char ds_out_channels;
	unsigned char qs_out_channels;

	unsigned char max_channels_in;
	unsigned char max_channels_out;

	signed char *channel_map_in;
	signed char *channel_map_out;

	signed char *channel_map_in_ss, *channel_map_in_ds, *channel_map_in_qs;
	signed char *channel_map_out_ss, *channel_map_out_ds, *channel_map_out_qs;

	char **port_names_in;
	char **port_names_out;

	char **port_names_in_ss, **port_names_in_ds, **port_names_in_qs;
	char **port_names_out_ss, **port_names_out_ds, **port_names_out_qs;

	unsigned char *playback_buffer;	/* suitably aligned address */
	unsigned char *capture_buffer;	/* suitably aligned address */

	pid_t capture_pid;	/* process id which uses capture */
	pid_t playback_pid;	/* process id which uses capture */
	int running;		/* running status */

	int last_external_sample_rate;	/* samplerate mystic ... */
	int last_internal_sample_rate;
	int system_sample_rate;

	int dev;		/* Hardware vars... */
	int irq;
	unsigned long port;
	void __iomem *iobase;

	int irq_count;		/* for debug */
	int midiPorts;

	struct snd_card *card;	/* one card */
	struct snd_pcm *pcm;		/* has one pcm */
	struct snd_hwdep *hwdep;	/* and a hwdep for additional ioctl */
	struct pci_dev *pci;	/* and an pci info */

	/* Mixer vars */
	/* fast alsa mixer */
	struct snd_kcontrol *playback_mixer_ctls[HDSPM_MAX_CHANNELS];
	/* but input to much, so not used */
	struct snd_kcontrol *input_mixer_ctls[HDSPM_MAX_CHANNELS];
	/* full mixer accessible over mixer ioctl or hwdep-device */
	struct hdspm_mixer *mixer;

	struct hdspm_tco *tco;  /* NULL if no TCO detected */

	const char *const *texts_autosync;
	int texts_autosync_items;

	cycles_t last_interrupt;

	unsigned int serial;

	struct hdspm_peak_rms peak_rms;
};


static const struct pci_device_id snd_hdspm_ids[] = {
	{
	 .vendor = PCI_VENDOR_ID_XILINX,
	 .device = PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP_MADI,
	 .subvendor = PCI_ANY_ID,
	 .subdevice = PCI_ANY_ID,
	 .class = 0,
	 .class_mask = 0,
	 .driver_data = 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, snd_hdspm_ids);

/* prototypes */
static int snd_hdspm_create_alsa_devices(struct snd_card *card,
					 struct hdspm *hdspm);
static int snd_hdspm_create_pcm(struct snd_card *card,
				struct hdspm *hdspm);

static inline void snd_hdspm_initialize_midi_flush(struct hdspm *hdspm);
static inline int hdspm_get_pll_freq(struct hdspm *hdspm);
static int hdspm_update_simple_mixer_controls(struct hdspm *hdspm);
static int hdspm_autosync_ref(struct hdspm *hdspm);
static int hdspm_set_toggle_setting(struct hdspm *hdspm, u32 regmask, int out);
static int snd_hdspm_set_defaults(struct hdspm *hdspm);
static int hdspm_system_clock_mode(struct hdspm *hdspm);
static void hdspm_set_sgbuf(struct hdspm *hdspm,
			    struct snd_pcm_substream *substream,
			     unsigned int reg, int channels);

static int hdspm_aes_sync_check(struct hdspm *hdspm, int idx);
static int hdspm_wc_sync_check(struct hdspm *hdspm);
static int hdspm_tco_sync_check(struct hdspm *hdspm);
static int hdspm_sync_in_sync_check(struct hdspm *hdspm);

static int hdspm_get_aes_sample_rate(struct hdspm *hdspm, int index);
static int hdspm_get_tco_sample_rate(struct hdspm *hdspm);
static int hdspm_get_wc_sample_rate(struct hdspm *hdspm);



static inline int HDSPM_bit2freq(int n)
{
	static const int bit2freq_tab[] = {
		0, 32000, 44100, 48000, 64000, 88200,
		96000, 128000, 176400, 192000 };
	if (n < 1 || n > 9)
		return 0;
	return bit2freq_tab[n];
}

static bool hdspm_is_raydat_or_aio(struct hdspm *hdspm)
{
	return ((AIO == hdspm->io_type) || (RayDAT == hdspm->io_type));
}


/* Write/read to/from HDSPM with Adresses in Bytes
   not words but only 32Bit writes are allowed */

static inline void hdspm_write(struct hdspm * hdspm, unsigned int reg,
			       unsigned int val)
{
	writel(val, hdspm->iobase + reg);
}

static inline unsigned int hdspm_read(struct hdspm * hdspm, unsigned int reg)
{
	return readl(hdspm->iobase + reg);
}

/* for each output channel (chan) I have an Input (in) and Playback (pb) Fader
   mixer is write only on hardware so we have to cache him for read
   each fader is a u32, but uses only the first 16 bit */

static inline int hdspm_read_in_gain(struct hdspm * hdspm, unsigned int chan,
				     unsigned int in)
{
	if (chan >= HDSPM_MIXER_CHANNELS || in >= HDSPM_MIXER_CHANNELS)
		return 0;

	return hdspm->mixer->ch[chan].in[in];
}

static inline int hdspm_read_pb_gain(struct hdspm * hdspm, unsigned int chan,
				     unsigned int pb)
{
	if (chan >= HDSPM_MIXER_CHANNELS || pb >= HDSPM_MIXER_CHANNELS)
		return 0;
	return hdspm->mixer->ch[chan].pb[pb];
}

static int hdspm_write_in_gain(struct hdspm *hdspm, unsigned int chan,
				      unsigned int in, unsigned short data)
{
	if (chan >= HDSPM_MIXER_CHANNELS || in >= HDSPM_MIXER_CHANNELS)
		return -1;

	hdspm_write(hdspm,
		    HDSPM_MADI_mixerBase +
		    ((in + 128 * chan) * sizeof(u32)),
		    (hdspm->mixer->ch[chan].in[in] = data & 0xFFFF));
	return 0;
}

static int hdspm_write_pb_gain(struct hdspm *hdspm, unsigned int chan,
				      unsigned int pb, unsigned short data)
{
	if (chan >= HDSPM_MIXER_CHANNELS || pb >= HDSPM_MIXER_CHANNELS)
		return -1;

	hdspm_write(hdspm,
		    HDSPM_MADI_mixerBase +
		    ((64 + pb + 128 * chan) * sizeof(u32)),
		    (hdspm->mixer->ch[chan].pb[pb] = data & 0xFFFF));
	return 0;
}


/* enable DMA for specific channels, now available for DSP-MADI */
static inline void snd_hdspm_enable_in(struct hdspm * hdspm, int i, int v)
{
	hdspm_write(hdspm, HDSPM_inputEnableBase + (4 * i), v);
}

static inline void snd_hdspm_enable_out(struct hdspm * hdspm, int i, int v)
{
	hdspm_write(hdspm, HDSPM_outputEnableBase + (4 * i), v);
}

/* check if same process is writing and reading */
static int snd_hdspm_use_is_exclusive(struct hdspm *hdspm)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&hdspm->lock, flags);
	if ((hdspm->playback_pid != hdspm->capture_pid) &&
	    (hdspm->playback_pid >= 0) && (hdspm->capture_pid >= 0)) {
		ret = 0;
	}
	spin_unlock_irqrestore(&hdspm->lock, flags);
	return ret;
}

/* round arbitary sample rates to commonly known rates */
static int hdspm_round_frequency(int rate)
{
	if (rate < 38050)
		return 32000;
	if (rate < 46008)
		return 44100;
	else
		return 48000;
}

/* QS and DS rates normally can not be detected
 * automatically by the card. Only exception is MADI
 * in 96k frame mode.
 *
 * So if we read SS values (32 .. 48k), check for
 * user-provided DS/QS bits in the control register
 * and multiply the base frequency accordingly.
 */
static int hdspm_rate_multiplier(struct hdspm *hdspm, int rate)
{
	if (rate <= 48000) {
		if (hdspm->control_register & HDSPM_QuadSpeed)
			return rate * 4;
		else if (hdspm->control_register &
				HDSPM_DoubleSpeed)
			return rate * 2;
	}
	return rate;
}

/* check for external sample rate, returns the sample rate in Hz*/
static int hdspm_external_sample_rate(struct hdspm *hdspm)
{
	unsigned int status, status2;
	int syncref, rate = 0, rate_bits;

	switch (hdspm->io_type) {
	case AES32:
		status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
		status = hdspm_read(hdspm, HDSPM_statusRegister);

		syncref = hdspm_autosync_ref(hdspm);
		switch (syncref) {
		case HDSPM_AES32_AUTOSYNC_FROM_WORD:
		/* Check WC sync and get sample rate */
			if (hdspm_wc_sync_check(hdspm))
				return HDSPM_bit2freq(hdspm_get_wc_sample_rate(hdspm));
			break;

		case HDSPM_AES32_AUTOSYNC_FROM_AES1:
		case HDSPM_AES32_AUTOSYNC_FROM_AES2:
		case HDSPM_AES32_AUTOSYNC_FROM_AES3:
		case HDSPM_AES32_AUTOSYNC_FROM_AES4:
		case HDSPM_AES32_AUTOSYNC_FROM_AES5:
		case HDSPM_AES32_AUTOSYNC_FROM_AES6:
		case HDSPM_AES32_AUTOSYNC_FROM_AES7:
		case HDSPM_AES32_AUTOSYNC_FROM_AES8:
		/* Check AES sync and get sample rate */
			if (hdspm_aes_sync_check(hdspm, syncref - HDSPM_AES32_AUTOSYNC_FROM_AES1))
				return HDSPM_bit2freq(hdspm_get_aes_sample_rate(hdspm,
							syncref - HDSPM_AES32_AUTOSYNC_FROM_AES1));
			break;


		case HDSPM_AES32_AUTOSYNC_FROM_TCO:
		/* Check TCO sync and get sample rate */
			if (hdspm_tco_sync_check(hdspm))
				return HDSPM_bit2freq(hdspm_get_tco_sample_rate(hdspm));
			break;
		default:
			return 0;
		} /* end switch(syncref) */
		break;

	case MADIface:
		status = hdspm_read(hdspm, HDSPM_statusRegister);

		if (!(status & HDSPM_madiLock)) {
			rate = 0;  /* no lock */
		} else {
			switch (status & (HDSPM_status1_freqMask)) {
			case HDSPM_status1_F_0*1:
				rate = 32000; break;
			case HDSPM_status1_F_0*2:
				rate = 44100; break;
			case HDSPM_status1_F_0*3:
				rate = 48000; break;
			case HDSPM_status1_F_0*4:
				rate = 64000; break;
			case HDSPM_status1_F_0*5:
				rate = 88200; break;
			case HDSPM_status1_F_0*6:
				rate = 96000; break;
			case HDSPM_status1_F_0*7:
				rate = 128000; break;
			case HDSPM_status1_F_0*8:
				rate = 176400; break;
			case HDSPM_status1_F_0*9:
				rate = 192000; break;
			default:
				rate = 0; break;
			}
		}

		break;

	case MADI:
	case AIO:
	case RayDAT:
		status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
		status = hdspm_read(hdspm, HDSPM_statusRegister);
		rate = 0;

		/* if wordclock has synced freq and wordclock is valid */
		if ((status2 & HDSPM_wcLock) != 0 &&
				(status2 & HDSPM_SelSyncRef0) == 0) {

			rate_bits = status2 & HDSPM_wcFreqMask;


			switch (rate_bits) {
			case HDSPM_wcFreq32:
				rate = 32000;
				break;
			case HDSPM_wcFreq44_1:
				rate = 44100;
				break;
			case HDSPM_wcFreq48:
				rate = 48000;
				break;
			case HDSPM_wcFreq64:
				rate = 64000;
				break;
			case HDSPM_wcFreq88_2:
				rate = 88200;
				break;
			case HDSPM_wcFreq96:
				rate = 96000;
				break;
			case HDSPM_wcFreq128:
				rate = 128000;
				break;
			case HDSPM_wcFreq176_4:
				rate = 176400;
				break;
			case HDSPM_wcFreq192:
				rate = 192000;
				break;
			default:
				rate = 0;
				break;
			}
		}

		/* if rate detected and Syncref is Word than have it,
		 * word has priority to MADI
		 */
		if (rate != 0 &&
		(status2 & HDSPM_SelSyncRefMask) == HDSPM_SelSyncRef_WORD)
			return hdspm_rate_multiplier(hdspm, rate);

		/* maybe a madi input (which is taken if sel sync is madi) */
		if (status & HDSPM_madiLock) {
			rate_bits = status & HDSPM_madiFreqMask;

			switch (rate_bits) {
			case HDSPM_madiFreq32:
				rate = 32000;
				break;
			case HDSPM_madiFreq44_1:
				rate = 44100;
				break;
			case HDSPM_madiFreq48:
				rate = 48000;
				break;
			case HDSPM_madiFreq64:
				rate = 64000;
				break;
			case HDSPM_madiFreq88_2:
				rate = 88200;
				break;
			case HDSPM_madiFreq96:
				rate = 96000;
				break;
			case HDSPM_madiFreq128:
				rate = 128000;
				break;
			case HDSPM_madiFreq176_4:
				rate = 176400;
				break;
			case HDSPM_madiFreq192:
				rate = 192000;
				break;
			default:
				rate = 0;
				break;
			}

		} /* endif HDSPM_madiLock */

		/* check sample rate from TCO or SYNC_IN */
		{
			bool is_valid_input = 0;
			bool has_sync = 0;

			syncref = hdspm_autosync_ref(hdspm);
			if (HDSPM_AUTOSYNC_FROM_TCO == syncref) {
				is_valid_input = 1;
				has_sync = (HDSPM_SYNC_CHECK_SYNC ==
					hdspm_tco_sync_check(hdspm));
			} else if (HDSPM_AUTOSYNC_FROM_SYNC_IN == syncref) {
				is_valid_input = 1;
				has_sync = (HDSPM_SYNC_CHECK_SYNC ==
					hdspm_sync_in_sync_check(hdspm));
			}

			if (is_valid_input && has_sync) {
				rate = hdspm_round_frequency(
					hdspm_get_pll_freq(hdspm));
			}
		}

		rate = hdspm_rate_multiplier(hdspm, rate);

		break;
	}

	return rate;
}

/* return latency in samples per period */
static int hdspm_get_latency(struct hdspm *hdspm)
{
	int n;

	n = hdspm_decode_latency(hdspm->control_register);

	/* Special case for new RME cards with 32 samples period size.
	 * The three latency bits in the control register
	 * (HDSP_LatencyMask) encode latency values of 64 samples as
	 * 0, 128 samples as 1 ... 4096 samples as 6. For old cards, 7
	 * denotes 8192 samples, but on new cards like RayDAT or AIO,
	 * it corresponds to 32 samples.
	 */
	if ((7 == n) && (RayDAT == hdspm->io_type || AIO == hdspm->io_type))
		n = -1;

	return 1 << (n + 6);
}

/* Latency function */
static inline void hdspm_compute_period_size(struct hdspm *hdspm)
{
	hdspm->period_bytes = 4 * hdspm_get_latency(hdspm);
}


static snd_pcm_uframes_t hdspm_hw_pointer(struct hdspm *hdspm)
{
	int position;

	position = hdspm_read(hdspm, HDSPM_statusRegister);

	switch (hdspm->io_type) {
	case RayDAT:
	case AIO:
		position &= HDSPM_BufferPositionMask;
		position /= 4; /* Bytes per sample */
		break;
	default:
		position = (position & HDSPM_BufferID) ?
			(hdspm->period_bytes / 4) : 0;
	}

	return position;
}


static inline void hdspm_start_audio(struct hdspm * s)
{
	s->control_register |= (HDSPM_AudioInterruptEnable | HDSPM_Start);
	hdspm_write(s, HDSPM_controlRegister, s->control_register);
}

static inline void hdspm_stop_audio(struct hdspm * s)
{
	s->control_register &= ~(HDSPM_Start | HDSPM_AudioInterruptEnable);
	hdspm_write(s, HDSPM_controlRegister, s->control_register);
}

/* should I silence all or only opened ones ? doit all for first even is 4MB*/
static void hdspm_silence_playback(struct hdspm *hdspm)
{
	int i;
	int n = hdspm->period_bytes;
	void *buf = hdspm->playback_buffer;

	if (buf == NULL)
		return;

	for (i = 0; i < HDSPM_MAX_CHANNELS; i++) {
		memset(buf, 0, n);
		buf += HDSPM_CHANNEL_BUFFER_BYTES;
	}
}

static int hdspm_set_interrupt_interval(struct hdspm *s, unsigned int frames)
{
	int n;

	spin_lock_irq(&s->lock);

	if (32 == frames) {
		/* Special case for new RME cards like RayDAT/AIO which
		 * support period sizes of 32 samples. Since latency is
		 * encoded in the three bits of HDSP_LatencyMask, we can only
		 * have values from 0 .. 7. While 0 still means 64 samples and
		 * 6 represents 4096 samples on all cards, 7 represents 8192
		 * on older cards and 32 samples on new cards.
		 *
		 * In other words, period size in samples is calculated by
		 * 2^(n+6) with n ranging from 0 .. 7.
		 */
		n = 7;
	} else {
		frames >>= 7;
		n = 0;
		while (frames) {
			n++;
			frames >>= 1;
		}
	}

	s->control_register &= ~HDSPM_LatencyMask;
	s->control_register |= hdspm_encode_latency(n);

	hdspm_write(s, HDSPM_controlRegister, s->control_register);

	hdspm_compute_period_size(s);

	spin_unlock_irq(&s->lock);

	return 0;
}

static u64 hdspm_calc_dds_value(struct hdspm *hdspm, u64 period)
{
	u64 freq_const;

	if (period == 0)
		return 0;

	switch (hdspm->io_type) {
	case MADI:
	case AES32:
		freq_const = 110069313433624ULL;
		break;
	case RayDAT:
	case AIO:
		freq_const = 104857600000000ULL;
		break;
	case MADIface:
		freq_const = 131072000000000ULL;
		break;
	default:
		snd_BUG();
		return 0;
	}

	return div_u64(freq_const, period);
}


static void hdspm_set_dds_value(struct hdspm *hdspm, int rate)
{
	u64 n;

	if (rate >= 112000)
		rate /= 4;
	else if (rate >= 56000)
		rate /= 2;

	switch (hdspm->io_type) {
	case MADIface:
		n = 131072000000000ULL;  /* 125 MHz */
		break;
	case MADI:
	case AES32:
		n = 110069313433624ULL;  /* 105 MHz */
		break;
	case RayDAT:
	case AIO:
		n = 104857600000000ULL;  /* 100 MHz */
		break;
	default:
		snd_BUG();
		return;
	}

	n = div_u64(n, rate);
	/* n should be less than 2^32 for being written to FREQ register */
	snd_BUG_ON(n >> 32);
	hdspm_write(hdspm, HDSPM_freqReg, (u32)n);
}

/* dummy set rate lets see what happens */
static int hdspm_set_rate(struct hdspm * hdspm, int rate, int called_internally)
{
	int current_rate;
	int rate_bits;
	int not_set = 0;
	int current_speed, target_speed;

	/* ASSUMPTION: hdspm->lock is either set, or there is no need for
	   it (e.g. during module initialization).
	 */

	if (!(hdspm->control_register & HDSPM_ClockModeMaster)) {

		/* SLAVE --- */
		if (called_internally) {

			/* request from ctl or card initialization
			   just make a warning an remember setting
			   for future master mode switching */

			dev_warn(hdspm->card->dev,
				 "Warning: device is not running as a clock master.\n");
			not_set = 1;
		} else {

			/* hw_param request while in AutoSync mode */
			int external_freq =
			    hdspm_external_sample_rate(hdspm);

			if (hdspm_autosync_ref(hdspm) ==
			    HDSPM_AUTOSYNC_FROM_NONE) {

				dev_warn(hdspm->card->dev,
					 "Detected no Externel Sync\n");
				not_set = 1;

			} else if (rate != external_freq) {

				dev_warn(hdspm->card->dev,
					 "Warning: No AutoSync source for requested rate\n");
				not_set = 1;
			}
		}
	}

	current_rate = hdspm->system_sample_rate;

	/* Changing between Singe, Double and Quad speed is not
	   allowed if any substreams are open. This is because such a change
	   causes a shift in the location of the DMA buffers and a reduction
	   in the number of available buffers.

	   Note that a similar but essentially insoluble problem exists for
	   externally-driven rate changes. All we can do is to flag rate
	   changes in the read/write routines.
	 */

	if (current_rate <= 48000)
		current_speed = HDSPM_SPEED_SINGLE;
	else if (current_rate <= 96000)
		current_speed = HDSPM_SPEED_DOUBLE;
	else
		current_speed = HDSPM_SPEED_QUAD;

	if (rate <= 48000)
		target_speed = HDSPM_SPEED_SINGLE;
	else if (rate <= 96000)
		target_speed = HDSPM_SPEED_DOUBLE;
	else
		target_speed = HDSPM_SPEED_QUAD;

	switch (rate) {
	case 32000:
		rate_bits = HDSPM_Frequency32KHz;
		break;
	case 44100:
		rate_bits = HDSPM_Frequency44_1KHz;
		break;
	case 48000:
		rate_bits = HDSPM_Frequency48KHz;
		break;
	case 64000:
		rate_bits = HDSPM_Frequency64KHz;
		break;
	case 88200:
		rate_bits = HDSPM_Frequency88_2KHz;
		break;
	case 96000:
		rate_bits = HDSPM_Frequency96KHz;
		break;
	case 128000:
		rate_bits = HDSPM_Frequency128KHz;
		break;
	case 176400:
		rate_bits = HDSPM_Frequency176_4KHz;
		break;
	case 192000:
		rate_bits = HDSPM_Frequency192KHz;
		break;
	default:
		return -EINVAL;
	}

	if (current_speed != target_speed
	    && (hdspm->capture_pid >= 0 || hdspm->playback_pid >= 0)) {
		dev_err(hdspm->card->dev,
			"cannot change from %s speed to %s speed mode (capture PID = %d, playback PID = %d)\n",
			hdspm_speed_names[current_speed],
			hdspm_speed_names[target_speed],
			hdspm->capture_pid, hdspm->playback_pid);
		return -EBUSY;
	}

	hdspm->control_register &= ~HDSPM_FrequencyMask;
	hdspm->control_register |= rate_bits;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	/* For AES32, need to set DDS value in FREQ register
	   For MADI, also apparently */
	hdspm_set_dds_value(hdspm, rate);

	if (AES32 == hdspm->io_type && rate != current_rate)
		hdspm_write(hdspm, HDSPM_eeprom_wr, 0);

	hdspm->system_sample_rate = rate;

	if (rate <= 48000) {
		hdspm->channel_map_in = hdspm->channel_map_in_ss;
		hdspm->channel_map_out = hdspm->channel_map_out_ss;
		hdspm->max_channels_in = hdspm->ss_in_channels;
		hdspm->max_channels_out = hdspm->ss_out_channels;
		hdspm->port_names_in = hdspm->port_names_in_ss;
		hdspm->port_names_out = hdspm->port_names_out_ss;
	} else if (rate <= 96000) {
		hdspm->channel_map_in = hdspm->channel_map_in_ds;
		hdspm->channel_map_out = hdspm->channel_map_out_ds;
		hdspm->max_channels_in = hdspm->ds_in_channels;
		hdspm->max_channels_out = hdspm->ds_out_channels;
		hdspm->port_names_in = hdspm->port_names_in_ds;
		hdspm->port_names_out = hdspm->port_names_out_ds;
	} else {
		hdspm->channel_map_in = hdspm->channel_map_in_qs;
		hdspm->channel_map_out = hdspm->channel_map_out_qs;
		hdspm->max_channels_in = hdspm->qs_in_channels;
		hdspm->max_channels_out = hdspm->qs_out_channels;
		hdspm->port_names_in = hdspm->port_names_in_qs;
		hdspm->port_names_out = hdspm->port_names_out_qs;
	}

	if (not_set != 0)
		return -1;

	return 0;
}

/* mainly for init to 0 on load */
static void all_in_all_mixer(struct hdspm * hdspm, int sgain)
{
	int i, j;
	unsigned int gain;

	if (sgain > UNITY_GAIN)
		gain = UNITY_GAIN;
	else if (sgain < 0)
		gain = 0;
	else
		gain = sgain;

	for (i = 0; i < HDSPM_MIXER_CHANNELS; i++)
		for (j = 0; j < HDSPM_MIXER_CHANNELS; j++) {
			hdspm_write_in_gain(hdspm, i, j, gain);
			hdspm_write_pb_gain(hdspm, i, j, gain);
		}
}

/*----------------------------------------------------------------------------
   MIDI
  ----------------------------------------------------------------------------*/

static inline unsigned char snd_hdspm_midi_read_byte (struct hdspm *hdspm,
						      int id)
{
	/* the hardware already does the relevant bit-mask with 0xff */
	return hdspm_read(hdspm, hdspm->midi[id].dataIn);
}

static inline void snd_hdspm_midi_write_byte (struct hdspm *hdspm, int id,
					      int val)
{
	/* the hardware already does the relevant bit-mask with 0xff */
	return hdspm_write(hdspm, hdspm->midi[id].dataOut, val);
}

static inline int snd_hdspm_midi_input_available (struct hdspm *hdspm, int id)
{
	return hdspm_read(hdspm, hdspm->midi[id].statusIn) & 0xFF;
}

static inline int snd_hdspm_midi_output_possible (struct hdspm *hdspm, int id)
{
	int fifo_bytes_used;

	fifo_bytes_used = hdspm_read(hdspm, hdspm->midi[id].statusOut) & 0xFF;

	if (fifo_bytes_used < 128)
		return  128 - fifo_bytes_used;
	else
		return 0;
}

static void snd_hdspm_flush_midi_input(struct hdspm *hdspm, int id)
{
	while (snd_hdspm_midi_input_available (hdspm, id))
		snd_hdspm_midi_read_byte (hdspm, id);
}

static int snd_hdspm_midi_output_write (struct hdspm_midi *hmidi)
{
	unsigned long flags;
	int n_pending;
	int to_write;
	int i;
	unsigned char buf[128];

	/* Output is not interrupt driven */

	spin_lock_irqsave (&hmidi->lock, flags);
	if (hmidi->output &&
	    !snd_rawmidi_transmit_empty (hmidi->output)) {
		n_pending = snd_hdspm_midi_output_possible (hmidi->hdspm,
							    hmidi->id);
		if (n_pending > 0) {
			if (n_pending > (int)sizeof (buf))
				n_pending = sizeof (buf);

			to_write = snd_rawmidi_transmit (hmidi->output, buf,
							 n_pending);
			if (to_write > 0) {
				for (i = 0; i < to_write; ++i)
					snd_hdspm_midi_write_byte (hmidi->hdspm,
								   hmidi->id,
								   buf[i]);
			}
		}
	}
	spin_unlock_irqrestore (&hmidi->lock, flags);
	return 0;
}

static int snd_hdspm_midi_input_read (struct hdspm_midi *hmidi)
{
	unsigned char buf[128]; /* this buffer is designed to match the MIDI
				 * input FIFO size
				 */
	unsigned long flags;
	int n_pending;
	int i;

	spin_lock_irqsave (&hmidi->lock, flags);
	n_pending = snd_hdspm_midi_input_available (hmidi->hdspm, hmidi->id);
	if (n_pending > 0) {
		if (hmidi->input) {
			if (n_pending > (int)sizeof (buf))
				n_pending = sizeof (buf);
			for (i = 0; i < n_pending; ++i)
				buf[i] = snd_hdspm_midi_read_byte (hmidi->hdspm,
								   hmidi->id);
			if (n_pending)
				snd_rawmidi_receive (hmidi->input, buf,
						     n_pending);
		} else {
			/* flush the MIDI input FIFO */
			while (n_pending--)
				snd_hdspm_midi_read_byte (hmidi->hdspm,
							  hmidi->id);
		}
	}
	hmidi->pending = 0;
	spin_unlock_irqrestore(&hmidi->lock, flags);

	spin_lock_irqsave(&hmidi->hdspm->lock, flags);
	hmidi->hdspm->control_register |= hmidi->ie;
	hdspm_write(hmidi->hdspm, HDSPM_controlRegister,
		    hmidi->hdspm->control_register);
	spin_unlock_irqrestore(&hmidi->hdspm->lock, flags);

	return snd_hdspm_midi_output_write (hmidi);
}

static void
snd_hdspm_midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct hdspm *hdspm;
	struct hdspm_midi *hmidi;
	unsigned long flags;

	hmidi = substream->rmidi->private_data;
	hdspm = hmidi->hdspm;

	spin_lock_irqsave (&hdspm->lock, flags);
	if (up) {
		if (!(hdspm->control_register & hmidi->ie)) {
			snd_hdspm_flush_midi_input (hdspm, hmidi->id);
			hdspm->control_register |= hmidi->ie;
		}
	} else {
		hdspm->control_register &= ~hmidi->ie;
	}

	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);
	spin_unlock_irqrestore (&hdspm->lock, flags);
}

static void snd_hdspm_midi_output_timer(unsigned long data)
{
	struct hdspm_midi *hmidi = (struct hdspm_midi *) data;
	unsigned long flags;

	snd_hdspm_midi_output_write(hmidi);
	spin_lock_irqsave (&hmidi->lock, flags);

	/* this does not bump hmidi->istimer, because the
	   kernel automatically removed the timer when it
	   expired, and we are now adding it back, thus
	   leaving istimer wherever it was set before.
	*/

	if (hmidi->istimer)
		mod_timer(&hmidi->timer, 1 + jiffies);

	spin_unlock_irqrestore (&hmidi->lock, flags);
}

static void
snd_hdspm_midi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct hdspm_midi *hmidi;
	unsigned long flags;

	hmidi = substream->rmidi->private_data;
	spin_lock_irqsave (&hmidi->lock, flags);
	if (up) {
		if (!hmidi->istimer) {
			setup_timer(&hmidi->timer, snd_hdspm_midi_output_timer,
				    (unsigned long) hmidi);
			mod_timer(&hmidi->timer, 1 + jiffies);
			hmidi->istimer++;
		}
	} else {
		if (hmidi->istimer && --hmidi->istimer <= 0)
			del_timer (&hmidi->timer);
	}
	spin_unlock_irqrestore (&hmidi->lock, flags);
	if (up)
		snd_hdspm_midi_output_write(hmidi);
}

static int snd_hdspm_midi_input_open(struct snd_rawmidi_substream *substream)
{
	struct hdspm_midi *hmidi;

	hmidi = substream->rmidi->private_data;
	spin_lock_irq (&hmidi->lock);
	snd_hdspm_flush_midi_input (hmidi->hdspm, hmidi->id);
	hmidi->input = substream;
	spin_unlock_irq (&hmidi->lock);

	return 0;
}

static int snd_hdspm_midi_output_open(struct snd_rawmidi_substream *substream)
{
	struct hdspm_midi *hmidi;

	hmidi = substream->rmidi->private_data;
	spin_lock_irq (&hmidi->lock);
	hmidi->output = substream;
	spin_unlock_irq (&hmidi->lock);

	return 0;
}

static int snd_hdspm_midi_input_close(struct snd_rawmidi_substream *substream)
{
	struct hdspm_midi *hmidi;

	snd_hdspm_midi_input_trigger (substream, 0);

	hmidi = substream->rmidi->private_data;
	spin_lock_irq (&hmidi->lock);
	hmidi->input = NULL;
	spin_unlock_irq (&hmidi->lock);

	return 0;
}

static int snd_hdspm_midi_output_close(struct snd_rawmidi_substream *substream)
{
	struct hdspm_midi *hmidi;

	snd_hdspm_midi_output_trigger (substream, 0);

	hmidi = substream->rmidi->private_data;
	spin_lock_irq (&hmidi->lock);
	hmidi->output = NULL;
	spin_unlock_irq (&hmidi->lock);

	return 0;
}

static struct snd_rawmidi_ops snd_hdspm_midi_output =
{
	.open =		snd_hdspm_midi_output_open,
	.close =	snd_hdspm_midi_output_close,
	.trigger =	snd_hdspm_midi_output_trigger,
};

static struct snd_rawmidi_ops snd_hdspm_midi_input =
{
	.open =		snd_hdspm_midi_input_open,
	.close =	snd_hdspm_midi_input_close,
	.trigger =	snd_hdspm_midi_input_trigger,
};

static int snd_hdspm_create_midi(struct snd_card *card,
				 struct hdspm *hdspm, int id)
{
	int err;
	char buf[32];

	hdspm->midi[id].id = id;
	hdspm->midi[id].hdspm = hdspm;
	spin_lock_init (&hdspm->midi[id].lock);

	if (0 == id) {
		if (MADIface == hdspm->io_type) {
			/* MIDI-over-MADI on HDSPe MADIface */
			hdspm->midi[0].dataIn = HDSPM_midiDataIn2;
			hdspm->midi[0].statusIn = HDSPM_midiStatusIn2;
			hdspm->midi[0].dataOut = HDSPM_midiDataOut2;
			hdspm->midi[0].statusOut = HDSPM_midiStatusOut2;
			hdspm->midi[0].ie = HDSPM_Midi2InterruptEnable;
			hdspm->midi[0].irq = HDSPM_midi2IRQPending;
		} else {
			hdspm->midi[0].dataIn = HDSPM_midiDataIn0;
			hdspm->midi[0].statusIn = HDSPM_midiStatusIn0;
			hdspm->midi[0].dataOut = HDSPM_midiDataOut0;
			hdspm->midi[0].statusOut = HDSPM_midiStatusOut0;
			hdspm->midi[0].ie = HDSPM_Midi0InterruptEnable;
			hdspm->midi[0].irq = HDSPM_midi0IRQPending;
		}
	} else if (1 == id) {
		hdspm->midi[1].dataIn = HDSPM_midiDataIn1;
		hdspm->midi[1].statusIn = HDSPM_midiStatusIn1;
		hdspm->midi[1].dataOut = HDSPM_midiDataOut1;
		hdspm->midi[1].statusOut = HDSPM_midiStatusOut1;
		hdspm->midi[1].ie = HDSPM_Midi1InterruptEnable;
		hdspm->midi[1].irq = HDSPM_midi1IRQPending;
	} else if ((2 == id) && (MADI == hdspm->io_type)) {
		/* MIDI-over-MADI on HDSPe MADI */
		hdspm->midi[2].dataIn = HDSPM_midiDataIn2;
		hdspm->midi[2].statusIn = HDSPM_midiStatusIn2;
		hdspm->midi[2].dataOut = HDSPM_midiDataOut2;
		hdspm->midi[2].statusOut = HDSPM_midiStatusOut2;
		hdspm->midi[2].ie = HDSPM_Midi2InterruptEnable;
		hdspm->midi[2].irq = HDSPM_midi2IRQPending;
	} else if (2 == id) {
		/* TCO MTC, read only */
		hdspm->midi[2].dataIn = HDSPM_midiDataIn2;
		hdspm->midi[2].statusIn = HDSPM_midiStatusIn2;
		hdspm->midi[2].dataOut = -1;
		hdspm->midi[2].statusOut = -1;
		hdspm->midi[2].ie = HDSPM_Midi2InterruptEnable;
		hdspm->midi[2].irq = HDSPM_midi2IRQPendingAES;
	} else if (3 == id) {
		/* TCO MTC on HDSPe MADI */
		hdspm->midi[3].dataIn = HDSPM_midiDataIn3;
		hdspm->midi[3].statusIn = HDSPM_midiStatusIn3;
		hdspm->midi[3].dataOut = -1;
		hdspm->midi[3].statusOut = -1;
		hdspm->midi[3].ie = HDSPM_Midi3InterruptEnable;
		hdspm->midi[3].irq = HDSPM_midi3IRQPending;
	}

	if ((id < 2) || ((2 == id) && ((MADI == hdspm->io_type) ||
					(MADIface == hdspm->io_type)))) {
		if ((id == 0) && (MADIface == hdspm->io_type)) {
			sprintf(buf, "%s MIDIoverMADI", card->shortname);
		} else if ((id == 2) && (MADI == hdspm->io_type)) {
			sprintf(buf, "%s MIDIoverMADI", card->shortname);
		} else {
			sprintf(buf, "%s MIDI %d", card->shortname, id+1);
		}
		err = snd_rawmidi_new(card, buf, id, 1, 1,
				&hdspm->midi[id].rmidi);
		if (err < 0)
			return err;

		sprintf(hdspm->midi[id].rmidi->name, "%s MIDI %d",
				card->id, id+1);
		hdspm->midi[id].rmidi->private_data = &hdspm->midi[id];

		snd_rawmidi_set_ops(hdspm->midi[id].rmidi,
				SNDRV_RAWMIDI_STREAM_OUTPUT,
				&snd_hdspm_midi_output);
		snd_rawmidi_set_ops(hdspm->midi[id].rmidi,
				SNDRV_RAWMIDI_STREAM_INPUT,
				&snd_hdspm_midi_input);

		hdspm->midi[id].rmidi->info_flags |=
			SNDRV_RAWMIDI_INFO_OUTPUT |
			SNDRV_RAWMIDI_INFO_INPUT |
			SNDRV_RAWMIDI_INFO_DUPLEX;
	} else {
		/* TCO MTC, read only */
		sprintf(buf, "%s MTC %d", card->shortname, id+1);
		err = snd_rawmidi_new(card, buf, id, 1, 1,
				&hdspm->midi[id].rmidi);
		if (err < 0)
			return err;

		sprintf(hdspm->midi[id].rmidi->name,
				"%s MTC %d", card->id, id+1);
		hdspm->midi[id].rmidi->private_data = &hdspm->midi[id];

		snd_rawmidi_set_ops(hdspm->midi[id].rmidi,
				SNDRV_RAWMIDI_STREAM_INPUT,
				&snd_hdspm_midi_input);

		hdspm->midi[id].rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
	}

	return 0;
}


static void hdspm_midi_tasklet(unsigned long arg)
{
	struct hdspm *hdspm = (struct hdspm *)arg;
	int i = 0;

	while (i < hdspm->midiPorts) {
		if (hdspm->midi[i].pending)
			snd_hdspm_midi_input_read(&hdspm->midi[i]);

		i++;
	}
}


/*-----------------------------------------------------------------------------
  Status Interface
  ----------------------------------------------------------------------------*/

/* get the system sample rate which is set */


static inline int hdspm_get_pll_freq(struct hdspm *hdspm)
{
	unsigned int period, rate;

	period = hdspm_read(hdspm, HDSPM_RD_PLL_FREQ);
	rate = hdspm_calc_dds_value(hdspm, period);

	return rate;
}

/*
 * Calculate the real sample rate from the
 * current DDS value.
 */
static int hdspm_get_system_sample_rate(struct hdspm *hdspm)
{
	unsigned int rate;

	rate = hdspm_get_pll_freq(hdspm);

	if (rate > 207000) {
		/* Unreasonable high sample rate as seen on PCI MADI cards. */
		if (0 == hdspm_system_clock_mode(hdspm)) {
			/* master mode, return internal sample rate */
			rate = hdspm->system_sample_rate;
		} else {
			/* slave mode, return external sample rate */
			rate = hdspm_external_sample_rate(hdspm);
		}
	}

	return rate;
}


#define HDSPM_SYSTEM_SAMPLE_RATE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
		SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_system_sample_rate, \
	.put = snd_hdspm_put_system_sample_rate, \
	.get = snd_hdspm_get_system_sample_rate \
}

static int snd_hdspm_info_system_sample_rate(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 27000;
	uinfo->value.integer.max = 207000;
	uinfo->value.integer.step = 1;
	return 0;
}


static int snd_hdspm_get_system_sample_rate(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *
					    ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = hdspm_get_system_sample_rate(hdspm);
	return 0;
}

static int snd_hdspm_put_system_sample_rate(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *
					    ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	hdspm_set_dds_value(hdspm, ucontrol->value.enumerated.item[0]);
	return 0;
}


/*
 * Returns the WordClock sample rate class for the given card.
 */
static int hdspm_get_wc_sample_rate(struct hdspm *hdspm)
{
	int status;

	switch (hdspm->io_type) {
	case RayDAT:
	case AIO:
		status = hdspm_read(hdspm, HDSPM_RD_STATUS_1);
		return (status >> 16) & 0xF;
		break;
	case AES32:
		status = hdspm_read(hdspm, HDSPM_statusRegister);
		return (status >> HDSPM_AES32_wcFreq_bit) & 0xF;
	default:
		break;
	}


	return 0;
}


/*
 * Returns the TCO sample rate class for the given card.
 */
static int hdspm_get_tco_sample_rate(struct hdspm *hdspm)
{
	int status;

	if (hdspm->tco) {
		switch (hdspm->io_type) {
		case RayDAT:
		case AIO:
			status = hdspm_read(hdspm, HDSPM_RD_STATUS_1);
			return (status >> 20) & 0xF;
			break;
		case AES32:
			status = hdspm_read(hdspm, HDSPM_statusRegister);
			return (status >> 1) & 0xF;
		default:
			break;
		}
	}

	return 0;
}


/*
 * Returns the SYNC_IN sample rate class for the given card.
 */
static int hdspm_get_sync_in_sample_rate(struct hdspm *hdspm)
{
	int status;

	if (hdspm->tco) {
		switch (hdspm->io_type) {
		case RayDAT:
		case AIO:
			status = hdspm_read(hdspm, HDSPM_RD_STATUS_2);
			return (status >> 12) & 0xF;
			break;
		default:
			break;
		}
	}

	return 0;
}

/*
 * Returns the AES sample rate class for the given card.
 */
static int hdspm_get_aes_sample_rate(struct hdspm *hdspm, int index)
{
	int timecode;

	switch (hdspm->io_type) {
	case AES32:
		timecode = hdspm_read(hdspm, HDSPM_timecodeRegister);
		return (timecode >> (4*index)) & 0xF;
		break;
	default:
		break;
	}
	return 0;
}

/*
 * Returns the sample rate class for input source <idx> for
 * 'new style' cards like the AIO and RayDAT.
 */
static int hdspm_get_s1_sample_rate(struct hdspm *hdspm, unsigned int idx)
{
	int status = hdspm_read(hdspm, HDSPM_RD_STATUS_2);

	return (status >> (idx*4)) & 0xF;
}

#define ENUMERATED_CTL_INFO(info, texts) \
	snd_ctl_enum_info(info, 1, ARRAY_SIZE(texts), texts)


/* Helper function to query the external sample rate and return the
 * corresponding enum to be returned to userspace.
 */
static int hdspm_external_rate_to_enum(struct hdspm *hdspm)
{
	int rate = hdspm_external_sample_rate(hdspm);
	int i, selected_rate = 0;
	for (i = 1; i < 10; i++)
		if (HDSPM_bit2freq(i) == rate) {
			selected_rate = i;
			break;
		}
	return selected_rate;
}


#define HDSPM_AUTOSYNC_SAMPLE_RATE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.private_value = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READ, \
	.info = snd_hdspm_info_autosync_sample_rate, \
	.get = snd_hdspm_get_autosync_sample_rate \
}


static int snd_hdspm_info_autosync_sample_rate(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_info *uinfo)
{
	ENUMERATED_CTL_INFO(uinfo, texts_freq);
	return 0;
}


static int snd_hdspm_get_autosync_sample_rate(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value *
					      ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	switch (hdspm->io_type) {
	case RayDAT:
		switch (kcontrol->private_value) {
		case 0:
			ucontrol->value.enumerated.item[0] =
				hdspm_get_wc_sample_rate(hdspm);
			break;
		case 7:
			ucontrol->value.enumerated.item[0] =
				hdspm_get_tco_sample_rate(hdspm);
			break;
		case 8:
			ucontrol->value.enumerated.item[0] =
				hdspm_get_sync_in_sample_rate(hdspm);
			break;
		default:
			ucontrol->value.enumerated.item[0] =
				hdspm_get_s1_sample_rate(hdspm,
						kcontrol->private_value-1);
		}
		break;

	case AIO:
		switch (kcontrol->private_value) {
		case 0: /* WC */
			ucontrol->value.enumerated.item[0] =
				hdspm_get_wc_sample_rate(hdspm);
			break;
		case 4: /* TCO */
			ucontrol->value.enumerated.item[0] =
				hdspm_get_tco_sample_rate(hdspm);
			break;
		case 5: /* SYNC_IN */
			ucontrol->value.enumerated.item[0] =
				hdspm_get_sync_in_sample_rate(hdspm);
			break;
		default:
			ucontrol->value.enumerated.item[0] =
				hdspm_get_s1_sample_rate(hdspm,
						kcontrol->private_value-1);
		}
		break;

	case AES32:

		switch (kcontrol->private_value) {
		case 0: /* WC */
			ucontrol->value.enumerated.item[0] =
				hdspm_get_wc_sample_rate(hdspm);
			break;
		case 9: /* TCO */
			ucontrol->value.enumerated.item[0] =
				hdspm_get_tco_sample_rate(hdspm);
			break;
		case 10: /* SYNC_IN */
			ucontrol->value.enumerated.item[0] =
				hdspm_get_sync_in_sample_rate(hdspm);
			break;
		case 11: /* External Rate */
			ucontrol->value.enumerated.item[0] =
				hdspm_external_rate_to_enum(hdspm);
			break;
		default: /* AES1 to AES8 */
			ucontrol->value.enumerated.item[0] =
				hdspm_get_aes_sample_rate(hdspm,
						kcontrol->private_value -
						HDSPM_AES32_AUTOSYNC_FROM_AES1);
			break;
		}
		break;

	case MADI:
	case MADIface:
		ucontrol->value.enumerated.item[0] =
			hdspm_external_rate_to_enum(hdspm);
		break;
	default:
		break;
	}

	return 0;
}


#define HDSPM_SYSTEM_CLOCK_MODE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
		SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_system_clock_mode, \
	.get = snd_hdspm_get_system_clock_mode, \
	.put = snd_hdspm_put_system_clock_mode, \
}


/*
 * Returns the system clock mode for the given card.
 * @returns 0 - master, 1 - slave
 */
static int hdspm_system_clock_mode(struct hdspm *hdspm)
{
	switch (hdspm->io_type) {
	case AIO:
	case RayDAT:
		if (hdspm->settings_register & HDSPM_c0Master)
			return 0;
		break;

	default:
		if (hdspm->control_register & HDSPM_ClockModeMaster)
			return 0;
	}

	return 1;
}


/*
 * Sets the system clock mode.
 * @param mode 0 - master, 1 - slave
 */
static void hdspm_set_system_clock_mode(struct hdspm *hdspm, int mode)
{
	hdspm_set_toggle_setting(hdspm,
			(hdspm_is_raydat_or_aio(hdspm)) ?
			HDSPM_c0Master : HDSPM_ClockModeMaster,
			(0 == mode));
}


static int snd_hdspm_info_system_clock_mode(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "Master", "AutoSync" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_system_clock_mode(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm_system_clock_mode(hdspm);
	return 0;
}

static int snd_hdspm_put_system_clock_mode(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;

	val = ucontrol->value.enumerated.item[0];
	if (val < 0)
		val = 0;
	else if (val > 1)
		val = 1;

	hdspm_set_system_clock_mode(hdspm, val);

	return 0;
}


#define HDSPM_INTERNAL_CLOCK(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.info = snd_hdspm_info_clock_source, \
	.get = snd_hdspm_get_clock_source, \
	.put = snd_hdspm_put_clock_source \
}


static int hdspm_clock_source(struct hdspm * hdspm)
{
	switch (hdspm->system_sample_rate) {
	case 32000: return 0;
	case 44100: return 1;
	case 48000: return 2;
	case 64000: return 3;
	case 88200: return 4;
	case 96000: return 5;
	case 128000: return 6;
	case 176400: return 7;
	case 192000: return 8;
	}

	return -1;
}

static int hdspm_set_clock_source(struct hdspm * hdspm, int mode)
{
	int rate;
	switch (mode) {
	case 0:
		rate = 32000; break;
	case 1:
		rate = 44100; break;
	case 2:
		rate = 48000; break;
	case 3:
		rate = 64000; break;
	case 4:
		rate = 88200; break;
	case 5:
		rate = 96000; break;
	case 6:
		rate = 128000; break;
	case 7:
		rate = 176400; break;
	case 8:
		rate = 192000; break;
	default:
		rate = 48000;
	}
	hdspm_set_rate(hdspm, rate, 1);
	return 0;
}

static int snd_hdspm_info_clock_source(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	return snd_ctl_enum_info(uinfo, 1, 9, texts_freq + 1);
}

static int snd_hdspm_get_clock_source(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm_clock_source(hdspm);
	return 0;
}

static int snd_hdspm_put_clock_source(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0];
	if (val < 0)
		val = 0;
	if (val > 9)
		val = 9;
	spin_lock_irq(&hdspm->lock);
	if (val != hdspm_clock_source(hdspm))
		change = (hdspm_set_clock_source(hdspm, val) == 0) ? 1 : 0;
	else
		change = 0;
	spin_unlock_irq(&hdspm->lock);
	return change;
}


#define HDSPM_PREF_SYNC_REF(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
			SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_pref_sync_ref, \
	.get = snd_hdspm_get_pref_sync_ref, \
	.put = snd_hdspm_put_pref_sync_ref \
}


/*
 * Returns the current preferred sync reference setting.
 * The semantics of the return value are depending on the
 * card, please see the comments for clarification.
 */
static int hdspm_pref_sync_ref(struct hdspm * hdspm)
{
	switch (hdspm->io_type) {
	case AES32:
		switch (hdspm->control_register & HDSPM_SyncRefMask) {
		case 0: return 0;  /* WC */
		case HDSPM_SyncRef0: return 1; /* AES 1 */
		case HDSPM_SyncRef1: return 2; /* AES 2 */
		case HDSPM_SyncRef1+HDSPM_SyncRef0: return 3; /* AES 3 */
		case HDSPM_SyncRef2: return 4; /* AES 4 */
		case HDSPM_SyncRef2+HDSPM_SyncRef0: return 5; /* AES 5 */
		case HDSPM_SyncRef2+HDSPM_SyncRef1: return 6; /* AES 6 */
		case HDSPM_SyncRef2+HDSPM_SyncRef1+HDSPM_SyncRef0:
						    return 7; /* AES 7 */
		case HDSPM_SyncRef3: return 8; /* AES 8 */
		case HDSPM_SyncRef3+HDSPM_SyncRef0: return 9; /* TCO */
		}
		break;

	case MADI:
	case MADIface:
		if (hdspm->tco) {
			switch (hdspm->control_register & HDSPM_SyncRefMask) {
			case 0: return 0;  /* WC */
			case HDSPM_SyncRef0: return 1;  /* MADI */
			case HDSPM_SyncRef1: return 2;  /* TCO */
			case HDSPM_SyncRef1+HDSPM_SyncRef0:
					     return 3;  /* SYNC_IN */
			}
		} else {
			switch (hdspm->control_register & HDSPM_SyncRefMask) {
			case 0: return 0;  /* WC */
			case HDSPM_SyncRef0: return 1;  /* MADI */
			case HDSPM_SyncRef1+HDSPM_SyncRef0:
					     return 2;  /* SYNC_IN */
			}
		}
		break;

	case RayDAT:
		if (hdspm->tco) {
			switch ((hdspm->settings_register &
				HDSPM_c0_SyncRefMask) / HDSPM_c0_SyncRef0) {
			case 0: return 0;  /* WC */
			case 3: return 1;  /* ADAT 1 */
			case 4: return 2;  /* ADAT 2 */
			case 5: return 3;  /* ADAT 3 */
			case 6: return 4;  /* ADAT 4 */
			case 1: return 5;  /* AES */
			case 2: return 6;  /* SPDIF */
			case 9: return 7;  /* TCO */
			case 10: return 8; /* SYNC_IN */
			}
		} else {
			switch ((hdspm->settings_register &
				HDSPM_c0_SyncRefMask) / HDSPM_c0_SyncRef0) {
			case 0: return 0;  /* WC */
			case 3: return 1;  /* ADAT 1 */
			case 4: return 2;  /* ADAT 2 */
			case 5: return 3;  /* ADAT 3 */
			case 6: return 4;  /* ADAT 4 */
			case 1: return 5;  /* AES */
			case 2: return 6;  /* SPDIF */
			case 10: return 7; /* SYNC_IN */
			}
		}

		break;

	case AIO:
		if (hdspm->tco) {
			switch ((hdspm->settings_register &
				HDSPM_c0_SyncRefMask) / HDSPM_c0_SyncRef0) {
			case 0: return 0;  /* WC */
			case 3: return 1;  /* ADAT */
			case 1: return 2;  /* AES */
			case 2: return 3;  /* SPDIF */
			case 9: return 4;  /* TCO */
			case 10: return 5; /* SYNC_IN */
			}
		} else {
			switch ((hdspm->settings_register &
				HDSPM_c0_SyncRefMask) / HDSPM_c0_SyncRef0) {
			case 0: return 0;  /* WC */
			case 3: return 1;  /* ADAT */
			case 1: return 2;  /* AES */
			case 2: return 3;  /* SPDIF */
			case 10: return 4; /* SYNC_IN */
			}
		}

		break;
	}

	return -1;
}


/*
 * Set the preferred sync reference to <pref>. The semantics
 * of <pref> are depending on the card type, see the comments
 * for clarification.
 */
static int hdspm_set_pref_sync_ref(struct hdspm * hdspm, int pref)
{
	int p = 0;

	switch (hdspm->io_type) {
	case AES32:
		hdspm->control_register &= ~HDSPM_SyncRefMask;
		switch (pref) {
		case 0: /* WC  */
			break;
		case 1: /* AES 1 */
			hdspm->control_register |= HDSPM_SyncRef0;
			break;
		case 2: /* AES 2 */
			hdspm->control_register |= HDSPM_SyncRef1;
			break;
		case 3: /* AES 3 */
			hdspm->control_register |=
				HDSPM_SyncRef1+HDSPM_SyncRef0;
			break;
		case 4: /* AES 4 */
			hdspm->control_register |= HDSPM_SyncRef2;
			break;
		case 5: /* AES 5 */
			hdspm->control_register |=
				HDSPM_SyncRef2+HDSPM_SyncRef0;
			break;
		case 6: /* AES 6 */
			hdspm->control_register |=
				HDSPM_SyncRef2+HDSPM_SyncRef1;
			break;
		case 7: /* AES 7 */
			hdspm->control_register |=
				HDSPM_SyncRef2+HDSPM_SyncRef1+HDSPM_SyncRef0;
			break;
		case 8: /* AES 8 */
			hdspm->control_register |= HDSPM_SyncRef3;
			break;
		case 9: /* TCO */
			hdspm->control_register |=
				HDSPM_SyncRef3+HDSPM_SyncRef0;
			break;
		default:
			return -1;
		}

		break;

	case MADI:
	case MADIface:
		hdspm->control_register &= ~HDSPM_SyncRefMask;
		if (hdspm->tco) {
			switch (pref) {
			case 0: /* WC */
				break;
			case 1: /* MADI */
				hdspm->control_register |= HDSPM_SyncRef0;
				break;
			case 2: /* TCO */
				hdspm->control_register |= HDSPM_SyncRef1;
				break;
			case 3: /* SYNC_IN */
				hdspm->control_register |=
					HDSPM_SyncRef0+HDSPM_SyncRef1;
				break;
			default:
				return -1;
			}
		} else {
			switch (pref) {
			case 0: /* WC */
				break;
			case 1: /* MADI */
				hdspm->control_register |= HDSPM_SyncRef0;
				break;
			case 2: /* SYNC_IN */
				hdspm->control_register |=
					HDSPM_SyncRef0+HDSPM_SyncRef1;
				break;
			default:
				return -1;
			}
		}

		break;

	case RayDAT:
		if (hdspm->tco) {
			switch (pref) {
			case 0: p = 0; break;  /* WC */
			case 1: p = 3; break;  /* ADAT 1 */
			case 2: p = 4; break;  /* ADAT 2 */
			case 3: p = 5; break;  /* ADAT 3 */
			case 4: p = 6; break;  /* ADAT 4 */
			case 5: p = 1; break;  /* AES */
			case 6: p = 2; break;  /* SPDIF */
			case 7: p = 9; break;  /* TCO */
			case 8: p = 10; break; /* SYNC_IN */
			default: return -1;
			}
		} else {
			switch (pref) {
			case 0: p = 0; break;  /* WC */
			case 1: p = 3; break;  /* ADAT 1 */
			case 2: p = 4; break;  /* ADAT 2 */
			case 3: p = 5; break;  /* ADAT 3 */
			case 4: p = 6; break;  /* ADAT 4 */
			case 5: p = 1; break;  /* AES */
			case 6: p = 2; break;  /* SPDIF */
			case 7: p = 10; break; /* SYNC_IN */
			default: return -1;
			}
		}
		break;

	case AIO:
		if (hdspm->tco) {
			switch (pref) {
			case 0: p = 0; break;  /* WC */
			case 1: p = 3; break;  /* ADAT */
			case 2: p = 1; break;  /* AES */
			case 3: p = 2; break;  /* SPDIF */
			case 4: p = 9; break;  /* TCO */
			case 5: p = 10; break; /* SYNC_IN */
			default: return -1;
			}
		} else {
			switch (pref) {
			case 0: p = 0; break;  /* WC */
			case 1: p = 3; break;  /* ADAT */
			case 2: p = 1; break;  /* AES */
			case 3: p = 2; break;  /* SPDIF */
			case 4: p = 10; break; /* SYNC_IN */
			default: return -1;
			}
		}
		break;
	}

	switch (hdspm->io_type) {
	case RayDAT:
	case AIO:
		hdspm->settings_register &= ~HDSPM_c0_SyncRefMask;
		hdspm->settings_register |= HDSPM_c0_SyncRef0 * p;
		hdspm_write(hdspm, HDSPM_WR_SETTINGS, hdspm->settings_register);
		break;

	case MADI:
	case MADIface:
	case AES32:
		hdspm_write(hdspm, HDSPM_controlRegister,
				hdspm->control_register);
	}

	return 0;
}


static int snd_hdspm_info_pref_sync_ref(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	snd_ctl_enum_info(uinfo, 1, hdspm->texts_autosync_items, hdspm->texts_autosync);

	return 0;
}

static int snd_hdspm_get_pref_sync_ref(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int psf = hdspm_pref_sync_ref(hdspm);

	if (psf >= 0) {
		ucontrol->value.enumerated.item[0] = psf;
		return 0;
	}

	return -1;
}

static int snd_hdspm_put_pref_sync_ref(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int val, change = 0;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;

	val = ucontrol->value.enumerated.item[0];

	if (val < 0)
		val = 0;
	else if (val >= hdspm->texts_autosync_items)
		val = hdspm->texts_autosync_items-1;

	spin_lock_irq(&hdspm->lock);
	if (val != hdspm_pref_sync_ref(hdspm))
		change = (0 == hdspm_set_pref_sync_ref(hdspm, val)) ? 1 : 0;

	spin_unlock_irq(&hdspm->lock);
	return change;
}


#define HDSPM_AUTOSYNC_REF(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READ, \
	.info = snd_hdspm_info_autosync_ref, \
	.get = snd_hdspm_get_autosync_ref, \
}

static int hdspm_autosync_ref(struct hdspm *hdspm)
{
	/* This looks at the autosync selected sync reference */
	if (AES32 == hdspm->io_type) {

		unsigned int status = hdspm_read(hdspm, HDSPM_statusRegister);
		unsigned int syncref = (status >> HDSPM_AES32_syncref_bit) & 0xF;
		if ((syncref >= HDSPM_AES32_AUTOSYNC_FROM_WORD) &&
				(syncref <= HDSPM_AES32_AUTOSYNC_FROM_SYNC_IN)) {
			return syncref;
		}
		return HDSPM_AES32_AUTOSYNC_FROM_NONE;

	} else if (MADI == hdspm->io_type) {

		unsigned int status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
		switch (status2 & HDSPM_SelSyncRefMask) {
		case HDSPM_SelSyncRef_WORD:
			return HDSPM_AUTOSYNC_FROM_WORD;
		case HDSPM_SelSyncRef_MADI:
			return HDSPM_AUTOSYNC_FROM_MADI;
		case HDSPM_SelSyncRef_TCO:
			return HDSPM_AUTOSYNC_FROM_TCO;
		case HDSPM_SelSyncRef_SyncIn:
			return HDSPM_AUTOSYNC_FROM_SYNC_IN;
		case HDSPM_SelSyncRef_NVALID:
			return HDSPM_AUTOSYNC_FROM_NONE;
		default:
			return HDSPM_AUTOSYNC_FROM_NONE;
		}

	}
	return 0;
}


static int snd_hdspm_info_autosync_ref(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (AES32 == hdspm->io_type) {
		static const char *const texts[] = { "WordClock", "AES1", "AES2", "AES3",
			"AES4",	"AES5", "AES6", "AES7", "AES8", "TCO", "Sync In", "None"};

		ENUMERATED_CTL_INFO(uinfo, texts);
	} else if (MADI == hdspm->io_type) {
		static const char *const texts[] = {"Word Clock", "MADI", "TCO",
			"Sync In", "None" };

		ENUMERATED_CTL_INFO(uinfo, texts);
	}
	return 0;
}

static int snd_hdspm_get_autosync_ref(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm_autosync_ref(hdspm);
	return 0;
}



#define HDSPM_TCO_VIDEO_INPUT_FORMAT(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_READ |\
		SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_tco_video_input_format, \
	.get = snd_hdspm_get_tco_video_input_format, \
}

static int snd_hdspm_info_tco_video_input_format(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = {"No video", "NTSC", "PAL"};
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_tco_video_input_format(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	u32 status;
	int ret = 0;

	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	status = hdspm_read(hdspm, HDSPM_RD_TCO + 4);
	switch (status & (HDSPM_TCO1_Video_Input_Format_NTSC |
			HDSPM_TCO1_Video_Input_Format_PAL)) {
	case HDSPM_TCO1_Video_Input_Format_NTSC:
		/* ntsc */
		ret = 1;
		break;
	case HDSPM_TCO1_Video_Input_Format_PAL:
		/* pal */
		ret = 2;
		break;
	default:
		/* no video */
		ret = 0;
		break;
	}
	ucontrol->value.enumerated.item[0] = ret;
	return 0;
}



#define HDSPM_TCO_LTC_FRAMES(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_READ |\
		SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_tco_ltc_frames, \
	.get = snd_hdspm_get_tco_ltc_frames, \
}

static int snd_hdspm_info_tco_ltc_frames(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = {"No lock", "24 fps", "25 fps", "29.97 fps",
				"30 fps"};
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int hdspm_tco_ltc_frames(struct hdspm *hdspm)
{
	u32 status;
	int ret = 0;

	status = hdspm_read(hdspm, HDSPM_RD_TCO + 4);
	if (status & HDSPM_TCO1_LTC_Input_valid) {
		switch (status & (HDSPM_TCO1_LTC_Format_LSB |
					HDSPM_TCO1_LTC_Format_MSB)) {
		case 0:
			/* 24 fps */
			ret = fps_24;
			break;
		case HDSPM_TCO1_LTC_Format_LSB:
			/* 25 fps */
			ret = fps_25;
			break;
		case HDSPM_TCO1_LTC_Format_MSB:
			/* 29.97 fps */
			ret = fps_2997;
			break;
		default:
			/* 30 fps */
			ret = fps_30;
			break;
		}
	}

	return ret;
}

static int snd_hdspm_get_tco_ltc_frames(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm_tco_ltc_frames(hdspm);
	return 0;
}

#define HDSPM_TOGGLE_SETTING(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.private_value = xindex, \
	.info = snd_hdspm_info_toggle_setting, \
	.get = snd_hdspm_get_toggle_setting, \
	.put = snd_hdspm_put_toggle_setting \
}

static int hdspm_toggle_setting(struct hdspm *hdspm, u32 regmask)
{
	u32 reg;

	if (hdspm_is_raydat_or_aio(hdspm))
		reg = hdspm->settings_register;
	else
		reg = hdspm->control_register;

	return (reg & regmask) ? 1 : 0;
}

static int hdspm_set_toggle_setting(struct hdspm *hdspm, u32 regmask, int out)
{
	u32 *reg;
	u32 target_reg;

	if (hdspm_is_raydat_or_aio(hdspm)) {
		reg = &(hdspm->settings_register);
		target_reg = HDSPM_WR_SETTINGS;
	} else {
		reg = &(hdspm->control_register);
		target_reg = HDSPM_controlRegister;
	}

	if (out)
		*reg |= regmask;
	else
		*reg &= ~regmask;

	hdspm_write(hdspm, target_reg, *reg);

	return 0;
}

#define snd_hdspm_info_toggle_setting		snd_ctl_boolean_mono_info

static int snd_hdspm_get_toggle_setting(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	u32 regmask = kcontrol->private_value;

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.integer.value[0] = hdspm_toggle_setting(hdspm, regmask);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_toggle_setting(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	u32 regmask = kcontrol->private_value;
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_toggle_setting(hdspm, regmask);
	hdspm_set_toggle_setting(hdspm, regmask, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_INPUT_SELECT(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.info = snd_hdspm_info_input_select, \
	.get = snd_hdspm_get_input_select, \
	.put = snd_hdspm_put_input_select \
}

static int hdspm_input_select(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_InputSelect0) ? 1 : 0;
}

static int hdspm_set_input_select(struct hdspm * hdspm, int out)
{
	if (out)
		hdspm->control_register |= HDSPM_InputSelect0;
	else
		hdspm->control_register &= ~HDSPM_InputSelect0;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

static int snd_hdspm_info_input_select(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "optical", "coaxial" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_input_select(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.enumerated.item[0] = hdspm_input_select(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_input_select(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_input_select(hdspm);
	hdspm_set_input_select(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}


#define HDSPM_DS_WIRE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.info = snd_hdspm_info_ds_wire, \
	.get = snd_hdspm_get_ds_wire, \
	.put = snd_hdspm_put_ds_wire \
}

static int hdspm_ds_wire(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_DS_DoubleWire) ? 1 : 0;
}

static int hdspm_set_ds_wire(struct hdspm * hdspm, int ds)
{
	if (ds)
		hdspm->control_register |= HDSPM_DS_DoubleWire;
	else
		hdspm->control_register &= ~HDSPM_DS_DoubleWire;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

static int snd_hdspm_info_ds_wire(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "Single", "Double" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_ds_wire(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.enumerated.item[0] = hdspm_ds_wire(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_ds_wire(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_ds_wire(hdspm);
	hdspm_set_ds_wire(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}


#define HDSPM_QS_WIRE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.info = snd_hdspm_info_qs_wire, \
	.get = snd_hdspm_get_qs_wire, \
	.put = snd_hdspm_put_qs_wire \
}

static int hdspm_qs_wire(struct hdspm * hdspm)
{
	if (hdspm->control_register & HDSPM_QS_DoubleWire)
		return 1;
	if (hdspm->control_register & HDSPM_QS_QuadWire)
		return 2;
	return 0;
}

static int hdspm_set_qs_wire(struct hdspm * hdspm, int mode)
{
	hdspm->control_register &= ~(HDSPM_QS_DoubleWire | HDSPM_QS_QuadWire);
	switch (mode) {
	case 0:
		break;
	case 1:
		hdspm->control_register |= HDSPM_QS_DoubleWire;
		break;
	case 2:
		hdspm->control_register |= HDSPM_QS_QuadWire;
		break;
	}
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

static int snd_hdspm_info_qs_wire(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "Single", "Double", "Quad" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_qs_wire(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.enumerated.item[0] = hdspm_qs_wire(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_qs_wire(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0];
	if (val < 0)
		val = 0;
	if (val > 2)
		val = 2;
	spin_lock_irq(&hdspm->lock);
	change = val != hdspm_qs_wire(hdspm);
	hdspm_set_qs_wire(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_CONTROL_TRISTATE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.private_value = xindex, \
	.info = snd_hdspm_info_tristate, \
	.get = snd_hdspm_get_tristate, \
	.put = snd_hdspm_put_tristate \
}

static int hdspm_tristate(struct hdspm *hdspm, u32 regmask)
{
	u32 reg = hdspm->settings_register & (regmask * 3);
	return reg / regmask;
}

static int hdspm_set_tristate(struct hdspm *hdspm, int mode, u32 regmask)
{
	hdspm->settings_register &= ~(regmask * 3);
	hdspm->settings_register |= (regmask * mode);
	hdspm_write(hdspm, HDSPM_WR_SETTINGS, hdspm->settings_register);

	return 0;
}

static int snd_hdspm_info_tristate(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	u32 regmask = kcontrol->private_value;

	static const char *const texts_spdif[] = { "Optical", "Coaxial", "Internal" };
	static const char *const texts_levels[] = { "Hi Gain", "+4 dBu", "-10 dBV" };

	switch (regmask) {
	case HDSPM_c0_Input0:
		ENUMERATED_CTL_INFO(uinfo, texts_spdif);
		break;
	default:
		ENUMERATED_CTL_INFO(uinfo, texts_levels);
		break;
	}
	return 0;
}

static int snd_hdspm_get_tristate(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	u32 regmask = kcontrol->private_value;

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.enumerated.item[0] = hdspm_tristate(hdspm, regmask);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_tristate(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	u32 regmask = kcontrol->private_value;
	int change;
	int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0];
	if (val < 0)
		val = 0;
	if (val > 2)
		val = 2;

	spin_lock_irq(&hdspm->lock);
	change = val != hdspm_tristate(hdspm, regmask);
	hdspm_set_tristate(hdspm, val, regmask);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_MADI_SPEEDMODE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.info = snd_hdspm_info_madi_speedmode, \
	.get = snd_hdspm_get_madi_speedmode, \
	.put = snd_hdspm_put_madi_speedmode \
}

static int hdspm_madi_speedmode(struct hdspm *hdspm)
{
	if (hdspm->control_register & HDSPM_QuadSpeed)
		return 2;
	if (hdspm->control_register & HDSPM_DoubleSpeed)
		return 1;
	return 0;
}

static int hdspm_set_madi_speedmode(struct hdspm *hdspm, int mode)
{
	hdspm->control_register &= ~(HDSPM_DoubleSpeed | HDSPM_QuadSpeed);
	switch (mode) {
	case 0:
		break;
	case 1:
		hdspm->control_register |= HDSPM_DoubleSpeed;
		break;
	case 2:
		hdspm->control_register |= HDSPM_QuadSpeed;
		break;
	}
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

static int snd_hdspm_info_madi_speedmode(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "Single", "Double", "Quad" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_madi_speedmode(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.enumerated.item[0] = hdspm_madi_speedmode(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_madi_speedmode(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0];
	if (val < 0)
		val = 0;
	if (val > 2)
		val = 2;
	spin_lock_irq(&hdspm->lock);
	change = val != hdspm_madi_speedmode(hdspm);
	hdspm_set_madi_speedmode(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_MIXER(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
	.name = xname, \
	.index = xindex, \
	.device = 0, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
		SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_mixer, \
	.get = snd_hdspm_get_mixer, \
	.put = snd_hdspm_put_mixer \
}

static int snd_hdspm_info_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 65535;
	uinfo->value.integer.step = 1;
	return 0;
}

static int snd_hdspm_get_mixer(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int source;
	int destination;

	source = ucontrol->value.integer.value[0];
	if (source < 0)
		source = 0;
	else if (source >= 2 * HDSPM_MAX_CHANNELS)
		source = 2 * HDSPM_MAX_CHANNELS - 1;

	destination = ucontrol->value.integer.value[1];
	if (destination < 0)
		destination = 0;
	else if (destination >= HDSPM_MAX_CHANNELS)
		destination = HDSPM_MAX_CHANNELS - 1;

	spin_lock_irq(&hdspm->lock);
	if (source >= HDSPM_MAX_CHANNELS)
		ucontrol->value.integer.value[2] =
		    hdspm_read_pb_gain(hdspm, destination,
				       source - HDSPM_MAX_CHANNELS);
	else
		ucontrol->value.integer.value[2] =
		    hdspm_read_in_gain(hdspm, destination, source);

	spin_unlock_irq(&hdspm->lock);

	return 0;
}

static int snd_hdspm_put_mixer(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	int source;
	int destination;
	int gain;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;

	source = ucontrol->value.integer.value[0];
	destination = ucontrol->value.integer.value[1];

	if (source < 0 || source >= 2 * HDSPM_MAX_CHANNELS)
		return -1;
	if (destination < 0 || destination >= HDSPM_MAX_CHANNELS)
		return -1;

	gain = ucontrol->value.integer.value[2];

	spin_lock_irq(&hdspm->lock);

	if (source >= HDSPM_MAX_CHANNELS)
		change = gain != hdspm_read_pb_gain(hdspm, destination,
						    source -
						    HDSPM_MAX_CHANNELS);
	else
		change = gain != hdspm_read_in_gain(hdspm, destination,
						    source);

	if (change) {
		if (source >= HDSPM_MAX_CHANNELS)
			hdspm_write_pb_gain(hdspm, destination,
					    source - HDSPM_MAX_CHANNELS,
					    gain);
		else
			hdspm_write_in_gain(hdspm, destination, source,
					    gain);
	}
	spin_unlock_irq(&hdspm->lock);

	return change;
}

/* The simple mixer control(s) provide gain control for the
   basic 1:1 mappings of playback streams to output
   streams.
*/

#define HDSPM_PLAYBACK_MIXER \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE | \
		SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_playback_mixer, \
	.get = snd_hdspm_get_playback_mixer, \
	.put = snd_hdspm_put_playback_mixer \
}

static int snd_hdspm_info_playback_mixer(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 64;
	uinfo->value.integer.step = 1;
	return 0;
}

static int snd_hdspm_get_playback_mixer(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int channel;

	channel = ucontrol->id.index - 1;

	if (snd_BUG_ON(channel < 0 || channel >= HDSPM_MAX_CHANNELS))
		return -EINVAL;

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.integer.value[0] =
	  (hdspm_read_pb_gain(hdspm, channel, channel)*64)/UNITY_GAIN;
	spin_unlock_irq(&hdspm->lock);

	return 0;
}

static int snd_hdspm_put_playback_mixer(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	int channel;
	int gain;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;

	channel = ucontrol->id.index - 1;

	if (snd_BUG_ON(channel < 0 || channel >= HDSPM_MAX_CHANNELS))
		return -EINVAL;

	gain = ucontrol->value.integer.value[0]*UNITY_GAIN/64;

	spin_lock_irq(&hdspm->lock);
	change =
	    gain != hdspm_read_pb_gain(hdspm, channel,
				       channel);
	if (change)
		hdspm_write_pb_gain(hdspm, channel, channel,
				    gain);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_SYNC_CHECK(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.private_value = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_sync_check, \
	.get = snd_hdspm_get_sync_check \
}

#define HDSPM_TCO_LOCK_CHECK(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.private_value = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_tco_info_lock_check, \
	.get = snd_hdspm_get_sync_check \
}



static int snd_hdspm_info_sync_check(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "No Lock", "Lock", "Sync", "N/A" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_tco_info_lock_check(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "No Lock", "Lock" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int hdspm_wc_sync_check(struct hdspm *hdspm)
{
	int status, status2;

	switch (hdspm->io_type) {
	case AES32:
		status = hdspm_read(hdspm, HDSPM_statusRegister);
		if (status & HDSPM_AES32_wcLock) {
			if (status & HDSPM_AES32_wcSync)
				return 2;
			else
				return 1;
		}
		return 0;
		break;

	case MADI:
		status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
		if (status2 & HDSPM_wcLock) {
			if (status2 & HDSPM_wcSync)
				return 2;
			else
				return 1;
		}
		return 0;
		break;

	case RayDAT:
	case AIO:
		status = hdspm_read(hdspm, HDSPM_statusRegister);

		if (status & 0x2000000)
			return 2;
		else if (status & 0x1000000)
			return 1;
		return 0;

		break;

	case MADIface:
		break;
	}


	return 3;
}


static int hdspm_madi_sync_check(struct hdspm *hdspm)
{
	int status = hdspm_read(hdspm, HDSPM_statusRegister);
	if (status & HDSPM_madiLock) {
		if (status & HDSPM_madiSync)
			return 2;
		else
			return 1;
	}
	return 0;
}


static int hdspm_s1_sync_check(struct hdspm *hdspm, int idx)
{
	int status, lock, sync;

	status = hdspm_read(hdspm, HDSPM_RD_STATUS_1);

	lock = (status & (0x1<<idx)) ? 1 : 0;
	sync = (status & (0x100<<idx)) ? 1 : 0;

	if (lock && sync)
		return 2;
	else if (lock)
		return 1;
	return 0;
}


static int hdspm_sync_in_sync_check(struct hdspm *hdspm)
{
	int status, lock = 0, sync = 0;

	switch (hdspm->io_type) {
	case RayDAT:
	case AIO:
		status = hdspm_read(hdspm, HDSPM_RD_STATUS_3);
		lock = (status & 0x400) ? 1 : 0;
		sync = (status & 0x800) ? 1 : 0;
		break;

	case MADI:
		status = hdspm_read(hdspm, HDSPM_statusRegister);
		lock = (status & HDSPM_syncInLock) ? 1 : 0;
		sync = (status & HDSPM_syncInSync) ? 1 : 0;
		break;

	case AES32:
		status = hdspm_read(hdspm, HDSPM_statusRegister2);
		lock = (status & 0x100000) ? 1 : 0;
		sync = (status & 0x200000) ? 1 : 0;
		break;

	case MADIface:
		break;
	}

	if (lock && sync)
		return 2;
	else if (lock)
		return 1;

	return 0;
}

static int hdspm_aes_sync_check(struct hdspm *hdspm, int idx)
{
	int status2, lock, sync;
	status2 = hdspm_read(hdspm, HDSPM_statusRegister2);

	lock = (status2 & (0x0080 >> idx)) ? 1 : 0;
	sync = (status2 & (0x8000 >> idx)) ? 1 : 0;

	if (sync)
		return 2;
	else if (lock)
		return 1;
	return 0;
}

static int hdspm_tco_input_check(struct hdspm *hdspm, u32 mask)
{
	u32 status;
	status = hdspm_read(hdspm, HDSPM_RD_TCO + 4);

	return (status & mask) ? 1 : 0;
}


static int hdspm_tco_sync_check(struct hdspm *hdspm)
{
	int status;

	if (hdspm->tco) {
		switch (hdspm->io_type) {
		case MADI:
			status = hdspm_read(hdspm, HDSPM_statusRegister);
			if (status & HDSPM_tcoLockMadi) {
				if (status & HDSPM_tcoSync)
					return 2;
				else
					return 1;
			}
			return 0;
		case AES32:
			status = hdspm_read(hdspm, HDSPM_statusRegister);
			if (status & HDSPM_tcoLockAes) {
				if (status & HDSPM_tcoSync)
					return 2;
				else
					return 1;
			}
			return 0;
		case RayDAT:
		case AIO:
			status = hdspm_read(hdspm, HDSPM_RD_STATUS_1);

			if (status & 0x8000000)
				return 2; /* Sync */
			if (status & 0x4000000)
				return 1; /* Lock */
			return 0; /* No signal */

		default:
			break;
		}
	}

	return 3; /* N/A */
}


static int snd_hdspm_get_sync_check(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int val = -1;

	switch (hdspm->io_type) {
	case RayDAT:
		switch (kcontrol->private_value) {
		case 0: /* WC */
			val = hdspm_wc_sync_check(hdspm); break;
		case 7: /* TCO */
			val = hdspm_tco_sync_check(hdspm); break;
		case 8: /* SYNC IN */
			val = hdspm_sync_in_sync_check(hdspm); break;
		default:
			val = hdspm_s1_sync_check(hdspm,
					kcontrol->private_value-1);
		}
		break;

	case AIO:
		switch (kcontrol->private_value) {
		case 0: /* WC */
			val = hdspm_wc_sync_check(hdspm); break;
		case 4: /* TCO */
			val = hdspm_tco_sync_check(hdspm); break;
		case 5: /* SYNC IN */
			val = hdspm_sync_in_sync_check(hdspm); break;
		default:
			val = hdspm_s1_sync_check(hdspm,
					kcontrol->private_value-1);
		}
		break;

	case MADI:
		switch (kcontrol->private_value) {
		case 0: /* WC */
			val = hdspm_wc_sync_check(hdspm); break;
		case 1: /* MADI */
			val = hdspm_madi_sync_check(hdspm); break;
		case 2: /* TCO */
			val = hdspm_tco_sync_check(hdspm); break;
		case 3: /* SYNC_IN */
			val = hdspm_sync_in_sync_check(hdspm); break;
		}
		break;

	case MADIface:
		val = hdspm_madi_sync_check(hdspm); /* MADI */
		break;

	case AES32:
		switch (kcontrol->private_value) {
		case 0: /* WC */
			val = hdspm_wc_sync_check(hdspm); break;
		case 9: /* TCO */
			val = hdspm_tco_sync_check(hdspm); break;
		case 10 /* SYNC IN */:
			val = hdspm_sync_in_sync_check(hdspm); break;
		default: /* AES1 to AES8 */
			 val = hdspm_aes_sync_check(hdspm,
					 kcontrol->private_value-1);
		}
		break;

	}

	if (hdspm->tco) {
		switch (kcontrol->private_value) {
		case 11:
			/* Check TCO for lock state of its current input */
			val = hdspm_tco_input_check(hdspm, HDSPM_TCO1_TCO_lock);
			break;
		case 12:
			/* Check TCO for valid time code on LTC input. */
			val = hdspm_tco_input_check(hdspm,
				HDSPM_TCO1_LTC_Input_valid);
			break;
		default:
			break;
		}
	}

	if (-1 == val)
		val = 3;

	ucontrol->value.enumerated.item[0] = val;
	return 0;
}



/*
 * TCO controls
 */
static void hdspm_tco_write(struct hdspm *hdspm)
{
	unsigned int tc[4] = { 0, 0, 0, 0};

	switch (hdspm->tco->input) {
	case 0:
		tc[2] |= HDSPM_TCO2_set_input_MSB;
		break;
	case 1:
		tc[2] |= HDSPM_TCO2_set_input_LSB;
		break;
	default:
		break;
	}

	switch (hdspm->tco->framerate) {
	case 1:
		tc[1] |= HDSPM_TCO1_LTC_Format_LSB;
		break;
	case 2:
		tc[1] |= HDSPM_TCO1_LTC_Format_MSB;
		break;
	case 3:
		tc[1] |= HDSPM_TCO1_LTC_Format_MSB +
			HDSPM_TCO1_set_drop_frame_flag;
		break;
	case 4:
		tc[1] |= HDSPM_TCO1_LTC_Format_LSB +
			HDSPM_TCO1_LTC_Format_MSB;
		break;
	case 5:
		tc[1] |= HDSPM_TCO1_LTC_Format_LSB +
			HDSPM_TCO1_LTC_Format_MSB +
			HDSPM_TCO1_set_drop_frame_flag;
		break;
	default:
		break;
	}

	switch (hdspm->tco->wordclock) {
	case 1:
		tc[2] |= HDSPM_TCO2_WCK_IO_ratio_LSB;
		break;
	case 2:
		tc[2] |= HDSPM_TCO2_WCK_IO_ratio_MSB;
		break;
	default:
		break;
	}

	switch (hdspm->tco->samplerate) {
	case 1:
		tc[2] |= HDSPM_TCO2_set_freq;
		break;
	case 2:
		tc[2] |= HDSPM_TCO2_set_freq_from_app;
		break;
	default:
		break;
	}

	switch (hdspm->tco->pull) {
	case 1:
		tc[2] |= HDSPM_TCO2_set_pull_up;
		break;
	case 2:
		tc[2] |= HDSPM_TCO2_set_pull_down;
		break;
	case 3:
		tc[2] |= HDSPM_TCO2_set_pull_up + HDSPM_TCO2_set_01_4;
		break;
	case 4:
		tc[2] |= HDSPM_TCO2_set_pull_down + HDSPM_TCO2_set_01_4;
		break;
	default:
		break;
	}

	if (1 == hdspm->tco->term) {
		tc[2] |= HDSPM_TCO2_set_term_75R;
	}

	hdspm_write(hdspm, HDSPM_WR_TCO, tc[0]);
	hdspm_write(hdspm, HDSPM_WR_TCO+4, tc[1]);
	hdspm_write(hdspm, HDSPM_WR_TCO+8, tc[2]);
	hdspm_write(hdspm, HDSPM_WR_TCO+12, tc[3]);
}


#define HDSPM_TCO_SAMPLE_RATE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
		SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_tco_sample_rate, \
	.get = snd_hdspm_get_tco_sample_rate, \
	.put = snd_hdspm_put_tco_sample_rate \
}

static int snd_hdspm_info_tco_sample_rate(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	/* TODO freq from app could be supported here, see tco->samplerate */
	static const char *const texts[] = { "44.1 kHz", "48 kHz" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_tco_sample_rate(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm->tco->samplerate;

	return 0;
}

static int snd_hdspm_put_tco_sample_rate(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (hdspm->tco->samplerate != ucontrol->value.enumerated.item[0]) {
		hdspm->tco->samplerate = ucontrol->value.enumerated.item[0];

		hdspm_tco_write(hdspm);

		return 1;
	}

	return 0;
}


#define HDSPM_TCO_PULL(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
		SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_tco_pull, \
	.get = snd_hdspm_get_tco_pull, \
	.put = snd_hdspm_put_tco_pull \
}

static int snd_hdspm_info_tco_pull(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "0", "+ 0.1 %", "- 0.1 %",
		"+ 4 %", "- 4 %" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_tco_pull(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm->tco->pull;

	return 0;
}

static int snd_hdspm_put_tco_pull(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (hdspm->tco->pull != ucontrol->value.enumerated.item[0]) {
		hdspm->tco->pull = ucontrol->value.enumerated.item[0];

		hdspm_tco_write(hdspm);

		return 1;
	}

	return 0;
}

#define HDSPM_TCO_WCK_CONVERSION(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
			SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_tco_wck_conversion, \
	.get = snd_hdspm_get_tco_wck_conversion, \
	.put = snd_hdspm_put_tco_wck_conversion \
}

static int snd_hdspm_info_tco_wck_conversion(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "1:1", "44.1 -> 48", "48 -> 44.1" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_tco_wck_conversion(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm->tco->wordclock;

	return 0;
}

static int snd_hdspm_put_tco_wck_conversion(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (hdspm->tco->wordclock != ucontrol->value.enumerated.item[0]) {
		hdspm->tco->wordclock = ucontrol->value.enumerated.item[0];

		hdspm_tco_write(hdspm);

		return 1;
	}

	return 0;
}


#define HDSPM_TCO_FRAME_RATE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
			SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_tco_frame_rate, \
	.get = snd_hdspm_get_tco_frame_rate, \
	.put = snd_hdspm_put_tco_frame_rate \
}

static int snd_hdspm_info_tco_frame_rate(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "24 fps", "25 fps", "29.97fps",
		"29.97 dfps", "30 fps", "30 dfps" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_tco_frame_rate(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm->tco->framerate;

	return 0;
}

static int snd_hdspm_put_tco_frame_rate(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (hdspm->tco->framerate != ucontrol->value.enumerated.item[0]) {
		hdspm->tco->framerate = ucontrol->value.enumerated.item[0];

		hdspm_tco_write(hdspm);

		return 1;
	}

	return 0;
}


#define HDSPM_TCO_SYNC_SOURCE(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
			SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_tco_sync_source, \
	.get = snd_hdspm_get_tco_sync_source, \
	.put = snd_hdspm_put_tco_sync_source \
}

static int snd_hdspm_info_tco_sync_source(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[] = { "LTC", "Video", "WCK" };
	ENUMERATED_CTL_INFO(uinfo, texts);
	return 0;
}

static int snd_hdspm_get_tco_sync_source(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm->tco->input;

	return 0;
}

static int snd_hdspm_put_tco_sync_source(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (hdspm->tco->input != ucontrol->value.enumerated.item[0]) {
		hdspm->tco->input = ucontrol->value.enumerated.item[0];

		hdspm_tco_write(hdspm);

		return 1;
	}

	return 0;
}


#define HDSPM_TCO_WORD_TERM(xname, xindex) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |\
			SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = snd_hdspm_info_tco_word_term, \
	.get = snd_hdspm_get_tco_word_term, \
	.put = snd_hdspm_put_tco_word_term \
}

static int snd_hdspm_info_tco_word_term(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}


static int snd_hdspm_get_tco_word_term(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm->tco->term;

	return 0;
}


static int snd_hdspm_put_tco_word_term(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (hdspm->tco->term != ucontrol->value.enumerated.item[0]) {
		hdspm->tco->term = ucontrol->value.enumerated.item[0];

		hdspm_tco_write(hdspm);

		return 1;
	}

	return 0;
}




static struct snd_kcontrol_new snd_hdspm_controls_madi[] = {
	HDSPM_MIXER("Mixer", 0),
	HDSPM_INTERNAL_CLOCK("Internal Clock", 0),
	HDSPM_SYSTEM_CLOCK_MODE("System Clock Mode", 0),
	HDSPM_PREF_SYNC_REF("Preferred Sync Reference", 0),
	HDSPM_AUTOSYNC_REF("AutoSync Reference", 0),
	HDSPM_SYSTEM_SAMPLE_RATE("System Sample Rate", 0),
	HDSPM_AUTOSYNC_SAMPLE_RATE("External Rate", 0),
	HDSPM_SYNC_CHECK("WC SyncCheck", 0),
	HDSPM_SYNC_CHECK("MADI SyncCheck", 1),
	HDSPM_SYNC_CHECK("TCO SyncCheck", 2),
	HDSPM_SYNC_CHECK("SYNC IN SyncCheck", 3),
	HDSPM_TOGGLE_SETTING("Line Out", HDSPM_LineOut),
	HDSPM_TOGGLE_SETTING("TX 64 channels mode", HDSPM_TX_64ch),
	HDSPM_TOGGLE_SETTING("Disable 96K frames", HDSPM_SMUX),
	HDSPM_TOGGLE_SETTING("Clear Track Marker", HDSPM_clr_tms),
	HDSPM_TOGGLE_SETTING("Safe Mode", HDSPM_AutoInp),
	HDSPM_INPUT_SELECT("Input Select", 0),
	HDSPM_MADI_SPEEDMODE("MADI Speed Mode", 0)
};


static struct snd_kcontrol_new snd_hdspm_controls_madiface[] = {
	HDSPM_MIXER("Mixer", 0),
	HDSPM_INTERNAL_CLOCK("Internal Clock", 0),
	HDSPM_SYSTEM_CLOCK_MODE("System Clock Mode", 0),
	HDSPM_SYSTEM_SAMPLE_RATE("System Sample Rate", 0),
	HDSPM_AUTOSYNC_SAMPLE_RATE("External Rate", 0),
	HDSPM_SYNC_CHECK("MADI SyncCheck", 0),
	HDSPM_TOGGLE_SETTING("TX 64 channels mode", HDSPM_TX_64ch),
	HDSPM_TOGGLE_SETTING("Clear Track Marker", HDSPM_clr_tms),
	HDSPM_TOGGLE_SETTING("Safe Mode", HDSPM_AutoInp),
	HDSPM_MADI_SPEEDMODE("MADI Speed Mode", 0)
};

static struct snd_kcontrol_new snd_hdspm_controls_aio[] = {
	HDSPM_MIXER("Mixer", 0),
	HDSPM_INTERNAL_CLOCK("Internal Clock", 0),
	HDSPM_SYSTEM_CLOCK_MODE("System Clock Mode", 0),
	HDSPM_PREF_SYNC_REF("Preferred Sync Reference", 0),
	HDSPM_SYSTEM_SAMPLE_RATE("System Sample Rate", 0),
	HDSPM_AUTOSYNC_SAMPLE_RATE("External Rate", 0),
	HDSPM_SYNC_CHECK("WC SyncCheck", 0),
	HDSPM_SYNC_CHECK("AES SyncCheck", 1),
	HDSPM_SYNC_CHECK("SPDIF SyncCheck", 2),
	HDSPM_SYNC_CHECK("ADAT SyncCheck", 3),
	HDSPM_SYNC_CHECK("TCO SyncCheck", 4),
	HDSPM_SYNC_CHECK("SYNC IN SyncCheck", 5),
	HDSPM_AUTOSYNC_SAMPLE_RATE("WC Frequency", 0),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES Frequency", 1),
	HDSPM_AUTOSYNC_SAMPLE_RATE("SPDIF Frequency", 2),
	HDSPM_AUTOSYNC_SAMPLE_RATE("ADAT Frequency", 3),
	HDSPM_AUTOSYNC_SAMPLE_RATE("TCO Frequency", 4),
	HDSPM_AUTOSYNC_SAMPLE_RATE("SYNC IN Frequency", 5),
	HDSPM_CONTROL_TRISTATE("S/PDIF Input", HDSPM_c0_Input0),
	HDSPM_TOGGLE_SETTING("S/PDIF Out Optical", HDSPM_c0_Spdif_Opt),
	HDSPM_TOGGLE_SETTING("S/PDIF Out Professional", HDSPM_c0_Pro),
	HDSPM_TOGGLE_SETTING("ADAT internal (AEB/TEB)", HDSPM_c0_AEB1),
	HDSPM_TOGGLE_SETTING("XLR Breakout Cable", HDSPM_c0_Sym6db),
	HDSPM_TOGGLE_SETTING("Single Speed WordClock Out", HDSPM_c0_Wck48),
	HDSPM_CONTROL_TRISTATE("Input Level", HDSPM_c0_AD_GAIN0),
	HDSPM_CONTROL_TRISTATE("Output Level", HDSPM_c0_DA_GAIN0),
	HDSPM_CONTROL_TRISTATE("Phones Level", HDSPM_c0_PH_GAIN0)

		/*
		   HDSPM_INPUT_SELECT("Input Select", 0),
		   HDSPM_SPDIF_OPTICAL("SPDIF Out Optical", 0),
		   HDSPM_PROFESSIONAL("SPDIF Out Professional", 0);
		   HDSPM_SPDIF_IN("SPDIF In", 0);
		   HDSPM_BREAKOUT_CABLE("Breakout Cable", 0);
		   HDSPM_INPUT_LEVEL("Input Level", 0);
		   HDSPM_OUTPUT_LEVEL("Output Level", 0);
		   HDSPM_PHONES("Phones", 0);
		   */
};

static struct snd_kcontrol_new snd_hdspm_controls_raydat[] = {
	HDSPM_MIXER("Mixer", 0),
	HDSPM_INTERNAL_CLOCK("Internal Clock", 0),
	HDSPM_SYSTEM_CLOCK_MODE("Clock Mode", 0),
	HDSPM_PREF_SYNC_REF("Pref Sync Ref", 0),
	HDSPM_SYSTEM_SAMPLE_RATE("System Sample Rate", 0),
	HDSPM_SYNC_CHECK("WC SyncCheck", 0),
	HDSPM_SYNC_CHECK("AES SyncCheck", 1),
	HDSPM_SYNC_CHECK("SPDIF SyncCheck", 2),
	HDSPM_SYNC_CHECK("ADAT1 SyncCheck", 3),
	HDSPM_SYNC_CHECK("ADAT2 SyncCheck", 4),
	HDSPM_SYNC_CHECK("ADAT3 SyncCheck", 5),
	HDSPM_SYNC_CHECK("ADAT4 SyncCheck", 6),
	HDSPM_SYNC_CHECK("TCO SyncCheck", 7),
	HDSPM_SYNC_CHECK("SYNC IN SyncCheck", 8),
	HDSPM_AUTOSYNC_SAMPLE_RATE("WC Frequency", 0),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES Frequency", 1),
	HDSPM_AUTOSYNC_SAMPLE_RATE("SPDIF Frequency", 2),
	HDSPM_AUTOSYNC_SAMPLE_RATE("ADAT1 Frequency", 3),
	HDSPM_AUTOSYNC_SAMPLE_RATE("ADAT2 Frequency", 4),
	HDSPM_AUTOSYNC_SAMPLE_RATE("ADAT3 Frequency", 5),
	HDSPM_AUTOSYNC_SAMPLE_RATE("ADAT4 Frequency", 6),
	HDSPM_AUTOSYNC_SAMPLE_RATE("TCO Frequency", 7),
	HDSPM_AUTOSYNC_SAMPLE_RATE("SYNC IN Frequency", 8),
	HDSPM_TOGGLE_SETTING("S/PDIF Out Professional", HDSPM_c0_Pro),
	HDSPM_TOGGLE_SETTING("Single Speed WordClock Out", HDSPM_c0_Wck48)
};

static struct snd_kcontrol_new snd_hdspm_controls_aes32[] = {
	HDSPM_MIXER("Mixer", 0),
	HDSPM_INTERNAL_CLOCK("Internal Clock", 0),
	HDSPM_SYSTEM_CLOCK_MODE("System Clock Mode", 0),
	HDSPM_PREF_SYNC_REF("Preferred Sync Reference", 0),
	HDSPM_AUTOSYNC_REF("AutoSync Reference", 0),
	HDSPM_SYSTEM_SAMPLE_RATE("System Sample Rate", 0),
	HDSPM_AUTOSYNC_SAMPLE_RATE("External Rate", 11),
	HDSPM_SYNC_CHECK("WC Sync Check", 0),
	HDSPM_SYNC_CHECK("AES1 Sync Check", 1),
	HDSPM_SYNC_CHECK("AES2 Sync Check", 2),
	HDSPM_SYNC_CHECK("AES3 Sync Check", 3),
	HDSPM_SYNC_CHECK("AES4 Sync Check", 4),
	HDSPM_SYNC_CHECK("AES5 Sync Check", 5),
	HDSPM_SYNC_CHECK("AES6 Sync Check", 6),
	HDSPM_SYNC_CHECK("AES7 Sync Check", 7),
	HDSPM_SYNC_CHECK("AES8 Sync Check", 8),
	HDSPM_SYNC_CHECK("TCO Sync Check", 9),
	HDSPM_SYNC_CHECK("SYNC IN Sync Check", 10),
	HDSPM_AUTOSYNC_SAMPLE_RATE("WC Frequency", 0),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES1 Frequency", 1),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES2 Frequency", 2),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES3 Frequency", 3),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES4 Frequency", 4),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES5 Frequency", 5),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES6 Frequency", 6),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES7 Frequency", 7),
	HDSPM_AUTOSYNC_SAMPLE_RATE("AES8 Frequency", 8),
	HDSPM_AUTOSYNC_SAMPLE_RATE("TCO Frequency", 9),
	HDSPM_AUTOSYNC_SAMPLE_RATE("SYNC IN Frequency", 10),
	HDSPM_TOGGLE_SETTING("Line Out", HDSPM_LineOut),
	HDSPM_TOGGLE_SETTING("Emphasis", HDSPM_Emphasis),
	HDSPM_TOGGLE_SETTING("Non Audio", HDSPM_Dolby),
	HDSPM_TOGGLE_SETTING("Professional", HDSPM_Professional),
	HDSPM_TOGGLE_SETTING("Clear Track Marker", HDSPM_clr_tms),
	HDSPM_DS_WIRE("Double Speed Wire Mode", 0),
	HDSPM_QS_WIRE("Quad Speed Wire Mode", 0),
};



/* Control elements for the optional TCO module */
static struct snd_kcontrol_new snd_hdspm_controls_tco[] = {
	HDSPM_TCO_SAMPLE_RATE("TCO Sample Rate", 0),
	HDSPM_TCO_PULL("TCO Pull", 0),
	HDSPM_TCO_WCK_CONVERSION("TCO WCK Conversion", 0),
	HDSPM_TCO_FRAME_RATE("TCO Frame Rate", 0),
	HDSPM_TCO_SYNC_SOURCE("TCO Sync Source", 0),
	HDSPM_TCO_WORD_TERM("TCO Word Term", 0),
	HDSPM_TCO_LOCK_CHECK("TCO Input Check", 11),
	HDSPM_TCO_LOCK_CHECK("TCO LTC Valid", 12),
	HDSPM_TCO_LTC_FRAMES("TCO Detected Frame Rate", 0),
	HDSPM_TCO_VIDEO_INPUT_FORMAT("Video Input Format", 0)
};


static struct snd_kcontrol_new snd_hdspm_playback_mixer = HDSPM_PLAYBACK_MIXER;


static int hdspm_update_simple_mixer_controls(struct hdspm * hdspm)
{
	int i;

	for (i = hdspm->ds_out_channels; i < hdspm->ss_out_channels; ++i) {
		if (hdspm->system_sample_rate > 48000) {
			hdspm->playback_mixer_ctls[i]->vd[0].access =
				SNDRV_CTL_ELEM_ACCESS_INACTIVE |
				SNDRV_CTL_ELEM_ACCESS_READ |
				SNDRV_CTL_ELEM_ACCESS_VOLATILE;
		} else {
			hdspm->playback_mixer_ctls[i]->vd[0].access =
				SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_VOLATILE;
		}
		snd_ctl_notify(hdspm->card, SNDRV_CTL_EVENT_MASK_VALUE |
				SNDRV_CTL_EVENT_MASK_INFO,
				&hdspm->playback_mixer_ctls[i]->id);
	}

	return 0;
}


static int snd_hdspm_create_controls(struct snd_card *card,
					struct hdspm *hdspm)
{
	unsigned int idx, limit;
	int err;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_new *list = NULL;

	switch (hdspm->io_type) {
	case MADI:
		list = snd_hdspm_controls_madi;
		limit = ARRAY_SIZE(snd_hdspm_controls_madi);
		break;
	case MADIface:
		list = snd_hdspm_controls_madiface;
		limit = ARRAY_SIZE(snd_hdspm_controls_madiface);
		break;
	case AIO:
		list = snd_hdspm_controls_aio;
		limit = ARRAY_SIZE(snd_hdspm_controls_aio);
		break;
	case RayDAT:
		list = snd_hdspm_controls_raydat;
		limit = ARRAY_SIZE(snd_hdspm_controls_raydat);
		break;
	case AES32:
		list = snd_hdspm_controls_aes32;
		limit = ARRAY_SIZE(snd_hdspm_controls_aes32);
		break;
	}

	if (NULL != list) {
		for (idx = 0; idx < limit; idx++) {
			err = snd_ctl_add(card,
					snd_ctl_new1(&list[idx], hdspm));
			if (err < 0)
				return err;
		}
	}


	/* create simple 1:1 playback mixer controls */
	snd_hdspm_playback_mixer.name = "Chn";
	if (hdspm->system_sample_rate >= 128000) {
		limit = hdspm->qs_out_channels;
	} else if (hdspm->system_sample_rate >= 64000) {
		limit = hdspm->ds_out_channels;
	} else {
		limit = hdspm->ss_out_channels;
	}
	for (idx = 0; idx < limit; ++idx) {
		snd_hdspm_playback_mixer.index = idx + 1;
		kctl = snd_ctl_new1(&snd_hdspm_playback_mixer, hdspm);
		err = snd_ctl_add(card, kctl);
		if (err < 0)
			return err;
		hdspm->playback_mixer_ctls[idx] = kctl;
	}


	if (hdspm->tco) {
		/* add tco control elements */
		list = snd_hdspm_controls_tco;
		limit = ARRAY_SIZE(snd_hdspm_controls_tco);
		for (idx = 0; idx < limit; idx++) {
			err = snd_ctl_add(card,
					snd_ctl_new1(&list[idx], hdspm));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

/*------------------------------------------------------------
   /proc interface
 ------------------------------------------------------------*/

static void
snd_hdspm_proc_read_tco(struct snd_info_entry *entry,
					struct snd_info_buffer *buffer)
{
	struct hdspm *hdspm = entry->private_data;
	unsigned int status, control;
	int a, ltc, frames, seconds, minutes, hours;
	unsigned int period;
	u64 freq_const = 0;
	u32 rate;

	snd_iprintf(buffer, "--- TCO ---\n");

	status = hdspm_read(hdspm, HDSPM_statusRegister);
	control = hdspm->control_register;


	if (status & HDSPM_tco_detect) {
		snd_iprintf(buffer, "TCO module detected.\n");
		a = hdspm_read(hdspm, HDSPM_RD_TCO+4);
		if (a & HDSPM_TCO1_LTC_Input_valid) {
			snd_iprintf(buffer, "  LTC valid, ");
			switch (a & (HDSPM_TCO1_LTC_Format_LSB |
						HDSPM_TCO1_LTC_Format_MSB)) {
			case 0:
				snd_iprintf(buffer, "24 fps, ");
				break;
			case HDSPM_TCO1_LTC_Format_LSB:
				snd_iprintf(buffer, "25 fps, ");
				break;
			case HDSPM_TCO1_LTC_Format_MSB:
				snd_iprintf(buffer, "29.97 fps, ");
				break;
			default:
				snd_iprintf(buffer, "30 fps, ");
				break;
			}
			if (a & HDSPM_TCO1_set_drop_frame_flag) {
				snd_iprintf(buffer, "drop frame\n");
			} else {
				snd_iprintf(buffer, "full frame\n");
			}
		} else {
			snd_iprintf(buffer, "  no LTC\n");
		}
		if (a & HDSPM_TCO1_Video_Input_Format_NTSC) {
			snd_iprintf(buffer, "  Video: NTSC\n");
		} else if (a & HDSPM_TCO1_Video_Input_Format_PAL) {
			snd_iprintf(buffer, "  Video: PAL\n");
		} else {
			snd_iprintf(buffer, "  No video\n");
		}
		if (a & HDSPM_TCO1_TCO_lock) {
			snd_iprintf(buffer, "  Sync: lock\n");
		} else {
			snd_iprintf(buffer, "  Sync: no lock\n");
		}

		switch (hdspm->io_type) {
		case MADI:
		case AES32:
			freq_const = 110069313433624ULL;
			break;
		case RayDAT:
		case AIO:
			freq_const = 104857600000000ULL;
			break;
		case MADIface:
			break; /* no TCO possible */
		}

		period = hdspm_read(hdspm, HDSPM_RD_PLL_FREQ);
		snd_iprintf(buffer, "    period: %u\n", period);


		/* rate = freq_const/period; */
		rate = div_u64(freq_const, period);

		if (control & HDSPM_QuadSpeed) {
			rate *= 4;
		} else if (control & HDSPM_DoubleSpeed) {
			rate *= 2;
		}

		snd_iprintf(buffer, "  Frequency: %u Hz\n",
				(unsigned int) rate);

		ltc = hdspm_read(hdspm, HDSPM_RD_TCO);
		frames = ltc & 0xF;
		ltc >>= 4;
		frames += (ltc & 0x3) * 10;
		ltc >>= 4;
		seconds = ltc & 0xF;
		ltc >>= 4;
		seconds += (ltc & 0x7) * 10;
		ltc >>= 4;
		minutes = ltc & 0xF;
		ltc >>= 4;
		minutes += (ltc & 0x7) * 10;
		ltc >>= 4;
		hours = ltc & 0xF;
		ltc >>= 4;
		hours += (ltc & 0x3) * 10;
		snd_iprintf(buffer,
			"  LTC In: %02d:%02d:%02d:%02d\n",
			hours, minutes, seconds, frames);

	} else {
		snd_iprintf(buffer, "No TCO module detected.\n");
	}
}

static void
snd_hdspm_proc_read_madi(struct snd_info_entry *entry,
			 struct snd_info_buffer *buffer)
{
	struct hdspm *hdspm = entry->private_data;
	unsigned int status, status2;

	char *pref_sync_ref;
	char *autosync_ref;
	char *system_clock_mode;
	int x, x2;

	status = hdspm_read(hdspm, HDSPM_statusRegister);
	status2 = hdspm_read(hdspm, HDSPM_statusRegister2);

	snd_iprintf(buffer, "%s (Card #%d) Rev.%x Status2first3bits: %x\n",
			hdspm->card_name, hdspm->card->number + 1,
			hdspm->firmware_rev,
			(status2 & HDSPM_version0) |
			(status2 & HDSPM_version1) | (status2 &
				HDSPM_version2));

	snd_iprintf(buffer, "HW Serial: 0x%06x%06x\n",
			(hdspm_read(hdspm, HDSPM_midiStatusIn1)>>8) & 0xFFFFFF,
			hdspm->serial);

	snd_iprintf(buffer, "IRQ: %d Registers bus: 0x%lx VM: 0x%lx\n",
			hdspm->irq, hdspm->port, (unsigned long)hdspm->iobase);

	snd_iprintf(buffer, "--- System ---\n");

	snd_iprintf(buffer,
		"IRQ Pending: Audio=%d, MIDI0=%d, MIDI1=%d, IRQcount=%d\n",
		status & HDSPM_audioIRQPending,
		(status & HDSPM_midi0IRQPending) ? 1 : 0,
		(status & HDSPM_midi1IRQPending) ? 1 : 0,
		hdspm->irq_count);
	snd_iprintf(buffer,
		"HW pointer: id = %d, rawptr = %d (%d->%d) "
		"estimated= %ld (bytes)\n",
		((status & HDSPM_BufferID) ? 1 : 0),
		(status & HDSPM_BufferPositionMask),
		(status & HDSPM_BufferPositionMask) %
		(2 * (int)hdspm->period_bytes),
		((status & HDSPM_BufferPositionMask) - 64) %
		(2 * (int)hdspm->period_bytes),
		(long) hdspm_hw_pointer(hdspm) * 4);

	snd_iprintf(buffer,
		"MIDI FIFO: Out1=0x%x, Out2=0x%x, In1=0x%x, In2=0x%x \n",
		hdspm_read(hdspm, HDSPM_midiStatusOut0) & 0xFF,
		hdspm_read(hdspm, HDSPM_midiStatusOut1) & 0xFF,
		hdspm_read(hdspm, HDSPM_midiStatusIn0) & 0xFF,
		hdspm_read(hdspm, HDSPM_midiStatusIn1) & 0xFF);
	snd_iprintf(buffer,
		"MIDIoverMADI FIFO: In=0x%x, Out=0x%x \n",
		hdspm_read(hdspm, HDSPM_midiStatusIn2) & 0xFF,
		hdspm_read(hdspm, HDSPM_midiStatusOut2) & 0xFF);
	snd_iprintf(buffer,
		"Register: ctrl1=0x%x, ctrl2=0x%x, status1=0x%x, "
		"status2=0x%x\n",
		hdspm->control_register, hdspm->control2_register,
		status, status2);


	snd_iprintf(buffer, "--- Settings ---\n");

	x = hdspm_get_latency(hdspm);

	snd_iprintf(buffer,
		"Size (Latency): %d samples (2 periods of %lu bytes)\n",
		x, (unsigned long) hdspm->period_bytes);

	snd_iprintf(buffer, "Line out: %s\n",
		(hdspm->control_register & HDSPM_LineOut) ? "on " : "off");

	snd_iprintf(buffer,
		"ClearTrackMarker = %s, Transmit in %s Channel Mode, "
		"Auto Input %s\n",
		(hdspm->control_register & HDSPM_clr_tms) ? "on" : "off",
		(hdspm->control_register & HDSPM_TX_64ch) ? "64" : "56",
		(hdspm->control_register & HDSPM_AutoInp) ? "on" : "off");


	if (!(hdspm->control_register & HDSPM_ClockModeMaster))
		system_clock_mode = "AutoSync";
	else
		system_clock_mode = "Master";
	snd_iprintf(buffer, "AutoSync Reference: %s\n", system_clock_mode);

	switch (hdspm_pref_sync_ref(hdspm)) {
	case HDSPM_SYNC_FROM_WORD:
		pref_sync_ref = "Word Clock";
		break;
	case HDSPM_SYNC_FROM_MADI:
		pref_sync_ref = "MADI Sync";
		break;
	case HDSPM_SYNC_FROM_TCO:
		pref_sync_ref = "TCO";
		break;
	case HDSPM_SYNC_FROM_SYNC_IN:
		pref_sync_ref = "Sync In";
		break;
	default:
		pref_sync_ref = "XXXX Clock";
		break;
	}
	snd_iprintf(buffer, "Preferred Sync Reference: %s\n",
			pref_sync_ref);

	snd_iprintf(buffer, "System Clock Frequency: %d\n",
			hdspm->system_sample_rate);


	snd_iprintf(buffer, "--- Status:\n");

	x = status & HDSPM_madiSync;
	x2 = status2 & HDSPM_wcSync;

	snd_iprintf(buffer, "Inputs MADI=%s, WordClock=%s\n",
			(status & HDSPM_madiLock) ? (x ? "Sync" : "Lock") :
			"NoLock",
			(status2 & HDSPM_wcLock) ? (x2 ? "Sync" : "Lock") :
			"NoLock");

	switch (hdspm_autosync_ref(hdspm)) {
	case HDSPM_AUTOSYNC_FROM_SYNC_IN:
		autosync_ref = "Sync In";
		break;
	case HDSPM_AUTOSYNC_FROM_TCO:
		autosync_ref = "TCO";
		break;
	case HDSPM_AUTOSYNC_FROM_WORD:
		autosync_ref = "Word Clock";
		break;
	case HDSPM_AUTOSYNC_FROM_MADI:
		autosync_ref = "MADI Sync";
		break;
	case HDSPM_AUTOSYNC_FROM_NONE:
		autosync_ref = "Input not valid";
		break;
	default:
		autosync_ref = "---";
		break;
	}
	snd_iprintf(buffer,
		"AutoSync: Reference= %s, Freq=%d (MADI = %d, Word = %d)\n",
		autosync_ref, hdspm_external_sample_rate(hdspm),
		(status & HDSPM_madiFreqMask) >> 22,
		(status2 & HDSPM_wcFreqMask) >> 5);

	snd_iprintf(buffer, "Input: %s, Mode=%s\n",
		(status & HDSPM_AB_int) ? "Coax" : "Optical",
		(status & HDSPM_RX_64ch) ? "64 channels" :
		"56 channels");

	/* call readout function for TCO specific status */
	snd_hdspm_proc_read_tco(entry, buffer);

	snd_iprintf(buffer, "\n");
}

static void
snd_hdspm_proc_read_aes32(struct snd_info_entry * entry,
			  struct snd_info_buffer *buffer)
{
	struct hdspm *hdspm = entry->private_data;
	unsigned int status;
	unsigned int status2;
	unsigned int timecode;
	unsigned int wcLock, wcSync;
	int pref_syncref;
	char *autosync_ref;
	int x;

	status = hdspm_read(hdspm, HDSPM_statusRegister);
	status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
	timecode = hdspm_read(hdspm, HDSPM_timecodeRegister);

	snd_iprintf(buffer, "%s (Card #%d) Rev.%x\n",
		    hdspm->card_name, hdspm->card->number + 1,
		    hdspm->firmware_rev);

	snd_iprintf(buffer, "IRQ: %d Registers bus: 0x%lx VM: 0x%lx\n",
		    hdspm->irq, hdspm->port, (unsigned long)hdspm->iobase);

	snd_iprintf(buffer, "--- System ---\n");

	snd_iprintf(buffer,
		    "IRQ Pending: Audio=%d, MIDI0=%d, MIDI1=%d, IRQcount=%d\n",
		    status & HDSPM_audioIRQPending,
		    (status & HDSPM_midi0IRQPending) ? 1 : 0,
		    (status & HDSPM_midi1IRQPending) ? 1 : 0,
		    hdspm->irq_count);
	snd_iprintf(buffer,
		    "HW pointer: id = %d, rawptr = %d (%d->%d) "
		    "estimated= %ld (bytes)\n",
		    ((status & HDSPM_BufferID) ? 1 : 0),
		    (status & HDSPM_BufferPositionMask),
		    (status & HDSPM_BufferPositionMask) %
		    (2 * (int)hdspm->period_bytes),
		    ((status & HDSPM_BufferPositionMask) - 64) %
		    (2 * (int)hdspm->period_bytes),
		    (long) hdspm_hw_pointer(hdspm) * 4);

	snd_iprintf(buffer,
		    "MIDI FIFO: Out1=0x%x, Out2=0x%x, In1=0x%x, In2=0x%x \n",
		    hdspm_read(hdspm, HDSPM_midiStatusOut0) & 0xFF,
		    hdspm_read(hdspm, HDSPM_midiStatusOut1) & 0xFF,
		    hdspm_read(hdspm, HDSPM_midiStatusIn0) & 0xFF,
		    hdspm_read(hdspm, HDSPM_midiStatusIn1) & 0xFF);
	snd_iprintf(buffer,
		    "MIDIoverMADI FIFO: In=0x%x, Out=0x%x \n",
		    hdspm_read(hdspm, HDSPM_midiStatusIn2) & 0xFF,
		    hdspm_read(hdspm, HDSPM_midiStatusOut2) & 0xFF);
	snd_iprintf(buffer,
		    "Register: ctrl1=0x%x, ctrl2=0x%x, status1=0x%x, "
		    "status2=0x%x\n",
		    hdspm->control_register, hdspm->control2_register,
		    status, status2);

	snd_iprintf(buffer, "--- Settings ---\n");

	x = hdspm_get_latency(hdspm);

	snd_iprintf(buffer,
		    "Size (Latency): %d samples (2 periods of %lu bytes)\n",
		    x, (unsigned long) hdspm->period_bytes);

	snd_iprintf(buffer, "Line out: %s\n",
		    (hdspm->
		     control_register & HDSPM_LineOut) ? "on " : "off");

	snd_iprintf(buffer,
		    "ClearTrackMarker %s, Emphasis %s, Dolby %s\n",
		    (hdspm->
		     control_register & HDSPM_clr_tms) ? "on" : "off",
		    (hdspm->
		     control_register & HDSPM_Emphasis) ? "on" : "off",
		    (hdspm->
		     control_register & HDSPM_Dolby) ? "on" : "off");


	pref_syncref = hdspm_pref_sync_ref(hdspm);
	if (pref_syncref == 0)
		snd_iprintf(buffer, "Preferred Sync Reference: Word Clock\n");
	else
		snd_iprintf(buffer, "Preferred Sync Reference: AES%d\n",
				pref_syncref);

	snd_iprintf(buffer, "System Clock Frequency: %d\n",
		    hdspm->system_sample_rate);

	snd_iprintf(buffer, "Double speed: %s\n",
			hdspm->control_register & HDSPM_DS_DoubleWire?
			"Double wire" : "Single wire");
	snd_iprintf(buffer, "Quad speed: %s\n",
			hdspm->control_register & HDSPM_QS_DoubleWire?
			"Double wire" :
			hdspm->control_register & HDSPM_QS_QuadWire?
			"Quad wire" : "Single wire");

	snd_iprintf(buffer, "--- Status:\n");

	wcLock = status & HDSPM_AES32_wcLock;
	wcSync = wcLock && (status & HDSPM_AES32_wcSync);

	snd_iprintf(buffer, "Word: %s  Frequency: %d\n",
		    (wcLock) ? (wcSync ? "Sync   " : "Lock   ") : "No Lock",
		    HDSPM_bit2freq((status >> HDSPM_AES32_wcFreq_bit) & 0xF));

	for (x = 0; x < 8; x++) {
		snd_iprintf(buffer, "AES%d: %s  Frequency: %d\n",
			    x+1,
			    (status2 & (HDSPM_LockAES >> x)) ?
			    "Sync   " : "No Lock",
			    HDSPM_bit2freq((timecode >> (4*x)) & 0xF));
	}

	switch (hdspm_autosync_ref(hdspm)) {
	case HDSPM_AES32_AUTOSYNC_FROM_NONE:
		autosync_ref = "None"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_WORD:
		autosync_ref = "Word Clock"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES1:
		autosync_ref = "AES1"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES2:
		autosync_ref = "AES2"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES3:
		autosync_ref = "AES3"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES4:
		autosync_ref = "AES4"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES5:
		autosync_ref = "AES5"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES6:
		autosync_ref = "AES6"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES7:
		autosync_ref = "AES7"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES8:
		autosync_ref = "AES8"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_TCO:
		autosync_ref = "TCO"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_SYNC_IN:
		autosync_ref = "Sync In"; break;
	default:
		autosync_ref = "---"; break;
	}
	snd_iprintf(buffer, "AutoSync ref = %s\n", autosync_ref);

	/* call readout function for TCO specific status */
	snd_hdspm_proc_read_tco(entry, buffer);

	snd_iprintf(buffer, "\n");
}

static void
snd_hdspm_proc_read_raydat(struct snd_info_entry *entry,
			 struct snd_info_buffer *buffer)
{
	struct hdspm *hdspm = entry->private_data;
	unsigned int status1, status2, status3, i;
	unsigned int lock, sync;

	status1 = hdspm_read(hdspm, HDSPM_RD_STATUS_1); /* s1 */
	status2 = hdspm_read(hdspm, HDSPM_RD_STATUS_2); /* freq */
	status3 = hdspm_read(hdspm, HDSPM_RD_STATUS_3); /* s2 */

	snd_iprintf(buffer, "STATUS1: 0x%08x\n", status1);
	snd_iprintf(buffer, "STATUS2: 0x%08x\n", status2);
	snd_iprintf(buffer, "STATUS3: 0x%08x\n", status3);


	snd_iprintf(buffer, "\n*** CLOCK MODE\n\n");

	snd_iprintf(buffer, "Clock mode      : %s\n",
		(hdspm_system_clock_mode(hdspm) == 0) ? "master" : "slave");
	snd_iprintf(buffer, "System frequency: %d Hz\n",
		hdspm_get_system_sample_rate(hdspm));

	snd_iprintf(buffer, "\n*** INPUT STATUS\n\n");

	lock = 0x1;
	sync = 0x100;

	for (i = 0; i < 8; i++) {
		snd_iprintf(buffer, "s1_input %d: Lock %d, Sync %d, Freq %s\n",
				i,
				(status1 & lock) ? 1 : 0,
				(status1 & sync) ? 1 : 0,
				texts_freq[(status2 >> (i * 4)) & 0xF]);

		lock = lock<<1;
		sync = sync<<1;
	}

	snd_iprintf(buffer, "WC input: Lock %d, Sync %d, Freq %s\n",
			(status1 & 0x1000000) ? 1 : 0,
			(status1 & 0x2000000) ? 1 : 0,
			texts_freq[(status1 >> 16) & 0xF]);

	snd_iprintf(buffer, "TCO input: Lock %d, Sync %d, Freq %s\n",
			(status1 & 0x4000000) ? 1 : 0,
			(status1 & 0x8000000) ? 1 : 0,
			texts_freq[(status1 >> 20) & 0xF]);

	snd_iprintf(buffer, "SYNC IN: Lock %d, Sync %d, Freq %s\n",
			(status3 & 0x400) ? 1 : 0,
			(status3 & 0x800) ? 1 : 0,
			texts_freq[(status2 >> 12) & 0xF]);

}

#ifdef CONFIG_SND_DEBUG
static void
snd_hdspm_proc_read_debug(struct snd_info_entry *entry,
			  struct snd_info_buffer *buffer)
{
	struct hdspm *hdspm = entry->private_data;

	int j,i;

	for (i = 0; i < 256 /* 1024*64 */; i += j) {
		snd_iprintf(buffer, "0x%08X: ", i);
		for (j = 0; j < 16; j += 4)
			snd_iprintf(buffer, "%08X ", hdspm_read(hdspm, i + j));
		snd_iprintf(buffer, "\n");
	}
}
#endif


static void snd_hdspm_proc_ports_in(struct snd_info_entry *entry,
			  struct snd_info_buffer *buffer)
{
	struct hdspm *hdspm = entry->private_data;
	int i;

	snd_iprintf(buffer, "# generated by hdspm\n");

	for (i = 0; i < hdspm->max_channels_in; i++) {
		snd_iprintf(buffer, "%d=%s\n", i+1, hdspm->port_names_in[i]);
	}
}

static void snd_hdspm_proc_ports_out(struct snd_info_entry *entry,
			  struct snd_info_buffer *buffer)
{
	struct hdspm *hdspm = entry->private_data;
	int i;

	snd_iprintf(buffer, "# generated by hdspm\n");

	for (i = 0; i < hdspm->max_channels_out; i++) {
		snd_iprintf(buffer, "%d=%s\n", i+1, hdspm->port_names_out[i]);
	}
}


static void snd_hdspm_proc_init(struct hdspm *hdspm)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(hdspm->card, "hdspm", &entry)) {
		switch (hdspm->io_type) {
		case AES32:
			snd_info_set_text_ops(entry, hdspm,
					snd_hdspm_proc_read_aes32);
			break;
		case MADI:
			snd_info_set_text_ops(entry, hdspm,
					snd_hdspm_proc_read_madi);
			break;
		case MADIface:
			/* snd_info_set_text_ops(entry, hdspm,
			 snd_hdspm_proc_read_madiface); */
			break;
		case RayDAT:
			snd_info_set_text_ops(entry, hdspm,
					snd_hdspm_proc_read_raydat);
			break;
		case AIO:
			break;
		}
	}

	if (!snd_card_proc_new(hdspm->card, "ports.in", &entry)) {
		snd_info_set_text_ops(entry, hdspm, snd_hdspm_proc_ports_in);
	}

	if (!snd_card_proc_new(hdspm->card, "ports.out", &entry)) {
		snd_info_set_text_ops(entry, hdspm, snd_hdspm_proc_ports_out);
	}

#ifdef CONFIG_SND_DEBUG
	/* debug file to read all hdspm registers */
	if (!snd_card_proc_new(hdspm->card, "debug", &entry))
		snd_info_set_text_ops(entry, hdspm,
				snd_hdspm_proc_read_debug);
#endif
}

/*------------------------------------------------------------
   hdspm intitialize
 ------------------------------------------------------------*/

static int snd_hdspm_set_defaults(struct hdspm * hdspm)
{
	/* ASSUMPTION: hdspm->lock is either held, or there is no need to
	   hold it (e.g. during module initialization).
	   */

	/* set defaults:       */

	hdspm->settings_register = 0;

	switch (hdspm->io_type) {
	case MADI:
	case MADIface:
		hdspm->control_register =
			0x2 + 0x8 + 0x10 + 0x80 + 0x400 + 0x4000 + 0x1000000;
		break;

	case RayDAT:
	case AIO:
		hdspm->settings_register = 0x1 + 0x1000;
		/* Magic values are: LAT_0, LAT_2, Master, freq1, tx64ch, inp_0,
		 * line_out */
		hdspm->control_register =
			0x2 + 0x8 + 0x10 + 0x80 + 0x400 + 0x4000 + 0x1000000;
		break;

	case AES32:
		hdspm->control_register =
			HDSPM_ClockModeMaster |	/* Master Clock Mode on */
			hdspm_encode_latency(7) | /* latency max=8192samples */
			HDSPM_SyncRef0 |	/* AES1 is syncclock */
			HDSPM_LineOut |	/* Analog output in */
			HDSPM_Professional;  /* Professional mode */
		break;
	}

	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	if (AES32 == hdspm->io_type) {
		/* No control2 register for AES32 */
#ifdef SNDRV_BIG_ENDIAN
		hdspm->control2_register = HDSPM_BIGENDIAN_MODE;
#else
		hdspm->control2_register = 0;
#endif

		hdspm_write(hdspm, HDSPM_control2Reg, hdspm->control2_register);
	}
	hdspm_compute_period_size(hdspm);

	/* silence everything */

	all_in_all_mixer(hdspm, 0 * UNITY_GAIN);

	if (hdspm_is_raydat_or_aio(hdspm))
		hdspm_write(hdspm, HDSPM_WR_SETTINGS, hdspm->settings_register);

	/* set a default rate so that the channel map is set up. */
	hdspm_set_rate(hdspm, 48000, 1);

	return 0;
}


/*------------------------------------------------------------
   interrupt
 ------------------------------------------------------------*/

static irqreturn_t snd_hdspm_interrupt(int irq, void *dev_id)
{
	struct hdspm *hdspm = (struct hdspm *) dev_id;
	unsigned int status;
	int i, audio, midi, schedule = 0;
	/* cycles_t now; */

	status = hdspm_read(hdspm, HDSPM_statusRegister);

	audio = status & HDSPM_audioIRQPending;
	midi = status & (HDSPM_midi0IRQPending | HDSPM_midi1IRQPending |
			HDSPM_midi2IRQPending | HDSPM_midi3IRQPending);

	/* now = get_cycles(); */
	/*
	 *   LAT_2..LAT_0 period  counter (win)  counter (mac)
	 *          6       4096   ~256053425     ~514672358
	 *          5       2048   ~128024983     ~257373821
	 *          4       1024    ~64023706     ~128718089
	 *          3        512    ~32005945      ~64385999
	 *          2        256    ~16003039      ~32260176
	 *          1        128     ~7998738      ~16194507
	 *          0         64     ~3998231       ~8191558
	 */
	/*
	  dev_info(hdspm->card->dev, "snd_hdspm_interrupt %llu @ %llx\n",
	   now-hdspm->last_interrupt, status & 0xFFC0);
	   hdspm->last_interrupt = now;
	*/

	if (!audio && !midi)
		return IRQ_NONE;

	hdspm_write(hdspm, HDSPM_interruptConfirmation, 0);
	hdspm->irq_count++;


	if (audio) {
		if (hdspm->capture_substream)
			snd_pcm_period_elapsed(hdspm->capture_substream);

		if (hdspm->playback_substream)
			snd_pcm_period_elapsed(hdspm->playback_substream);
	}

	if (midi) {
		i = 0;
		while (i < hdspm->midiPorts) {
			if ((hdspm_read(hdspm,
				hdspm->midi[i].statusIn) & 0xff) &&
					(status & hdspm->midi[i].irq)) {
				/* we disable interrupts for this input until
				 * processing is done
				 */
				hdspm->control_register &= ~hdspm->midi[i].ie;
				hdspm_write(hdspm, HDSPM_controlRegister,
						hdspm->control_register);
				hdspm->midi[i].pending = 1;
				schedule = 1;
			}

			i++;
		}

		if (schedule)
			tasklet_hi_schedule(&hdspm->midi_tasklet);
	}

	return IRQ_HANDLED;
}

/*------------------------------------------------------------
   pcm interface
  ------------------------------------------------------------*/


static snd_pcm_uframes_t snd_hdspm_hw_pointer(struct snd_pcm_substream
					      *substream)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	return hdspm_hw_pointer(hdspm);
}


static int snd_hdspm_reset(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *other;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		other = hdspm->capture_substream;
	else
		other = hdspm->playback_substream;

	if (hdspm->running)
		runtime->status->hw_ptr = hdspm_hw_pointer(hdspm);
	else
		runtime->status->hw_ptr = 0;
	if (other) {
		struct snd_pcm_substream *s;
		struct snd_pcm_runtime *oruntime = other->runtime;
		snd_pcm_group_for_each_entry(s, substream) {
			if (s == other) {
				oruntime->status->hw_ptr =
					runtime->status->hw_ptr;
				break;
			}
		}
	}
	return 0;
}

static int snd_hdspm_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	int err;
	int i;
	pid_t this_pid;
	pid_t other_pid;

	spin_lock_irq(&hdspm->lock);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		this_pid = hdspm->playback_pid;
		other_pid = hdspm->capture_pid;
	} else {
		this_pid = hdspm->capture_pid;
		other_pid = hdspm->playback_pid;
	}

	if (other_pid > 0 && this_pid != other_pid) {

		/* The other stream is open, and not by the same
		   task as this one. Make sure that the parameters
		   that matter are the same.
		   */

		if (params_rate(params) != hdspm->system_sample_rate) {
			spin_unlock_irq(&hdspm->lock);
			_snd_pcm_hw_param_setempty(params,
					SNDRV_PCM_HW_PARAM_RATE);
			return -EBUSY;
		}

		if (params_period_size(params) != hdspm->period_bytes / 4) {
			spin_unlock_irq(&hdspm->lock);
			_snd_pcm_hw_param_setempty(params,
					SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
			return -EBUSY;
		}

	}
	/* We're fine. */
	spin_unlock_irq(&hdspm->lock);

	/* how to make sure that the rate matches an externally-set one ?   */

	spin_lock_irq(&hdspm->lock);
	err = hdspm_set_rate(hdspm, params_rate(params), 0);
	if (err < 0) {
		dev_info(hdspm->card->dev, "err on hdspm_set_rate: %d\n", err);
		spin_unlock_irq(&hdspm->lock);
		_snd_pcm_hw_param_setempty(params,
				SNDRV_PCM_HW_PARAM_RATE);
		return err;
	}
	spin_unlock_irq(&hdspm->lock);

	err = hdspm_set_interrupt_interval(hdspm,
			params_period_size(params));
	if (err < 0) {
		dev_info(hdspm->card->dev,
			 "err on hdspm_set_interrupt_interval: %d\n", err);
		_snd_pcm_hw_param_setempty(params,
				SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
		return err;
	}

	/* Memory allocation, takashi's method, dont know if we should
	 * spinlock
	 */
	/* malloc all buffer even if not enabled to get sure */
	/* Update for MADI rev 204: we need to allocate for all channels,
	 * otherwise it doesn't work at 96kHz */

	err =
		snd_pcm_lib_malloc_pages(substream, HDSPM_DMA_AREA_BYTES);
	if (err < 0) {
		dev_info(hdspm->card->dev,
			 "err on snd_pcm_lib_malloc_pages: %d\n", err);
		return err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		hdspm_set_sgbuf(hdspm, substream, HDSPM_pageAddressBufferOut,
				params_channels(params));

		for (i = 0; i < params_channels(params); ++i)
			snd_hdspm_enable_out(hdspm, i, 1);

		hdspm->playback_buffer =
			(unsigned char *) substream->runtime->dma_area;
		dev_dbg(hdspm->card->dev,
			"Allocated sample buffer for playback at %p\n",
				hdspm->playback_buffer);
	} else {
		hdspm_set_sgbuf(hdspm, substream, HDSPM_pageAddressBufferIn,
				params_channels(params));

		for (i = 0; i < params_channels(params); ++i)
			snd_hdspm_enable_in(hdspm, i, 1);

		hdspm->capture_buffer =
			(unsigned char *) substream->runtime->dma_area;
		dev_dbg(hdspm->card->dev,
			"Allocated sample buffer for capture at %p\n",
				hdspm->capture_buffer);
	}

	/*
	   dev_dbg(hdspm->card->dev,
	   "Allocated sample buffer for %s at 0x%08X\n",
	   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
	   "playback" : "capture",
	   snd_pcm_sgbuf_get_addr(substream, 0));
	   */
	/*
	   dev_dbg(hdspm->card->dev,
	   "set_hwparams: %s %d Hz, %d channels, bs = %d\n",
	   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
	   "playback" : "capture",
	   params_rate(params), params_channels(params),
	   params_buffer_size(params));
	   */


	/*  For AES cards, the float format bit is the same as the
	 *  preferred sync reference. Since we don't want to break
	 *  sync settings, we have to skip the remaining part of this
	 *  function.
	 */
	if (hdspm->io_type == AES32) {
		return 0;
	}


	/* Switch to native float format if requested */
	if (SNDRV_PCM_FORMAT_FLOAT_LE == params_format(params)) {
		if (!(hdspm->control_register & HDSPe_FLOAT_FORMAT))
			dev_info(hdspm->card->dev,
				 "Switching to native 32bit LE float format.\n");

		hdspm->control_register |= HDSPe_FLOAT_FORMAT;
	} else if (SNDRV_PCM_FORMAT_S32_LE == params_format(params)) {
		if (hdspm->control_register & HDSPe_FLOAT_FORMAT)
			dev_info(hdspm->card->dev,
				 "Switching to native 32bit LE integer format.\n");

		hdspm->control_register &= ~HDSPe_FLOAT_FORMAT;
	}
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

static int snd_hdspm_hw_free(struct snd_pcm_substream *substream)
{
	int i;
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		/* params_channels(params) should be enough,
		   but to get sure in case of error */
		for (i = 0; i < hdspm->max_channels_out; ++i)
			snd_hdspm_enable_out(hdspm, i, 0);

		hdspm->playback_buffer = NULL;
	} else {
		for (i = 0; i < hdspm->max_channels_in; ++i)
			snd_hdspm_enable_in(hdspm, i, 0);

		hdspm->capture_buffer = NULL;

	}

	snd_pcm_lib_free_pages(substream);

	return 0;
}


static int snd_hdspm_channel_info(struct snd_pcm_substream *substream,
		struct snd_pcm_channel_info *info)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (snd_BUG_ON(info->channel >= hdspm->max_channels_out)) {
			dev_info(hdspm->card->dev,
				 "snd_hdspm_channel_info: output channel out of range (%d)\n",
				 info->channel);
			return -EINVAL;
		}

		if (hdspm->channel_map_out[info->channel] < 0) {
			dev_info(hdspm->card->dev,
				 "snd_hdspm_channel_info: output channel %d mapped out\n",
				 info->channel);
			return -EINVAL;
		}

		info->offset = hdspm->channel_map_out[info->channel] *
			HDSPM_CHANNEL_BUFFER_BYTES;
	} else {
		if (snd_BUG_ON(info->channel >= hdspm->max_channels_in)) {
			dev_info(hdspm->card->dev,
				 "snd_hdspm_channel_info: input channel out of range (%d)\n",
				 info->channel);
			return -EINVAL;
		}

		if (hdspm->channel_map_in[info->channel] < 0) {
			dev_info(hdspm->card->dev,
				 "snd_hdspm_channel_info: input channel %d mapped out\n",
				 info->channel);
			return -EINVAL;
		}

		info->offset = hdspm->channel_map_in[info->channel] *
			HDSPM_CHANNEL_BUFFER_BYTES;
	}

	info->first = 0;
	info->step = 32;
	return 0;
}


static int snd_hdspm_ioctl(struct snd_pcm_substream *substream,
		unsigned int cmd, void *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL1_RESET:
		return snd_hdspm_reset(substream);

	case SNDRV_PCM_IOCTL1_CHANNEL_INFO:
		{
			struct snd_pcm_channel_info *info = arg;
			return snd_hdspm_channel_info(substream, info);
		}
	default:
		break;
	}

	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_hdspm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *other;
	int running;

	spin_lock(&hdspm->lock);
	running = hdspm->running;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		running |= 1 << substream->stream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		running &= ~(1 << substream->stream);
		break;
	default:
		snd_BUG();
		spin_unlock(&hdspm->lock);
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		other = hdspm->capture_substream;
	else
		other = hdspm->playback_substream;

	if (other) {
		struct snd_pcm_substream *s;
		snd_pcm_group_for_each_entry(s, substream) {
			if (s == other) {
				snd_pcm_trigger_done(s, substream);
				if (cmd == SNDRV_PCM_TRIGGER_START)
					running |= 1 << s->stream;
				else
					running &= ~(1 << s->stream);
				goto _ok;
			}
		}
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			if (!(running & (1 << SNDRV_PCM_STREAM_PLAYBACK))
					&& substream->stream ==
					SNDRV_PCM_STREAM_CAPTURE)
				hdspm_silence_playback(hdspm);
		} else {
			if (running &&
				substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				hdspm_silence_playback(hdspm);
		}
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			hdspm_silence_playback(hdspm);
	}
_ok:
	snd_pcm_trigger_done(substream, substream);
	if (!hdspm->running && running)
		hdspm_start_audio(hdspm);
	else if (hdspm->running && !running)
		hdspm_stop_audio(hdspm);
	hdspm->running = running;
	spin_unlock(&hdspm->lock);

	return 0;
}

static int snd_hdspm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static struct snd_pcm_hardware snd_hdspm_playback_subinfo = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_NONINTERLEAVED |
		 SNDRV_PCM_INFO_SYNC_START | SNDRV_PCM_INFO_DOUBLE),
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rates = (SNDRV_PCM_RATE_32000 |
		  SNDRV_PCM_RATE_44100 |
		  SNDRV_PCM_RATE_48000 |
		  SNDRV_PCM_RATE_64000 |
		  SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
		  SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000 ),
	.rate_min = 32000,
	.rate_max = 192000,
	.channels_min = 1,
	.channels_max = HDSPM_MAX_CHANNELS,
	.buffer_bytes_max =
	    HDSPM_CHANNEL_BUFFER_BYTES * HDSPM_MAX_CHANNELS,
	.period_bytes_min = (32 * 4),
	.period_bytes_max = (8192 * 4) * HDSPM_MAX_CHANNELS,
	.periods_min = 2,
	.periods_max = 512,
	.fifo_size = 0
};

static struct snd_pcm_hardware snd_hdspm_capture_subinfo = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_NONINTERLEAVED |
		 SNDRV_PCM_INFO_SYNC_START),
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rates = (SNDRV_PCM_RATE_32000 |
		  SNDRV_PCM_RATE_44100 |
		  SNDRV_PCM_RATE_48000 |
		  SNDRV_PCM_RATE_64000 |
		  SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
		  SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000),
	.rate_min = 32000,
	.rate_max = 192000,
	.channels_min = 1,
	.channels_max = HDSPM_MAX_CHANNELS,
	.buffer_bytes_max =
	    HDSPM_CHANNEL_BUFFER_BYTES * HDSPM_MAX_CHANNELS,
	.period_bytes_min = (32 * 4),
	.period_bytes_max = (8192 * 4) * HDSPM_MAX_CHANNELS,
	.periods_min = 2,
	.periods_max = 512,
	.fifo_size = 0
};

static int snd_hdspm_hw_rule_in_channels_rate(struct snd_pcm_hw_params *params,
					   struct snd_pcm_hw_rule *rule)
{
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	if (r->min > 96000 && r->max <= 192000) {
		struct snd_interval t = {
			.min = hdspm->qs_in_channels,
			.max = hdspm->qs_in_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->min > 48000 && r->max <= 96000) {
		struct snd_interval t = {
			.min = hdspm->ds_in_channels,
			.max = hdspm->ds_in_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->max < 64000) {
		struct snd_interval t = {
			.min = hdspm->ss_in_channels,
			.max = hdspm->ss_in_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	}

	return 0;
}

static int snd_hdspm_hw_rule_out_channels_rate(struct snd_pcm_hw_params *params,
					   struct snd_pcm_hw_rule * rule)
{
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	if (r->min > 96000 && r->max <= 192000) {
		struct snd_interval t = {
			.min = hdspm->qs_out_channels,
			.max = hdspm->qs_out_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->min > 48000 && r->max <= 96000) {
		struct snd_interval t = {
			.min = hdspm->ds_out_channels,
			.max = hdspm->ds_out_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->max < 64000) {
		struct snd_interval t = {
			.min = hdspm->ss_out_channels,
			.max = hdspm->ss_out_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else {
	}
	return 0;
}

static int snd_hdspm_hw_rule_rate_in_channels(struct snd_pcm_hw_params *params,
					   struct snd_pcm_hw_rule * rule)
{
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	if (c->min >= hdspm->ss_in_channels) {
		struct snd_interval t = {
			.min = 32000,
			.max = 48000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdspm->qs_in_channels) {
		struct snd_interval t = {
			.min = 128000,
			.max = 192000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdspm->ds_in_channels) {
		struct snd_interval t = {
			.min = 64000,
			.max = 96000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	}

	return 0;
}
static int snd_hdspm_hw_rule_rate_out_channels(struct snd_pcm_hw_params *params,
					   struct snd_pcm_hw_rule *rule)
{
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	if (c->min >= hdspm->ss_out_channels) {
		struct snd_interval t = {
			.min = 32000,
			.max = 48000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdspm->qs_out_channels) {
		struct snd_interval t = {
			.min = 128000,
			.max = 192000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdspm->ds_out_channels) {
		struct snd_interval t = {
			.min = 64000,
			.max = 96000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	}

	return 0;
}

static int snd_hdspm_hw_rule_in_channels(struct snd_pcm_hw_params *params,
				      struct snd_pcm_hw_rule *rule)
{
	unsigned int list[3];
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);

	list[0] = hdspm->qs_in_channels;
	list[1] = hdspm->ds_in_channels;
	list[2] = hdspm->ss_in_channels;
	return snd_interval_list(c, 3, list, 0);
}

static int snd_hdspm_hw_rule_out_channels(struct snd_pcm_hw_params *params,
				      struct snd_pcm_hw_rule *rule)
{
	unsigned int list[3];
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);

	list[0] = hdspm->qs_out_channels;
	list[1] = hdspm->ds_out_channels;
	list[2] = hdspm->ss_out_channels;
	return snd_interval_list(c, 3, list, 0);
}


static unsigned int hdspm_aes32_sample_rates[] = {
	32000, 44100, 48000, 64000, 88200, 96000, 128000, 176400, 192000
};

static struct snd_pcm_hw_constraint_list
hdspm_hw_constraints_aes32_sample_rates = {
	.count = ARRAY_SIZE(hdspm_aes32_sample_rates),
	.list = hdspm_aes32_sample_rates,
	.mask = 0
};

static int snd_hdspm_open(struct snd_pcm_substream *substream)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	spin_lock_irq(&hdspm->lock);
	snd_pcm_set_sync(substream);
	runtime->hw = (playback) ? snd_hdspm_playback_subinfo :
		snd_hdspm_capture_subinfo;

	if (playback) {
		if (hdspm->capture_substream == NULL)
			hdspm_stop_audio(hdspm);

		hdspm->playback_pid = current->pid;
		hdspm->playback_substream = substream;
	} else {
		if (hdspm->playback_substream == NULL)
			hdspm_stop_audio(hdspm);

		hdspm->capture_pid = current->pid;
		hdspm->capture_substream = substream;
	}

	spin_unlock_irq(&hdspm->lock);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_pow2(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);

	switch (hdspm->io_type) {
	case AIO:
	case RayDAT:
		snd_pcm_hw_constraint_minmax(runtime,
					     SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					     32, 4096);
		/* RayDAT & AIO have a fixed buffer of 16384 samples per channel */
		snd_pcm_hw_constraint_single(runtime,
					     SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
					     16384);
		break;

	default:
		snd_pcm_hw_constraint_minmax(runtime,
					     SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					     64, 8192);
		snd_pcm_hw_constraint_single(runtime,
					     SNDRV_PCM_HW_PARAM_PERIODS, 2);
		break;
	}

	if (AES32 == hdspm->io_type) {
		runtime->hw.rates |= SNDRV_PCM_RATE_KNOT;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				&hdspm_hw_constraints_aes32_sample_rates);
	} else {
		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				(playback ?
				 snd_hdspm_hw_rule_rate_out_channels :
				 snd_hdspm_hw_rule_rate_in_channels), hdspm,
				SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	}

	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			(playback ? snd_hdspm_hw_rule_out_channels :
			 snd_hdspm_hw_rule_in_channels), hdspm,
			SNDRV_PCM_HW_PARAM_CHANNELS, -1);

	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			(playback ? snd_hdspm_hw_rule_out_channels_rate :
			 snd_hdspm_hw_rule_in_channels_rate), hdspm,
			SNDRV_PCM_HW_PARAM_RATE, -1);

	return 0;
}

static int snd_hdspm_release(struct snd_pcm_substream *substream)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	spin_lock_irq(&hdspm->lock);

	if (playback) {
		hdspm->playback_pid = -1;
		hdspm->playback_substream = NULL;
	} else {
		hdspm->capture_pid = -1;
		hdspm->capture_substream = NULL;
	}

	spin_unlock_irq(&hdspm->lock);

	return 0;
}

static int snd_hdspm_hwdep_dummy_op(struct snd_hwdep *hw, struct file *file)
{
	/* we have nothing to initialize but the call is required */
	return 0;
}

static inline int copy_u32_le(void __user *dest, void __iomem *src)
{
	u32 val = readl(src);
	return copy_to_user(dest, &val, 4);
}

static int snd_hdspm_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct hdspm *hdspm = hw->private_data;
	struct hdspm_mixer_ioctl mixer;
	struct hdspm_config info;
	struct hdspm_status status;
	struct hdspm_version hdspm_version;
	struct hdspm_peak_rms *levels;
	struct hdspm_ltc ltc;
	unsigned int statusregister;
	long unsigned int s;
	int i = 0;

	switch (cmd) {

	case SNDRV_HDSPM_IOCTL_GET_PEAK_RMS:
		levels = &hdspm->peak_rms;
		for (i = 0; i < HDSPM_MAX_CHANNELS; i++) {
			levels->input_peaks[i] =
				readl(hdspm->iobase +
						HDSPM_MADI_INPUT_PEAK + i*4);
			levels->playback_peaks[i] =
				readl(hdspm->iobase +
						HDSPM_MADI_PLAYBACK_PEAK + i*4);
			levels->output_peaks[i] =
				readl(hdspm->iobase +
						HDSPM_MADI_OUTPUT_PEAK + i*4);

			levels->input_rms[i] =
				((uint64_t) readl(hdspm->iobase +
					HDSPM_MADI_INPUT_RMS_H + i*4) << 32) |
				(uint64_t) readl(hdspm->iobase +
						HDSPM_MADI_INPUT_RMS_L + i*4);
			levels->playback_rms[i] =
				((uint64_t)readl(hdspm->iobase +
					HDSPM_MADI_PLAYBACK_RMS_H+i*4) << 32) |
				(uint64_t)readl(hdspm->iobase +
					HDSPM_MADI_PLAYBACK_RMS_L + i*4);
			levels->output_rms[i] =
				((uint64_t)readl(hdspm->iobase +
					HDSPM_MADI_OUTPUT_RMS_H + i*4) << 32) |
				(uint64_t)readl(hdspm->iobase +
						HDSPM_MADI_OUTPUT_RMS_L + i*4);
		}

		if (hdspm->system_sample_rate > 96000) {
			levels->speed = qs;
		} else if (hdspm->system_sample_rate > 48000) {
			levels->speed = ds;
		} else {
			levels->speed = ss;
		}
		levels->status2 = hdspm_read(hdspm, HDSPM_statusRegister2);

		s = copy_to_user(argp, levels, sizeof(struct hdspm_peak_rms));
		if (0 != s) {
			/* dev_err(hdspm->card->dev, "copy_to_user(.., .., %lu): %lu
			 [Levels]\n", sizeof(struct hdspm_peak_rms), s);
			 */
			return -EFAULT;
		}
		break;

	case SNDRV_HDSPM_IOCTL_GET_LTC:
		ltc.ltc = hdspm_read(hdspm, HDSPM_RD_TCO);
		i = hdspm_read(hdspm, HDSPM_RD_TCO + 4);
		if (i & HDSPM_TCO1_LTC_Input_valid) {
			switch (i & (HDSPM_TCO1_LTC_Format_LSB |
				HDSPM_TCO1_LTC_Format_MSB)) {
			case 0:
				ltc.format = fps_24;
				break;
			case HDSPM_TCO1_LTC_Format_LSB:
				ltc.format = fps_25;
				break;
			case HDSPM_TCO1_LTC_Format_MSB:
				ltc.format = fps_2997;
				break;
			default:
				ltc.format = fps_30;
				break;
			}
			if (i & HDSPM_TCO1_set_drop_frame_flag) {
				ltc.frame = drop_frame;
			} else {
				ltc.frame = full_frame;
			}
		} else {
			ltc.format = format_invalid;
			ltc.frame = frame_invalid;
		}
		if (i & HDSPM_TCO1_Video_Input_Format_NTSC) {
			ltc.input_format = ntsc;
		} else if (i & HDSPM_TCO1_Video_Input_Format_PAL) {
			ltc.input_format = pal;
		} else {
			ltc.input_format = no_video;
		}

		s = copy_to_user(argp, &ltc, sizeof(struct hdspm_ltc));
		if (0 != s) {
			/*
			  dev_err(hdspm->card->dev, "copy_to_user(.., .., %lu): %lu [LTC]\n", sizeof(struct hdspm_ltc), s); */
			return -EFAULT;
		}

		break;

	case SNDRV_HDSPM_IOCTL_GET_CONFIG:

		memset(&info, 0, sizeof(info));
		spin_lock_irq(&hdspm->lock);
		info.pref_sync_ref = hdspm_pref_sync_ref(hdspm);
		info.wordclock_sync_check = hdspm_wc_sync_check(hdspm);

		info.system_sample_rate = hdspm->system_sample_rate;
		info.autosync_sample_rate =
			hdspm_external_sample_rate(hdspm);
		info.system_clock_mode = hdspm_system_clock_mode(hdspm);
		info.clock_source = hdspm_clock_source(hdspm);
		info.autosync_ref = hdspm_autosync_ref(hdspm);
		info.line_out = hdspm_toggle_setting(hdspm, HDSPM_LineOut);
		info.passthru = 0;
		spin_unlock_irq(&hdspm->lock);
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		break;

	case SNDRV_HDSPM_IOCTL_GET_STATUS:
		memset(&status, 0, sizeof(status));

		status.card_type = hdspm->io_type;

		status.autosync_source = hdspm_autosync_ref(hdspm);

		status.card_clock = 110069313433624ULL;
		status.master_period = hdspm_read(hdspm, HDSPM_RD_PLL_FREQ);

		switch (hdspm->io_type) {
		case MADI:
		case MADIface:
			status.card_specific.madi.sync_wc =
				hdspm_wc_sync_check(hdspm);
			status.card_specific.madi.sync_madi =
				hdspm_madi_sync_check(hdspm);
			status.card_specific.madi.sync_tco =
				hdspm_tco_sync_check(hdspm);
			status.card_specific.madi.sync_in =
				hdspm_sync_in_sync_check(hdspm);

			statusregister =
				hdspm_read(hdspm, HDSPM_statusRegister);
			status.card_specific.madi.madi_input =
				(statusregister & HDSPM_AB_int) ? 1 : 0;
			status.card_specific.madi.channel_format =
				(statusregister & HDSPM_RX_64ch) ? 1 : 0;
			/* TODO: Mac driver sets it when f_s>48kHz */
			status.card_specific.madi.frame_format = 0;

		default:
			break;
		}

		if (copy_to_user(argp, &status, sizeof(status)))
			return -EFAULT;


		break;

	case SNDRV_HDSPM_IOCTL_GET_VERSION:
		memset(&hdspm_version, 0, sizeof(hdspm_version));

		hdspm_version.card_type = hdspm->io_type;
		strlcpy(hdspm_version.cardname, hdspm->card_name,
				sizeof(hdspm_version.cardname));
		hdspm_version.serial = hdspm->serial;
		hdspm_version.firmware_rev = hdspm->firmware_rev;
		hdspm_version.addons = 0;
		if (hdspm->tco)
			hdspm_version.addons |= HDSPM_ADDON_TCO;

		if (copy_to_user(argp, &hdspm_version,
					sizeof(hdspm_version)))
			return -EFAULT;
		break;

	case SNDRV_HDSPM_IOCTL_GET_MIXER:
		if (copy_from_user(&mixer, argp, sizeof(mixer)))
			return -EFAULT;
		if (copy_to_user((void __user *)mixer.mixer, hdspm->mixer,
					sizeof(struct hdspm_mixer)))
			return -EFAULT;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static struct snd_pcm_ops snd_hdspm_ops = {
	.open = snd_hdspm_open,
	.close = snd_hdspm_release,
	.ioctl = snd_hdspm_ioctl,
	.hw_params = snd_hdspm_hw_params,
	.hw_free = snd_hdspm_hw_free,
	.prepare = snd_hdspm_prepare,
	.trigger = snd_hdspm_trigger,
	.pointer = snd_hdspm_hw_pointer,
	.page = snd_pcm_sgbuf_ops_page,
};

static int snd_hdspm_create_hwdep(struct snd_card *card,
				  struct hdspm *hdspm)
{
	struct snd_hwdep *hw;
	int err;

	err = snd_hwdep_new(card, "HDSPM hwdep", 0, &hw);
	if (err < 0)
		return err;

	hdspm->hwdep = hw;
	hw->private_data = hdspm;
	strcpy(hw->name, "HDSPM hwdep interface");

	hw->ops.open = snd_hdspm_hwdep_dummy_op;
	hw->ops.ioctl = snd_hdspm_hwdep_ioctl;
	hw->ops.ioctl_compat = snd_hdspm_hwdep_ioctl;
	hw->ops.release = snd_hdspm_hwdep_dummy_op;

	return 0;
}


/*------------------------------------------------------------
   memory interface
 ------------------------------------------------------------*/
static int snd_hdspm_preallocate_memory(struct hdspm *hdspm)
{
	int err;
	struct snd_pcm *pcm;
	size_t wanted;

	pcm = hdspm->pcm;

	wanted = HDSPM_DMA_AREA_BYTES;

	err =
	     snd_pcm_lib_preallocate_pages_for_all(pcm,
						   SNDRV_DMA_TYPE_DEV_SG,
						   snd_dma_pci_data(hdspm->pci),
						   wanted,
						   wanted);
	if (err < 0) {
		dev_dbg(hdspm->card->dev,
			"Could not preallocate %zd Bytes\n", wanted);

		return err;
	} else
		dev_dbg(hdspm->card->dev,
			" Preallocated %zd Bytes\n", wanted);

	return 0;
}


static void hdspm_set_sgbuf(struct hdspm *hdspm,
			    struct snd_pcm_substream *substream,
			     unsigned int reg, int channels)
{
	int i;

	/* continuous memory segment */
	for (i = 0; i < (channels * 16); i++)
		hdspm_write(hdspm, reg + 4 * i,
				snd_pcm_sgbuf_get_addr(substream, 4096 * i));
}


/* ------------- ALSA Devices ---------------------------- */
static int snd_hdspm_create_pcm(struct snd_card *card,
				struct hdspm *hdspm)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(card, hdspm->card_name, 0, 1, 1, &pcm);
	if (err < 0)
		return err;

	hdspm->pcm = pcm;
	pcm->private_data = hdspm;
	strcpy(pcm->name, hdspm->card_name);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_hdspm_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_hdspm_ops);

	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;

	err = snd_hdspm_preallocate_memory(hdspm);
	if (err < 0)
		return err;

	return 0;
}

static inline void snd_hdspm_initialize_midi_flush(struct hdspm * hdspm)
{
	int i;

	for (i = 0; i < hdspm->midiPorts; i++)
		snd_hdspm_flush_midi_input(hdspm, i);
}

static int snd_hdspm_create_alsa_devices(struct snd_card *card,
					 struct hdspm *hdspm)
{
	int err, i;

	dev_dbg(card->dev, "Create card...\n");
	err = snd_hdspm_create_pcm(card, hdspm);
	if (err < 0)
		return err;

	i = 0;
	while (i < hdspm->midiPorts) {
		err = snd_hdspm_create_midi(card, hdspm, i);
		if (err < 0) {
			return err;
		}
		i++;
	}

	err = snd_hdspm_create_controls(card, hdspm);
	if (err < 0)
		return err;

	err = snd_hdspm_create_hwdep(card, hdspm);
	if (err < 0)
		return err;

	dev_dbg(card->dev, "proc init...\n");
	snd_hdspm_proc_init(hdspm);

	hdspm->system_sample_rate = -1;
	hdspm->last_external_sample_rate = -1;
	hdspm->last_internal_sample_rate = -1;
	hdspm->playback_pid = -1;
	hdspm->capture_pid = -1;
	hdspm->capture_substream = NULL;
	hdspm->playback_substream = NULL;

	dev_dbg(card->dev, "Set defaults...\n");
	err = snd_hdspm_set_defaults(hdspm);
	if (err < 0)
		return err;

	dev_dbg(card->dev, "Update mixer controls...\n");
	hdspm_update_simple_mixer_controls(hdspm);

	dev_dbg(card->dev, "Initializeing complete ???\n");

	err = snd_card_register(card);
	if (err < 0) {
		dev_err(card->dev, "error registering card\n");
		return err;
	}

	dev_dbg(card->dev, "... yes now\n");

	return 0;
}

static int snd_hdspm_create(struct snd_card *card,
			    struct hdspm *hdspm)
{

	struct pci_dev *pci = hdspm->pci;
	int err;
	unsigned long io_extent;

	hdspm->irq = -1;
	hdspm->card = card;

	spin_lock_init(&hdspm->lock);

	pci_read_config_word(hdspm->pci,
			PCI_CLASS_REVISION, &hdspm->firmware_rev);

	strcpy(card->mixername, "Xilinx FPGA");
	strcpy(card->driver, "HDSPM");

	switch (hdspm->firmware_rev) {
	case HDSPM_RAYDAT_REV:
		hdspm->io_type = RayDAT;
		hdspm->card_name = "RME RayDAT";
		hdspm->midiPorts = 2;
		break;
	case HDSPM_AIO_REV:
		hdspm->io_type = AIO;
		hdspm->card_name = "RME AIO";
		hdspm->midiPorts = 1;
		break;
	case HDSPM_MADIFACE_REV:
		hdspm->io_type = MADIface;
		hdspm->card_name = "RME MADIface";
		hdspm->midiPorts = 1;
		break;
	default:
		if ((hdspm->firmware_rev == 0xf0) ||
			((hdspm->firmware_rev >= 0xe6) &&
					(hdspm->firmware_rev <= 0xea))) {
			hdspm->io_type = AES32;
			hdspm->card_name = "RME AES32";
			hdspm->midiPorts = 2;
		} else if ((hdspm->firmware_rev == 0xd2) ||
			((hdspm->firmware_rev >= 0xc8)  &&
				(hdspm->firmware_rev <= 0xcf))) {
			hdspm->io_type = MADI;
			hdspm->card_name = "RME MADI";
			hdspm->midiPorts = 3;
		} else {
			dev_err(card->dev,
				"unknown firmware revision %x\n",
				hdspm->firmware_rev);
			return -ENODEV;
		}
	}

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	pci_set_master(hdspm->pci);

	err = pci_request_regions(pci, "hdspm");
	if (err < 0)
		return err;

	hdspm->port = pci_resource_start(pci, 0);
	io_extent = pci_resource_len(pci, 0);

	dev_dbg(card->dev, "grabbed memory region 0x%lx-0x%lx\n",
			hdspm->port, hdspm->port + io_extent - 1);

	hdspm->iobase = ioremap_nocache(hdspm->port, io_extent);
	if (!hdspm->iobase) {
		dev_err(card->dev, "unable to remap region 0x%lx-0x%lx\n",
				hdspm->port, hdspm->port + io_extent - 1);
		return -EBUSY;
	}
	dev_dbg(card->dev, "remapped region (0x%lx) 0x%lx-0x%lx\n",
			(unsigned long)hdspm->iobase, hdspm->port,
			hdspm->port + io_extent - 1);

	if (request_irq(pci->irq, snd_hdspm_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, hdspm)) {
		dev_err(card->dev, "unable to use IRQ %d\n", pci->irq);
		return -EBUSY;
	}

	dev_dbg(card->dev, "use IRQ %d\n", pci->irq);

	hdspm->irq = pci->irq;

	dev_dbg(card->dev, "kmalloc Mixer memory of %zd Bytes\n",
			sizeof(struct hdspm_mixer));
	hdspm->mixer = kzalloc(sizeof(struct hdspm_mixer), GFP_KERNEL);
	if (!hdspm->mixer) {
		dev_err(card->dev,
			"unable to kmalloc Mixer memory of %d Bytes\n",
				(int)sizeof(struct hdspm_mixer));
		return -ENOMEM;
	}

	hdspm->port_names_in = NULL;
	hdspm->port_names_out = NULL;

	switch (hdspm->io_type) {
	case AES32:
		hdspm->ss_in_channels = hdspm->ss_out_channels = AES32_CHANNELS;
		hdspm->ds_in_channels = hdspm->ds_out_channels = AES32_CHANNELS;
		hdspm->qs_in_channels = hdspm->qs_out_channels = AES32_CHANNELS;

		hdspm->channel_map_in_ss = hdspm->channel_map_out_ss =
			channel_map_aes32;
		hdspm->channel_map_in_ds = hdspm->channel_map_out_ds =
			channel_map_aes32;
		hdspm->channel_map_in_qs = hdspm->channel_map_out_qs =
			channel_map_aes32;
		hdspm->port_names_in_ss = hdspm->port_names_out_ss =
			texts_ports_aes32;
		hdspm->port_names_in_ds = hdspm->port_names_out_ds =
			texts_ports_aes32;
		hdspm->port_names_in_qs = hdspm->port_names_out_qs =
			texts_ports_aes32;

		hdspm->max_channels_out = hdspm->max_channels_in =
			AES32_CHANNELS;
		hdspm->port_names_in = hdspm->port_names_out =
			texts_ports_aes32;
		hdspm->channel_map_in = hdspm->channel_map_out =
			channel_map_aes32;

		break;

	case MADI:
	case MADIface:
		hdspm->ss_in_channels = hdspm->ss_out_channels =
			MADI_SS_CHANNELS;
		hdspm->ds_in_channels = hdspm->ds_out_channels =
			MADI_DS_CHANNELS;
		hdspm->qs_in_channels = hdspm->qs_out_channels =
			MADI_QS_CHANNELS;

		hdspm->channel_map_in_ss = hdspm->channel_map_out_ss =
			channel_map_unity_ss;
		hdspm->channel_map_in_ds = hdspm->channel_map_out_ds =
			channel_map_unity_ss;
		hdspm->channel_map_in_qs = hdspm->channel_map_out_qs =
			channel_map_unity_ss;

		hdspm->port_names_in_ss = hdspm->port_names_out_ss =
			texts_ports_madi;
		hdspm->port_names_in_ds = hdspm->port_names_out_ds =
			texts_ports_madi;
		hdspm->port_names_in_qs = hdspm->port_names_out_qs =
			texts_ports_madi;
		break;

	case AIO:
		hdspm->ss_in_channels = AIO_IN_SS_CHANNELS;
		hdspm->ds_in_channels = AIO_IN_DS_CHANNELS;
		hdspm->qs_in_channels = AIO_IN_QS_CHANNELS;
		hdspm->ss_out_channels = AIO_OUT_SS_CHANNELS;
		hdspm->ds_out_channels = AIO_OUT_DS_CHANNELS;
		hdspm->qs_out_channels = AIO_OUT_QS_CHANNELS;

		if (0 == (hdspm_read(hdspm, HDSPM_statusRegister2) & HDSPM_s2_AEBI_D)) {
			dev_info(card->dev, "AEB input board found\n");
			hdspm->ss_in_channels += 4;
			hdspm->ds_in_channels += 4;
			hdspm->qs_in_channels += 4;
		}

		if (0 == (hdspm_read(hdspm, HDSPM_statusRegister2) & HDSPM_s2_AEBO_D)) {
			dev_info(card->dev, "AEB output board found\n");
			hdspm->ss_out_channels += 4;
			hdspm->ds_out_channels += 4;
			hdspm->qs_out_channels += 4;
		}

		hdspm->channel_map_out_ss = channel_map_aio_out_ss;
		hdspm->channel_map_out_ds = channel_map_aio_out_ds;
		hdspm->channel_map_out_qs = channel_map_aio_out_qs;

		hdspm->channel_map_in_ss = channel_map_aio_in_ss;
		hdspm->channel_map_in_ds = channel_map_aio_in_ds;
		hdspm->channel_map_in_qs = channel_map_aio_in_qs;

		hdspm->port_names_in_ss = texts_ports_aio_in_ss;
		hdspm->port_names_out_ss = texts_ports_aio_out_ss;
		hdspm->port_names_in_ds = texts_ports_aio_in_ds;
		hdspm->port_names_out_ds = texts_ports_aio_out_ds;
		hdspm->port_names_in_qs = texts_ports_aio_in_qs;
		hdspm->port_names_out_qs = texts_ports_aio_out_qs;

		break;

	case RayDAT:
		hdspm->ss_in_channels = hdspm->ss_out_channels =
			RAYDAT_SS_CHANNELS;
		hdspm->ds_in_channels = hdspm->ds_out_channels =
			RAYDAT_DS_CHANNELS;
		hdspm->qs_in_channels = hdspm->qs_out_channels =
			RAYDAT_QS_CHANNELS;

		hdspm->max_channels_in = RAYDAT_SS_CHANNELS;
		hdspm->max_channels_out = RAYDAT_SS_CHANNELS;

		hdspm->channel_map_in_ss = hdspm->channel_map_out_ss =
			channel_map_raydat_ss;
		hdspm->channel_map_in_ds = hdspm->channel_map_out_ds =
			channel_map_raydat_ds;
		hdspm->channel_map_in_qs = hdspm->channel_map_out_qs =
			channel_map_raydat_qs;
		hdspm->channel_map_in = hdspm->channel_map_out =
			channel_map_raydat_ss;

		hdspm->port_names_in_ss = hdspm->port_names_out_ss =
			texts_ports_raydat_ss;
		hdspm->port_names_in_ds = hdspm->port_names_out_ds =
			texts_ports_raydat_ds;
		hdspm->port_names_in_qs = hdspm->port_names_out_qs =
			texts_ports_raydat_qs;


		break;

	}

	/* TCO detection */
	switch (hdspm->io_type) {
	case AIO:
	case RayDAT:
		if (hdspm_read(hdspm, HDSPM_statusRegister2) &
				HDSPM_s2_tco_detect) {
			hdspm->midiPorts++;
			hdspm->tco = kzalloc(sizeof(struct hdspm_tco),
					GFP_KERNEL);
			if (NULL != hdspm->tco) {
				hdspm_tco_write(hdspm);
			}
			dev_info(card->dev, "AIO/RayDAT TCO module found\n");
		} else {
			hdspm->tco = NULL;
		}
		break;

	case MADI:
	case AES32:
		if (hdspm_read(hdspm, HDSPM_statusRegister) & HDSPM_tco_detect) {
			hdspm->midiPorts++;
			hdspm->tco = kzalloc(sizeof(struct hdspm_tco),
					GFP_KERNEL);
			if (NULL != hdspm->tco) {
				hdspm_tco_write(hdspm);
			}
			dev_info(card->dev, "MADI/AES TCO module found\n");
		} else {
			hdspm->tco = NULL;
		}
		break;

	default:
		hdspm->tco = NULL;
	}

	/* texts */
	switch (hdspm->io_type) {
	case AES32:
		if (hdspm->tco) {
			hdspm->texts_autosync = texts_autosync_aes_tco;
			hdspm->texts_autosync_items =
				ARRAY_SIZE(texts_autosync_aes_tco);
		} else {
			hdspm->texts_autosync = texts_autosync_aes;
			hdspm->texts_autosync_items =
				ARRAY_SIZE(texts_autosync_aes);
		}
		break;

	case MADI:
		if (hdspm->tco) {
			hdspm->texts_autosync = texts_autosync_madi_tco;
			hdspm->texts_autosync_items = 4;
		} else {
			hdspm->texts_autosync = texts_autosync_madi;
			hdspm->texts_autosync_items = 3;
		}
		break;

	case MADIface:

		break;

	case RayDAT:
		if (hdspm->tco) {
			hdspm->texts_autosync = texts_autosync_raydat_tco;
			hdspm->texts_autosync_items = 9;
		} else {
			hdspm->texts_autosync = texts_autosync_raydat;
			hdspm->texts_autosync_items = 8;
		}
		break;

	case AIO:
		if (hdspm->tco) {
			hdspm->texts_autosync = texts_autosync_aio_tco;
			hdspm->texts_autosync_items = 6;
		} else {
			hdspm->texts_autosync = texts_autosync_aio;
			hdspm->texts_autosync_items = 5;
		}
		break;

	}

	tasklet_init(&hdspm->midi_tasklet,
			hdspm_midi_tasklet, (unsigned long) hdspm);


	if (hdspm->io_type != MADIface) {
		hdspm->serial = (hdspm_read(hdspm,
				HDSPM_midiStatusIn0)>>8) & 0xFFFFFF;
		/* id contains either a user-provided value or the default
		 * NULL. If it's the default, we're safe to
		 * fill card->id with the serial number.
		 *
		 * If the serial number is 0xFFFFFF, then we're dealing with
		 * an old PCI revision that comes without a sane number. In
		 * this case, we don't set card->id to avoid collisions
		 * when running with multiple cards.
		 */
		if (NULL == id[hdspm->dev] && hdspm->serial != 0xFFFFFF) {
			sprintf(card->id, "HDSPMx%06x", hdspm->serial);
			snd_card_set_id(card, card->id);
		}
	}

	dev_dbg(card->dev, "create alsa devices.\n");
	err = snd_hdspm_create_alsa_devices(card, hdspm);
	if (err < 0)
		return err;

	snd_hdspm_initialize_midi_flush(hdspm);

	return 0;
}


static int snd_hdspm_free(struct hdspm * hdspm)
{

	if (hdspm->port) {

		/* stop th audio, and cancel all interrupts */
		hdspm->control_register &=
		    ~(HDSPM_Start | HDSPM_AudioInterruptEnable |
		      HDSPM_Midi0InterruptEnable | HDSPM_Midi1InterruptEnable |
		      HDSPM_Midi2InterruptEnable | HDSPM_Midi3InterruptEnable);
		hdspm_write(hdspm, HDSPM_controlRegister,
			    hdspm->control_register);
	}

	if (hdspm->irq >= 0)
		free_irq(hdspm->irq, (void *) hdspm);

	kfree(hdspm->mixer);
	iounmap(hdspm->iobase);

	if (hdspm->port)
		pci_release_regions(hdspm->pci);

	pci_disable_device(hdspm->pci);
	return 0;
}


static void snd_hdspm_card_free(struct snd_card *card)
{
	struct hdspm *hdspm = card->private_data;

	if (hdspm)
		snd_hdspm_free(hdspm);
}


static int snd_hdspm_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	static int dev;
	struct hdspm *hdspm;
	struct snd_card *card;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev],
			   THIS_MODULE, sizeof(struct hdspm), &card);
	if (err < 0)
		return err;

	hdspm = card->private_data;
	card->private_free = snd_hdspm_card_free;
	hdspm->dev = dev;
	hdspm->pci = pci;

	err = snd_hdspm_create(card, hdspm);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	if (hdspm->io_type != MADIface) {
		sprintf(card->shortname, "%s_%x",
			hdspm->card_name,
			hdspm->serial);
		sprintf(card->longname, "%s S/N 0x%x at 0x%lx, irq %d",
			hdspm->card_name,
			hdspm->serial,
			hdspm->port, hdspm->irq);
	} else {
		sprintf(card->shortname, "%s", hdspm->card_name);
		sprintf(card->longname, "%s at 0x%lx, irq %d",
				hdspm->card_name, hdspm->port, hdspm->irq);
	}

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);

	dev++;
	return 0;
}

static void snd_hdspm_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

static struct pci_driver hdspm_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_hdspm_ids,
	.probe = snd_hdspm_probe,
	.remove = snd_hdspm_remove,
};

module_pci_driver(hdspm_driver);
