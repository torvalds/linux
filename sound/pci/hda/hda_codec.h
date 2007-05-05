/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __SOUND_HDA_CODEC_H
#define __SOUND_HDA_CODEC_H

#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>

/*
 * nodes
 */
#define	AC_NODE_ROOT		0x00

/*
 * function group types
 */
enum {
	AC_GRP_AUDIO_FUNCTION = 0x01,
	AC_GRP_MODEM_FUNCTION = 0x02,
};
	
/*
 * widget types
 */
enum {
	AC_WID_AUD_OUT,		/* Audio Out */
	AC_WID_AUD_IN,		/* Audio In */
	AC_WID_AUD_MIX,		/* Audio Mixer */
	AC_WID_AUD_SEL,		/* Audio Selector */
	AC_WID_PIN,		/* Pin Complex */
	AC_WID_POWER,		/* Power */
	AC_WID_VOL_KNB,		/* Volume Knob */
	AC_WID_BEEP,		/* Beep Generator */
	AC_WID_VENDOR = 0x0f	/* Vendor specific */
};

/*
 * GET verbs
 */
#define AC_VERB_GET_STREAM_FORMAT		0x0a00
#define AC_VERB_GET_AMP_GAIN_MUTE		0x0b00
#define AC_VERB_GET_PROC_COEF			0x0c00
#define AC_VERB_GET_COEF_INDEX			0x0d00
#define AC_VERB_PARAMETERS			0x0f00
#define AC_VERB_GET_CONNECT_SEL			0x0f01
#define AC_VERB_GET_CONNECT_LIST		0x0f02
#define AC_VERB_GET_PROC_STATE			0x0f03
#define AC_VERB_GET_SDI_SELECT			0x0f04
#define AC_VERB_GET_POWER_STATE			0x0f05
#define AC_VERB_GET_CONV			0x0f06
#define AC_VERB_GET_PIN_WIDGET_CONTROL		0x0f07
#define AC_VERB_GET_UNSOLICITED_RESPONSE	0x0f08
#define AC_VERB_GET_PIN_SENSE			0x0f09
#define AC_VERB_GET_BEEP_CONTROL		0x0f0a
#define AC_VERB_GET_EAPD_BTLENABLE		0x0f0c
#define AC_VERB_GET_DIGI_CONVERT		0x0f0d
#define AC_VERB_GET_VOLUME_KNOB_CONTROL		0x0f0f
/* f10-f1a: GPIO */
#define AC_VERB_GET_GPIO_DATA			0x0f15
#define AC_VERB_GET_GPIO_MASK			0x0f16
#define AC_VERB_GET_GPIO_DIRECTION		0x0f17
#define AC_VERB_GET_CONFIG_DEFAULT		0x0f1c
/* f20: AFG/MFG */
#define AC_VERB_GET_SUBSYSTEM_ID		0x0f20

/*
 * SET verbs
 */
#define AC_VERB_SET_STREAM_FORMAT		0x200
#define AC_VERB_SET_AMP_GAIN_MUTE		0x300
#define AC_VERB_SET_PROC_COEF			0x400
#define AC_VERB_SET_COEF_INDEX			0x500
#define AC_VERB_SET_CONNECT_SEL			0x701
#define AC_VERB_SET_PROC_STATE			0x703
#define AC_VERB_SET_SDI_SELECT			0x704
#define AC_VERB_SET_POWER_STATE			0x705
#define AC_VERB_SET_CHANNEL_STREAMID		0x706
#define AC_VERB_SET_PIN_WIDGET_CONTROL		0x707
#define AC_VERB_SET_UNSOLICITED_ENABLE		0x708
#define AC_VERB_SET_PIN_SENSE			0x709
#define AC_VERB_SET_BEEP_CONTROL		0x70a
#define AC_VERB_SET_EAPD_BTLENABLE		0x70c
#define AC_VERB_SET_DIGI_CONVERT_1		0x70d
#define AC_VERB_SET_DIGI_CONVERT_2		0x70e
#define AC_VERB_SET_VOLUME_KNOB_CONTROL		0x70f
#define AC_VERB_SET_GPIO_DATA			0x715
#define AC_VERB_SET_GPIO_MASK			0x716
#define AC_VERB_SET_GPIO_DIRECTION		0x717
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_0	0x71c
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_1	0x71d
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_2	0x71e
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_3	0x71f
#define AC_VERB_SET_CODEC_RESET			0x7ff

/*
 * Parameter IDs
 */
#define AC_PAR_VENDOR_ID		0x00
#define AC_PAR_SUBSYSTEM_ID		0x01
#define AC_PAR_REV_ID			0x02
#define AC_PAR_NODE_COUNT		0x04
#define AC_PAR_FUNCTION_TYPE		0x05
#define AC_PAR_AUDIO_FG_CAP		0x08
#define AC_PAR_AUDIO_WIDGET_CAP		0x09
#define AC_PAR_PCM			0x0a
#define AC_PAR_STREAM			0x0b
#define AC_PAR_PIN_CAP			0x0c
#define AC_PAR_AMP_IN_CAP		0x0d
#define AC_PAR_CONNLIST_LEN		0x0e
#define AC_PAR_POWER_STATE		0x0f
#define AC_PAR_PROC_CAP			0x10
#define AC_PAR_GPIO_CAP			0x11
#define AC_PAR_AMP_OUT_CAP		0x12

/*
 * AC_VERB_PARAMETERS results (32bit)
 */

/* Function Group Type */
#define AC_FGT_TYPE			(0xff<<0)
#define AC_FGT_TYPE_SHIFT		0
#define AC_FGT_UNSOL_CAP		(1<<8)

/* Audio Function Group Capabilities */
#define AC_AFG_OUT_DELAY		(0xf<<0)
#define AC_AFG_IN_DELAY			(0xf<<8)
#define AC_AFG_BEEP_GEN			(1<<16)

/* Audio Widget Capabilities */
#define AC_WCAP_STEREO			(1<<0)	/* stereo I/O */
#define AC_WCAP_IN_AMP			(1<<1)	/* AMP-in present */
#define AC_WCAP_OUT_AMP			(1<<2)	/* AMP-out present */
#define AC_WCAP_AMP_OVRD		(1<<3)	/* AMP-parameter override */
#define AC_WCAP_FORMAT_OVRD		(1<<4)	/* format override */
#define AC_WCAP_STRIPE			(1<<5)	/* stripe */
#define AC_WCAP_PROC_WID		(1<<6)	/* Proc Widget */
#define AC_WCAP_UNSOL_CAP		(1<<7)	/* Unsol capable */
#define AC_WCAP_CONN_LIST		(1<<8)	/* connection list */
#define AC_WCAP_DIGITAL			(1<<9)	/* digital I/O */
#define AC_WCAP_POWER			(1<<10)	/* power control */
#define AC_WCAP_LR_SWAP			(1<<11)	/* L/R swap */
#define AC_WCAP_DELAY			(0xf<<16)
#define AC_WCAP_DELAY_SHIFT		16
#define AC_WCAP_TYPE			(0xf<<20)
#define AC_WCAP_TYPE_SHIFT		20

/* supported PCM rates and bits */
#define AC_SUPPCM_RATES			(0xfff << 0)
#define AC_SUPPCM_BITS_8		(1<<16)
#define AC_SUPPCM_BITS_16		(1<<17)
#define AC_SUPPCM_BITS_20		(1<<18)
#define AC_SUPPCM_BITS_24		(1<<19)
#define AC_SUPPCM_BITS_32		(1<<20)

/* supported PCM stream format */
#define AC_SUPFMT_PCM			(1<<0)
#define AC_SUPFMT_FLOAT32		(1<<1)
#define AC_SUPFMT_AC3			(1<<2)

/* Pin widget capabilies */
#define AC_PINCAP_IMP_SENSE		(1<<0)	/* impedance sense capable */
#define AC_PINCAP_TRIG_REQ		(1<<1)	/* trigger required */
#define AC_PINCAP_PRES_DETECT		(1<<2)	/* presence detect capable */
#define AC_PINCAP_HP_DRV		(1<<3)	/* headphone drive capable */
#define AC_PINCAP_OUT			(1<<4)	/* output capable */
#define AC_PINCAP_IN			(1<<5)	/* input capable */
#define AC_PINCAP_BALANCE		(1<<6)	/* balanced I/O capable */
#define AC_PINCAP_VREF			(0x37<<8)
#define AC_PINCAP_VREF_SHIFT		8
#define AC_PINCAP_EAPD			(1<<16)	/* EAPD capable */
/* Vref status (used in pin cap) */
#define AC_PINCAP_VREF_HIZ		(1<<0)	/* Hi-Z */
#define AC_PINCAP_VREF_50		(1<<1)	/* 50% */
#define AC_PINCAP_VREF_GRD		(1<<2)	/* ground */
#define AC_PINCAP_VREF_80		(1<<4)	/* 80% */
#define AC_PINCAP_VREF_100		(1<<5)	/* 100% */

/* Amplifier capabilities */
#define AC_AMPCAP_OFFSET		(0x7f<<0)  /* 0dB offset */
#define AC_AMPCAP_OFFSET_SHIFT		0
#define AC_AMPCAP_NUM_STEPS		(0x7f<<8)  /* number of steps */
#define AC_AMPCAP_NUM_STEPS_SHIFT	8
#define AC_AMPCAP_STEP_SIZE		(0x7f<<16) /* step size 0-32dB in 0.25dB */
#define AC_AMPCAP_STEP_SIZE_SHIFT	16
#define AC_AMPCAP_MUTE			(1<<31)    /* mute capable */
#define AC_AMPCAP_MUTE_SHIFT		31

/* Connection list */
#define AC_CLIST_LENGTH			(0x7f<<0)
#define AC_CLIST_LONG			(1<<7)

/* Supported power status */
#define AC_PWRST_D0SUP			(1<<0)
#define AC_PWRST_D1SUP			(1<<1)
#define AC_PWRST_D2SUP			(1<<2)
#define AC_PWRST_D3SUP			(1<<3)

/* Power state values */
#define AC_PWRST_D0			0x00
#define AC_PWRST_D1			0x01
#define AC_PWRST_D2			0x02
#define AC_PWRST_D3			0x03

/* Processing capabilies */
#define AC_PCAP_BENIGN			(1<<0)
#define AC_PCAP_NUM_COEF		(0xff<<8)

/* Volume knobs capabilities */
#define AC_KNBCAP_NUM_STEPS		(0x7f<<0)
#define AC_KNBCAP_DELTA			(1<<8)

/*
 * Control Parameters
 */

/* Amp gain/mute */
#define AC_AMP_MUTE			(1<<7)
#define AC_AMP_GAIN			(0x7f)
#define AC_AMP_GET_INDEX		(0xf<<0)

#define AC_AMP_GET_LEFT			(1<<13)
#define AC_AMP_GET_RIGHT		(0<<13)
#define AC_AMP_GET_OUTPUT		(1<<15)
#define AC_AMP_GET_INPUT		(0<<15)

#define AC_AMP_SET_INDEX		(0xf<<8)
#define AC_AMP_SET_INDEX_SHIFT		8
#define AC_AMP_SET_RIGHT		(1<<12)
#define AC_AMP_SET_LEFT			(1<<13)
#define AC_AMP_SET_INPUT		(1<<14)
#define AC_AMP_SET_OUTPUT		(1<<15)

/* DIGITAL1 bits */
#define AC_DIG1_ENABLE			(1<<0)
#define AC_DIG1_V			(1<<1)
#define AC_DIG1_VCFG			(1<<2)
#define AC_DIG1_EMPHASIS		(1<<3)
#define AC_DIG1_COPYRIGHT		(1<<4)
#define AC_DIG1_NONAUDIO		(1<<5)
#define AC_DIG1_PROFESSIONAL		(1<<6)
#define AC_DIG1_LEVEL			(1<<7)

/* Pin widget control - 8bit */
#define AC_PINCTL_VREFEN		(0x7<<0)
#define AC_PINCTL_VREF_HIZ		0	/* Hi-Z */
#define AC_PINCTL_VREF_50		1	/* 50% */
#define AC_PINCTL_VREF_GRD		2	/* ground */
#define AC_PINCTL_VREF_80		4	/* 80% */
#define AC_PINCTL_VREF_100		5	/* 100% */
#define AC_PINCTL_IN_EN			(1<<5)
#define AC_PINCTL_OUT_EN		(1<<6)
#define AC_PINCTL_HP_EN			(1<<7)

/* Unsolicited response - 8bit */
#define AC_USRSP_EN			(1<<7)

/* configuration default - 32bit */
#define AC_DEFCFG_SEQUENCE		(0xf<<0)
#define AC_DEFCFG_DEF_ASSOC		(0xf<<4)
#define AC_DEFCFG_ASSOC_SHIFT		4
#define AC_DEFCFG_MISC			(0xf<<8)
#define AC_DEFCFG_MISC_SHIFT		8
#define AC_DEFCFG_COLOR			(0xf<<12)
#define AC_DEFCFG_COLOR_SHIFT		12
#define AC_DEFCFG_CONN_TYPE		(0xf<<16)
#define AC_DEFCFG_CONN_TYPE_SHIFT	16
#define AC_DEFCFG_DEVICE		(0xf<<20)
#define AC_DEFCFG_DEVICE_SHIFT		20
#define AC_DEFCFG_LOCATION		(0x3f<<24)
#define AC_DEFCFG_LOCATION_SHIFT	24
#define AC_DEFCFG_PORT_CONN		(0x3<<30)
#define AC_DEFCFG_PORT_CONN_SHIFT	30

/* device device types (0x0-0xf) */
enum {
	AC_JACK_LINE_OUT,
	AC_JACK_SPEAKER,
	AC_JACK_HP_OUT,
	AC_JACK_CD,
	AC_JACK_SPDIF_OUT,
	AC_JACK_DIG_OTHER_OUT,
	AC_JACK_MODEM_LINE_SIDE,
	AC_JACK_MODEM_HAND_SIDE,
	AC_JACK_LINE_IN,
	AC_JACK_AUX,
	AC_JACK_MIC_IN,
	AC_JACK_TELEPHONY,
	AC_JACK_SPDIF_IN,
	AC_JACK_DIG_OTHER_IN,
	AC_JACK_OTHER = 0xf,
};

/* jack connection types (0x0-0xf) */
enum {
	AC_JACK_CONN_UNKNOWN,
	AC_JACK_CONN_1_8,
	AC_JACK_CONN_1_4,
	AC_JACK_CONN_ATAPI,
	AC_JACK_CONN_RCA,
	AC_JACK_CONN_OPTICAL,
	AC_JACK_CONN_OTHER_DIGITAL,
	AC_JACK_CONN_OTHER_ANALOG,
	AC_JACK_CONN_DIN,
	AC_JACK_CONN_XLR,
	AC_JACK_CONN_RJ11,
	AC_JACK_CONN_COMB,
	AC_JACK_CONN_OTHER = 0xf,
};

/* jack colors (0x0-0xf) */
enum {
	AC_JACK_COLOR_UNKNOWN,
	AC_JACK_COLOR_BLACK,
	AC_JACK_COLOR_GREY,
	AC_JACK_COLOR_BLUE,
	AC_JACK_COLOR_GREEN,
	AC_JACK_COLOR_RED,
	AC_JACK_COLOR_ORANGE,
	AC_JACK_COLOR_YELLOW,
	AC_JACK_COLOR_PURPLE,
	AC_JACK_COLOR_PINK,
	AC_JACK_COLOR_WHITE = 0xe,
	AC_JACK_COLOR_OTHER,
};

/* Jack location (0x0-0x3f) */
/* common case */
enum {
	AC_JACK_LOC_NONE,
	AC_JACK_LOC_REAR,
	AC_JACK_LOC_FRONT,
	AC_JACK_LOC_LEFT,
	AC_JACK_LOC_RIGHT,
	AC_JACK_LOC_TOP,
	AC_JACK_LOC_BOTTOM,
};
/* bits 4-5 */
enum {
	AC_JACK_LOC_EXTERNAL = 0x00,
	AC_JACK_LOC_INTERNAL = 0x10,
	AC_JACK_LOC_SEPARATE = 0x20,
	AC_JACK_LOC_OTHER    = 0x30,
};
enum {
	/* external on primary chasis */
	AC_JACK_LOC_REAR_PANEL = 0x07,
	AC_JACK_LOC_DRIVE_BAY,
	/* internal */
	AC_JACK_LOC_RISER = 0x17,
	AC_JACK_LOC_HDMI,
	AC_JACK_LOC_ATAPI,
	/* others */
	AC_JACK_LOC_MOBILE_IN = 0x37,
	AC_JACK_LOC_MOBILE_OUT,
};

/* Port connectivity (0-3) */
enum {
	AC_JACK_PORT_COMPLEX,
	AC_JACK_PORT_NONE,
	AC_JACK_PORT_FIXED,
	AC_JACK_PORT_BOTH,
};

/* max. connections to a widget */
#define HDA_MAX_CONNECTIONS	32

/* max. codec address */
#define HDA_MAX_CODEC_ADDRESS	0x0f

/*
 * Structures
 */

struct hda_bus;
struct hda_codec;
struct hda_pcm;
struct hda_pcm_stream;
struct hda_bus_unsolicited;

/* NID type */
typedef u16 hda_nid_t;

/* bus operators */
struct hda_bus_ops {
	/* send a single command */
	int (*command)(struct hda_codec *codec, hda_nid_t nid, int direct,
		       unsigned int verb, unsigned int parm);
	/* get a response from the last command */
	unsigned int (*get_response)(struct hda_codec *codec);
	/* free the private data */
	void (*private_free)(struct hda_bus *);
};

/* template to pass to the bus constructor */
struct hda_bus_template {
	void *private_data;
	struct pci_dev *pci;
	const char *modelname;
	struct hda_bus_ops ops;
};

/*
 * codec bus
 *
 * each controller needs to creata a hda_bus to assign the accessor.
 * A hda_bus contains several codecs in the list codec_list.
 */
struct hda_bus {
	struct snd_card *card;

	/* copied from template */
	void *private_data;
	struct pci_dev *pci;
	const char *modelname;
	struct hda_bus_ops ops;

	/* codec linked list */
	struct list_head codec_list;
	struct hda_codec *caddr_tbl[HDA_MAX_CODEC_ADDRESS + 1]; /* caddr -> codec */

	struct mutex cmd_mutex;

	/* unsolicited event queue */
	struct hda_bus_unsolicited *unsol;

	struct snd_info_entry *proc;
};

/*
 * codec preset
 *
 * Known codecs have the patch to build and set up the controls/PCMs
 * better than the generic parser.
 */
struct hda_codec_preset {
	unsigned int id;
	unsigned int mask;
	unsigned int subs;
	unsigned int subs_mask;
	unsigned int rev;
	const char *name;
	int (*patch)(struct hda_codec *codec);
};
	
/* ops set by the preset patch */
struct hda_codec_ops {
	int (*build_controls)(struct hda_codec *codec);
	int (*build_pcms)(struct hda_codec *codec);
	int (*init)(struct hda_codec *codec);
	void (*free)(struct hda_codec *codec);
	void (*unsol_event)(struct hda_codec *codec, unsigned int res);
#ifdef CONFIG_PM
	int (*suspend)(struct hda_codec *codec, pm_message_t state);
	int (*resume)(struct hda_codec *codec);
#endif
};

/* record for amp information cache */
struct hda_amp_info {
	u32 key;		/* hash key */
	u32 amp_caps;		/* amp capabilities */
	u16 vol[2];		/* current volume & mute */
	u16 status;		/* update flag */
	u16 next;		/* next link */
};

/* PCM callbacks */
struct hda_pcm_ops {
	int (*open)(struct hda_pcm_stream *info, struct hda_codec *codec,
		    struct snd_pcm_substream *substream);
	int (*close)(struct hda_pcm_stream *info, struct hda_codec *codec,
		     struct snd_pcm_substream *substream);
	int (*prepare)(struct hda_pcm_stream *info, struct hda_codec *codec,
		       unsigned int stream_tag, unsigned int format,
		       struct snd_pcm_substream *substream);
	int (*cleanup)(struct hda_pcm_stream *info, struct hda_codec *codec,
		       struct snd_pcm_substream *substream);
};

/* PCM information for each substream */
struct hda_pcm_stream {
	unsigned int substreams;	/* number of substreams, 0 = not exist */
	unsigned int channels_min;	/* min. number of channels */
	unsigned int channels_max;	/* max. number of channels */
	hda_nid_t nid;	/* default NID to query rates/formats/bps, or set up */
	u32 rates;	/* supported rates */
	u64 formats;	/* supported formats (SNDRV_PCM_FMTBIT_) */
	unsigned int maxbps;	/* supported max. bit per sample */
	struct hda_pcm_ops ops;
};

/* for PCM creation */
struct hda_pcm {
	char *name;
	struct hda_pcm_stream stream[2];
	unsigned int is_modem;	/* modem codec? */
};

/* codec information */
struct hda_codec {
	struct hda_bus *bus;
	unsigned int addr;	/* codec addr*/
	struct list_head list;	/* list point */

	hda_nid_t afg;	/* AFG node id */
	hda_nid_t mfg;	/* MFG node id */

	/* ids */
	u32 vendor_id;
	u32 subsystem_id;
	u32 revision_id;

	/* detected preset */
	const struct hda_codec_preset *preset;

	/* set by patch */
	struct hda_codec_ops patch_ops;

	/* resume phase - all controls should update even if
	 * the values are not changed
	 */
	unsigned int in_resume;

	/* PCM to create, set by patch_ops.build_pcms callback */
	unsigned int num_pcms;
	struct hda_pcm *pcm_info;

	/* codec specific info */
	void *spec;

	/* widget capabilities cache */
	unsigned int num_nodes;
	hda_nid_t start_nid;
	u32 *wcaps;

	/* hash for amp access */
	u16 amp_hash[32];
	int num_amp_entries;
	int amp_info_size;
	struct hda_amp_info *amp_info;

	struct mutex spdif_mutex;
	unsigned int spdif_status;	/* IEC958 status bits */
	unsigned short spdif_ctls;	/* SPDIF control bits */
	unsigned int spdif_in_enable;	/* SPDIF input enable? */
};

/* direction */
enum {
	HDA_INPUT, HDA_OUTPUT
};


/*
 * constructors
 */
int snd_hda_bus_new(struct snd_card *card, const struct hda_bus_template *temp,
		    struct hda_bus **busp);
int snd_hda_codec_new(struct hda_bus *bus, unsigned int codec_addr,
		      struct hda_codec **codecp);

/*
 * low level functions
 */
unsigned int snd_hda_codec_read(struct hda_codec *codec, hda_nid_t nid, int direct,
				unsigned int verb, unsigned int parm);
int snd_hda_codec_write(struct hda_codec *codec, hda_nid_t nid, int direct,
			unsigned int verb, unsigned int parm);
#define snd_hda_param_read(codec, nid, param) snd_hda_codec_read(codec, nid, 0, AC_VERB_PARAMETERS, param)
int snd_hda_get_sub_nodes(struct hda_codec *codec, hda_nid_t nid, hda_nid_t *start_id);
int snd_hda_get_connections(struct hda_codec *codec, hda_nid_t nid, hda_nid_t *conn_list, int max_conns);

struct hda_verb {
	hda_nid_t nid;
	u32 verb;
	u32 param;
};

void snd_hda_sequence_write(struct hda_codec *codec, const struct hda_verb *seq);

/* unsolicited event */
int snd_hda_queue_unsol_event(struct hda_bus *bus, u32 res, u32 res_ex);

/*
 * Mixer
 */
int snd_hda_build_controls(struct hda_bus *bus);

/*
 * PCM
 */
int snd_hda_build_pcms(struct hda_bus *bus);
void snd_hda_codec_setup_stream(struct hda_codec *codec, hda_nid_t nid, u32 stream_tag,
				int channel_id, int format);
unsigned int snd_hda_calc_stream_format(unsigned int rate, unsigned int channels,
					unsigned int format, unsigned int maxbps);
int snd_hda_query_supported_pcm(struct hda_codec *codec, hda_nid_t nid,
				u32 *ratesp, u64 *formatsp, unsigned int *bpsp);
int snd_hda_is_supported_format(struct hda_codec *codec, hda_nid_t nid,
				unsigned int format);

/*
 * Misc
 */
void snd_hda_get_codec_name(struct hda_codec *codec, char *name, int namelen);

/*
 * power management
 */
#ifdef CONFIG_PM
int snd_hda_suspend(struct hda_bus *bus, pm_message_t state);
int snd_hda_resume(struct hda_bus *bus);
#endif

#endif /* __SOUND_HDA_CODEC_H */
