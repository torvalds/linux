/*
 *   ALSA driver for RME Hammerfall DSP audio interface(s)
 *
 *      Copyright (c) 2002  Paul Davis
 *                          Marcus Andersson
 *                          Thomas Charbonnel
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
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/moduleparam.h>
#include <linux/math64.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/asoundef.h>
#include <sound/rawmidi.h>
#include <sound/hwdep.h>
#include <sound/initval.h>
#include <sound/hdsp.h>

#include <asm/byteorder.h>
#include <asm/current.h>
#include <asm/io.h>

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for RME Hammerfall DSP interface.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for RME Hammerfall DSP interface.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable/disable specific Hammerfall DSP soundcards.");
MODULE_AUTHOR("Paul Davis <paul@linuxaudiosystems.com>, Marcus Andersson, Thomas Charbonnel <thomas@undata.org>");
MODULE_DESCRIPTION("RME Hammerfall DSP");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{RME Hammerfall-DSP},"
	        "{RME HDSP-9652},"
		"{RME HDSP-9632}}");
#ifdef HDSP_FW_LOADER
MODULE_FIRMWARE("multiface_firmware.bin");
MODULE_FIRMWARE("multiface_firmware_rev11.bin");
MODULE_FIRMWARE("digiface_firmware.bin");
MODULE_FIRMWARE("digiface_firmware_rev11.bin");
#endif

#define HDSP_MAX_CHANNELS        26
#define HDSP_MAX_DS_CHANNELS     14
#define HDSP_MAX_QS_CHANNELS     8
#define DIGIFACE_SS_CHANNELS     26
#define DIGIFACE_DS_CHANNELS     14
#define MULTIFACE_SS_CHANNELS    18
#define MULTIFACE_DS_CHANNELS    14
#define H9652_SS_CHANNELS        26
#define H9652_DS_CHANNELS        14
/* This does not include possible Analog Extension Boards
   AEBs are detected at card initialization
*/
#define H9632_SS_CHANNELS	 12
#define H9632_DS_CHANNELS	 8
#define H9632_QS_CHANNELS	 4

/* Write registers. These are defined as byte-offsets from the iobase value.
 */
#define HDSP_resetPointer               0
#define HDSP_freqReg			0
#define HDSP_outputBufferAddress	32
#define HDSP_inputBufferAddress		36
#define HDSP_controlRegister		64
#define HDSP_interruptConfirmation	96
#define HDSP_outputEnable	  	128
#define HDSP_control2Reg		256
#define HDSP_midiDataOut0  		352
#define HDSP_midiDataOut1  		356
#define HDSP_fifoData  			368
#define HDSP_inputEnable	 	384

/* Read registers. These are defined as byte-offsets from the iobase value
 */

#define HDSP_statusRegister    0
#define HDSP_timecode        128
#define HDSP_status2Register 192
#define HDSP_midiDataIn0     360
#define HDSP_midiDataIn1     364
#define HDSP_midiStatusOut0  384
#define HDSP_midiStatusOut1  388
#define HDSP_midiStatusIn0   392
#define HDSP_midiStatusIn1   396
#define HDSP_fifoStatus      400

/* the meters are regular i/o-mapped registers, but offset
   considerably from the rest. the peak registers are reset
   when read; the least-significant 4 bits are full-scale counters;
   the actual peak value is in the most-significant 24 bits.
*/

#define HDSP_playbackPeakLevel  4096  /* 26 * 32 bit values */
#define HDSP_inputPeakLevel     4224  /* 26 * 32 bit values */
#define HDSP_outputPeakLevel    4352  /* (26+2) * 32 bit values */
#define HDSP_playbackRmsLevel   4612  /* 26 * 64 bit values */
#define HDSP_inputRmsLevel      4868  /* 26 * 64 bit values */


/* This is for H9652 cards
   Peak values are read downward from the base
   Rms values are read upward
   There are rms values for the outputs too
   26*3 values are read in ss mode
   14*3 in ds mode, with no gap between values
*/
#define HDSP_9652_peakBase	7164
#define HDSP_9652_rmsBase	4096

/* c.f. the hdsp_9632_meters_t struct */
#define HDSP_9632_metersBase	4096

#define HDSP_IO_EXTENT     7168

/* control2 register bits */

#define HDSP_TMS                0x01
#define HDSP_TCK                0x02
#define HDSP_TDI                0x04
#define HDSP_JTAG               0x08
#define HDSP_PWDN               0x10
#define HDSP_PROGRAM	        0x020
#define HDSP_CONFIG_MODE_0	0x040
#define HDSP_CONFIG_MODE_1	0x080
#define HDSP_VERSION_BIT	0x100
#define HDSP_BIGENDIAN_MODE     0x200
#define HDSP_RD_MULTIPLE        0x400
#define HDSP_9652_ENABLE_MIXER  0x800
#define HDSP_TDO                0x10000000

#define HDSP_S_PROGRAM     	(HDSP_PROGRAM|HDSP_CONFIG_MODE_0)
#define HDSP_S_LOAD		(HDSP_PROGRAM|HDSP_CONFIG_MODE_1)

/* Control Register bits */

#define HDSP_Start                (1<<0)  /* start engine */
#define HDSP_Latency0             (1<<1)  /* buffer size = 2^n where n is defined by Latency{2,1,0} */
#define HDSP_Latency1             (1<<2)  /* [ see above ] */
#define HDSP_Latency2             (1<<3)  /* [ see above ] */
#define HDSP_ClockModeMaster      (1<<4)  /* 1=Master, 0=Slave/Autosync */
#define HDSP_AudioInterruptEnable (1<<5)  /* what do you think ? */
#define HDSP_Frequency0           (1<<6)  /* 0=44.1kHz/88.2kHz/176.4kHz 1=48kHz/96kHz/192kHz */
#define HDSP_Frequency1           (1<<7)  /* 0=32kHz/64kHz/128kHz */
#define HDSP_DoubleSpeed          (1<<8)  /* 0=normal speed, 1=double speed */
#define HDSP_SPDIFProfessional    (1<<9)  /* 0=consumer, 1=professional */
#define HDSP_SPDIFEmphasis        (1<<10) /* 0=none, 1=on */
#define HDSP_SPDIFNonAudio        (1<<11) /* 0=off, 1=on */
#define HDSP_SPDIFOpticalOut      (1<<12) /* 1=use 1st ADAT connector for SPDIF, 0=do not */
#define HDSP_SyncRef2             (1<<13)
#define HDSP_SPDIFInputSelect0    (1<<14)
#define HDSP_SPDIFInputSelect1    (1<<15)
#define HDSP_SyncRef0             (1<<16)
#define HDSP_SyncRef1             (1<<17)
#define HDSP_AnalogExtensionBoard (1<<18) /* For H9632 cards */
#define HDSP_XLRBreakoutCable     (1<<20) /* For H9632 cards */
#define HDSP_Midi0InterruptEnable (1<<22)
#define HDSP_Midi1InterruptEnable (1<<23)
#define HDSP_LineOut              (1<<24)
#define HDSP_ADGain0		  (1<<25) /* From here : H9632 specific */
#define HDSP_ADGain1		  (1<<26)
#define HDSP_DAGain0		  (1<<27)
#define HDSP_DAGain1		  (1<<28)
#define HDSP_PhoneGain0		  (1<<29)
#define HDSP_PhoneGain1		  (1<<30)
#define HDSP_QuadSpeed	  	  (1<<31)

#define HDSP_ADGainMask       (HDSP_ADGain0|HDSP_ADGain1)
#define HDSP_ADGainMinus10dBV  HDSP_ADGainMask
#define HDSP_ADGainPlus4dBu   (HDSP_ADGain0)
#define HDSP_ADGainLowGain     0

#define HDSP_DAGainMask         (HDSP_DAGain0|HDSP_DAGain1)
#define HDSP_DAGainHighGain      HDSP_DAGainMask
#define HDSP_DAGainPlus4dBu     (HDSP_DAGain0)
#define HDSP_DAGainMinus10dBV    0

#define HDSP_PhoneGainMask      (HDSP_PhoneGain0|HDSP_PhoneGain1)
#define HDSP_PhoneGain0dB        HDSP_PhoneGainMask
#define HDSP_PhoneGainMinus6dB  (HDSP_PhoneGain0)
#define HDSP_PhoneGainMinus12dB  0

#define HDSP_LatencyMask    (HDSP_Latency0|HDSP_Latency1|HDSP_Latency2)
#define HDSP_FrequencyMask  (HDSP_Frequency0|HDSP_Frequency1|HDSP_DoubleSpeed|HDSP_QuadSpeed)

#define HDSP_SPDIFInputMask    (HDSP_SPDIFInputSelect0|HDSP_SPDIFInputSelect1)
#define HDSP_SPDIFInputADAT1    0
#define HDSP_SPDIFInputCoaxial (HDSP_SPDIFInputSelect0)
#define HDSP_SPDIFInputCdrom   (HDSP_SPDIFInputSelect1)
#define HDSP_SPDIFInputAES     (HDSP_SPDIFInputSelect0|HDSP_SPDIFInputSelect1)

#define HDSP_SyncRefMask        (HDSP_SyncRef0|HDSP_SyncRef1|HDSP_SyncRef2)
#define HDSP_SyncRef_ADAT1       0
#define HDSP_SyncRef_ADAT2      (HDSP_SyncRef0)
#define HDSP_SyncRef_ADAT3      (HDSP_SyncRef1)
#define HDSP_SyncRef_SPDIF      (HDSP_SyncRef0|HDSP_SyncRef1)
#define HDSP_SyncRef_WORD       (HDSP_SyncRef2)
#define HDSP_SyncRef_ADAT_SYNC  (HDSP_SyncRef0|HDSP_SyncRef2)

/* Sample Clock Sources */

#define HDSP_CLOCK_SOURCE_AUTOSYNC           0
#define HDSP_CLOCK_SOURCE_INTERNAL_32KHZ     1
#define HDSP_CLOCK_SOURCE_INTERNAL_44_1KHZ   2
#define HDSP_CLOCK_SOURCE_INTERNAL_48KHZ     3
#define HDSP_CLOCK_SOURCE_INTERNAL_64KHZ     4
#define HDSP_CLOCK_SOURCE_INTERNAL_88_2KHZ   5
#define HDSP_CLOCK_SOURCE_INTERNAL_96KHZ     6
#define HDSP_CLOCK_SOURCE_INTERNAL_128KHZ    7
#define HDSP_CLOCK_SOURCE_INTERNAL_176_4KHZ  8
#define HDSP_CLOCK_SOURCE_INTERNAL_192KHZ    9

/* Preferred sync reference choices - used by "pref_sync_ref" control switch */

#define HDSP_SYNC_FROM_WORD      0
#define HDSP_SYNC_FROM_SPDIF     1
#define HDSP_SYNC_FROM_ADAT1     2
#define HDSP_SYNC_FROM_ADAT_SYNC 3
#define HDSP_SYNC_FROM_ADAT2     4
#define HDSP_SYNC_FROM_ADAT3     5

/* SyncCheck status */

#define HDSP_SYNC_CHECK_NO_LOCK 0
#define HDSP_SYNC_CHECK_LOCK    1
#define HDSP_SYNC_CHECK_SYNC	2

/* AutoSync references - used by "autosync_ref" control switch */

#define HDSP_AUTOSYNC_FROM_WORD      0
#define HDSP_AUTOSYNC_FROM_ADAT_SYNC 1
#define HDSP_AUTOSYNC_FROM_SPDIF     2
#define HDSP_AUTOSYNC_FROM_NONE	     3
#define HDSP_AUTOSYNC_FROM_ADAT1     4
#define HDSP_AUTOSYNC_FROM_ADAT2     5
#define HDSP_AUTOSYNC_FROM_ADAT3     6

/* Possible sources of S/PDIF input */

#define HDSP_SPDIFIN_OPTICAL  0	/* optical  (ADAT1) */
#define HDSP_SPDIFIN_COAXIAL  1	/* coaxial (RCA) */
#define HDSP_SPDIFIN_INTERNAL 2	/* internal (CDROM) */
#define HDSP_SPDIFIN_AES      3 /* xlr for H9632 (AES)*/

#define HDSP_Frequency32KHz    HDSP_Frequency0
#define HDSP_Frequency44_1KHz  HDSP_Frequency1
#define HDSP_Frequency48KHz    (HDSP_Frequency1|HDSP_Frequency0)
#define HDSP_Frequency64KHz    (HDSP_DoubleSpeed|HDSP_Frequency0)
#define HDSP_Frequency88_2KHz  (HDSP_DoubleSpeed|HDSP_Frequency1)
#define HDSP_Frequency96KHz    (HDSP_DoubleSpeed|HDSP_Frequency1|HDSP_Frequency0)
/* For H9632 cards */
#define HDSP_Frequency128KHz   (HDSP_QuadSpeed|HDSP_DoubleSpeed|HDSP_Frequency0)
#define HDSP_Frequency176_4KHz (HDSP_QuadSpeed|HDSP_DoubleSpeed|HDSP_Frequency1)
#define HDSP_Frequency192KHz   (HDSP_QuadSpeed|HDSP_DoubleSpeed|HDSP_Frequency1|HDSP_Frequency0)
/* RME says n = 104857600000000, but in the windows MADI driver, I see:
	return 104857600000000 / rate; // 100 MHz
	return 110100480000000 / rate; // 105 MHz
*/
#define DDS_NUMERATOR 104857600000000ULL;  /*  =  2^20 * 10^8 */

#define hdsp_encode_latency(x)       (((x)<<1) & HDSP_LatencyMask)
#define hdsp_decode_latency(x)       (((x) & HDSP_LatencyMask)>>1)

#define hdsp_encode_spdif_in(x) (((x)&0x3)<<14)
#define hdsp_decode_spdif_in(x) (((x)>>14)&0x3)

/* Status Register bits */

#define HDSP_audioIRQPending    (1<<0)
#define HDSP_Lock2              (1<<1)     /* this is for Digiface and H9652 */
#define HDSP_spdifFrequency3	HDSP_Lock2 /* this is for H9632 only */
#define HDSP_Lock1              (1<<2)
#define HDSP_Lock0              (1<<3)
#define HDSP_SPDIFSync          (1<<4)
#define HDSP_TimecodeLock       (1<<5)
#define HDSP_BufferPositionMask 0x000FFC0 /* Bit 6..15 : h/w buffer pointer */
#define HDSP_Sync2              (1<<16)
#define HDSP_Sync1              (1<<17)
#define HDSP_Sync0              (1<<18)
#define HDSP_DoubleSpeedStatus  (1<<19)
#define HDSP_ConfigError        (1<<20)
#define HDSP_DllError           (1<<21)
#define HDSP_spdifFrequency0    (1<<22)
#define HDSP_spdifFrequency1    (1<<23)
#define HDSP_spdifFrequency2    (1<<24)
#define HDSP_SPDIFErrorFlag     (1<<25)
#define HDSP_BufferID           (1<<26)
#define HDSP_TimecodeSync       (1<<27)
#define HDSP_AEBO          	(1<<28) /* H9632 specific Analog Extension Boards */
#define HDSP_AEBI		(1<<29) /* 0 = present, 1 = absent */
#define HDSP_midi0IRQPending    (1<<30)
#define HDSP_midi1IRQPending    (1<<31)

#define HDSP_spdifFrequencyMask    (HDSP_spdifFrequency0|HDSP_spdifFrequency1|HDSP_spdifFrequency2)
#define HDSP_spdifFrequencyMask_9632 (HDSP_spdifFrequency0|\
				      HDSP_spdifFrequency1|\
				      HDSP_spdifFrequency2|\
				      HDSP_spdifFrequency3)

#define HDSP_spdifFrequency32KHz   (HDSP_spdifFrequency0)
#define HDSP_spdifFrequency44_1KHz (HDSP_spdifFrequency1)
#define HDSP_spdifFrequency48KHz   (HDSP_spdifFrequency0|HDSP_spdifFrequency1)

#define HDSP_spdifFrequency64KHz   (HDSP_spdifFrequency2)
#define HDSP_spdifFrequency88_2KHz (HDSP_spdifFrequency0|HDSP_spdifFrequency2)
#define HDSP_spdifFrequency96KHz   (HDSP_spdifFrequency2|HDSP_spdifFrequency1)

/* This is for H9632 cards */
#define HDSP_spdifFrequency128KHz   (HDSP_spdifFrequency0|\
				     HDSP_spdifFrequency1|\
				     HDSP_spdifFrequency2)
#define HDSP_spdifFrequency176_4KHz HDSP_spdifFrequency3
#define HDSP_spdifFrequency192KHz   (HDSP_spdifFrequency3|HDSP_spdifFrequency0)

/* Status2 Register bits */

#define HDSP_version0     (1<<0)
#define HDSP_version1     (1<<1)
#define HDSP_version2     (1<<2)
#define HDSP_wc_lock      (1<<3)
#define HDSP_wc_sync      (1<<4)
#define HDSP_inp_freq0    (1<<5)
#define HDSP_inp_freq1    (1<<6)
#define HDSP_inp_freq2    (1<<7)
#define HDSP_SelSyncRef0  (1<<8)
#define HDSP_SelSyncRef1  (1<<9)
#define HDSP_SelSyncRef2  (1<<10)

#define HDSP_wc_valid (HDSP_wc_lock|HDSP_wc_sync)

#define HDSP_systemFrequencyMask (HDSP_inp_freq0|HDSP_inp_freq1|HDSP_inp_freq2)
#define HDSP_systemFrequency32   (HDSP_inp_freq0)
#define HDSP_systemFrequency44_1 (HDSP_inp_freq1)
#define HDSP_systemFrequency48   (HDSP_inp_freq0|HDSP_inp_freq1)
#define HDSP_systemFrequency64   (HDSP_inp_freq2)
#define HDSP_systemFrequency88_2 (HDSP_inp_freq0|HDSP_inp_freq2)
#define HDSP_systemFrequency96   (HDSP_inp_freq1|HDSP_inp_freq2)
/* FIXME : more values for 9632 cards ? */

#define HDSP_SelSyncRefMask        (HDSP_SelSyncRef0|HDSP_SelSyncRef1|HDSP_SelSyncRef2)
#define HDSP_SelSyncRef_ADAT1      0
#define HDSP_SelSyncRef_ADAT2      (HDSP_SelSyncRef0)
#define HDSP_SelSyncRef_ADAT3      (HDSP_SelSyncRef1)
#define HDSP_SelSyncRef_SPDIF      (HDSP_SelSyncRef0|HDSP_SelSyncRef1)
#define HDSP_SelSyncRef_WORD       (HDSP_SelSyncRef2)
#define HDSP_SelSyncRef_ADAT_SYNC  (HDSP_SelSyncRef0|HDSP_SelSyncRef2)

/* Card state flags */

#define HDSP_InitializationComplete  (1<<0)
#define HDSP_FirmwareLoaded	     (1<<1)
#define HDSP_FirmwareCached	     (1<<2)

/* FIFO wait times, defined in terms of 1/10ths of msecs */

#define HDSP_LONG_WAIT	 5000
#define HDSP_SHORT_WAIT  30

#define UNITY_GAIN                       32768
#define MINUS_INFINITY_GAIN              0

/* the size of a substream (1 mono data stream) */

#define HDSP_CHANNEL_BUFFER_SAMPLES  (16*1024)
#define HDSP_CHANNEL_BUFFER_BYTES    (4*HDSP_CHANNEL_BUFFER_SAMPLES)

/* the size of the area we need to allocate for DMA transfers. the
   size is the same regardless of the number of channels - the
   Multiface still uses the same memory area.

   Note that we allocate 1 more channel than is apparently needed
   because the h/w seems to write 1 byte beyond the end of the last
   page. Sigh.
*/

#define HDSP_DMA_AREA_BYTES ((HDSP_MAX_CHANNELS+1) * HDSP_CHANNEL_BUFFER_BYTES)
#define HDSP_DMA_AREA_KILOBYTES (HDSP_DMA_AREA_BYTES/1024)

/* use hotplug firmware loader? */
#if defined(CONFIG_FW_LOADER) || defined(CONFIG_FW_LOADER_MODULE)
#if !defined(HDSP_USE_HWDEP_LOADER)
#define HDSP_FW_LOADER
#endif
#endif

struct hdsp_9632_meters {
    u32 input_peak[16];
    u32 playback_peak[16];
    u32 output_peak[16];
    u32 xxx_peak[16];
    u32 padding[64];
    u32 input_rms_low[16];
    u32 playback_rms_low[16];
    u32 output_rms_low[16];
    u32 xxx_rms_low[16];
    u32 input_rms_high[16];
    u32 playback_rms_high[16];
    u32 output_rms_high[16];
    u32 xxx_rms_high[16];
};

struct hdsp_midi {
    struct hdsp             *hdsp;
    int                      id;
    struct snd_rawmidi           *rmidi;
    struct snd_rawmidi_substream *input;
    struct snd_rawmidi_substream *output;
    char                     istimer; /* timer in use */
    struct timer_list	     timer;
    spinlock_t               lock;
    int			     pending;
};

struct hdsp {
	spinlock_t            lock;
	struct snd_pcm_substream *capture_substream;
	struct snd_pcm_substream *playback_substream;
        struct hdsp_midi      midi[2];
	struct tasklet_struct midi_tasklet;
	int		      use_midi_tasklet;
	int                   precise_ptr;
	u32                   control_register;	     /* cached value */
	u32                   control2_register;     /* cached value */
	u32                   creg_spdif;
	u32                   creg_spdif_stream;
	int                   clock_source_locked;
	char                 *card_name;	     /* digiface/multiface */
	enum HDSP_IO_Type     io_type;               /* ditto, but for code use */
        unsigned short        firmware_rev;
	unsigned short	      state;		     /* stores state bits */
	u32		      firmware_cache[24413]; /* this helps recover from accidental iobox power failure */
	size_t                period_bytes; 	     /* guess what this is */
	unsigned char	      max_channels;
	unsigned char	      qs_in_channels;	     /* quad speed mode for H9632 */
	unsigned char         ds_in_channels;
	unsigned char         ss_in_channels;	    /* different for multiface/digiface */
	unsigned char	      qs_out_channels;
	unsigned char         ds_out_channels;
	unsigned char         ss_out_channels;

	struct snd_dma_buffer capture_dma_buf;
	struct snd_dma_buffer playback_dma_buf;
	unsigned char        *capture_buffer;	    /* suitably aligned address */
	unsigned char        *playback_buffer;	    /* suitably aligned address */

	pid_t                 capture_pid;
	pid_t                 playback_pid;
	int                   running;
	int                   system_sample_rate;
	char                 *channel_map;
	int                   dev;
	int                   irq;
	unsigned long         port;
        void __iomem         *iobase;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_hwdep          *hwdep;
	struct pci_dev       *pci;
	struct snd_kcontrol *spdif_ctl;
        unsigned short        mixer_matrix[HDSP_MATRIX_MIXER_SIZE];
	unsigned int          dds_value; /* last value written to freq register */
};

/* These tables map the ALSA channels 1..N to the channels that we
   need to use in order to find the relevant channel buffer. RME
   refer to this kind of mapping as between "the ADAT channel and
   the DMA channel." We index it using the logical audio channel,
   and the value is the DMA channel (i.e. channel buffer number)
   where the data for that channel can be read/written from/to.
*/

static char channel_map_df_ss[HDSP_MAX_CHANNELS] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23, 24, 25
};

static char channel_map_mf_ss[HDSP_MAX_CHANNELS] = { /* Multiface */
	/* Analog */
	0, 1, 2, 3, 4, 5, 6, 7,
	/* ADAT 2 */
	16, 17, 18, 19, 20, 21, 22, 23,
	/* SPDIF */
	24, 25,
	-1, -1, -1, -1, -1, -1, -1, -1
};

static char channel_map_ds[HDSP_MAX_CHANNELS] = {
	/* ADAT channels are remapped */
	1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23,
	/* channels 12 and 13 are S/PDIF */
	24, 25,
	/* others don't exist */
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static char channel_map_H9632_ss[HDSP_MAX_CHANNELS] = {
	/* ADAT channels */
	0, 1, 2, 3, 4, 5, 6, 7,
	/* SPDIF */
	8, 9,
	/* Analog */
	10, 11,
	/* AO4S-192 and AI4S-192 extension boards */
	12, 13, 14, 15,
	/* others don't exist */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1
};

static char channel_map_H9632_ds[HDSP_MAX_CHANNELS] = {
	/* ADAT */
	1, 3, 5, 7,
	/* SPDIF */
	8, 9,
	/* Analog */
	10, 11,
	/* AO4S-192 and AI4S-192 extension boards */
	12, 13, 14, 15,
	/* others don't exist */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1
};

static char channel_map_H9632_qs[HDSP_MAX_CHANNELS] = {
	/* ADAT is disabled in this mode */
	/* SPDIF */
	8, 9,
	/* Analog */
	10, 11,
	/* AO4S-192 and AI4S-192 extension boards */
	12, 13, 14, 15,
	/* others don't exist */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1
};

static int snd_hammerfall_get_buffer(struct pci_dev *pci, struct snd_dma_buffer *dmab, size_t size)
{
	dmab->dev.type = SNDRV_DMA_TYPE_DEV;
	dmab->dev.dev = snd_dma_pci_data(pci);
	if (snd_dma_get_reserved_buf(dmab, snd_dma_pci_buf_id(pci))) {
		if (dmab->bytes >= size)
			return 0;
	}
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
				size, dmab) < 0)
		return -ENOMEM;
	return 0;
}

static void snd_hammerfall_free_buffer(struct snd_dma_buffer *dmab, struct pci_dev *pci)
{
	if (dmab->area) {
		dmab->dev.dev = NULL; /* make it anonymous */
		snd_dma_reserve_buf(dmab, snd_dma_pci_buf_id(pci));
	}
}


static DEFINE_PCI_DEVICE_TABLE(snd_hdsp_ids) = {
	{
		.vendor = PCI_VENDOR_ID_XILINX,
		.device = PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	}, /* RME Hammerfall-DSP */
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, snd_hdsp_ids);

/* prototypes */
static int snd_hdsp_create_alsa_devices(struct snd_card *card, struct hdsp *hdsp);
static int snd_hdsp_create_pcm(struct snd_card *card, struct hdsp *hdsp);
static int snd_hdsp_enable_io (struct hdsp *hdsp);
static void snd_hdsp_initialize_midi_flush (struct hdsp *hdsp);
static void snd_hdsp_initialize_channels (struct hdsp *hdsp);
static int hdsp_fifo_wait(struct hdsp *hdsp, int count, int timeout);
static int hdsp_autosync_ref(struct hdsp *hdsp);
static int snd_hdsp_set_defaults(struct hdsp *hdsp);
static void snd_hdsp_9652_enable_mixer (struct hdsp *hdsp);

static int hdsp_playback_to_output_key (struct hdsp *hdsp, int in, int out)
{
	switch (hdsp->io_type) {
	case Multiface:
	case Digiface:
	default:
		if (hdsp->firmware_rev == 0xa)
			return (64 * out) + (32 + (in));
		else
			return (52 * out) + (26 + (in));
	case H9632:
		return (32 * out) + (16 + (in));
	case H9652:
		return (52 * out) + (26 + (in));
	}
}

static int hdsp_input_to_output_key (struct hdsp *hdsp, int in, int out)
{
	switch (hdsp->io_type) {
	case Multiface:
	case Digiface:
	default:
		if (hdsp->firmware_rev == 0xa)
			return (64 * out) + in;
		else
			return (52 * out) + in;
	case H9632:
		return (32 * out) + in;
	case H9652:
		return (52 * out) + in;
	}
}

static void hdsp_write(struct hdsp *hdsp, int reg, int val)
{
	writel(val, hdsp->iobase + reg);
}

static unsigned int hdsp_read(struct hdsp *hdsp, int reg)
{
	return readl (hdsp->iobase + reg);
}

static int hdsp_check_for_iobox (struct hdsp *hdsp)
{
	if (hdsp->io_type == H9652 || hdsp->io_type == H9632) return 0;
	if (hdsp_read (hdsp, HDSP_statusRegister) & HDSP_ConfigError) {
		snd_printk ("Hammerfall-DSP: no Digiface or Multiface connected!\n");
		hdsp->state &= ~HDSP_FirmwareLoaded;
		return -EIO;
	}
	return 0;
}

static int hdsp_wait_for_iobox(struct hdsp *hdsp, unsigned int loops,
			       unsigned int delay)
{
	unsigned int i;

	if (hdsp->io_type == H9652 || hdsp->io_type == H9632)
		return 0;

	for (i = 0; i != loops; ++i) {
		if (hdsp_read(hdsp, HDSP_statusRegister) & HDSP_ConfigError)
			msleep(delay);
		else {
			snd_printd("Hammerfall-DSP: iobox found after %ums!\n",
				   i * delay);
			return 0;
		}
	}

	snd_printk("Hammerfall-DSP: no Digiface or Multiface connected!\n");
	hdsp->state &= ~HDSP_FirmwareLoaded;
	return -EIO;
}

static int snd_hdsp_load_firmware_from_cache(struct hdsp *hdsp) {

	int i;
	unsigned long flags;

	if ((hdsp_read (hdsp, HDSP_statusRegister) & HDSP_DllError) != 0) {

		snd_printk ("Hammerfall-DSP: loading firmware\n");

		hdsp_write (hdsp, HDSP_control2Reg, HDSP_S_PROGRAM);
		hdsp_write (hdsp, HDSP_fifoData, 0);

		if (hdsp_fifo_wait (hdsp, 0, HDSP_LONG_WAIT)) {
			snd_printk ("Hammerfall-DSP: timeout waiting for download preparation\n");
			return -EIO;
		}

		hdsp_write (hdsp, HDSP_control2Reg, HDSP_S_LOAD);

		for (i = 0; i < 24413; ++i) {
			hdsp_write(hdsp, HDSP_fifoData, hdsp->firmware_cache[i]);
			if (hdsp_fifo_wait (hdsp, 127, HDSP_LONG_WAIT)) {
				snd_printk ("Hammerfall-DSP: timeout during firmware loading\n");
				return -EIO;
			}
		}

		ssleep(3);

		if (hdsp_fifo_wait (hdsp, 0, HDSP_LONG_WAIT)) {
			snd_printk ("Hammerfall-DSP: timeout at end of firmware loading\n");
		    	return -EIO;
		}

#ifdef SNDRV_BIG_ENDIAN
		hdsp->control2_register = HDSP_BIGENDIAN_MODE;
#else
		hdsp->control2_register = 0;
#endif
		hdsp_write (hdsp, HDSP_control2Reg, hdsp->control2_register);
		snd_printk ("Hammerfall-DSP: finished firmware loading\n");

	}
	if (hdsp->state & HDSP_InitializationComplete) {
		snd_printk(KERN_INFO "Hammerfall-DSP: firmware loaded from cache, restoring defaults\n");
		spin_lock_irqsave(&hdsp->lock, flags);
		snd_hdsp_set_defaults(hdsp);
		spin_unlock_irqrestore(&hdsp->lock, flags);
	}

	hdsp->state |= HDSP_FirmwareLoaded;

	return 0;
}

static int hdsp_get_iobox_version (struct hdsp *hdsp)
{
	if ((hdsp_read (hdsp, HDSP_statusRegister) & HDSP_DllError) != 0) {

		hdsp_write (hdsp, HDSP_control2Reg, HDSP_PROGRAM);
		hdsp_write (hdsp, HDSP_fifoData, 0);
		if (hdsp_fifo_wait (hdsp, 0, HDSP_SHORT_WAIT) < 0)
			return -EIO;

		hdsp_write (hdsp, HDSP_control2Reg, HDSP_S_LOAD);
		hdsp_write (hdsp, HDSP_fifoData, 0);

		if (hdsp_fifo_wait (hdsp, 0, HDSP_SHORT_WAIT)) {
			hdsp->io_type = Multiface;
			hdsp_write (hdsp, HDSP_control2Reg, HDSP_VERSION_BIT);
			hdsp_write (hdsp, HDSP_control2Reg, HDSP_S_LOAD);
			hdsp_fifo_wait (hdsp, 0, HDSP_SHORT_WAIT);
		} else {
			hdsp->io_type = Digiface;
		}
	} else {
		/* firmware was already loaded, get iobox type */
		if (hdsp_read(hdsp, HDSP_status2Register) & HDSP_version1)
			hdsp->io_type = Multiface;
		else
			hdsp->io_type = Digiface;
	}
	return 0;
}


#ifdef HDSP_FW_LOADER
static int hdsp_request_fw_loader(struct hdsp *hdsp);
#endif

static int hdsp_check_for_firmware (struct hdsp *hdsp, int load_on_demand)
{
	if (hdsp->io_type == H9652 || hdsp->io_type == H9632)
		return 0;
	if ((hdsp_read (hdsp, HDSP_statusRegister) & HDSP_DllError) != 0) {
		hdsp->state &= ~HDSP_FirmwareLoaded;
		if (! load_on_demand)
			return -EIO;
		snd_printk(KERN_ERR "Hammerfall-DSP: firmware not present.\n");
		/* try to load firmware */
		if (! (hdsp->state & HDSP_FirmwareCached)) {
#ifdef HDSP_FW_LOADER
			if (! hdsp_request_fw_loader(hdsp))
				return 0;
#endif
			snd_printk(KERN_ERR
				   "Hammerfall-DSP: No firmware loaded nor "
				   "cached, please upload firmware.\n");
			return -EIO;
		}
		if (snd_hdsp_load_firmware_from_cache(hdsp) != 0) {
			snd_printk(KERN_ERR
				   "Hammerfall-DSP: Firmware loading from "
				   "cache failed, please upload manually.\n");
			return -EIO;
		}
	}
	return 0;
}


static int hdsp_fifo_wait(struct hdsp *hdsp, int count, int timeout)
{
	int i;

	/* the fifoStatus registers reports on how many words
	   are available in the command FIFO.
	*/

	for (i = 0; i < timeout; i++) {

		if ((int)(hdsp_read (hdsp, HDSP_fifoStatus) & 0xff) <= count)
			return 0;

		/* not very friendly, but we only do this during a firmware
		   load and changing the mixer, so we just put up with it.
		*/

		udelay (100);
	}

	snd_printk ("Hammerfall-DSP: wait for FIFO status <= %d failed after %d iterations\n",
		    count, timeout);
	return -1;
}

static int hdsp_read_gain (struct hdsp *hdsp, unsigned int addr)
{
	if (addr >= HDSP_MATRIX_MIXER_SIZE)
		return 0;

	return hdsp->mixer_matrix[addr];
}

static int hdsp_write_gain(struct hdsp *hdsp, unsigned int addr, unsigned short data)
{
	unsigned int ad;

	if (addr >= HDSP_MATRIX_MIXER_SIZE)
		return -1;

	if (hdsp->io_type == H9652 || hdsp->io_type == H9632) {

		/* from martin bjornsen:

		   "You can only write dwords to the
		   mixer memory which contain two
		   mixer values in the low and high
		   word. So if you want to change
		   value 0 you have to read value 1
		   from the cache and write both to
		   the first dword in the mixer
		   memory."
		*/

		if (hdsp->io_type == H9632 && addr >= 512)
			return 0;

		if (hdsp->io_type == H9652 && addr >= 1352)
			return 0;

		hdsp->mixer_matrix[addr] = data;


		/* `addr' addresses a 16-bit wide address, but
		   the address space accessed via hdsp_write
		   uses byte offsets. put another way, addr
		   varies from 0 to 1351, but to access the
		   corresponding memory location, we need
		   to access 0 to 2703 ...
		*/
		ad = addr/2;

		hdsp_write (hdsp, 4096 + (ad*4),
			    (hdsp->mixer_matrix[(addr&0x7fe)+1] << 16) +
			    hdsp->mixer_matrix[addr&0x7fe]);

		return 0;

	} else {

		ad = (addr << 16) + data;

		if (hdsp_fifo_wait(hdsp, 127, HDSP_LONG_WAIT))
			return -1;

		hdsp_write (hdsp, HDSP_fifoData, ad);
		hdsp->mixer_matrix[addr] = data;

	}

	return 0;
}

static int snd_hdsp_use_is_exclusive(struct hdsp *hdsp)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&hdsp->lock, flags);
	if ((hdsp->playback_pid != hdsp->capture_pid) &&
	    (hdsp->playback_pid >= 0) && (hdsp->capture_pid >= 0))
		ret = 0;
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return ret;
}

static int hdsp_spdif_sample_rate(struct hdsp *hdsp)
{
	unsigned int status = hdsp_read(hdsp, HDSP_statusRegister);
	unsigned int rate_bits = (status & HDSP_spdifFrequencyMask);

	/* For the 9632, the mask is different */
	if (hdsp->io_type == H9632)
		 rate_bits = (status & HDSP_spdifFrequencyMask_9632);

	if (status & HDSP_SPDIFErrorFlag)
		return 0;

	switch (rate_bits) {
	case HDSP_spdifFrequency32KHz: return 32000;
	case HDSP_spdifFrequency44_1KHz: return 44100;
	case HDSP_spdifFrequency48KHz: return 48000;
	case HDSP_spdifFrequency64KHz: return 64000;
	case HDSP_spdifFrequency88_2KHz: return 88200;
	case HDSP_spdifFrequency96KHz: return 96000;
	case HDSP_spdifFrequency128KHz:
		if (hdsp->io_type == H9632) return 128000;
		break;
	case HDSP_spdifFrequency176_4KHz:
		if (hdsp->io_type == H9632) return 176400;
		break;
	case HDSP_spdifFrequency192KHz:
		if (hdsp->io_type == H9632) return 192000;
		break;
	default:
		break;
	}
	snd_printk ("Hammerfall-DSP: unknown spdif frequency status; bits = 0x%x, status = 0x%x\n", rate_bits, status);
	return 0;
}

static int hdsp_external_sample_rate(struct hdsp *hdsp)
{
	unsigned int status2 = hdsp_read(hdsp, HDSP_status2Register);
	unsigned int rate_bits = status2 & HDSP_systemFrequencyMask;

	/* For the 9632 card, there seems to be no bit for indicating external
	 * sample rate greater than 96kHz. The card reports the corresponding
	 * single speed. So the best means seems to get spdif rate when
	 * autosync reference is spdif */
	if (hdsp->io_type == H9632 &&
	    hdsp_autosync_ref(hdsp) == HDSP_AUTOSYNC_FROM_SPDIF)
		 return hdsp_spdif_sample_rate(hdsp);

	switch (rate_bits) {
	case HDSP_systemFrequency32:   return 32000;
	case HDSP_systemFrequency44_1: return 44100;
	case HDSP_systemFrequency48:   return 48000;
	case HDSP_systemFrequency64:   return 64000;
	case HDSP_systemFrequency88_2: return 88200;
	case HDSP_systemFrequency96:   return 96000;
	default:
		return 0;
	}
}

static void hdsp_compute_period_size(struct hdsp *hdsp)
{
	hdsp->period_bytes = 1 << ((hdsp_decode_latency(hdsp->control_register) + 8));
}

static snd_pcm_uframes_t hdsp_hw_pointer(struct hdsp *hdsp)
{
	int position;

	position = hdsp_read(hdsp, HDSP_statusRegister);

	if (!hdsp->precise_ptr)
		return (position & HDSP_BufferID) ? (hdsp->period_bytes / 4) : 0;

	position &= HDSP_BufferPositionMask;
	position /= 4;
	position &= (hdsp->period_bytes/2) - 1;
	return position;
}

static void hdsp_reset_hw_pointer(struct hdsp *hdsp)
{
	hdsp_write (hdsp, HDSP_resetPointer, 0);
	if (hdsp->io_type == H9632 && hdsp->firmware_rev >= 152)
		/* HDSP_resetPointer = HDSP_freqReg, which is strange and
		 * requires (?) to write again DDS value after a reset pointer
		 * (at least, it works like this) */
		hdsp_write (hdsp, HDSP_freqReg, hdsp->dds_value);
}

static void hdsp_start_audio(struct hdsp *s)
{
	s->control_register |= (HDSP_AudioInterruptEnable | HDSP_Start);
	hdsp_write(s, HDSP_controlRegister, s->control_register);
}

static void hdsp_stop_audio(struct hdsp *s)
{
	s->control_register &= ~(HDSP_Start | HDSP_AudioInterruptEnable);
	hdsp_write(s, HDSP_controlRegister, s->control_register);
}

static void hdsp_silence_playback(struct hdsp *hdsp)
{
	memset(hdsp->playback_buffer, 0, HDSP_DMA_AREA_BYTES);
}

static int hdsp_set_interrupt_interval(struct hdsp *s, unsigned int frames)
{
	int n;

	spin_lock_irq(&s->lock);

	frames >>= 7;
	n = 0;
	while (frames) {
		n++;
		frames >>= 1;
	}

	s->control_register &= ~HDSP_LatencyMask;
	s->control_register |= hdsp_encode_latency(n);

	hdsp_write(s, HDSP_controlRegister, s->control_register);

	hdsp_compute_period_size(s);

	spin_unlock_irq(&s->lock);

	return 0;
}

static void hdsp_set_dds_value(struct hdsp *hdsp, int rate)
{
	u64 n;

	if (rate >= 112000)
		rate /= 4;
	else if (rate >= 56000)
		rate /= 2;

	n = DDS_NUMERATOR;
	n = div_u64(n, rate);
	/* n should be less than 2^32 for being written to FREQ register */
	snd_BUG_ON(n >> 32);
	/* HDSP_freqReg and HDSP_resetPointer are the same, so keep the DDS
	   value to write it after a reset */
	hdsp->dds_value = n;
	hdsp_write(hdsp, HDSP_freqReg, hdsp->dds_value);
}

static int hdsp_set_rate(struct hdsp *hdsp, int rate, int called_internally)
{
	int reject_if_open = 0;
	int current_rate;
	int rate_bits;

	/* ASSUMPTION: hdsp->lock is either held, or
	   there is no need for it (e.g. during module
	   initialization).
	*/

	if (!(hdsp->control_register & HDSP_ClockModeMaster)) {
		if (called_internally) {
			/* request from ctl or card initialization */
			snd_printk(KERN_ERR "Hammerfall-DSP: device is not running as a clock master: cannot set sample rate.\n");
			return -1;
		} else {
			/* hw_param request while in AutoSync mode */
			int external_freq = hdsp_external_sample_rate(hdsp);
			int spdif_freq = hdsp_spdif_sample_rate(hdsp);

			if ((spdif_freq == external_freq*2) && (hdsp_autosync_ref(hdsp) >= HDSP_AUTOSYNC_FROM_ADAT1))
				snd_printk(KERN_INFO "Hammerfall-DSP: Detected ADAT in double speed mode\n");
			else if (hdsp->io_type == H9632 && (spdif_freq == external_freq*4) && (hdsp_autosync_ref(hdsp) >= HDSP_AUTOSYNC_FROM_ADAT1))
				snd_printk(KERN_INFO "Hammerfall-DSP: Detected ADAT in quad speed mode\n");
			else if (rate != external_freq) {
				snd_printk(KERN_INFO "Hammerfall-DSP: No AutoSync source for requested rate\n");
				return -1;
			}
		}
	}

	current_rate = hdsp->system_sample_rate;

	/* Changing from a "single speed" to a "double speed" rate is
	   not allowed if any substreams are open. This is because
	   such a change causes a shift in the location of
	   the DMA buffers and a reduction in the number of available
	   buffers.

	   Note that a similar but essentially insoluble problem
	   exists for externally-driven rate changes. All we can do
	   is to flag rate changes in the read/write routines.  */

	if (rate > 96000 && hdsp->io_type != H9632)
		return -EINVAL;

	switch (rate) {
	case 32000:
		if (current_rate > 48000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency32KHz;
		break;
	case 44100:
		if (current_rate > 48000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency44_1KHz;
		break;
	case 48000:
		if (current_rate > 48000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency48KHz;
		break;
	case 64000:
		if (current_rate <= 48000 || current_rate > 96000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency64KHz;
		break;
	case 88200:
		if (current_rate <= 48000 || current_rate > 96000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency88_2KHz;
		break;
	case 96000:
		if (current_rate <= 48000 || current_rate > 96000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency96KHz;
		break;
	case 128000:
		if (current_rate < 128000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency128KHz;
		break;
	case 176400:
		if (current_rate < 128000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency176_4KHz;
		break;
	case 192000:
		if (current_rate < 128000)
			reject_if_open = 1;
		rate_bits = HDSP_Frequency192KHz;
		break;
	default:
		return -EINVAL;
	}

	if (reject_if_open && (hdsp->capture_pid >= 0 || hdsp->playback_pid >= 0)) {
		snd_printk ("Hammerfall-DSP: cannot change speed mode (capture PID = %d, playback PID = %d)\n",
			    hdsp->capture_pid,
			    hdsp->playback_pid);
		return -EBUSY;
	}

	hdsp->control_register &= ~HDSP_FrequencyMask;
	hdsp->control_register |= rate_bits;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);

	/* For HDSP9632 rev 152, need to set DDS value in FREQ register */
	if (hdsp->io_type == H9632 && hdsp->firmware_rev >= 152)
		hdsp_set_dds_value(hdsp, rate);

	if (rate >= 128000) {
		hdsp->channel_map = channel_map_H9632_qs;
	} else if (rate > 48000) {
		if (hdsp->io_type == H9632)
			hdsp->channel_map = channel_map_H9632_ds;
		else
			hdsp->channel_map = channel_map_ds;
	} else {
		switch (hdsp->io_type) {
		case Multiface:
			hdsp->channel_map = channel_map_mf_ss;
			break;
		case Digiface:
		case H9652:
			hdsp->channel_map = channel_map_df_ss;
			break;
		case H9632:
			hdsp->channel_map = channel_map_H9632_ss;
			break;
		default:
			/* should never happen */
			break;
		}
	}

	hdsp->system_sample_rate = rate;

	return 0;
}

/*----------------------------------------------------------------------------
   MIDI
  ----------------------------------------------------------------------------*/

static unsigned char snd_hdsp_midi_read_byte (struct hdsp *hdsp, int id)
{
	/* the hardware already does the relevant bit-mask with 0xff */
	if (id)
		return hdsp_read(hdsp, HDSP_midiDataIn1);
	else
		return hdsp_read(hdsp, HDSP_midiDataIn0);
}

static void snd_hdsp_midi_write_byte (struct hdsp *hdsp, int id, int val)
{
	/* the hardware already does the relevant bit-mask with 0xff */
	if (id)
		hdsp_write(hdsp, HDSP_midiDataOut1, val);
	else
		hdsp_write(hdsp, HDSP_midiDataOut0, val);
}

static int snd_hdsp_midi_input_available (struct hdsp *hdsp, int id)
{
	if (id)
		return (hdsp_read(hdsp, HDSP_midiStatusIn1) & 0xff);
	else
		return (hdsp_read(hdsp, HDSP_midiStatusIn0) & 0xff);
}

static int snd_hdsp_midi_output_possible (struct hdsp *hdsp, int id)
{
	int fifo_bytes_used;

	if (id)
		fifo_bytes_used = hdsp_read(hdsp, HDSP_midiStatusOut1) & 0xff;
	else
		fifo_bytes_used = hdsp_read(hdsp, HDSP_midiStatusOut0) & 0xff;

	if (fifo_bytes_used < 128)
		return  128 - fifo_bytes_used;
	else
		return 0;
}

static void snd_hdsp_flush_midi_input (struct hdsp *hdsp, int id)
{
	while (snd_hdsp_midi_input_available (hdsp, id))
		snd_hdsp_midi_read_byte (hdsp, id);
}

static int snd_hdsp_midi_output_write (struct hdsp_midi *hmidi)
{
	unsigned long flags;
	int n_pending;
	int to_write;
	int i;
	unsigned char buf[128];

	/* Output is not interrupt driven */

	spin_lock_irqsave (&hmidi->lock, flags);
	if (hmidi->output) {
		if (!snd_rawmidi_transmit_empty (hmidi->output)) {
			if ((n_pending = snd_hdsp_midi_output_possible (hmidi->hdsp, hmidi->id)) > 0) {
				if (n_pending > (int)sizeof (buf))
					n_pending = sizeof (buf);

				if ((to_write = snd_rawmidi_transmit (hmidi->output, buf, n_pending)) > 0) {
					for (i = 0; i < to_write; ++i)
						snd_hdsp_midi_write_byte (hmidi->hdsp, hmidi->id, buf[i]);
				}
			}
		}
	}
	spin_unlock_irqrestore (&hmidi->lock, flags);
	return 0;
}

static int snd_hdsp_midi_input_read (struct hdsp_midi *hmidi)
{
	unsigned char buf[128]; /* this buffer is designed to match the MIDI input FIFO size */
	unsigned long flags;
	int n_pending;
	int i;

	spin_lock_irqsave (&hmidi->lock, flags);
	if ((n_pending = snd_hdsp_midi_input_available (hmidi->hdsp, hmidi->id)) > 0) {
		if (hmidi->input) {
			if (n_pending > (int)sizeof (buf))
				n_pending = sizeof (buf);
			for (i = 0; i < n_pending; ++i)
				buf[i] = snd_hdsp_midi_read_byte (hmidi->hdsp, hmidi->id);
			if (n_pending)
				snd_rawmidi_receive (hmidi->input, buf, n_pending);
		} else {
			/* flush the MIDI input FIFO */
			while (--n_pending)
				snd_hdsp_midi_read_byte (hmidi->hdsp, hmidi->id);
		}
	}
	hmidi->pending = 0;
	if (hmidi->id)
		hmidi->hdsp->control_register |= HDSP_Midi1InterruptEnable;
	else
		hmidi->hdsp->control_register |= HDSP_Midi0InterruptEnable;
	hdsp_write(hmidi->hdsp, HDSP_controlRegister, hmidi->hdsp->control_register);
	spin_unlock_irqrestore (&hmidi->lock, flags);
	return snd_hdsp_midi_output_write (hmidi);
}

static void snd_hdsp_midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct hdsp *hdsp;
	struct hdsp_midi *hmidi;
	unsigned long flags;
	u32 ie;

	hmidi = (struct hdsp_midi *) substream->rmidi->private_data;
	hdsp = hmidi->hdsp;
	ie = hmidi->id ? HDSP_Midi1InterruptEnable : HDSP_Midi0InterruptEnable;
	spin_lock_irqsave (&hdsp->lock, flags);
	if (up) {
		if (!(hdsp->control_register & ie)) {
			snd_hdsp_flush_midi_input (hdsp, hmidi->id);
			hdsp->control_register |= ie;
		}
	} else {
		hdsp->control_register &= ~ie;
		tasklet_kill(&hdsp->midi_tasklet);
	}

	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	spin_unlock_irqrestore (&hdsp->lock, flags);
}

static void snd_hdsp_midi_output_timer(unsigned long data)
{
	struct hdsp_midi *hmidi = (struct hdsp_midi *) data;
	unsigned long flags;

	snd_hdsp_midi_output_write(hmidi);
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

static void snd_hdsp_midi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct hdsp_midi *hmidi;
	unsigned long flags;

	hmidi = (struct hdsp_midi *) substream->rmidi->private_data;
	spin_lock_irqsave (&hmidi->lock, flags);
	if (up) {
		if (!hmidi->istimer) {
			init_timer(&hmidi->timer);
			hmidi->timer.function = snd_hdsp_midi_output_timer;
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
		snd_hdsp_midi_output_write(hmidi);
}

static int snd_hdsp_midi_input_open(struct snd_rawmidi_substream *substream)
{
	struct hdsp_midi *hmidi;

	hmidi = (struct hdsp_midi *) substream->rmidi->private_data;
	spin_lock_irq (&hmidi->lock);
	snd_hdsp_flush_midi_input (hmidi->hdsp, hmidi->id);
	hmidi->input = substream;
	spin_unlock_irq (&hmidi->lock);

	return 0;
}

static int snd_hdsp_midi_output_open(struct snd_rawmidi_substream *substream)
{
	struct hdsp_midi *hmidi;

	hmidi = (struct hdsp_midi *) substream->rmidi->private_data;
	spin_lock_irq (&hmidi->lock);
	hmidi->output = substream;
	spin_unlock_irq (&hmidi->lock);

	return 0;
}

static int snd_hdsp_midi_input_close(struct snd_rawmidi_substream *substream)
{
	struct hdsp_midi *hmidi;

	snd_hdsp_midi_input_trigger (substream, 0);

	hmidi = (struct hdsp_midi *) substream->rmidi->private_data;
	spin_lock_irq (&hmidi->lock);
	hmidi->input = NULL;
	spin_unlock_irq (&hmidi->lock);

	return 0;
}

static int snd_hdsp_midi_output_close(struct snd_rawmidi_substream *substream)
{
	struct hdsp_midi *hmidi;

	snd_hdsp_midi_output_trigger (substream, 0);

	hmidi = (struct hdsp_midi *) substream->rmidi->private_data;
	spin_lock_irq (&hmidi->lock);
	hmidi->output = NULL;
	spin_unlock_irq (&hmidi->lock);

	return 0;
}

static struct snd_rawmidi_ops snd_hdsp_midi_output =
{
	.open =		snd_hdsp_midi_output_open,
	.close =	snd_hdsp_midi_output_close,
	.trigger =	snd_hdsp_midi_output_trigger,
};

static struct snd_rawmidi_ops snd_hdsp_midi_input =
{
	.open =		snd_hdsp_midi_input_open,
	.close =	snd_hdsp_midi_input_close,
	.trigger =	snd_hdsp_midi_input_trigger,
};

static int snd_hdsp_create_midi (struct snd_card *card, struct hdsp *hdsp, int id)
{
	char buf[32];

	hdsp->midi[id].id = id;
	hdsp->midi[id].rmidi = NULL;
	hdsp->midi[id].input = NULL;
	hdsp->midi[id].output = NULL;
	hdsp->midi[id].hdsp = hdsp;
	hdsp->midi[id].istimer = 0;
	hdsp->midi[id].pending = 0;
	spin_lock_init (&hdsp->midi[id].lock);

	sprintf (buf, "%s MIDI %d", card->shortname, id+1);
	if (snd_rawmidi_new (card, buf, id, 1, 1, &hdsp->midi[id].rmidi) < 0)
		return -1;

	sprintf(hdsp->midi[id].rmidi->name, "HDSP MIDI %d", id+1);
	hdsp->midi[id].rmidi->private_data = &hdsp->midi[id];

	snd_rawmidi_set_ops (hdsp->midi[id].rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_hdsp_midi_output);
	snd_rawmidi_set_ops (hdsp->midi[id].rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_hdsp_midi_input);

	hdsp->midi[id].rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
		SNDRV_RAWMIDI_INFO_INPUT |
		SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}

/*-----------------------------------------------------------------------------
  Control Interface
  ----------------------------------------------------------------------------*/

static u32 snd_hdsp_convert_from_aes(struct snd_aes_iec958 *aes)
{
	u32 val = 0;
	val |= (aes->status[0] & IEC958_AES0_PROFESSIONAL) ? HDSP_SPDIFProfessional : 0;
	val |= (aes->status[0] & IEC958_AES0_NONAUDIO) ? HDSP_SPDIFNonAudio : 0;
	if (val & HDSP_SPDIFProfessional)
		val |= (aes->status[0] & IEC958_AES0_PRO_EMPHASIS_5015) ? HDSP_SPDIFEmphasis : 0;
	else
		val |= (aes->status[0] & IEC958_AES0_CON_EMPHASIS_5015) ? HDSP_SPDIFEmphasis : 0;
	return val;
}

static void snd_hdsp_convert_to_aes(struct snd_aes_iec958 *aes, u32 val)
{
	aes->status[0] = ((val & HDSP_SPDIFProfessional) ? IEC958_AES0_PROFESSIONAL : 0) |
			 ((val & HDSP_SPDIFNonAudio) ? IEC958_AES0_NONAUDIO : 0);
	if (val & HDSP_SPDIFProfessional)
		aes->status[0] |= (val & HDSP_SPDIFEmphasis) ? IEC958_AES0_PRO_EMPHASIS_5015 : 0;
	else
		aes->status[0] |= (val & HDSP_SPDIFEmphasis) ? IEC958_AES0_CON_EMPHASIS_5015 : 0;
}

static int snd_hdsp_control_spdif_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_control_spdif_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	snd_hdsp_convert_to_aes(&ucontrol->value.iec958, hdsp->creg_spdif);
	return 0;
}

static int snd_hdsp_control_spdif_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	u32 val;

	val = snd_hdsp_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irq(&hdsp->lock);
	change = val != hdsp->creg_spdif;
	hdsp->creg_spdif = val;
	spin_unlock_irq(&hdsp->lock);
	return change;
}

static int snd_hdsp_control_spdif_stream_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_control_spdif_stream_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	snd_hdsp_convert_to_aes(&ucontrol->value.iec958, hdsp->creg_spdif_stream);
	return 0;
}

static int snd_hdsp_control_spdif_stream_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	u32 val;

	val = snd_hdsp_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irq(&hdsp->lock);
	change = val != hdsp->creg_spdif_stream;
	hdsp->creg_spdif_stream = val;
	hdsp->control_register &= ~(HDSP_SPDIFProfessional | HDSP_SPDIFNonAudio | HDSP_SPDIFEmphasis);
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register |= val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

static int snd_hdsp_control_spdif_mask_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_control_spdif_mask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = kcontrol->private_value;
	return 0;
}

#define HDSP_SPDIF_IN(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_spdif_in, \
  .get = snd_hdsp_get_spdif_in, \
  .put = snd_hdsp_put_spdif_in }

static unsigned int hdsp_spdif_in(struct hdsp *hdsp)
{
	return hdsp_decode_spdif_in(hdsp->control_register & HDSP_SPDIFInputMask);
}

static int hdsp_set_spdif_input(struct hdsp *hdsp, int in)
{
	hdsp->control_register &= ~HDSP_SPDIFInputMask;
	hdsp->control_register |= hdsp_encode_spdif_in(in);
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_spdif_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {"Optical", "Coaxial", "Internal", "AES"};
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ((hdsp->io_type == H9632) ? 4 : 3);
	if (uinfo->value.enumerated.item > ((hdsp->io_type == H9632) ? 3 : 2))
		uinfo->value.enumerated.item = ((hdsp->io_type == H9632) ? 3 : 2);
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_spdif_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_spdif_in(hdsp);
	return 0;
}

static int snd_hdsp_put_spdif_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0] % ((hdsp->io_type == H9632) ? 4 : 3);
	spin_lock_irq(&hdsp->lock);
	change = val != hdsp_spdif_in(hdsp);
	if (change)
		hdsp_set_spdif_input(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_SPDIF_OUT(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_spdif_bits, \
  .get = snd_hdsp_get_spdif_out, .put = snd_hdsp_put_spdif_out }

static int hdsp_spdif_out(struct hdsp *hdsp)
{
	return (hdsp->control_register & HDSP_SPDIFOpticalOut) ? 1 : 0;
}

static int hdsp_set_spdif_output(struct hdsp *hdsp, int out)
{
	if (out)
		hdsp->control_register |= HDSP_SPDIFOpticalOut;
	else
		hdsp->control_register &= ~HDSP_SPDIFOpticalOut;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

#define snd_hdsp_info_spdif_bits	snd_ctl_boolean_mono_info

static int snd_hdsp_get_spdif_out(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = hdsp_spdif_out(hdsp);
	return 0;
}

static int snd_hdsp_put_spdif_out(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp_spdif_out(hdsp);
	hdsp_set_spdif_output(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_SPDIF_PROFESSIONAL(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_spdif_bits, \
  .get = snd_hdsp_get_spdif_professional, .put = snd_hdsp_put_spdif_professional }

static int hdsp_spdif_professional(struct hdsp *hdsp)
{
	return (hdsp->control_register & HDSP_SPDIFProfessional) ? 1 : 0;
}

static int hdsp_set_spdif_professional(struct hdsp *hdsp, int val)
{
	if (val)
		hdsp->control_register |= HDSP_SPDIFProfessional;
	else
		hdsp->control_register &= ~HDSP_SPDIFProfessional;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_get_spdif_professional(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = hdsp_spdif_professional(hdsp);
	return 0;
}

static int snd_hdsp_put_spdif_professional(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp_spdif_professional(hdsp);
	hdsp_set_spdif_professional(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_SPDIF_EMPHASIS(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_spdif_bits, \
  .get = snd_hdsp_get_spdif_emphasis, .put = snd_hdsp_put_spdif_emphasis }

static int hdsp_spdif_emphasis(struct hdsp *hdsp)
{
	return (hdsp->control_register & HDSP_SPDIFEmphasis) ? 1 : 0;
}

static int hdsp_set_spdif_emphasis(struct hdsp *hdsp, int val)
{
	if (val)
		hdsp->control_register |= HDSP_SPDIFEmphasis;
	else
		hdsp->control_register &= ~HDSP_SPDIFEmphasis;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_get_spdif_emphasis(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = hdsp_spdif_emphasis(hdsp);
	return 0;
}

static int snd_hdsp_put_spdif_emphasis(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp_spdif_emphasis(hdsp);
	hdsp_set_spdif_emphasis(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_SPDIF_NON_AUDIO(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_spdif_bits, \
  .get = snd_hdsp_get_spdif_nonaudio, .put = snd_hdsp_put_spdif_nonaudio }

static int hdsp_spdif_nonaudio(struct hdsp *hdsp)
{
	return (hdsp->control_register & HDSP_SPDIFNonAudio) ? 1 : 0;
}

static int hdsp_set_spdif_nonaudio(struct hdsp *hdsp, int val)
{
	if (val)
		hdsp->control_register |= HDSP_SPDIFNonAudio;
	else
		hdsp->control_register &= ~HDSP_SPDIFNonAudio;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_get_spdif_nonaudio(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = hdsp_spdif_nonaudio(hdsp);
	return 0;
}

static int snd_hdsp_put_spdif_nonaudio(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp_spdif_nonaudio(hdsp);
	hdsp_set_spdif_nonaudio(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_SPDIF_SAMPLE_RATE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdsp_info_spdif_sample_rate, \
  .get = snd_hdsp_get_spdif_sample_rate \
}

static int snd_hdsp_info_spdif_sample_rate(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"32000", "44100", "48000", "64000", "88200", "96000", "None", "128000", "176400", "192000"};
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = (hdsp->io_type == H9632) ? 10 : 7;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_spdif_sample_rate(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	switch (hdsp_spdif_sample_rate(hdsp)) {
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
		ucontrol->value.enumerated.item[0] = 7;
		break;
	case 176400:
		ucontrol->value.enumerated.item[0] = 8;
		break;
	case 192000:
		ucontrol->value.enumerated.item[0] = 9;
		break;
	default:
		ucontrol->value.enumerated.item[0] = 6;
	}
	return 0;
}

#define HDSP_SYSTEM_SAMPLE_RATE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdsp_info_system_sample_rate, \
  .get = snd_hdsp_get_system_sample_rate \
}

static int snd_hdsp_info_system_sample_rate(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_get_system_sample_rate(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp->system_sample_rate;
	return 0;
}

#define HDSP_AUTOSYNC_SAMPLE_RATE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdsp_info_autosync_sample_rate, \
  .get = snd_hdsp_get_autosync_sample_rate \
}

static int snd_hdsp_info_autosync_sample_rate(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	static char *texts[] = {"32000", "44100", "48000", "64000", "88200", "96000", "None", "128000", "176400", "192000"};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = (hdsp->io_type == H9632) ? 10 : 7 ;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_autosync_sample_rate(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	switch (hdsp_external_sample_rate(hdsp)) {
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
		ucontrol->value.enumerated.item[0] = 7;
		break;
	case 176400:
		ucontrol->value.enumerated.item[0] = 8;
		break;
	case 192000:
		ucontrol->value.enumerated.item[0] = 9;
		break;
	default:
		ucontrol->value.enumerated.item[0] = 6;
	}
	return 0;
}

#define HDSP_SYSTEM_CLOCK_MODE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdsp_info_system_clock_mode, \
  .get = snd_hdsp_get_system_clock_mode \
}

static int hdsp_system_clock_mode(struct hdsp *hdsp)
{
	if (hdsp->control_register & HDSP_ClockModeMaster)
		return 0;
	else if (hdsp_external_sample_rate(hdsp) != hdsp->system_sample_rate)
			return 0;
	return 1;
}

static int snd_hdsp_info_system_clock_mode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"Master", "Slave" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_system_clock_mode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_system_clock_mode(hdsp);
	return 0;
}

#define HDSP_CLOCK_SOURCE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_clock_source, \
  .get = snd_hdsp_get_clock_source, \
  .put = snd_hdsp_put_clock_source \
}

static int hdsp_clock_source(struct hdsp *hdsp)
{
	if (hdsp->control_register & HDSP_ClockModeMaster) {
		switch (hdsp->system_sample_rate) {
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

static int hdsp_set_clock_source(struct hdsp *hdsp, int mode)
{
	int rate;
	switch (mode) {
	case HDSP_CLOCK_SOURCE_AUTOSYNC:
		if (hdsp_external_sample_rate(hdsp) != 0) {
		    if (!hdsp_set_rate(hdsp, hdsp_external_sample_rate(hdsp), 1)) {
			hdsp->control_register &= ~HDSP_ClockModeMaster;
			hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
			return 0;
		    }
		}
		return -1;
	case HDSP_CLOCK_SOURCE_INTERNAL_32KHZ:
		rate = 32000;
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_44_1KHZ:
		rate = 44100;
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_48KHZ:
		rate = 48000;
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_64KHZ:
		rate = 64000;
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_88_2KHZ:
		rate = 88200;
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_96KHZ:
		rate = 96000;
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_128KHZ:
		rate = 128000;
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_176_4KHZ:
		rate = 176400;
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_192KHZ:
		rate = 192000;
		break;
	default:
		rate = 48000;
	}
	hdsp->control_register |= HDSP_ClockModeMaster;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	hdsp_set_rate(hdsp, rate, 1);
	return 0;
}

static int snd_hdsp_info_clock_source(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"AutoSync", "Internal 32.0 kHz", "Internal 44.1 kHz", "Internal 48.0 kHz", "Internal 64.0 kHz", "Internal 88.2 kHz", "Internal 96.0 kHz", "Internal 128 kHz", "Internal 176.4 kHz", "Internal 192.0 KHz" };
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	if (hdsp->io_type == H9632)
	    uinfo->value.enumerated.items = 10;
	else
	    uinfo->value.enumerated.items = 7;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_clock_source(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_clock_source(hdsp);
	return 0;
}

static int snd_hdsp_put_clock_source(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0];
	if (val < 0) val = 0;
	if (hdsp->io_type == H9632) {
		if (val > 9)
			val = 9;
	} else {
		if (val > 6)
			val = 6;
	}
	spin_lock_irq(&hdsp->lock);
	if (val != hdsp_clock_source(hdsp))
		change = (hdsp_set_clock_source(hdsp, val) == 0) ? 1 : 0;
	else
		change = 0;
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define snd_hdsp_info_clock_source_lock		snd_ctl_boolean_mono_info

static int snd_hdsp_get_clock_source_lock(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = hdsp->clock_source_locked;
	return 0;
}

static int snd_hdsp_put_clock_source_lock(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;

	change = (int)ucontrol->value.integer.value[0] != hdsp->clock_source_locked;
	if (change)
		hdsp->clock_source_locked = !!ucontrol->value.integer.value[0];
	return change;
}

#define HDSP_DA_GAIN(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_da_gain, \
  .get = snd_hdsp_get_da_gain, \
  .put = snd_hdsp_put_da_gain \
}

static int hdsp_da_gain(struct hdsp *hdsp)
{
	switch (hdsp->control_register & HDSP_DAGainMask) {
	case HDSP_DAGainHighGain:
		return 0;
	case HDSP_DAGainPlus4dBu:
		return 1;
	case HDSP_DAGainMinus10dBV:
		return 2;
	default:
		return 1;
	}
}

static int hdsp_set_da_gain(struct hdsp *hdsp, int mode)
{
	hdsp->control_register &= ~HDSP_DAGainMask;
	switch (mode) {
	case 0:
		hdsp->control_register |= HDSP_DAGainHighGain;
		break;
	case 1:
		hdsp->control_register |= HDSP_DAGainPlus4dBu;
		break;
	case 2:
		hdsp->control_register |= HDSP_DAGainMinus10dBV;
		break;
	default:
		return -1;

	}
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_da_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"Hi Gain", "+4 dBu", "-10 dbV"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_da_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_da_gain(hdsp);
	return 0;
}

static int snd_hdsp_put_da_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0];
	if (val < 0) val = 0;
	if (val > 2) val = 2;
	spin_lock_irq(&hdsp->lock);
	if (val != hdsp_da_gain(hdsp))
		change = (hdsp_set_da_gain(hdsp, val) == 0) ? 1 : 0;
	else
		change = 0;
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_AD_GAIN(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_ad_gain, \
  .get = snd_hdsp_get_ad_gain, \
  .put = snd_hdsp_put_ad_gain \
}

static int hdsp_ad_gain(struct hdsp *hdsp)
{
	switch (hdsp->control_register & HDSP_ADGainMask) {
	case HDSP_ADGainMinus10dBV:
		return 0;
	case HDSP_ADGainPlus4dBu:
		return 1;
	case HDSP_ADGainLowGain:
		return 2;
	default:
		return 1;
	}
}

static int hdsp_set_ad_gain(struct hdsp *hdsp, int mode)
{
	hdsp->control_register &= ~HDSP_ADGainMask;
	switch (mode) {
	case 0:
		hdsp->control_register |= HDSP_ADGainMinus10dBV;
		break;
	case 1:
		hdsp->control_register |= HDSP_ADGainPlus4dBu;
		break;
	case 2:
		hdsp->control_register |= HDSP_ADGainLowGain;
		break;
	default:
		return -1;

	}
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_ad_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"-10 dBV", "+4 dBu", "Lo Gain"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_ad_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_ad_gain(hdsp);
	return 0;
}

static int snd_hdsp_put_ad_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0];
	if (val < 0) val = 0;
	if (val > 2) val = 2;
	spin_lock_irq(&hdsp->lock);
	if (val != hdsp_ad_gain(hdsp))
		change = (hdsp_set_ad_gain(hdsp, val) == 0) ? 1 : 0;
	else
		change = 0;
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_PHONE_GAIN(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_phone_gain, \
  .get = snd_hdsp_get_phone_gain, \
  .put = snd_hdsp_put_phone_gain \
}

static int hdsp_phone_gain(struct hdsp *hdsp)
{
	switch (hdsp->control_register & HDSP_PhoneGainMask) {
	case HDSP_PhoneGain0dB:
		return 0;
	case HDSP_PhoneGainMinus6dB:
		return 1;
	case HDSP_PhoneGainMinus12dB:
		return 2;
	default:
		return 0;
	}
}

static int hdsp_set_phone_gain(struct hdsp *hdsp, int mode)
{
	hdsp->control_register &= ~HDSP_PhoneGainMask;
	switch (mode) {
	case 0:
		hdsp->control_register |= HDSP_PhoneGain0dB;
		break;
	case 1:
		hdsp->control_register |= HDSP_PhoneGainMinus6dB;
		break;
	case 2:
		hdsp->control_register |= HDSP_PhoneGainMinus12dB;
		break;
	default:
		return -1;

	}
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_phone_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"0 dB", "-6 dB", "-12 dB"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_phone_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_phone_gain(hdsp);
	return 0;
}

static int snd_hdsp_put_phone_gain(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0];
	if (val < 0) val = 0;
	if (val > 2) val = 2;
	spin_lock_irq(&hdsp->lock);
	if (val != hdsp_phone_gain(hdsp))
		change = (hdsp_set_phone_gain(hdsp, val) == 0) ? 1 : 0;
	else
		change = 0;
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_XLR_BREAKOUT_CABLE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_xlr_breakout_cable, \
  .get = snd_hdsp_get_xlr_breakout_cable, \
  .put = snd_hdsp_put_xlr_breakout_cable \
}

static int hdsp_xlr_breakout_cable(struct hdsp *hdsp)
{
	if (hdsp->control_register & HDSP_XLRBreakoutCable)
		return 1;
	return 0;
}

static int hdsp_set_xlr_breakout_cable(struct hdsp *hdsp, int mode)
{
	if (mode)
		hdsp->control_register |= HDSP_XLRBreakoutCable;
	else
		hdsp->control_register &= ~HDSP_XLRBreakoutCable;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

#define snd_hdsp_info_xlr_breakout_cable	snd_ctl_boolean_mono_info

static int snd_hdsp_get_xlr_breakout_cable(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_xlr_breakout_cable(hdsp);
	return 0;
}

static int snd_hdsp_put_xlr_breakout_cable(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp_xlr_breakout_cable(hdsp);
	hdsp_set_xlr_breakout_cable(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

/* (De)activates old RME Analog Extension Board
   These are connected to the internal ADAT connector
   Switching this on desactivates external ADAT
*/
#define HDSP_AEB(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_aeb, \
  .get = snd_hdsp_get_aeb, \
  .put = snd_hdsp_put_aeb \
}

static int hdsp_aeb(struct hdsp *hdsp)
{
	if (hdsp->control_register & HDSP_AnalogExtensionBoard)
		return 1;
	return 0;
}

static int hdsp_set_aeb(struct hdsp *hdsp, int mode)
{
	if (mode)
		hdsp->control_register |= HDSP_AnalogExtensionBoard;
	else
		hdsp->control_register &= ~HDSP_AnalogExtensionBoard;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

#define snd_hdsp_info_aeb		snd_ctl_boolean_mono_info

static int snd_hdsp_get_aeb(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_aeb(hdsp);
	return 0;
}

static int snd_hdsp_put_aeb(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp_aeb(hdsp);
	hdsp_set_aeb(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_PREF_SYNC_REF(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_pref_sync_ref, \
  .get = snd_hdsp_get_pref_sync_ref, \
  .put = snd_hdsp_put_pref_sync_ref \
}

static int hdsp_pref_sync_ref(struct hdsp *hdsp)
{
	/* Notice that this looks at the requested sync source,
	   not the one actually in use.
	*/

	switch (hdsp->control_register & HDSP_SyncRefMask) {
	case HDSP_SyncRef_ADAT1:
		return HDSP_SYNC_FROM_ADAT1;
	case HDSP_SyncRef_ADAT2:
		return HDSP_SYNC_FROM_ADAT2;
	case HDSP_SyncRef_ADAT3:
		return HDSP_SYNC_FROM_ADAT3;
	case HDSP_SyncRef_SPDIF:
		return HDSP_SYNC_FROM_SPDIF;
	case HDSP_SyncRef_WORD:
		return HDSP_SYNC_FROM_WORD;
	case HDSP_SyncRef_ADAT_SYNC:
		return HDSP_SYNC_FROM_ADAT_SYNC;
	default:
		return HDSP_SYNC_FROM_WORD;
	}
	return 0;
}

static int hdsp_set_pref_sync_ref(struct hdsp *hdsp, int pref)
{
	hdsp->control_register &= ~HDSP_SyncRefMask;
	switch (pref) {
	case HDSP_SYNC_FROM_ADAT1:
		hdsp->control_register &= ~HDSP_SyncRefMask; /* clear SyncRef bits */
		break;
	case HDSP_SYNC_FROM_ADAT2:
		hdsp->control_register |= HDSP_SyncRef_ADAT2;
		break;
	case HDSP_SYNC_FROM_ADAT3:
		hdsp->control_register |= HDSP_SyncRef_ADAT3;
		break;
	case HDSP_SYNC_FROM_SPDIF:
		hdsp->control_register |= HDSP_SyncRef_SPDIF;
		break;
	case HDSP_SYNC_FROM_WORD:
		hdsp->control_register |= HDSP_SyncRef_WORD;
		break;
	case HDSP_SYNC_FROM_ADAT_SYNC:
		hdsp->control_register |= HDSP_SyncRef_ADAT_SYNC;
		break;
	default:
		return -1;
	}
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_pref_sync_ref(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"Word", "IEC958", "ADAT1", "ADAT Sync", "ADAT2", "ADAT3" };
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	switch (hdsp->io_type) {
	case Digiface:
	case H9652:
		uinfo->value.enumerated.items = 6;
		break;
	case Multiface:
		uinfo->value.enumerated.items = 4;
		break;
	case H9632:
		uinfo->value.enumerated.items = 3;
		break;
	default:
		uinfo->value.enumerated.items = 0;
		break;
	}

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_pref_sync_ref(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_pref_sync_ref(hdsp);
	return 0;
}

static int snd_hdsp_put_pref_sync_ref(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change, max;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;

	switch (hdsp->io_type) {
	case Digiface:
	case H9652:
		max = 6;
		break;
	case Multiface:
		max = 4;
		break;
	case H9632:
		max = 3;
		break;
	default:
		return -EIO;
	}

	val = ucontrol->value.enumerated.item[0] % max;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp_pref_sync_ref(hdsp);
	hdsp_set_pref_sync_ref(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_AUTOSYNC_REF(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = snd_hdsp_info_autosync_ref, \
  .get = snd_hdsp_get_autosync_ref, \
}

static int hdsp_autosync_ref(struct hdsp *hdsp)
{
	/* This looks at the autosync selected sync reference */
	unsigned int status2 = hdsp_read(hdsp, HDSP_status2Register);

	switch (status2 & HDSP_SelSyncRefMask) {
	case HDSP_SelSyncRef_WORD:
		return HDSP_AUTOSYNC_FROM_WORD;
	case HDSP_SelSyncRef_ADAT_SYNC:
		return HDSP_AUTOSYNC_FROM_ADAT_SYNC;
	case HDSP_SelSyncRef_SPDIF:
		return HDSP_AUTOSYNC_FROM_SPDIF;
	case HDSP_SelSyncRefMask:
		return HDSP_AUTOSYNC_FROM_NONE;
	case HDSP_SelSyncRef_ADAT1:
		return HDSP_AUTOSYNC_FROM_ADAT1;
	case HDSP_SelSyncRef_ADAT2:
		return HDSP_AUTOSYNC_FROM_ADAT2;
	case HDSP_SelSyncRef_ADAT3:
		return HDSP_AUTOSYNC_FROM_ADAT3;
	default:
		return HDSP_AUTOSYNC_FROM_WORD;
	}
	return 0;
}

static int snd_hdsp_info_autosync_ref(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"Word", "ADAT Sync", "IEC958", "None", "ADAT1", "ADAT2", "ADAT3" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 7;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_autosync_ref(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_autosync_ref(hdsp);
	return 0;
}

#define HDSP_LINE_OUT(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_line_out, \
  .get = snd_hdsp_get_line_out, \
  .put = snd_hdsp_put_line_out \
}

static int hdsp_line_out(struct hdsp *hdsp)
{
	return (hdsp->control_register & HDSP_LineOut) ? 1 : 0;
}

static int hdsp_set_line_output(struct hdsp *hdsp, int out)
{
	if (out)
		hdsp->control_register |= HDSP_LineOut;
	else
		hdsp->control_register &= ~HDSP_LineOut;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

#define snd_hdsp_info_line_out		snd_ctl_boolean_mono_info

static int snd_hdsp_get_line_out(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdsp->lock);
	ucontrol->value.integer.value[0] = hdsp_line_out(hdsp);
	spin_unlock_irq(&hdsp->lock);
	return 0;
}

static int snd_hdsp_put_line_out(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp_line_out(hdsp);
	hdsp_set_line_output(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_PRECISE_POINTER(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_CARD, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_precise_pointer, \
  .get = snd_hdsp_get_precise_pointer, \
  .put = snd_hdsp_put_precise_pointer \
}

static int hdsp_set_precise_pointer(struct hdsp *hdsp, int precise)
{
	if (precise)
		hdsp->precise_ptr = 1;
	else
		hdsp->precise_ptr = 0;
	return 0;
}

#define snd_hdsp_info_precise_pointer		snd_ctl_boolean_mono_info

static int snd_hdsp_get_precise_pointer(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdsp->lock);
	ucontrol->value.integer.value[0] = hdsp->precise_ptr;
	spin_unlock_irq(&hdsp->lock);
	return 0;
}

static int snd_hdsp_put_precise_pointer(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp->precise_ptr;
	hdsp_set_precise_pointer(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_USE_MIDI_TASKLET(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_CARD, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_use_midi_tasklet, \
  .get = snd_hdsp_get_use_midi_tasklet, \
  .put = snd_hdsp_put_use_midi_tasklet \
}

static int hdsp_set_use_midi_tasklet(struct hdsp *hdsp, int use_tasklet)
{
	if (use_tasklet)
		hdsp->use_midi_tasklet = 1;
	else
		hdsp->use_midi_tasklet = 0;
	return 0;
}

#define snd_hdsp_info_use_midi_tasklet		snd_ctl_boolean_mono_info

static int snd_hdsp_get_use_midi_tasklet(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&hdsp->lock);
	ucontrol->value.integer.value[0] = hdsp->use_midi_tasklet;
	spin_unlock_irq(&hdsp->lock);
	return 0;
}

static int snd_hdsp_put_use_midi_tasklet(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&hdsp->lock);
	change = (int)val != hdsp->use_midi_tasklet;
	hdsp_set_use_midi_tasklet(hdsp, val);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_MIXER(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
		 SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_mixer, \
  .get = snd_hdsp_get_mixer, \
  .put = snd_hdsp_put_mixer \
}

static int snd_hdsp_info_mixer(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 65536;
	uinfo->value.integer.step = 1;
	return 0;
}

static int snd_hdsp_get_mixer(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int source;
	int destination;
	int addr;

	source = ucontrol->value.integer.value[0];
	destination = ucontrol->value.integer.value[1];

	if (source >= hdsp->max_channels)
		addr = hdsp_playback_to_output_key(hdsp,source-hdsp->max_channels,destination);
	else
		addr = hdsp_input_to_output_key(hdsp,source, destination);

	spin_lock_irq(&hdsp->lock);
	ucontrol->value.integer.value[2] = hdsp_read_gain (hdsp, addr);
	spin_unlock_irq(&hdsp->lock);
	return 0;
}

static int snd_hdsp_put_mixer(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	int source;
	int destination;
	int gain;
	int addr;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;

	source = ucontrol->value.integer.value[0];
	destination = ucontrol->value.integer.value[1];

	if (source >= hdsp->max_channels)
		addr = hdsp_playback_to_output_key(hdsp,source-hdsp->max_channels, destination);
	else
		addr = hdsp_input_to_output_key(hdsp,source, destination);

	gain = ucontrol->value.integer.value[2];

	spin_lock_irq(&hdsp->lock);
	change = gain != hdsp_read_gain(hdsp, addr);
	if (change)
		hdsp_write_gain(hdsp, addr, gain);
	spin_unlock_irq(&hdsp->lock);
	return change;
}

#define HDSP_WC_SYNC_CHECK(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_sync_check, \
  .get = snd_hdsp_get_wc_sync_check \
}

static int snd_hdsp_info_sync_check(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {"No Lock", "Lock", "Sync" };
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int hdsp_wc_sync_check(struct hdsp *hdsp)
{
	int status2 = hdsp_read(hdsp, HDSP_status2Register);
	if (status2 & HDSP_wc_lock) {
		if (status2 & HDSP_wc_sync)
			return 2;
		else
			 return 1;
	} else
		return 0;
	return 0;
}

static int snd_hdsp_get_wc_sync_check(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_wc_sync_check(hdsp);
	return 0;
}

#define HDSP_SPDIF_SYNC_CHECK(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_sync_check, \
  .get = snd_hdsp_get_spdif_sync_check \
}

static int hdsp_spdif_sync_check(struct hdsp *hdsp)
{
	int status = hdsp_read(hdsp, HDSP_statusRegister);
	if (status & HDSP_SPDIFErrorFlag)
		return 0;
	else {
		if (status & HDSP_SPDIFSync)
			return 2;
		else
			return 1;
	}
	return 0;
}

static int snd_hdsp_get_spdif_sync_check(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_spdif_sync_check(hdsp);
	return 0;
}

#define HDSP_ADATSYNC_SYNC_CHECK(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_sync_check, \
  .get = snd_hdsp_get_adatsync_sync_check \
}

static int hdsp_adatsync_sync_check(struct hdsp *hdsp)
{
	int status = hdsp_read(hdsp, HDSP_statusRegister);
	if (status & HDSP_TimecodeLock) {
		if (status & HDSP_TimecodeSync)
			return 2;
		else
			return 1;
	} else
		return 0;
}

static int snd_hdsp_get_adatsync_sync_check(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_adatsync_sync_check(hdsp);
	return 0;
}

#define HDSP_ADAT_SYNC_CHECK \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_sync_check, \
  .get = snd_hdsp_get_adat_sync_check \
}

static int hdsp_adat_sync_check(struct hdsp *hdsp, int idx)
{
	int status = hdsp_read(hdsp, HDSP_statusRegister);

	if (status & (HDSP_Lock0>>idx)) {
		if (status & (HDSP_Sync0>>idx))
			return 2;
		else
			return 1;
	} else
		return 0;
}

static int snd_hdsp_get_adat_sync_check(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int offset;
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	offset = ucontrol->id.index - 1;
	snd_BUG_ON(offset < 0);

	switch (hdsp->io_type) {
	case Digiface:
	case H9652:
		if (offset >= 3)
			return -EINVAL;
		break;
	case Multiface:
	case H9632:
		if (offset >= 1)
			return -EINVAL;
		break;
	default:
		return -EIO;
	}

	ucontrol->value.enumerated.item[0] = hdsp_adat_sync_check(hdsp, offset);
	return 0;
}

#define HDSP_DDS_OFFSET(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = xname, \
  .index = xindex, \
  .info = snd_hdsp_info_dds_offset, \
  .get = snd_hdsp_get_dds_offset, \
  .put = snd_hdsp_put_dds_offset \
}

static int hdsp_dds_offset(struct hdsp *hdsp)
{
	u64 n;
	unsigned int dds_value = hdsp->dds_value;
	int system_sample_rate = hdsp->system_sample_rate;

	if (!dds_value)
		return 0;

	n = DDS_NUMERATOR;
	/*
	 * dds_value = n / rate
	 * rate = n / dds_value
	 */
	n = div_u64(n, dds_value);
	if (system_sample_rate >= 112000)
		n *= 4;
	else if (system_sample_rate >= 56000)
		n *= 2;
	return ((int)n) - system_sample_rate;
}

static int hdsp_set_dds_offset(struct hdsp *hdsp, int offset_hz)
{
	int rate = hdsp->system_sample_rate + offset_hz;
	hdsp_set_dds_value(hdsp, rate);
	return 0;
}

static int snd_hdsp_info_dds_offset(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = -5000;
	uinfo->value.integer.max = 5000;
	return 0;
}

static int snd_hdsp_get_dds_offset(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = hdsp_dds_offset(hdsp);
	return 0;
}

static int snd_hdsp_put_dds_offset(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hdsp *hdsp = snd_kcontrol_chip(kcontrol);
	int change;
	int val;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0];
	spin_lock_irq(&hdsp->lock);
	if (val != hdsp_dds_offset(hdsp))
		change = (hdsp_set_dds_offset(hdsp, val) == 0) ? 1 : 0;
	else
		change = 0;
	spin_unlock_irq(&hdsp->lock);
	return change;
}

static struct snd_kcontrol_new snd_hdsp_9632_controls[] = {
HDSP_DA_GAIN("DA Gain", 0),
HDSP_AD_GAIN("AD Gain", 0),
HDSP_PHONE_GAIN("Phones Gain", 0),
HDSP_XLR_BREAKOUT_CABLE("XLR Breakout Cable", 0),
HDSP_DDS_OFFSET("DDS Sample Rate Offset", 0)
};

static struct snd_kcontrol_new snd_hdsp_controls[] = {
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		snd_hdsp_control_spdif_info,
	.get =		snd_hdsp_control_spdif_get,
	.put =		snd_hdsp_control_spdif_put,
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =		snd_hdsp_control_spdif_stream_info,
	.get =		snd_hdsp_control_spdif_stream_get,
	.put =		snd_hdsp_control_spdif_stream_put,
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	.info =		snd_hdsp_control_spdif_mask_info,
	.get =		snd_hdsp_control_spdif_mask_get,
	.private_value = IEC958_AES0_NONAUDIO |
  			 IEC958_AES0_PROFESSIONAL |
			 IEC958_AES0_CON_EMPHASIS,
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
	.info =		snd_hdsp_control_spdif_mask_info,
	.get =		snd_hdsp_control_spdif_mask_get,
	.private_value = IEC958_AES0_NONAUDIO |
			 IEC958_AES0_PROFESSIONAL |
			 IEC958_AES0_PRO_EMPHASIS,
},
HDSP_MIXER("Mixer", 0),
HDSP_SPDIF_IN("IEC958 Input Connector", 0),
HDSP_SPDIF_OUT("IEC958 Output also on ADAT1", 0),
HDSP_SPDIF_PROFESSIONAL("IEC958 Professional Bit", 0),
HDSP_SPDIF_EMPHASIS("IEC958 Emphasis Bit", 0),
HDSP_SPDIF_NON_AUDIO("IEC958 Non-audio Bit", 0),
/* 'Sample Clock Source' complies with the alsa control naming scheme */
HDSP_CLOCK_SOURCE("Sample Clock Source", 0),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Sample Clock Source Locking",
	.info = snd_hdsp_info_clock_source_lock,
	.get = snd_hdsp_get_clock_source_lock,
	.put = snd_hdsp_put_clock_source_lock,
},
HDSP_SYSTEM_CLOCK_MODE("System Clock Mode", 0),
HDSP_PREF_SYNC_REF("Preferred Sync Reference", 0),
HDSP_AUTOSYNC_REF("AutoSync Reference", 0),
HDSP_SPDIF_SAMPLE_RATE("SPDIF Sample Rate", 0),
HDSP_SYSTEM_SAMPLE_RATE("System Sample Rate", 0),
/* 'External Rate' complies with the alsa control naming scheme */
HDSP_AUTOSYNC_SAMPLE_RATE("External Rate", 0),
HDSP_WC_SYNC_CHECK("Word Clock Lock Status", 0),
HDSP_SPDIF_SYNC_CHECK("SPDIF Lock Status", 0),
HDSP_ADATSYNC_SYNC_CHECK("ADAT Sync Lock Status", 0),
HDSP_LINE_OUT("Line Out", 0),
HDSP_PRECISE_POINTER("Precise Pointer", 0),
HDSP_USE_MIDI_TASKLET("Use Midi Tasklet", 0),
};

static struct snd_kcontrol_new snd_hdsp_96xx_aeb = HDSP_AEB("Analog Extension Board", 0);
static struct snd_kcontrol_new snd_hdsp_adat_sync_check = HDSP_ADAT_SYNC_CHECK;

static int snd_hdsp_create_controls(struct snd_card *card, struct hdsp *hdsp)
{
	unsigned int idx;
	int err;
	struct snd_kcontrol *kctl;

	for (idx = 0; idx < ARRAY_SIZE(snd_hdsp_controls); idx++) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_hdsp_controls[idx], hdsp))) < 0)
			return err;
		if (idx == 1)	/* IEC958 (S/PDIF) Stream */
			hdsp->spdif_ctl = kctl;
	}

	/* ADAT SyncCheck status */
	snd_hdsp_adat_sync_check.name = "ADAT Lock Status";
	snd_hdsp_adat_sync_check.index = 1;
	if ((err = snd_ctl_add (card, kctl = snd_ctl_new1(&snd_hdsp_adat_sync_check, hdsp))))
		return err;
	if (hdsp->io_type == Digiface || hdsp->io_type == H9652) {
		for (idx = 1; idx < 3; ++idx) {
			snd_hdsp_adat_sync_check.index = idx+1;
			if ((err = snd_ctl_add (card, kctl = snd_ctl_new1(&snd_hdsp_adat_sync_check, hdsp))))
				return err;
		}
	}

	/* DA, AD and Phone gain and XLR breakout cable controls for H9632 cards */
	if (hdsp->io_type == H9632) {
		for (idx = 0; idx < ARRAY_SIZE(snd_hdsp_9632_controls); idx++) {
			if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_hdsp_9632_controls[idx], hdsp))) < 0)
				return err;
		}
	}

	/* AEB control for H96xx card */
	if (hdsp->io_type == H9632 || hdsp->io_type == H9652) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_hdsp_96xx_aeb, hdsp))) < 0)
				return err;
	}

	return 0;
}

/*------------------------------------------------------------
   /proc interface
 ------------------------------------------------------------*/

static void
snd_hdsp_proc_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct hdsp *hdsp = (struct hdsp *) entry->private_data;
	unsigned int status;
	unsigned int status2;
	char *pref_sync_ref;
	char *autosync_ref;
	char *system_clock_mode;
	char *clock_source;
	int x;

	status = hdsp_read(hdsp, HDSP_statusRegister);
	status2 = hdsp_read(hdsp, HDSP_status2Register);

	snd_iprintf(buffer, "%s (Card #%d)\n", hdsp->card_name,
		    hdsp->card->number + 1);
	snd_iprintf(buffer, "Buffers: capture %p playback %p\n",
		    hdsp->capture_buffer, hdsp->playback_buffer);
	snd_iprintf(buffer, "IRQ: %d Registers bus: 0x%lx VM: 0x%lx\n",
		    hdsp->irq, hdsp->port, (unsigned long)hdsp->iobase);
	snd_iprintf(buffer, "Control register: 0x%x\n", hdsp->control_register);
	snd_iprintf(buffer, "Control2 register: 0x%x\n",
		    hdsp->control2_register);
	snd_iprintf(buffer, "Status register: 0x%x\n", status);
	snd_iprintf(buffer, "Status2 register: 0x%x\n", status2);

	if (hdsp_check_for_iobox(hdsp)) {
		snd_iprintf(buffer, "No I/O box connected.\n"
			    "Please connect one and upload firmware.\n");
		return;
	}

	if (hdsp_check_for_firmware(hdsp, 0)) {
		if (hdsp->state & HDSP_FirmwareCached) {
			if (snd_hdsp_load_firmware_from_cache(hdsp) != 0) {
				snd_iprintf(buffer, "Firmware loading from "
					    "cache failed, "
					    "please upload manually.\n");
				return;
			}
		} else {
			int err = -EINVAL;
#ifdef HDSP_FW_LOADER
			err = hdsp_request_fw_loader(hdsp);
#endif
			if (err < 0) {
				snd_iprintf(buffer,
					    "No firmware loaded nor cached, "
					    "please upload firmware.\n");
				return;
			}
		}
	}

	snd_iprintf(buffer, "FIFO status: %d\n", hdsp_read(hdsp, HDSP_fifoStatus) & 0xff);
	snd_iprintf(buffer, "MIDI1 Output status: 0x%x\n", hdsp_read(hdsp, HDSP_midiStatusOut0));
	snd_iprintf(buffer, "MIDI1 Input status: 0x%x\n", hdsp_read(hdsp, HDSP_midiStatusIn0));
	snd_iprintf(buffer, "MIDI2 Output status: 0x%x\n", hdsp_read(hdsp, HDSP_midiStatusOut1));
	snd_iprintf(buffer, "MIDI2 Input status: 0x%x\n", hdsp_read(hdsp, HDSP_midiStatusIn1));
	snd_iprintf(buffer, "Use Midi Tasklet: %s\n", hdsp->use_midi_tasklet ? "on" : "off");

	snd_iprintf(buffer, "\n");

	x = 1 << (6 + hdsp_decode_latency(hdsp->control_register & HDSP_LatencyMask));

	snd_iprintf(buffer, "Buffer Size (Latency): %d samples (2 periods of %lu bytes)\n", x, (unsigned long) hdsp->period_bytes);
	snd_iprintf(buffer, "Hardware pointer (frames): %ld\n", hdsp_hw_pointer(hdsp));
	snd_iprintf(buffer, "Precise pointer: %s\n", hdsp->precise_ptr ? "on" : "off");
	snd_iprintf(buffer, "Line out: %s\n", (hdsp->control_register & HDSP_LineOut) ? "on" : "off");

	snd_iprintf(buffer, "Firmware version: %d\n", (status2&HDSP_version0)|(status2&HDSP_version1)<<1|(status2&HDSP_version2)<<2);

	snd_iprintf(buffer, "\n");

	switch (hdsp_clock_source(hdsp)) {
	case HDSP_CLOCK_SOURCE_AUTOSYNC:
		clock_source = "AutoSync";
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_32KHZ:
		clock_source = "Internal 32 kHz";
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_44_1KHZ:
		clock_source = "Internal 44.1 kHz";
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_48KHZ:
		clock_source = "Internal 48 kHz";
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_64KHZ:
		clock_source = "Internal 64 kHz";
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_88_2KHZ:
		clock_source = "Internal 88.2 kHz";
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_96KHZ:
		clock_source = "Internal 96 kHz";
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_128KHZ:
		clock_source = "Internal 128 kHz";
		break;
	case HDSP_CLOCK_SOURCE_INTERNAL_176_4KHZ:
		clock_source = "Internal 176.4 kHz";
		break;
		case HDSP_CLOCK_SOURCE_INTERNAL_192KHZ:
		clock_source = "Internal 192 kHz";
		break;
	default:
		clock_source = "Error";
	}
	snd_iprintf (buffer, "Sample Clock Source: %s\n", clock_source);

	if (hdsp_system_clock_mode(hdsp))
		system_clock_mode = "Slave";
	else
		system_clock_mode = "Master";

	switch (hdsp_pref_sync_ref (hdsp)) {
	case HDSP_SYNC_FROM_WORD:
		pref_sync_ref = "Word Clock";
		break;
	case HDSP_SYNC_FROM_ADAT_SYNC:
		pref_sync_ref = "ADAT Sync";
		break;
	case HDSP_SYNC_FROM_SPDIF:
		pref_sync_ref = "SPDIF";
		break;
	case HDSP_SYNC_FROM_ADAT1:
		pref_sync_ref = "ADAT1";
		break;
	case HDSP_SYNC_FROM_ADAT2:
		pref_sync_ref = "ADAT2";
		break;
	case HDSP_SYNC_FROM_ADAT3:
		pref_sync_ref = "ADAT3";
		break;
	default:
		pref_sync_ref = "Word Clock";
		break;
	}
	snd_iprintf (buffer, "Preferred Sync Reference: %s\n", pref_sync_ref);

	switch (hdsp_autosync_ref (hdsp)) {
	case HDSP_AUTOSYNC_FROM_WORD:
		autosync_ref = "Word Clock";
		break;
	case HDSP_AUTOSYNC_FROM_ADAT_SYNC:
		autosync_ref = "ADAT Sync";
		break;
	case HDSP_AUTOSYNC_FROM_SPDIF:
		autosync_ref = "SPDIF";
		break;
	case HDSP_AUTOSYNC_FROM_NONE:
		autosync_ref = "None";
		break;
	case HDSP_AUTOSYNC_FROM_ADAT1:
		autosync_ref = "ADAT1";
		break;
	case HDSP_AUTOSYNC_FROM_ADAT2:
		autosync_ref = "ADAT2";
		break;
	case HDSP_AUTOSYNC_FROM_ADAT3:
		autosync_ref = "ADAT3";
		break;
	default:
		autosync_ref = "---";
		break;
	}
	snd_iprintf (buffer, "AutoSync Reference: %s\n", autosync_ref);

	snd_iprintf (buffer, "AutoSync Frequency: %d\n", hdsp_external_sample_rate(hdsp));

	snd_iprintf (buffer, "System Clock Mode: %s\n", system_clock_mode);

	snd_iprintf (buffer, "System Clock Frequency: %d\n", hdsp->system_sample_rate);
	snd_iprintf (buffer, "System Clock Locked: %s\n", hdsp->clock_source_locked ? "Yes" : "No");

	snd_iprintf(buffer, "\n");

	switch (hdsp_spdif_in(hdsp)) {
	case HDSP_SPDIFIN_OPTICAL:
		snd_iprintf(buffer, "IEC958 input: Optical\n");
		break;
	case HDSP_SPDIFIN_COAXIAL:
		snd_iprintf(buffer, "IEC958 input: Coaxial\n");
		break;
	case HDSP_SPDIFIN_INTERNAL:
		snd_iprintf(buffer, "IEC958 input: Internal\n");
		break;
	case HDSP_SPDIFIN_AES:
		snd_iprintf(buffer, "IEC958 input: AES\n");
		break;
	default:
		snd_iprintf(buffer, "IEC958 input: ???\n");
		break;
	}

	if (hdsp->control_register & HDSP_SPDIFOpticalOut)
		snd_iprintf(buffer, "IEC958 output: Coaxial & ADAT1\n");
	else
		snd_iprintf(buffer, "IEC958 output: Coaxial only\n");

	if (hdsp->control_register & HDSP_SPDIFProfessional)
		snd_iprintf(buffer, "IEC958 quality: Professional\n");
	else
		snd_iprintf(buffer, "IEC958 quality: Consumer\n");

	if (hdsp->control_register & HDSP_SPDIFEmphasis)
		snd_iprintf(buffer, "IEC958 emphasis: on\n");
	else
		snd_iprintf(buffer, "IEC958 emphasis: off\n");

	if (hdsp->control_register & HDSP_SPDIFNonAudio)
		snd_iprintf(buffer, "IEC958 NonAudio: on\n");
	else
		snd_iprintf(buffer, "IEC958 NonAudio: off\n");
	if ((x = hdsp_spdif_sample_rate (hdsp)) != 0)
		snd_iprintf (buffer, "IEC958 sample rate: %d\n", x);
	else
		snd_iprintf (buffer, "IEC958 sample rate: Error flag set\n");

	snd_iprintf(buffer, "\n");

	/* Sync Check */
	x = status & HDSP_Sync0;
	if (status & HDSP_Lock0)
		snd_iprintf(buffer, "ADAT1: %s\n", x ? "Sync" : "Lock");
	else
		snd_iprintf(buffer, "ADAT1: No Lock\n");

	switch (hdsp->io_type) {
	case Digiface:
	case H9652:
		x = status & HDSP_Sync1;
		if (status & HDSP_Lock1)
			snd_iprintf(buffer, "ADAT2: %s\n", x ? "Sync" : "Lock");
		else
			snd_iprintf(buffer, "ADAT2: No Lock\n");
		x = status & HDSP_Sync2;
		if (status & HDSP_Lock2)
			snd_iprintf(buffer, "ADAT3: %s\n", x ? "Sync" : "Lock");
		else
			snd_iprintf(buffer, "ADAT3: No Lock\n");
		break;
	default:
		/* relax */
		break;
	}

	x = status & HDSP_SPDIFSync;
	if (status & HDSP_SPDIFErrorFlag)
		snd_iprintf (buffer, "SPDIF: No Lock\n");
	else
		snd_iprintf (buffer, "SPDIF: %s\n", x ? "Sync" : "Lock");

	x = status2 & HDSP_wc_sync;
	if (status2 & HDSP_wc_lock)
		snd_iprintf (buffer, "Word Clock: %s\n", x ? "Sync" : "Lock");
	else
		snd_iprintf (buffer, "Word Clock: No Lock\n");

	x = status & HDSP_TimecodeSync;
	if (status & HDSP_TimecodeLock)
		snd_iprintf(buffer, "ADAT Sync: %s\n", x ? "Sync" : "Lock");
	else
		snd_iprintf(buffer, "ADAT Sync: No Lock\n");

	snd_iprintf(buffer, "\n");

	/* Informations about H9632 specific controls */
	if (hdsp->io_type == H9632) {
		char *tmp;

		switch (hdsp_ad_gain(hdsp)) {
		case 0:
			tmp = "-10 dBV";
			break;
		case 1:
			tmp = "+4 dBu";
			break;
		default:
			tmp = "Lo Gain";
			break;
		}
		snd_iprintf(buffer, "AD Gain : %s\n", tmp);

		switch (hdsp_da_gain(hdsp)) {
		case 0:
			tmp = "Hi Gain";
			break;
		case 1:
			tmp = "+4 dBu";
			break;
		default:
			tmp = "-10 dBV";
			break;
		}
		snd_iprintf(buffer, "DA Gain : %s\n", tmp);

		switch (hdsp_phone_gain(hdsp)) {
		case 0:
			tmp = "0 dB";
			break;
		case 1:
			tmp = "-6 dB";
			break;
		default:
			tmp = "-12 dB";
			break;
		}
		snd_iprintf(buffer, "Phones Gain : %s\n", tmp);

		snd_iprintf(buffer, "XLR Breakout Cable : %s\n", hdsp_xlr_breakout_cable(hdsp) ? "yes" : "no");

		if (hdsp->control_register & HDSP_AnalogExtensionBoard)
			snd_iprintf(buffer, "AEB : on (ADAT1 internal)\n");
		else
			snd_iprintf(buffer, "AEB : off (ADAT1 external)\n");
		snd_iprintf(buffer, "\n");
	}

}

static void snd_hdsp_proc_init(struct hdsp *hdsp)
{
	struct snd_info_entry *entry;

	if (! snd_card_proc_new(hdsp->card, "hdsp", &entry))
		snd_info_set_text_ops(entry, hdsp, snd_hdsp_proc_read);
}

static void snd_hdsp_free_buffers(struct hdsp *hdsp)
{
	snd_hammerfall_free_buffer(&hdsp->capture_dma_buf, hdsp->pci);
	snd_hammerfall_free_buffer(&hdsp->playback_dma_buf, hdsp->pci);
}

static int __devinit snd_hdsp_initialize_memory(struct hdsp *hdsp)
{
	unsigned long pb_bus, cb_bus;

	if (snd_hammerfall_get_buffer(hdsp->pci, &hdsp->capture_dma_buf, HDSP_DMA_AREA_BYTES) < 0 ||
	    snd_hammerfall_get_buffer(hdsp->pci, &hdsp->playback_dma_buf, HDSP_DMA_AREA_BYTES) < 0) {
		if (hdsp->capture_dma_buf.area)
			snd_dma_free_pages(&hdsp->capture_dma_buf);
		printk(KERN_ERR "%s: no buffers available\n", hdsp->card_name);
		return -ENOMEM;
	}

	/* Align to bus-space 64K boundary */

	cb_bus = ALIGN(hdsp->capture_dma_buf.addr, 0x10000ul);
	pb_bus = ALIGN(hdsp->playback_dma_buf.addr, 0x10000ul);

	/* Tell the card where it is */

	hdsp_write(hdsp, HDSP_inputBufferAddress, cb_bus);
	hdsp_write(hdsp, HDSP_outputBufferAddress, pb_bus);

	hdsp->capture_buffer = hdsp->capture_dma_buf.area + (cb_bus - hdsp->capture_dma_buf.addr);
	hdsp->playback_buffer = hdsp->playback_dma_buf.area + (pb_bus - hdsp->playback_dma_buf.addr);

	return 0;
}

static int snd_hdsp_set_defaults(struct hdsp *hdsp)
{
	unsigned int i;

	/* ASSUMPTION: hdsp->lock is either held, or
	   there is no need to hold it (e.g. during module
	   initialization).
	 */

	/* set defaults:

	   SPDIF Input via Coax
	   Master clock mode
	   maximum latency (7 => 2^7 = 8192 samples, 64Kbyte buffer,
	                    which implies 2 4096 sample, 32Kbyte periods).
           Enable line out.
	 */

	hdsp->control_register = HDSP_ClockModeMaster |
		                 HDSP_SPDIFInputCoaxial |
		                 hdsp_encode_latency(7) |
		                 HDSP_LineOut;


	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);

#ifdef SNDRV_BIG_ENDIAN
	hdsp->control2_register = HDSP_BIGENDIAN_MODE;
#else
	hdsp->control2_register = 0;
#endif
	if (hdsp->io_type == H9652)
	        snd_hdsp_9652_enable_mixer (hdsp);
	else
		hdsp_write (hdsp, HDSP_control2Reg, hdsp->control2_register);

	hdsp_reset_hw_pointer(hdsp);
	hdsp_compute_period_size(hdsp);

	/* silence everything */

	for (i = 0; i < HDSP_MATRIX_MIXER_SIZE; ++i)
		hdsp->mixer_matrix[i] = MINUS_INFINITY_GAIN;

	for (i = 0; i < ((hdsp->io_type == H9652 || hdsp->io_type == H9632) ? 1352 : HDSP_MATRIX_MIXER_SIZE); ++i) {
		if (hdsp_write_gain (hdsp, i, MINUS_INFINITY_GAIN))
			return -EIO;
	}

	/* H9632 specific defaults */
	if (hdsp->io_type == H9632) {
		hdsp->control_register |= (HDSP_DAGainPlus4dBu | HDSP_ADGainPlus4dBu | HDSP_PhoneGain0dB);
		hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	}

	/* set a default rate so that the channel map is set up.
	 */

	hdsp_set_rate(hdsp, 48000, 1);

	return 0;
}

static void hdsp_midi_tasklet(unsigned long arg)
{
	struct hdsp *hdsp = (struct hdsp *)arg;

	if (hdsp->midi[0].pending)
		snd_hdsp_midi_input_read (&hdsp->midi[0]);
	if (hdsp->midi[1].pending)
		snd_hdsp_midi_input_read (&hdsp->midi[1]);
}

static irqreturn_t snd_hdsp_interrupt(int irq, void *dev_id)
{
	struct hdsp *hdsp = (struct hdsp *) dev_id;
	unsigned int status;
	int audio;
	int midi0;
	int midi1;
	unsigned int midi0status;
	unsigned int midi1status;
	int schedule = 0;

	status = hdsp_read(hdsp, HDSP_statusRegister);

	audio = status & HDSP_audioIRQPending;
	midi0 = status & HDSP_midi0IRQPending;
	midi1 = status & HDSP_midi1IRQPending;

	if (!audio && !midi0 && !midi1)
		return IRQ_NONE;

	hdsp_write(hdsp, HDSP_interruptConfirmation, 0);

	midi0status = hdsp_read (hdsp, HDSP_midiStatusIn0) & 0xff;
	midi1status = hdsp_read (hdsp, HDSP_midiStatusIn1) & 0xff;

	if (!(hdsp->state & HDSP_InitializationComplete))
		return IRQ_HANDLED;

	if (audio) {
		if (hdsp->capture_substream)
			snd_pcm_period_elapsed(hdsp->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream);

		if (hdsp->playback_substream)
			snd_pcm_period_elapsed(hdsp->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream);
	}

	if (midi0 && midi0status) {
		if (hdsp->use_midi_tasklet) {
			/* we disable interrupts for this input until processing is done */
			hdsp->control_register &= ~HDSP_Midi0InterruptEnable;
			hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
			hdsp->midi[0].pending = 1;
			schedule = 1;
		} else {
			snd_hdsp_midi_input_read (&hdsp->midi[0]);
		}
	}
	if (hdsp->io_type != Multiface && hdsp->io_type != H9632 && midi1 && midi1status) {
		if (hdsp->use_midi_tasklet) {
			/* we disable interrupts for this input until processing is done */
			hdsp->control_register &= ~HDSP_Midi1InterruptEnable;
			hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
			hdsp->midi[1].pending = 1;
			schedule = 1;
		} else {
			snd_hdsp_midi_input_read (&hdsp->midi[1]);
		}
	}
	if (hdsp->use_midi_tasklet && schedule)
		tasklet_schedule(&hdsp->midi_tasklet);
	return IRQ_HANDLED;
}

static snd_pcm_uframes_t snd_hdsp_hw_pointer(struct snd_pcm_substream *substream)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	return hdsp_hw_pointer(hdsp);
}

static char *hdsp_channel_buffer_location(struct hdsp *hdsp,
					     int stream,
					     int channel)

{
	int mapped_channel;

        if (snd_BUG_ON(channel < 0 || channel >= hdsp->max_channels))
		return NULL;

	if ((mapped_channel = hdsp->channel_map[channel]) < 0)
		return NULL;

	if (stream == SNDRV_PCM_STREAM_CAPTURE)
		return hdsp->capture_buffer + (mapped_channel * HDSP_CHANNEL_BUFFER_BYTES);
	else
		return hdsp->playback_buffer + (mapped_channel * HDSP_CHANNEL_BUFFER_BYTES);
}

static int snd_hdsp_playback_copy(struct snd_pcm_substream *substream, int channel,
				  snd_pcm_uframes_t pos, void __user *src, snd_pcm_uframes_t count)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	char *channel_buf;

	if (snd_BUG_ON(pos + count > HDSP_CHANNEL_BUFFER_BYTES / 4))
		return -EINVAL;

	channel_buf = hdsp_channel_buffer_location (hdsp, substream->pstr->stream, channel);
	if (snd_BUG_ON(!channel_buf))
		return -EIO;
	if (copy_from_user(channel_buf + pos * 4, src, count * 4))
		return -EFAULT;
	return count;
}

static int snd_hdsp_capture_copy(struct snd_pcm_substream *substream, int channel,
				 snd_pcm_uframes_t pos, void __user *dst, snd_pcm_uframes_t count)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	char *channel_buf;

	if (snd_BUG_ON(pos + count > HDSP_CHANNEL_BUFFER_BYTES / 4))
		return -EINVAL;

	channel_buf = hdsp_channel_buffer_location (hdsp, substream->pstr->stream, channel);
	if (snd_BUG_ON(!channel_buf))
		return -EIO;
	if (copy_to_user(dst, channel_buf + pos * 4, count * 4))
		return -EFAULT;
	return count;
}

static int snd_hdsp_hw_silence(struct snd_pcm_substream *substream, int channel,
				  snd_pcm_uframes_t pos, snd_pcm_uframes_t count)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	char *channel_buf;

	channel_buf = hdsp_channel_buffer_location (hdsp, substream->pstr->stream, channel);
	if (snd_BUG_ON(!channel_buf))
		return -EIO;
	memset(channel_buf + pos * 4, 0, count * 4);
	return count;
}

static int snd_hdsp_reset(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *other;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		other = hdsp->capture_substream;
	else
		other = hdsp->playback_substream;
	if (hdsp->running)
		runtime->status->hw_ptr = hdsp_hw_pointer(hdsp);
	else
		runtime->status->hw_ptr = 0;
	if (other) {
		struct snd_pcm_substream *s;
		struct snd_pcm_runtime *oruntime = other->runtime;
		snd_pcm_group_for_each_entry(s, substream) {
			if (s == other) {
				oruntime->status->hw_ptr = runtime->status->hw_ptr;
				break;
			}
		}
	}
	return 0;
}

static int snd_hdsp_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	int err;
	pid_t this_pid;
	pid_t other_pid;

	if (hdsp_check_for_iobox (hdsp))
		return -EIO;

	if (hdsp_check_for_firmware(hdsp, 1))
		return -EIO;

	spin_lock_irq(&hdsp->lock);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		hdsp->control_register &= ~(HDSP_SPDIFProfessional | HDSP_SPDIFNonAudio | HDSP_SPDIFEmphasis);
		hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register |= hdsp->creg_spdif_stream);
		this_pid = hdsp->playback_pid;
		other_pid = hdsp->capture_pid;
	} else {
		this_pid = hdsp->capture_pid;
		other_pid = hdsp->playback_pid;
	}

	if ((other_pid > 0) && (this_pid != other_pid)) {

		/* The other stream is open, and not by the same
		   task as this one. Make sure that the parameters
		   that matter are the same.
		 */

		if (params_rate(params) != hdsp->system_sample_rate) {
			spin_unlock_irq(&hdsp->lock);
			_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_RATE);
			return -EBUSY;
		}

		if (params_period_size(params) != hdsp->period_bytes / 4) {
			spin_unlock_irq(&hdsp->lock);
			_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
			return -EBUSY;
		}

		/* We're fine. */

		spin_unlock_irq(&hdsp->lock);
 		return 0;

	} else {
		spin_unlock_irq(&hdsp->lock);
	}

	/* how to make sure that the rate matches an externally-set one ?
	 */

	spin_lock_irq(&hdsp->lock);
	if (! hdsp->clock_source_locked) {
		if ((err = hdsp_set_rate(hdsp, params_rate(params), 0)) < 0) {
			spin_unlock_irq(&hdsp->lock);
			_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_RATE);
			return err;
		}
	}
	spin_unlock_irq(&hdsp->lock);

	if ((err = hdsp_set_interrupt_interval(hdsp, params_period_size(params))) < 0) {
		_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
		return err;
	}

	return 0;
}

static int snd_hdsp_channel_info(struct snd_pcm_substream *substream,
				    struct snd_pcm_channel_info *info)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	int mapped_channel;

	if (snd_BUG_ON(info->channel >= hdsp->max_channels))
		return -EINVAL;

	if ((mapped_channel = hdsp->channel_map[info->channel]) < 0)
		return -EINVAL;

	info->offset = mapped_channel * HDSP_CHANNEL_BUFFER_BYTES;
	info->first = 0;
	info->step = 32;
	return 0;
}

static int snd_hdsp_ioctl(struct snd_pcm_substream *substream,
			     unsigned int cmd, void *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL1_RESET:
		return snd_hdsp_reset(substream);
	case SNDRV_PCM_IOCTL1_CHANNEL_INFO:
		return snd_hdsp_channel_info(substream, arg);
	default:
		break;
	}

	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_hdsp_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *other;
	int running;

	if (hdsp_check_for_iobox (hdsp))
		return -EIO;

	if (hdsp_check_for_firmware(hdsp, 0)) /* no auto-loading in trigger */
		return -EIO;

	spin_lock(&hdsp->lock);
	running = hdsp->running;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		running |= 1 << substream->stream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		running &= ~(1 << substream->stream);
		break;
	default:
		snd_BUG();
		spin_unlock(&hdsp->lock);
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		other = hdsp->capture_substream;
	else
		other = hdsp->playback_substream;

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
			if (!(running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) &&
			    substream->stream == SNDRV_PCM_STREAM_CAPTURE)
				hdsp_silence_playback(hdsp);
		} else {
			if (running &&
			    substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				hdsp_silence_playback(hdsp);
		}
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
				hdsp_silence_playback(hdsp);
	}
 _ok:
	snd_pcm_trigger_done(substream, substream);
	if (!hdsp->running && running)
		hdsp_start_audio(hdsp);
	else if (hdsp->running && !running)
		hdsp_stop_audio(hdsp);
	hdsp->running = running;
	spin_unlock(&hdsp->lock);

	return 0;
}

static int snd_hdsp_prepare(struct snd_pcm_substream *substream)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	int result = 0;

	if (hdsp_check_for_iobox (hdsp))
		return -EIO;

	if (hdsp_check_for_firmware(hdsp, 1))
		return -EIO;

	spin_lock_irq(&hdsp->lock);
	if (!hdsp->running)
		hdsp_reset_hw_pointer(hdsp);
	spin_unlock_irq(&hdsp->lock);
	return result;
}

static struct snd_pcm_hardware snd_hdsp_playback_subinfo =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_DOUBLE),
#ifdef SNDRV_BIG_ENDIAN
	.formats =		SNDRV_PCM_FMTBIT_S32_BE,
#else
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
#endif
	.rates =		(SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 |
				 SNDRV_PCM_RATE_64000 |
				 SNDRV_PCM_RATE_88200 |
				 SNDRV_PCM_RATE_96000),
	.rate_min =		32000,
	.rate_max =		96000,
	.channels_min =		14,
	.channels_max =		HDSP_MAX_CHANNELS,
	.buffer_bytes_max =	HDSP_CHANNEL_BUFFER_BYTES * HDSP_MAX_CHANNELS,
	.period_bytes_min =	(64 * 4) * 10,
	.period_bytes_max =	(8192 * 4) * HDSP_MAX_CHANNELS,
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0
};

static struct snd_pcm_hardware snd_hdsp_capture_subinfo =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_SYNC_START),
#ifdef SNDRV_BIG_ENDIAN
	.formats =		SNDRV_PCM_FMTBIT_S32_BE,
#else
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
#endif
	.rates =		(SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 |
				 SNDRV_PCM_RATE_64000 |
				 SNDRV_PCM_RATE_88200 |
				 SNDRV_PCM_RATE_96000),
	.rate_min =		32000,
	.rate_max =		96000,
	.channels_min =		14,
	.channels_max =		HDSP_MAX_CHANNELS,
	.buffer_bytes_max =	HDSP_CHANNEL_BUFFER_BYTES * HDSP_MAX_CHANNELS,
	.period_bytes_min =	(64 * 4) * 10,
	.period_bytes_max =	(8192 * 4) * HDSP_MAX_CHANNELS,
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0
};

static unsigned int hdsp_period_sizes[] = { 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

static struct snd_pcm_hw_constraint_list hdsp_hw_constraints_period_sizes = {
	.count = ARRAY_SIZE(hdsp_period_sizes),
	.list = hdsp_period_sizes,
	.mask = 0
};

static unsigned int hdsp_9632_sample_rates[] = { 32000, 44100, 48000, 64000, 88200, 96000, 128000, 176400, 192000 };

static struct snd_pcm_hw_constraint_list hdsp_hw_constraints_9632_sample_rates = {
	.count = ARRAY_SIZE(hdsp_9632_sample_rates),
	.list = hdsp_9632_sample_rates,
	.mask = 0
};

static int snd_hdsp_hw_rule_in_channels(struct snd_pcm_hw_params *params,
					struct snd_pcm_hw_rule *rule)
{
	struct hdsp *hdsp = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	if (hdsp->io_type == H9632) {
		unsigned int list[3];
		list[0] = hdsp->qs_in_channels;
		list[1] = hdsp->ds_in_channels;
		list[2] = hdsp->ss_in_channels;
		return snd_interval_list(c, 3, list, 0);
	} else {
		unsigned int list[2];
		list[0] = hdsp->ds_in_channels;
		list[1] = hdsp->ss_in_channels;
		return snd_interval_list(c, 2, list, 0);
	}
}

static int snd_hdsp_hw_rule_out_channels(struct snd_pcm_hw_params *params,
					struct snd_pcm_hw_rule *rule)
{
	unsigned int list[3];
	struct hdsp *hdsp = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	if (hdsp->io_type == H9632) {
		list[0] = hdsp->qs_out_channels;
		list[1] = hdsp->ds_out_channels;
		list[2] = hdsp->ss_out_channels;
		return snd_interval_list(c, 3, list, 0);
	} else {
		list[0] = hdsp->ds_out_channels;
		list[1] = hdsp->ss_out_channels;
	}
	return snd_interval_list(c, 2, list, 0);
}

static int snd_hdsp_hw_rule_in_channels_rate(struct snd_pcm_hw_params *params,
					     struct snd_pcm_hw_rule *rule)
{
	struct hdsp *hdsp = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (r->min > 96000 && hdsp->io_type == H9632) {
		struct snd_interval t = {
			.min = hdsp->qs_in_channels,
			.max = hdsp->qs_in_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->min > 48000 && r->max <= 96000) {
		struct snd_interval t = {
			.min = hdsp->ds_in_channels,
			.max = hdsp->ds_in_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->max < 64000) {
		struct snd_interval t = {
			.min = hdsp->ss_in_channels,
			.max = hdsp->ss_in_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	}
	return 0;
}

static int snd_hdsp_hw_rule_out_channels_rate(struct snd_pcm_hw_params *params,
					     struct snd_pcm_hw_rule *rule)
{
	struct hdsp *hdsp = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (r->min > 96000 && hdsp->io_type == H9632) {
		struct snd_interval t = {
			.min = hdsp->qs_out_channels,
			.max = hdsp->qs_out_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->min > 48000 && r->max <= 96000) {
		struct snd_interval t = {
			.min = hdsp->ds_out_channels,
			.max = hdsp->ds_out_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->max < 64000) {
		struct snd_interval t = {
			.min = hdsp->ss_out_channels,
			.max = hdsp->ss_out_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	}
	return 0;
}

static int snd_hdsp_hw_rule_rate_out_channels(struct snd_pcm_hw_params *params,
					     struct snd_pcm_hw_rule *rule)
{
	struct hdsp *hdsp = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (c->min >= hdsp->ss_out_channels) {
		struct snd_interval t = {
			.min = 32000,
			.max = 48000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdsp->qs_out_channels && hdsp->io_type == H9632) {
		struct snd_interval t = {
			.min = 128000,
			.max = 192000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdsp->ds_out_channels) {
		struct snd_interval t = {
			.min = 64000,
			.max = 96000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	}
	return 0;
}

static int snd_hdsp_hw_rule_rate_in_channels(struct snd_pcm_hw_params *params,
					     struct snd_pcm_hw_rule *rule)
{
	struct hdsp *hdsp = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (c->min >= hdsp->ss_in_channels) {
		struct snd_interval t = {
			.min = 32000,
			.max = 48000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdsp->qs_in_channels && hdsp->io_type == H9632) {
		struct snd_interval t = {
			.min = 128000,
			.max = 192000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdsp->ds_in_channels) {
		struct snd_interval t = {
			.min = 64000,
			.max = 96000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	}
	return 0;
}

static int snd_hdsp_playback_open(struct snd_pcm_substream *substream)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (hdsp_check_for_iobox (hdsp))
		return -EIO;

	if (hdsp_check_for_firmware(hdsp, 1))
		return -EIO;

	spin_lock_irq(&hdsp->lock);

	snd_pcm_set_sync(substream);

        runtime->hw = snd_hdsp_playback_subinfo;
	runtime->dma_area = hdsp->playback_buffer;
	runtime->dma_bytes = HDSP_DMA_AREA_BYTES;

	hdsp->playback_pid = current->pid;
	hdsp->playback_substream = substream;

	spin_unlock_irq(&hdsp->lock);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, &hdsp_hw_constraints_period_sizes);
	if (hdsp->clock_source_locked) {
		runtime->hw.rate_min = runtime->hw.rate_max = hdsp->system_sample_rate;
	} else if (hdsp->io_type == H9632) {
		runtime->hw.rate_max = 192000;
		runtime->hw.rates = SNDRV_PCM_RATE_KNOT;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hdsp_hw_constraints_9632_sample_rates);
	}
	if (hdsp->io_type == H9632) {
		runtime->hw.channels_min = hdsp->qs_out_channels;
		runtime->hw.channels_max = hdsp->ss_out_channels;
	}

	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_hdsp_hw_rule_out_channels, hdsp,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_hdsp_hw_rule_out_channels_rate, hdsp,
			     SNDRV_PCM_HW_PARAM_RATE, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			     snd_hdsp_hw_rule_rate_out_channels, hdsp,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);

	hdsp->creg_spdif_stream = hdsp->creg_spdif;
	hdsp->spdif_ctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(hdsp->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &hdsp->spdif_ctl->id);
	return 0;
}

static int snd_hdsp_playback_release(struct snd_pcm_substream *substream)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);

	spin_lock_irq(&hdsp->lock);

	hdsp->playback_pid = -1;
	hdsp->playback_substream = NULL;

	spin_unlock_irq(&hdsp->lock);

	hdsp->spdif_ctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(hdsp->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &hdsp->spdif_ctl->id);
	return 0;
}


static int snd_hdsp_capture_open(struct snd_pcm_substream *substream)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (hdsp_check_for_iobox (hdsp))
		return -EIO;

	if (hdsp_check_for_firmware(hdsp, 1))
		return -EIO;

	spin_lock_irq(&hdsp->lock);

	snd_pcm_set_sync(substream);

	runtime->hw = snd_hdsp_capture_subinfo;
	runtime->dma_area = hdsp->capture_buffer;
	runtime->dma_bytes = HDSP_DMA_AREA_BYTES;

	hdsp->capture_pid = current->pid;
	hdsp->capture_substream = substream;

	spin_unlock_irq(&hdsp->lock);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, &hdsp_hw_constraints_period_sizes);
	if (hdsp->io_type == H9632) {
		runtime->hw.channels_min = hdsp->qs_in_channels;
		runtime->hw.channels_max = hdsp->ss_in_channels;
		runtime->hw.rate_max = 192000;
		runtime->hw.rates = SNDRV_PCM_RATE_KNOT;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hdsp_hw_constraints_9632_sample_rates);
	}
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_hdsp_hw_rule_in_channels, hdsp,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_hdsp_hw_rule_in_channels_rate, hdsp,
			     SNDRV_PCM_HW_PARAM_RATE, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			     snd_hdsp_hw_rule_rate_in_channels, hdsp,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	return 0;
}

static int snd_hdsp_capture_release(struct snd_pcm_substream *substream)
{
	struct hdsp *hdsp = snd_pcm_substream_chip(substream);

	spin_lock_irq(&hdsp->lock);

	hdsp->capture_pid = -1;
	hdsp->capture_substream = NULL;

	spin_unlock_irq(&hdsp->lock);
	return 0;
}

/* helper functions for copying meter values */
static inline int copy_u32_le(void __user *dest, void __iomem *src)
{
	u32 val = readl(src);
	return copy_to_user(dest, &val, 4);
}

static inline int copy_u64_le(void __user *dest, void __iomem *src_low, void __iomem *src_high)
{
	u32 rms_low, rms_high;
	u64 rms;
	rms_low = readl(src_low);
	rms_high = readl(src_high);
	rms = ((u64)rms_high << 32) | rms_low;
	return copy_to_user(dest, &rms, 8);
}

static inline int copy_u48_le(void __user *dest, void __iomem *src_low, void __iomem *src_high)
{
	u32 rms_low, rms_high;
	u64 rms;
	rms_low = readl(src_low) & 0xffffff00;
	rms_high = readl(src_high) & 0xffffff00;
	rms = ((u64)rms_high << 32) | rms_low;
	return copy_to_user(dest, &rms, 8);
}

static int hdsp_9652_get_peak(struct hdsp *hdsp, struct hdsp_peak_rms __user *peak_rms)
{
	int doublespeed = 0;
	int i, j, channels, ofs;

	if (hdsp_read (hdsp, HDSP_statusRegister) & HDSP_DoubleSpeedStatus)
		doublespeed = 1;
	channels = doublespeed ? 14 : 26;
	for (i = 0, j = 0; i < 26; ++i) {
		if (doublespeed && (i & 4))
			continue;
		ofs = HDSP_9652_peakBase - j * 4;
		if (copy_u32_le(&peak_rms->input_peaks[i], hdsp->iobase + ofs))
			return -EFAULT;
		ofs -= channels * 4;
		if (copy_u32_le(&peak_rms->playback_peaks[i], hdsp->iobase + ofs))
			return -EFAULT;
		ofs -= channels * 4;
		if (copy_u32_le(&peak_rms->output_peaks[i], hdsp->iobase + ofs))
			return -EFAULT;
		ofs = HDSP_9652_rmsBase + j * 8;
		if (copy_u48_le(&peak_rms->input_rms[i], hdsp->iobase + ofs,
				hdsp->iobase + ofs + 4))
			return -EFAULT;
		ofs += channels * 8;
		if (copy_u48_le(&peak_rms->playback_rms[i], hdsp->iobase + ofs,
				hdsp->iobase + ofs + 4))
			return -EFAULT;
		ofs += channels * 8;
		if (copy_u48_le(&peak_rms->output_rms[i], hdsp->iobase + ofs,
				hdsp->iobase + ofs + 4))
			return -EFAULT;
		j++;
	}
	return 0;
}

static int hdsp_9632_get_peak(struct hdsp *hdsp, struct hdsp_peak_rms __user *peak_rms)
{
	int i, j;
	struct hdsp_9632_meters __iomem *m;
	int doublespeed = 0;

	if (hdsp_read (hdsp, HDSP_statusRegister) & HDSP_DoubleSpeedStatus)
		doublespeed = 1;
	m = (struct hdsp_9632_meters __iomem *)(hdsp->iobase+HDSP_9632_metersBase);
	for (i = 0, j = 0; i < 16; ++i, ++j) {
		if (copy_u32_le(&peak_rms->input_peaks[i], &m->input_peak[j]))
			return -EFAULT;
		if (copy_u32_le(&peak_rms->playback_peaks[i], &m->playback_peak[j]))
			return -EFAULT;
		if (copy_u32_le(&peak_rms->output_peaks[i], &m->output_peak[j]))
			return -EFAULT;
		if (copy_u64_le(&peak_rms->input_rms[i], &m->input_rms_low[j],
				&m->input_rms_high[j]))
			return -EFAULT;
		if (copy_u64_le(&peak_rms->playback_rms[i], &m->playback_rms_low[j],
				&m->playback_rms_high[j]))
			return -EFAULT;
		if (copy_u64_le(&peak_rms->output_rms[i], &m->output_rms_low[j],
				&m->output_rms_high[j]))
			return -EFAULT;
		if (doublespeed && i == 3) i += 4;
	}
	return 0;
}

static int hdsp_get_peak(struct hdsp *hdsp, struct hdsp_peak_rms __user *peak_rms)
{
	int i;

	for (i = 0; i < 26; i++) {
		if (copy_u32_le(&peak_rms->playback_peaks[i],
				hdsp->iobase + HDSP_playbackPeakLevel + i * 4))
			return -EFAULT;
		if (copy_u32_le(&peak_rms->input_peaks[i],
				hdsp->iobase + HDSP_inputPeakLevel + i * 4))
			return -EFAULT;
	}
	for (i = 0; i < 28; i++) {
		if (copy_u32_le(&peak_rms->output_peaks[i],
				hdsp->iobase + HDSP_outputPeakLevel + i * 4))
			return -EFAULT;
	}
	for (i = 0; i < 26; ++i) {
		if (copy_u64_le(&peak_rms->playback_rms[i],
				hdsp->iobase + HDSP_playbackRmsLevel + i * 8 + 4,
				hdsp->iobase + HDSP_playbackRmsLevel + i * 8))
			return -EFAULT;
		if (copy_u64_le(&peak_rms->input_rms[i],
				hdsp->iobase + HDSP_inputRmsLevel + i * 8 + 4,
				hdsp->iobase + HDSP_inputRmsLevel + i * 8))
			return -EFAULT;
	}
	return 0;
}

static int snd_hdsp_hwdep_ioctl(struct snd_hwdep *hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hdsp *hdsp = (struct hdsp *)hw->private_data;
	void __user *argp = (void __user *)arg;
	int err;

	switch (cmd) {
	case SNDRV_HDSP_IOCTL_GET_PEAK_RMS: {
		struct hdsp_peak_rms __user *peak_rms = (struct hdsp_peak_rms __user *)arg;

		err = hdsp_check_for_iobox(hdsp);
		if (err < 0)
			return err;

		err = hdsp_check_for_firmware(hdsp, 1);
		if (err < 0)
			return err;

		if (!(hdsp->state & HDSP_FirmwareLoaded)) {
			snd_printk(KERN_ERR "Hammerfall-DSP: firmware needs to be uploaded to the card.\n");
			return -EINVAL;
		}

		switch (hdsp->io_type) {
		case H9652:
			return hdsp_9652_get_peak(hdsp, peak_rms);
		case H9632:
			return hdsp_9632_get_peak(hdsp, peak_rms);
		default:
			return hdsp_get_peak(hdsp, peak_rms);
		}
	}
	case SNDRV_HDSP_IOCTL_GET_CONFIG_INFO: {
		struct hdsp_config_info info;
		unsigned long flags;
		int i;

		err = hdsp_check_for_iobox(hdsp);
		if (err < 0)
			return err;

		err = hdsp_check_for_firmware(hdsp, 1);
		if (err < 0)
			return err;

		spin_lock_irqsave(&hdsp->lock, flags);
		info.pref_sync_ref = (unsigned char)hdsp_pref_sync_ref(hdsp);
		info.wordclock_sync_check = (unsigned char)hdsp_wc_sync_check(hdsp);
		if (hdsp->io_type != H9632)
		    info.adatsync_sync_check = (unsigned char)hdsp_adatsync_sync_check(hdsp);
		info.spdif_sync_check = (unsigned char)hdsp_spdif_sync_check(hdsp);
		for (i = 0; i < ((hdsp->io_type != Multiface && hdsp->io_type != H9632) ? 3 : 1); ++i)
			info.adat_sync_check[i] = (unsigned char)hdsp_adat_sync_check(hdsp, i);
		info.spdif_in = (unsigned char)hdsp_spdif_in(hdsp);
		info.spdif_out = (unsigned char)hdsp_spdif_out(hdsp);
		info.spdif_professional = (unsigned char)hdsp_spdif_professional(hdsp);
		info.spdif_emphasis = (unsigned char)hdsp_spdif_emphasis(hdsp);
		info.spdif_nonaudio = (unsigned char)hdsp_spdif_nonaudio(hdsp);
		info.spdif_sample_rate = hdsp_spdif_sample_rate(hdsp);
		info.system_sample_rate = hdsp->system_sample_rate;
		info.autosync_sample_rate = hdsp_external_sample_rate(hdsp);
		info.system_clock_mode = (unsigned char)hdsp_system_clock_mode(hdsp);
		info.clock_source = (unsigned char)hdsp_clock_source(hdsp);
		info.autosync_ref = (unsigned char)hdsp_autosync_ref(hdsp);
		info.line_out = (unsigned char)hdsp_line_out(hdsp);
		if (hdsp->io_type == H9632) {
			info.da_gain = (unsigned char)hdsp_da_gain(hdsp);
			info.ad_gain = (unsigned char)hdsp_ad_gain(hdsp);
			info.phone_gain = (unsigned char)hdsp_phone_gain(hdsp);
			info.xlr_breakout_cable = (unsigned char)hdsp_xlr_breakout_cable(hdsp);

		}
		if (hdsp->io_type == H9632 || hdsp->io_type == H9652)
			info.analog_extension_board = (unsigned char)hdsp_aeb(hdsp);
		spin_unlock_irqrestore(&hdsp->lock, flags);
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		break;
	}
	case SNDRV_HDSP_IOCTL_GET_9632_AEB: {
		struct hdsp_9632_aeb h9632_aeb;

		if (hdsp->io_type != H9632) return -EINVAL;
		h9632_aeb.aebi = hdsp->ss_in_channels - H9632_SS_CHANNELS;
		h9632_aeb.aebo = hdsp->ss_out_channels - H9632_SS_CHANNELS;
		if (copy_to_user(argp, &h9632_aeb, sizeof(h9632_aeb)))
			return -EFAULT;
		break;
	}
	case SNDRV_HDSP_IOCTL_GET_VERSION: {
		struct hdsp_version hdsp_version;
		int err;

		if (hdsp->io_type == H9652 || hdsp->io_type == H9632) return -EINVAL;
		if (hdsp->io_type == Undefined) {
			if ((err = hdsp_get_iobox_version(hdsp)) < 0)
				return err;
		}
		hdsp_version.io_type = hdsp->io_type;
		hdsp_version.firmware_rev = hdsp->firmware_rev;
		if ((err = copy_to_user(argp, &hdsp_version, sizeof(hdsp_version))))
		    	return -EFAULT;
		break;
	}
	case SNDRV_HDSP_IOCTL_UPLOAD_FIRMWARE: {
		struct hdsp_firmware __user *firmware;
		u32 __user *firmware_data;
		int err;

		if (hdsp->io_type == H9652 || hdsp->io_type == H9632) return -EINVAL;
		/* SNDRV_HDSP_IOCTL_GET_VERSION must have been called */
		if (hdsp->io_type == Undefined) return -EINVAL;

		if (hdsp->state & (HDSP_FirmwareCached | HDSP_FirmwareLoaded))
			return -EBUSY;

		snd_printk(KERN_INFO "Hammerfall-DSP: initializing firmware upload\n");
		firmware = (struct hdsp_firmware __user *)argp;

		if (get_user(firmware_data, &firmware->firmware_data))
			return -EFAULT;

		if (hdsp_check_for_iobox (hdsp))
			return -EIO;

		if (copy_from_user(hdsp->firmware_cache, firmware_data, sizeof(hdsp->firmware_cache)) != 0)
			return -EFAULT;

		hdsp->state |= HDSP_FirmwareCached;

		if ((err = snd_hdsp_load_firmware_from_cache(hdsp)) < 0)
			return err;

		if (!(hdsp->state & HDSP_InitializationComplete)) {
			if ((err = snd_hdsp_enable_io(hdsp)) < 0)
				return err;

			snd_hdsp_initialize_channels(hdsp);
			snd_hdsp_initialize_midi_flush(hdsp);

			if ((err = snd_hdsp_create_alsa_devices(hdsp->card, hdsp)) < 0) {
				snd_printk(KERN_ERR "Hammerfall-DSP: error creating alsa devices\n");
				return err;
			}
		}
		break;
	}
	case SNDRV_HDSP_IOCTL_GET_MIXER: {
		struct hdsp_mixer __user *mixer = (struct hdsp_mixer __user *)argp;
		if (copy_to_user(mixer->matrix, hdsp->mixer_matrix, sizeof(unsigned short)*HDSP_MATRIX_MIXER_SIZE))
			return -EFAULT;
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static struct snd_pcm_ops snd_hdsp_playback_ops = {
	.open =		snd_hdsp_playback_open,
	.close =	snd_hdsp_playback_release,
	.ioctl =	snd_hdsp_ioctl,
	.hw_params =	snd_hdsp_hw_params,
	.prepare =	snd_hdsp_prepare,
	.trigger =	snd_hdsp_trigger,
	.pointer =	snd_hdsp_hw_pointer,
	.copy =		snd_hdsp_playback_copy,
	.silence =	snd_hdsp_hw_silence,
};

static struct snd_pcm_ops snd_hdsp_capture_ops = {
	.open =		snd_hdsp_capture_open,
	.close =	snd_hdsp_capture_release,
	.ioctl =	snd_hdsp_ioctl,
	.hw_params =	snd_hdsp_hw_params,
	.prepare =	snd_hdsp_prepare,
	.trigger =	snd_hdsp_trigger,
	.pointer =	snd_hdsp_hw_pointer,
	.copy =		snd_hdsp_capture_copy,
};

static int snd_hdsp_create_hwdep(struct snd_card *card, struct hdsp *hdsp)
{
	struct snd_hwdep *hw;
	int err;

	if ((err = snd_hwdep_new(card, "HDSP hwdep", 0, &hw)) < 0)
		return err;

	hdsp->hwdep = hw;
	hw->private_data = hdsp;
	strcpy(hw->name, "HDSP hwdep interface");

	hw->ops.ioctl = snd_hdsp_hwdep_ioctl;

	return 0;
}

static int snd_hdsp_create_pcm(struct snd_card *card, struct hdsp *hdsp)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(card, hdsp->card_name, 0, 1, 1, &pcm)) < 0)
		return err;

	hdsp->pcm = pcm;
	pcm->private_data = hdsp;
	strcpy(pcm->name, hdsp->card_name);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_hdsp_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_hdsp_capture_ops);

	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;

	return 0;
}

static void snd_hdsp_9652_enable_mixer (struct hdsp *hdsp)
{
        hdsp->control2_register |= HDSP_9652_ENABLE_MIXER;
	hdsp_write (hdsp, HDSP_control2Reg, hdsp->control2_register);
}

static int snd_hdsp_enable_io (struct hdsp *hdsp)
{
	int i;

	if (hdsp_fifo_wait (hdsp, 0, 100)) {
		snd_printk(KERN_ERR "Hammerfall-DSP: enable_io fifo_wait failed\n");
		return -EIO;
	}

	for (i = 0; i < hdsp->max_channels; ++i) {
		hdsp_write (hdsp, HDSP_inputEnable + (4 * i), 1);
		hdsp_write (hdsp, HDSP_outputEnable + (4 * i), 1);
	}

	return 0;
}

static void snd_hdsp_initialize_channels(struct hdsp *hdsp)
{
	int status, aebi_channels, aebo_channels;

	switch (hdsp->io_type) {
	case Digiface:
		hdsp->card_name = "RME Hammerfall DSP + Digiface";
		hdsp->ss_in_channels = hdsp->ss_out_channels = DIGIFACE_SS_CHANNELS;
		hdsp->ds_in_channels = hdsp->ds_out_channels = DIGIFACE_DS_CHANNELS;
		break;

	case H9652:
		hdsp->card_name = "RME Hammerfall HDSP 9652";
		hdsp->ss_in_channels = hdsp->ss_out_channels = H9652_SS_CHANNELS;
		hdsp->ds_in_channels = hdsp->ds_out_channels = H9652_DS_CHANNELS;
		break;

	case H9632:
		status = hdsp_read(hdsp, HDSP_statusRegister);
		/* HDSP_AEBx bits are low when AEB are connected */
		aebi_channels = (status & HDSP_AEBI) ? 0 : 4;
		aebo_channels = (status & HDSP_AEBO) ? 0 : 4;
		hdsp->card_name = "RME Hammerfall HDSP 9632";
		hdsp->ss_in_channels = H9632_SS_CHANNELS+aebi_channels;
		hdsp->ds_in_channels = H9632_DS_CHANNELS+aebi_channels;
		hdsp->qs_in_channels = H9632_QS_CHANNELS+aebi_channels;
		hdsp->ss_out_channels = H9632_SS_CHANNELS+aebo_channels;
		hdsp->ds_out_channels = H9632_DS_CHANNELS+aebo_channels;
		hdsp->qs_out_channels = H9632_QS_CHANNELS+aebo_channels;
		break;

	case Multiface:
		hdsp->card_name = "RME Hammerfall DSP + Multiface";
		hdsp->ss_in_channels = hdsp->ss_out_channels = MULTIFACE_SS_CHANNELS;
		hdsp->ds_in_channels = hdsp->ds_out_channels = MULTIFACE_DS_CHANNELS;
		break;

	default:
 		/* should never get here */
		break;
	}
}

static void snd_hdsp_initialize_midi_flush (struct hdsp *hdsp)
{
	snd_hdsp_flush_midi_input (hdsp, 0);
	snd_hdsp_flush_midi_input (hdsp, 1);
}

static int snd_hdsp_create_alsa_devices(struct snd_card *card, struct hdsp *hdsp)
{
	int err;

	if ((err = snd_hdsp_create_pcm(card, hdsp)) < 0) {
		snd_printk(KERN_ERR "Hammerfall-DSP: Error creating pcm interface\n");
		return err;
	}


	if ((err = snd_hdsp_create_midi(card, hdsp, 0)) < 0) {
		snd_printk(KERN_ERR "Hammerfall-DSP: Error creating first midi interface\n");
		return err;
	}

	if (hdsp->io_type == Digiface || hdsp->io_type == H9652) {
		if ((err = snd_hdsp_create_midi(card, hdsp, 1)) < 0) {
			snd_printk(KERN_ERR "Hammerfall-DSP: Error creating second midi interface\n");
			return err;
		}
	}

	if ((err = snd_hdsp_create_controls(card, hdsp)) < 0) {
		snd_printk(KERN_ERR "Hammerfall-DSP: Error creating ctl interface\n");
		return err;
	}

	snd_hdsp_proc_init(hdsp);

	hdsp->system_sample_rate = -1;
	hdsp->playback_pid = -1;
	hdsp->capture_pid = -1;
	hdsp->capture_substream = NULL;
	hdsp->playback_substream = NULL;

	if ((err = snd_hdsp_set_defaults(hdsp)) < 0) {
		snd_printk(KERN_ERR "Hammerfall-DSP: Error setting default values\n");
		return err;
	}

	if (!(hdsp->state & HDSP_InitializationComplete)) {
		strcpy(card->shortname, "Hammerfall DSP");
		sprintf(card->longname, "%s at 0x%lx, irq %d", hdsp->card_name,
			hdsp->port, hdsp->irq);

		if ((err = snd_card_register(card)) < 0) {
			snd_printk(KERN_ERR "Hammerfall-DSP: error registering card\n");
			return err;
		}
		hdsp->state |= HDSP_InitializationComplete;
	}

	return 0;
}

#ifdef HDSP_FW_LOADER
/* load firmware via hotplug fw loader */
static int hdsp_request_fw_loader(struct hdsp *hdsp)
{
	const char *fwfile;
	const struct firmware *fw;
	int err;

	if (hdsp->io_type == H9652 || hdsp->io_type == H9632)
		return 0;
	if (hdsp->io_type == Undefined) {
		if ((err = hdsp_get_iobox_version(hdsp)) < 0)
			return err;
		if (hdsp->io_type == H9652 || hdsp->io_type == H9632)
			return 0;
	}

	/* caution: max length of firmware filename is 30! */
	switch (hdsp->io_type) {
	case Multiface:
		if (hdsp->firmware_rev == 0xa)
			fwfile = "multiface_firmware.bin";
		else
			fwfile = "multiface_firmware_rev11.bin";
		break;
	case Digiface:
		if (hdsp->firmware_rev == 0xa)
			fwfile = "digiface_firmware.bin";
		else
			fwfile = "digiface_firmware_rev11.bin";
		break;
	default:
		snd_printk(KERN_ERR "Hammerfall-DSP: invalid io_type %d\n", hdsp->io_type);
		return -EINVAL;
	}

	if (request_firmware(&fw, fwfile, &hdsp->pci->dev)) {
		snd_printk(KERN_ERR "Hammerfall-DSP: cannot load firmware %s\n", fwfile);
		return -ENOENT;
	}
	if (fw->size < sizeof(hdsp->firmware_cache)) {
		snd_printk(KERN_ERR "Hammerfall-DSP: too short firmware size %d (expected %d)\n",
			   (int)fw->size, (int)sizeof(hdsp->firmware_cache));
		release_firmware(fw);
		return -EINVAL;
	}

	memcpy(hdsp->firmware_cache, fw->data, sizeof(hdsp->firmware_cache));

	release_firmware(fw);

	hdsp->state |= HDSP_FirmwareCached;

	if ((err = snd_hdsp_load_firmware_from_cache(hdsp)) < 0)
		return err;

	if (!(hdsp->state & HDSP_InitializationComplete)) {
		if ((err = snd_hdsp_enable_io(hdsp)) < 0)
			return err;

		if ((err = snd_hdsp_create_hwdep(hdsp->card, hdsp)) < 0) {
			snd_printk(KERN_ERR "Hammerfall-DSP: error creating hwdep device\n");
			return err;
		}
		snd_hdsp_initialize_channels(hdsp);
		snd_hdsp_initialize_midi_flush(hdsp);
		if ((err = snd_hdsp_create_alsa_devices(hdsp->card, hdsp)) < 0) {
			snd_printk(KERN_ERR "Hammerfall-DSP: error creating alsa devices\n");
			return err;
		}
	}
	return 0;
}
#endif

static int __devinit snd_hdsp_create(struct snd_card *card,
				     struct hdsp *hdsp)
{
	struct pci_dev *pci = hdsp->pci;
	int err;
	int is_9652 = 0;
	int is_9632 = 0;

	hdsp->irq = -1;
	hdsp->state = 0;
	hdsp->midi[0].rmidi = NULL;
	hdsp->midi[1].rmidi = NULL;
	hdsp->midi[0].input = NULL;
	hdsp->midi[1].input = NULL;
	hdsp->midi[0].output = NULL;
	hdsp->midi[1].output = NULL;
	hdsp->midi[0].pending = 0;
	hdsp->midi[1].pending = 0;
	spin_lock_init(&hdsp->midi[0].lock);
	spin_lock_init(&hdsp->midi[1].lock);
	hdsp->iobase = NULL;
	hdsp->control_register = 0;
	hdsp->control2_register = 0;
	hdsp->io_type = Undefined;
	hdsp->max_channels = 26;

	hdsp->card = card;

	spin_lock_init(&hdsp->lock);

	tasklet_init(&hdsp->midi_tasklet, hdsp_midi_tasklet, (unsigned long)hdsp);

	pci_read_config_word(hdsp->pci, PCI_CLASS_REVISION, &hdsp->firmware_rev);
	hdsp->firmware_rev &= 0xff;

	/* From Martin Bjoernsen :
	    "It is important that the card's latency timer register in
	    the PCI configuration space is set to a value much larger
	    than 0 by the computer's BIOS or the driver.
	    The windows driver always sets this 8 bit register [...]
	    to its maximum 255 to avoid problems with some computers."
	*/
	pci_write_config_byte(hdsp->pci, PCI_LATENCY_TIMER, 0xFF);

	strcpy(card->driver, "H-DSP");
	strcpy(card->mixername, "Xilinx FPGA");

	if (hdsp->firmware_rev < 0xa)
		return -ENODEV;
	else if (hdsp->firmware_rev < 0x64)
		hdsp->card_name = "RME Hammerfall DSP";
	else if (hdsp->firmware_rev < 0x96) {
		hdsp->card_name = "RME HDSP 9652";
		is_9652 = 1;
	} else {
		hdsp->card_name = "RME HDSP 9632";
		hdsp->max_channels = 16;
		is_9632 = 1;
	}

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	pci_set_master(hdsp->pci);

	if ((err = pci_request_regions(pci, "hdsp")) < 0)
		return err;
	hdsp->port = pci_resource_start(pci, 0);
	if ((hdsp->iobase = ioremap_nocache(hdsp->port, HDSP_IO_EXTENT)) == NULL) {
		snd_printk(KERN_ERR "Hammerfall-DSP: unable to remap region 0x%lx-0x%lx\n", hdsp->port, hdsp->port + HDSP_IO_EXTENT - 1);
		return -EBUSY;
	}

	if (request_irq(pci->irq, snd_hdsp_interrupt, IRQF_SHARED,
			"hdsp", hdsp)) {
		snd_printk(KERN_ERR "Hammerfall-DSP: unable to use IRQ %d\n", pci->irq);
		return -EBUSY;
	}

	hdsp->irq = pci->irq;
	hdsp->precise_ptr = 0;
	hdsp->use_midi_tasklet = 1;
	hdsp->dds_value = 0;

	if ((err = snd_hdsp_initialize_memory(hdsp)) < 0)
		return err;

	if (!is_9652 && !is_9632) {
		/* we wait a maximum of 10 seconds to let freshly
		 * inserted cardbus cards do their hardware init */
		err = hdsp_wait_for_iobox(hdsp, 1000, 10);

		if (err < 0)
			return err;

		if ((hdsp_read (hdsp, HDSP_statusRegister) & HDSP_DllError) != 0) {
#ifdef HDSP_FW_LOADER
			if ((err = hdsp_request_fw_loader(hdsp)) < 0)
				/* we don't fail as this can happen
				   if userspace is not ready for
				   firmware upload
				*/
				snd_printk(KERN_ERR "Hammerfall-DSP: couldn't get firmware from userspace. try using hdsploader\n");
			else
				/* init is complete, we return */
				return 0;
#endif
			/* we defer initialization */
			snd_printk(KERN_INFO "Hammerfall-DSP: card initialization pending : waiting for firmware\n");
			if ((err = snd_hdsp_create_hwdep(card, hdsp)) < 0)
				return err;
			return 0;
		} else {
			snd_printk(KERN_INFO "Hammerfall-DSP: Firmware already present, initializing card.\n");
			if (hdsp_read(hdsp, HDSP_status2Register) & HDSP_version1)
				hdsp->io_type = Multiface;
			else
				hdsp->io_type = Digiface;
		}
	}

	if ((err = snd_hdsp_enable_io(hdsp)) != 0)
		return err;

	if (is_9652)
	        hdsp->io_type = H9652;

	if (is_9632)
		hdsp->io_type = H9632;

	if ((err = snd_hdsp_create_hwdep(card, hdsp)) < 0)
		return err;

	snd_hdsp_initialize_channels(hdsp);
	snd_hdsp_initialize_midi_flush(hdsp);

	hdsp->state |= HDSP_FirmwareLoaded;

	if ((err = snd_hdsp_create_alsa_devices(card, hdsp)) < 0)
		return err;

	return 0;
}

static int snd_hdsp_free(struct hdsp *hdsp)
{
	if (hdsp->port) {
		/* stop the audio, and cancel all interrupts */
		tasklet_kill(&hdsp->midi_tasklet);
		hdsp->control_register &= ~(HDSP_Start|HDSP_AudioInterruptEnable|HDSP_Midi0InterruptEnable|HDSP_Midi1InterruptEnable);
		hdsp_write (hdsp, HDSP_controlRegister, hdsp->control_register);
	}

	if (hdsp->irq >= 0)
		free_irq(hdsp->irq, (void *)hdsp);

	snd_hdsp_free_buffers(hdsp);

	if (hdsp->iobase)
		iounmap(hdsp->iobase);

	if (hdsp->port)
		pci_release_regions(hdsp->pci);

	pci_disable_device(hdsp->pci);
	return 0;
}

static void snd_hdsp_card_free(struct snd_card *card)
{
	struct hdsp *hdsp = (struct hdsp *) card->private_data;

	if (hdsp)
		snd_hdsp_free(hdsp);
}

static int __devinit snd_hdsp_probe(struct pci_dev *pci,
				    const struct pci_device_id *pci_id)
{
	static int dev;
	struct hdsp *hdsp;
	struct snd_card *card;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_create(index[dev], id[dev], THIS_MODULE,
			      sizeof(struct hdsp), &card);
	if (err < 0)
		return err;

	hdsp = (struct hdsp *) card->private_data;
	card->private_free = snd_hdsp_card_free;
	hdsp->dev = dev;
	hdsp->pci = pci;
	snd_card_set_dev(card, &pci->dev);

	if ((err = snd_hdsp_create(card, hdsp)) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->shortname, "Hammerfall DSP");
	sprintf(card->longname, "%s at 0x%lx, irq %d", hdsp->card_name,
		hdsp->port, hdsp->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_hdsp_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name =     "RME Hammerfall DSP",
	.id_table = snd_hdsp_ids,
	.probe =    snd_hdsp_probe,
	.remove = __devexit_p(snd_hdsp_remove),
};

static int __init alsa_card_hdsp_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_hdsp_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_hdsp_init)
module_exit(alsa_card_hdsp_exit)
