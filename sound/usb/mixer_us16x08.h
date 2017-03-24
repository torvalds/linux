#ifndef __USB_MIXER_US16X08_H
#define __USB_MIXER_US16X08_H

#define SND_US16X08_MAX_CHANNELS 16

/* define some bias, cause some alsa-mixers wont work with
 * negative ranges or if mixer-min != 0
 */
#define SND_US16X08_NO_BIAS 0
#define SND_US16X08_FADER_BIAS 127
#define SND_US16X08_EQ_HIGHFREQ_BIAS 0x20
#define SND_US16X08_COMP_THRESHOLD_BIAS 0x20
#define SND_US16X08_COMP_ATTACK_BIAS 2
#define SND_US16X08_COMP_RELEASE_BIAS 1

/* get macro for components of kcontrol private_value */
#define SND_US16X08_KCBIAS(x) (((x)->private_value >> 24) & 0xff)
#define SND_US16X08_KCSTEP(x) (((x)->private_value >> 16) & 0xff)
#define SND_US16X08_KCMIN(x) (((x)->private_value >> 8) & 0xff)
#define SND_US16X08_KCMAX(x) (((x)->private_value >> 0) & 0xff)
/* set macro for kcontrol private_value */
#define SND_US16X08_KCSET(bias, step, min, max)  \
	(((bias) << 24) | ((step) << 16) | ((min) << 8) | (max))

/* the URB request/type to control Tascam mixers */
#define SND_US16X08_URB_REQUEST 0x1D
#define SND_US16X08_URB_REQUESTTYPE 0x40

/* the URB params to retrieve meter ranges */
#define SND_US16X08_URB_METER_REQUEST       0x1e
#define SND_US16X08_URB_METER_REQUESTTYPE   0xc0

#define MUA0(x, y) ((x)[(y) * 10 + 4])
#define MUA1(x, y) ((x)[(y) * 10 + 5])
#define MUA2(x, y) ((x)[(y) * 10 + 6])
#define MUB0(x, y) ((x)[(y) * 10 + 7])
#define MUB1(x, y) ((x)[(y) * 10 + 8])
#define MUB2(x, y) ((x)[(y) * 10 + 9])
#define MUC0(x, y) ((x)[(y) * 10 + 10])
#define MUC1(x, y) ((x)[(y) * 10 + 11])
#define MUC2(x, y) ((x)[(y) * 10 + 12])
#define MUC3(x, y) ((x)[(y) * 10 + 13])

/* Common Channel control IDs */
#define SND_US16X08_ID_BYPASS 0x45
#define SND_US16X08_ID_BUSS_OUT 0x44
#define SND_US16X08_ID_PHASE 0x85
#define SND_US16X08_ID_MUTE 0x83
#define SND_US16X08_ID_FADER 0x81
#define SND_US16X08_ID_PAN 0x82
#define SND_US16X08_ID_METER 0xB1

#define SND_US16X08_ID_EQ_BAND_COUNT 4
#define SND_US16X08_ID_EQ_PARAM_COUNT 4

/* EQ level IDs */
#define SND_US16X08_ID_EQLOWLEVEL 0x01
#define SND_US16X08_ID_EQLOWMIDLEVEL 0x02
#define SND_US16X08_ID_EQHIGHMIDLEVEL 0x03
#define SND_US16X08_ID_EQHIGHLEVEL 0x04

/* EQ frequence IDs */
#define SND_US16X08_ID_EQLOWFREQ 0x11
#define SND_US16X08_ID_EQLOWMIDFREQ 0x12
#define SND_US16X08_ID_EQHIGHMIDFREQ 0x13
#define SND_US16X08_ID_EQHIGHFREQ 0x14

/* EQ width IDs */
#define SND_US16X08_ID_EQLOWMIDWIDTH 0x22
#define SND_US16X08_ID_EQHIGHMIDWIDTH 0x23

#define SND_US16X08_ID_EQENABLE 0x30

#define EQ_STORE_BAND_IDX(x) ((x) & 0xf)
#define EQ_STORE_PARAM_IDX(x) (((x) & 0xf0) >> 4)

#define SND_US16X08_ID_ROUTE 0x00

/* Compressor Ids */
#define SND_US16X08_ID_COMP_BASE	0x32
#define SND_US16X08_ID_COMP_THRESHOLD	SND_US16X08_ID_COMP_BASE
#define SND_US16X08_ID_COMP_RATIO	(SND_US16X08_ID_COMP_BASE + 1)
#define SND_US16X08_ID_COMP_ATTACK	(SND_US16X08_ID_COMP_BASE + 2)
#define SND_US16X08_ID_COMP_RELEASE	(SND_US16X08_ID_COMP_BASE + 3)
#define SND_US16X08_ID_COMP_GAIN	(SND_US16X08_ID_COMP_BASE + 4)
#define SND_US16X08_ID_COMP_SWITCH	(SND_US16X08_ID_COMP_BASE + 5)
#define SND_US16X08_ID_COMP_COUNT	6

#define COMP_STORE_IDX(x) ((x) - SND_US16X08_ID_COMP_BASE)

struct snd_us16x08_eq_store {
	u8 val[SND_US16X08_ID_EQ_BAND_COUNT][SND_US16X08_ID_EQ_PARAM_COUNT]
		[SND_US16X08_MAX_CHANNELS];
};

struct snd_us16x08_comp_store {
	u8 val[SND_US16X08_ID_COMP_COUNT][SND_US16X08_MAX_CHANNELS];
};

struct snd_us16x08_meter_store {
	int meter_level[SND_US16X08_MAX_CHANNELS];
	int master_level[2]; /* level of meter for master output */
	int comp_index; /* round trip channel selector */
	int comp_active_index; /* channel select from user space mixer */
	int comp_level[16]; /* compressor reduction level */
	struct snd_us16x08_comp_store *comp_store;
};

struct snd_us16x08_control_params {
	struct snd_kcontrol_new *kcontrol_new;
	int control_id;
	int type;
	int num_channels;
	const char *name;
	int default_val;
};

#define snd_us16x08_switch_info snd_ctl_boolean_mono_info

int snd_us16x08_controls_create(struct usb_mixer_interface *mixer);
#endif /* __USB_MIXER_US16X08_H */
