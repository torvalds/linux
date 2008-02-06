#ifndef _AC97_CODEC_H_
#define _AC97_CODEC_H_

#include <linux/types.h>
#include <linux/soundcard.h>

/* AC97 1.0 */
#define  AC97_RESET               0x0000      //
#define  AC97_MASTER_VOL_STEREO   0x0002      // Line Out
#define  AC97_HEADPHONE_VOL       0x0004      // 
#define  AC97_MASTER_VOL_MONO     0x0006      // TAD Output
#define  AC97_MASTER_TONE         0x0008      //
#define  AC97_PCBEEP_VOL          0x000a      // none
#define  AC97_PHONE_VOL           0x000c      // TAD Input (mono)
#define  AC97_MIC_VOL             0x000e      // MIC Input (mono)
#define  AC97_LINEIN_VOL          0x0010      // Line Input (stereo)
#define  AC97_CD_VOL              0x0012      // CD Input (stereo)
#define  AC97_VIDEO_VOL           0x0014      // none
#define  AC97_AUX_VOL             0x0016      // Aux Input (stereo)
#define  AC97_PCMOUT_VOL          0x0018      // Wave Output (stereo)
#define  AC97_RECORD_SELECT       0x001a      //
#define  AC97_RECORD_GAIN         0x001c
#define  AC97_RECORD_GAIN_MIC     0x001e
#define  AC97_GENERAL_PURPOSE     0x0020
#define  AC97_3D_CONTROL          0x0022
#define  AC97_MODEM_RATE          0x0024
#define  AC97_POWER_CONTROL       0x0026

/* AC'97 2.0 */
#define AC97_EXTENDED_ID          0x0028       /* Extended Audio ID */
#define AC97_EXTENDED_STATUS      0x002A       /* Extended Audio Status */
#define AC97_PCM_FRONT_DAC_RATE   0x002C       /* PCM Front DAC Rate */
#define AC97_PCM_SURR_DAC_RATE    0x002E       /* PCM Surround DAC Rate */
#define AC97_PCM_LFE_DAC_RATE     0x0030       /* PCM LFE DAC Rate */
#define AC97_PCM_LR_ADC_RATE      0x0032       /* PCM LR ADC Rate */
#define AC97_PCM_MIC_ADC_RATE     0x0034       /* PCM MIC ADC Rate */
#define AC97_CENTER_LFE_MASTER    0x0036       /* Center + LFE Master Volume */
#define AC97_SURROUND_MASTER      0x0038       /* Surround (Rear) Master Volume */
#define AC97_RESERVED_3A          0x003A       /* Reserved in AC '97 < 2.2 */

/* AC'97 2.2 */
#define AC97_SPDIF_CONTROL        0x003A       /* S/PDIF Control */

/* range 0x3c-0x58 - MODEM */
#define AC97_EXTENDED_MODEM_ID    0x003C
#define AC97_EXTEND_MODEM_STAT    0x003E
#define AC97_LINE1_RATE           0x0040
#define AC97_LINE2_RATE           0x0042
#define AC97_HANDSET_RATE         0x0044
#define AC97_LINE1_LEVEL          0x0046
#define AC97_LINE2_LEVEL          0x0048
#define AC97_HANDSET_LEVEL        0x004A
#define AC97_GPIO_CONFIG          0x004C
#define AC97_GPIO_POLARITY        0x004E
#define AC97_GPIO_STICKY          0x0050
#define AC97_GPIO_WAKE_UP         0x0052
#define AC97_GPIO_STATUS          0x0054
#define AC97_MISC_MODEM_STAT      0x0056
#define AC97_RESERVED_58          0x0058

/* registers 0x005a - 0x007a are vendor reserved */

#define AC97_VENDOR_ID1           0x007c
#define AC97_VENDOR_ID2           0x007e

/* volume control bit defines */
#define AC97_MUTE                 0x8000
#define AC97_MICBOOST             0x0040
#define AC97_LEFTVOL              0x3f00
#define AC97_RIGHTVOL             0x003f

/* record mux defines */
#define AC97_RECMUX_MIC           0x0000
#define AC97_RECMUX_CD            0x0101
#define AC97_RECMUX_VIDEO         0x0202
#define AC97_RECMUX_AUX           0x0303
#define AC97_RECMUX_LINE          0x0404
#define AC97_RECMUX_STEREO_MIX    0x0505
#define AC97_RECMUX_MONO_MIX      0x0606
#define AC97_RECMUX_PHONE         0x0707

/* general purpose register bit defines */
#define AC97_GP_LPBK              0x0080       /* Loopback mode */
#define AC97_GP_MS                0x0100       /* Mic Select 0=Mic1, 1=Mic2 */
#define AC97_GP_MIX               0x0200       /* Mono output select 0=Mix, 1=Mic */
#define AC97_GP_RLBK              0x0400       /* Remote Loopback - Modem line codec */
#define AC97_GP_LLBK              0x0800       /* Local Loopback - Modem Line codec */
#define AC97_GP_LD                0x1000       /* Loudness 1=on */
#define AC97_GP_3D                0x2000       /* 3D Enhancement 1=on */
#define AC97_GP_ST                0x4000       /* Stereo Enhancement 1=on */
#define AC97_GP_POP               0x8000       /* Pcm Out Path, 0=pre 3D, 1=post 3D */

/* extended audio status and control bit defines */
#define AC97_EA_VRA               0x0001       /* Variable bit rate enable bit */
#define AC97_EA_DRA               0x0002       /* Double-rate audio enable bit */
#define AC97_EA_SPDIF             0x0004       /* S/PDIF Enable bit */
#define AC97_EA_VRM               0x0008       /* Variable bit rate for MIC enable bit */
#define AC97_EA_CDAC              0x0040       /* PCM Center DAC is ready (Read only) */
#define AC97_EA_SDAC              0x0040       /* PCM Surround DACs are ready (Read only) */
#define AC97_EA_LDAC              0x0080       /* PCM LFE DAC is ready (Read only) */
#define AC97_EA_MDAC              0x0100       /* MIC ADC is ready (Read only) */
#define AC97_EA_SPCV              0x0400       /* S/PDIF configuration valid (Read only) */
#define AC97_EA_PRI               0x0800       /* Turns the PCM Center DAC off */
#define AC97_EA_PRJ               0x1000       /* Turns the PCM Surround DACs off */
#define AC97_EA_PRK               0x2000       /* Turns the PCM LFE DAC off */
#define AC97_EA_PRL               0x4000       /* Turns the MIC ADC off */
#define AC97_EA_SLOT_MASK         0xffcf       /* Mask for slot assignment bits */
#define AC97_EA_SPSA_3_4          0x0000       /* Slot assigned to 3 & 4 */
#define AC97_EA_SPSA_7_8          0x0010       /* Slot assigned to 7 & 8 */
#define AC97_EA_SPSA_6_9          0x0020       /* Slot assigned to 6 & 9 */
#define AC97_EA_SPSA_10_11        0x0030       /* Slot assigned to 10 & 11 */

/* S/PDIF control bit defines */
#define AC97_SC_PRO               0x0001       /* Professional status */
#define AC97_SC_NAUDIO            0x0002       /* Non audio stream */
#define AC97_SC_COPY              0x0004       /* Copyright status */
#define AC97_SC_PRE               0x0008       /* Preemphasis status */
#define AC97_SC_CC_MASK           0x07f0       /* Category Code mask */
#define AC97_SC_L                 0x0800       /* Generation Level status */
#define AC97_SC_SPSR_MASK         0xcfff       /* S/PDIF Sample Rate bits */
#define AC97_SC_SPSR_44K          0x0000       /* Use 44.1kHz Sample rate */
#define AC97_SC_SPSR_48K          0x2000       /* Use 48kHz Sample rate */
#define AC97_SC_SPSR_32K          0x3000       /* Use 32kHz Sample rate */
#define AC97_SC_DRS               0x4000       /* Double Rate S/PDIF */
#define AC97_SC_V                 0x8000       /* Validity status */

/* powerdown control and status bit defines */

/* status */
#define AC97_PWR_MDM              0x0010       /* Modem section ready */
#define AC97_PWR_REF              0x0008       /* Vref nominal */
#define AC97_PWR_ANL              0x0004       /* Analog section ready */
#define AC97_PWR_DAC              0x0002       /* DAC section ready */
#define AC97_PWR_ADC              0x0001       /* ADC section ready */

/* control */
#define AC97_PWR_PR0              0x0100       /* ADC and Mux powerdown */
#define AC97_PWR_PR1              0x0200       /* DAC powerdown */
#define AC97_PWR_PR2              0x0400       /* Output mixer powerdown (Vref on) */
#define AC97_PWR_PR3              0x0800       /* Output mixer powerdown (Vref off) */
#define AC97_PWR_PR4              0x1000       /* AC-link powerdown */
#define AC97_PWR_PR5              0x2000       /* Internal Clk disable */
#define AC97_PWR_PR6              0x4000       /* HP amp powerdown */
#define AC97_PWR_PR7              0x8000       /* Modem off - if supported */

/* extended audio ID register bit defines */
#define AC97_EXTID_VRA            0x0001
#define AC97_EXTID_DRA            0x0002
#define AC97_EXTID_SPDIF          0x0004
#define AC97_EXTID_VRM            0x0008
#define AC97_EXTID_DSA0           0x0010
#define AC97_EXTID_DSA1           0x0020
#define AC97_EXTID_CDAC           0x0040
#define AC97_EXTID_SDAC           0x0080
#define AC97_EXTID_LDAC           0x0100
#define AC97_EXTID_AMAP           0x0200
#define AC97_EXTID_REV0           0x0400
#define AC97_EXTID_REV1           0x0800
#define AC97_EXTID_ID0            0x4000
#define AC97_EXTID_ID1            0x8000

/* extended status register bit defines */
#define AC97_EXTSTAT_VRA          0x0001
#define AC97_EXTSTAT_DRA          0x0002
#define AC97_EXTSTAT_SPDIF        0x0004
#define AC97_EXTSTAT_VRM          0x0008
#define AC97_EXTSTAT_SPSA0        0x0010
#define AC97_EXTSTAT_SPSA1        0x0020
#define AC97_EXTSTAT_CDAC         0x0040
#define AC97_EXTSTAT_SDAC         0x0080
#define AC97_EXTSTAT_LDAC         0x0100
#define AC97_EXTSTAT_MADC         0x0200
#define AC97_EXTSTAT_SPCV         0x0400
#define AC97_EXTSTAT_PRI          0x0800
#define AC97_EXTSTAT_PRJ          0x1000
#define AC97_EXTSTAT_PRK          0x2000
#define AC97_EXTSTAT_PRL          0x4000

/* extended audio ID register bit defines */
#define AC97_EXTID_VRA            0x0001
#define AC97_EXTID_DRA            0x0002
#define AC97_EXTID_SPDIF          0x0004
#define AC97_EXTID_VRM            0x0008
#define AC97_EXTID_DSA0           0x0010
#define AC97_EXTID_DSA1           0x0020
#define AC97_EXTID_CDAC           0x0040
#define AC97_EXTID_SDAC           0x0080
#define AC97_EXTID_LDAC           0x0100
#define AC97_EXTID_AMAP           0x0200
#define AC97_EXTID_REV0           0x0400
#define AC97_EXTID_REV1           0x0800
#define AC97_EXTID_ID0            0x4000
#define AC97_EXTID_ID1            0x8000

/* extended status register bit defines */
#define AC97_EXTSTAT_VRA          0x0001
#define AC97_EXTSTAT_DRA          0x0002
#define AC97_EXTSTAT_SPDIF        0x0004
#define AC97_EXTSTAT_VRM          0x0008
#define AC97_EXTSTAT_SPSA0        0x0010
#define AC97_EXTSTAT_SPSA1        0x0020
#define AC97_EXTSTAT_CDAC         0x0040
#define AC97_EXTSTAT_SDAC         0x0080
#define AC97_EXTSTAT_LDAC         0x0100
#define AC97_EXTSTAT_MADC         0x0200
#define AC97_EXTSTAT_SPCV         0x0400
#define AC97_EXTSTAT_PRI          0x0800
#define AC97_EXTSTAT_PRJ          0x1000
#define AC97_EXTSTAT_PRK          0x2000
#define AC97_EXTSTAT_PRL          0x4000

/* useful power states */
#define AC97_PWR_D0               0x0000      /* everything on */
#define AC97_PWR_D1              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR4
#define AC97_PWR_D2              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR2|AC97_PWR_PR3|AC97_PWR_PR4
#define AC97_PWR_D3              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR2|AC97_PWR_PR3|AC97_PWR_PR4
#define AC97_PWR_ANLOFF          AC97_PWR_PR2|AC97_PWR_PR3  /* analog section off */

/* Total number of defined registers.  */
#define AC97_REG_CNT 64


/* OSS interface to the ac97s.. */
#define AC97_STEREO_MASK (SOUND_MASK_VOLUME|SOUND_MASK_PCM|\
	SOUND_MASK_LINE|SOUND_MASK_CD|\
	SOUND_MASK_ALTPCM|SOUND_MASK_IGAIN|\
	SOUND_MASK_LINE1|SOUND_MASK_VIDEO)

#define AC97_SUPPORTED_MASK (AC97_STEREO_MASK | \
	SOUND_MASK_BASS|SOUND_MASK_TREBLE|\
	SOUND_MASK_SPEAKER|SOUND_MASK_MIC|\
	SOUND_MASK_PHONEIN|SOUND_MASK_PHONEOUT)

#define AC97_RECORD_MASK (SOUND_MASK_MIC|\
	SOUND_MASK_CD|SOUND_MASK_IGAIN|SOUND_MASK_VIDEO|\
	SOUND_MASK_LINE1| SOUND_MASK_LINE|\
	SOUND_MASK_PHONEIN)

/* original check is not good enough in case FOO is greater than
 * SOUND_MIXER_NRDEVICES because the supported_mixers has exactly
 * SOUND_MIXER_NRDEVICES elements.
 * before matching the given mixer against the bitmask in supported_mixers we
 * check if mixer number exceeds maximum allowed size which is as mentioned
 * above SOUND_MIXER_NRDEVICES */
#define supported_mixer(CODEC,FOO) ((FOO >= 0) && \
                                    (FOO < SOUND_MIXER_NRDEVICES) && \
                                    (CODEC)->supported_mixers & (1<<FOO) )

struct ac97_codec {
	/* Linked list of codecs */
	struct list_head list;

	/* AC97 controller connected with */
	void *private_data;

	char *name;
	int id;
	int dev_mixer; 
	int type;
	u32 model;

	unsigned int modem:1;

	struct ac97_ops *codec_ops;

	/* controller specific lower leverl ac97 accessing routines.
	   must be re-entrant safe */
	u16  (*codec_read)  (struct ac97_codec *codec, u8 reg);
	void (*codec_write) (struct ac97_codec *codec, u8 reg, u16 val);

	/* Wait for codec-ready.  Ok to sleep here.  */
	void  (*codec_wait)  (struct ac97_codec *codec);

	/* callback used by helper drivers for interesting ac97 setups */
	void  (*codec_unregister) (struct ac97_codec *codec);
	
	struct ac97_driver *driver;
	void *driver_private;	/* Private data for the driver */
	
	spinlock_t lock;
	
	/* OSS mixer masks */
	int modcnt;
	int supported_mixers;
	int stereo_mixers;
	int record_sources;

	/* Property flags */
	int flags;

	int bit_resolution;

	/* OSS mixer interface */
	int  (*read_mixer) (struct ac97_codec *codec, int oss_channel);
	void (*write_mixer)(struct ac97_codec *codec, int oss_channel,
			    unsigned int left, unsigned int right);
	int  (*recmask_io) (struct ac97_codec *codec, int rw, int mask);
	int  (*mixer_ioctl)(struct ac97_codec *codec, unsigned int cmd, unsigned long arg);

	/* saved OSS mixer states */
	unsigned int mixer_state[SOUND_MIXER_NRDEVICES];

	/* Software Modem interface */
	int  (*modem_ioctl)(struct ac97_codec *codec, unsigned int cmd, unsigned long arg);
};

/*
 *	Operation structures for each known AC97 chip
 */
 
struct ac97_ops
{
	/* Initialise */
	int (*init)(struct ac97_codec *c);
	/* Amplifier control */
	int (*amplifier)(struct ac97_codec *codec, int on);
	/* Digital mode control */
	int (*digital)(struct ac97_codec *codec, int slots, int rate, int mode);
#define AUDIO_DIGITAL		0x8000
#define AUDIO_PRO		0x4000
#define AUDIO_DRS		0x2000
#define AUDIO_CCMASK		0x003F
	
#define AC97_DELUDED_MODEM	1	/* Audio codec reports its a modem */
#define AC97_NO_PCM_VOLUME	2	/* Volume control is missing 	   */
#define AC97_DEFAULT_POWER_OFF 4 /* Needs warm reset to power up */
};

extern int ac97_probe_codec(struct ac97_codec *);

extern struct ac97_codec *ac97_alloc_codec(void);
extern void ac97_release_codec(struct ac97_codec *codec);

struct ac97_driver {
	struct list_head list;
	char *name;
	u32 codec_id;
	u32 codec_mask;
	int (*probe) (struct ac97_codec *codec, struct ac97_driver *driver);
	void (*remove) (struct ac97_codec *codec, struct ac97_driver *driver);
};

/* quirk types */
enum {
	AC97_TUNE_DEFAULT = -1, /* use default from quirk list (not valid in list) */
	AC97_TUNE_NONE = 0,     /* nothing extra to do */
	AC97_TUNE_HP_ONLY,      /* headphone (true line-out) control as master only */
	AC97_TUNE_SWAP_HP,      /* swap headphone and master controls */
	AC97_TUNE_SWAP_SURROUND, /* swap master and surround controls */
	AC97_TUNE_AD_SHARING,   /* for AD1985, turn on OMS bit and use headphone */
	AC97_TUNE_ALC_JACK,     /* for Realtek, enable JACK detection */
};

struct ac97_quirk {
	unsigned short vendor;  /* PCI vendor id */
	unsigned short device;  /* PCI device id */
	unsigned short mask;    /* device id bit mask, 0 = accept all */
	const char *name;       /* name shown as info */
	int type;               /* quirk type above */
};

#endif /* _AC97_CODEC_H_ */
