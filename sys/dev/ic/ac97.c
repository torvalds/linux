/*	$OpenBSD: ac97.c,v 1.85 2024/04/29 00:29:48 jsg Exp $	*/

/*
 * Copyright (c) 1999, 2000 Constantine Sapuntzakis
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.  */

/* Partially inspired by FreeBSD's sys/dev/pcm/ac97.c. It came with
   the following copyright */

/*
 * Copyright (c) 1999 Cameron Grant <gandalf@vilnya.demon.co.uk>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/ic/ac97.h>

const struct audio_mixer_enum ac97_on_off = {
	2,
	{ { { AudioNoff } , 0 },
	{ { AudioNon }  , 1 } }
};

const struct audio_mixer_enum ac97_mic_select = {
	2,
	{ { { AudioNmicrophone "0" }, 0 },
	{ { AudioNmicrophone "1" }, 1 } }
};

const struct audio_mixer_enum ac97_mono_select = {
	2,
	{ { { AudioNmixerout }, 0 },
	{ { AudioNmicrophone }, 1 } }
};

const struct audio_mixer_enum ac97_source = {
	8,
	{ { { AudioNmicrophone } , 0 },
	{ { AudioNcd }, 1 },
	{ { "video" }, 2 },
	{ { AudioNaux }, 3 },
	{ { AudioNline }, 4 },
	{ { AudioNmixerout }, 5 },
	{ { AudioNmixerout AudioNmono }, 6 },
	{ { "phone" }, 7 }}
};

/*
 * Due to different values for each source that uses these structures,
 * the ac97_query_devinfo function sets delta in mixer_devinfo_t using
 * ac97_source_info.bits.
 */
const struct audio_mixer_value ac97_volume_stereo = {
	{ AudioNvolume },
	2
};

const struct audio_mixer_value ac97_volume_mono = {
	{ AudioNvolume },
	1
};

#define AudioNspdif	"spdif"

#define WRAP(a)  &a, sizeof(a)

const struct ac97_source_info {
	char *class;
	char *device;
	char *qualifier;
	int  type;

	const void *info;
	int16_t info_size;

	u_int8_t  reg;
	u_int16_t default_value;
	u_int8_t  bits:3;
	u_int8_t  ofs:4;
	u_int8_t  mute:1;
	u_int8_t  polarity:1;		/* Does 0 == MAX or MIN */
	enum {
		CHECK_NONE = 0,
		CHECK_SURROUND,
		CHECK_CENTER,
		CHECK_LFE,
		CHECK_HEADPHONES,
		CHECK_TONE,
		CHECK_MIC,
		CHECK_LOUDNESS,
		CHECK_3D,
		CHECK_SPDIF
	} req_feature;

	int16_t  prev;
	int16_t  next;
	int16_t  mixer_class;
} source_info[] = {
	{ AudioCinputs,		NULL,		NULL,
	  AUDIO_MIXER_CLASS, },
	{ AudioCoutputs,	NULL,		NULL,
	  AUDIO_MIXER_CLASS, },
	{ AudioCrecord,		NULL,		NULL,
	  AUDIO_MIXER_CLASS, },
	/* Stereo master volume*/
	{ AudioCoutputs,	AudioNmaster,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_MASTER_VOLUME, 0x8000, 5, 0, 1,
	},
	/* Mono volume */
	{ AudioCoutputs,	AudioNmono,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_MASTER_VOLUME_MONO, 0x8000, 6, 0, 1,
	},
	{ AudioCoutputs,	AudioNmono,	AudioNsource,
	  AUDIO_MIXER_ENUM, WRAP(ac97_mono_select),
	  AC97_REG_GP, 0x0000, 1, 9, 0,
	},
	/* Headphone volume */
	{ AudioCoutputs,	AudioNheadphone, NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_HEADPHONE_VOLUME, 0x8000, 6, 0, 1, 0, CHECK_HEADPHONES
	},
	/* Surround volume - logic hard coded for mute */
	{ AudioCoutputs,	AudioNsurround,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_SURR_MASTER, 0x8080, 5, 0, 1, 0, CHECK_SURROUND
	},
	/* Center volume*/
	{ AudioCoutputs,	AudioNcenter,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_CENTER_LFE_MASTER, 0x8080, 5, 0, 0, 0, CHECK_CENTER
	},
	{ AudioCoutputs,	AudioNcenter,	AudioNmute,
	  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
	  AC97_REG_CENTER_LFE_MASTER, 0x8080, 1, 7, 0, 0, CHECK_CENTER
	},
	/* LFE volume*/
	{ AudioCoutputs,	AudioNlfe,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_CENTER_LFE_MASTER, 0x8080, 5, 8, 0, 0, CHECK_LFE
	},
	{ AudioCoutputs,	AudioNlfe,	AudioNmute,
	  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
	  AC97_REG_CENTER_LFE_MASTER, 0x8080, 1, 15, 0, 0, CHECK_LFE
	},
	/* Tone */
	{ AudioCoutputs,	"tone",	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_MASTER_TONE, 0x0f0f, 4, 0, 0, 0, CHECK_TONE
	},
	/* PC Beep Volume */
	{ AudioCinputs,		AudioNspeaker,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_PCBEEP_VOLUME, 0x0000, 4, 1, 1,
	},

	/* Phone */
	{ AudioCinputs,		"phone",	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_PHONE_VOLUME, 0x8008, 5, 0, 1,
	},
	/* Mic Volume */
	{ AudioCinputs,		AudioNmicrophone, NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_MIC_VOLUME, 0x8008, 5, 0, 1,
	},
	{ AudioCinputs,		AudioNmicrophone, AudioNpreamp,
	  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
	  AC97_REG_MIC_VOLUME, 0x8008, 1, 6, 0,
	},
	{ AudioCinputs,		AudioNmicrophone, AudioNsource,
	  AUDIO_MIXER_ENUM, WRAP(ac97_mic_select),
	  AC97_REG_GP, 0x0000, 1, 8, 0,
	},
	/* Line in Volume */
	{ AudioCinputs,		AudioNline,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_LINEIN_VOLUME, 0x8808, 5, 0, 1,
	},
	/* CD Volume */
	{ AudioCinputs,		AudioNcd,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_CD_VOLUME, 0x8808, 5, 0, 1,
	},
	/* Video Volume */
	{ AudioCinputs,		AudioNvideo,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_VIDEO_VOLUME, 0x8808, 5, 0, 1,
	},
	/* AUX volume */
	{ AudioCinputs,		AudioNaux,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_AUX_VOLUME, 0x8808, 5, 0, 1,
	},
	/* PCM out volume */
	{ AudioCinputs,		AudioNdac,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_PCMOUT_VOLUME, 0x8808, 5, 0, 1,
	},
	/* Record Source - some logic for this is hard coded - see below */
	{ AudioCrecord,		AudioNsource,	NULL,
	  AUDIO_MIXER_ENUM, WRAP(ac97_source),
	  AC97_REG_RECORD_SELECT, 0x0000, 3, 0, 0,
	},
	/* Record Gain */
	{ AudioCrecord,		AudioNvolume,	NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_stereo),
	  AC97_REG_RECORD_GAIN, 0x8000, 4, 0, 1,
	},
	/* Record Gain mic */
	{ AudioCrecord,		AudioNmicrophone, NULL,
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_RECORD_GAIN_MIC, 0x8000, 4, 0, 1, 1, CHECK_MIC
	},
	/* */
	{ AudioCoutputs,	AudioNloudness,	NULL,
	  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
	  AC97_REG_GP, 0x0000, 1, 12, 0, 0, CHECK_LOUDNESS
	},
	{ AudioCoutputs,	AudioNspatial,	NULL,
	  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
	  AC97_REG_GP, 0x0000, 1, 13, 0, 1, CHECK_3D
	},
	{ AudioCoutputs,	AudioNspatial,	"center",
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_3D_CONTROL, 0x0000, 4, 8, 0, 1, CHECK_3D
	},
	{ AudioCoutputs,	AudioNspatial,	"depth",
	  AUDIO_MIXER_VALUE, WRAP(ac97_volume_mono),
	  AC97_REG_3D_CONTROL, 0x0000, 4, 0, 0, 1, CHECK_3D
	},
	/* External Amp */
	{ AudioCoutputs,	AudioNextamp,	NULL,
	  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
	  AC97_REG_POWER, 0x0000, 1, 15, 0, 0
	},
	/* S/PDIF output enable */
	{ AudioCoutputs,	AudioNspdif,	NULL,
	  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
	  AC97_REG_EXT_AUDIO_CTRL, 0x0000, 1, 2, 0, 0, CHECK_SPDIF
	}

	/* Missing features: Simulated Stereo, POP, Loopback mode */
};

/*
 * Check out http://www.intel.com/technology/computing/audio/index.htm
 * for information on AC-97
 */

struct ac97_softc {
	/* ac97_codec_if must be at the first of ac97_softc. */
	struct ac97_codec_if codec_if;
	struct ac97_host_if *host_if;
#define MAX_SOURCES	(2 * nitems(source_info))
	struct ac97_source_info source_info[MAX_SOURCES];
	int num_source_info;
	enum ac97_host_flags host_flags;
	unsigned int ac97_clock; /* usually 48000 */
#define AC97_STANDARD_CLOCK	48000U
	u_int16_t caps;		/* -> AC97_REG_RESET */
	u_int16_t ext_id;	/* -> AC97_REG_EXT_AUDIO_ID */
	u_int16_t shadow_reg[128];
	int lock_counter;
};

int	ac97_mixer_get_port(struct ac97_codec_if *, mixer_ctrl_t *);
int	ac97_mixer_set_port(struct ac97_codec_if *, mixer_ctrl_t *);
void	ac97_lock(struct ac97_codec_if *);
void	ac97_unlock(struct ac97_codec_if *);
int	ac97_query_devinfo(struct ac97_codec_if *, mixer_devinfo_t *);
int	ac97_get_portnum_by_name(struct ac97_codec_if *, char *, char *,
	    char *);
int	ac97_set_rate(struct ac97_codec_if *codec_if, int target, u_long *rate);
void	ac97_set_clock(struct ac97_codec_if *codec_if, unsigned int clock);
u_int16_t ac97_get_extcaps(struct ac97_codec_if *codec_if);
int	ac97_add_port(struct ac97_softc *as, struct ac97_source_info *src);

void	ac97_ad1885_init(struct ac97_softc *, int);
void	ac97_ad1886_init(struct ac97_softc *, int);
void	ac97_ad198x_init(struct ac97_softc *, int);
void	ac97_alc650_init(struct ac97_softc *, int);
void	ac97_cx20468_init(struct ac97_softc *, int);
void	ac97_vt1616_init(struct ac97_softc *, int);

struct ac97_codec_if_vtbl ac97civ = {
	ac97_mixer_get_port,
	ac97_mixer_set_port,
	ac97_query_devinfo,
	ac97_get_portnum_by_name,
	ac97_get_extcaps,
	ac97_set_rate,
	ac97_set_clock,
	ac97_lock,
	ac97_unlock
};

const struct ac97_codecid {
	u_int8_t id;
	u_int8_t mask;
	u_int8_t rev;
	u_int8_t shift;	/* no use yet */
	char * const name;
	void (*init)(struct ac97_softc *, int);
}  ac97_ad[] = {
	{ 0x03, 0xff, 0, 0,	"AD1819" },
	{ 0x40, 0xff, 0, 0,	"AD1881" },
	{ 0x48, 0xff, 0, 0,	"AD1881A" },
	{ 0x60, 0xff, 0, 0,	"AD1885", 	ac97_ad1885_init },
	{ 0x61, 0xff, 0, 0,	"AD1886",	ac97_ad1886_init },
	{ 0x63, 0xff, 0, 0,	"AD1886A" },
	{ 0x68, 0xff, 0, 0,	"AD1888",	ac97_ad198x_init },
	{ 0x70, 0xff, 0, 0,	"AD1980",	ac97_ad198x_init },
	{ 0x72, 0xff, 0, 0,	"AD1981A" },
	{ 0x74, 0xff, 0, 0,	"AD1981B" },
	{ 0x75, 0xff, 0, 0,	"AD1985",	ac97_ad198x_init },
}, ac97_ak[] = {
	{ 0x00,	0xfe, 1, 0,	"AK4540" },
	{ 0x01,	0xfe, 1, 0,	"AK4540" },
	{ 0x02,	0xff, 0, 0,	"AK4543" },
	{ 0x05,	0xff, 0, 0,	"AK4544" },
	{ 0x06,	0xff, 0, 0,	"AK4544A" },
	{ 0x07,	0xff, 0, 0,	"AK4545" },
}, ac97_av[] = {
	{ 0x10, 0xff, 0, 0,	"ALC200" },
	{ 0x20, 0xff, 0, 0,	"ALC650" },
	{ 0x21, 0xff, 0, 0,	"ALC650D" },
	{ 0x22, 0xff, 0, 0,	"ALC650E" },
	{ 0x23, 0xff, 0, 0,	"ALC650F" },
	{ 0x30, 0xff, 0, 0,	"ALC101" },
	{ 0x40, 0xff, 0, 0,	"ALC202" },
	{ 0x50, 0xff, 0, 0,	"ALC250" },
	{ 0x52, 0xff, 0, 0,	"ALC250A?" },
	{ 0x60, 0xf0, 0xf, 0,	"ALC655",	ac97_alc650_init },
	{ 0x70, 0xf0, 0xf, 0,	"ALC203" },
	{ 0x80, 0xf0, 0xf, 0,	"ALC658",	ac97_alc650_init },
	{ 0x90, 0xf0, 0xf, 0,	"ALC850" },
}, ac97_rl[] = {
	{ 0x00, 0xf0, 0xf, 0,	"RL5306" },
	{ 0x10, 0xf0, 0xf, 0,	"RL5382" },
	{ 0x20, 0xf0, 0xf, 0,	"RL5383" },
}, ac97_cm[] = {
	{ 0x41,	0xff, 0, 0,	"CMI9738" },
	{ 0x61,	0xff, 0, 0,	"CMI9739" },
	{ 0x78,	0xff, 0, 0,	"CMI9761A" },
	{ 0x82,	0xff, 0, 0,	"CMI9761B" },
	{ 0x83,	0xff, 0, 0,	"CMI9761A+" },
}, ac97_cr[] = {
	{ 0x84,	0xff, 0, 0,	"EV1938" },
}, ac97_cs[] = {
	{ 0x00,	0xf8, 7, 0,	"CS4297" },
	{ 0x10,	0xf8, 7, 0,	"CS4297A" },
	{ 0x20,	0xf8, 7, 0,	"CS4298" },
	{ 0x28,	0xf8, 7, 0,	"CS4294" },
	{ 0x30,	0xf8, 7, 0,	"CS4299" },
	{ 0x48,	0xf8, 7, 0,	"CS4201" },
	{ 0x58,	0xf8, 7, 0,	"CS4205" },
	{ 0x60,	0xf8, 7, 0,	"CS4291" },
	{ 0x70,	0xf8, 7, 0,	"CS4202" },
}, ac97_cx[] = {
	{ 0x21, 0xff, 0, 0,	"HSD11246" },
	{ 0x28, 0xf8, 7, 0,	"CX20468",	ac97_cx20468_init },
	{ 0x30, 0xff, 0, 0,	"CXT48", },
	{ 0x42, 0xff, 0, 0,	"CXT66", },
}, ac97_dt[] = {
	{ 0x00, 0xff, 0, 0,	"DT0398" },
}, ac97_em[] = {
	{ 0x23, 0xff, 0, 0,	"EM28023" },
	{ 0x28, 0xff, 0, 0,	"EM28028" },
}, ac97_es[] = {
	{ 0x08, 0xff, 0, 0,	"ES1921" },
}, ac97_is[] = {
	{ 0x00, 0xff, 0, 0,	"HMP9701" },
}, ac97_ic[] = {
	{ 0x01, 0xff, 0, 0,	"ICE1230" },
	{ 0x11, 0xff, 0, 0,	"ICE1232" },
	{ 0x14, 0xff, 0, 0,	"ICE1232A" },
	{ 0x51, 0xff, 0, 0,	"VIA VT1616" },
	{ 0x52, 0xff, 0, 0,	"VIA VT1616i",	ac97_vt1616_init },
}, ac97_it[] = {
	{ 0x20, 0xff, 0, 0,	"ITE2226E" },
	{ 0x60, 0xff, 0, 0,	"ITE2646E" },
}, ac97_ns[] = {
	{ 0x00,	0xff, 0, 0,	"LM454[03568]" },
	{ 0x31,	0xff, 0, 0,	"LM4549" },
	{ 0x40, 0xff, 0, 0,	"LM4540" },
	{ 0x43, 0xff, 0, 0,	"LM4543" },
	{ 0x46, 0xff, 0, 0,	"LM4546A" },
	{ 0x48, 0xff, 0, 0,	"LM4548A" },
	{ 0x49, 0xff, 0, 0,	"LM4549A" },
	{ 0x50, 0xff, 0, 0,	"LM4550" },
}, ac97_ps[] = {
	{ 0x01,	0xff, 0, 0,	"UCB1510" },
	{ 0x04,	0xff, 0, 0,	"UCB1400" },
}, ac97_sl[] = {
	{ 0x20,	0xe0, 0, 0,	"Si3036/38" },
}, ac97_st[] = {
	{ 0x00,	0xff, 0, 0,	"STAC9700" },
	{ 0x04,	0xff, 0, 0,	"STAC970[135]" },
	{ 0x05,	0xff, 0, 0,	"STAC9704" },
	{ 0x08,	0xff, 0, 0,	"STAC9708/11" },
	{ 0x09,	0xff, 0, 0,	"STAC9721/23" },
	{ 0x44,	0xff, 0, 0,	"STAC9744/45" },
	{ 0x50,	0xff, 0, 0,	"STAC9750/51" },
	{ 0x52,	0xff, 0, 0,	"STAC9752/53" },
	{ 0x56,	0xff, 0, 0,	"STAC9756/57" },
	{ 0x58,	0xff, 0, 0,	"STAC9758/59" },
	{ 0x60,	0xff, 0, 0,	"STAC9760/61" },
	{ 0x62,	0xff, 0, 0,	"STAC9762/63" },
	{ 0x66,	0xff, 0, 0,	"STAC9766/67" },
	{ 0x84,	0xff, 0, 0,	"STAC9784/85" },
}, ac97_vi[] = {
	{ 0x61, 0xff, 0, 0,	"VT1612A" },
	{ 0x70, 0xff, 0, 0,	"VT1617" },
}, ac97_tt[] = {
	{ 0x02,	0xff, 0, 0,	"TR28022" },
	{ 0x03,	0xff, 0, 0,	"TR28023" },
	{ 0x06,	0xff, 0, 0,	"TR28026" },
	{ 0x08,	0xff, 0, 0,	"TR28028" },
	{ 0x23,	0xff, 0, 0,	"TR28602" },
}, ac97_ti[] = {
	{ 0x20, 0xff, 0, 0,	"TLC320AD9xC" },
}, ac97_wb[] = {
	{ 0x01, 0xff, 0, 0,	"W83971D" },
}, ac97_wo[] = {
	{ 0x00,	0xff, 0, 0,	"WM9701A" },
	{ 0x03,	0xff, 0, 0,	"WM9704M/Q-0" }, /* & WM9703 */
	{ 0x04,	0xff, 0, 0,	"WM9704M/Q-1" },
	{ 0x05,	0xff, 0, 0,	"WM9705/10" },
	{ 0x09,	0xff, 0, 0,	"WM9709" },
	{ 0x12,	0xff, 0, 0,	"WM9711/12" },
}, ac97_ym[] = {
	{ 0x00, 0xff, 0, 0,	"YMF743-S" },
	{ 0x02, 0xff, 0, 0,	"YMF752-S" },
	{ 0x03, 0xff, 0, 0,	"YMF753-S" },
};

#define	cl(n)	n, nitems(n)
const struct ac97_vendorid {
	u_int32_t id;
	char * const name;
	const struct ac97_codecid * const codecs;
	u_int8_t num;
} ac97_vendors[] = {
	{ 0x01408300, "Creative",		cl(ac97_cr) },
	{ 0x41445300, "Analog Devices",		cl(ac97_ad) },
	{ 0x414b4D00, "Asahi Kasei",		cl(ac97_ak) },
	{ 0x414c4300, "Realtek",		cl(ac97_rl) },
	{ 0x414c4700, "Avance Logic",		cl(ac97_av) },
	{ 0x434d4900, "C-Media Electronics",	cl(ac97_cm) },
	{ 0x43525900, "Cirrus Logic",		cl(ac97_cs) },
	{ 0x43585400, "Conexant",		cl(ac97_cx) },
	{ 0x44543000, "Diamond Technology",	cl(ac97_dt) },
	{ 0x454d4300, "eMicro",			cl(ac97_em) },
	{ 0x45838300, "ESS Technology",		cl(ac97_es) },
	{ 0x48525300, "Intersil",		cl(ac97_is) },
	{ 0x49434500, "ICEnsemble",		cl(ac97_ic) },
	{ 0x49544500, "ITE, Inc.",		cl(ac97_it) },
	{ 0x4e534300, "National Semiconductor", cl(ac97_ns) },
	{ 0x50534300, "Philips Semiconductor",	cl(ac97_ps) },
	{ 0x53494c00, "Silicon Laboratory",	cl(ac97_sl) },
	{ 0x54524100, "TriTech Microelectronics", cl(ac97_tt) },
	{ 0x54584e00, "Texas Instruments",	cl(ac97_ti) },
	{ 0x56494100, "VIA Technologies",	cl(ac97_vi) },
	{ 0x57454300, "Winbond",		cl(ac97_wb) },
	{ 0x574d4c00, "Wolfson",		cl(ac97_wo) },
	{ 0x594d4800, "Yamaha",			cl(ac97_ym) },
	{ 0x83847600, "SigmaTel",		cl(ac97_st) },
};
#undef cl

const char * const ac97enhancement[] = {
	"No 3D Stereo",
	"Analog Devices Phat Stereo",
	"Creative",
	"National Semi 3D",
	"Yamaha Ymersion",
	"BBE 3D",
	"Crystal Semi 3D",
	"Qsound QXpander",
	"Spatializer 3D",
	"SRS 3D",
	"Platform Tech 3D",
	"AKM 3D",
	"Aureal",
	"AZTECH 3D",
	"Binaura 3D",
	"ESS Technology",
	"Harman International VMAx",
	"Nvidea 3D",
	"Philips Incredible Sound",
	"Texas Instruments 3D",
	"VLSI Technology 3D",
	"TriTech 3D",
	"Realtek 3D",
	"Samsung 3D",
	"Wolfson Microelectronics 3D",
	"Delta Integration 3D",
	"SigmaTel 3D",
	"KS Waves 3D",
	"Rockwell 3D",
	"Unknown 3D",
	"Unknown 3D",
	"Unknown 3D"
};

const char * const ac97feature[] = {
	"mic channel",
	"reserved",
	"tone",
	"simulated stereo",
	"headphone",
	"bass boost",
	"18 bit DAC",
	"20 bit DAC",
	"18 bit ADC",
	"20 bit ADC"
};


int	ac97_str_equal(const char *, const char *);
int	ac97_check_capability(struct ac97_softc *, int);
void	ac97_setup_source_info(struct ac97_softc *);
void	ac97_setup_defaults(struct ac97_softc *);
int	ac97_read(struct ac97_softc *, u_int8_t, u_int16_t *);
int	ac97_write(struct ac97_softc *, u_int8_t, u_int16_t);


#ifdef AUDIO_DEBUG
#define DPRINTF(x)	if (ac97debug) printf x
#define DPRINTFN(n,x)	if (ac97debug>(n)) printf x
#ifdef AC97_DEBUG
int	ac97debug = 1;
#else
int	ac97debug = 0;
#endif
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

int
ac97_read(struct ac97_softc *as, u_int8_t reg, u_int16_t *val)
{
	int error;

	if (((as->host_flags & AC97_HOST_DONT_READ) &&
	    (reg != AC97_REG_VENDOR_ID1 && reg != AC97_REG_VENDOR_ID2 &&
	    reg != AC97_REG_RESET)) ||
	    (as->host_flags & AC97_HOST_DONT_READANY)) {
		*val = as->shadow_reg[reg >> 1];
		return (0);
	}

	if ((error = as->host_if->read(as->host_if->arg, reg, val)))
		*val = as->shadow_reg[reg >> 1];
	return (error);
}

int
ac97_write(struct ac97_softc *as, u_int8_t reg, u_int16_t val)
{
	as->shadow_reg[reg >> 1] = val;
	return (as->host_if->write(as->host_if->arg, reg, val));
}

void
ac97_setup_defaults(struct ac97_softc *as)
{
	int idx;

	bzero(as->shadow_reg, sizeof(as->shadow_reg));

	for (idx = 0; idx < nitems(source_info); idx++) {
		const struct ac97_source_info *si = &source_info[idx];

		ac97_write(as, si->reg, si->default_value);
	}
}

int
ac97_str_equal(const char *a, const char *b)
{
	return ((a == b) || (a && b && (!strcmp(a, b))));
}

int
ac97_check_capability(struct ac97_softc *as, int check)
{
	switch (check) {
	case CHECK_NONE:
		return 1;
	case CHECK_SURROUND:
		return as->ext_id & AC97_EXT_AUDIO_SDAC;
	case CHECK_CENTER:
		return as->ext_id & AC97_EXT_AUDIO_CDAC;
	case CHECK_LFE:
		return as->ext_id & AC97_EXT_AUDIO_LDAC;
	case CHECK_SPDIF:
		return as->ext_id & AC97_EXT_AUDIO_SPDIF;
	case CHECK_HEADPHONES:
		return as->caps & AC97_CAPS_HEADPHONES;
	case CHECK_TONE:
		return as->caps & AC97_CAPS_TONECTRL;
	case CHECK_MIC:
		return as->caps & AC97_CAPS_MICIN;
	case CHECK_LOUDNESS:
		return as->caps & AC97_CAPS_LOUDNESS;
	case CHECK_3D:
		return AC97_CAPS_ENHANCEMENT(as->caps) != 0;
	default:
		printf("%s: internal error: feature=%d\n", __func__, check);
		return 0;
	}
}

void
ac97_setup_source_info(struct ac97_softc *as)
{
	struct ac97_source_info *si, *si2;
	int idx, ouridx;

	for (idx = 0, ouridx = 0; idx < nitems(source_info); idx++) {
		si = &as->source_info[ouridx];

		if (!ac97_check_capability(as, source_info[idx].req_feature))
			continue;

		bcopy(&source_info[idx], si, sizeof(*si));

		switch (si->type) {
		case AUDIO_MIXER_CLASS:
			si->mixer_class = ouridx;
			ouridx++;
			break;
		case AUDIO_MIXER_VALUE:
			/* Todo - Test to see if it works */
			ouridx++;

			/* Add an entry for mute, if necessary */
			if (si->mute) {
				si = &as->source_info[ouridx];
				bcopy(&source_info[idx], si, sizeof(*si));
				si->qualifier = AudioNmute;
				si->type = AUDIO_MIXER_ENUM;
				si->info = &ac97_on_off;
				si->info_size = sizeof(ac97_on_off);
				si->bits = 1;
				si->ofs = 15;
				si->mute = 0;
				si->polarity = 0;
				ouridx++;
			}
			break;
		case AUDIO_MIXER_ENUM:
			/* Todo - Test to see if it works */
			ouridx++;
			break;
		default:
			printf ("ac97: shouldn't get here\n");
			break;
		}
	}

	as->num_source_info = ouridx;

	for (idx = 0; idx < as->num_source_info; idx++) {
		int idx2, previdx;

		si = &as->source_info[idx];

		/* Find mixer class */
		for (idx2 = 0; idx2 < as->num_source_info; idx2++) {
			si2 = &as->source_info[idx2];

			if (si2->type == AUDIO_MIXER_CLASS &&
			    ac97_str_equal(si->class, si2->class)) {
				si->mixer_class = idx2;
			}
		}


		/* Setup prev and next pointers */
		if (si->prev != 0 || si->qualifier)
			continue;

		si->prev = AUDIO_MIXER_LAST;
		previdx = idx;

		for (idx2 = 0; idx2 < as->num_source_info; idx2++) {
			if (idx2 == idx)
				continue;

			si2 = &as->source_info[idx2];

			if (!si2->prev &&
			    ac97_str_equal(si->class, si2->class) &&
			    ac97_str_equal(si->device, si2->device)) {
				as->source_info[previdx].next = idx2;
				as->source_info[idx2].prev = previdx;

				previdx = idx2;
			}
		}

		as->source_info[previdx].next = AUDIO_MIXER_LAST;
	}
}

int
ac97_attach(struct ac97_host_if *host_if)
{
	struct ac97_softc *as;
	u_int16_t id1, id2, val;
	u_int32_t id;
	u_int16_t extstat, rate;
	mixer_ctrl_t ctl;
	int error, i;
	void (*initfunc)(struct ac97_softc *, int);

	initfunc = NULL;

	if (!(as = malloc(sizeof(*as), M_DEVBUF, M_NOWAIT | M_ZERO)))
		return (ENOMEM);

	as->codec_if.as = as;
	as->codec_if.vtbl = &ac97civ;
	as->host_if = host_if;

	if ((error = host_if->attach(host_if->arg, &as->codec_if))) {
		free(as, M_DEVBUF, sizeof(*as));
		return (error);
	}

	host_if->reset(host_if->arg);
	DELAY(1000);

	host_if->write(host_if->arg, AC97_REG_POWER, 0);
	host_if->write(host_if->arg, AC97_REG_RESET, 0);
	DELAY(10000);

	if (host_if->flags)
		as->host_flags = host_if->flags(host_if->arg);

	ac97_setup_defaults(as);
	ac97_read(as, AC97_REG_VENDOR_ID1, &id1);
	ac97_read(as, AC97_REG_VENDOR_ID2, &id2);
	ac97_read(as, AC97_REG_RESET, &as->caps);

	id = (id1 << 16) | id2;
	if (id) {
		register const struct ac97_vendorid *vendor;
		register const struct ac97_codecid *codec;

		printf("ac97: codec id 0x%08x", id);
		for (vendor = &ac97_vendors[sizeof(ac97_vendors) /
		     sizeof(ac97_vendors[0]) - 1];
		     vendor >= ac97_vendors; vendor--) {
			if (vendor->id == (id & AC97_VENDOR_ID_MASK)) {
				printf(" (%s", vendor->name);
				for (codec = &vendor->codecs[vendor->num-1];
				     codec >= vendor->codecs; codec--) {
					if (codec->id == (id & codec->mask))
						break;
				}
				if (codec >= vendor->codecs && codec->mask) {
					printf(" %s", codec->name);
					initfunc = codec->init;
				} else
					printf(" <%02x>", id & 0xff);
				if (codec >= vendor->codecs && codec->rev)
					printf(" rev %d", id & codec->rev);
				printf(")");
				break;
			}
		}
		printf("\n");
	} else
		printf("ac97: codec id not read\n");

	if (as->caps) {
		printf("ac97: codec features ");
		for (i = 0; i < 10; i++) {
			if (as->caps & (1 << i))
				printf("%s, ", ac97feature[i]);
		}
		printf("%s\n",
		    ac97enhancement[AC97_CAPS_ENHANCEMENT(as->caps)]);
	}


	as->ac97_clock = AC97_STANDARD_CLOCK;
	ac97_read(as, AC97_REG_EXT_AUDIO_ID, &as->ext_id);
	if (as->ext_id & (AC97_EXT_AUDIO_VRA | AC97_EXT_AUDIO_DRA
			  | AC97_EXT_AUDIO_SPDIF | AC97_EXT_AUDIO_VRM
			  | AC97_EXT_AUDIO_CDAC | AC97_EXT_AUDIO_SDAC
			  | AC97_EXT_AUDIO_LDAC)) {

		ac97_read(as, AC97_REG_EXT_AUDIO_CTRL, &extstat);
		extstat &= ~AC97_EXT_AUDIO_DRA;

		if (as->ext_id & AC97_EXT_AUDIO_VRM)
			extstat |= AC97_EXT_AUDIO_VRM;

		if (as->ext_id & AC97_EXT_AUDIO_LDAC)
			extstat |= AC97_EXT_AUDIO_LDAC;
		if (as->ext_id & AC97_EXT_AUDIO_SDAC)
			extstat |= AC97_EXT_AUDIO_SDAC;
		if (as->ext_id & AC97_EXT_AUDIO_CDAC)
			extstat |= AC97_EXT_AUDIO_CDAC;
		if (as->ext_id & AC97_EXT_AUDIO_SPDIF) {
			/* XXX S/PDIF gets same data as DAC?
			 * maybe this should be settable?
			 * default is SPSAAB (10/11) on AD1980 and ALC codecs.
			 */
			extstat &= ~AC97_EXT_AUDIO_SPSA_MASK;
			extstat |= AC97_EXT_AUDIO_SPSA34;
			ac97_read(as, AC97_REG_SPDIF_CTRL, &val);
			val = (val & ~AC97_SPDIF_SPSR_MASK) |
			    AC97_SPDIF_SPSR_48K;
			ac97_write(as, AC97_REG_SPDIF_CTRL, val);
		}
		if (as->ext_id & AC97_EXT_AUDIO_VRA)
			extstat |= AC97_EXT_AUDIO_VRA;
		ac97_write(as, AC97_REG_EXT_AUDIO_CTRL, extstat);
		if (as->ext_id & AC97_EXT_AUDIO_VRA) {
			/* VRA should be enabled. */
			/* so it claims to do variable rate, let's make sure */
			ac97_write(as, AC97_REG_PCM_FRONT_DAC_RATE, 44100);
			ac97_read(as, AC97_REG_PCM_FRONT_DAC_RATE, &rate);
			if (rate != 44100) {
				/* We can't believe ext_id */
				as->ext_id = 0;
			}
			/* restore the default value */
			ac97_write(as, AC97_REG_PCM_FRONT_DAC_RATE,
				   AC97_SINGLE_RATE);
		}
	}

	ac97_setup_source_info(as);

	DELAY(900 * 1000);

	/* use initfunc for specific device */
	as->codec_if.initfunc = initfunc;
	if (initfunc != NULL)
		initfunc(as, 0);

	/* Just enable the DAC and master volumes by default */
	bzero(&ctl, sizeof(ctl));

	ctl.type = AUDIO_MIXER_ENUM;
	ctl.un.ord = 0;  /* off */
	ctl.dev = ac97_get_portnum_by_name(&as->codec_if, AudioCoutputs,
	    AudioNmaster, AudioNmute);
	ac97_mixer_set_port(&as->codec_if, &ctl);

	ctl.dev = ac97_get_portnum_by_name(&as->codec_if, AudioCinputs,
	    AudioNdac, AudioNmute);
	ac97_mixer_set_port(&as->codec_if, &ctl);

	ctl.dev = ac97_get_portnum_by_name(&as->codec_if, AudioCrecord,
	    AudioNvolume, AudioNmute);
	ac97_mixer_set_port(&as->codec_if, &ctl);

	ctl.type = AUDIO_MIXER_ENUM;
	ctl.un.ord = 0;
	ctl.dev = ac97_get_portnum_by_name(&as->codec_if, AudioCrecord,
	    AudioNsource, NULL);
	ac97_mixer_set_port(&as->codec_if, &ctl);

	return (0);
}

int
ac97_resume(struct ac97_host_if *host_if, struct ac97_codec_if *codec_if)
{
	struct ac97_softc *as = codec_if->as;
	u_int16_t val, extstat;

	host_if->reset(host_if->arg);
	DELAY(1000);

	host_if->write(host_if->arg, AC97_REG_POWER, 0);
	host_if->write(host_if->arg, AC97_REG_RESET, 0);
	DELAY(10000);

	if (as->ext_id & (AC97_EXT_AUDIO_VRA | AC97_EXT_AUDIO_DRA
			  | AC97_EXT_AUDIO_SPDIF | AC97_EXT_AUDIO_VRM
			  | AC97_EXT_AUDIO_CDAC | AC97_EXT_AUDIO_SDAC
			  | AC97_EXT_AUDIO_LDAC)) {

		ac97_read(as, AC97_REG_EXT_AUDIO_CTRL, &extstat);
		extstat &= ~AC97_EXT_AUDIO_DRA;

		if (as->ext_id & AC97_EXT_AUDIO_VRM)
			extstat |= AC97_EXT_AUDIO_VRM;

		if (as->ext_id & AC97_EXT_AUDIO_LDAC)
			extstat |= AC97_EXT_AUDIO_LDAC;
		if (as->ext_id & AC97_EXT_AUDIO_SDAC)
			extstat |= AC97_EXT_AUDIO_SDAC;
		if (as->ext_id & AC97_EXT_AUDIO_CDAC)
			extstat |= AC97_EXT_AUDIO_CDAC;

		if (as->ext_id & AC97_EXT_AUDIO_SPDIF) {
			extstat &= ~AC97_EXT_AUDIO_SPSA_MASK;
			extstat |= AC97_EXT_AUDIO_SPSA34;
			ac97_read(as, AC97_REG_SPDIF_CTRL, &val);
			val = (val & ~AC97_SPDIF_SPSR_MASK) |
			    AC97_SPDIF_SPSR_48K;
			ac97_write(as, AC97_REG_SPDIF_CTRL, val);
		}
		if (as->ext_id & AC97_EXT_AUDIO_VRA)
			extstat |= AC97_EXT_AUDIO_VRA;
		ac97_write(as, AC97_REG_EXT_AUDIO_CTRL, extstat);
	}

	/* use initfunc for specific device */
	if (as->codec_if.initfunc != NULL)
		as->codec_if.initfunc(as, 1);

	return (0);
}

void
ac97_lock(struct ac97_codec_if *codec_if)
{
	struct ac97_softc *as = (struct ac97_softc *)codec_if;
	as->lock_counter++;
}

void
ac97_unlock(struct ac97_codec_if *codec_if)
{
	struct ac97_softc *as = (struct ac97_softc *)codec_if;
	as->lock_counter--;
}

int
ac97_query_devinfo(struct ac97_codec_if *codec_if, mixer_devinfo_t *dip)
{
	struct ac97_softc *as = (struct ac97_softc *)codec_if;

	if (dip->index < as->num_source_info && dip->index >= 0) {
		struct ac97_source_info *si = &as->source_info[dip->index];
		const char *name;

		dip->type = si->type;
		dip->mixer_class = si->mixer_class;
		dip->prev = si->prev;
		dip->next = si->next;

		if (si->qualifier)
			name = si->qualifier;
		else if (si->device)
			name = si->device;
		else if (si->class)
			name = si->class;
		else
			name = NULL;

		if (name)
			strlcpy(dip->label.name, name, sizeof dip->label.name);

		bcopy(si->info, &dip->un, si->info_size);

		/* Set the delta for volume sources */
		if (dip->type == AUDIO_MIXER_VALUE)
			dip->un.v.delta = 1 << (8 - si->bits);

		return (0);
	}

	return (ENXIO);
}

int
ac97_mixer_set_port(struct ac97_codec_if *codec_if, mixer_ctrl_t *cp)
{
	struct ac97_softc *as = (struct ac97_softc *)codec_if;
	struct ac97_source_info *si;
	u_int16_t mask;
	u_int16_t val, newval;
	int error, spdif;

	if (cp->dev < 0 || cp->dev >= as->num_source_info)
		return (EINVAL);

	si = &as->source_info[cp->dev];

	if (cp->type == AUDIO_MIXER_CLASS || cp->type != si->type)
		return (EINVAL);

	spdif = si->req_feature == CHECK_SPDIF &&
	    si->reg == AC97_REG_EXT_AUDIO_CTRL;
	if (spdif && as->lock_counter >= 0)
		return EBUSY;

	ac97_read(as, si->reg, &val);

	DPRINTFN(5, ("read(%x) = %x\n", si->reg, val));

	mask = (1 << si->bits) - 1;

	switch (cp->type) {
	case AUDIO_MIXER_ENUM:
		if (cp->un.ord > mask || cp->un.ord < 0)
			return (EINVAL);

		newval = (cp->un.ord << si->ofs);
		if (si->reg == AC97_REG_RECORD_SELECT) {
			newval |= (newval << (8 + si->ofs));
			mask |= (mask << 8);
			mask = mask << si->ofs;
		} else if (si->reg == AC97_REG_SURR_MASTER) {
			newval = cp->un.ord ? 0x8080 : 0x0000;
			mask = 0x8080;
		} else
			mask = mask << si->ofs;

		if (si->mute) {
			newval |= newval << 8;
			mask |= mask << 8;
		}

		break;
	case AUDIO_MIXER_VALUE:
	{
		const struct audio_mixer_value *value = si->info;
		u_int16_t  l, r;

		if (cp->un.value.num_channels <= 0 ||
		    cp->un.value.num_channels > value->num_channels)
			return (EINVAL);

		if (cp->un.value.num_channels == 1) {
			l = r = cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		} else {
			if (!(as->host_flags & AC97_HOST_SWAPPED_CHANNELS)) {
				l = cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
				r = cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
			} else {
				r = cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
				l = cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
			}
		}

		if (!si->polarity) {
			l = 255 - l;
			r = 255 - r;
		}

		l >>= 8 - si->bits;
		r >>= 8 - si->bits;

		newval = ((l & mask) << si->ofs);
		if (value->num_channels == 2) {
			newval |= ((r & mask) << (si->ofs + 8));
			mask |= (mask << 8);
		}
		mask = mask << si->ofs;
		break;
	}
	default:
		return (EINVAL);
	}

	error = ac97_write(as, si->reg, (val & ~mask) | newval);
	if (error)
		return (error);

	if (spdif && as->host_if->spdif_event != NULL)
		as->host_if->spdif_event(as->host_if->arg, cp->un.ord);

	return (0);
}


int
ac97_set_rate(struct ac97_codec_if *codec_if, int target, u_long *rate)
{
	struct ac97_softc *as;
	u_long value;
	u_int16_t ext_stat;
	u_int16_t actual;
	u_int16_t power;
	u_int16_t power_bit;

	as = (struct ac97_softc *)codec_if;

	if ((target == AC97_REG_PCM_SURR_DAC_RATE) &&
	    !(as->ext_id & AC97_EXT_AUDIO_SDAC))
			return 0;
	if ((target == AC97_REG_PCM_LFE_DAC_RATE) &&
	    !(as->ext_id & AC97_EXT_AUDIO_LDAC))
			return 0;
	if (target == AC97_REG_PCM_MIC_ADC_RATE) {
		if (!(as->ext_id & AC97_EXT_AUDIO_VRM)) {
			*rate = AC97_SINGLE_RATE;
			return 0;
		}
	} else {
		if (!(as->ext_id & AC97_EXT_AUDIO_VRA)) {
			*rate = AC97_SINGLE_RATE;
			return 0;
		}
	}
	if (as->ac97_clock == 0)
		as->ac97_clock = AC97_STANDARD_CLOCK;
	value = *rate * AC97_STANDARD_CLOCK / as->ac97_clock;
	ext_stat = 0;
	/*
	 * PCM_FRONT_DAC_RATE/PCM_SURR_DAC_RATE/PCM_LFE_DAC_RATE
	 *	Check VRA, DRA
	 * PCM_LR_ADC_RATE
	 *	Check VRA
	 * PCM_MIC_ADC_RATE
	 *	Check VRM
	 */
	switch (target) {
	case AC97_REG_PCM_FRONT_DAC_RATE:
	case AC97_REG_PCM_SURR_DAC_RATE:
	case AC97_REG_PCM_LFE_DAC_RATE:
		power_bit = AC97_POWER_OUT;
		if (as->ext_id & AC97_EXT_AUDIO_DRA) {
			ac97_read(as, AC97_REG_EXT_AUDIO_CTRL, &ext_stat);
			if (value > 0x1ffff) {
				return EINVAL;
			} else if (value > 0xffff) {
				/* Enable DRA */
				ext_stat |= AC97_EXT_AUDIO_DRA;
				ac97_write(as, AC97_REG_EXT_AUDIO_CTRL, ext_stat);
				value /= 2;
			} else {
				/* Disable DRA */
				ext_stat &= ~AC97_EXT_AUDIO_DRA;
				ac97_write(as, AC97_REG_EXT_AUDIO_CTRL, ext_stat);
			}
		} else {
			if (value > 0xffff)
				return EINVAL;
		}
		break;
	case AC97_REG_PCM_LR_ADC_RATE:
		power_bit = AC97_POWER_IN;
		if (value > 0xffff)
			return EINVAL;
		break;
	case AC97_REG_PCM_MIC_ADC_RATE:
		power_bit = AC97_POWER_IN;
		if (value > 0xffff)
			return EINVAL;
		break;
	default:
		printf("%s: Unknown register: 0x%x\n", __func__, target);
		return EINVAL;
	}

	ac97_read(as, AC97_REG_POWER, &power);
	ac97_write(as, AC97_REG_POWER, power | power_bit);

	ac97_write(as, target, (u_int16_t)value);
	ac97_read(as, target, &actual);
	actual = (u_int32_t)actual * as->ac97_clock / AC97_STANDARD_CLOCK;

	ac97_write(as, AC97_REG_POWER, power);
	if (ext_stat & AC97_EXT_AUDIO_DRA) {
		*rate = actual * 2;
	} else {
		*rate = actual;
	}
	return 0;
}

void
ac97_set_clock(struct ac97_codec_if *codec_if, unsigned int clock)
{
	struct ac97_softc *as;

	as = (struct ac97_softc *)codec_if;
	as->ac97_clock = clock;
}

u_int16_t
ac97_get_extcaps(struct ac97_codec_if *codec_if)
{
	struct ac97_softc *as;

	as = (struct ac97_softc *)codec_if;
	return as->ext_id;
}

int
ac97_add_port(struct ac97_softc *as, struct ac97_source_info *src)
{
	struct ac97_source_info *si;
	int ouridx, idx;

	if (as->num_source_info >= MAX_SOURCES) {
		printf("%s: internal error: increase MAX_SOURCES in %s\n",
		       __func__, __FILE__);
		return -1;
	}
	if (!ac97_check_capability(as, src->req_feature))
		return -1;
	ouridx = as->num_source_info;
	si = &as->source_info[ouridx];
	memcpy(si, src, sizeof(*si));

	switch (si->type) {
	case AUDIO_MIXER_CLASS:
	case AUDIO_MIXER_VALUE:
		printf("%s: adding class/value is not supported yet.\n",
		       __func__);
		return -1;
	case AUDIO_MIXER_ENUM:
		break;
	default:
		printf("%s: unknown type: %d\n", __func__, si->type);
		return -1;
	}
	as->num_source_info++;

	si->mixer_class = ac97_get_portnum_by_name(&as->codec_if, si->class,
						   NULL, NULL);
	/* Find the root of the device */
	idx = ac97_get_portnum_by_name(&as->codec_if, si->class,
				       si->device, NULL);
	/* Find the last item */
	while (as->source_info[idx].next != AUDIO_MIXER_LAST)
		idx = as->source_info[idx].next;
	/* Append */
	as->source_info[idx].next = ouridx;
	si->prev = idx;
	si->next = AUDIO_MIXER_LAST;

	return 0;
}

int
ac97_get_portnum_by_name(struct ac97_codec_if *codec_if, char *class,
    char *device, char *qualifier)
{
	struct ac97_softc *as = (struct ac97_softc *)codec_if;
	int idx;

	for (idx = 0; idx < as->num_source_info; idx++) {
		struct ac97_source_info *si = &as->source_info[idx];
		if (ac97_str_equal(class, si->class) &&
		    ac97_str_equal(device, si->device) &&
		    ac97_str_equal(qualifier, si->qualifier))
			return (idx);
	}

	return (-1);
}

int
ac97_mixer_get_port(struct ac97_codec_if *codec_if, mixer_ctrl_t *cp)
{
	struct ac97_softc *as = (struct ac97_softc *)codec_if;
	struct ac97_source_info *si;
	u_int16_t mask;
	u_int16_t val;

	if (cp->dev < 0 || cp->dev >= as->num_source_info)
		return (EINVAL);

	si = &as->source_info[cp->dev];

	if (cp->type != si->type)
		return (EINVAL);

	ac97_read(as, si->reg, &val);

	DPRINTFN(5, ("read(%x) = %x\n", si->reg, val));

	mask = (1 << si->bits) - 1;

	switch (cp->type) {
	case AUDIO_MIXER_ENUM:
		cp->un.ord = (val >> si->ofs) & mask;
		DPRINTFN(4, ("AUDIO_MIXER_ENUM: %x %d %x %d\n", val, si->ofs,
		    mask, cp->un.ord));
		break;
	case AUDIO_MIXER_VALUE:
	{
		const struct audio_mixer_value *value = si->info;
		u_int16_t  l, r;

		if ((cp->un.value.num_channels <= 0) ||
		    (cp->un.value.num_channels > value->num_channels))
			return (EINVAL);

		if (value->num_channels == 1)
			l = r = (val >> si->ofs) & mask;
		else {
			if (!(as->host_flags & AC97_HOST_SWAPPED_CHANNELS)) {
				l = (val >> si->ofs) & mask;
				r = (val >> (si->ofs + 8)) & mask;
			} else {
				r = (val >> si->ofs) & mask;
				l = (val >> (si->ofs + 8)) & mask;
			}
		}

		l <<= 8 - si->bits;
		r <<= 8 - si->bits;
		if (!si->polarity) {
			l = 255 - l;
			r = 255 - r;
		}

		/*
		 * The EAP driver averages l and r for stereo
		 * channels that are requested in MONO mode. Does this
		 * make sense?
		 */
		if (cp->un.value.num_channels == 1) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] = l;
		} else if (cp->un.value.num_channels == 2) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] = l;
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = r;
		}

		break;
	}
	default:
		return (EINVAL);
	}

	return (0);
}


/*
 * Codec-dependent initialization
 */

void
ac97_ad1885_init(struct ac97_softc *as, int resuming)
{
	int i;

	if (resuming)
		return;

	for (i = 0; i < as->num_source_info; i++) {
		if (as->source_info[i].reg == AC97_REG_HEADPHONE_VOLUME)
			as->source_info[i].reg = AC97_REG_MASTER_VOLUME;
		else if (as->source_info[i].reg == AC97_REG_MASTER_VOLUME)
			as->source_info[i].reg = AC97_REG_HEADPHONE_VOLUME;
	}
}

#define AC97_AD1886_JACK_SENSE	0x72

void
ac97_ad1886_init(struct ac97_softc *as, int resuming)
{
	ac97_write(as, AC97_AD1886_JACK_SENSE, 0x0010);
}

void
ac97_ad198x_init(struct ac97_softc *as, int resuming)
{
	int i;
	u_int16_t misc;

	ac97_read(as, AC97_AD_REG_MISC, &misc);
	ac97_write(as, AC97_AD_REG_MISC,
	    misc|AC97_AD_MISC_HPSEL|AC97_AD_MISC_LOSEL);

	if (resuming)
		return;

	for (i = 0; i < as->num_source_info; i++) {
		if (as->source_info[i].reg == AC97_REG_SURR_MASTER)
			as->source_info[i].reg = AC97_REG_MASTER_VOLUME;
		else if (as->source_info[i].reg == AC97_REG_MASTER_VOLUME)
			as->source_info[i].reg = AC97_REG_SURR_MASTER;
	}
}

void
ac97_alc650_init(struct ac97_softc *as, int resuming)
{
	u_int16_t misc;

	ac97_read(as, AC97_ALC650_REG_MISC, &misc);
	if (as->host_flags & AC97_HOST_ALC650_PIN47_IS_EAPD)
		misc &= ~AC97_ALC650_MISC_PIN47;
	misc &= ~AC97_ALC650_MISC_VREFDIS;
	ac97_write(as, AC97_ALC650_REG_MISC, misc);

	if (resuming)
		return;

	struct ac97_source_info sources[3] = {
		{ AudioCoutputs, AudioNsurround, "lineinjack",
		  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
		  AC97_ALC650_REG_MULTI_CHANNEL_CONTROL,
		  0x0000, 1, 9, 0, 0, CHECK_SURROUND },
		{ AudioCoutputs, AudioNcenter, "micjack",
		  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
		  AC97_ALC650_REG_MULTI_CHANNEL_CONTROL,
		  0x0000, 1, 10, 0, 0, CHECK_CENTER },
		{ AudioCoutputs, AudioNlfe, "micjack",
		  AUDIO_MIXER_ENUM, WRAP(ac97_on_off),
		  AC97_ALC650_REG_MULTI_CHANNEL_CONTROL,
		  0x0000, 1, 10, 0, 0, CHECK_LFE }};

	ac97_add_port(as, &sources[0]);
	ac97_add_port(as, &sources[1]);
	ac97_add_port(as, &sources[2]);
}

void
ac97_cx20468_init(struct ac97_softc *as, int resuming)
{
	u_int16_t misc;

	ac97_read(as, AC97_CX_REG_MISC, &misc);
	ac97_write(as, AC97_CX_REG_MISC, misc &
	    ~(AC97_CX_SPDIFEN | AC97_CX_COPYRIGHT | AC97_CX_MASK));
}

void
ac97_vt1616_init(struct ac97_softc *as, int resuming)
{
	u_int16_t reg;

	if (as->host_flags & AC97_HOST_VT1616_DYNEX) {
		ac97_read(as, AC97_VT_REG_TEST, &reg);

		/* disable 'hp' mixer controls controlling the surround pins */
		reg &= ~(AC97_VT_LVL);

		/* disable downmixing */
		reg &= ~(AC97_VT_LCTF | AC97_VT_STF);

		/* enable DC offset removal */
		reg |= AC97_VT_BPDC;

		ac97_write(as, AC97_VT_REG_TEST, reg);
	}
}
