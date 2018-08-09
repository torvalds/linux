/* SPDX-License-Identifier: GPL-2.0+
 *
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.1
 *  by Intel Corporation (http://developer.intel.com).
 */

#ifndef __SOUND_AC97_CODEC_H
#define __SOUND_AC97_CODEC_H

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <sound/ac97/regs.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/info.h>

/* maximum number of devices on the AC97 bus */
#define	AC97_BUS_MAX_DEVICES	4

/* specific - SigmaTel */
#define AC97_SIGMATEL_OUTSEL	0x64	/* Output Select, STAC9758 */
#define AC97_SIGMATEL_INSEL	0x66	/* Input Select, STAC9758 */
#define AC97_SIGMATEL_IOMISC	0x68	/* STAC9758 */
#define AC97_SIGMATEL_ANALOG	0x6c	/* Analog Special */
#define AC97_SIGMATEL_DAC2INVERT 0x6e
#define AC97_SIGMATEL_BIAS1	0x70
#define AC97_SIGMATEL_BIAS2	0x72
#define AC97_SIGMATEL_VARIOUS	0x72	/* STAC9758 */
#define AC97_SIGMATEL_MULTICHN	0x74	/* Multi-Channel programming */
#define AC97_SIGMATEL_CIC1	0x76
#define AC97_SIGMATEL_CIC2	0x78

/* specific - Analog Devices */
#define AC97_AD_TEST		0x5a	/* test register */
#define AC97_AD_TEST2		0x5c	/* undocumented test register 2 */
#define AC97_AD_HPFD_SHIFT	12	/* High Pass Filter Disable */
#define AC97_AD_CODEC_CFG	0x70	/* codec configuration */
#define AC97_AD_JACK_SPDIF	0x72	/* Jack Sense & S/PDIF */
#define AC97_AD_SERIAL_CFG	0x74	/* Serial Configuration */
#define AC97_AD_MISC		0x76	/* Misc Control Bits */
#define AC97_AD_VREFD_SHIFT	2	/* V_REFOUT Disable (AD1888) */

/* specific - Cirrus Logic */
#define AC97_CSR_ACMODE		0x5e	/* AC Mode Register */
#define AC97_CSR_MISC_CRYSTAL	0x60	/* Misc Crystal Control */
#define AC97_CSR_SPDIF		0x68	/* S/PDIF Register */
#define AC97_CSR_SERIAL		0x6a	/* Serial Port Control */
#define AC97_CSR_SPECF_ADDR	0x6c	/* Special Feature Address */
#define AC97_CSR_SPECF_DATA	0x6e	/* Special Feature Data */
#define AC97_CSR_BDI_STATUS	0x7a	/* BDI Status */

/* specific - Conexant */
#define AC97_CXR_AUDIO_MISC	0x5c
#define AC97_CXR_SPDIFEN	(1<<3)
#define AC97_CXR_COPYRGT	(1<<2)
#define AC97_CXR_SPDIF_MASK	(3<<0)
#define AC97_CXR_SPDIF_PCM	0x0
#define AC97_CXR_SPDIF_AC3	0x2

/* specific - ALC */
#define AC97_ALC650_SPDIF_INPUT_STATUS1	0x60
/* S/PDIF input status 1 bit defines */
#define AC97_ALC650_PRO             0x0001  /* Professional status */
#define AC97_ALC650_NAUDIO          0x0002  /* Non audio stream */
#define AC97_ALC650_COPY            0x0004  /* Copyright status */
#define AC97_ALC650_PRE             0x0038  /* Preemphasis status */
#define AC97_ALC650_PRE_SHIFT       3
#define AC97_ALC650_MODE            0x00C0  /* Preemphasis status */
#define AC97_ALC650_MODE_SHIFT      6
#define AC97_ALC650_CC_MASK         0x7f00  /* Category Code mask */
#define AC97_ALC650_CC_SHIFT        8
#define AC97_ALC650_L               0x8000  /* Generation Level status */

#define AC97_ALC650_SPDIF_INPUT_STATUS2	0x62
/* S/PDIF input status 2 bit defines */
#define AC97_ALC650_SOUCE_MASK      0x000f  /* Source number */
#define AC97_ALC650_CHANNEL_MASK    0x00f0  /* Channel number */
#define AC97_ALC650_CHANNEL_SHIFT   4 
#define AC97_ALC650_SPSR_MASK       0x0f00  /* S/PDIF Sample Rate bits */
#define AC97_ALC650_SPSR_SHIFT      8
#define AC97_ALC650_SPSR_44K        0x0000  /* Use 44.1kHz Sample rate */
#define AC97_ALC650_SPSR_48K        0x0200  /* Use 48kHz Sample rate */
#define AC97_ALC650_SPSR_32K        0x0300  /* Use 32kHz Sample rate */
#define AC97_ALC650_CLOCK_ACCURACY  0x3000  /* Clock accuracy */
#define AC97_ALC650_CLOCK_SHIFT     12
#define AC97_ALC650_CLOCK_LOCK      0x4000  /* Clock locked status */
#define AC97_ALC650_V               0x8000  /* Validity status */

#define AC97_ALC650_SURR_DAC_VOL	0x64
#define AC97_ALC650_LFE_DAC_VOL		0x66
#define AC97_ALC650_UNKNOWN1		0x68
#define AC97_ALC650_MULTICH		0x6a
#define AC97_ALC650_UNKNOWN2		0x6c
#define AC97_ALC650_REVISION		0x6e
#define AC97_ALC650_UNKNOWN3		0x70
#define AC97_ALC650_UNKNOWN4		0x72
#define AC97_ALC650_MISC		0x74
#define AC97_ALC650_GPIO_SETUP		0x76
#define AC97_ALC650_GPIO_STATUS		0x78
#define AC97_ALC650_CLOCK		0x7a

/* specific - Yamaha YMF7x3 */
#define AC97_YMF7X3_DIT_CTRL	0x66	/* DIT Control (YMF743) / 2 (YMF753) */
#define AC97_YMF7X3_3D_MODE_SEL	0x68	/* 3D Mode Select */

/* specific - C-Media */
#define AC97_CM9738_VENDOR_CTRL	0x5a
#define AC97_CM9739_MULTI_CHAN	0x64
#define AC97_CM9739_SPDIF_IN_STATUS	0x68 /* 32bit */
#define AC97_CM9739_SPDIF_CTRL	0x6c

/* specific - wolfson */
#define AC97_WM97XX_FMIXER_VOL  0x72
#define AC97_WM9704_RMIXER_VOL  0x74
#define AC97_WM9704_TEST        0x5a
#define AC97_WM9704_RPCM_VOL    0x70
#define AC97_WM9711_OUT3VOL     0x16


/* ac97->scaps */
#define AC97_SCAP_AUDIO		(1<<0)	/* audio codec 97 */
#define AC97_SCAP_MODEM		(1<<1)	/* modem codec 97 */
#define AC97_SCAP_SURROUND_DAC	(1<<2)	/* surround L&R DACs are present */
#define AC97_SCAP_CENTER_LFE_DAC (1<<3)	/* center and LFE DACs are present */
#define AC97_SCAP_SKIP_AUDIO	(1<<4)	/* skip audio part of codec */
#define AC97_SCAP_SKIP_MODEM	(1<<5)	/* skip modem part of codec */
#define AC97_SCAP_INDEP_SDIN	(1<<6)	/* independent SDIN */
#define AC97_SCAP_INV_EAPD	(1<<7)	/* inverted EAPD */
#define AC97_SCAP_DETECT_BY_VENDOR (1<<8) /* use vendor registers for read tests */
#define AC97_SCAP_NO_SPDIF	(1<<9)	/* don't build SPDIF controls */
#define AC97_SCAP_EAPD_LED	(1<<10)	/* EAPD as mute LED */
#define AC97_SCAP_POWER_SAVE	(1<<11)	/* capable for aggressive power-saving */

/* ac97->flags */
#define AC97_HAS_PC_BEEP	(1<<0)	/* force PC Speaker usage */
#define AC97_AD_MULTI		(1<<1)	/* Analog Devices - multi codecs */
#define AC97_CS_SPDIF		(1<<2)	/* Cirrus Logic uses funky SPDIF */
#define AC97_CX_SPDIF		(1<<3)	/* Conexant's spdif interface */
#define AC97_STEREO_MUTES	(1<<4)	/* has stereo mute bits */
#define AC97_DOUBLE_RATE	(1<<5)	/* supports double rate playback */
#define AC97_HAS_NO_MASTER_VOL	(1<<6)	/* no Master volume */
#define AC97_HAS_NO_PCM_VOL	(1<<7)	/* no PCM volume */
#define AC97_DEFAULT_POWER_OFF	(1<<8)	/* no RESET write */
#define AC97_MODEM_PATCH	(1<<9)	/* modem patch */
#define AC97_HAS_NO_REC_GAIN	(1<<10) /* no Record gain */
#define AC97_HAS_NO_PHONE	(1<<11) /* no PHONE volume */
#define AC97_HAS_NO_PC_BEEP	(1<<12) /* no PC Beep volume */
#define AC97_HAS_NO_VIDEO	(1<<13) /* no Video volume */
#define AC97_HAS_NO_CD		(1<<14) /* no CD volume */
#define AC97_HAS_NO_MIC	(1<<15) /* no MIC volume */
#define AC97_HAS_NO_TONE	(1<<16) /* no Tone volume */
#define AC97_HAS_NO_STD_PCM	(1<<17)	/* no standard AC97 PCM volume and mute */
#define AC97_HAS_NO_AUX		(1<<18) /* no standard AC97 AUX volume and mute */
#define AC97_HAS_8CH		(1<<19) /* supports 8-channel output */

/* rates indexes */
#define AC97_RATES_FRONT_DAC	0
#define AC97_RATES_SURR_DAC	1
#define AC97_RATES_LFE_DAC	2
#define AC97_RATES_ADC		3
#define AC97_RATES_MIC_ADC	4
#define AC97_RATES_SPDIF	5

#define AC97_NUM_GPIOS		16
/*
 *
 */

struct snd_ac97;
struct snd_ac97_gpio_priv;
struct snd_pcm_chmap;

struct snd_ac97_build_ops {
	int (*build_3d) (struct snd_ac97 *ac97);
	int (*build_specific) (struct snd_ac97 *ac97);
	int (*build_spdif) (struct snd_ac97 *ac97);
	int (*build_post_spdif) (struct snd_ac97 *ac97);
#ifdef CONFIG_PM
	void (*suspend) (struct snd_ac97 *ac97);
	void (*resume) (struct snd_ac97 *ac97);
#endif
	void (*update_jacks) (struct snd_ac97 *ac97);	/* for jack-sharing */
};

struct snd_ac97_bus_ops {
	void (*reset) (struct snd_ac97 *ac97);
	void (*warm_reset)(struct snd_ac97 *ac97);
	void (*write) (struct snd_ac97 *ac97, unsigned short reg, unsigned short val);
	unsigned short (*read) (struct snd_ac97 *ac97, unsigned short reg);
	void (*wait) (struct snd_ac97 *ac97);
	void (*init) (struct snd_ac97 *ac97);
};

struct snd_ac97_bus {
	/* -- lowlevel (hardware) driver specific -- */
	struct snd_ac97_bus_ops *ops;
	void *private_data;
	void (*private_free) (struct snd_ac97_bus *bus);
	/* --- */
	struct snd_card *card;
	unsigned short num;	/* bus number */
	unsigned short no_vra: 1, /* bridge doesn't support VRA */
		       dra: 1,	/* bridge supports double rate */
		       isdin: 1;/* independent SDIN */
	unsigned int clock;	/* AC'97 base clock (usually 48000Hz) */
	spinlock_t bus_lock;	/* used mainly for slot allocation */
	unsigned short used_slots[2][4]; /* actually used PCM slots */
	unsigned short pcms_count; /* count of PCMs */
	struct ac97_pcm *pcms;
	struct snd_ac97 *codec[4];
	struct snd_info_entry *proc;
};

/* static resolution table */
struct snd_ac97_res_table {
	unsigned short reg;	/* register */
	unsigned short bits;	/* resolution bitmask */
};

struct snd_ac97_template {
	void *private_data;
	void (*private_free) (struct snd_ac97 *ac97);
	struct pci_dev *pci;	/* assigned PCI device - used for quirks */
	unsigned short num;	/* number of codec: 0 = primary, 1 = secondary */
	unsigned short addr;	/* physical address of codec [0-3] */
	unsigned int scaps;	/* driver capabilities */
	const struct snd_ac97_res_table *res_table;	/* static resolution */
};

struct snd_ac97 {
	/* -- lowlevel (hardware) driver specific -- */
	const struct snd_ac97_build_ops *build_ops;
	void *private_data;
	void (*private_free) (struct snd_ac97 *ac97);
	/* --- */
	struct snd_ac97_bus *bus;
	struct pci_dev *pci;	/* assigned PCI device - used for quirks */
	struct snd_info_entry *proc;
	struct snd_info_entry *proc_regs;
	unsigned short subsystem_vendor;
	unsigned short subsystem_device;
	struct mutex reg_mutex;
	struct mutex page_mutex;	/* mutex for AD18xx multi-codecs and paging (2.3) */
	unsigned short num;	/* number of codec: 0 = primary, 1 = secondary */
	unsigned short addr;	/* physical address of codec [0-3] */
	unsigned int id;	/* identification of codec */
	unsigned short caps;	/* capabilities (register 0) */
	unsigned short ext_id;	/* extended feature identification (register 28) */
	unsigned short ext_mid;	/* extended modem ID (register 3C) */
	const struct snd_ac97_res_table *res_table;	/* static resolution */
	unsigned int scaps;	/* driver capabilities */
	unsigned int flags;	/* specific code */
	unsigned int rates[6];	/* see AC97_RATES_* defines */
	unsigned int spdif_status;
	unsigned short regs[0x80]; /* register cache */
	DECLARE_BITMAP(reg_accessed, 0x80); /* bit flags */
	union {			/* vendor specific code */
		struct {
			unsigned short unchained[3];	// 0 = C34, 1 = C79, 2 = C69
			unsigned short chained[3];	// 0 = C34, 1 = C79, 2 = C69
			unsigned short id[3];		// codec IDs (lower 16-bit word)
			unsigned short pcmreg[3];	// PCM registers
			unsigned short codec_cfg[3];	// CODEC_CFG bits
			unsigned char swap_mic_linein;	// AD1986/AD1986A only
			unsigned char lo_as_master;	/* LO as master */
		} ad18xx;
		unsigned int dev_flags;		/* device specific */
	} spec;
	/* jack-sharing info */
	unsigned char indep_surround;
	unsigned char channel_mode;

#ifdef CONFIG_SND_AC97_POWER_SAVE
	unsigned int power_up;	/* power states */
	struct delayed_work power_work;
#endif
	struct device dev;
	struct snd_ac97_gpio_priv *gpio_priv;

	struct snd_pcm_chmap *chmaps[2]; /* channel-maps (optional) */
};

#define to_ac97_t(d) container_of(d, struct snd_ac97, dev)

/* conditions */
static inline int ac97_is_audio(struct snd_ac97 * ac97)
{
	return (ac97->scaps & AC97_SCAP_AUDIO);
}
static inline int ac97_is_modem(struct snd_ac97 * ac97)
{
	return (ac97->scaps & AC97_SCAP_MODEM);
}
static inline int ac97_is_rev22(struct snd_ac97 * ac97)
{
	return (ac97->ext_id & AC97_EI_REV_MASK) >= AC97_EI_REV_22;
}
static inline int ac97_can_amap(struct snd_ac97 * ac97)
{
	return (ac97->ext_id & AC97_EI_AMAP) != 0;
}
static inline int ac97_can_spdif(struct snd_ac97 * ac97)
{
	return (ac97->ext_id & AC97_EI_SPDIF) != 0;
}

/* functions */
/* create new AC97 bus */
int snd_ac97_bus(struct snd_card *card, int num, struct snd_ac97_bus_ops *ops,
		 void *private_data, struct snd_ac97_bus **rbus);
/* create mixer controls */
int snd_ac97_mixer(struct snd_ac97_bus *bus, struct snd_ac97_template *template,
		   struct snd_ac97 **rac97);
const char *snd_ac97_get_short_name(struct snd_ac97 *ac97);

void snd_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short value);
unsigned short snd_ac97_read(struct snd_ac97 *ac97, unsigned short reg);
void snd_ac97_write_cache(struct snd_ac97 *ac97, unsigned short reg, unsigned short value);
int snd_ac97_update(struct snd_ac97 *ac97, unsigned short reg, unsigned short value);
int snd_ac97_update_bits(struct snd_ac97 *ac97, unsigned short reg, unsigned short mask, unsigned short value);
#ifdef CONFIG_SND_AC97_POWER_SAVE
int snd_ac97_update_power(struct snd_ac97 *ac97, int reg, int powerup);
#else
static inline int snd_ac97_update_power(struct snd_ac97 *ac97, int reg,
					int powerup)
{
	return 0;
}
#endif
#ifdef CONFIG_PM
void snd_ac97_suspend(struct snd_ac97 *ac97);
void snd_ac97_resume(struct snd_ac97 *ac97);
#endif
int snd_ac97_reset(struct snd_ac97 *ac97, bool try_warm, unsigned int id,
	unsigned int id_mask);

/* quirk types */
enum {
	AC97_TUNE_DEFAULT = -1,	/* use default from quirk list (not valid in list) */
	AC97_TUNE_NONE = 0,	/* nothing extra to do */
	AC97_TUNE_HP_ONLY,	/* headphone (true line-out) control as master only */
	AC97_TUNE_SWAP_HP,	/* swap headphone and master controls */
	AC97_TUNE_SWAP_SURROUND, /* swap master and surround controls */
	AC97_TUNE_AD_SHARING,	/* for AD1985, turn on OMS bit and use headphone */
	AC97_TUNE_ALC_JACK,	/* for Realtek, enable JACK detection */
	AC97_TUNE_INV_EAPD,	/* inverted EAPD implementation */
	AC97_TUNE_MUTE_LED,	/* EAPD bit works as mute LED */
	AC97_TUNE_HP_MUTE_LED,  /* EAPD bit works as mute LED, use headphone control as master */
};

struct ac97_quirk {
	unsigned short subvendor; /* PCI subsystem vendor id */
	unsigned short subdevice; /* PCI subsystem device id */
	unsigned short mask;	/* device id bit mask, 0 = accept all */
	unsigned int codec_id;	/* codec id (if any), 0 = accept all */
	const char *name;	/* name shown as info */
	int type;		/* quirk type above */
};

int snd_ac97_tune_hardware(struct snd_ac97 *ac97,
			   const struct ac97_quirk *quirk,
			   const char *override);
int snd_ac97_set_rate(struct snd_ac97 *ac97, int reg, unsigned int rate);

/*
 * PCM allocation
 */

enum ac97_pcm_cfg {
	AC97_PCM_CFG_FRONT = 2,
	AC97_PCM_CFG_REAR = 10,		/* alias surround */
	AC97_PCM_CFG_LFE = 11,		/* center + lfe */
	AC97_PCM_CFG_40 = 4,		/* front + rear */
	AC97_PCM_CFG_51 = 6,		/* front + rear + center/lfe */
	AC97_PCM_CFG_SPDIF = 20
};

struct ac97_pcm {
	struct snd_ac97_bus *bus;
	unsigned int stream: 1,	   	   /* stream type: 1 = capture */
		     exclusive: 1,	   /* exclusive mode, don't override with other pcms */
		     copy_flag: 1,	   /* lowlevel driver must fill all entries */
		     spdif: 1;		   /* spdif pcm */
	unsigned short aslots;		   /* active slots */
	unsigned short cur_dbl;		   /* current double-rate state */
	unsigned int rates;		   /* available rates */
	struct {
		unsigned short slots;	   /* driver input: requested AC97 slot numbers */
		unsigned short rslots[4];  /* allocated slots per codecs */
		unsigned char rate_table[4];
		struct snd_ac97 *codec[4];	   /* allocated codecs */
	} r[2];				   /* 0 = standard rates, 1 = double rates */
	unsigned long private_value;	   /* used by the hardware driver */
};

int snd_ac97_pcm_assign(struct snd_ac97_bus *ac97,
			unsigned short pcms_count,
			const struct ac97_pcm *pcms);
int snd_ac97_pcm_open(struct ac97_pcm *pcm, unsigned int rate,
		      enum ac97_pcm_cfg cfg, unsigned short slots);
int snd_ac97_pcm_close(struct ac97_pcm *pcm);
int snd_ac97_pcm_double_rate_rules(struct snd_pcm_runtime *runtime);

/* ad hoc AC97 device driver access */
extern struct bus_type ac97_bus_type;

/* AC97 platform_data adding function */
static inline void snd_ac97_dev_add_pdata(struct snd_ac97 *ac97, void *data)
{
	ac97->dev.platform_data = data;
}

#endif /* __SOUND_AC97_CODEC_H */
