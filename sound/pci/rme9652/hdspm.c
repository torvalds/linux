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
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/math64.h>
#include <asm/io.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/asoundef.h>
#include <sound/rawmidi.h>
#include <sound/hwdep.h>
#include <sound/initval.h>

#include <sound/hdspm.h>

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	  /* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	  /* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;/* Enable this card */

/* Disable precise pointer at start */
static int precise_ptr[SNDRV_CARDS];

/* Send all playback to line outs */
static int line_outs_monitor[SNDRV_CARDS];

/* Enable Analog Outs on Channel 63/64 by default */
static int enable_monitor[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for RME HDSPM interface.");

module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for RME HDSPM interface.");

module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable/disable specific HDSPM soundcards.");

module_param_array(precise_ptr, bool, NULL, 0444);
MODULE_PARM_DESC(precise_ptr, "Enable or disable precise pointer.");

module_param_array(line_outs_monitor, bool, NULL, 0444);
MODULE_PARM_DESC(line_outs_monitor,
		 "Send playback streams to analog outs by default.");

module_param_array(enable_monitor, bool, NULL, 0444);
MODULE_PARM_DESC(enable_monitor,
		 "Enable Analog Out on Channel 63/64 by default.");

MODULE_AUTHOR
      ("Winfried Ritsch <ritsch_AT_iem.at>, "
       "Paul Davis <paul@linuxaudiosystems.com>, "
       "Marcus Andersson, Thomas Charbonnel <thomas@undata.org>, "
       "Remy Bruno <remy.bruno@trinnov.com>");
MODULE_DESCRIPTION("RME HDSPM");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{RME HDSPM-MADI}}");

/* --- Write registers. --- 
  These are defined as byte-offsets from the iobase value.  */

#define HDSPM_controlRegister	     64
#define HDSPM_interruptConfirmation  96
#define HDSPM_control2Reg	     256  /* not in specs ???????? */
#define HDSPM_freqReg                256  /* for AES32 */
#define HDSPM_midiDataOut0  	     352  /* just believe in old code */
#define HDSPM_midiDataOut1  	     356
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

#define HDSPM_midiDataIn0     360
#define HDSPM_midiDataIn1     364

/* status is data bytes in MIDI-FIFO (0-128) */
#define HDSPM_midiStatusOut0  384	
#define HDSPM_midiStatusOut1  388	
#define HDSPM_midiStatusIn0   392	
#define HDSPM_midiStatusIn1   396	


/* the meters are regular i/o-mapped registers, but offset
   considerably from the rest. the peak registers are reset
   when read; the least-significant 4 bits are full-scale counters; 
   the actual peak value is in the most-significant 24 bits.
*/
#define HDSPM_MADI_peakrmsbase 	4096	/* 4096-8191 2x64x32Bit Meters */

/* --- Control Register bits --------- */
#define HDSPM_Start                (1<<0) /* start engine */

#define HDSPM_Latency0             (1<<1) /* buffer size = 2^n */
#define HDSPM_Latency1             (1<<2) /* where n is defined */
#define HDSPM_Latency2             (1<<3) /* by Latency{2,1,0} */

#define HDSPM_ClockModeMaster      (1<<4) /* 1=Master, 0=Slave/Autosync */

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

#define HDSPM_SyncRef0     (1<<16) /* 0=WOrd, 1=MADI */
#define HDSPM_SyncRef1     (1<<17) /* for AES32: SyncRefN codes the AES # */
#define HDSPM_SyncRef2     (1<<13)
#define HDSPM_SyncRef3     (1<<25)

#define HDSPM_SMUX         (1<<18) /* Frame ??? */ /* MADI ONY */
#define HDSPM_clr_tms      (1<<19) /* clear track marker, do not use 
                                      AES additional bits in
				      lower 5 Audiodatabits ??? */
#define HDSPM_taxi_reset   (1<<20) /* ??? */ /* MADI ONLY ? */
#define HDSPM_WCK48        (1<<20) /* Frame ??? = HDSPM_SMUX */ /* AES32 ONLY */

#define HDSPM_Midi0InterruptEnable (1<<22)
#define HDSPM_Midi1InterruptEnable (1<<23)

#define HDSPM_LineOut (1<<24) /* Analog Out on channel 63/64 on=1, mute=0 */

#define HDSPM_DS_DoubleWire (1<<26) /* AES32 ONLY */
#define HDSPM_QS_DoubleWire (1<<27) /* AES32 ONLY */
#define HDSPM_QS_QuadWire   (1<<28) /* AES32 ONLY */

#define HDSPM_wclk_sel (1<<30)

/* --- bit helper defines */
#define HDSPM_LatencyMask    (HDSPM_Latency0|HDSPM_Latency1|HDSPM_Latency2)
#define HDSPM_FrequencyMask  (HDSPM_Frequency0|HDSPM_Frequency1|\
			      HDSPM_DoubleSpeed|HDSPM_QuadSpeed)
#define HDSPM_InputMask      (HDSPM_InputSelect0|HDSPM_InputSelect1)
#define HDSPM_InputOptical   0
#define HDSPM_InputCoaxial   (HDSPM_InputSelect0)
#define HDSPM_SyncRefMask    (HDSPM_SyncRef0|HDSPM_SyncRef1|\
			      HDSPM_SyncRef2|HDSPM_SyncRef3)
#define HDSPM_SyncRef_Word   0
#define HDSPM_SyncRef_MADI   (HDSPM_SyncRef0)

#define HDSPM_SYNC_FROM_WORD 0	/* Preferred sync reference */
#define HDSPM_SYNC_FROM_MADI 1	/* choices - used by "pref_sync_ref" */

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

/* --- for internal discrimination */
#define HDSPM_CLOCK_SOURCE_AUTOSYNC          0	/* Sample Clock Sources */
#define HDSPM_CLOCK_SOURCE_INTERNAL_32KHZ    1
#define HDSPM_CLOCK_SOURCE_INTERNAL_44_1KHZ  2
#define HDSPM_CLOCK_SOURCE_INTERNAL_48KHZ    3
#define HDSPM_CLOCK_SOURCE_INTERNAL_64KHZ    4
#define HDSPM_CLOCK_SOURCE_INTERNAL_88_2KHZ  5
#define HDSPM_CLOCK_SOURCE_INTERNAL_96KHZ    6
#define HDSPM_CLOCK_SOURCE_INTERNAL_128KHZ   7
#define HDSPM_CLOCK_SOURCE_INTERNAL_176_4KHZ 8
#define HDSPM_CLOCK_SOURCE_INTERNAL_192KHZ   9

/* Synccheck Status */
#define HDSPM_SYNC_CHECK_NO_LOCK 0
#define HDSPM_SYNC_CHECK_LOCK    1
#define HDSPM_SYNC_CHECK_SYNC	 2

/* AutoSync References - used by "autosync_ref" control switch */
#define HDSPM_AUTOSYNC_FROM_WORD      0
#define HDSPM_AUTOSYNC_FROM_MADI      1
#define HDSPM_AUTOSYNC_FROM_NONE      2

/* Possible sources of MADI input */
#define HDSPM_OPTICAL 0		/* optical   */
#define HDSPM_COAXIAL 1		/* BNC */

#define hdspm_encode_latency(x)       (((x)<<1) & HDSPM_LatencyMask)
#define hdspm_decode_latency(x)       (((x) & HDSPM_LatencyMask)>>1)

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

#define HDSPM_BufferPositionMask 0x000FFC0 /* Bit 6..15 : h/w buffer pointer */
                                           /* since 64byte accurate last 6 bits 
                                              are not used */

#define HDSPM_madiSync          (1<<18) /* MADI is in sync */
#define HDSPM_DoubleSpeedStatus (1<<19) /* (input) card in double speed */

#define HDSPM_madiFreq0         (1<<22)	/* system freq 0=error */
#define HDSPM_madiFreq1         (1<<23)	/* 1=32, 2=44.1 3=48 */
#define HDSPM_madiFreq2         (1<<24)	/* 4=64, 5=88.2 6=96 */
#define HDSPM_madiFreq3         (1<<25)	/* 7=128, 8=176.4 9=192 */

#define HDSPM_BufferID          (1<<26)	/* (Double)Buffer ID toggles with
					 * Interrupt
					 */
#define HDSPM_midi0IRQPending   (1<<30)	/* MIDI IRQ is pending  */
#define HDSPM_midi1IRQPending   (1<<31)	/* and aktiv */

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

#define HDSPM_version0 (1<<0)	/* not realy defined but I guess */
#define HDSPM_version1 (1<<1)	/* in former cards it was ??? */
#define HDSPM_version2 (1<<2)

#define HDSPM_wcLock (1<<3)	/* Wordclock is detected and locked */
#define HDSPM_wcSync (1<<4)	/* Wordclock is in sync with systemclock */

#define HDSPM_wc_freq0 (1<<5)	/* input freq detected via autosync  */
#define HDSPM_wc_freq1 (1<<6)	/* 001=32, 010==44.1, 011=48, */
#define HDSPM_wc_freq2 (1<<7)	/* 100=64, 101=88.2, 110=96, */
/* missing Bit   for               111=128, 1000=176.4, 1001=192 */

#define HDSPM_SelSyncRef0 (1<<8)	/* Sync Source in slave mode */
#define HDSPM_SelSyncRef1 (1<<9)	/* 000=word, 001=MADI, */
#define HDSPM_SelSyncRef2 (1<<10)	/* 111=no valid signal */

#define HDSPM_wc_valid (HDSPM_wcLock|HDSPM_wcSync)

#define HDSPM_wcFreqMask  (HDSPM_wc_freq0|HDSPM_wc_freq1|HDSPM_wc_freq2)
#define HDSPM_wcFreq32    (HDSPM_wc_freq0)
#define HDSPM_wcFreq44_1  (HDSPM_wc_freq1)
#define HDSPM_wcFreq48    (HDSPM_wc_freq0|HDSPM_wc_freq1)
#define HDSPM_wcFreq64    (HDSPM_wc_freq2)
#define HDSPM_wcFreq88_2  (HDSPM_wc_freq0|HDSPM_wc_freq2)
#define HDSPM_wcFreq96    (HDSPM_wc_freq1|HDSPM_wc_freq2)


#define HDSPM_SelSyncRefMask       (HDSPM_SelSyncRef0|HDSPM_SelSyncRef1|\
				    HDSPM_SelSyncRef2)
#define HDSPM_SelSyncRef_WORD      0
#define HDSPM_SelSyncRef_MADI      (HDSPM_SelSyncRef0)
#define HDSPM_SelSyncRef_NVALID    (HDSPM_SelSyncRef0|HDSPM_SelSyncRef1|\
				    HDSPM_SelSyncRef2)

/*
   For AES32, bits for status, status2 and timecode are different
*/
/* status */
#define HDSPM_AES32_wcLock	0x0200000
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
#define HDSPM_AES32_AUTOSYNC_FROM_NONE 9

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

/* revisions >= 230 indicate AES32 card */
#define HDSPM_AESREVISION 230

/* speed factor modes */
#define HDSPM_SPEED_SINGLE 0
#define HDSPM_SPEED_DOUBLE 1
#define HDSPM_SPEED_QUAD   2
/* names for speed modes */
static char *hdspm_speed_names[] = { "single", "double", "quad" };

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
};

struct hdspm {
        spinlock_t lock;
	/* only one playback and/or capture stream */
        struct snd_pcm_substream *capture_substream;
        struct snd_pcm_substream *playback_substream;

	char *card_name;	     /* for procinfo */
	unsigned short firmware_rev; /* dont know if relevant (yes if AES32)*/

	unsigned char is_aes32;    /* indicates if card is AES32 */

	int precise_ptr;	/* use precise pointers, to be tested */
	int monitor_outs;	/* set up monitoring outs init flag */

	u32 control_register;	/* cached value */
	u32 control2_register;	/* cached value */

	struct hdspm_midi midi[2];
	struct tasklet_struct midi_tasklet;

	size_t period_bytes;
	unsigned char ss_channels;	/* channels of card in single speed */
	unsigned char ds_channels;	/* Double Speed */
	unsigned char qs_channels;	/* Quad Speed */

	unsigned char *playback_buffer;	/* suitably aligned address */
	unsigned char *capture_buffer;	/* suitably aligned address */

	pid_t capture_pid;	/* process id which uses capture */
	pid_t playback_pid;	/* process id which uses capture */
	int running;		/* running status */

	int last_external_sample_rate;	/* samplerate mystic ... */
	int last_internal_sample_rate;
	int system_sample_rate;

	char *channel_map;	/* channel map for DS and Quadspeed */

	int dev;		/* Hardware vars... */
	int irq;
	unsigned long port;
	void __iomem *iobase;

	int irq_count;		/* for debug */

	struct snd_card *card;	/* one card */
	struct snd_pcm *pcm;		/* has one pcm */
	struct snd_hwdep *hwdep;	/* and a hwdep for additional ioctl */
	struct pci_dev *pci;	/* and an pci info */

	/* Mixer vars */
	/* fast alsa mixer */
	struct snd_kcontrol *playback_mixer_ctls[HDSPM_MAX_CHANNELS];
	/* but input to much, so not used */
	struct snd_kcontrol *input_mixer_ctls[HDSPM_MAX_CHANNELS];
	/* full mixer accessable over mixer ioctl or hwdep-device */
	struct hdspm_mixer *mixer;

};

/* These tables map the ALSA channels 1..N to the channels that we
   need to use in order to find the relevant channel buffer. RME
   refer to this kind of mapping as between "the ADAT channel and
   the DMA channel." We index it using the logical audio channel,
   and the value is the DMA channel (i.e. channel buffer number)
   where the data for that channel can be read/written from/to.
*/

static char channel_map_madi_ss[HDSPM_MAX_CHANNELS] = {
   0, 1, 2, 3, 4, 5, 6, 7,
   8, 9, 10, 11, 12, 13, 14, 15,
   16, 17, 18, 19, 20, 21, 22, 23,
   24, 25, 26, 27, 28, 29, 30, 31,
   32, 33, 34, 35, 36, 37, 38, 39,
   40, 41, 42, 43, 44, 45, 46, 47,
   48, 49, 50, 51, 52, 53, 54, 55,
   56, 57, 58, 59, 60, 61, 62, 63
};


static DEFINE_PCI_DEVICE_TABLE(snd_hdspm_ids) = {
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
static int __devinit snd_hdspm_create_alsa_devices(struct snd_card *card,
						   struct hdspm * hdspm);
static int __devinit snd_hdspm_create_pcm(struct snd_card *card,
					  struct hdspm * hdspm);

static inline void snd_hdspm_initialize_midi_flush(struct hdspm * hdspm);
static int hdspm_update_simple_mixer_controls(struct hdspm * hdspm);
static int hdspm_autosync_ref(struct hdspm * hdspm);
static int snd_hdspm_set_defaults(struct hdspm * hdspm);
static void hdspm_set_sgbuf(struct hdspm * hdspm,
			    struct snd_pcm_substream *substream,
			     unsigned int reg, int channels);

static inline int HDSPM_bit2freq(int n)
{
	static const int bit2freq_tab[] = {
		0, 32000, 44100, 48000, 64000, 88200,
		96000, 128000, 176400, 192000 };
	if (n < 1 || n > 9)
		return 0;
	return bit2freq_tab[n];
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

/* check for external sample rate */
static int hdspm_external_sample_rate(struct hdspm *hdspm)
{
	if (hdspm->is_aes32) {
		unsigned int status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
		unsigned int status = hdspm_read(hdspm, HDSPM_statusRegister);
		unsigned int timecode =
			hdspm_read(hdspm, HDSPM_timecodeRegister);

		int syncref = hdspm_autosync_ref(hdspm);

		if (syncref == HDSPM_AES32_AUTOSYNC_FROM_WORD &&
				status & HDSPM_AES32_wcLock)
			return HDSPM_bit2freq((status >> HDSPM_AES32_wcFreq_bit)
					      & 0xF);
		if (syncref >= HDSPM_AES32_AUTOSYNC_FROM_AES1 &&
			syncref <= HDSPM_AES32_AUTOSYNC_FROM_AES8 &&
			status2 & (HDSPM_LockAES >>
			          (syncref - HDSPM_AES32_AUTOSYNC_FROM_AES1)))
			return HDSPM_bit2freq((timecode >>
			  (4*(syncref-HDSPM_AES32_AUTOSYNC_FROM_AES1))) & 0xF);
		return 0;
	} else {
		unsigned int status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
		unsigned int status = hdspm_read(hdspm, HDSPM_statusRegister);
		unsigned int rate_bits;
		int rate = 0;

		/* if wordclock has synced freq and wordclock is valid */
		if ((status2 & HDSPM_wcLock) != 0 &&
				(status & HDSPM_SelSyncRef0) == 0) {

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
				/* Quadspeed Bit missing ???? */
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
			return rate;

		/* maby a madi input (which is taken if sel sync is madi) */
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
		}
		return rate;
	}
}

/* Latency function */
static inline void hdspm_compute_period_size(struct hdspm * hdspm)
{
	hdspm->period_bytes =
	    1 << ((hdspm_decode_latency(hdspm->control_register) + 8));
}

static snd_pcm_uframes_t hdspm_hw_pointer(struct hdspm * hdspm)
{
	int position;

	position = hdspm_read(hdspm, HDSPM_statusRegister);

	if (!hdspm->precise_ptr)
		return (position & HDSPM_BufferID) ?
			(hdspm->period_bytes / 4) : 0;

	/* hwpointer comes in bytes and is 64Bytes accurate (by docu since
	   PCI Burst)
	   i have experimented that it is at most 64 Byte to much for playing 
	   so substraction of 64 byte should be ok for ALSA, but use it only
	   for application where you know what you do since if you come to
	   near with record pointer it can be a disaster */

	position &= HDSPM_BufferPositionMask;
	position = ((position - 64) % (2 * hdspm->period_bytes)) / 4;

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

static int hdspm_set_interrupt_interval(struct hdspm * s, unsigned int frames)
{
	int n;

	spin_lock_irq(&s->lock);

	frames >>= 7;
	n = 0;
	while (frames) {
		n++;
		frames >>= 1;
	}
	s->control_register &= ~HDSPM_LatencyMask;
	s->control_register |= hdspm_encode_latency(n);

	hdspm_write(s, HDSPM_controlRegister, s->control_register);

	hdspm_compute_period_size(s);

	spin_unlock_irq(&s->lock);

	return 0;
}

static void hdspm_set_dds_value(struct hdspm *hdspm, int rate)
{
	u64 n;
	
	if (rate >= 112000)
		rate /= 4;
	else if (rate >= 56000)
		rate /= 2;

	/* RME says n = 104857600000000, but in the windows MADI driver, I see:
//	return 104857600000000 / rate; // 100 MHz
	return 110100480000000 / rate; // 105 MHz
        */	   
	/* n = 104857600000000ULL; */ /*  =  2^20 * 10^8 */
	n = 110100480000000ULL;    /* Value checked for AES32 and MADI */
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
    
			snd_printk(KERN_WARNING "HDSPM: "
				   "Warning: device is not running "
				   "as a clock master.\n");
			not_set = 1;
		} else {

			/* hw_param request while in AutoSync mode */
			int external_freq =
			    hdspm_external_sample_rate(hdspm);

			if (hdspm_autosync_ref(hdspm) ==
			    HDSPM_AUTOSYNC_FROM_NONE) {

				snd_printk(KERN_WARNING "HDSPM: "
					   "Detected no Externel Sync \n");
				not_set = 1;

			} else if (rate != external_freq) {

				snd_printk(KERN_WARNING "HDSPM: "
					   "Warning: No AutoSync source for "
					   "requested rate\n");
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
		snd_printk
		    (KERN_ERR "HDSPM: "
		     "cannot change from %s speed to %s speed mode "
		     "(capture PID = %d, playback PID = %d)\n",
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
	
	if (hdspm->is_aes32 && rate != current_rate)
		hdspm_write(hdspm, HDSPM_eeprom_wr, 0);
	
	/* For AES32 and for MADI (at least rev 204), channel_map needs to
	 * always be channel_map_madi_ss, whatever the sample rate */
	hdspm->channel_map = channel_map_madi_ss;

	hdspm->system_sample_rate = rate;

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
	if (id)
		return hdspm_read(hdspm, HDSPM_midiDataIn1);
	else
		return hdspm_read(hdspm, HDSPM_midiDataIn0);
}

static inline void snd_hdspm_midi_write_byte (struct hdspm *hdspm, int id,
					      int val)
{
	/* the hardware already does the relevant bit-mask with 0xff */
	if (id)
		hdspm_write(hdspm, HDSPM_midiDataOut1, val);
	else
		hdspm_write(hdspm, HDSPM_midiDataOut0, val);
}

static inline int snd_hdspm_midi_input_available (struct hdspm *hdspm, int id)
{
	if (id)
		return (hdspm_read(hdspm, HDSPM_midiStatusIn1) & 0xff);
	else
		return (hdspm_read(hdspm, HDSPM_midiStatusIn0) & 0xff);
}

static inline int snd_hdspm_midi_output_possible (struct hdspm *hdspm, int id)
{
	int fifo_bytes_used;

	if (id)
		fifo_bytes_used = hdspm_read(hdspm, HDSPM_midiStatusOut1);
	else
		fifo_bytes_used = hdspm_read(hdspm, HDSPM_midiStatusOut0);
	fifo_bytes_used &= 0xff;

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
	if (hmidi->id)
		hmidi->hdspm->control_register |= HDSPM_Midi1InterruptEnable;
	else
		hmidi->hdspm->control_register |= HDSPM_Midi0InterruptEnable;
	hdspm_write(hmidi->hdspm, HDSPM_controlRegister,
		    hmidi->hdspm->control_register);
	spin_unlock_irqrestore (&hmidi->lock, flags);
	return snd_hdspm_midi_output_write (hmidi);
}

static void
snd_hdspm_midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct hdspm *hdspm;
	struct hdspm_midi *hmidi;
	unsigned long flags;
	u32 ie;

	hmidi = substream->rmidi->private_data;
	hdspm = hmidi->hdspm;
	ie = hmidi->id ?
		HDSPM_Midi1InterruptEnable : HDSPM_Midi0InterruptEnable;
	spin_lock_irqsave (&hdspm->lock, flags);
	if (up) {
		if (!(hdspm->control_register & ie)) {
			snd_hdspm_flush_midi_input (hdspm, hmidi->id);
			hdspm->control_register |= ie;
		}
	} else {
		hdspm->control_register &= ~ie;
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

	if (hmidi->istimer) {
		hmidi->timer.expires = 1 + jiffies;
		add_timer(&hmidi->timer);
	}

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
			init_timer(&hmidi->timer);
			hmidi->timer.function = snd_hdspm_midi_output_timer;
			hmidi->timer.data = (unsigned long) hmidi;
			hmidi->timer.expires = 1 + jiffies;
			add_timer(&hmidi->timer);
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

static int __devinit snd_hdspm_create_midi (struct snd_card *card,
					    struct hdspm *hdspm, int id)
{
	int err;
	char buf[32];

	hdspm->midi[id].id = id;
	hdspm->midi[id].hdspm = hdspm;
	spin_lock_init (&hdspm->midi[id].lock);

	sprintf (buf, "%s MIDI %d", card->shortname, id+1);
	err = snd_rawmidi_new (card, buf, id, 1, 1, &hdspm->midi[id].rmidi);
	if (err < 0)
		return err;

	sprintf(hdspm->midi[id].rmidi->name, "HDSPM MIDI %d", id+1);
	hdspm->midi[id].rmidi->private_data = &hdspm->midi[id];

	snd_rawmidi_set_ops(hdspm->midi[id].rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &snd_hdspm_midi_output);
	snd_rawmidi_set_ops(hdspm->midi[id].rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &snd_hdspm_midi_input);

	hdspm->midi[id].rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
		SNDRV_RAWMIDI_INFO_INPUT |
		SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}


static void hdspm_midi_tasklet(unsigned long arg)
{
	struct hdspm *hdspm = (struct hdspm *)arg;
	
	if (hdspm->midi[0].pending)
		snd_hdspm_midi_input_read (&hdspm->midi[0]);
	if (hdspm->midi[1].pending)
		snd_hdspm_midi_input_read (&hdspm->midi[1]);
} 


/*-----------------------------------------------------------------------------
  Status Interface
  ----------------------------------------------------------------------------*/

/* get the system sample rate which is set */

#define HDSPM_SYSTEM_SAMPLE_RATE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdspm_info_system_sample_rate, \
  .get = snd_hdspm_get_system_sample_rate \
}

static int snd_hdspm_info_system_sample_rate(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	return 0;
}

static int snd_hdspm_get_system_sample_rate(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *
					    ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm->system_sample_rate;
	return 0;
}

#define HDSPM_AUTOSYNC_SAMPLE_RATE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdspm_info_autosync_sample_rate, \
  .get = snd_hdspm_get_autosync_sample_rate \
}

static int snd_hdspm_info_autosync_sample_rate(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = { "32000", "44100", "48000",
		"64000", "88200", "96000",
		"128000", "176400", "192000",
		"None"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 10;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdspm_get_autosync_sample_rate(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value *
					      ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	switch (hdspm_external_sample_rate(hdspm)) {
	case 32000:
		ucontrol->value.enumerated.item[0] = 0;
		break;
	case 44100:
		ucontrol->value.enumerated.item[0] = 1;
		break;
	case 48000:
		ucontrol->value.enumerated.item[0] = 2;
		break;
	case 64000:
		ucontrol->value.enumerated.item[0] = 3;
		break;
	case 88200:
		ucontrol->value.enumerated.item[0] = 4;
		break;
	case 96000:
		ucontrol->value.enumerated.item[0] = 5;
		break;
	case 128000:
		ucontrol->value.enumerated.item[0] = 6;
		break;
	case 176400:
		ucontrol->value.enumerated.item[0] = 7;
		break;
	case 192000:
		ucontrol->value.enumerated.item[0] = 8;
		break;

	default:
		ucontrol->value.enumerated.item[0] = 9;
	}
	return 0;
}

#define HDSPM_SYSTEM_CLOCK_MODE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdspm_info_system_clock_mode, \
  .get = snd_hdspm_get_system_clock_mode, \
}



static int hdspm_system_clock_mode(struct hdspm * hdspm)
{
        /* Always reflect the hardware info, rme is never wrong !!!! */

	if (hdspm->control_register & HDSPM_ClockModeMaster)
		return 0;
	return 1;
}

static int snd_hdspm_info_system_clock_mode(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = { "Master", "Slave" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdspm_get_system_clock_mode(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] =
	    hdspm_system_clock_mode(hdspm);
	return 0;
}

#define HDSPM_CLOCK_SOURCE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_clock_source, \
  .get = snd_hdspm_get_clock_source, \
  .put = snd_hdspm_put_clock_source \
}

static int hdspm_clock_source(struct hdspm * hdspm)
{
	if (hdspm->control_register & HDSPM_ClockModeMaster) {
		switch (hdspm->system_sample_rate) {
		case 32000:
			return 1;
		case 44100:
			return 2;
		case 48000:
			return 3;
		case 64000:
			return 4;
		case 88200:
			return 5;
		case 96000:
			return 6;
		case 128000:
			return 7;
		case 176400:
			return 8;
		case 192000:
			return 9;
		default:
			return 3;
		}
	} else {
		return 0;
	}
}

static int hdspm_set_clock_source(struct hdspm * hdspm, int mode)
{
	int rate;
	switch (mode) {

	case HDSPM_CLOCK_SOURCE_AUTOSYNC:
		if (hdspm_external_sample_rate(hdspm) != 0) {
			hdspm->control_register &= ~HDSPM_ClockModeMaster;
			hdspm_write(hdspm, HDSPM_controlRegister,
				    hdspm->control_register);
			return 0;
		}
		return -1;
	case HDSPM_CLOCK_SOURCE_INTERNAL_32KHZ:
		rate = 32000;
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_44_1KHZ:
		rate = 44100;
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_48KHZ:
		rate = 48000;
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_64KHZ:
		rate = 64000;
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_88_2KHZ:
		rate = 88200;
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_96KHZ:
		rate = 96000;
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_128KHZ:
		rate = 128000;
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_176_4KHZ:
		rate = 176400;
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_192KHZ:
		rate = 192000;
		break;

	default:
		rate = 44100;
	}
	hdspm->control_register |= HDSPM_ClockModeMaster;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);
	hdspm_set_rate(hdspm, rate, 1);
	return 0;
}

static int snd_hdspm_info_clock_source(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = { "AutoSync",
		"Internal 32.0 kHz", "Internal 44.1 kHz",
		    "Internal 48.0 kHz",
		"Internal 64.0 kHz", "Internal 88.2 kHz",
		    "Internal 96.0 kHz",
		"Internal 128.0 kHz", "Internal 176.4 kHz",
		    "Internal 192.0 kHz"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 10;

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;

	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);

	return 0;
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
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_pref_sync_ref, \
  .get = snd_hdspm_get_pref_sync_ref, \
  .put = snd_hdspm_put_pref_sync_ref \
}

static int hdspm_pref_sync_ref(struct hdspm * hdspm)
{
	/* Notice that this looks at the requested sync source,
	   not the one actually in use.
	 */
	if (hdspm->is_aes32) {
		switch (hdspm->control_register & HDSPM_SyncRefMask) {
		/* number gives AES index, except for 0 which
		   corresponds to WordClock */
		case 0: return 0;
		case HDSPM_SyncRef0: return 1;
		case HDSPM_SyncRef1: return 2;
		case HDSPM_SyncRef1+HDSPM_SyncRef0: return 3;
		case HDSPM_SyncRef2: return 4;
		case HDSPM_SyncRef2+HDSPM_SyncRef0: return 5;
		case HDSPM_SyncRef2+HDSPM_SyncRef1: return 6;
		case HDSPM_SyncRef2+HDSPM_SyncRef1+HDSPM_SyncRef0: return 7;
		case HDSPM_SyncRef3: return 8;
		}
	} else {
		switch (hdspm->control_register & HDSPM_SyncRefMask) {
		case HDSPM_SyncRef_Word:
			return HDSPM_SYNC_FROM_WORD;
		case HDSPM_SyncRef_MADI:
			return HDSPM_SYNC_FROM_MADI;
		}
	}

	return HDSPM_SYNC_FROM_WORD;
}

static int hdspm_set_pref_sync_ref(struct hdspm * hdspm, int pref)
{
	hdspm->control_register &= ~HDSPM_SyncRefMask;

	if (hdspm->is_aes32) {
		switch (pref) {
		case 0:
		       hdspm->control_register |= 0;
		       break;
		case 1:
		       hdspm->control_register |= HDSPM_SyncRef0;
		       break;
		case 2:
		       hdspm->control_register |= HDSPM_SyncRef1;
		       break;
		case 3:
		       hdspm->control_register |= HDSPM_SyncRef1+HDSPM_SyncRef0;
		       break;
		case 4:
		       hdspm->control_register |= HDSPM_SyncRef2;
		       break;
		case 5:
		       hdspm->control_register |= HDSPM_SyncRef2+HDSPM_SyncRef0;
		       break;
		case 6:
		       hdspm->control_register |= HDSPM_SyncRef2+HDSPM_SyncRef1;
		       break;
		case 7:
		       hdspm->control_register |=
			       HDSPM_SyncRef2+HDSPM_SyncRef1+HDSPM_SyncRef0;
		       break;
		case 8:
		       hdspm->control_register |= HDSPM_SyncRef3;
		       break;
		default:
		       return -1;
		}
	} else {
		switch (pref) {
		case HDSPM_SYNC_FROM_MADI:
			hdspm->control_register |= HDSPM_SyncRef_MADI;
			break;
		case HDSPM_SYNC_FROM_WORD:
			hdspm->control_register |= HDSPM_SyncRef_Word;
			break;
		default:
			return -1;
		}
	}
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);
	return 0;
}

static int snd_hdspm_info_pref_sync_ref(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (hdspm->is_aes32) {
		static char *texts[] = { "Word", "AES1", "AES2", "AES3",
			"AES4", "AES5",	"AES6", "AES7", "AES8" };

		uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
		uinfo->count = 1;

		uinfo->value.enumerated.items = 9;

		if (uinfo->value.enumerated.item >=
		    uinfo->value.enumerated.items)
			uinfo->value.enumerated.item =
				uinfo->value.enumerated.items - 1;
		strcpy(uinfo->value.enumerated.name,
				texts[uinfo->value.enumerated.item]);
	} else {
		static char *texts[] = { "Word", "MADI" };

		uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
		uinfo->count = 1;

		uinfo->value.enumerated.items = 2;

		if (uinfo->value.enumerated.item >=
		    uinfo->value.enumerated.items)
			uinfo->value.enumerated.item =
				uinfo->value.enumerated.items - 1;
		strcpy(uinfo->value.enumerated.name,
				texts[uinfo->value.enumerated.item]);
	}
	return 0;
}

static int snd_hdspm_get_pref_sync_ref(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm_pref_sync_ref(hdspm);
	return 0;
}

static int snd_hdspm_put_pref_sync_ref(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change, max;
	unsigned int val;

	max = hdspm->is_aes32 ? 9 : 2;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;

	val = ucontrol->value.enumerated.item[0] % max;

	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_pref_sync_ref(hdspm);
	hdspm_set_pref_sync_ref(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_AUTOSYNC_REF(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdspm_info_autosync_ref, \
  .get = snd_hdspm_get_autosync_ref, \
}

static int hdspm_autosync_ref(struct hdspm * hdspm)
{
	if (hdspm->is_aes32) {
		unsigned int status = hdspm_read(hdspm, HDSPM_statusRegister);
		unsigned int syncref = (status >> HDSPM_AES32_syncref_bit) &
			0xF;
		if (syncref == 0)
			return HDSPM_AES32_AUTOSYNC_FROM_WORD;
		if (syncref <= 8)
			return syncref;
		return HDSPM_AES32_AUTOSYNC_FROM_NONE;
	} else {
		/* This looks at the autosync selected sync reference */
		unsigned int status2 = hdspm_read(hdspm, HDSPM_statusRegister2);

		switch (status2 & HDSPM_SelSyncRefMask) {
		case HDSPM_SelSyncRef_WORD:
			return HDSPM_AUTOSYNC_FROM_WORD;
		case HDSPM_SelSyncRef_MADI:
			return HDSPM_AUTOSYNC_FROM_MADI;
		case HDSPM_SelSyncRef_NVALID:
			return HDSPM_AUTOSYNC_FROM_NONE;
		default:
			return 0;
		}

		return 0;
	}
}

static int snd_hdspm_info_autosync_ref(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	if (hdspm->is_aes32) {
		static char *texts[] = { "WordClock", "AES1", "AES2", "AES3",
			"AES4",	"AES5", "AES6", "AES7", "AES8", "None"};

		uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
		uinfo->count = 1;
		uinfo->value.enumerated.items = 10;
		if (uinfo->value.enumerated.item >=
		    uinfo->value.enumerated.items)
			uinfo->value.enumerated.item =
				uinfo->value.enumerated.items - 1;
		strcpy(uinfo->value.enumerated.name,
				texts[uinfo->value.enumerated.item]);
	} else {
		static char *texts[] = { "WordClock", "MADI", "None" };

		uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
		uinfo->count = 1;
		uinfo->value.enumerated.items = 3;
		if (uinfo->value.enumerated.item >=
		    uinfo->value.enumerated.items)
			uinfo->value.enumerated.item =
				uinfo->value.enumerated.items - 1;
		strcpy(uinfo->value.enumerated.name,
				texts[uinfo->value.enumerated.item]);
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

#define HDSPM_LINE_OUT(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_line_out, \
  .get = snd_hdspm_get_line_out, \
  .put = snd_hdspm_put_line_out \
}

static int hdspm_line_out(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_LineOut) ? 1 : 0;
}


static int hdspm_set_line_output(struct hdspm * hdspm, int out)
{
	if (out)
		hdspm->control_register |= HDSPM_LineOut;
	else
		hdspm->control_register &= ~HDSPM_LineOut;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

#define snd_hdspm_info_line_out		snd_ctl_boolean_mono_info

static int snd_hdspm_get_line_out(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.integer.value[0] = hdspm_line_out(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_line_out(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_line_out(hdspm);
	hdspm_set_line_output(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_TX_64(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_tx_64, \
  .get = snd_hdspm_get_tx_64, \
  .put = snd_hdspm_put_tx_64 \
}

static int hdspm_tx_64(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_TX_64ch) ? 1 : 0;
}

static int hdspm_set_tx_64(struct hdspm * hdspm, int out)
{
	if (out)
		hdspm->control_register |= HDSPM_TX_64ch;
	else
		hdspm->control_register &= ~HDSPM_TX_64ch;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

#define snd_hdspm_info_tx_64		snd_ctl_boolean_mono_info

static int snd_hdspm_get_tx_64(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.integer.value[0] = hdspm_tx_64(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_tx_64(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_tx_64(hdspm);
	hdspm_set_tx_64(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_C_TMS(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_c_tms, \
  .get = snd_hdspm_get_c_tms, \
  .put = snd_hdspm_put_c_tms \
}

static int hdspm_c_tms(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_clr_tms) ? 1 : 0;
}

static int hdspm_set_c_tms(struct hdspm * hdspm, int out)
{
	if (out)
		hdspm->control_register |= HDSPM_clr_tms;
	else
		hdspm->control_register &= ~HDSPM_clr_tms;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

#define snd_hdspm_info_c_tms		snd_ctl_boolean_mono_info

static int snd_hdspm_get_c_tms(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.integer.value[0] = hdspm_c_tms(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_c_tms(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_c_tms(hdspm);
	hdspm_set_c_tms(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_SAFE_MODE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_safe_mode, \
  .get = snd_hdspm_get_safe_mode, \
  .put = snd_hdspm_put_safe_mode \
}

static int hdspm_safe_mode(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_AutoInp) ? 1 : 0;
}

static int hdspm_set_safe_mode(struct hdspm * hdspm, int out)
{
	if (out)
		hdspm->control_register |= HDSPM_AutoInp;
	else
		hdspm->control_register &= ~HDSPM_AutoInp;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

#define snd_hdspm_info_safe_mode	snd_ctl_boolean_mono_info

static int snd_hdspm_get_safe_mode(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.integer.value[0] = hdspm_safe_mode(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_safe_mode(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_safe_mode(hdspm);
	hdspm_set_safe_mode(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_EMPHASIS(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_emphasis, \
  .get = snd_hdspm_get_emphasis, \
  .put = snd_hdspm_put_emphasis \
}

static int hdspm_emphasis(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_Emphasis) ? 1 : 0;
}

static int hdspm_set_emphasis(struct hdspm * hdspm, int emp)
{
	if (emp)
		hdspm->control_register |= HDSPM_Emphasis;
	else
		hdspm->control_register &= ~HDSPM_Emphasis;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

#define snd_hdspm_info_emphasis		snd_ctl_boolean_mono_info

static int snd_hdspm_get_emphasis(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.enumerated.item[0] = hdspm_emphasis(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_emphasis(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_emphasis(hdspm);
	hdspm_set_emphasis(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_DOLBY(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_dolby, \
  .get = snd_hdspm_get_dolby, \
  .put = snd_hdspm_put_dolby \
}

static int hdspm_dolby(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_Dolby) ? 1 : 0;
}

static int hdspm_set_dolby(struct hdspm * hdspm, int dol)
{
	if (dol)
		hdspm->control_register |= HDSPM_Dolby;
	else
		hdspm->control_register &= ~HDSPM_Dolby;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

#define snd_hdspm_info_dolby		snd_ctl_boolean_mono_info

static int snd_hdspm_get_dolby(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.enumerated.item[0] = hdspm_dolby(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_dolby(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_dolby(hdspm);
	hdspm_set_dolby(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_PROFESSIONAL(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdspm_info_professional, \
  .get = snd_hdspm_get_professional, \
  .put = snd_hdspm_put_professional \
}

static int hdspm_professional(struct hdspm * hdspm)
{
	return (hdspm->control_register & HDSPM_Professional) ? 1 : 0;
}

static int hdspm_set_professional(struct hdspm * hdspm, int dol)
{
	if (dol)
		hdspm->control_register |= HDSPM_Professional;
	else
		hdspm->control_register &= ~HDSPM_Professional;
	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

	return 0;
}

#define snd_hdspm_info_professional	snd_ctl_boolean_mono_info

static int snd_hdspm_get_professional(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.enumerated.item[0] = hdspm_professional(hdspm);
	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_put_professional(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdspm->lock);
	change = (int) val != hdspm_professional(hdspm);
	hdspm_set_professional(hdspm, val);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_INPUT_SELECT(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
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
	static char *texts[] = { "optical", "coaxial" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);

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
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
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
	static char *texts[] = { "Single", "Double" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);

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
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
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
	static char *texts[] = { "Single", "Double", "Quad" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);

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

/*           Simple Mixer
  deprecated since to much faders ???
  MIXER interface says output (source, destination, value)
   where source > MAX_channels are playback channels 
   on MADICARD 
  - playback mixer matrix: [channelout+64] [output] [value]
  - input(thru) mixer matrix: [channelin] [output] [value]
  (better do 2 kontrols for separation ?)
*/

#define HDSPM_MIXER(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
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
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
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
	uinfo->value.integer.max = 65536;
	uinfo->value.integer.step = 1;
	return 0;
}

static int snd_hdspm_get_playback_mixer(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int channel;
	int mapped_channel;

	channel = ucontrol->id.index - 1;

	if (snd_BUG_ON(channel < 0 || channel >= HDSPM_MAX_CHANNELS))
		return -EINVAL;

	mapped_channel = hdspm->channel_map[channel];
	if (mapped_channel < 0)
		return -EINVAL;

	spin_lock_irq(&hdspm->lock);
	ucontrol->value.integer.value[0] =
	    hdspm_read_pb_gain(hdspm, mapped_channel, mapped_channel);
	spin_unlock_irq(&hdspm->lock);

	/*
	snd_printdd("get pb mixer index %d, channel %d, mapped_channel %d, "
		    "value %d\n",
		    ucontrol->id.index, channel, mapped_channel,
		    ucontrol->value.integer.value[0]); 
	*/
	return 0;
}

static int snd_hdspm_put_playback_mixer(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);
	int change;
	int channel;
	int mapped_channel;
	int gain;

	if (!snd_hdspm_use_is_exclusive(hdspm))
		return -EBUSY;

	channel = ucontrol->id.index - 1;

	if (snd_BUG_ON(channel < 0 || channel >= HDSPM_MAX_CHANNELS))
		return -EINVAL;

	mapped_channel = hdspm->channel_map[channel];
	if (mapped_channel < 0)
		return -EINVAL;

	gain = ucontrol->value.integer.value[0];

	spin_lock_irq(&hdspm->lock);
	change =
	    gain != hdspm_read_pb_gain(hdspm, mapped_channel,
				       mapped_channel);
	if (change)
		hdspm_write_pb_gain(hdspm, mapped_channel, mapped_channel,
				    gain);
	spin_unlock_irq(&hdspm->lock);
	return change;
}

#define HDSPM_WC_SYNC_CHECK(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdspm_info_sync_check, \
  .get = snd_hdspm_get_wc_sync_check \
}

static int snd_hdspm_info_sync_check(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = { "No Lock", "Lock", "Sync" };
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int hdspm_wc_sync_check(struct hdspm * hdspm)
{
	if (hdspm->is_aes32) {
		int status = hdspm_read(hdspm, HDSPM_statusRegister);
		if (status & HDSPM_AES32_wcLock) {
			/* I don't know how to differenciate sync from lock.
			   Doing as if sync for now */
			return 2;
		}
		return 0;
	} else {
		int status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
		if (status2 & HDSPM_wcLock) {
			if (status2 & HDSPM_wcSync)
				return 2;
			else
				return 1;
		}
		return 0;
	}
}

static int snd_hdspm_get_wc_sync_check(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdspm_wc_sync_check(hdspm);
	return 0;
}


#define HDSPM_MADI_SYNC_CHECK(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdspm_info_sync_check, \
  .get = snd_hdspm_get_madisync_sync_check \
}

static int hdspm_madisync_sync_check(struct hdspm * hdspm)
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

static int snd_hdspm_get_madisync_sync_check(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *
					     ucontrol)
{
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] =
	    hdspm_madisync_sync_check(hdspm);
	return 0;
}


#define HDSPM_AES_SYNC_CHECK(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdspm_info_sync_check, \
  .get = snd_hdspm_get_aes_sync_check \
}

static int hdspm_aes_sync_check(struct hdspm * hdspm, int idx)
{
	int status2 = hdspm_read(hdspm, HDSPM_statusRegister2);
	if (status2 & (HDSPM_LockAES >> idx)) {
		/* I don't know how to differenciate sync from lock.
		   Doing as if sync for now */
		return 2;
	}
	return 0;
}

static int snd_hdspm_get_aes_sync_check(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	int offset;
	struct hdspm *hdspm = snd_kcontrol_chip(kcontrol);

	offset = ucontrol->id.index - 1;
	if (offset < 0 || offset >= 8)
		return -EINVAL;

	ucontrol->value.enumerated.item[0] =
		hdspm_aes_sync_check(hdspm, offset);
	return 0;
}


static struct snd_kcontrol_new snd_hdspm_controls_madi[] = {

	HDSPM_MIXER("Mixer", 0),
/* 'Sample Clock Source' complies with the alsa control naming scheme */
	HDSPM_CLOCK_SOURCE("Sample Clock Source", 0),

	HDSPM_SYSTEM_CLOCK_MODE("System Clock Mode", 0),
	HDSPM_PREF_SYNC_REF("Preferred Sync Reference", 0),
	HDSPM_AUTOSYNC_REF("AutoSync Reference", 0),
	HDSPM_SYSTEM_SAMPLE_RATE("System Sample Rate", 0),
/* 'External Rate' complies with the alsa control naming scheme */
	HDSPM_AUTOSYNC_SAMPLE_RATE("External Rate", 0),
	HDSPM_WC_SYNC_CHECK("Word Clock Lock Status", 0),
	HDSPM_MADI_SYNC_CHECK("MADI Sync Lock Status", 0),
	HDSPM_LINE_OUT("Line Out", 0),
	HDSPM_TX_64("TX 64 channels mode", 0),
	HDSPM_C_TMS("Clear Track Marker", 0),
	HDSPM_SAFE_MODE("Safe Mode", 0),
	HDSPM_INPUT_SELECT("Input Select", 0),
};

static struct snd_kcontrol_new snd_hdspm_controls_aes32[] = {

	HDSPM_MIXER("Mixer", 0),
/* 'Sample Clock Source' complies with the alsa control naming scheme */
	HDSPM_CLOCK_SOURCE("Sample Clock Source", 0),

	HDSPM_SYSTEM_CLOCK_MODE("System Clock Mode", 0),
	HDSPM_PREF_SYNC_REF("Preferred Sync Reference", 0),
	HDSPM_AUTOSYNC_REF("AutoSync Reference", 0),
	HDSPM_SYSTEM_SAMPLE_RATE("System Sample Rate", 0),
/* 'External Rate' complies with the alsa control naming scheme */
	HDSPM_AUTOSYNC_SAMPLE_RATE("External Rate", 0),
	HDSPM_WC_SYNC_CHECK("Word Clock Lock Status", 0),
/*	HDSPM_AES_SYNC_CHECK("AES Lock Status", 0),*/ /* created in snd_hdspm_create_controls() */
	HDSPM_LINE_OUT("Line Out", 0),
	HDSPM_EMPHASIS("Emphasis", 0),
	HDSPM_DOLBY("Non Audio", 0),
	HDSPM_PROFESSIONAL("Professional", 0),
	HDSPM_C_TMS("Clear Track Marker", 0),
	HDSPM_DS_WIRE("Double Speed Wire Mode", 0),
	HDSPM_QS_WIRE("Quad Speed Wire Mode", 0),
};

static struct snd_kcontrol_new snd_hdspm_playback_mixer = HDSPM_PLAYBACK_MIXER;


static int hdspm_update_simple_mixer_controls(struct hdspm * hdspm)
{
	int i;

	for (i = hdspm->ds_channels; i < hdspm->ss_channels; ++i) {
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


static int snd_hdspm_create_controls(struct snd_card *card, struct hdspm * hdspm)
{
	unsigned int idx, limit;
	int err;
	struct snd_kcontrol *kctl;

	/* add control list first */
	if (hdspm->is_aes32) {
		struct snd_kcontrol_new aes_sync_ctl =
			HDSPM_AES_SYNC_CHECK("AES Lock Status", 0);

		for (idx = 0; idx < ARRAY_SIZE(snd_hdspm_controls_aes32);
		     idx++) {
			err = snd_ctl_add(card,
					  snd_ctl_new1(&snd_hdspm_controls_aes32[idx],
						       hdspm));
			if (err < 0)
				return err;
		}
		for (idx = 1; idx <= 8; idx++) {
			aes_sync_ctl.index = idx;
			err = snd_ctl_add(card,
					  snd_ctl_new1(&aes_sync_ctl, hdspm));
			if (err < 0)
				return err;
		}
	} else {
		for (idx = 0; idx < ARRAY_SIZE(snd_hdspm_controls_madi);
		     idx++) {
			err = snd_ctl_add(card,
					  snd_ctl_new1(&snd_hdspm_controls_madi[idx],
						       hdspm));
			if (err < 0)
				return err;
		}
	}

	/* Channel playback mixer as default control 
	   Note: the whole matrix would be 128*HDSPM_MIXER_CHANNELS Faders,
	   thats too * big for any alsamixer they are accesible via special
	   IOCTL on hwdep and the mixer 2dimensional mixer control
	*/

	snd_hdspm_playback_mixer.name = "Chn";
	limit = HDSPM_MAX_CHANNELS;

	/* The index values are one greater than the channel ID so that
	 * alsamixer will display them correctly. We want to use the index
	 * for fast lookup of the relevant channel, but if we use it at all,
	 * most ALSA software does the wrong thing with it ...
	 */

	for (idx = 0; idx < limit; ++idx) {
		snd_hdspm_playback_mixer.index = idx + 1;
		kctl = snd_ctl_new1(&snd_hdspm_playback_mixer, hdspm);
		err = snd_ctl_add(card, kctl);
		if (err < 0)
			return err;
		hdspm->playback_mixer_ctls[idx] = kctl;
	}

	return 0;
}

/*------------------------------------------------------------
   /proc interface 
 ------------------------------------------------------------*/

static void
snd_hdspm_proc_read_madi(struct snd_info_entry * entry,
			 struct snd_info_buffer *buffer)
{
	struct hdspm *hdspm = entry->private_data;
	unsigned int status;
	unsigned int status2;
	char *pref_sync_ref;
	char *autosync_ref;
	char *system_clock_mode;
	char *clock_source;
	char *insel;
	char *syncref;
	int x, x2;

	status = hdspm_read(hdspm, HDSPM_statusRegister);
	status2 = hdspm_read(hdspm, HDSPM_statusRegister2);

	snd_iprintf(buffer, "%s (Card #%d) Rev.%x Status2first3bits: %x\n",
		    hdspm->card_name, hdspm->card->number + 1,
		    hdspm->firmware_rev,
		    (status2 & HDSPM_version0) |
		    (status2 & HDSPM_version1) | (status2 &
						  HDSPM_version2));

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
		    "Register: ctrl1=0x%x, ctrl2=0x%x, status1=0x%x, "
		    "status2=0x%x\n",
		    hdspm->control_register, hdspm->control2_register,
		    status, status2);

	snd_iprintf(buffer, "--- Settings ---\n");

	x = 1 << (6 + hdspm_decode_latency(hdspm->control_register &
					   HDSPM_LatencyMask));

	snd_iprintf(buffer,
		    "Size (Latency): %d samples (2 periods of %lu bytes)\n",
		    x, (unsigned long) hdspm->period_bytes);

	snd_iprintf(buffer, "Line out: %s,   Precise Pointer: %s\n",
		    (hdspm->control_register & HDSPM_LineOut) ? "on " : "off",
		    (hdspm->precise_ptr) ? "on" : "off");

	switch (hdspm->control_register & HDSPM_InputMask) {
	case HDSPM_InputOptical:
		insel = "Optical";
		break;
	case HDSPM_InputCoaxial:
		insel = "Coaxial";
		break;
	default:
		insel = "Unknown";
	}

	switch (hdspm->control_register & HDSPM_SyncRefMask) {
	case HDSPM_SyncRef_Word:
		syncref = "WordClock";
		break;
	case HDSPM_SyncRef_MADI:
		syncref = "MADI";
		break;
	default:
		syncref = "Unknown";
	}
	snd_iprintf(buffer, "Inputsel = %s, SyncRef = %s\n", insel,
		    syncref);

	snd_iprintf(buffer,
		    "ClearTrackMarker = %s, Transmit in %s Channel Mode, "
		    "Auto Input %s\n",
		    (hdspm->
		     control_register & HDSPM_clr_tms) ? "on" : "off",
		    (hdspm->
		     control_register & HDSPM_TX_64ch) ? "64" : "56",
		    (hdspm->
		     control_register & HDSPM_AutoInp) ? "on" : "off");

	switch (hdspm_clock_source(hdspm)) {
	case HDSPM_CLOCK_SOURCE_AUTOSYNC:
		clock_source = "AutoSync";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_32KHZ:
		clock_source = "Internal 32 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_44_1KHZ:
		clock_source = "Internal 44.1 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_48KHZ:
		clock_source = "Internal 48 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_64KHZ:
		clock_source = "Internal 64 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_88_2KHZ:
		clock_source = "Internal 88.2 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_96KHZ:
		clock_source = "Internal 96 kHz";
		break;
	default:
		clock_source = "Error";
	}
	snd_iprintf(buffer, "Sample Clock Source: %s\n", clock_source);
	if (!(hdspm->control_register & HDSPM_ClockModeMaster))
		system_clock_mode = "Slave";
	else
		system_clock_mode = "Master";
	snd_iprintf(buffer, "System Clock Mode: %s\n", system_clock_mode);

	switch (hdspm_pref_sync_ref(hdspm)) {
	case HDSPM_SYNC_FROM_WORD:
		pref_sync_ref = "Word Clock";
		break;
	case HDSPM_SYNC_FROM_MADI:
		pref_sync_ref = "MADI Sync";
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
	int pref_syncref;
	char *autosync_ref;
	char *system_clock_mode;
	char *clock_source;
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
		    "Register: ctrl1=0x%x, status1=0x%x, status2=0x%x, "
		    "timecode=0x%x\n",
		    hdspm->control_register,
		    status, status2, timecode);

	snd_iprintf(buffer, "--- Settings ---\n");

	x = 1 << (6 + hdspm_decode_latency(hdspm->control_register &
					   HDSPM_LatencyMask));

	snd_iprintf(buffer,
		    "Size (Latency): %d samples (2 periods of %lu bytes)\n",
		    x, (unsigned long) hdspm->period_bytes);

	snd_iprintf(buffer, "Line out: %s,   Precise Pointer: %s\n",
		    (hdspm->
		     control_register & HDSPM_LineOut) ? "on " : "off",
		    (hdspm->precise_ptr) ? "on" : "off");

	snd_iprintf(buffer,
		    "ClearTrackMarker %s, Emphasis %s, Dolby %s\n",
		    (hdspm->
		     control_register & HDSPM_clr_tms) ? "on" : "off",
		    (hdspm->
		     control_register & HDSPM_Emphasis) ? "on" : "off",
		    (hdspm->
		     control_register & HDSPM_Dolby) ? "on" : "off");

	switch (hdspm_clock_source(hdspm)) {
	case HDSPM_CLOCK_SOURCE_AUTOSYNC:
		clock_source = "AutoSync";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_32KHZ:
		clock_source = "Internal 32 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_44_1KHZ:
		clock_source = "Internal 44.1 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_48KHZ:
		clock_source = "Internal 48 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_64KHZ:
		clock_source = "Internal 64 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_88_2KHZ:
		clock_source = "Internal 88.2 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_96KHZ:
		clock_source = "Internal 96 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_128KHZ:
		clock_source = "Internal 128 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_176_4KHZ:
		clock_source = "Internal 176.4 kHz";
		break;
	case HDSPM_CLOCK_SOURCE_INTERNAL_192KHZ:
		clock_source = "Internal 192 kHz";
		break;
	default:
		clock_source = "Error";
	}
	snd_iprintf(buffer, "Sample Clock Source: %s\n", clock_source);
	if (!(hdspm->control_register & HDSPM_ClockModeMaster))
		system_clock_mode = "Slave";
	else
		system_clock_mode = "Master";
	snd_iprintf(buffer, "System Clock Mode: %s\n", system_clock_mode);

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

	snd_iprintf(buffer, "Word: %s  Frequency: %d\n",
		    (status & HDSPM_AES32_wcLock)? "Sync   " : "No Lock",
		    HDSPM_bit2freq((status >> HDSPM_AES32_wcFreq_bit) & 0xF));

	for (x = 0; x < 8; x++) {
		snd_iprintf(buffer, "AES%d: %s  Frequency: %d\n",
			    x+1,
			    (status2 & (HDSPM_LockAES >> x)) ?
			    "Sync   ": "No Lock",
			    HDSPM_bit2freq((timecode >> (4*x)) & 0xF));
	}

	switch (hdspm_autosync_ref(hdspm)) {
	case HDSPM_AES32_AUTOSYNC_FROM_NONE: autosync_ref="None"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_WORD: autosync_ref="Word Clock"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES1: autosync_ref="AES1"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES2: autosync_ref="AES2"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES3: autosync_ref="AES3"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES4: autosync_ref="AES4"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES5: autosync_ref="AES5"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES6: autosync_ref="AES6"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES7: autosync_ref="AES7"; break;
	case HDSPM_AES32_AUTOSYNC_FROM_AES8: autosync_ref="AES8"; break;
	default: autosync_ref = "---"; break;
	}
	snd_iprintf(buffer, "AutoSync ref = %s\n", autosync_ref);

	snd_iprintf(buffer, "\n");
}

#ifdef CONFIG_SND_DEBUG
static void
snd_hdspm_proc_read_debug(struct snd_info_entry * entry,
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



static void __devinit snd_hdspm_proc_init(struct hdspm * hdspm)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(hdspm->card, "hdspm", &entry))
		snd_info_set_text_ops(entry, hdspm,
				      hdspm->is_aes32 ?
				      snd_hdspm_proc_read_aes32 :
				      snd_hdspm_proc_read_madi);
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
	unsigned int i;

	/* ASSUMPTION: hdspm->lock is either held, or there is no need to
	   hold it (e.g. during module initialization).
	 */

	/* set defaults:       */

	if (hdspm->is_aes32)
		hdspm->control_register =
			HDSPM_ClockModeMaster |	/* Master Cloack Mode on */
			hdspm_encode_latency(7) | /* latency maximum =
						   * 8192 samples
						   */
			HDSPM_SyncRef0 |	/* AES1 is syncclock */
			HDSPM_LineOut |	/* Analog output in */
			HDSPM_Professional;  /* Professional mode */
	else
		hdspm->control_register =
			HDSPM_ClockModeMaster |	/* Master Cloack Mode on */
			hdspm_encode_latency(7) | /* latency maximum =
						   * 8192 samples
						   */
			HDSPM_InputCoaxial |	/* Input Coax not Optical */
			HDSPM_SyncRef_MADI |	/* Madi is syncclock */
			HDSPM_LineOut |	/* Analog output in */
			HDSPM_TX_64ch |	/* transmit in 64ch mode */
			HDSPM_AutoInp;	/* AutoInput chossing (takeover) */

	/* ! HDSPM_Frequency0|HDSPM_Frequency1 = 44.1khz */
	/* !  HDSPM_DoubleSpeed HDSPM_QuadSpeed = normal speed */
	/* ! HDSPM_clr_tms = do not clear bits in track marks */

	hdspm_write(hdspm, HDSPM_controlRegister, hdspm->control_register);

        if (!hdspm->is_aes32) {
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

	if (line_outs_monitor[hdspm->dev]) {

		snd_printk(KERN_INFO "HDSPM: "
			   "sending all playback streams to line outs.\n");

		for (i = 0; i < HDSPM_MIXER_CHANNELS; i++) {
			if (hdspm_write_pb_gain(hdspm, i, i, UNITY_GAIN))
				return -EIO;
		}
	}

	/* set a default rate so that the channel map is set up. */
	hdspm->channel_map = channel_map_madi_ss;
	hdspm_set_rate(hdspm, 44100, 1);

	return 0;
}


/*------------------------------------------------------------
   interrupt 
 ------------------------------------------------------------*/

static irqreturn_t snd_hdspm_interrupt(int irq, void *dev_id)
{
	struct hdspm *hdspm = (struct hdspm *) dev_id;
	unsigned int status;
	int audio;
	int midi0;
	int midi1;
	unsigned int midi0status;
	unsigned int midi1status;
	int schedule = 0;

	status = hdspm_read(hdspm, HDSPM_statusRegister);

	audio = status & HDSPM_audioIRQPending;
	midi0 = status & HDSPM_midi0IRQPending;
	midi1 = status & HDSPM_midi1IRQPending;

	if (!audio && !midi0 && !midi1)
		return IRQ_NONE;

	hdspm_write(hdspm, HDSPM_interruptConfirmation, 0);
	hdspm->irq_count++;

	midi0status = hdspm_read(hdspm, HDSPM_midiStatusIn0) & 0xff;
	midi1status = hdspm_read(hdspm, HDSPM_midiStatusIn1) & 0xff;

	if (audio) {

		if (hdspm->capture_substream)
			snd_pcm_period_elapsed(hdspm->capture_substream);

		if (hdspm->playback_substream)
			snd_pcm_period_elapsed(hdspm->playback_substream);
	}

	if (midi0 && midi0status) {
		/* we disable interrupts for this input until processing
		 * is done
		 */
		hdspm->control_register &= ~HDSPM_Midi0InterruptEnable;
		hdspm_write(hdspm, HDSPM_controlRegister,
			    hdspm->control_register);
		hdspm->midi[0].pending = 1;
		schedule = 1;
	}
	if (midi1 && midi1status) {
		/* we disable interrupts for this input until processing
		 * is done
		 */
		hdspm->control_register &= ~HDSPM_Midi1InterruptEnable;
		hdspm_write(hdspm, HDSPM_controlRegister,
			    hdspm->control_register);
		hdspm->midi[1].pending = 1;
		schedule = 1;
	}
	if (schedule)
		tasklet_schedule(&hdspm->midi_tasklet);
	return IRQ_HANDLED;
}

/*------------------------------------------------------------
   pcm interface 
  ------------------------------------------------------------*/


static snd_pcm_uframes_t snd_hdspm_hw_pointer(struct snd_pcm_substream *
					      substream)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	return hdspm_hw_pointer(hdspm);
}

static char *hdspm_channel_buffer_location(struct hdspm * hdspm,
					   int stream, int channel)
{
	int mapped_channel;

	if (snd_BUG_ON(channel < 0 || channel >= HDSPM_MAX_CHANNELS))
		return NULL;

	mapped_channel = hdspm->channel_map[channel];
	if (mapped_channel < 0)
		return NULL;

	if (stream == SNDRV_PCM_STREAM_CAPTURE)
		return hdspm->capture_buffer +
		    mapped_channel * HDSPM_CHANNEL_BUFFER_BYTES;
	else
		return hdspm->playback_buffer +
		    mapped_channel * HDSPM_CHANNEL_BUFFER_BYTES;
}


/* dont know why need it ??? */
static int snd_hdspm_playback_copy(struct snd_pcm_substream *substream,
				   int channel, snd_pcm_uframes_t pos,
				   void __user *src, snd_pcm_uframes_t count)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	char *channel_buf;

	if (snd_BUG_ON(pos + count > HDSPM_CHANNEL_BUFFER_BYTES / 4))
		return -EINVAL;

	channel_buf =
		hdspm_channel_buffer_location(hdspm, substream->pstr->stream,
					      channel);

	if (snd_BUG_ON(!channel_buf))
		return -EIO;

	return copy_from_user(channel_buf + pos * 4, src, count * 4);
}

static int snd_hdspm_capture_copy(struct snd_pcm_substream *substream,
				  int channel, snd_pcm_uframes_t pos,
				  void __user *dst, snd_pcm_uframes_t count)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	char *channel_buf;

	if (snd_BUG_ON(pos + count > HDSPM_CHANNEL_BUFFER_BYTES / 4))
		return -EINVAL;

	channel_buf =
		hdspm_channel_buffer_location(hdspm, substream->pstr->stream,
					      channel);
	if (snd_BUG_ON(!channel_buf))
		return -EIO;
	return copy_to_user(dst, channel_buf + pos * 4, count * 4);
}

static int snd_hdspm_hw_silence(struct snd_pcm_substream *substream,
				int channel, snd_pcm_uframes_t pos,
				snd_pcm_uframes_t count)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	char *channel_buf;

	channel_buf =
		hdspm_channel_buffer_location(hdspm, substream->pstr->stream,
					      channel);
	if (snd_BUG_ON(!channel_buf))
		return -EIO;
	memset(channel_buf + pos * 4, 0, count * 4);
	return 0;
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
		spin_unlock_irq(&hdspm->lock);
		_snd_pcm_hw_param_setempty(params,
					   SNDRV_PCM_HW_PARAM_RATE);
		return err;
	}
	spin_unlock_irq(&hdspm->lock);

	err = hdspm_set_interrupt_interval(hdspm,
					   params_period_size(params));
	if (err < 0) {
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
	if (err < 0)
		return err;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		hdspm_set_sgbuf(hdspm, substream, HDSPM_pageAddressBufferOut,
				params_channels(params));

		for (i = 0; i < params_channels(params); ++i)
			snd_hdspm_enable_out(hdspm, i, 1);

		hdspm->playback_buffer =
		    (unsigned char *) substream->runtime->dma_area;
		snd_printdd("Allocated sample buffer for playback at %p\n",
				hdspm->playback_buffer);
	} else {
		hdspm_set_sgbuf(hdspm, substream, HDSPM_pageAddressBufferIn,
				params_channels(params));

		for (i = 0; i < params_channels(params); ++i)
			snd_hdspm_enable_in(hdspm, i, 1);

		hdspm->capture_buffer =
		    (unsigned char *) substream->runtime->dma_area;
		snd_printdd("Allocated sample buffer for capture at %p\n",
				hdspm->capture_buffer);
	}
	/*
	   snd_printdd("Allocated sample buffer for %s at 0x%08X\n",
	   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
	   "playback" : "capture",
	   snd_pcm_sgbuf_get_addr(substream, 0));
	 */
	/*
	snd_printdd("set_hwparams: %s %d Hz, %d channels, bs = %d\n",
			substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			  "playback" : "capture",
			params_rate(params), params_channels(params),
			params_buffer_size(params));
	*/
	return 0;
}

static int snd_hdspm_hw_free(struct snd_pcm_substream *substream)
{
	int i;
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		/* params_channels(params) should be enough, 
		   but to get sure in case of error */
		for (i = 0; i < HDSPM_MAX_CHANNELS; ++i)
			snd_hdspm_enable_out(hdspm, i, 0);

		hdspm->playback_buffer = NULL;
	} else {
		for (i = 0; i < HDSPM_MAX_CHANNELS; ++i)
			snd_hdspm_enable_in(hdspm, i, 0);

		hdspm->capture_buffer = NULL;

	}

	snd_pcm_lib_free_pages(substream);

	return 0;
}

static int snd_hdspm_channel_info(struct snd_pcm_substream *substream,
				  struct snd_pcm_channel_info * info)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	int mapped_channel;

	if (snd_BUG_ON(info->channel >= HDSPM_MAX_CHANNELS))
		return -EINVAL;

	mapped_channel = hdspm->channel_map[info->channel];
	if (mapped_channel < 0)
		return -EINVAL;

	info->offset = mapped_channel * HDSPM_CHANNEL_BUFFER_BYTES;
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

static unsigned int period_sizes[] =
    { 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

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
	.period_bytes_min = (64 * 4),
	.period_bytes_max = (8192 * 4) * HDSPM_MAX_CHANNELS,
	.periods_min = 2,
	.periods_max = 2,
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
	.period_bytes_min = (64 * 4),
	.period_bytes_max = (8192 * 4) * HDSPM_MAX_CHANNELS,
	.periods_min = 2,
	.periods_max = 2,
	.fifo_size = 0
};

static struct snd_pcm_hw_constraint_list hw_constraints_period_sizes = {
	.count = ARRAY_SIZE(period_sizes),
	.list = period_sizes,
	.mask = 0
};


static int snd_hdspm_hw_rule_channels_rate(struct snd_pcm_hw_params *params,
					   struct snd_pcm_hw_rule * rule)
{
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	if (r->min > 48000 && r->max <= 96000) {
		struct snd_interval t = {
			.min = hdspm->ds_channels,
			.max = hdspm->ds_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->max < 64000) {
		struct snd_interval t = {
			.min = hdspm->ss_channels,
			.max = hdspm->ss_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	}
	return 0;
}

static int snd_hdspm_hw_rule_rate_channels(struct snd_pcm_hw_params *params,
					   struct snd_pcm_hw_rule * rule)
{
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	if (c->min >= hdspm->ss_channels) {
		struct snd_interval t = {
			.min = 32000,
			.max = 48000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdspm->ds_channels) {
		struct snd_interval t = {
			.min = 64000,
			.max = 96000,
			.integer = 1,
		};

		return snd_interval_refine(r, &t);
	}
	return 0;
}

static int snd_hdspm_hw_rule_channels(struct snd_pcm_hw_params *params,
				      struct snd_pcm_hw_rule *rule)
{
	unsigned int list[3];
	struct hdspm *hdspm = rule->private;
	struct snd_interval *c = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	if (hdspm->is_aes32) {
		list[0] = hdspm->qs_channels;
		list[1] = hdspm->ds_channels;
		list[2] = hdspm->ss_channels;
		return snd_interval_list(c, 3, list, 0);
	} else {
		list[0] = hdspm->ds_channels;
		list[1] = hdspm->ss_channels;
		return snd_interval_list(c, 2, list, 0);
	}
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

static int snd_hdspm_playback_open(struct snd_pcm_substream *substream)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	spin_lock_irq(&hdspm->lock);

	snd_pcm_set_sync(substream);

	runtime->hw = snd_hdspm_playback_subinfo;

	if (hdspm->capture_substream == NULL)
		hdspm_stop_audio(hdspm);

	hdspm->playback_pid = current->pid;
	hdspm->playback_substream = substream;

	spin_unlock_irq(&hdspm->lock);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);

	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
				   &hw_constraints_period_sizes);

	if (hdspm->is_aes32) {
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				&hdspm_hw_constraints_aes32_sample_rates);
	} else {
		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				     snd_hdspm_hw_rule_channels, hdspm,
				     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				    snd_hdspm_hw_rule_channels_rate, hdspm,
				    SNDRV_PCM_HW_PARAM_RATE, -1);

		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				    snd_hdspm_hw_rule_rate_channels, hdspm,
				    SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	}
	return 0;
}

static int snd_hdspm_playback_release(struct snd_pcm_substream *substream)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);

	spin_lock_irq(&hdspm->lock);

	hdspm->playback_pid = -1;
	hdspm->playback_substream = NULL;

	spin_unlock_irq(&hdspm->lock);

	return 0;
}


static int snd_hdspm_capture_open(struct snd_pcm_substream *substream)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	spin_lock_irq(&hdspm->lock);
	snd_pcm_set_sync(substream);
	runtime->hw = snd_hdspm_capture_subinfo;

	if (hdspm->playback_substream == NULL)
		hdspm_stop_audio(hdspm);

	hdspm->capture_pid = current->pid;
	hdspm->capture_substream = substream;

	spin_unlock_irq(&hdspm->lock);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
				   &hw_constraints_period_sizes);
	if (hdspm->is_aes32) {
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				&hdspm_hw_constraints_aes32_sample_rates);
	} else {
		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				     snd_hdspm_hw_rule_channels, hdspm,
				     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				    snd_hdspm_hw_rule_channels_rate, hdspm,
				    SNDRV_PCM_HW_PARAM_RATE, -1);

		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				    snd_hdspm_hw_rule_rate_channels, hdspm,
				    SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	}
	return 0;
}

static int snd_hdspm_capture_release(struct snd_pcm_substream *substream)
{
	struct hdspm *hdspm = snd_pcm_substream_chip(substream);

	spin_lock_irq(&hdspm->lock);

	hdspm->capture_pid = -1;
	hdspm->capture_substream = NULL;

	spin_unlock_irq(&hdspm->lock);
	return 0;
}

static int snd_hdspm_hwdep_ioctl(struct snd_hwdep * hw, struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	struct hdspm *hdspm = hw->private_data;
	struct hdspm_mixer_ioctl mixer;
	struct hdspm_config_info info;
	struct hdspm_version hdspm_version;
	struct hdspm_peak_rms_ioctl rms;

	switch (cmd) {

	case SNDRV_HDSPM_IOCTL_GET_PEAK_RMS:
		if (copy_from_user(&rms, (void __user *)arg, sizeof(rms)))
			return -EFAULT;
		/* maybe there is a chance to memorymap in future
		 * so dont touch just copy
		 */
		if(copy_to_user_fromio((void __user *)rms.peak,
				       hdspm->iobase+HDSPM_MADI_peakrmsbase,
				       sizeof(struct hdspm_peak_rms)) != 0 )
			return -EFAULT;

		break;
		

	case SNDRV_HDSPM_IOCTL_GET_CONFIG_INFO:

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
		info.line_out = hdspm_line_out(hdspm);
		info.passthru = 0;
		spin_unlock_irq(&hdspm->lock);
		if (copy_to_user((void __user *) arg, &info, sizeof(info)))
			return -EFAULT;
		break;

	case SNDRV_HDSPM_IOCTL_GET_VERSION:
		hdspm_version.firmware_rev = hdspm->firmware_rev;
		if (copy_to_user((void __user *) arg, &hdspm_version,
				 sizeof(hdspm_version)))
			return -EFAULT;
		break;

	case SNDRV_HDSPM_IOCTL_GET_MIXER:
		if (copy_from_user(&mixer, (void __user *)arg, sizeof(mixer)))
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

static struct snd_pcm_ops snd_hdspm_playback_ops = {
	.open = snd_hdspm_playback_open,
	.close = snd_hdspm_playback_release,
	.ioctl = snd_hdspm_ioctl,
	.hw_params = snd_hdspm_hw_params,
	.hw_free = snd_hdspm_hw_free,
	.prepare = snd_hdspm_prepare,
	.trigger = snd_hdspm_trigger,
	.pointer = snd_hdspm_hw_pointer,
	.copy = snd_hdspm_playback_copy,
	.silence = snd_hdspm_hw_silence,
	.page = snd_pcm_sgbuf_ops_page,
};

static struct snd_pcm_ops snd_hdspm_capture_ops = {
	.open = snd_hdspm_capture_open,
	.close = snd_hdspm_capture_release,
	.ioctl = snd_hdspm_ioctl,
	.hw_params = snd_hdspm_hw_params,
	.hw_free = snd_hdspm_hw_free,
	.prepare = snd_hdspm_prepare,
	.trigger = snd_hdspm_trigger,
	.pointer = snd_hdspm_hw_pointer,
	.copy = snd_hdspm_capture_copy,
	.page = snd_pcm_sgbuf_ops_page,
};

static int __devinit snd_hdspm_create_hwdep(struct snd_card *card,
					    struct hdspm * hdspm)
{
	struct snd_hwdep *hw;
	int err;

	err = snd_hwdep_new(card, "HDSPM hwdep", 0, &hw);
	if (err < 0)
		return err;

	hdspm->hwdep = hw;
	hw->private_data = hdspm;
	strcpy(hw->name, "HDSPM hwdep interface");

	hw->ops.ioctl = snd_hdspm_hwdep_ioctl;

	return 0;
}


/*------------------------------------------------------------
   memory interface 
 ------------------------------------------------------------*/
static int __devinit snd_hdspm_preallocate_memory(struct hdspm * hdspm)
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
		snd_printdd("Could not preallocate %zd Bytes\n", wanted);

		return err;
	} else
		snd_printdd(" Preallocated %zd Bytes\n", wanted);

	return 0;
}

static void hdspm_set_sgbuf(struct hdspm * hdspm,
			    struct snd_pcm_substream *substream,
			     unsigned int reg, int channels)
{
	int i;
	for (i = 0; i < (channels * 16); i++)
		hdspm_write(hdspm, reg + 4 * i,
			    snd_pcm_sgbuf_get_addr(substream, 4096 * i));
}

/* ------------- ALSA Devices ---------------------------- */
static int __devinit snd_hdspm_create_pcm(struct snd_card *card,
					  struct hdspm * hdspm)
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
			&snd_hdspm_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_hdspm_capture_ops);

	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;

	err = snd_hdspm_preallocate_memory(hdspm);
	if (err < 0)
		return err;

	return 0;
}

static inline void snd_hdspm_initialize_midi_flush(struct hdspm * hdspm)
{
	snd_hdspm_flush_midi_input(hdspm, 0);
	snd_hdspm_flush_midi_input(hdspm, 1);
}

static int __devinit snd_hdspm_create_alsa_devices(struct snd_card *card,
						   struct hdspm * hdspm)
{
	int err;

	snd_printdd("Create card...\n");
	err = snd_hdspm_create_pcm(card, hdspm);
	if (err < 0)
		return err;

	err = snd_hdspm_create_midi(card, hdspm, 0);
	if (err < 0)
		return err;

	err = snd_hdspm_create_midi(card, hdspm, 1);
	if (err < 0)
		return err;

	err = snd_hdspm_create_controls(card, hdspm);
	if (err < 0)
		return err;

	err = snd_hdspm_create_hwdep(card, hdspm);
	if (err < 0)
		return err;

	snd_printdd("proc init...\n");
	snd_hdspm_proc_init(hdspm);

	hdspm->system_sample_rate = -1;
	hdspm->last_external_sample_rate = -1;
	hdspm->last_internal_sample_rate = -1;
	hdspm->playback_pid = -1;
	hdspm->capture_pid = -1;
	hdspm->capture_substream = NULL;
	hdspm->playback_substream = NULL;

	snd_printdd("Set defaults...\n");
	err = snd_hdspm_set_defaults(hdspm);
	if (err < 0)
		return err;

	snd_printdd("Update mixer controls...\n");
	hdspm_update_simple_mixer_controls(hdspm);

	snd_printdd("Initializeing complete ???\n");

	err = snd_card_register(card);
	if (err < 0) {
		snd_printk(KERN_ERR "HDSPM: error registering card\n");
		return err;
	}

	snd_printdd("... yes now\n");

	return 0;
}

static int __devinit snd_hdspm_create(struct snd_card *card,
				      struct hdspm *hdspm,
				      int precise_ptr, int enable_monitor)
{
	struct pci_dev *pci = hdspm->pci;
	int err;
	unsigned long io_extent;

	hdspm->irq = -1;

	spin_lock_init(&hdspm->midi[0].lock);
	spin_lock_init(&hdspm->midi[1].lock);

	hdspm->card = card;

	spin_lock_init(&hdspm->lock);

	tasklet_init(&hdspm->midi_tasklet,
		     hdspm_midi_tasklet, (unsigned long) hdspm);

	pci_read_config_word(hdspm->pci,
			     PCI_CLASS_REVISION, &hdspm->firmware_rev);

	hdspm->is_aes32 = (hdspm->firmware_rev >= HDSPM_AESREVISION);

	strcpy(card->mixername, "Xilinx FPGA");
	if (hdspm->is_aes32) {
		strcpy(card->driver, "HDSPAES32");
		hdspm->card_name = "RME HDSPM AES32";
	} else {
		strcpy(card->driver, "HDSPM");
		hdspm->card_name = "RME HDSPM MADI";
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

	snd_printdd("grabbed memory region 0x%lx-0x%lx\n",
		   hdspm->port, hdspm->port + io_extent - 1);


	hdspm->iobase = ioremap_nocache(hdspm->port, io_extent);
	if (!hdspm->iobase) {
		snd_printk(KERN_ERR "HDSPM: "
			   "unable to remap region 0x%lx-0x%lx\n",
			   hdspm->port, hdspm->port + io_extent - 1);
		return -EBUSY;
	}
	snd_printdd("remapped region (0x%lx) 0x%lx-0x%lx\n",
		   (unsigned long)hdspm->iobase, hdspm->port,
		   hdspm->port + io_extent - 1);

	if (request_irq(pci->irq, snd_hdspm_interrupt,
			IRQF_SHARED, "hdspm", hdspm)) {
		snd_printk(KERN_ERR "HDSPM: unable to use IRQ %d\n", pci->irq);
		return -EBUSY;
	}

	snd_printdd("use IRQ %d\n", pci->irq);

	hdspm->irq = pci->irq;
	hdspm->precise_ptr = precise_ptr;

	hdspm->monitor_outs = enable_monitor;

	snd_printdd("kmalloc Mixer memory of %zd Bytes\n",
		   sizeof(struct hdspm_mixer));
	hdspm->mixer = kzalloc(sizeof(struct hdspm_mixer), GFP_KERNEL);
	if (!hdspm->mixer) {
		snd_printk(KERN_ERR "HDSPM: "
			   "unable to kmalloc Mixer memory of %d Bytes\n",
			   (int)sizeof(struct hdspm_mixer));
		return err;
	}

	hdspm->ss_channels = MADI_SS_CHANNELS;
	hdspm->ds_channels = MADI_DS_CHANNELS;
	hdspm->qs_channels = MADI_QS_CHANNELS;

	snd_printdd("create alsa devices.\n");
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
		      HDSPM_Midi0InterruptEnable | HDSPM_Midi1InterruptEnable);
		hdspm_write(hdspm, HDSPM_controlRegister,
			    hdspm->control_register);
	}

	if (hdspm->irq >= 0)
		free_irq(hdspm->irq, (void *) hdspm);

	kfree(hdspm->mixer);

	if (hdspm->iobase)
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

static int __devinit snd_hdspm_probe(struct pci_dev *pci,
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

	err = snd_card_create(index[dev], id[dev],
			      THIS_MODULE, sizeof(struct hdspm), &card);
	if (err < 0)
		return err;

	hdspm = card->private_data;
	card->private_free = snd_hdspm_card_free;
	hdspm->dev = dev;
	hdspm->pci = pci;

	snd_card_set_dev(card, &pci->dev);

	err = snd_hdspm_create(card, hdspm, precise_ptr[dev],
			       enable_monitor[dev]);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->shortname, "HDSPM MADI");
	sprintf(card->longname, "%s at 0x%lx, irq %d", hdspm->card_name,
		hdspm->port, hdspm->irq);

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);

	dev++;
	return 0;
}

static void __devexit snd_hdspm_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "RME Hammerfall DSP MADI",
	.id_table = snd_hdspm_ids,
	.probe = snd_hdspm_probe,
	.remove = __devexit_p(snd_hdspm_remove),
};


static int __init alsa_card_hdspm_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_hdspm_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_hdspm_init)
module_exit(alsa_card_hdspm_exit)
