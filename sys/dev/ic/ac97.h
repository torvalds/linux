/*	$OpenBSD: ac97.h,v 1.27 2018/04/11 04:48:31 ratchov Exp $	*/

/*
 * Copyright (c) 1999 Constantine Sapuntzakis
 *
 * Author:	Constantine Sapuntzakis <csapuntz@stanford.edu>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY CONSTANTINE SAPUNTZAKIS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

struct ac97_codec_if;

/*
 * This is the interface used to attach the AC97 compliant CODEC.
 */
enum ac97_host_flags {
	AC97_HOST_DONT_READ = 0x1,
	AC97_HOST_DONT_READANY = 0x2,
	AC97_HOST_SWAPPED_CHANNELS = 0x4,
	AC97_HOST_ALC650_PIN47_IS_EAPD = 0x8,
	AC97_HOST_VT1616_DYNEX = 0x10
};

struct ac97_host_if {
	void  *arg;

	int (*attach)(void *arg, struct ac97_codec_if *codecif);
	int (*read)(void *arg, u_int8_t reg, u_int16_t *val);
	int (*write)(void *arg, u_int8_t reg, u_int16_t val);
	void (*reset)(void *arg);

	enum ac97_host_flags (*flags)(void *arg);
	void (*spdif_event)(void *arg, int);
};

/*
 * This is the interface exported by the AC97 compliant CODEC
 */

struct ac97_codec_if_vtbl {
	int (*mixer_get_port)(struct ac97_codec_if *addr, mixer_ctrl_t *cp);
	int (*mixer_set_port)(struct ac97_codec_if *addr, mixer_ctrl_t *cp);
	int (*query_devinfo)(struct ac97_codec_if *addr, mixer_devinfo_t *cp);
	int (*get_portnum_by_name)(struct ac97_codec_if *addr, char *class,
	    char *device, char *qualifier);
	u_int16_t (*get_caps)(struct ac97_codec_if *codec_if);
	int (*set_rate)(struct ac97_codec_if *codec_if, int target,
	    u_long *rate);
	void (*set_clock)(struct ac97_codec_if *codec_if,
	    unsigned int clock);
	void (*lock)(struct ac97_codec_if *codec_if);
	void (*unlock)(struct ac97_codec_if *codec_if);
};

struct ac97_codec_if {
	struct ac97_softc *as;
	void (*initfunc)(struct ac97_softc *, int);
	struct ac97_codec_if_vtbl *vtbl;
};

int ac97_attach(struct ac97_host_if *);
int ac97_resume(struct ac97_host_if *, struct ac97_codec_if *);
int ac97_set_rate(struct ac97_codec_if *, int, u_long *);

#define	AC97_REG_RESET			0x00
#define	AC97_CAPS_MICIN			0x0001
#define	AC97_CAPS_TONECTRL		0x0004
#define	AC97_CAPS_SIMSTEREO		0x0008
#define	AC97_CAPS_HEADPHONES		0x0010
#define	AC97_CAPS_LOUDNESS		0x0020
#define	AC97_CAPS_DAC18			0x0040
#define	AC97_CAPS_DAC20			0x0080
#define	AC97_CAPS_ADC18			0x0100
#define	AC97_CAPS_ADC20			0x0200
#define	AC97_CAPS_ENHANCEMENT_MASK	0xfc00
#define	AC97_CAPS_ENHANCEMENT_SHIFT	10
#define	AC97_CAPS_ENHANCEMENT(reg)	(((reg) >> 10) & 0x1f)
#define	AC97_REG_MASTER_VOLUME		0x02
#define	AC97_REG_HEADPHONE_VOLUME	0x04
#define	AC97_REG_MASTER_VOLUME_MONO	0x06
#define	AC97_REG_MASTER_TONE		0x08
#define	AC97_REG_PCBEEP_VOLUME		0x0a
#define	AC97_REG_PHONE_VOLUME		0x0c
#define	AC97_REG_MIC_VOLUME		0x0e
#define	AC97_REG_LINEIN_VOLUME		0x10
#define	AC97_REG_CD_VOLUME		0x12
#define	AC97_REG_VIDEO_VOLUME		0x14
#define	AC97_REG_AUX_VOLUME		0x16
#define	AC97_REG_PCMOUT_VOLUME		0x18
#define	AC97_REG_RECORD_SELECT		0x1a
#define	AC97_REG_RECORD_GAIN		0x1c
#define	AC97_REG_RECORD_GAIN_MIC	0x1e
#define	AC97_REG_GP			0x20
#define	AC97_REG_3D_CONTROL		0x22
#define	AC97_REG_MODEM_SAMPLE_RATE	0x24
#define	AC97_REG_POWER			0x26
#define	AC97_POWER_ADC			0x0001
#define	AC97_POWER_DAC			0x0002
#define	AC97_POWER_ANL			0x0004
#define	AC97_POWER_REF			0x0008
#define	AC97_POWER_IN			0x0100
#define	AC97_POWER_OUT			0x0200
#define	AC97_POWER_MIXER		0x0400
#define	AC97_POWER_MIXER_VREF		0x0800
#define	AC97_POWER_ACLINK		0x1000
#define	AC97_POWER_CLK			0x2000
#define	AC97_POWER_AUX			0x4000
#define	AC97_POWER_EAMP			0x8000
	/* Extended Audio Register Set */
#define	AC97_REG_EXT_AUDIO_ID		0x28
#define	AC97_REG_EXT_AUDIO_CTRL		0x2a
#define	AC97_EXT_AUDIO_VRA		0x0001
#define	AC97_EXT_AUDIO_DRA		0x0002
#define	AC97_EXT_AUDIO_SPDIF		0x0004
#define	AC97_EXT_AUDIO_VRM		0x0008
#define	AC97_EXT_AUDIO_DSA_MASK		0x0030
#define	AC97_EXT_AUDIO_DSA00		0x0000
#define	AC97_EXT_AUDIO_DSA01		0x0010
#define	AC97_EXT_AUDIO_DSA10		0x0020
#define	AC97_EXT_AUDIO_DSA11		0x0030
#define	AC97_EXT_AUDIO_SPSA_MASK	0x0030
#define	AC97_EXT_AUDIO_SPSA34		0x0000
#define	AC97_EXT_AUDIO_SPSA78		0x0010
#define	AC97_EXT_AUDIO_SPSA69		0x0020
#define	AC97_EXT_AUDIO_SPSAAB		0x0030
#define	AC97_EXT_AUDIO_CDAC		0x0040
#define	AC97_EXT_AUDIO_SDAC		0x0080
#define	AC97_EXT_AUDIO_LDAC		0x0100
#define	AC97_EXT_AUDIO_AMAP		0x0200
#define AC97_EXT_AUDIO_SPCV		0x0400
#define	AC97_EXT_AUDIO_REV_11		0x0000
#define	AC97_EXT_AUDIO_REV_22		0x0400
#define	AC97_EXT_AUDIO_REV_23		0x0800
#define	AC97_EXT_AUDIO_REV_MASK		0x0c00
#define	AC97_EXT_AUDIO_ID		0xc000
#define	AC97_EXT_AUDIO_BITS		"\020\01vra\02dra\03spdif\04vrm\05dsa0\06dsa1\07cdac\010sdac\011ldac\012amap\013rev0\014rev1\017id0\020id1"
#define AC97_SINGLE_RATE		48000
#define AC97_REG_PCM_FRONT_DAC_RATE	0x2c
#define AC97_REG_PCM_SURR_DAC_RATE	0x2e
#define AC97_REG_PCM_LFE_DAC_RATE	0x30
#define AC97_REG_PCM_LR_ADC_RATE	0x32
#define AC97_REG_PCM_MIC_ADC_RATE	0x34
#define AC97_REG_CENTER_LFE_MASTER	0x36
#define AC97_REG_SURR_MASTER		0x38
#define	AC97_REG_SPDIF_CTRL		0x3a
#define	AC97_REG_SPDIF_CTRL_BITS	"\02\01pro\02/audio\03copy\04pre\014l\017drs\020valid"
#define	AC97_SPDIF_V			0x8000
#define	AC97_SPDIF_DRS			0x4000
#define	AC97_SPDIF_SPSR_MASK		0x3000
#define	AC97_SPDIF_SPSR_44K		0x0000
#define	AC97_SPDIF_SPSR_48K		0x2000
#define	AC97_SPDIF_SPSR_32K		0x1000
#define	AC97_SPDIF_L			0x0800
#define	AC97_SPDIF_CC_MASK		0x07f0
#define	AC97_SPDIF_PRE			0x0008
#define	AC97_SPDIF_COPY			0x0004
#define	AC97_SPDIF_NOAUDIO		0x0002
#define	AC97_SPDIF_PRO			0x0001

#define	AC97_REG_VENDOR_ID1		0x7c
#define	AC97_REG_VENDOR_ID2		0x7e
#define	AC97_VENDOR_ID_MASK		0xffffff00


/* Analog Devices codec specific data */
#define AC97_AD_REG_MISC	0x76
#define AC97_AD_MISC_MBG0	0x0001	/* 0 */
#define AC97_AD_MISC_MBG1	0x0002	/* 1 */
#define AC97_AD_MISC_VREFD	0x0004	/* 2 */
#define AC97_AD_MISC_VREFH	0x0008	/* 3 */
#define AC97_AD_MISC_SRU	0x0010	/* 4 */
#define AC97_AD_MISC_LOSEL	0x0020	/* 5 */
#define AC97_AD_MISC_2CMIC	0x0040	/* 6 */
#define AC97_AD_MISC_SPRD	0x0080	/* 7 */
#define AC97_AD_MISC_DMIX0	0x0100	/* 8 */
#define AC97_AD_MISC_DMIX1	0x0200	/* 9 */
#define AC97_AD_MISC_HPSEL	0x0400	/*10 */
#define AC97_AD_MISC_CLDIS	0x0800	/*11 */
#define AC97_AD_MISC_LODIS	0x1000	/*12 */
#define AC97_AD_MISC_MSPLT	0x2000	/*13 */
#define AC97_AD_MISC_AC97NC	0x4000	/*14 */
#define AC97_AD_MISC_DACZ	0x8000	/*15 */

/* Avance Logic codec specific data*/
#define AC97_ALC650_REG_MULTI_CHANNEL_CONTROL		0x6a
#define	AC97_ALC650_MCC_SLOT_MODIFY_MASK		0xc000
#define	AC97_ALC650_MCC_FRONTDAC_FROM_SPDIFIN		0x2000
#define	AC97_ALC650_MCC_SPDIFOUT_FROM_ADC		0x1000
#define	AC97_ALC650_MCC_PCM_FROM_SPDIFIN		0x0800
#define	AC97_ALC650_MCC_MIC_OR_CENTERLFE		0x0400
#define	AC97_ALC650_MCC_LINEIN_OR_SURROUND		0x0200
#define	AC97_ALC650_MCC_INDEPENDENT_MASTER_L		0x0080
#define	AC97_ALC650_MCC_INDEPENDENT_MASTER_R		0x0040
#define	AC97_ALC650_MCC_ANALOG_TO_CENTERLFE		0x0020
#define	AC97_ALC650_MCC_ANALOG_TO_SURROUND		0x0010
#define	AC97_ALC650_MCC_EXCHANGE_CENTERLFE		0x0008
#define	AC97_ALC650_MCC_CENTERLFE_DOWNMIX		0x0004
#define	AC97_ALC650_MCC_SURROUND_DOWNMIX		0x0002
#define	AC97_ALC650_MCC_LINEOUT_TO_SURROUND		0x0001
#define AC97_ALC650_REG_MISC				0x7a
#define	AC97_ALC650_MISC_PIN47				0x0002
#define	AC97_ALC650_MISC_VREFDIS			0x1000


/* Conexant codec specific data */
#define AC97_CX_REG_MISC	0x5c
#define AC97_CX_PCM		0x00
#define AC97_CX_AC3		0x02
#define AC97_CX_MASK		0x03
#define AC97_CX_COPYRIGHT	0x04
#define AC97_CX_SPDIFEN		0x08


/* VIA codec specific data */
#define AC97_VT_REG_TEST	0x5a
#define AC97_VT_LVL		0x8000	/* hp controls rear */
#define AC97_VT_LCTF		0x1000	/* lfe/center to front downmix */
#define AC97_VT_STF		0x0800	/* surround to front downmix */
#define AC97_VT_BPDC		0x0400	/* enable DC-offset cancellation */
#define AC97_VT_DC		0x0200	/* DC offset cancellation capable */

#define AC97_IS_FIXED_RATE(codec)	!((codec)->vtbl->get_caps(codec) & \
    AC97_EXT_AUDIO_VRA)
#define AC97_BITS_6CH	(AC97_EXT_AUDIO_SDAC | AC97_EXT_AUDIO_CDAC | \
    AC97_EXT_AUDIO_LDAC)
