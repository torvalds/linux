/*
 * sound/awe_wave.c
 *
 * The low level driver for the AWE32/SB32/AWE64 wave table synth.
 *   version 0.4.4; Jan. 4, 2000
 *
 * Copyright (C) 1996-2000 Takashi Iwai
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Changelog:
 * Aug 18, 2003, Adam Belay <ambx1@neo.rr.com>
 * - detection code rewrite
 */

#include <linux/awe_voice.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/pnp.h>

#include "sound_config.h"

#include "awe_wave.h"
#include "awe_hw.h"

#ifdef AWE_HAS_GUS_COMPATIBILITY
#include "tuning.h"
#include <linux/ultrasound.h>
#endif

/*
 * debug message
 */

#ifdef AWE_DEBUG_ON
#define DEBUG(LVL,XXX)	{if (ctrls[AWE_MD_DEBUG_MODE] > LVL) { XXX; }}
#define ERRMSG(XXX)	{if (ctrls[AWE_MD_DEBUG_MODE]) { XXX; }}
#define FATALERR(XXX)	XXX
#else
#define DEBUG(LVL,XXX) /**/
#define ERRMSG(XXX)	XXX
#define FATALERR(XXX)	XXX
#endif

/*
 * bank and voice record
 */

typedef struct _sf_list sf_list;
typedef struct _awe_voice_list awe_voice_list;
typedef struct _awe_sample_list awe_sample_list;

/* soundfont record */
struct _sf_list {
	unsigned short sf_id;	/* id number */
	unsigned short type;	/* lock & shared flags */
	int num_info;		/* current info table index */
	int num_sample;		/* current sample table index */
	int mem_ptr;		/* current word byte pointer */
	awe_voice_list *infos, *last_infos;	/* instruments */
	awe_sample_list *samples, *last_samples;	/* samples */
#ifdef AWE_ALLOW_SAMPLE_SHARING
	sf_list *shared;	/* shared list */
	unsigned char name[AWE_PATCH_NAME_LEN];	/* sharing id */
#endif
	sf_list *next, *prev;
};

/* instrument list */
struct _awe_voice_list {
	awe_voice_info v;	/* instrument information */
	sf_list *holder;	/* parent sf_list of this record */
	unsigned char bank, instr;	/* preset number information */
	char type, disabled;	/* type=normal/mapped, disabled=boolean */
	awe_voice_list *next;	/* linked list with same sf_id */
	awe_voice_list *next_instr;	/* instrument list */
	awe_voice_list *next_bank;	/* hash table list */
};

/* voice list type */
#define V_ST_NORMAL	0
#define V_ST_MAPPED	1

/* sample list */
struct _awe_sample_list {
	awe_sample_info v;	/* sample information */
	sf_list *holder;	/* parent sf_list of this record */
	awe_sample_list *next;	/* linked list with same sf_id */
};

/* sample and information table */
static int current_sf_id;	/* current number of fonts */
static int locked_sf_id;	/* locked position */
static sf_list *sfhead, *sftail;	/* linked-lists */

#define awe_free_mem_ptr() (sftail ? sftail->mem_ptr : 0)
#define awe_free_info() (sftail ? sftail->num_info : 0)
#define awe_free_sample() (sftail ? sftail->num_sample : 0)

#define AWE_MAX_PRESETS		256
#define AWE_DEFAULT_PRESET	0
#define AWE_DEFAULT_BANK	0
#define AWE_DEFAULT_DRUM	0
#define AWE_DRUM_BANK		128

#define MAX_LAYERS	AWE_MAX_VOICES

/* preset table index */
static awe_voice_list *preset_table[AWE_MAX_PRESETS];

/*
 * voice table
 */

/* effects table */
typedef	struct FX_Rec { /* channel effects */
	unsigned char flags[AWE_FX_END];
	short val[AWE_FX_END];
} FX_Rec;


/* channel parameters */
typedef struct _awe_chan_info {
	int channel;		/* channel number */
	int bank;		/* current tone bank */
	int instr;		/* current program */
	int bender;		/* midi pitchbend (-8192 - 8192) */
	int bender_range;	/* midi bender range (x100) */
	int panning;		/* panning (0-127) */
	int main_vol;		/* channel volume (0-127) */
	int expression_vol;	/* midi expression (0-127) */
	int chan_press;		/* channel pressure */
	int sustained;		/* sustain status in MIDI */
	FX_Rec fx;		/* effects */
	FX_Rec fx_layer[MAX_LAYERS]; /* layer effects */
} awe_chan_info;

/* voice parameters */
typedef struct _voice_info {
	int state;
#define AWE_ST_OFF		(1<<0)	/* no sound */
#define AWE_ST_ON		(1<<1)	/* playing */
#define AWE_ST_STANDBY		(1<<2)	/* stand by for playing */
#define AWE_ST_SUSTAINED	(1<<3)	/* sustained */
#define AWE_ST_MARK		(1<<4)	/* marked for allocation */
#define AWE_ST_DRAM		(1<<5)	/* DRAM read/write */
#define AWE_ST_FM		(1<<6)	/* reserved for FM */
#define AWE_ST_RELEASED		(1<<7)	/* released */

	int ch;			/* midi channel */
	int key;		/* internal key for search */
	int layer;		/* layer number (for channel mode only) */
	int time;		/* allocated time */
	awe_chan_info	*cinfo;	/* channel info */

	int note;		/* midi key (0-127) */
	int velocity;		/* midi velocity (0-127) */
	int sostenuto;		/* sostenuto on/off */
	awe_voice_info *sample;	/* assigned voice */

	/* EMU8000 parameters */
	int apitch;		/* pitch parameter */
	int avol;		/* volume parameter */
	int apan;		/* panning parameter */
	int acutoff;		/* cutoff parameter */
	short aaux;		/* aux word */
} voice_info;

/* voice information */
static voice_info voices[AWE_MAX_VOICES];

#define IS_NO_SOUND(v)	(voices[v].state & (AWE_ST_OFF|AWE_ST_RELEASED|AWE_ST_STANDBY|AWE_ST_SUSTAINED))
#define IS_NO_EFFECT(v)	(voices[v].state != AWE_ST_ON)
#define IS_PLAYING(v)	(voices[v].state & (AWE_ST_ON|AWE_ST_SUSTAINED|AWE_ST_RELEASED))
#define IS_EMPTY(v)	(voices[v].state & (AWE_ST_OFF|AWE_ST_MARK|AWE_ST_DRAM|AWE_ST_FM))


/* MIDI channel effects information (for hw control) */
static awe_chan_info channels[AWE_MAX_CHANNELS];


/*
 * global variables
 */

#ifndef AWE_DEFAULT_BASE_ADDR
#define AWE_DEFAULT_BASE_ADDR	0	/* autodetect */
#endif

#ifndef AWE_DEFAULT_MEM_SIZE
#define AWE_DEFAULT_MEM_SIZE	-1	/* autodetect */
#endif

static int io = AWE_DEFAULT_BASE_ADDR; /* Emu8000 base address */
static int memsize = AWE_DEFAULT_MEM_SIZE; /* memory size in Kbytes */
#ifdef CONFIG_PNP
static int isapnp = -1;
#else
static int isapnp;
#endif

MODULE_AUTHOR("Takashi Iwai <iwai@ww.uni-erlangen.de>");
MODULE_DESCRIPTION("SB AWE32/64 WaveTable driver");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "base i/o port of Emu8000");
module_param(memsize, int, 0);
MODULE_PARM_DESC(memsize, "onboard DRAM size in Kbytes");
module_param(isapnp, bool, 0);
MODULE_PARM_DESC(isapnp, "use ISAPnP detection");

/* DRAM start offset */
static int awe_mem_start = AWE_DRAM_OFFSET;

/* maximum channels for playing */
static int awe_max_voices = AWE_MAX_VOICES;

static int patch_opened;		/* sample already loaded? */

static char atten_relative = FALSE;
static short atten_offset;

static int awe_present = FALSE;		/* awe device present? */
static int awe_busy = FALSE;		/* awe device opened? */

static int my_dev = -1;

#define DEFAULT_DRUM_FLAGS	((1 << 9) | (1 << 25))
#define IS_DRUM_CHANNEL(c)	(drum_flags & (1 << (c)))
#define DRUM_CHANNEL_ON(c)	(drum_flags |= (1 << (c)))
#define DRUM_CHANNEL_OFF(c)	(drum_flags &= ~(1 << (c)))
static unsigned int drum_flags = DEFAULT_DRUM_FLAGS; /* channel flags */

static int playing_mode = AWE_PLAY_INDIRECT;
#define SINGLE_LAYER_MODE()	(playing_mode == AWE_PLAY_INDIRECT || playing_mode == AWE_PLAY_DIRECT)
#define MULTI_LAYER_MODE()	(playing_mode == AWE_PLAY_MULTI || playing_mode == AWE_PLAY_MULTI2)

static int current_alloc_time;  	/* voice allocation index for channel mode */

static struct synth_info awe_info = {
	"AWE32 Synth",		/* name */
	0,			/* device */
	SYNTH_TYPE_SAMPLE,	/* synth_type */
	SAMPLE_TYPE_AWE32,	/* synth_subtype */
	0,			/* perc_mode (obsolete) */
	AWE_MAX_VOICES,		/* nr_voices */
	0,			/* nr_drums (obsolete) */
	400			/* instr_bank_size */
};


static struct voice_alloc_info *voice_alloc;	/* set at initialization */


/*
 * function prototypes
 */

static int awe_request_region(void);
static void awe_release_region(void);

static void awe_reset_samples(void);
/* emu8000 chip i/o access */
static void setup_ports(int p1, int p2, int p3);
static void awe_poke(unsigned short cmd, unsigned short port, unsigned short data);
static void awe_poke_dw(unsigned short cmd, unsigned short port, unsigned int data);
static unsigned short awe_peek(unsigned short cmd, unsigned short port);
static unsigned int awe_peek_dw(unsigned short cmd, unsigned short port);
static void awe_wait(unsigned short delay);

/* initialize emu8000 chip */
static void awe_initialize(void);

/* set voice parameters */
static void awe_init_ctrl_parms(int init_all);
static void awe_init_voice_info(awe_voice_info *vp);
static void awe_init_voice_parm(awe_voice_parm *pp);
#ifdef AWE_HAS_GUS_COMPATIBILITY
static int freq_to_note(int freq);
static int calc_rate_offset(int Hz);
/*static int calc_parm_delay(int msec);*/
static int calc_parm_hold(int msec);
static int calc_parm_attack(int msec);
static int calc_parm_decay(int msec);
static int calc_parm_search(int msec, short *table);
#endif /* gus compat */

/* turn on/off note */
static void awe_note_on(int voice);
static void awe_note_off(int voice);
static void awe_terminate(int voice);
static void awe_exclusive_off(int voice);
static void awe_note_off_all(int do_sustain);

/* calculate voice parameters */
typedef void (*fx_affect_func)(int voice, int forced);
static void awe_set_pitch(int voice, int forced);
static void awe_set_voice_pitch(int voice, int forced);
static void awe_set_volume(int voice, int forced);
static void awe_set_voice_vol(int voice, int forced);
static void awe_set_pan(int voice, int forced);
static void awe_fx_fmmod(int voice, int forced);
static void awe_fx_tremfrq(int voice, int forced);
static void awe_fx_fm2frq2(int voice, int forced);
static void awe_fx_filterQ(int voice, int forced);
static void awe_calc_pitch(int voice);
#ifdef AWE_HAS_GUS_COMPATIBILITY
static void awe_calc_pitch_from_freq(int voice, int freq);
#endif
static void awe_calc_volume(int voice);
static void awe_update_volume(void);
static void awe_change_master_volume(short val);
static void awe_voice_init(int voice, int init_all);
static void awe_channel_init(int ch, int init_all);
static void awe_fx_init(int ch);
static void awe_send_effect(int voice, int layer, int type, int val);
static void awe_modwheel_change(int voice, int value);

/* sequencer interface */
static int awe_open(int dev, int mode);
static void awe_close(int dev);
static int awe_ioctl(int dev, unsigned int cmd, void __user * arg);
static int awe_kill_note(int dev, int voice, int note, int velocity);
static int awe_start_note(int dev, int v, int note_num, int volume);
static int awe_set_instr(int dev, int voice, int instr_no);
static int awe_set_instr_2(int dev, int voice, int instr_no);
static void awe_reset(int dev);
static void awe_hw_control(int dev, unsigned char *event);
static int awe_load_patch(int dev, int format, const char __user *addr,
			  int offs, int count, int pmgr_flag);
static void awe_aftertouch(int dev, int voice, int pressure);
static void awe_controller(int dev, int voice, int ctrl_num, int value);
static void awe_panning(int dev, int voice, int value);
static void awe_volume_method(int dev, int mode);
static void awe_bender(int dev, int voice, int value);
static int awe_alloc(int dev, int chn, int note, struct voice_alloc_info *alloc);
static void awe_setup_voice(int dev, int voice, int chn);

#define awe_key_pressure(dev,voice,key,press) awe_start_note(dev,voice,(key)+128,press)

/* hardware controls */
#ifdef AWE_HAS_GUS_COMPATIBILITY
static void awe_hw_gus_control(int dev, int cmd, unsigned char *event);
#endif
static void awe_hw_awe_control(int dev, int cmd, unsigned char *event);
static void awe_voice_change(int voice, fx_affect_func func);
static void awe_sostenuto_on(int voice, int forced);
static void awe_sustain_off(int voice, int forced);
static void awe_terminate_and_init(int voice, int forced);

/* voice search */
static int awe_search_key(int bank, int preset, int note);
static awe_voice_list *awe_search_instr(int bank, int preset, int note);
static int awe_search_multi_voices(awe_voice_list *rec, int note, int velocity, awe_voice_info **vlist);
static void awe_alloc_multi_voices(int ch, int note, int velocity, int key);
static void awe_alloc_one_voice(int voice, int note, int velocity);
static int awe_clear_voice(void);

/* load / remove patches */
static int awe_open_patch(awe_patch_info *patch, const char __user *addr, int count);
static int awe_close_patch(awe_patch_info *patch, const char __user *addr, int count);
static int awe_unload_patch(awe_patch_info *patch, const char __user *addr, int count);
static int awe_load_info(awe_patch_info *patch, const char __user *addr, int count);
static int awe_remove_info(awe_patch_info *patch, const char __user *addr, int count);
static int awe_load_data(awe_patch_info *patch, const char __user *addr, int count);
static int awe_replace_data(awe_patch_info *patch, const char __user *addr, int count);
static int awe_load_map(awe_patch_info *patch, const char __user *addr, int count);
#ifdef AWE_HAS_GUS_COMPATIBILITY
static int awe_load_guspatch(const char __user *addr, int offs, int size, int pmgr_flag);
#endif
/*static int awe_probe_info(awe_patch_info *patch, const char __user *addr, int count);*/
static int awe_probe_data(awe_patch_info *patch, const char __user *addr, int count);
static sf_list *check_patch_opened(int type, char *name);
static int awe_write_wave_data(const char __user *addr, int offset, awe_sample_list *sp, int channels);
static int awe_create_sf(int type, char *name);
static void awe_free_sf(sf_list *sf);
static void add_sf_info(sf_list *sf, awe_voice_list *rec);
static void add_sf_sample(sf_list *sf, awe_sample_list *smp);
static void purge_old_list(awe_voice_list *rec, awe_voice_list *next);
static void add_info_list(awe_voice_list *rec);
static void awe_remove_samples(int sf_id);
static void rebuild_preset_list(void);
static short awe_set_sample(awe_voice_list *rec);
static awe_sample_list *search_sample_index(sf_list *sf, int sample);

static int is_identical_holder(sf_list *sf1, sf_list *sf2);
#ifdef AWE_ALLOW_SAMPLE_SHARING
static int is_identical_name(unsigned char *name, sf_list *p);
static int is_shared_sf(unsigned char *name);
static int info_duplicated(sf_list *sf, awe_voice_list *rec);
#endif /* allow sharing */

/* lowlevel functions */
static void awe_init_audio(void);
static void awe_init_dma(void);
static void awe_init_array(void);
static void awe_send_array(unsigned short *data);
static void awe_tweak_voice(int voice);
static void awe_tweak(void);
static void awe_init_fm(void);
static int awe_open_dram_for_write(int offset, int channels);
static void awe_open_dram_for_check(void);
static void awe_close_dram(void);
/*static void awe_write_dram(unsigned short c);*/
static int awe_detect_base(int addr);
static int awe_detect(void);
static void awe_check_dram(void);
static int awe_load_chorus_fx(awe_patch_info *patch, const char __user *addr, int count);
static void awe_set_chorus_mode(int mode);
static void awe_update_chorus_mode(void);
static int awe_load_reverb_fx(awe_patch_info *patch, const char __user *addr, int count);
static void awe_set_reverb_mode(int mode);
static void awe_update_reverb_mode(void);
static void awe_equalizer(int bass, int treble);
static void awe_update_equalizer(void);

#ifdef CONFIG_AWE32_MIXER
static void attach_mixer(void);
static void unload_mixer(void);
#endif

#ifdef CONFIG_AWE32_MIDIEMU
static void attach_midiemu(void);
static void unload_midiemu(void);
#endif

#define limitvalue(x, a, b) if ((x) < (a)) (x) = (a); else if ((x) > (b)) (x) = (b)

/*
 * control parameters
 */


#ifdef AWE_USE_NEW_VOLUME_CALC
#define DEF_VOLUME_CALC	TRUE
#else
#define DEF_VOLUME_CALC	FALSE
#endif /* new volume */

#define DEF_ZERO_ATTEN		32	/* 12dB below */
#define DEF_MOD_SENSE		18
#define DEF_CHORUS_MODE		2
#define DEF_REVERB_MODE		4
#define DEF_BASS_LEVEL		5
#define DEF_TREBLE_LEVEL	9

static struct CtrlParmsDef {
	int value;
	int init_each_time;
	void (*update)(void);
} ctrl_parms[AWE_MD_END] = {
	{0,0, NULL}, {0,0, NULL}, /* <-- not used */
	{AWE_VERSION_NUMBER, FALSE, NULL},
	{TRUE, FALSE, NULL}, /* exclusive */
	{TRUE, FALSE, NULL}, /* realpan */
	{AWE_DEFAULT_BANK, FALSE, NULL}, /* gusbank */
	{FALSE, TRUE, NULL}, /* keep effect */
	{DEF_ZERO_ATTEN, FALSE, awe_update_volume}, /* zero_atten */
	{FALSE, FALSE, NULL}, /* chn_prior */
	{DEF_MOD_SENSE, FALSE, NULL}, /* modwheel sense */
	{AWE_DEFAULT_PRESET, FALSE, NULL}, /* def_preset */
	{AWE_DEFAULT_BANK, FALSE, NULL}, /* def_bank */
	{AWE_DEFAULT_DRUM, FALSE, NULL}, /* def_drum */
	{FALSE, FALSE, NULL}, /* toggle_drum_bank */
	{DEF_VOLUME_CALC, FALSE, awe_update_volume}, /* new_volume_calc */
	{DEF_CHORUS_MODE, FALSE, awe_update_chorus_mode}, /* chorus mode */
	{DEF_REVERB_MODE, FALSE, awe_update_reverb_mode}, /* reverb mode */
	{DEF_BASS_LEVEL, FALSE, awe_update_equalizer}, /* bass level */
	{DEF_TREBLE_LEVEL, FALSE, awe_update_equalizer}, /* treble level */
	{0, FALSE, NULL},	/* debug mode */
	{FALSE, FALSE, NULL}, /* pan exchange */
};

static int ctrls[AWE_MD_END];


/*
 * synth operation table
 */

static struct synth_operations awe_operations =
{
	.owner		= THIS_MODULE,
	.id		= "EMU8K",
	.info		= &awe_info,
	.midi_dev	= 0,
	.synth_type	= SYNTH_TYPE_SAMPLE,
	.synth_subtype	= SAMPLE_TYPE_AWE32,
	.open		= awe_open,
	.close		= awe_close,
	.ioctl		= awe_ioctl,
	.kill_note	= awe_kill_note,
	.start_note	= awe_start_note,
	.set_instr	= awe_set_instr_2,
	.reset		= awe_reset,
	.hw_control	= awe_hw_control,
	.load_patch	= awe_load_patch,
	.aftertouch	= awe_aftertouch,
	.controller	= awe_controller,
	.panning	= awe_panning,
	.volume_method	= awe_volume_method,
	.bender		= awe_bender,
	.alloc_voice	= awe_alloc,
	.setup_voice	= awe_setup_voice
};

static void free_tables(void)
{
	if (sftail) {
		sf_list *p, *prev;
		for (p = sftail; p; p = prev) {
			prev = p->prev;
			awe_free_sf(p);
		}
	}
	sfhead = sftail = NULL;
}

/*
 * clear sample tables 
 */

static void
awe_reset_samples(void)
{
	/* free all bank tables */
	memset(preset_table, 0, sizeof(preset_table));
	free_tables();

	current_sf_id = 0;
	locked_sf_id = 0;
	patch_opened = 0;
}


/*
 * EMU register access
 */

/* select a given AWE32 pointer */
static int awe_ports[5];
static int port_setuped = FALSE;
static int awe_cur_cmd = -1;
#define awe_set_cmd(cmd) \
if (awe_cur_cmd != cmd) { outw(cmd, awe_ports[Pointer]); awe_cur_cmd = cmd; }

/* write 16bit data */
static void
awe_poke(unsigned short cmd, unsigned short port, unsigned short data)
{
	awe_set_cmd(cmd);
	outw(data, awe_ports[port]);
}

/* write 32bit data */
static void
awe_poke_dw(unsigned short cmd, unsigned short port, unsigned int data)
{
	unsigned short addr = awe_ports[port];
	awe_set_cmd(cmd);
	outw(data, addr);		/* write lower 16 bits */
	outw(data >> 16, addr + 2);	/* write higher 16 bits */
}

/* read 16bit data */
static unsigned short
awe_peek(unsigned short cmd, unsigned short port)
{
	unsigned short k;
	awe_set_cmd(cmd);
	k = inw(awe_ports[port]);
	return k;
}

/* read 32bit data */
static unsigned int
awe_peek_dw(unsigned short cmd, unsigned short port)
{
	unsigned int k1, k2;
	unsigned short addr = awe_ports[port];
	awe_set_cmd(cmd);
	k1 = inw(addr);
	k2 = inw(addr + 2);
	k1 |= k2 << 16;
	return k1;
}

/* wait delay number of AWE32 44100Hz clocks */
#ifdef WAIT_BY_LOOP /* wait by loop -- that's not good.. */
static void
awe_wait(unsigned short delay)
{
	unsigned short clock, target;
	unsigned short port = awe_ports[AWE_WC_Port];
	int counter;
  
	/* sample counter */
	awe_set_cmd(AWE_WC_Cmd);
	clock = (unsigned short)inw(port);
	target = clock + delay;
	counter = 0;
	if (target < clock) {
		for (; (unsigned short)inw(port) > target; counter++)
			if (counter > 65536)
				break;
	}
	for (; (unsigned short)inw(port) < target; counter++)
		if (counter > 65536)
			break;
}
#else

static void awe_wait(unsigned short delay)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((HZ*(unsigned long)delay + 44099)/44100);
}
/*
static void awe_wait(unsigned short delay)
{
	udelay(((unsigned long)delay * 1000000L + 44099) / 44100);
}
*/
#endif /* wait by loop */

/* write a word data */
#define awe_write_dram(c)	awe_poke(AWE_SMLD, c)

/*
 * AWE32 voice parameters
 */

/* initialize voice_info record */
static void
awe_init_voice_info(awe_voice_info *vp)
{
	vp->sample = 0;
	vp->rate_offset = 0;

	vp->start = 0;
	vp->end = 0;
	vp->loopstart = 0;
	vp->loopend = 0;
	vp->mode = 0;
	vp->root = 60;
	vp->tune = 0;
	vp->low = 0;
	vp->high = 127;
	vp->vellow = 0;
	vp->velhigh = 127;

	vp->fixkey = -1;
	vp->fixvel = -1;
	vp->fixpan = -1;
	vp->pan = -1;

	vp->exclusiveClass = 0;
	vp->amplitude = 127;
	vp->attenuation = 0;
	vp->scaleTuning = 100;

	awe_init_voice_parm(&vp->parm);
}

/* initialize voice_parm record:
 * Env1/2: delay=0, attack=0, hold=0, sustain=0, decay=0, release=0.
 * Vibrato and Tremolo effects are zero.
 * Cutoff is maximum.
 * Chorus and Reverb effects are zero.
 */
static void
awe_init_voice_parm(awe_voice_parm *pp)
{
	pp->moddelay = 0x8000;
	pp->modatkhld = 0x7f7f;
	pp->moddcysus = 0x7f7f;
	pp->modrelease = 0x807f;
	pp->modkeyhold = 0;
	pp->modkeydecay = 0;

	pp->voldelay = 0x8000;
	pp->volatkhld = 0x7f7f;
	pp->voldcysus = 0x7f7f;
	pp->volrelease = 0x807f;
	pp->volkeyhold = 0;
	pp->volkeydecay = 0;

	pp->lfo1delay = 0x8000;
	pp->lfo2delay = 0x8000;
	pp->pefe = 0;

	pp->fmmod = 0;
	pp->tremfrq = 0;
	pp->fm2frq2 = 0;

	pp->cutoff = 0xff;
	pp->filterQ = 0;

	pp->chorus = 0;
	pp->reverb = 0;
}	


#ifdef AWE_HAS_GUS_COMPATIBILITY

/* convert frequency mHz to abstract cents (= midi key * 100) */
static int
freq_to_note(int mHz)
{
	/* abscents = log(mHz/8176) / log(2) * 1200 */
	unsigned int max_val = (unsigned int)0xffffffff / 10000;
	int i, times;
	unsigned int base;
	unsigned int freq;
	int note, tune;

	if (mHz == 0)
		return 0;
	if (mHz < 0)
		return 12799; /* maximum */

	freq = mHz;
	note = 0;
	for (base = 8176 * 2; freq >= base; base *= 2) {
		note += 12;
		if (note >= 128) /* over maximum */
			return 12799;
	}
	base /= 2;

	/* to avoid overflow... */
	times = 10000;
	while (freq > max_val) {
		max_val *= 10;
		times /= 10;
		base /= 10;
	}

	freq = freq * times / base;
	for (i = 0; i < 12; i++) {
		if (freq < semitone_tuning[i+1])
			break;
		note++;
	}

	tune = 0;
	freq = freq * 10000 / semitone_tuning[i];
	for (i = 0; i < 100; i++) {
		if (freq < cent_tuning[i+1])
			break;
		tune++;
	}

	return note * 100 + tune;
}


/* convert Hz to AWE32 rate offset:
 * sample pitch offset for the specified sample rate
 * rate=44100 is no offset, each 4096 is 1 octave (twice).
 * eg, when rate is 22050, this offset becomes -4096.
 */
static int
calc_rate_offset(int Hz)
{
	/* offset = log(Hz / 44100) / log(2) * 4096 */
	int freq, base, i;

	/* maybe smaller than max (44100Hz) */
	if (Hz <= 0 || Hz >= 44100) return 0;

	base = 0;
	for (freq = Hz * 2; freq < 44100; freq *= 2)
		base++;
	base *= 1200;

	freq = 44100 * 10000 / (freq/2);
	for (i = 0; i < 12; i++) {
		if (freq < semitone_tuning[i+1])
			break;
		base += 100;
	}
	freq = freq * 10000 / semitone_tuning[i];
	for (i = 0; i < 100; i++) {
		if (freq < cent_tuning[i+1])
			break;
		base++;
	}
	return -base * 4096 / 1200;
}


/*
 * convert envelope time parameter to AWE32 raw parameter
 */

/* attack & decay/release time table (msec) */
static short attack_time_tbl[128] = {
32767, 32767, 5989, 4235, 2994, 2518, 2117, 1780, 1497, 1373, 1259, 1154, 1058, 970, 890, 816,
707, 691, 662, 634, 607, 581, 557, 533, 510, 489, 468, 448, 429, 411, 393, 377,
361, 345, 331, 317, 303, 290, 278, 266, 255, 244, 234, 224, 214, 205, 196, 188,
180, 172, 165, 158, 151, 145, 139, 133, 127, 122, 117, 112, 107, 102, 98, 94,
90, 86, 82, 79, 75, 72, 69, 66, 63, 61, 58, 56, 53, 51, 49, 47,
45, 43, 41, 39, 37, 36, 34, 33, 31, 30, 29, 28, 26, 25, 24, 23,
22, 21, 20, 19, 19, 18, 17, 16, 16, 15, 15, 14, 13, 13, 12, 12,
11, 11, 10, 10, 10, 9, 9, 8, 8, 8, 8, 7, 7, 7, 6, 0,
};

static short decay_time_tbl[128] = {
32767, 32767, 22614, 15990, 11307, 9508, 7995, 6723, 5653, 5184, 4754, 4359, 3997, 3665, 3361, 3082,
2828, 2765, 2648, 2535, 2428, 2325, 2226, 2132, 2042, 1955, 1872, 1793, 1717, 1644, 1574, 1507,
1443, 1382, 1324, 1267, 1214, 1162, 1113, 1066, 978, 936, 897, 859, 822, 787, 754, 722,
691, 662, 634, 607, 581, 557, 533, 510, 489, 468, 448, 429, 411, 393, 377, 361,
345, 331, 317, 303, 290, 278, 266, 255, 244, 234, 224, 214, 205, 196, 188, 180,
172, 165, 158, 151, 145, 139, 133, 127, 122, 117, 112, 107, 102, 98, 94, 90,
86, 82, 79, 75, 72, 69, 66, 63, 61, 58, 56, 53, 51, 49, 47, 45,
43, 41, 39, 37, 36, 34, 33, 31, 30, 29, 28, 26, 25, 24, 23, 22,
};

#define calc_parm_delay(msec) (0x8000 - (msec) * 1000 / 725);

/* delay time = 0x8000 - msec/92 */
static int
calc_parm_hold(int msec)
{
	int val = (0x7f * 92 - msec) / 92;
	if (val < 1) val = 1;
	if (val > 127) val = 127;
	return val;
}

/* attack time: search from time table */
static int
calc_parm_attack(int msec)
{
	return calc_parm_search(msec, attack_time_tbl);
}

/* decay/release time: search from time table */
static int
calc_parm_decay(int msec)
{
	return calc_parm_search(msec, decay_time_tbl);
}

/* search an index for specified time from given time table */
static int
calc_parm_search(int msec, short *table)
{
	int left = 1, right = 127, mid;
	while (left < right) {
		mid = (left + right) / 2;
		if (msec < (int)table[mid])
			left = mid + 1;
		else
			right = mid;
	}
	return left;
}
#endif /* AWE_HAS_GUS_COMPATIBILITY */


/*
 * effects table
 */

/* set an effect value */
#define FX_FLAG_OFF	0
#define FX_FLAG_SET	1
#define FX_FLAG_ADD	2

#define FX_SET(rec,type,value) \
	((rec)->flags[type] = FX_FLAG_SET, (rec)->val[type] = (value))
#define FX_ADD(rec,type,value) \
	((rec)->flags[type] = FX_FLAG_ADD, (rec)->val[type] = (value))
#define FX_UNSET(rec,type) \
	((rec)->flags[type] = FX_FLAG_OFF, (rec)->val[type] = 0)

/* check the effect value is set */
#define FX_ON(rec,type)	((rec)->flags[type])

#define PARM_BYTE	0
#define PARM_WORD	1
#define PARM_SIGN	2

static struct PARM_DEFS {
	int type;	/* byte or word */
	int low, high;	/* value range */
	fx_affect_func realtime;	/* realtime paramater change */
} parm_defs[] = {
	{PARM_WORD, 0, 0x8000, NULL},	/* env1 delay */
	{PARM_BYTE, 1, 0x7f, NULL},	/* env1 attack */
	{PARM_BYTE, 0, 0x7e, NULL},	/* env1 hold */
	{PARM_BYTE, 1, 0x7f, NULL},	/* env1 decay */
	{PARM_BYTE, 1, 0x7f, NULL},	/* env1 release */
	{PARM_BYTE, 0, 0x7f, NULL},	/* env1 sustain */
	{PARM_BYTE, 0, 0xff, NULL},	/* env1 pitch */
	{PARM_BYTE, 0, 0xff, NULL},	/* env1 cutoff */

	{PARM_WORD, 0, 0x8000, NULL},	/* env2 delay */
	{PARM_BYTE, 1, 0x7f, NULL},	/* env2 attack */
	{PARM_BYTE, 0, 0x7e, NULL},	/* env2 hold */
	{PARM_BYTE, 1, 0x7f, NULL},	/* env2 decay */
	{PARM_BYTE, 1, 0x7f, NULL},	/* env2 release */
	{PARM_BYTE, 0, 0x7f, NULL},	/* env2 sustain */

	{PARM_WORD, 0, 0x8000, NULL},	/* lfo1 delay */
	{PARM_BYTE, 0, 0xff, awe_fx_tremfrq},	/* lfo1 freq */
	{PARM_SIGN, -128, 127, awe_fx_tremfrq},	/* lfo1 volume */
	{PARM_SIGN, -128, 127, awe_fx_fmmod},	/* lfo1 pitch */
	{PARM_BYTE, 0, 0xff, awe_fx_fmmod},	/* lfo1 cutoff */

	{PARM_WORD, 0, 0x8000, NULL},	/* lfo2 delay */
	{PARM_BYTE, 0, 0xff, awe_fx_fm2frq2},	/* lfo2 freq */
	{PARM_SIGN, -128, 127, awe_fx_fm2frq2},	/* lfo2 pitch */

	{PARM_WORD, 0, 0xffff, awe_set_voice_pitch},	/* initial pitch */
	{PARM_BYTE, 0, 0xff, NULL},	/* chorus */
	{PARM_BYTE, 0, 0xff, NULL},	/* reverb */
	{PARM_BYTE, 0, 0xff, awe_set_volume},	/* initial cutoff */
	{PARM_BYTE, 0, 15, awe_fx_filterQ},	/* initial resonance */

	{PARM_WORD, 0, 0xffff, NULL},	/* sample start */
	{PARM_WORD, 0, 0xffff, NULL},	/* loop start */
	{PARM_WORD, 0, 0xffff, NULL},	/* loop end */
	{PARM_WORD, 0, 0xffff, NULL},	/* coarse sample start */
	{PARM_WORD, 0, 0xffff, NULL},	/* coarse loop start */
	{PARM_WORD, 0, 0xffff, NULL},	/* coarse loop end */
	{PARM_BYTE, 0, 0xff, awe_set_volume},	/* initial attenuation */
};


static unsigned char
FX_BYTE(FX_Rec *rec, FX_Rec *lay, int type, unsigned char value)
{
	int effect = 0;
	int on = 0;
	if (lay && (on = FX_ON(lay, type)) != 0)
		effect = lay->val[type];
	if (!on && (on = FX_ON(rec, type)) != 0)
		effect = rec->val[type];
	if (on == FX_FLAG_ADD) {
		if (parm_defs[type].type == PARM_SIGN) {
			if (value > 0x7f)
				effect += (int)value - 0x100;
			else
				effect += (int)value;
		} else {
			effect += (int)value;
		}
	}
	if (on) {
		if (effect < parm_defs[type].low)
			effect = parm_defs[type].low;
		else if (effect > parm_defs[type].high)
			effect = parm_defs[type].high;
		return (unsigned char)effect;
	}
	return value;
}

/* get word effect value */
static unsigned short
FX_WORD(FX_Rec *rec, FX_Rec *lay, int type, unsigned short value)
{
	int effect = 0;
	int on = 0;
	if (lay && (on = FX_ON(lay, type)) != 0)
		effect = lay->val[type];
	if (!on && (on = FX_ON(rec, type)) != 0)
		effect = rec->val[type];
	if (on == FX_FLAG_ADD)
		effect += (int)value;
	if (on) {
		if (effect < parm_defs[type].low)
			effect = parm_defs[type].low;
		else if (effect > parm_defs[type].high)
			effect = parm_defs[type].high;
		return (unsigned short)effect;
	}
	return value;
}

/* get word (upper=type1/lower=type2) effect value */
static unsigned short
FX_COMB(FX_Rec *rec, FX_Rec *lay, int type1, int type2, unsigned short value)
{
	unsigned short tmp;
	tmp = FX_BYTE(rec, lay, type1, (unsigned char)(value >> 8));
	tmp <<= 8;
	tmp |= FX_BYTE(rec, lay, type2, (unsigned char)(value & 0xff));
	return tmp;
}

/* address offset */
static int
FX_OFFSET(FX_Rec *rec, FX_Rec *lay, int lo, int hi, int mode)
{
	int addr = 0;
	if (lay && FX_ON(lay, hi))
		addr = (short)lay->val[hi];
	else if (FX_ON(rec, hi))
		addr = (short)rec->val[hi];
	addr = addr << 15;
	if (lay && FX_ON(lay, lo))
		addr += (short)lay->val[lo];
	else if (FX_ON(rec, lo))
		addr += (short)rec->val[lo];
	if (!(mode & AWE_SAMPLE_8BITS))
		addr /= 2;
	return addr;
}


/*
 * turn on/off sample
 */

/* table for volume target calculation */
static unsigned short voltarget[16] = { 
   0xEAC0, 0XE0C8, 0XD740, 0XCE20, 0XC560, 0XBD08, 0XB500, 0XAD58,
   0XA5F8, 0X9EF0, 0X9830, 0X91C0, 0X8B90, 0X85A8, 0X8000, 0X7A90
};

static void
awe_note_on(int voice)
{
	unsigned int temp;
	int addr;
	int vtarget, ftarget, ptarget, pitch;
	awe_voice_info *vp;
	awe_voice_parm_block *parm;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	/* A voice sample must assigned before calling */
	if ((vp = voices[voice].sample) == NULL || vp->index == 0)
		return;

	parm = (awe_voice_parm_block*)&vp->parm;

	/* channel to be silent and idle */
	awe_poke(AWE_DCYSUSV(voice), 0x0080);
	awe_poke(AWE_VTFT(voice), 0x0000FFFF);
	awe_poke(AWE_CVCF(voice), 0x0000FFFF);
	awe_poke(AWE_PTRX(voice), 0);
	awe_poke(AWE_CPF(voice), 0);

	/* set pitch offset */
	awe_set_pitch(voice, TRUE);

	/* modulation & volume envelope */
	if (parm->modatk >= 0x80 && parm->moddelay >= 0x8000) {
		awe_poke(AWE_ENVVAL(voice), 0xBFFF);
		pitch = (parm->env1pit<<4) + voices[voice].apitch;
		if (pitch > 0xffff) pitch = 0xffff;
		/* calculate filter target */
		ftarget = parm->cutoff + parm->env1fc;
		limitvalue(ftarget, 0, 255);
		ftarget <<= 8;
	} else {
		awe_poke(AWE_ENVVAL(voice),
			 FX_WORD(fx, fx_lay, AWE_FX_ENV1_DELAY, parm->moddelay));
		ftarget = parm->cutoff;
		ftarget <<= 8;
		pitch = voices[voice].apitch;
	}

	/* calcualte pitch target */
	if (pitch != 0xffff) {
		ptarget = 1 << (pitch >> 12);
		if (pitch & 0x800) ptarget += (ptarget*0x102e)/0x2710;
		if (pitch & 0x400) ptarget += (ptarget*0x764)/0x2710;
		if (pitch & 0x200) ptarget += (ptarget*0x389)/0x2710;
		ptarget += (ptarget>>1);
		if (ptarget > 0xffff) ptarget = 0xffff;

	} else ptarget = 0xffff;
	if (parm->modatk >= 0x80)
		awe_poke(AWE_ATKHLD(voice),
			 FX_BYTE(fx, fx_lay, AWE_FX_ENV1_HOLD, parm->modhld) << 8 | 0x7f);
	else
		awe_poke(AWE_ATKHLD(voice),
			 FX_COMB(fx, fx_lay, AWE_FX_ENV1_HOLD, AWE_FX_ENV1_ATTACK,
				 vp->parm.modatkhld));
	awe_poke(AWE_DCYSUS(voice),
		 FX_COMB(fx, fx_lay, AWE_FX_ENV1_SUSTAIN, AWE_FX_ENV1_DECAY,
			  vp->parm.moddcysus));

	if (parm->volatk >= 0x80 && parm->voldelay >= 0x8000) {
		awe_poke(AWE_ENVVOL(voice), 0xBFFF);
		vtarget = voltarget[voices[voice].avol%0x10]>>(voices[voice].avol>>4);
	} else {
		awe_poke(AWE_ENVVOL(voice),
			 FX_WORD(fx, fx_lay, AWE_FX_ENV2_DELAY, vp->parm.voldelay));
		vtarget = 0;
	}
	if (parm->volatk >= 0x80)
		awe_poke(AWE_ATKHLDV(voice),
			 FX_BYTE(fx, fx_lay, AWE_FX_ENV2_HOLD, parm->volhld) << 8 | 0x7f);
	else
		awe_poke(AWE_ATKHLDV(voice),
			 FX_COMB(fx, fx_lay, AWE_FX_ENV2_HOLD, AWE_FX_ENV2_ATTACK,
			 vp->parm.volatkhld));
	/* decay/sustain parameter for volume envelope must be set at last */

	/* cutoff and volume */
	awe_set_volume(voice, TRUE);

	/* modulation envelope heights */
	awe_poke(AWE_PEFE(voice),
		 FX_COMB(fx, fx_lay, AWE_FX_ENV1_PITCH, AWE_FX_ENV1_CUTOFF,
			 vp->parm.pefe));

	/* lfo1/2 delay */
	awe_poke(AWE_LFO1VAL(voice),
		 FX_WORD(fx, fx_lay, AWE_FX_LFO1_DELAY, vp->parm.lfo1delay));
	awe_poke(AWE_LFO2VAL(voice),
		 FX_WORD(fx, fx_lay, AWE_FX_LFO2_DELAY, vp->parm.lfo2delay));

	/* lfo1 pitch & cutoff shift */
	awe_fx_fmmod(voice, TRUE);
	/* lfo1 volume & freq */
	awe_fx_tremfrq(voice, TRUE);
	/* lfo2 pitch & freq */
	awe_fx_fm2frq2(voice, TRUE);
	/* pan & loop start */
	awe_set_pan(voice, TRUE);

	/* chorus & loop end (chorus 8bit, MSB) */
	addr = vp->loopend - 1;
	addr += FX_OFFSET(fx, fx_lay, AWE_FX_LOOP_END,
			  AWE_FX_COARSE_LOOP_END, vp->mode);
	temp = FX_BYTE(fx, fx_lay, AWE_FX_CHORUS, vp->parm.chorus);
	temp = (temp <<24) | (unsigned int)addr;
	awe_poke_dw(AWE_CSL(voice), temp);
	DEBUG(4,printk("AWE32: [-- loopend=%x/%x]\n", vp->loopend, addr));

	/* Q & current address (Q 4bit value, MSB) */
	addr = vp->start - 1;
	addr += FX_OFFSET(fx, fx_lay, AWE_FX_SAMPLE_START,
			  AWE_FX_COARSE_SAMPLE_START, vp->mode);
	temp = FX_BYTE(fx, fx_lay, AWE_FX_FILTERQ, vp->parm.filterQ);
	temp = (temp<<28) | (unsigned int)addr;
	awe_poke_dw(AWE_CCCA(voice), temp);
	DEBUG(4,printk("AWE32: [-- startaddr=%x/%x]\n", vp->start, addr));

	/* clear unknown registers */
	awe_poke_dw(AWE_00A0(voice), 0);
	awe_poke_dw(AWE_0080(voice), 0);

	/* reset volume */
	awe_poke_dw(AWE_VTFT(voice), (vtarget<<16)|ftarget);
	awe_poke_dw(AWE_CVCF(voice), (vtarget<<16)|ftarget);

	/* set reverb */
	temp = FX_BYTE(fx, fx_lay, AWE_FX_REVERB, vp->parm.reverb);
	temp = (temp << 8) | (ptarget << 16) | voices[voice].aaux;
	awe_poke_dw(AWE_PTRX(voice), temp);
	awe_poke_dw(AWE_CPF(voice), ptarget << 16);
	/* turn on envelope */
	awe_poke(AWE_DCYSUSV(voice),
		 FX_COMB(fx, fx_lay, AWE_FX_ENV2_SUSTAIN, AWE_FX_ENV2_DECAY,
			  vp->parm.voldcysus));

	voices[voice].state = AWE_ST_ON;

	/* clear voice position for the next note on this channel */
	if (SINGLE_LAYER_MODE()) {
		FX_UNSET(fx, AWE_FX_SAMPLE_START);
		FX_UNSET(fx, AWE_FX_COARSE_SAMPLE_START);
	}
}


/* turn off the voice */
static void
awe_note_off(int voice)
{
	awe_voice_info *vp;
	unsigned short tmp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	if ((vp = voices[voice].sample) == NULL) {
		voices[voice].state = AWE_ST_OFF;
		return;
	}

	tmp = 0x8000 | FX_BYTE(fx, fx_lay, AWE_FX_ENV1_RELEASE,
			       (unsigned char)vp->parm.modrelease);
	awe_poke(AWE_DCYSUS(voice), tmp);
	tmp = 0x8000 | FX_BYTE(fx, fx_lay, AWE_FX_ENV2_RELEASE,
			       (unsigned char)vp->parm.volrelease);
	awe_poke(AWE_DCYSUSV(voice), tmp);
	voices[voice].state = AWE_ST_RELEASED;
}

/* force to terminate the voice (no releasing echo) */
static void
awe_terminate(int voice)
{
	awe_poke(AWE_DCYSUSV(voice), 0x807F);
	awe_tweak_voice(voice);
	voices[voice].state = AWE_ST_OFF;
}

/* turn off other voices with the same exclusive class (for drums) */
static void
awe_exclusive_off(int voice)
{
	int i, exclass;

	if (voices[voice].sample == NULL)
		return;
	if ((exclass = voices[voice].sample->exclusiveClass) == 0)
		return;	/* not exclusive */

	/* turn off voices with the same class */
	for (i = 0; i < awe_max_voices; i++) {
		if (i != voice && IS_PLAYING(i) &&
		    voices[i].sample && voices[i].ch == voices[voice].ch &&
		    voices[i].sample->exclusiveClass == exclass) {
			DEBUG(4,printk("AWE32: [exoff(%d)]\n", i));
			awe_terminate(i);
			awe_voice_init(i, TRUE);
		}
	}
}


/*
 * change the parameters of an audible voice
 */

/* change pitch */
static void
awe_set_pitch(int voice, int forced)
{
	if (IS_NO_EFFECT(voice) && !forced) return;
	awe_poke(AWE_IP(voice), voices[voice].apitch);
	DEBUG(3,printk("AWE32: [-- pitch=%x]\n", voices[voice].apitch));
}

/* calculate & change pitch */
static void
awe_set_voice_pitch(int voice, int forced)
{
	awe_calc_pitch(voice);
	awe_set_pitch(voice, forced);
}

/* change volume & cutoff */
static void
awe_set_volume(int voice, int forced)
{
	awe_voice_info *vp;
	unsigned short tmp2;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	if (!IS_PLAYING(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index == 0)
		return;

	tmp2 = FX_BYTE(fx, fx_lay, AWE_FX_CUTOFF,
		       (unsigned char)voices[voice].acutoff);
	tmp2 = (tmp2 << 8);
	tmp2 |= FX_BYTE(fx, fx_lay, AWE_FX_ATTEN,
			(unsigned char)voices[voice].avol);
	awe_poke(AWE_IFATN(voice), tmp2);
}

/* calculate & change volume */
static void
awe_set_voice_vol(int voice, int forced)
{
	if (IS_EMPTY(voice))
		return;
	awe_calc_volume(voice);
	awe_set_volume(voice, forced);
}


/* change pan; this could make a click noise.. */
static void
awe_set_pan(int voice, int forced)
{
	unsigned int temp;
	int addr;
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index == 0)
		return;

	/* pan & loop start (pan 8bit, MSB, 0:right, 0xff:left) */
	if (vp->fixpan > 0)	/* 0-127 */
		temp = 255 - (int)vp->fixpan * 2;
	else {
		int pos = 0;
		if (vp->pan >= 0) /* 0-127 */
			pos = (int)vp->pan * 2 - 128;
		pos += voices[voice].cinfo->panning; /* -128 - 127 */
		temp = 127 - pos;
	}
	limitvalue(temp, 0, 255);
	if (ctrls[AWE_MD_PAN_EXCHANGE]) {
		temp = 255 - temp;
	}
	if (forced || temp != voices[voice].apan) {
		voices[voice].apan = temp;
		if (temp == 0)
			voices[voice].aaux = 0xff;
		else
			voices[voice].aaux = (-temp) & 0xff;
		addr = vp->loopstart - 1;
		addr += FX_OFFSET(fx, fx_lay, AWE_FX_LOOP_START,
				  AWE_FX_COARSE_LOOP_START, vp->mode);
		temp = (temp<<24) | (unsigned int)addr;
		awe_poke_dw(AWE_PSST(voice), temp);
		DEBUG(4,printk("AWE32: [-- loopstart=%x/%x]\n", vp->loopstart, addr));
	}
}

/* effects change during playing */
static void
awe_fx_fmmod(int voice, int forced)
{
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index == 0)
		return;
	awe_poke(AWE_FMMOD(voice),
		 FX_COMB(fx, fx_lay, AWE_FX_LFO1_PITCH, AWE_FX_LFO1_CUTOFF,
			 vp->parm.fmmod));
}

/* set tremolo (lfo1) volume & frequency */
static void
awe_fx_tremfrq(int voice, int forced)
{
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index == 0)
		return;
	awe_poke(AWE_TREMFRQ(voice),
		 FX_COMB(fx, fx_lay, AWE_FX_LFO1_VOLUME, AWE_FX_LFO1_FREQ,
			 vp->parm.tremfrq));
}

/* set lfo2 pitch & frequency */
static void
awe_fx_fm2frq2(int voice, int forced)
{
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index == 0)
		return;
	awe_poke(AWE_FM2FRQ2(voice),
		 FX_COMB(fx, fx_lay, AWE_FX_LFO2_PITCH, AWE_FX_LFO2_FREQ,
			 vp->parm.fm2frq2));
}


/* Q & current address (Q 4bit value, MSB) */
static void
awe_fx_filterQ(int voice, int forced)
{
	unsigned int addr;
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index == 0)
		return;

	addr = awe_peek_dw(AWE_CCCA(voice)) & 0xffffff;
	addr |= (FX_BYTE(fx, fx_lay, AWE_FX_FILTERQ, vp->parm.filterQ) << 28);
	awe_poke_dw(AWE_CCCA(voice), addr);
}

/*
 * calculate pitch offset
 *
 * 0xE000 is no pitch offset at 44100Hz sample.
 * Every 4096 is one octave.
 */

static void
awe_calc_pitch(int voice)
{
	voice_info *vp = &voices[voice];
	awe_voice_info *ap;
	awe_chan_info *cp = voices[voice].cinfo;
	int offset;

	/* search voice information */
	if ((ap = vp->sample) == NULL)
			return;
	if (ap->index == 0) {
		DEBUG(3,printk("AWE32: set sample (%d)\n", ap->sample));
		if (awe_set_sample((awe_voice_list*)ap) == 0)
			return;
	}

	/* calculate offset */
	if (ap->fixkey >= 0) {
		DEBUG(3,printk("AWE32: p-> fixkey(%d) tune(%d)\n", ap->fixkey, ap->tune));
		offset = (ap->fixkey - ap->root) * 4096 / 12;
	} else {
		DEBUG(3,printk("AWE32: p(%d)-> root(%d) tune(%d)\n", vp->note, ap->root, ap->tune));
		offset = (vp->note - ap->root) * 4096 / 12;
		DEBUG(4,printk("AWE32: p-> ofs=%d\n", offset));
	}
	offset = (offset * ap->scaleTuning) / 100;
	DEBUG(4,printk("AWE32: p-> scale* ofs=%d\n", offset));
	offset += ap->tune * 4096 / 1200;
	DEBUG(4,printk("AWE32: p-> tune+ ofs=%d\n", offset));
	if (cp->bender != 0) {
		DEBUG(3,printk("AWE32: p-> bend(%d) %d\n", voice, cp->bender));
		/* (819200: 1 semitone) ==> (4096: 12 semitones) */
		offset += cp->bender * cp->bender_range / 2400;
	}

	/* add initial pitch correction */
	if (FX_ON(&cp->fx_layer[vp->layer], AWE_FX_INIT_PITCH))
		offset += cp->fx_layer[vp->layer].val[AWE_FX_INIT_PITCH];
	else if (FX_ON(&cp->fx, AWE_FX_INIT_PITCH))
		offset += cp->fx.val[AWE_FX_INIT_PITCH];

	/* 0xe000: root pitch */
	vp->apitch = 0xe000 + ap->rate_offset + offset;
	DEBUG(4,printk("AWE32: p-> sum aofs=%x, rate_ofs=%d\n", vp->apitch, ap->rate_offset));
	if (vp->apitch > 0xffff)
		vp->apitch = 0xffff;
	if (vp->apitch < 0)
		vp->apitch = 0;
}


#ifdef AWE_HAS_GUS_COMPATIBILITY
/* calculate MIDI key and semitone from the specified frequency */
static void
awe_calc_pitch_from_freq(int voice, int freq)
{
	voice_info *vp = &voices[voice];
	awe_voice_info *ap;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	FX_Rec *fx_lay = NULL;
	int offset;
	int note;

	if (voices[voice].layer < MAX_LAYERS)
		fx_lay = &voices[voice].cinfo->fx_layer[voices[voice].layer];

	/* search voice information */
	if ((ap = vp->sample) == NULL)
		return;
	if (ap->index == 0) {
		DEBUG(3,printk("AWE32: set sample (%d)\n", ap->sample));
		if (awe_set_sample((awe_voice_list*)ap) == 0)
			return;
	}
	note = freq_to_note(freq);
	offset = (note - ap->root * 100 + ap->tune) * 4096 / 1200;
	offset = (offset * ap->scaleTuning) / 100;
	if (fx_lay && FX_ON(fx_lay, AWE_FX_INIT_PITCH))
		offset += fx_lay->val[AWE_FX_INIT_PITCH];
	else if (FX_ON(fx, AWE_FX_INIT_PITCH))
		offset += fx->val[AWE_FX_INIT_PITCH];
	vp->apitch = 0xe000 + ap->rate_offset + offset;
	if (vp->apitch > 0xffff)
		vp->apitch = 0xffff;
	if (vp->apitch < 0)
		vp->apitch = 0;
}
#endif /* AWE_HAS_GUS_COMPATIBILITY */


/*
 * calculate volume attenuation
 *
 * Voice volume is controlled by volume attenuation parameter.
 * So volume becomes maximum when avol is 0 (no attenuation), and
 * minimum when 255 (-96dB or silence).
 */

static int vol_table[128] = {
	255,111,95,86,79,74,70,66,63,61,58,56,54,52,50,49,
	47,46,45,43,42,41,40,39,38,37,36,35,34,34,33,32,
	31,31,30,29,29,28,27,27,26,26,25,24,24,23,23,22,
	22,21,21,21,20,20,19,19,18,18,18,17,17,16,16,16,
	15,15,15,14,14,14,13,13,13,12,12,12,11,11,11,10,
	10,10,10,9,9,9,8,8,8,8,7,7,7,7,6,6,
	6,6,5,5,5,5,5,4,4,4,4,3,3,3,3,3,
	2,2,2,2,2,1,1,1,1,1,0,0,0,0,0,0,
};

/* tables for volume->attenuation calculation */
static unsigned char voltab1[128] = {
   0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
   0x63, 0x2b, 0x29, 0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22,
   0x21, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a,
   0x19, 0x19, 0x18, 0x17, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14,
   0x14, 0x13, 0x13, 0x13, 0x12, 0x12, 0x11, 0x11, 0x11, 0x10,
   0x10, 0x10, 0x0f, 0x0f, 0x0f, 0x0e, 0x0e, 0x0e, 0x0e, 0x0d,
   0x0d, 0x0d, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0b, 0x0b, 0x0b,
   0x0b, 0x0a, 0x0a, 0x0a, 0x0a, 0x09, 0x09, 0x09, 0x09, 0x09,
   0x08, 0x08, 0x08, 0x08, 0x08, 0x07, 0x07, 0x07, 0x07, 0x06,
   0x06, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04,
   0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03, 0x03, 0x02,
   0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
   0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char voltab2[128] = {
   0x32, 0x31, 0x30, 0x2f, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x2a,
   0x29, 0x28, 0x27, 0x26, 0x25, 0x24, 0x24, 0x23, 0x22, 0x21,
   0x21, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1c, 0x1c, 0x1b, 0x1a,
   0x1a, 0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15,
   0x14, 0x14, 0x13, 0x13, 0x13, 0x12, 0x12, 0x11, 0x11, 0x10,
   0x10, 0x10, 0x0f, 0x0f, 0x0f, 0x0e, 0x0e, 0x0e, 0x0d, 0x0d,
   0x0d, 0x0c, 0x0c, 0x0c, 0x0b, 0x0b, 0x0b, 0x0b, 0x0a, 0x0a,
   0x0a, 0x0a, 0x09, 0x09, 0x09, 0x09, 0x09, 0x08, 0x08, 0x08,
   0x08, 0x08, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06,
   0x06, 0x06, 0x06, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
   0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03,
   0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01,
   0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char expressiontab[128] = {
   0x7f, 0x6c, 0x62, 0x5a, 0x54, 0x50, 0x4b, 0x48, 0x45, 0x42,
   0x40, 0x3d, 0x3b, 0x39, 0x38, 0x36, 0x34, 0x33, 0x31, 0x30,
   0x2f, 0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x28, 0x27, 0x26, 0x25,
   0x24, 0x24, 0x23, 0x22, 0x21, 0x21, 0x20, 0x1f, 0x1e, 0x1e,
   0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a, 0x1a, 0x19, 0x18, 0x18,
   0x17, 0x17, 0x16, 0x16, 0x15, 0x15, 0x15, 0x14, 0x14, 0x13,
   0x13, 0x12, 0x12, 0x11, 0x11, 0x11, 0x10, 0x10, 0x0f, 0x0f,
   0x0f, 0x0e, 0x0e, 0x0e, 0x0d, 0x0d, 0x0d, 0x0c, 0x0c, 0x0c,
   0x0b, 0x0b, 0x0b, 0x0a, 0x0a, 0x0a, 0x09, 0x09, 0x09, 0x09,
   0x08, 0x08, 0x08, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06,
   0x06, 0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03,
   0x03, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01,
   0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void
awe_calc_volume(int voice)
{
	voice_info *vp = &voices[voice];
	awe_voice_info *ap;
	awe_chan_info *cp = voices[voice].cinfo;
	int vol;

	/* search voice information */
	if ((ap = vp->sample) == NULL)
		return;

	ap = vp->sample;
	if (ap->index == 0) {
		DEBUG(3,printk("AWE32: set sample (%d)\n", ap->sample));
		if (awe_set_sample((awe_voice_list*)ap) == 0)
			return;
	}
	
	if (ctrls[AWE_MD_NEW_VOLUME_CALC]) {
		int main_vol = cp->main_vol * ap->amplitude / 127;
		limitvalue(vp->velocity, 0, 127);
		limitvalue(main_vol, 0, 127);
		limitvalue(cp->expression_vol, 0, 127);

		vol = voltab1[main_vol] + voltab2[vp->velocity];
		vol = (vol * 8) / 3;
		vol += ap->attenuation;
		if (cp->expression_vol < 127)
			vol += ((0x100 - vol) * expressiontab[cp->expression_vol])/128;
		vol += atten_offset;
		if (atten_relative)
			vol += ctrls[AWE_MD_ZERO_ATTEN];
		limitvalue(vol, 0, 255);
		vp->avol = vol;
		
	} else {
		/* 0 - 127 */
		vol = (vp->velocity * cp->main_vol * cp->expression_vol) / (127*127);
		vol = vol * ap->amplitude / 127;

		if (vol < 0) vol = 0;
		if (vol > 127) vol = 127;

		/* calc to attenuation */
		vol = vol_table[vol];
		vol += (int)ap->attenuation;
		vol += atten_offset;
		if (atten_relative)
			vol += ctrls[AWE_MD_ZERO_ATTEN];
		if (vol > 255) vol = 255;

		vp->avol = vol;
	}
	if (cp->bank !=  AWE_DRUM_BANK && ((awe_voice_parm_block*)(&ap->parm))->volatk < 0x7d) {
		int atten;
		if (vp->velocity < 70) atten = 70;
		else atten = vp->velocity;
		vp->acutoff = (atten * ap->parm.cutoff + 0xa0) >> 7;
	} else {
		vp->acutoff = ap->parm.cutoff;
	}
	DEBUG(3,printk("AWE32: [-- voice(%d) vol=%x]\n", voice, vol));
}

/* change master volume */
static void
awe_change_master_volume(short val)
{
	limitvalue(val, 0, 127);
	atten_offset = vol_table[val];
	atten_relative = TRUE;
	awe_update_volume();
}

/* update volumes of all available channels */
static void awe_update_volume(void)
{
	int i;
	for (i = 0; i < awe_max_voices; i++)
		awe_set_voice_vol(i, TRUE);
}

/* set sostenuto on */
static void awe_sostenuto_on(int voice, int forced)
{
	if (IS_NO_EFFECT(voice) && !forced) return;
	voices[voice].sostenuto = 127;
}


/* drop sustain */
static void awe_sustain_off(int voice, int forced)
{
	if (voices[voice].state == AWE_ST_SUSTAINED) {
		awe_note_off(voice);
		awe_fx_init(voices[voice].ch);
		awe_voice_init(voice, FALSE);
	}
}


/* terminate and initialize voice */
static void awe_terminate_and_init(int voice, int forced)
{
	awe_terminate(voice);
	awe_fx_init(voices[voice].ch);
	awe_voice_init(voice, TRUE);
}


/*
 * synth operation routines
 */

#define AWE_VOICE_KEY(v)	(0x8000 | (v))
#define AWE_CHAN_KEY(c,n)	(((c) << 8) | ((n) + 1))
#define KEY_CHAN_MATCH(key,c)	(((key) >> 8) == (c))

/* initialize the voice */
static void
awe_voice_init(int voice, int init_all)
{
	voice_info *vp = &voices[voice];

	/* reset voice search key */
	if (playing_mode == AWE_PLAY_DIRECT)
		vp->key = AWE_VOICE_KEY(voice);
	else
		vp->key = 0;

	/* clear voice mapping */
	voice_alloc->map[voice] = 0;

	/* touch the timing flag */
	vp->time = current_alloc_time;

	/* initialize other parameters if necessary */
	if (init_all) {
		vp->note = -1;
		vp->velocity = 0;
		vp->sostenuto = 0;

		vp->sample = NULL;
		vp->cinfo = &channels[voice];
		vp->ch = voice;
		vp->state = AWE_ST_OFF;

		/* emu8000 parameters */
		vp->apitch = 0;
		vp->avol = 255;
		vp->apan = -1;
	}
}

/* clear effects */
static void awe_fx_init(int ch)
{
	if (SINGLE_LAYER_MODE() && !ctrls[AWE_MD_KEEP_EFFECT]) {
		memset(&channels[ch].fx, 0, sizeof(channels[ch].fx));
		memset(&channels[ch].fx_layer, 0, sizeof(&channels[ch].fx_layer));
	}
}

/* initialize channel info */
static void awe_channel_init(int ch, int init_all)
{
	awe_chan_info *cp = &channels[ch];
	cp->channel = ch;
	if (init_all) {
		cp->panning = 0; /* zero center */
		cp->bender_range = 200; /* sense * 100 */
		cp->main_vol = 127;
		if (MULTI_LAYER_MODE() && IS_DRUM_CHANNEL(ch)) {
			cp->instr = ctrls[AWE_MD_DEF_DRUM];
			cp->bank = AWE_DRUM_BANK;
		} else {
			cp->instr = ctrls[AWE_MD_DEF_PRESET];
			cp->bank = ctrls[AWE_MD_DEF_BANK];
		}
	}

	cp->bender = 0; /* zero tune skew */
	cp->expression_vol = 127;
	cp->chan_press = 0;
	cp->sustained = 0;

	if (! ctrls[AWE_MD_KEEP_EFFECT]) {
		memset(&cp->fx, 0, sizeof(cp->fx));
		memset(&cp->fx_layer, 0, sizeof(cp->fx_layer));
	}
}


/* change the voice parameters; voice = channel */
static void awe_voice_change(int voice, fx_affect_func func)
{
	int i; 
	switch (playing_mode) {
	case AWE_PLAY_DIRECT:
		func(voice, FALSE);
		break;
	case AWE_PLAY_INDIRECT:
		for (i = 0; i < awe_max_voices; i++)
			if (voices[i].key == AWE_VOICE_KEY(voice))
				func(i, FALSE);
		break;
	default:
		for (i = 0; i < awe_max_voices; i++)
			if (KEY_CHAN_MATCH(voices[i].key, voice))
				func(i, FALSE);
		break;
	}
}


/*
 * device open / close
 */

/* open device:
 *   reset status of all voices, and clear sample position flag
 */
static int
awe_open(int dev, int mode)
{
	if (awe_busy)
		return -EBUSY;

	awe_busy = TRUE;

	/* set default mode */
	awe_init_ctrl_parms(FALSE);
	atten_relative = TRUE;
	atten_offset = 0;
	drum_flags = DEFAULT_DRUM_FLAGS;
	playing_mode = AWE_PLAY_INDIRECT;

	/* reset voices & channels */
	awe_reset(dev);

	patch_opened = 0;

	return 0;
}


/* close device:
 *   reset all voices again (terminate sounds)
 */
static void
awe_close(int dev)
{
	awe_reset(dev);
	awe_busy = FALSE;
}


/* set miscellaneous mode parameters
 */
static void
awe_init_ctrl_parms(int init_all)
{
	int i;
	for (i = 0; i < AWE_MD_END; i++) {
		if (init_all || ctrl_parms[i].init_each_time)
			ctrls[i] = ctrl_parms[i].value;
	}
}


/* sequencer I/O control:
 */
static int
awe_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	switch (cmd) {
	case SNDCTL_SYNTH_INFO:
		if (playing_mode == AWE_PLAY_DIRECT)
			awe_info.nr_voices = awe_max_voices;
		else
			awe_info.nr_voices = AWE_MAX_CHANNELS;
		if (copy_to_user(arg, &awe_info, sizeof(awe_info)))
			return -EFAULT;
		return 0;
		break;

	case SNDCTL_SEQ_RESETSAMPLES:
		awe_reset(dev);
		awe_reset_samples();
		return 0;
		break;

	case SNDCTL_SEQ_PERCMODE:
		/* what's this? */
		return 0;
		break;

	case SNDCTL_SYNTH_MEMAVL:
		return memsize - awe_free_mem_ptr() * 2;
		break;

	default:
		printk(KERN_WARNING "AWE32: unsupported ioctl %d\n", cmd);
		return -EINVAL;
		break;
	}
}


static int voice_in_range(int voice)
{
	if (playing_mode == AWE_PLAY_DIRECT) {
		if (voice < 0 || voice >= awe_max_voices)
			return FALSE;
	} else {
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return FALSE;
	}
	return TRUE;
}

static void release_voice(int voice, int do_sustain)
{
	if (IS_NO_SOUND(voice))
		return;
	if (do_sustain && (voices[voice].cinfo->sustained == 127 ||
			    voices[voice].sostenuto == 127))
		voices[voice].state = AWE_ST_SUSTAINED;
	else {
		awe_note_off(voice);
		awe_fx_init(voices[voice].ch);
		awe_voice_init(voice, FALSE);
	}
}

/* release all notes */
static void awe_note_off_all(int do_sustain)
{
	int i;
	for (i = 0; i < awe_max_voices; i++)
		release_voice(i, do_sustain);
}

/* kill a voice:
 *   not terminate, just release the voice.
 */
static int
awe_kill_note(int dev, int voice, int note, int velocity)
{
	int i, v2, key;

	DEBUG(2,printk("AWE32: [off(%d) nt=%d vl=%d]\n", voice, note, velocity));
	if (! voice_in_range(voice))
		return -EINVAL;

	switch (playing_mode) {
	case AWE_PLAY_DIRECT:
	case AWE_PLAY_INDIRECT:
		key = AWE_VOICE_KEY(voice);
		break;

	case AWE_PLAY_MULTI2:
		v2 = voice_alloc->map[voice] >> 8;
		voice_alloc->map[voice] = 0;
		voice = v2;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return -EINVAL;
		/* continue to below */
	default:
		key = AWE_CHAN_KEY(voice, note);
		break;
	}

	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].key == key)
			release_voice(i, TRUE);
	}
	return 0;
}


static void start_or_volume_change(int voice, int velocity)
{
	voices[voice].velocity = velocity;
	awe_calc_volume(voice);
	if (voices[voice].state == AWE_ST_STANDBY)
		awe_note_on(voice);
	else if (voices[voice].state == AWE_ST_ON)
		awe_set_volume(voice, FALSE);
}

static void set_and_start_voice(int voice, int state)
{
	/* calculate pitch & volume parameters */
	voices[voice].state = state;
	awe_calc_pitch(voice);
	awe_calc_volume(voice);
	if (state == AWE_ST_ON)
		awe_note_on(voice);
}

/* start a voice:
 *   if note is 255, identical with aftertouch function.
 *   Otherwise, start a voice with specified not and volume.
 */
static int
awe_start_note(int dev, int voice, int note, int velocity)
{
	int i, key, state, volonly;

	DEBUG(2,printk("AWE32: [on(%d) nt=%d vl=%d]\n", voice, note, velocity));
	if (! voice_in_range(voice))
		return -EINVAL;
	    
	if (velocity == 0)
		state = AWE_ST_STANDBY; /* stand by for playing */
	else
		state = AWE_ST_ON;	/* really play */
	volonly = FALSE;

	switch (playing_mode) {
	case AWE_PLAY_DIRECT:
	case AWE_PLAY_INDIRECT:
		key = AWE_VOICE_KEY(voice);
		if (note == 255)
			volonly = TRUE;
		break;

	case AWE_PLAY_MULTI2:
		voice = voice_alloc->map[voice] >> 8;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return -EINVAL;
		/* continue to below */
	default:
		if (note >= 128) { /* key volume mode */
			note -= 128;
			volonly = TRUE;
		}
		key = AWE_CHAN_KEY(voice, note);
		break;
	}

	/* dynamic volume change */
	if (volonly) {
		for (i = 0; i < awe_max_voices; i++) {
			if (voices[i].key == key)
				start_or_volume_change(i, velocity);
		}
		return 0;
	}

	/* if the same note still playing, stop it */
	if (playing_mode != AWE_PLAY_DIRECT || ctrls[AWE_MD_EXCLUSIVE_SOUND]) {
		for (i = 0; i < awe_max_voices; i++)
			if (voices[i].key == key) {
				if (voices[i].state == AWE_ST_ON) {
					awe_note_off(i);
					awe_voice_init(i, FALSE);
				} else if (voices[i].state == AWE_ST_STANDBY)
					awe_voice_init(i, TRUE);
			}
	}

	/* allocate voices */
	if (playing_mode == AWE_PLAY_DIRECT)
		awe_alloc_one_voice(voice, note, velocity);
	else
		awe_alloc_multi_voices(voice, note, velocity, key);

	/* turn off other voices exlusively (for drums) */
	for (i = 0; i < awe_max_voices; i++)
		if (voices[i].key == key)
			awe_exclusive_off(i);

	/* set up pitch and volume parameters */
	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].key == key && voices[i].state == AWE_ST_OFF)
			set_and_start_voice(i, state);
	}

	return 0;
}


/* calculate hash key */
static int
awe_search_key(int bank, int preset, int note)
{
	unsigned int key;

#if 1 /* new hash table */
	if (bank == AWE_DRUM_BANK)
		key = preset + note + 128;
	else
		key = bank + preset;
#else
	key = preset;
#endif
	key %= AWE_MAX_PRESETS;

	return (int)key;
}


/* search instrument from hash table */
static awe_voice_list *
awe_search_instr(int bank, int preset, int note)
{
	awe_voice_list *p;
	int key, key2;

	key = awe_search_key(bank, preset, note);
	for (p = preset_table[key]; p; p = p->next_bank) {
		if (p->instr == preset && p->bank == bank)
			return p;
	}
	key2 = awe_search_key(bank, preset, 0); /* search default */
	if (key == key2)
		return NULL;
	for (p = preset_table[key2]; p; p = p->next_bank) {
		if (p->instr == preset && p->bank == bank)
			return p;
	}
	return NULL;
}


/* assign the instrument to a voice */
static int
awe_set_instr_2(int dev, int voice, int instr_no)
{
	if (playing_mode == AWE_PLAY_MULTI2) {
		voice = voice_alloc->map[voice] >> 8;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return -EINVAL;
	}
	return awe_set_instr(dev, voice, instr_no);
}

/* assign the instrument to a channel; voice is the channel number */
static int
awe_set_instr(int dev, int voice, int instr_no)
{
	awe_chan_info *cinfo;

	if (! voice_in_range(voice))
		return -EINVAL;

	if (instr_no < 0 || instr_no >= AWE_MAX_PRESETS)
		return -EINVAL;

	cinfo = &channels[voice];
	cinfo->instr = instr_no;
	DEBUG(2,printk("AWE32: [program(%d) %d]\n", voice, instr_no));

	return 0;
}


/* reset all voices; terminate sounds and initialize parameters */
static void
awe_reset(int dev)
{
	int i;
	current_alloc_time = 0;
	/* don't turn off voice 31 and 32.  they are used also for FM voices */
	for (i = 0; i < awe_max_voices; i++) {
		awe_terminate(i);
		awe_voice_init(i, TRUE);
	}
	for (i = 0; i < AWE_MAX_CHANNELS; i++)
		awe_channel_init(i, TRUE);
	for (i = 0; i < 16; i++) {
		awe_operations.chn_info[i].controllers[CTL_MAIN_VOLUME] = 127;
		awe_operations.chn_info[i].controllers[CTL_EXPRESSION] = 127;
	}
	awe_init_fm();
	awe_tweak();
}


/* hardware specific control:
 *   GUS specific and AWE32 specific controls are available.
 */
static void
awe_hw_control(int dev, unsigned char *event)
{
	int cmd = event[2];
	if (cmd & _AWE_MODE_FLAG)
		awe_hw_awe_control(dev, cmd & _AWE_MODE_VALUE_MASK, event);
#ifdef AWE_HAS_GUS_COMPATIBILITY
	else
		awe_hw_gus_control(dev, cmd & _AWE_MODE_VALUE_MASK, event);
#endif
}


#ifdef AWE_HAS_GUS_COMPATIBILITY

/* GUS compatible controls */
static void
awe_hw_gus_control(int dev, int cmd, unsigned char *event)
{
	int voice, i, key;
	unsigned short p1;
	short p2;
	int plong;

	if (MULTI_LAYER_MODE())
		return;
	if (cmd == _GUS_NUMVOICES)
		return;

	voice = event[3];
	if (! voice_in_range(voice))
		return;

	p1 = *(unsigned short *) &event[4];
	p2 = *(short *) &event[6];
	plong = *(int*) &event[4];

	switch (cmd) {
	case _GUS_VOICESAMPLE:
		awe_set_instr(dev, voice, p1);
		return;

	case _GUS_VOICEBALA:
		/* 0 to 15 --> -128 to 127 */
		awe_panning(dev, voice, ((int)p1 << 4) - 128);
		return;

	case _GUS_VOICEVOL:
	case _GUS_VOICEVOL2:
		/* not supported yet */
		return;

	case _GUS_RAMPRANGE:
	case _GUS_RAMPRATE:
	case _GUS_RAMPMODE:
	case _GUS_RAMPON:
	case _GUS_RAMPOFF:
		/* volume ramping not supported */
		return;

	case _GUS_VOLUME_SCALE:
		return;

	case _GUS_VOICE_POS:
		FX_SET(&channels[voice].fx, AWE_FX_SAMPLE_START,
		       (short)(plong & 0x7fff));
		FX_SET(&channels[voice].fx, AWE_FX_COARSE_SAMPLE_START,
		       (plong >> 15) & 0xffff);
		return;
	}

	key = AWE_VOICE_KEY(voice);
	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].key == key) {
			switch (cmd) {
			case _GUS_VOICEON:
				awe_note_on(i);
				break;

			case _GUS_VOICEOFF:
				awe_terminate(i);
				awe_fx_init(voices[i].ch);
				awe_voice_init(i, TRUE);
				break;

			case _GUS_VOICEFADE:
				awe_note_off(i);
				awe_fx_init(voices[i].ch);
				awe_voice_init(i, FALSE);
				break;

			case _GUS_VOICEFREQ:
				awe_calc_pitch_from_freq(i, plong);
				break;
			}
		}
	}
}

#endif /* gus_compat */


/* AWE32 specific controls */
static void
awe_hw_awe_control(int dev, int cmd, unsigned char *event)
{
	int voice;
	unsigned short p1;
	short p2;
	int i;

	voice = event[3];
	if (! voice_in_range(voice))
		return;

	if (playing_mode == AWE_PLAY_MULTI2) {
		voice = voice_alloc->map[voice] >> 8;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return;
	}

	p1 = *(unsigned short *) &event[4];
	p2 = *(short *) &event[6];

	switch (cmd) {
	case _AWE_DEBUG_MODE:
		ctrls[AWE_MD_DEBUG_MODE] = p1;
		printk(KERN_DEBUG "AWE32: debug mode = %d\n", ctrls[AWE_MD_DEBUG_MODE]);
		break;
	case _AWE_REVERB_MODE:
		ctrls[AWE_MD_REVERB_MODE] = p1;
		awe_update_reverb_mode();
		break;

	case _AWE_CHORUS_MODE:
		ctrls[AWE_MD_CHORUS_MODE] = p1;
		awe_update_chorus_mode();
		break;
		      
	case _AWE_REMOVE_LAST_SAMPLES:
		DEBUG(0,printk("AWE32: remove last samples\n"));
		awe_reset(0);
		if (locked_sf_id > 0)
			awe_remove_samples(locked_sf_id);
		break;

	case _AWE_INITIALIZE_CHIP:
		awe_initialize();
		break;

	case _AWE_SEND_EFFECT:
		i = -1;
		if (p1 >= 0x100) {
			i = (p1 >> 8);
			if (i < 0 || i >= MAX_LAYERS)
				break;
		}
		awe_send_effect(voice, i, p1, p2);
		break;

	case _AWE_RESET_CHANNEL:
		awe_channel_init(voice, !p1);
		break;
		
	case _AWE_TERMINATE_ALL:
		awe_reset(0);
		break;

	case _AWE_TERMINATE_CHANNEL:
		awe_voice_change(voice, awe_terminate_and_init);
		break;

	case _AWE_RELEASE_ALL:
		awe_note_off_all(FALSE);
		break;
	case _AWE_NOTEOFF_ALL:
		awe_note_off_all(TRUE);
		break;

	case _AWE_INITIAL_VOLUME:
		DEBUG(0,printk("AWE32: init attenuation %d\n", p1));
		atten_relative = (char)p2;
		atten_offset = (short)p1;
		awe_update_volume();
		break;

	case _AWE_CHN_PRESSURE:
		channels[voice].chan_press = p1;
		awe_modwheel_change(voice, p1);
		break;

	case _AWE_CHANNEL_MODE:
		DEBUG(0,printk("AWE32: channel mode = %d\n", p1));
		playing_mode = p1;
		awe_reset(0);
		break;

	case _AWE_DRUM_CHANNELS:
		DEBUG(0,printk("AWE32: drum flags = %x\n", p1));
		drum_flags = *(unsigned int*)&event[4];
		break;

	case _AWE_MISC_MODE:
		DEBUG(0,printk("AWE32: ctrl parms = %d %d\n", p1, p2));
		if (p1 > AWE_MD_VERSION && p1 < AWE_MD_END) {
			ctrls[p1] = p2;
			if (ctrl_parms[p1].update)
				ctrl_parms[p1].update();
		}
		break;

	case _AWE_EQUALIZER:
		ctrls[AWE_MD_BASS_LEVEL] = p1;
		ctrls[AWE_MD_TREBLE_LEVEL] = p2;
		awe_update_equalizer();
		break;

	default:
		DEBUG(0,printk("AWE32: hw control cmd=%d voice=%d\n", cmd, voice));
		break;
	}
}


/* change effects */
static void
awe_send_effect(int voice, int layer, int type, int val)
{
	awe_chan_info *cinfo;
	FX_Rec *fx;
	int mode;

	cinfo = &channels[voice];
	if (layer >= 0 && layer < MAX_LAYERS)
		fx = &cinfo->fx_layer[layer];
	else
		fx = &cinfo->fx;

	if (type & 0x40)
		mode = FX_FLAG_OFF;
	else if (type & 0x80)
		mode = FX_FLAG_ADD;
	else
		mode = FX_FLAG_SET;
	type &= 0x3f;

	if (type >= 0 && type < AWE_FX_END) {
		DEBUG(2,printk("AWE32: effects (%d) %d %d\n", voice, type, val));
		if (mode == FX_FLAG_SET)
			FX_SET(fx, type, val);
		else if (mode == FX_FLAG_ADD)
			FX_ADD(fx, type, val);
		else
			FX_UNSET(fx, type);
		if (mode != FX_FLAG_OFF && parm_defs[type].realtime) {
			DEBUG(2,printk("AWE32: fx_realtime (%d)\n", voice));
			awe_voice_change(voice, parm_defs[type].realtime);
		}
	}
}


/* change modulation wheel; voice is already mapped on multi2 mode */
static void
awe_modwheel_change(int voice, int value)
{
	int i;
	awe_chan_info *cinfo;

	cinfo = &channels[voice];
	i = value * ctrls[AWE_MD_MOD_SENSE] / 1200;
	FX_ADD(&cinfo->fx, AWE_FX_LFO1_PITCH, i);
	awe_voice_change(voice, awe_fx_fmmod);
	FX_ADD(&cinfo->fx, AWE_FX_LFO2_PITCH, i);
	awe_voice_change(voice, awe_fx_fm2frq2);
}


/* voice pressure change */
static void
awe_aftertouch(int dev, int voice, int pressure)
{
	int note;

	DEBUG(2,printk("AWE32: [after(%d) %d]\n", voice, pressure));
	if (! voice_in_range(voice))
		return;

	switch (playing_mode) {
	case AWE_PLAY_DIRECT:
	case AWE_PLAY_INDIRECT:
		awe_start_note(dev, voice, 255, pressure);
		break;
	case AWE_PLAY_MULTI2:
		note = (voice_alloc->map[voice] & 0xff) - 1;
		awe_key_pressure(dev, voice, note + 0x80, pressure);
		break;
	}
}


/* voice control change */
static void
awe_controller(int dev, int voice, int ctrl_num, int value)
{
	awe_chan_info *cinfo;

	if (! voice_in_range(voice))
		return;

	if (playing_mode == AWE_PLAY_MULTI2) {
		voice = voice_alloc->map[voice] >> 8;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return;
	}

	cinfo = &channels[voice];

	switch (ctrl_num) {
	case CTL_BANK_SELECT: /* MIDI control #0 */
		DEBUG(2,printk("AWE32: [bank(%d) %d]\n", voice, value));
		if (MULTI_LAYER_MODE() && IS_DRUM_CHANNEL(voice) &&
		    !ctrls[AWE_MD_TOGGLE_DRUM_BANK])
			break;
		if (value < 0 || value > 255)
			break;
		cinfo->bank = value;
		if (cinfo->bank == AWE_DRUM_BANK)
			DRUM_CHANNEL_ON(cinfo->channel);
		else
			DRUM_CHANNEL_OFF(cinfo->channel);
		awe_set_instr(dev, voice, cinfo->instr);
		break;

	case CTL_MODWHEEL: /* MIDI control #1 */
		DEBUG(2,printk("AWE32: [modwheel(%d) %d]\n", voice, value));
		awe_modwheel_change(voice, value);
		break;

	case CTRL_PITCH_BENDER: /* SEQ1 V2 contorl */
		DEBUG(2,printk("AWE32: [bend(%d) %d]\n", voice, value));
		/* zero centered */
		cinfo->bender = value;
		awe_voice_change(voice, awe_set_voice_pitch);
		break;

	case CTRL_PITCH_BENDER_RANGE: /* SEQ1 V2 control */
		DEBUG(2,printk("AWE32: [range(%d) %d]\n", voice, value));
		/* value = sense x 100 */
		cinfo->bender_range = value;
		/* no audible pitch change yet.. */
		break;

	case CTL_EXPRESSION: /* MIDI control #11 */
		if (SINGLE_LAYER_MODE())
			value /= 128;
	case CTRL_EXPRESSION: /* SEQ1 V2 control */
		DEBUG(2,printk("AWE32: [expr(%d) %d]\n", voice, value));
		/* 0 - 127 */
		cinfo->expression_vol = value;
		awe_voice_change(voice, awe_set_voice_vol);
		break;

	case CTL_PAN:	/* MIDI control #10 */
		DEBUG(2,printk("AWE32: [pan(%d) %d]\n", voice, value));
		/* (0-127) -> signed 8bit */
		cinfo->panning = value * 2 - 128;
		if (ctrls[AWE_MD_REALTIME_PAN])
			awe_voice_change(voice, awe_set_pan);
		break;

	case CTL_MAIN_VOLUME:	/* MIDI control #7 */
		if (SINGLE_LAYER_MODE())
			value = (value * 100) / 16383;
	case CTRL_MAIN_VOLUME:	/* SEQ1 V2 control */
		DEBUG(2,printk("AWE32: [mainvol(%d) %d]\n", voice, value));
		/* 0 - 127 */
		cinfo->main_vol = value;
		awe_voice_change(voice, awe_set_voice_vol);
		break;

	case CTL_EXT_EFF_DEPTH: /* reverb effects: 0-127 */
		DEBUG(2,printk("AWE32: [reverb(%d) %d]\n", voice, value));
		FX_SET(&cinfo->fx, AWE_FX_REVERB, value * 2);
		break;

	case CTL_CHORUS_DEPTH: /* chorus effects: 0-127 */
		DEBUG(2,printk("AWE32: [chorus(%d) %d]\n", voice, value));
		FX_SET(&cinfo->fx, AWE_FX_CHORUS, value * 2);
		break;

	case 120:  /* all sounds off */
		awe_note_off_all(FALSE);
		break;
	case 123:  /* all notes off */
		awe_note_off_all(TRUE);
		break;

	case CTL_SUSTAIN: /* MIDI control #64 */
		cinfo->sustained = value;
		if (value != 127)
			awe_voice_change(voice, awe_sustain_off);
		break;

	case CTL_SOSTENUTO: /* MIDI control #66 */
		if (value == 127)
			awe_voice_change(voice, awe_sostenuto_on);
		else
			awe_voice_change(voice, awe_sustain_off);
		break;

	default:
		DEBUG(0,printk("AWE32: [control(%d) ctrl=%d val=%d]\n",
			   voice, ctrl_num, value));
		break;
	}
}


/* voice pan change (value = -128 - 127) */
static void
awe_panning(int dev, int voice, int value)
{
	awe_chan_info *cinfo;

	if (! voice_in_range(voice))
		return;

	if (playing_mode == AWE_PLAY_MULTI2) {
		voice = voice_alloc->map[voice] >> 8;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return;
	}

	cinfo = &channels[voice];
	cinfo->panning = value;
	DEBUG(2,printk("AWE32: [pan(%d) %d]\n", voice, cinfo->panning));
	if (ctrls[AWE_MD_REALTIME_PAN])
		awe_voice_change(voice, awe_set_pan);
}


/* volume mode change */
static void
awe_volume_method(int dev, int mode)
{
	/* not impremented */
	DEBUG(0,printk("AWE32: [volmethod mode=%d]\n", mode));
}


/* pitch wheel change: 0-16384 */
static void
awe_bender(int dev, int voice, int value)
{
	awe_chan_info *cinfo;

	if (! voice_in_range(voice))
		return;

	if (playing_mode == AWE_PLAY_MULTI2) {
		voice = voice_alloc->map[voice] >> 8;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return;
	}

	/* convert to zero centered value */
	cinfo = &channels[voice];
	cinfo->bender = value - 8192;
	DEBUG(2,printk("AWE32: [bend(%d) %d]\n", voice, cinfo->bender));
	awe_voice_change(voice, awe_set_voice_pitch);
}


/*
 * load a sound patch:
 *   three types of patches are accepted: AWE, GUS, and SYSEX.
 */

static int
awe_load_patch(int dev, int format, const char __user *addr,
	       int offs, int count, int pmgr_flag)
{
	awe_patch_info patch;
	int rc = 0;

#ifdef AWE_HAS_GUS_COMPATIBILITY
	if (format == GUS_PATCH) {
		return awe_load_guspatch(addr, offs, count, pmgr_flag);
	} else
#endif
	if (format == SYSEX_PATCH) {
		/* no system exclusive message supported yet */
		return 0;
	} else if (format != AWE_PATCH) {
		printk(KERN_WARNING "AWE32 Error: Invalid patch format (key) 0x%x\n", format);
		return -EINVAL;
	}
	
	if (count < AWE_PATCH_INFO_SIZE) {
		printk(KERN_WARNING "AWE32 Error: Patch header too short\n");
		return -EINVAL;
	}
	if (copy_from_user(((char*)&patch) + offs, addr + offs, 
			   AWE_PATCH_INFO_SIZE - offs))
		return -EFAULT;

	count -= AWE_PATCH_INFO_SIZE;
	if (count < patch.len) {
		printk(KERN_WARNING "AWE32: sample: Patch record too short (%d<%d)\n",
		       count, patch.len);
		return -EINVAL;
	}
	
	switch (patch.type) {
	case AWE_LOAD_INFO:
		rc = awe_load_info(&patch, addr, count);
		break;
	case AWE_LOAD_DATA:
		rc = awe_load_data(&patch, addr, count);
		break;
	case AWE_OPEN_PATCH:
		rc = awe_open_patch(&patch, addr, count);
		break;
	case AWE_CLOSE_PATCH:
		rc = awe_close_patch(&patch, addr, count);
		break;
	case AWE_UNLOAD_PATCH:
		rc = awe_unload_patch(&patch, addr, count);
		break;
	case AWE_REPLACE_DATA:
		rc = awe_replace_data(&patch, addr, count);
		break;
	case AWE_MAP_PRESET:
		rc = awe_load_map(&patch, addr, count);
		break;
	/* case AWE_PROBE_INFO:
		rc = awe_probe_info(&patch, addr, count);
		break;*/
	case AWE_PROBE_DATA:
		rc = awe_probe_data(&patch, addr, count);
		break;
	case AWE_REMOVE_INFO:
		rc = awe_remove_info(&patch, addr, count);
		break;
	case AWE_LOAD_CHORUS_FX:
		rc = awe_load_chorus_fx(&patch, addr, count);
		break;
	case AWE_LOAD_REVERB_FX:
		rc = awe_load_reverb_fx(&patch, addr, count);
		break;

	default:
		printk(KERN_WARNING "AWE32 Error: unknown patch format type %d\n",
		       patch.type);
		rc = -EINVAL;
	}

	return rc;
}


/* create an sf list record */
static int
awe_create_sf(int type, char *name)
{
	sf_list *rec;

	/* terminate sounds */
	awe_reset(0);
	rec = (sf_list *)kmalloc(sizeof(*rec), GFP_KERNEL);
	if (rec == NULL)
		return 1; /* no memory */
	rec->sf_id = current_sf_id + 1;
	rec->type = type;
	if (/*current_sf_id == 0 ||*/ (type & AWE_PAT_LOCKED) != 0)
		locked_sf_id = current_sf_id + 1;
	rec->num_info = awe_free_info();
	rec->num_sample = awe_free_sample();
	rec->mem_ptr = awe_free_mem_ptr();
	rec->infos = rec->last_infos = NULL;
	rec->samples = rec->last_samples = NULL;

	/* add to linked-list */
	rec->next = NULL;
	rec->prev = sftail;
	if (sftail)
		sftail->next = rec;
	else
		sfhead = rec;
	sftail = rec;
	current_sf_id++;

#ifdef AWE_ALLOW_SAMPLE_SHARING
	rec->shared = NULL;
	if (name)
		memcpy(rec->name, name, AWE_PATCH_NAME_LEN);
	else
		strcpy(rec->name, "*TEMPORARY*");
	if (current_sf_id > 1 && name && (type & AWE_PAT_SHARED) != 0) {
		/* is the current font really a shared font? */
		if (is_shared_sf(rec->name)) {
			/* check if the shared font is already installed */
			sf_list *p;
			for (p = rec->prev; p; p = p->prev) {
				if (is_identical_name(rec->name, p)) {
					rec->shared = p;
					break;
				}
			}
		}
	}
#endif /* allow sharing */

	return 0;
}


#ifdef AWE_ALLOW_SAMPLE_SHARING

/* check if the given name is a valid shared name */
#define ASC_TO_KEY(c) ((c) - 'A' + 1)
static int is_shared_sf(unsigned char *name)
{
	static unsigned char id_head[4] = {
		ASC_TO_KEY('A'), ASC_TO_KEY('W'), ASC_TO_KEY('E'),
		AWE_MAJOR_VERSION,
	};
	if (memcmp(name, id_head, 4) == 0)
		return TRUE;
	return FALSE;
}

/* check if the given name matches to the existing list */
static int is_identical_name(unsigned char *name, sf_list *p) 
{
	char *id = p->name;
	if (is_shared_sf(id) && memcmp(id, name, AWE_PATCH_NAME_LEN) == 0)
		return TRUE;
	return FALSE;
}

/* check if the given voice info exists */
static int info_duplicated(sf_list *sf, awe_voice_list *rec)
{
	/* search for all sharing lists */
	for (; sf; sf = sf->shared) {
		awe_voice_list *p;
		for (p = sf->infos; p; p = p->next) {
			if (p->type == V_ST_NORMAL &&
			    p->bank == rec->bank &&
			    p->instr == rec->instr &&
			    p->v.low == rec->v.low &&
			    p->v.high == rec->v.high &&
			    p->v.sample == rec->v.sample)
				return TRUE;
		}
	}
	return FALSE;
}

#endif /* AWE_ALLOW_SAMPLE_SHARING */


/* free sf_list record */
/* linked-list in this function is not cared */
static void
awe_free_sf(sf_list *sf)
{
	if (sf->infos) {
		awe_voice_list *p, *next;
		for (p = sf->infos; p; p = next) {
			next = p->next;
			kfree(p);
		}
	}
	if (sf->samples) {
		awe_sample_list *p, *next;
		for (p = sf->samples; p; p = next) {
			next = p->next;
			kfree(p);
		}
	}
	kfree(sf);
}


/* open patch; create sf list and set opened flag */
static int
awe_open_patch(awe_patch_info *patch, const char __user *addr, int count)
{
	awe_open_parm parm;
	int shared;

	if (copy_from_user(&parm, addr + AWE_PATCH_INFO_SIZE, sizeof(parm)))
		return -EFAULT;
	shared = FALSE;

#ifdef AWE_ALLOW_SAMPLE_SHARING
	if (sftail && (parm.type & AWE_PAT_SHARED) != 0) {
		/* is the previous font the same font? */
		if (is_identical_name(parm.name, sftail)) {
			/* then append to the previous */
			shared = TRUE;
			awe_reset(0);
			if (parm.type & AWE_PAT_LOCKED)
				locked_sf_id = current_sf_id;
		}
	}
#endif /* allow sharing */
	if (! shared) {
		if (awe_create_sf(parm.type, parm.name)) {
			printk(KERN_ERR "AWE32: can't open: failed to alloc new list\n");
			return -ENOMEM;
		}
	}
	patch_opened = TRUE;
	return current_sf_id;
}

/* check if the patch is already opened */
static sf_list *
check_patch_opened(int type, char *name)
{
	if (! patch_opened) {
		if (awe_create_sf(type, name)) {
			printk(KERN_ERR "AWE32: failed to alloc new list\n");
			return NULL;
		}
		patch_opened = TRUE;
		return sftail;
	}
	return sftail;
}

/* close the patch; if no voice is loaded, remove the patch */
static int
awe_close_patch(awe_patch_info *patch, const char __user *addr, int count)
{
	if (patch_opened && sftail) {
		/* if no voice is loaded, release the current patch */
		if (sftail->infos == NULL) {
			awe_reset(0);
			awe_remove_samples(current_sf_id - 1);
		}
	}
	patch_opened = 0;
	return 0;
}


/* remove the latest patch */
static int
awe_unload_patch(awe_patch_info *patch, const char __user *addr, int count)
{
	if (current_sf_id > 0 && current_sf_id > locked_sf_id) {
		awe_reset(0);
		awe_remove_samples(current_sf_id - 1);
	}
	return 0;
}

/* allocate voice info list records */
static awe_voice_list *
alloc_new_info(void)
{
	awe_voice_list *newlist;
	
	newlist = (awe_voice_list *)kmalloc(sizeof(*newlist), GFP_KERNEL);
	if (newlist == NULL) {
		printk(KERN_ERR "AWE32: can't alloc info table\n");
		return NULL;
	}
	return newlist;
}

/* allocate sample info list records */
static awe_sample_list *
alloc_new_sample(void)
{
	awe_sample_list *newlist;
	
	newlist = (awe_sample_list *)kmalloc(sizeof(*newlist), GFP_KERNEL);
	if (newlist == NULL) {
		printk(KERN_ERR "AWE32: can't alloc sample table\n");
		return NULL;
	}
	return newlist;
}

/* load voice map */
static int
awe_load_map(awe_patch_info *patch, const char __user *addr, int count)
{
	awe_voice_map map;
	awe_voice_list *rec, *p;
	sf_list *sf;

	/* get the link info */
	if (count < sizeof(map)) {
		printk(KERN_WARNING "AWE32 Error: invalid patch info length\n");
		return -EINVAL;
	}
	if (copy_from_user(&map, addr + AWE_PATCH_INFO_SIZE, sizeof(map)))
		return -EFAULT;
	
	/* check if the identical mapping already exists */
	p = awe_search_instr(map.map_bank, map.map_instr, map.map_key);
	for (; p; p = p->next_instr) {
		if (p->type == V_ST_MAPPED &&
		    p->v.start == map.src_instr &&
		    p->v.end == map.src_bank &&
		    p->v.fixkey == map.src_key)
			return 0; /* already present! */
	}

	if ((sf = check_patch_opened(AWE_PAT_TYPE_MAP, NULL)) == NULL)
		return -ENOMEM;

	if ((rec = alloc_new_info()) == NULL)
		return -ENOMEM;

	rec->bank = map.map_bank;
	rec->instr = map.map_instr;
	rec->type = V_ST_MAPPED;
	rec->disabled = FALSE;
	awe_init_voice_info(&rec->v);
	if (map.map_key >= 0) {
		rec->v.low = map.map_key;
		rec->v.high = map.map_key;
	}
	rec->v.start = map.src_instr;
	rec->v.end = map.src_bank;
	rec->v.fixkey = map.src_key;
	add_sf_info(sf, rec);
	add_info_list(rec);

	return 0;
}

#if 0
/* probe preset in the current list -- nothing to be loaded */
static int
awe_probe_info(awe_patch_info *patch, const char __user *addr, int count)
{
#ifdef AWE_ALLOW_SAMPLE_SHARING
	awe_voice_map map;
	awe_voice_list *p;

	if (! patch_opened)
		return -EINVAL;

	/* get the link info */
	if (count < sizeof(map)) {
		printk(KERN_WARNING "AWE32 Error: invalid patch info length\n");
		return -EINVAL;
	}
	if (copy_from_user(&map, addr + AWE_PATCH_INFO_SIZE, sizeof(map)))
		return -EFAULT;
	
	/* check if the identical mapping already exists */
	if (sftail == NULL)
		return -EINVAL;
	p = awe_search_instr(map.src_bank, map.src_instr, map.src_key);
	for (; p; p = p->next_instr) {
		if (p->type == V_ST_NORMAL &&
		    is_identical_holder(p->holder, sftail) &&
		    p->v.low <= map.src_key &&
		    p->v.high >= map.src_key)
			return 0; /* already present! */
	}
#endif /* allow sharing */
	return -EINVAL;
}
#endif

/* probe sample in the current list -- nothing to be loaded */
static int
awe_probe_data(awe_patch_info *patch, const char __user *addr, int count)
{
#ifdef AWE_ALLOW_SAMPLE_SHARING
	if (! patch_opened)
		return -EINVAL;

	/* search the specified sample by optarg */
	if (search_sample_index(sftail, patch->optarg) != NULL)
		return 0;
#endif /* allow sharing */
	return -EINVAL;
}

		
/* remove the present instrument layers */
static int
remove_info(sf_list *sf, int bank, int instr)
{
	awe_voice_list *prev, *next, *p;
	int removed = 0;

	prev = NULL;
	for (p = sf->infos; p; p = next) {
		next = p->next;
		if (p->type == V_ST_NORMAL &&
		    p->bank == bank && p->instr == instr) {
			/* remove this layer */
			if (prev)
				prev->next = next;
			else
				sf->infos = next;
			if (p == sf->last_infos)
				sf->last_infos = prev;
			sf->num_info--;
			removed++;
			kfree(p);
		} else
			prev = p;
	}
	if (removed)
		rebuild_preset_list();
	return removed;
}

/* load voice information data */
static int
awe_load_info(awe_patch_info *patch, const char __user *addr, int count)
{
	int offset;
	awe_voice_rec_hdr hdr;
	int i;
	int total_size;
	sf_list *sf;
	awe_voice_list *rec;

	if (count < AWE_VOICE_REC_SIZE) {
		printk(KERN_WARNING "AWE32 Error: invalid patch info length\n");
		return -EINVAL;
	}

	offset = AWE_PATCH_INFO_SIZE;
	if (copy_from_user((char*)&hdr, addr + offset, AWE_VOICE_REC_SIZE))
		return -EFAULT;
	offset += AWE_VOICE_REC_SIZE;

	if (hdr.nvoices <= 0 || hdr.nvoices >= 100) {
		printk(KERN_WARNING "AWE32 Error: Invalid voice number %d\n", hdr.nvoices);
		return -EINVAL;
	}
	total_size = AWE_VOICE_REC_SIZE + AWE_VOICE_INFO_SIZE * hdr.nvoices;
	if (count < total_size) {
		printk(KERN_WARNING "AWE32 Error: patch length(%d) is smaller than nvoices(%d)\n",
		       count, hdr.nvoices);
		return -EINVAL;
	}

	if ((sf = check_patch_opened(AWE_PAT_TYPE_MISC, NULL)) == NULL)
		return -ENOMEM;

	switch (hdr.write_mode) {
	case AWE_WR_EXCLUSIVE:
		/* exclusive mode - if the instrument already exists,
		   return error */
		for (rec = sf->infos; rec; rec = rec->next) {
			if (rec->type == V_ST_NORMAL &&
			    rec->bank == hdr.bank &&
			    rec->instr == hdr.instr)
				return -EINVAL;
		}
		break;
	case AWE_WR_REPLACE:
		/* replace mode - remove the instrument if it already exists */
		remove_info(sf, hdr.bank, hdr.instr);
		break;
	}

	/* append new layers */
	for (i = 0; i < hdr.nvoices; i++) {
		rec = alloc_new_info();
		if (rec == NULL)
			return -ENOMEM;

		rec->bank = hdr.bank;
		rec->instr = hdr.instr;
		rec->type = V_ST_NORMAL;
		rec->disabled = FALSE;

		/* copy awe_voice_info parameters */
		if (copy_from_user(&rec->v, addr + offset, AWE_VOICE_INFO_SIZE)) {
			kfree(rec);
			return -EFAULT;
		}
		offset += AWE_VOICE_INFO_SIZE;
#ifdef AWE_ALLOW_SAMPLE_SHARING
		if (sf && sf->shared) {
			if (info_duplicated(sf, rec)) {
				kfree(rec);
				continue;
			}
		}
#endif /* allow sharing */
		if (rec->v.mode & AWE_MODE_INIT_PARM)
			awe_init_voice_parm(&rec->v.parm);
		add_sf_info(sf, rec);
		awe_set_sample(rec);
		add_info_list(rec);
	}

	return 0;
}


/* remove instrument layers */
static int
awe_remove_info(awe_patch_info *patch, const char __user *addr, int count)
{
	unsigned char bank, instr;
	sf_list *sf;

	if (! patch_opened || (sf = sftail) == NULL) {
		printk(KERN_WARNING "AWE32: remove_info: patch not opened\n");
		return -EINVAL;
	}

	bank = ((unsigned short)patch->optarg >> 8) & 0xff;
	instr = (unsigned short)patch->optarg & 0xff;
	if (! remove_info(sf, bank, instr))
		return -EINVAL;
	return 0;
}


/* load wave sample data */
static int
awe_load_data(awe_patch_info *patch, const char __user *addr, int count)
{
	int offset, size;
	int rc;
	awe_sample_info tmprec;
	awe_sample_list *rec;
	sf_list *sf;

	if ((sf = check_patch_opened(AWE_PAT_TYPE_MISC, NULL)) == NULL)
		return -ENOMEM;

	size = (count - AWE_SAMPLE_INFO_SIZE) / 2;
	offset = AWE_PATCH_INFO_SIZE;
	if (copy_from_user(&tmprec, addr + offset, AWE_SAMPLE_INFO_SIZE))
		return -EFAULT;
	offset += AWE_SAMPLE_INFO_SIZE;
	if (size != tmprec.size) {
		printk(KERN_WARNING "AWE32: load: sample size differed (%d != %d)\n",
		       tmprec.size, size);
		return -EINVAL;
	}

	if (search_sample_index(sf, tmprec.sample) != NULL) {
#ifdef AWE_ALLOW_SAMPLE_SHARING
		/* if shared sample, skip this data */
		if (sf->type & AWE_PAT_SHARED)
			return 0;
#endif /* allow sharing */
		DEBUG(1,printk("AWE32: sample data %d already present\n", tmprec.sample));
		return -EINVAL;
	}

	if ((rec = alloc_new_sample()) == NULL)
		return -ENOMEM;

	memcpy(&rec->v, &tmprec, sizeof(tmprec));

	if (rec->v.size > 0) {
		if ((rc = awe_write_wave_data(addr, offset, rec, -1)) < 0) {
			kfree(rec);
			return rc;
		}
		sf->mem_ptr += rc;
	}

	add_sf_sample(sf, rec);
	return 0;
}


/* replace wave sample data */
static int
awe_replace_data(awe_patch_info *patch, const char __user *addr, int count)
{
	int offset;
	int size;
	int rc;
	int channels;
	awe_sample_info cursmp;
	int save_mem_ptr;
	sf_list *sf;
	awe_sample_list *rec;

	if (! patch_opened || (sf = sftail) == NULL) {
		printk(KERN_WARNING "AWE32: replace: patch not opened\n");
		return -EINVAL;
	}

	size = (count - AWE_SAMPLE_INFO_SIZE) / 2;
	offset = AWE_PATCH_INFO_SIZE;
	if (copy_from_user(&cursmp, addr + offset, AWE_SAMPLE_INFO_SIZE))
		return -EFAULT;
	offset += AWE_SAMPLE_INFO_SIZE;
	if (cursmp.size == 0 || size != cursmp.size) {
		printk(KERN_WARNING "AWE32: replace: invalid sample size (%d!=%d)\n",
		       cursmp.size, size);
		return -EINVAL;
	}
	channels = patch->optarg;
	if (channels <= 0 || channels > AWE_NORMAL_VOICES) {
		printk(KERN_WARNING "AWE32: replace: invalid channels %d\n", channels);
		return -EINVAL;
	}

	for (rec = sf->samples; rec; rec = rec->next) {
		if (rec->v.sample == cursmp.sample)
			break;
	}
	if (rec == NULL) {
		printk(KERN_WARNING "AWE32: replace: cannot find existing sample data %d\n",
		       cursmp.sample);
		return -EINVAL;
	}
		
	if (rec->v.size != cursmp.size) {
		printk(KERN_WARNING "AWE32: replace: exiting size differed (%d!=%d)\n",
		       rec->v.size, cursmp.size);
		return -EINVAL;
	}

	save_mem_ptr = awe_free_mem_ptr();
	sftail->mem_ptr = rec->v.start - awe_mem_start;
	memcpy(&rec->v, &cursmp, sizeof(cursmp));
	rec->v.sf_id = current_sf_id;
	if ((rc = awe_write_wave_data(addr, offset, rec, channels)) < 0)
		return rc;
	sftail->mem_ptr = save_mem_ptr;

	return 0;
}


/*----------------------------------------------------------------*/

static const char __user *readbuf_addr;
static int readbuf_offs;
static int readbuf_flags;

/* initialize read buffer */
static int
readbuf_init(const char __user *addr, int offset, awe_sample_info *sp)
{
	readbuf_addr = addr;
	readbuf_offs = offset;
	readbuf_flags = sp->mode_flags;
	return 0;
}

/* read directly from user buffer */
static unsigned short
readbuf_word(int pos)
{
	unsigned short c;
	/* read from user buffer */
	if (readbuf_flags & AWE_SAMPLE_8BITS) {
		unsigned char cc;
		get_user(cc, (unsigned char __user *)(readbuf_addr + readbuf_offs + pos));
		c = (unsigned short)cc << 8; /* convert 8bit -> 16bit */
	} else {
		get_user(c, (unsigned short __user *)(readbuf_addr + readbuf_offs + pos * 2));
	}
	if (readbuf_flags & AWE_SAMPLE_UNSIGNED)
		c ^= 0x8000; /* unsigned -> signed */
	return c;
}

#define readbuf_word_cache	readbuf_word
#define readbuf_end()		/**/

/*----------------------------------------------------------------*/

#define BLANK_LOOP_START	8
#define BLANK_LOOP_END		40
#define BLANK_LOOP_SIZE		48

/* loading onto memory - return the actual written size */
static int 
awe_write_wave_data(const char __user *addr, int offset, awe_sample_list *list, int channels)
{
	int i, truesize, dram_offset;
	awe_sample_info *sp = &list->v;
	int rc;

	/* be sure loop points start < end */
	if (sp->loopstart > sp->loopend) {
		int tmp = sp->loopstart;
		sp->loopstart = sp->loopend;
		sp->loopend = tmp;
	}

	/* compute true data size to be loaded */
	truesize = sp->size;
	if (sp->mode_flags & (AWE_SAMPLE_BIDIR_LOOP|AWE_SAMPLE_REVERSE_LOOP))
		truesize += sp->loopend - sp->loopstart;
	if (sp->mode_flags & AWE_SAMPLE_NO_BLANK)
		truesize += BLANK_LOOP_SIZE;
	if (awe_free_mem_ptr() + truesize >= memsize/2) {
		DEBUG(-1,printk("AWE32 Error: Sample memory full\n"));
		return -ENOSPC;
	}

	/* recalculate address offset */
	sp->end -= sp->start;
	sp->loopstart -= sp->start;
	sp->loopend -= sp->start;

	dram_offset = awe_free_mem_ptr() + awe_mem_start;
	sp->start = dram_offset;
	sp->end += dram_offset;
	sp->loopstart += dram_offset;
	sp->loopend += dram_offset;

	/* set the total size (store onto obsolete checksum value) */
	if (sp->size == 0)
		sp->checksum = 0;
	else
		sp->checksum = truesize;

	if ((rc = awe_open_dram_for_write(dram_offset, channels)) != 0)
		return rc;

	if (readbuf_init(addr, offset, sp) < 0)
		return -ENOSPC;

	for (i = 0; i < sp->size; i++) {
		unsigned short c;
		c = readbuf_word(i);
		awe_write_dram(c);
		if (i == sp->loopend &&
		    (sp->mode_flags & (AWE_SAMPLE_BIDIR_LOOP|AWE_SAMPLE_REVERSE_LOOP))) {
			int looplen = sp->loopend - sp->loopstart;
			/* copy reverse loop */
			int k;
			for (k = 1; k <= looplen; k++) {
				c = readbuf_word_cache(i - k);
				awe_write_dram(c);
			}
			if (sp->mode_flags & AWE_SAMPLE_BIDIR_LOOP) {
				sp->end += looplen;
			} else {
				sp->start += looplen;
				sp->end += looplen;
			}
		}
	}
	readbuf_end();

	/* if no blank loop is attached in the sample, add it */
	if (sp->mode_flags & AWE_SAMPLE_NO_BLANK) {
		for (i = 0; i < BLANK_LOOP_SIZE; i++)
			awe_write_dram(0);
		if (sp->mode_flags & AWE_SAMPLE_SINGLESHOT) {
			sp->loopstart = sp->end + BLANK_LOOP_START;
			sp->loopend = sp->end + BLANK_LOOP_END;
		}
	}

	awe_close_dram();

	/* initialize FM */
	awe_init_fm();

	return truesize;
}


/*----------------------------------------------------------------*/

#ifdef AWE_HAS_GUS_COMPATIBILITY

/* calculate GUS envelope time:
 * is this correct?  i have no idea..
 */
static int
calc_gus_envelope_time(int rate, int start, int end)
{
	int r, p, t;
	r = (3 - ((rate >> 6) & 3)) * 3;
	p = rate & 0x3f;
	t = end - start;
	if (t < 0) t = -t;
	if (13 > r)
		t = t << (13 - r);
	else
		t = t >> (r - 13);
	return (t * 10) / (p * 441);
}

#define calc_gus_sustain(val)  (0x7f - vol_table[(val)/2])
#define calc_gus_attenuation(val)	vol_table[(val)/2]

/* load GUS patch */
static int
awe_load_guspatch(const char __user *addr, int offs, int size, int pmgr_flag)
{
	struct patch_info patch;
	awe_voice_info *rec;
	awe_sample_info *smp;
	awe_voice_list *vrec;
	awe_sample_list *smprec;
	int sizeof_patch;
	int note, rc;
	sf_list *sf;

	sizeof_patch = (int)((long)&patch.data[0] - (long)&patch); /* header size */
	if (size < sizeof_patch) {
		printk(KERN_WARNING "AWE32 Error: Patch header too short\n");
		return -EINVAL;
	}
	if (copy_from_user(((char*)&patch) + offs, addr + offs, sizeof_patch - offs))
		return -EFAULT;
	size -= sizeof_patch;
	if (size < patch.len) {
		printk(KERN_WARNING "AWE32 Error: Patch record too short (%d<%d)\n",
		       size, patch.len);
		return -EINVAL;
	}
	if ((sf = check_patch_opened(AWE_PAT_TYPE_GUS, NULL)) == NULL)
		return -ENOMEM;
	if ((smprec = alloc_new_sample()) == NULL)
		return -ENOMEM;
	if ((vrec = alloc_new_info()) == NULL) {
		kfree(smprec);
		return -ENOMEM;
	}

	smp = &smprec->v;
	smp->sample = sf->num_sample;
	smp->start = 0;
	smp->end = patch.len;
	smp->loopstart = patch.loop_start;
	smp->loopend = patch.loop_end;
	smp->size = patch.len;

	/* set up mode flags */
	smp->mode_flags = 0;
	if (!(patch.mode & WAVE_16_BITS))
		smp->mode_flags |= AWE_SAMPLE_8BITS;
	if (patch.mode & WAVE_UNSIGNED)
		smp->mode_flags |= AWE_SAMPLE_UNSIGNED;
	smp->mode_flags |= AWE_SAMPLE_NO_BLANK;
	if (!(patch.mode & (WAVE_LOOPING|WAVE_BIDIR_LOOP|WAVE_LOOP_BACK)))
		smp->mode_flags |= AWE_SAMPLE_SINGLESHOT;
	if (patch.mode & WAVE_BIDIR_LOOP)
		smp->mode_flags |= AWE_SAMPLE_BIDIR_LOOP;
	if (patch.mode & WAVE_LOOP_BACK)
		smp->mode_flags |= AWE_SAMPLE_REVERSE_LOOP;

	DEBUG(0,printk("AWE32: [sample %d mode %x]\n", patch.instr_no, smp->mode_flags));
	if (patch.mode & WAVE_16_BITS) {
		/* convert to word offsets */
		smp->size /= 2;
		smp->end /= 2;
		smp->loopstart /= 2;
		smp->loopend /= 2;
	}
	smp->checksum_flag = 0;
	smp->checksum = 0;

	if ((rc = awe_write_wave_data(addr, sizeof_patch, smprec, -1)) < 0)
		return rc;
	sf->mem_ptr += rc;
	add_sf_sample(sf, smprec);

	/* set up voice info */
	rec = &vrec->v;
	awe_init_voice_info(rec);
	rec->sample = sf->num_info; /* the last sample */
	rec->rate_offset = calc_rate_offset(patch.base_freq);
	note = freq_to_note(patch.base_note);
	rec->root = note / 100;
	rec->tune = -(note % 100);
	rec->low = freq_to_note(patch.low_note) / 100;
	rec->high = freq_to_note(patch.high_note) / 100;
	DEBUG(1,printk("AWE32: [gus base offset=%d, note=%d, range=%d-%d(%d-%d)]\n",
		       rec->rate_offset, note,
		       rec->low, rec->high,
	      patch.low_note, patch.high_note));
	/* panning position; -128 - 127 => 0-127 */
	rec->pan = (patch.panning + 128) / 2;

	/* detuning is ignored */
	/* 6points volume envelope */
	if (patch.mode & WAVE_ENVELOPES) {
		int attack, hold, decay, release;
		attack = calc_gus_envelope_time
			(patch.env_rate[0], 0, patch.env_offset[0]);
		hold = calc_gus_envelope_time
			(patch.env_rate[1], patch.env_offset[0],
			 patch.env_offset[1]);
		decay = calc_gus_envelope_time
			(patch.env_rate[2], patch.env_offset[1],
			 patch.env_offset[2]);
		release = calc_gus_envelope_time
			(patch.env_rate[3], patch.env_offset[1],
			 patch.env_offset[4]);
		release += calc_gus_envelope_time
			(patch.env_rate[4], patch.env_offset[3],
			 patch.env_offset[4]);
		release += calc_gus_envelope_time
			(patch.env_rate[5], patch.env_offset[4],
			 patch.env_offset[5]);
		rec->parm.volatkhld = (calc_parm_hold(hold) << 8) |
			calc_parm_attack(attack);
		rec->parm.voldcysus = (calc_gus_sustain(patch.env_offset[2]) << 8) |
			calc_parm_decay(decay);
		rec->parm.volrelease = 0x8000 | calc_parm_decay(release);
		DEBUG(2,printk("AWE32: [gusenv atk=%d, hld=%d, dcy=%d, rel=%d]\n", attack, hold, decay, release));
		rec->attenuation = calc_gus_attenuation(patch.env_offset[0]);
	}

	/* tremolo effect */
	if (patch.mode & WAVE_TREMOLO) {
		int rate = (patch.tremolo_rate * 1000 / 38) / 42;
		rec->parm.tremfrq = ((patch.tremolo_depth / 2) << 8) | rate;
		DEBUG(2,printk("AWE32: [gusenv tremolo rate=%d, dep=%d, tremfrq=%x]\n",
			       patch.tremolo_rate, patch.tremolo_depth,
			       rec->parm.tremfrq));
	}
	/* vibrato effect */
	if (patch.mode & WAVE_VIBRATO) {
		int rate = (patch.vibrato_rate * 1000 / 38) / 42;
		rec->parm.fm2frq2 = ((patch.vibrato_depth / 6) << 8) | rate;
		DEBUG(2,printk("AWE32: [gusenv vibrato rate=%d, dep=%d, tremfrq=%x]\n",
			       patch.tremolo_rate, patch.tremolo_depth,
			       rec->parm.tremfrq));
	}
	
	/* scale_freq, scale_factor, volume, and fractions not implemented */

	/* append to the tail of the list */
	vrec->bank = ctrls[AWE_MD_GUS_BANK];
	vrec->instr = patch.instr_no;
	vrec->disabled = FALSE;
	vrec->type = V_ST_NORMAL;

	add_sf_info(sf, vrec);
	add_info_list(vrec);

	/* set the voice index */
	awe_set_sample(vrec);

	return 0;
}

#endif  /* AWE_HAS_GUS_COMPATIBILITY */

/*
 * sample and voice list handlers
 */

/* append this to the current sf list */
static void add_sf_info(sf_list *sf, awe_voice_list *rec)
{
	if (sf == NULL)
		return;
	rec->holder = sf;
	rec->v.sf_id = sf->sf_id;
	if (sf->last_infos)
		sf->last_infos->next = rec;
	else
		sf->infos = rec;
	sf->last_infos = rec;
	rec->next = NULL;
	sf->num_info++;
}

/* prepend this sample to sf list */
static void add_sf_sample(sf_list *sf, awe_sample_list *rec)
{
	if (sf == NULL)
		return;
	rec->holder = sf;
	rec->v.sf_id = sf->sf_id;
	if (sf->last_samples)
		sf->last_samples->next = rec;
	else
		sf->samples = rec;
	sf->last_samples = rec;
	rec->next = NULL;
	sf->num_sample++;
}

/* purge the old records which don't belong with the same file id */
static void purge_old_list(awe_voice_list *rec, awe_voice_list *next)
{
	rec->next_instr = next;
	if (rec->bank == AWE_DRUM_BANK) {
		/* remove samples with the same note range */
		awe_voice_list *cur, *prev = rec;
		int low = rec->v.low;
		int high = rec->v.high;
		for (cur = next; cur; cur = cur->next_instr) {
			if (cur->v.low == low &&
			    cur->v.high == high &&
			    ! is_identical_holder(cur->holder, rec->holder))
				prev->next_instr = cur->next_instr;
			else
				prev = cur;
		}
	} else {
		if (! is_identical_holder(next->holder, rec->holder))
			/* remove all samples */
			rec->next_instr = NULL;
	}
}

/* prepend to top of the preset table */
static void add_info_list(awe_voice_list *rec)
{
	awe_voice_list *prev, *cur;
	int key;

	if (rec->disabled)
		return;

	key = awe_search_key(rec->bank, rec->instr, rec->v.low);
	prev = NULL;
	for (cur = preset_table[key]; cur; cur = cur->next_bank) {
		/* search the first record with the same bank number */
		if (cur->instr == rec->instr && cur->bank == rec->bank) {
			/* replace the list with the new record */
			rec->next_bank = cur->next_bank;
			if (prev)
				prev->next_bank = rec;
			else
				preset_table[key] = rec;
			purge_old_list(rec, cur);
			return;
		}
		prev = cur;
	}

	/* this is the first bank record.. just add this */
	rec->next_instr = NULL;
	rec->next_bank = preset_table[key];
	preset_table[key] = rec;
}

/* remove samples later than the specified sf_id */
static void
awe_remove_samples(int sf_id)
{
	sf_list *p, *prev;

	if (sf_id <= 0) {
		awe_reset_samples();
		return;
	}
	/* already removed? */
	if (current_sf_id <= sf_id)
		return;

	for (p = sftail; p; p = prev) {
		if (p->sf_id <= sf_id)
			break;
		prev = p->prev;
		awe_free_sf(p);
	}
	sftail = p;
	if (sftail) {
		sf_id = sftail->sf_id;
		sftail->next = NULL;
	} else {
		sf_id = 0;
		sfhead = NULL;
	}
	current_sf_id = sf_id;
	if (locked_sf_id > sf_id)
		locked_sf_id = sf_id;

	rebuild_preset_list();
}

/* rebuild preset search list */
static void rebuild_preset_list(void)
{
	sf_list *p;
	awe_voice_list *rec;

	memset(preset_table, 0, sizeof(preset_table));

	for (p = sfhead; p; p = p->next) {
		for (rec = p->infos; rec; rec = rec->next)
			add_info_list(rec);
	}
}

/* compare the given sf_id pair */
static int is_identical_holder(sf_list *sf1, sf_list *sf2)
{
	if (sf1 == NULL || sf2 == NULL)
		return FALSE;
	if (sf1 == sf2)
		return TRUE;
#ifdef AWE_ALLOW_SAMPLE_SHARING
	{
		/* compare with the sharing id */
		sf_list *p;
		int counter = 0;
		if (sf1->sf_id < sf2->sf_id) { /* make sure id1 > id2 */
			sf_list *tmp; tmp = sf1; sf1 = sf2; sf2 = tmp;
		}
		for (p = sf1->shared; p; p = p->shared) {
			if (counter++ > current_sf_id)
				break; /* strange sharing loop.. quit */
			if (p == sf2)
				return TRUE;
		}
	}
#endif /* allow sharing */
	return FALSE;
}

/* search the sample index matching with the given sample id */
static awe_sample_list *
search_sample_index(sf_list *sf, int sample)
{
	awe_sample_list *p;
#ifdef AWE_ALLOW_SAMPLE_SHARING
	int counter = 0;
	while (sf) {
		for (p = sf->samples; p; p = p->next) {
			if (p->v.sample == sample)
				return p;
		}
		sf = sf->shared;
		if (counter++ > current_sf_id)
			break; /* strange sharing loop.. quit */
	}
#else
	if (sf) {
		for (p = sf->samples; p; p = p->next) {
			if (p->v.sample == sample)
				return p;
		}
	}
#endif
	return NULL;
}

/* search the specified sample */
/* non-zero = found */
static short
awe_set_sample(awe_voice_list *rec)
{
	awe_sample_list *smp;
	awe_voice_info *vp = &rec->v;

	vp->index = 0;
	if ((smp = search_sample_index(rec->holder, vp->sample)) == NULL)
		return 0;

	/* set the actual sample offsets */
	vp->start += smp->v.start;
	vp->end += smp->v.end;
	vp->loopstart += smp->v.loopstart;
	vp->loopend += smp->v.loopend;
	/* copy mode flags */
	vp->mode = smp->v.mode_flags;
	/* set flag */
	vp->index = 1;

	return 1;
}


/*
 * voice allocation
 */

/* look for all voices associated with the specified note & velocity */
static int
awe_search_multi_voices(awe_voice_list *rec, int note, int velocity,
			awe_voice_info **vlist)
{
	int nvoices;

	nvoices = 0;
	for (; rec; rec = rec->next_instr) {
		if (note >= rec->v.low &&
		    note <= rec->v.high &&
		    velocity >= rec->v.vellow &&
		    velocity <= rec->v.velhigh) {
			if (rec->type == V_ST_MAPPED) {
				/* mapper */
				vlist[0] = &rec->v;
				return -1;
			}
			vlist[nvoices++] = &rec->v;
			if (nvoices >= AWE_MAX_VOICES)
				break;
		}
	}
	return nvoices;	
}

/* store the voice list from the specified note and velocity.
   if the preset is mapped, seek for the destination preset, and rewrite
   the note number if necessary.
   */
static int
really_alloc_voices(int bank, int instr, int *note, int velocity, awe_voice_info **vlist)
{
	int nvoices;
	awe_voice_list *vrec;
	int level = 0;

	for (;;) {
		vrec = awe_search_instr(bank, instr, *note);
		nvoices = awe_search_multi_voices(vrec, *note, velocity, vlist);
		if (nvoices == 0) {
			if (bank == AWE_DRUM_BANK)
				/* search default drumset */
				vrec = awe_search_instr(bank, ctrls[AWE_MD_DEF_DRUM], *note);
			else
				/* search default preset */
				vrec = awe_search_instr(ctrls[AWE_MD_DEF_BANK], instr, *note);
			nvoices = awe_search_multi_voices(vrec, *note, velocity, vlist);
		}
		if (nvoices == 0) {
			if (bank == AWE_DRUM_BANK && ctrls[AWE_MD_DEF_DRUM] != 0)
				/* search default drumset */
				vrec = awe_search_instr(bank, 0, *note);
			else if (bank != AWE_DRUM_BANK && ctrls[AWE_MD_DEF_BANK] != 0)
				/* search default preset */
				vrec = awe_search_instr(0, instr, *note);
			nvoices = awe_search_multi_voices(vrec, *note, velocity, vlist);
		}
		if (nvoices < 0) { /* mapping */
			int key = vlist[0]->fixkey;
			instr = vlist[0]->start;
			bank = vlist[0]->end;
			if (level++ > 5) {
				printk(KERN_ERR "AWE32: too deep mapping level\n");
				return 0;
			}
			if (key >= 0)
				*note = key;
		} else
			break;
	}

	return nvoices;
}

/* allocate voices corresponding note and velocity; supports multiple insts. */
static void
awe_alloc_multi_voices(int ch, int note, int velocity, int key)
{
	int i, v, nvoices, bank;
	awe_voice_info *vlist[AWE_MAX_VOICES];

	if (MULTI_LAYER_MODE() && IS_DRUM_CHANNEL(ch))
		bank = AWE_DRUM_BANK; /* always search drumset */
	else
		bank = channels[ch].bank;

	/* check the possible voices; note may be changeable if mapped */
	nvoices = really_alloc_voices(bank, channels[ch].instr,
				      &note, velocity, vlist);

	/* set the voices */
	current_alloc_time++;
	for (i = 0; i < nvoices; i++) {
		v = awe_clear_voice();
		voices[v].key = key;
		voices[v].ch = ch;
		voices[v].note = note;
		voices[v].velocity = velocity;
		voices[v].time = current_alloc_time;
		voices[v].cinfo = &channels[ch];
		voices[v].sample = vlist[i];
		voices[v].state = AWE_ST_MARK;
		voices[v].layer = nvoices - i - 1;  /* in reverse order */
	}

	/* clear the mark in allocated voices */
	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].state == AWE_ST_MARK)
			voices[i].state = AWE_ST_OFF;
			
	}
}


/* search an empty voice.
   if no empty voice is found, at least terminate a voice
   */
static int
awe_clear_voice(void)
{
	enum {
		OFF=0, RELEASED, SUSTAINED, PLAYING, END
	};
	struct voice_candidate_t {
		int best;
		int time;
		int vtarget;
	} candidate[END];
	int i, type, vtarget;

	vtarget = 0xffff;
	for (type = OFF; type < END; type++) {
		candidate[type].best = -1;
		candidate[type].time = current_alloc_time + 1;
		candidate[type].vtarget = vtarget;
	}

	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].state & AWE_ST_OFF)
			type = OFF;
		else if (voices[i].state & AWE_ST_RELEASED)
			type = RELEASED;
		else if (voices[i].state & AWE_ST_SUSTAINED)
			type = SUSTAINED;
		else if (voices[i].state & ~AWE_ST_MARK)
			type = PLAYING;
		else
			continue;
#ifdef AWE_CHECK_VTARGET
		/* get current volume */
		vtarget = (awe_peek_dw(AWE_VTFT(i)) >> 16) & 0xffff;
#endif
		if (candidate[type].best < 0 ||
		    vtarget < candidate[type].vtarget ||
		    (vtarget == candidate[type].vtarget &&
		     voices[i].time < candidate[type].time)) {
			candidate[type].best = i;
			candidate[type].time = voices[i].time;
			candidate[type].vtarget = vtarget;
		}
	}

	for (type = OFF; type < END; type++) {
		if ((i = candidate[type].best) >= 0) {
			if (voices[i].state != AWE_ST_OFF)
				awe_terminate(i);
			awe_voice_init(i, TRUE);
			return i;
		}
	}
	return 0;
}


/* search sample for the specified note & velocity and set it on the voice;
 * note that voice is the voice index (not channel index)
 */
static void
awe_alloc_one_voice(int voice, int note, int velocity)
{
	int ch, nvoices, bank;
	awe_voice_info *vlist[AWE_MAX_VOICES];

	ch = voices[voice].ch;
	if (MULTI_LAYER_MODE() && IS_DRUM_CHANNEL(voice))
		bank = AWE_DRUM_BANK; /* always search drumset */
	else
		bank = voices[voice].cinfo->bank;

	nvoices = really_alloc_voices(bank, voices[voice].cinfo->instr,
				      &note, velocity, vlist);
	if (nvoices > 0) {
		voices[voice].time = ++current_alloc_time;
		voices[voice].sample = vlist[0]; /* use the first one */
		voices[voice].layer = 0;
		voices[voice].note = note;
		voices[voice].velocity = velocity;
	}
}


/*
 * sequencer2 functions
 */

/* search an empty voice; used by sequencer2 */
static int
awe_alloc(int dev, int chn, int note, struct voice_alloc_info *alloc)
{
	playing_mode = AWE_PLAY_MULTI2;
	awe_info.nr_voices = AWE_MAX_CHANNELS;
	return awe_clear_voice();
}


/* set up voice; used by sequencer2 */
static void
awe_setup_voice(int dev, int voice, int chn)
{
	struct channel_info *info;
	if (synth_devs[dev] == NULL ||
	    (info = &synth_devs[dev]->chn_info[chn]) == NULL)
		return;

	if (voice < 0 || voice >= awe_max_voices)
		return;

	DEBUG(2,printk("AWE32: [setup(%d) ch=%d]\n", voice, chn));
	channels[chn].expression_vol = info->controllers[CTL_EXPRESSION];
	channels[chn].main_vol = info->controllers[CTL_MAIN_VOLUME];
	channels[chn].panning =
		info->controllers[CTL_PAN] * 2 - 128; /* signed 8bit */
	channels[chn].bender = info->bender_value; /* zero center */
	channels[chn].bank = info->controllers[CTL_BANK_SELECT];
	channels[chn].sustained = info->controllers[CTL_SUSTAIN];
	if (info->controllers[CTL_EXT_EFF_DEPTH]) {
		FX_SET(&channels[chn].fx, AWE_FX_REVERB,
		       info->controllers[CTL_EXT_EFF_DEPTH] * 2);
	}
	if (info->controllers[CTL_CHORUS_DEPTH]) {
		FX_SET(&channels[chn].fx, AWE_FX_CHORUS,
		       info->controllers[CTL_CHORUS_DEPTH] * 2);
	}
	awe_set_instr(dev, chn, info->pgm_num);
}


#ifdef CONFIG_AWE32_MIXER
/*
 * AWE32 mixer device control
 */

static int awe_mixer_ioctl(int dev, unsigned int cmd, void __user *arg);

static int my_mixerdev = -1;

static struct mixer_operations awe_mixer_operations = {
	.owner	= THIS_MODULE,
	.id	= "AWE",
	.name	= "AWE32 Equalizer",
	.ioctl	= awe_mixer_ioctl,
};

static void __init attach_mixer(void)
{
	if ((my_mixerdev = sound_alloc_mixerdev()) >= 0) {
		mixer_devs[my_mixerdev] = &awe_mixer_operations;
	}
}

static void unload_mixer(void)
{
	if (my_mixerdev >= 0)
		sound_unload_mixerdev(my_mixerdev);
}

static int
awe_mixer_ioctl(int dev, unsigned int cmd, void __user * arg)
{
	int i, level, value;

	if (((cmd >> 8) & 0xff) != 'M')
		return -EINVAL;

	if (get_user(level, (int __user *)arg))
		return -EFAULT;
	level = ((level & 0xff) + (level >> 8)) / 2;
	DEBUG(0,printk("AWEMix: cmd=%x val=%d\n", cmd & 0xff, level));

	if (_SIOC_DIR(cmd) & _SIOC_WRITE) {
		switch (cmd & 0xff) {
		case SOUND_MIXER_BASS:
			value = level * 12 / 100;
			if (value >= 12)
				value = 11;
			ctrls[AWE_MD_BASS_LEVEL] = value;
			awe_update_equalizer();
			break;
		case SOUND_MIXER_TREBLE:
			value = level * 12 / 100;
			if (value >= 12)
				value = 11;
			ctrls[AWE_MD_TREBLE_LEVEL] = value;
			awe_update_equalizer();
			break;
		case SOUND_MIXER_VOLUME:
			level = level * 127 / 100;
			if (level >= 128) level = 127;
			atten_relative = FALSE;
			atten_offset = vol_table[level];
			awe_update_volume();
			break;
		}
	}
	switch (cmd & 0xff) {
	case SOUND_MIXER_BASS:
		level = ctrls[AWE_MD_BASS_LEVEL] * 100 / 24;
		level = (level << 8) | level;
		break;
	case SOUND_MIXER_TREBLE:
		level = ctrls[AWE_MD_TREBLE_LEVEL] * 100 / 24;
		level = (level << 8) | level;
		break;
	case SOUND_MIXER_VOLUME:
		value = atten_offset;
		if (atten_relative)
			value += ctrls[AWE_MD_ZERO_ATTEN];
		for (i = 127; i > 0; i--) {
			if (value <= vol_table[i])
				break;
		}
		level = i * 100 / 127;
		level = (level << 8) | level;
		break;
	case SOUND_MIXER_DEVMASK:
		level = SOUND_MASK_BASS|SOUND_MASK_TREBLE|SOUND_MASK_VOLUME;
		break;
	default:
		level = 0;
		break;
	}
	if (put_user(level, (int __user *)arg))
		return -EFAULT;
	return level;
}
#endif /* CONFIG_AWE32_MIXER */


/*
 * initialization of Emu8000
 */

/* intiailize audio channels */
static void
awe_init_audio(void)
{
	int ch;

	/* turn off envelope engines */
	for (ch = 0; ch < AWE_MAX_VOICES; ch++) {
		awe_poke(AWE_DCYSUSV(ch), 0x80);
	}
  
	/* reset all other parameters to zero */
	for (ch = 0; ch < AWE_MAX_VOICES; ch++) {
		awe_poke(AWE_ENVVOL(ch), 0);
		awe_poke(AWE_ENVVAL(ch), 0);
		awe_poke(AWE_DCYSUS(ch), 0);
		awe_poke(AWE_ATKHLDV(ch), 0);
		awe_poke(AWE_LFO1VAL(ch), 0);
		awe_poke(AWE_ATKHLD(ch), 0);
		awe_poke(AWE_LFO2VAL(ch), 0);
		awe_poke(AWE_IP(ch), 0);
		awe_poke(AWE_IFATN(ch), 0);
		awe_poke(AWE_PEFE(ch), 0);
		awe_poke(AWE_FMMOD(ch), 0);
		awe_poke(AWE_TREMFRQ(ch), 0);
		awe_poke(AWE_FM2FRQ2(ch), 0);
		awe_poke_dw(AWE_PTRX(ch), 0);
		awe_poke_dw(AWE_VTFT(ch), 0);
		awe_poke_dw(AWE_PSST(ch), 0);
		awe_poke_dw(AWE_CSL(ch), 0);
		awe_poke_dw(AWE_CCCA(ch), 0);
	}

	for (ch = 0; ch < AWE_MAX_VOICES; ch++) {
		awe_poke_dw(AWE_CPF(ch), 0);
		awe_poke_dw(AWE_CVCF(ch), 0);
	}
}


/* initialize DMA address */
static void
awe_init_dma(void)
{
	awe_poke_dw(AWE_SMALR, 0);
	awe_poke_dw(AWE_SMARR, 0);
	awe_poke_dw(AWE_SMALW, 0);
	awe_poke_dw(AWE_SMARW, 0);
}


/* initialization arrays; from ADIP */

static unsigned short init1[128] = {
	0x03ff, 0x0030,  0x07ff, 0x0130, 0x0bff, 0x0230,  0x0fff, 0x0330,
	0x13ff, 0x0430,  0x17ff, 0x0530, 0x1bff, 0x0630,  0x1fff, 0x0730,
	0x23ff, 0x0830,  0x27ff, 0x0930, 0x2bff, 0x0a30,  0x2fff, 0x0b30,
	0x33ff, 0x0c30,  0x37ff, 0x0d30, 0x3bff, 0x0e30,  0x3fff, 0x0f30,

	0x43ff, 0x0030,  0x47ff, 0x0130, 0x4bff, 0x0230,  0x4fff, 0x0330,
	0x53ff, 0x0430,  0x57ff, 0x0530, 0x5bff, 0x0630,  0x5fff, 0x0730,
	0x63ff, 0x0830,  0x67ff, 0x0930, 0x6bff, 0x0a30,  0x6fff, 0x0b30,
	0x73ff, 0x0c30,  0x77ff, 0x0d30, 0x7bff, 0x0e30,  0x7fff, 0x0f30,

	0x83ff, 0x0030,  0x87ff, 0x0130, 0x8bff, 0x0230,  0x8fff, 0x0330,
	0x93ff, 0x0430,  0x97ff, 0x0530, 0x9bff, 0x0630,  0x9fff, 0x0730,
	0xa3ff, 0x0830,  0xa7ff, 0x0930, 0xabff, 0x0a30,  0xafff, 0x0b30,
	0xb3ff, 0x0c30,  0xb7ff, 0x0d30, 0xbbff, 0x0e30,  0xbfff, 0x0f30,

	0xc3ff, 0x0030,  0xc7ff, 0x0130, 0xcbff, 0x0230,  0xcfff, 0x0330,
	0xd3ff, 0x0430,  0xd7ff, 0x0530, 0xdbff, 0x0630,  0xdfff, 0x0730,
	0xe3ff, 0x0830,  0xe7ff, 0x0930, 0xebff, 0x0a30,  0xefff, 0x0b30,
	0xf3ff, 0x0c30,  0xf7ff, 0x0d30, 0xfbff, 0x0e30,  0xffff, 0x0f30,
};

static unsigned short init2[128] = {
	0x03ff, 0x8030, 0x07ff, 0x8130, 0x0bff, 0x8230, 0x0fff, 0x8330,
	0x13ff, 0x8430, 0x17ff, 0x8530, 0x1bff, 0x8630, 0x1fff, 0x8730,
	0x23ff, 0x8830, 0x27ff, 0x8930, 0x2bff, 0x8a30, 0x2fff, 0x8b30,
	0x33ff, 0x8c30, 0x37ff, 0x8d30, 0x3bff, 0x8e30, 0x3fff, 0x8f30,

	0x43ff, 0x8030, 0x47ff, 0x8130, 0x4bff, 0x8230, 0x4fff, 0x8330,
	0x53ff, 0x8430, 0x57ff, 0x8530, 0x5bff, 0x8630, 0x5fff, 0x8730,
	0x63ff, 0x8830, 0x67ff, 0x8930, 0x6bff, 0x8a30, 0x6fff, 0x8b30,
	0x73ff, 0x8c30, 0x77ff, 0x8d30, 0x7bff, 0x8e30, 0x7fff, 0x8f30,

	0x83ff, 0x8030, 0x87ff, 0x8130, 0x8bff, 0x8230, 0x8fff, 0x8330,
	0x93ff, 0x8430, 0x97ff, 0x8530, 0x9bff, 0x8630, 0x9fff, 0x8730,
	0xa3ff, 0x8830, 0xa7ff, 0x8930, 0xabff, 0x8a30, 0xafff, 0x8b30,
	0xb3ff, 0x8c30, 0xb7ff, 0x8d30, 0xbbff, 0x8e30, 0xbfff, 0x8f30,

	0xc3ff, 0x8030, 0xc7ff, 0x8130, 0xcbff, 0x8230, 0xcfff, 0x8330,
	0xd3ff, 0x8430, 0xd7ff, 0x8530, 0xdbff, 0x8630, 0xdfff, 0x8730,
	0xe3ff, 0x8830, 0xe7ff, 0x8930, 0xebff, 0x8a30, 0xefff, 0x8b30,
	0xf3ff, 0x8c30, 0xf7ff, 0x8d30, 0xfbff, 0x8e30, 0xffff, 0x8f30,
};

static unsigned short init3[128] = {
	0x0C10, 0x8470, 0x14FE, 0xB488, 0x167F, 0xA470, 0x18E7, 0x84B5,
	0x1B6E, 0x842A, 0x1F1D, 0x852A, 0x0DA3, 0x8F7C, 0x167E, 0xF254,
	0x0000, 0x842A, 0x0001, 0x852A, 0x18E6, 0x8BAA, 0x1B6D, 0xF234,
	0x229F, 0x8429, 0x2746, 0x8529, 0x1F1C, 0x86E7, 0x229E, 0xF224,

	0x0DA4, 0x8429, 0x2C29, 0x8529, 0x2745, 0x87F6, 0x2C28, 0xF254,
	0x383B, 0x8428, 0x320F, 0x8528, 0x320E, 0x8F02, 0x1341, 0xF264,
	0x3EB6, 0x8428, 0x3EB9, 0x8528, 0x383A, 0x8FA9, 0x3EB5, 0xF294,
	0x3EB7, 0x8474, 0x3EBA, 0x8575, 0x3EB8, 0xC4C3, 0x3EBB, 0xC5C3,

	0x0000, 0xA404, 0x0001, 0xA504, 0x141F, 0x8671, 0x14FD, 0x8287,
	0x3EBC, 0xE610, 0x3EC8, 0x8C7B, 0x031A, 0x87E6, 0x3EC8, 0x86F7,
	0x3EC0, 0x821E, 0x3EBE, 0xD208, 0x3EBD, 0x821F, 0x3ECA, 0x8386,
	0x3EC1, 0x8C03, 0x3EC9, 0x831E, 0x3ECA, 0x8C4C, 0x3EBF, 0x8C55,

	0x3EC9, 0xC208, 0x3EC4, 0xBC84, 0x3EC8, 0x8EAD, 0x3EC8, 0xD308,
	0x3EC2, 0x8F7E, 0x3ECB, 0x8219, 0x3ECB, 0xD26E, 0x3EC5, 0x831F,
	0x3EC6, 0xC308, 0x3EC3, 0xB2FF, 0x3EC9, 0x8265, 0x3EC9, 0x8319,
	0x1342, 0xD36E, 0x3EC7, 0xB3FF, 0x0000, 0x8365, 0x1420, 0x9570,
};

static unsigned short init4[128] = {
	0x0C10, 0x8470, 0x14FE, 0xB488, 0x167F, 0xA470, 0x18E7, 0x84B5,
	0x1B6E, 0x842A, 0x1F1D, 0x852A, 0x0DA3, 0x0F7C, 0x167E, 0x7254,
	0x0000, 0x842A, 0x0001, 0x852A, 0x18E6, 0x0BAA, 0x1B6D, 0x7234,
	0x229F, 0x8429, 0x2746, 0x8529, 0x1F1C, 0x06E7, 0x229E, 0x7224,

	0x0DA4, 0x8429, 0x2C29, 0x8529, 0x2745, 0x07F6, 0x2C28, 0x7254,
	0x383B, 0x8428, 0x320F, 0x8528, 0x320E, 0x0F02, 0x1341, 0x7264,
	0x3EB6, 0x8428, 0x3EB9, 0x8528, 0x383A, 0x0FA9, 0x3EB5, 0x7294,
	0x3EB7, 0x8474, 0x3EBA, 0x8575, 0x3EB8, 0x44C3, 0x3EBB, 0x45C3,

	0x0000, 0xA404, 0x0001, 0xA504, 0x141F, 0x0671, 0x14FD, 0x0287,
	0x3EBC, 0xE610, 0x3EC8, 0x0C7B, 0x031A, 0x07E6, 0x3EC8, 0x86F7,
	0x3EC0, 0x821E, 0x3EBE, 0xD208, 0x3EBD, 0x021F, 0x3ECA, 0x0386,
	0x3EC1, 0x0C03, 0x3EC9, 0x031E, 0x3ECA, 0x8C4C, 0x3EBF, 0x0C55,

	0x3EC9, 0xC208, 0x3EC4, 0xBC84, 0x3EC8, 0x0EAD, 0x3EC8, 0xD308,
	0x3EC2, 0x8F7E, 0x3ECB, 0x0219, 0x3ECB, 0xD26E, 0x3EC5, 0x031F,
	0x3EC6, 0xC308, 0x3EC3, 0x32FF, 0x3EC9, 0x0265, 0x3EC9, 0x8319,
	0x1342, 0xD36E, 0x3EC7, 0x33FF, 0x0000, 0x8365, 0x1420, 0x9570,
};


/* send initialization arrays to start up */
static void
awe_init_array(void)
{
	awe_send_array(init1);
	awe_wait(1024);
	awe_send_array(init2);
	awe_send_array(init3);
	awe_poke_dw(AWE_HWCF4, 0);
	awe_poke_dw(AWE_HWCF5, 0x83);
	awe_poke_dw(AWE_HWCF6, 0x8000);
	awe_send_array(init4);
}

/* send an initialization array */
static void
awe_send_array(unsigned short *data)
{
	int i;
	unsigned short *p;

	p = data;
	for (i = 0; i < AWE_MAX_VOICES; i++, p++)
		awe_poke(AWE_INIT1(i), *p);
	for (i = 0; i < AWE_MAX_VOICES; i++, p++)
		awe_poke(AWE_INIT2(i), *p);
	for (i = 0; i < AWE_MAX_VOICES; i++, p++)
		awe_poke(AWE_INIT3(i), *p);
	for (i = 0; i < AWE_MAX_VOICES; i++, p++)
		awe_poke(AWE_INIT4(i), *p);
}


/*
 * set up awe32 channels to some known state.
 */

/* set the envelope & LFO parameters to the default values; see ADIP */
static void
awe_tweak_voice(int i)
{
	/* set all mod/vol envelope shape to minimum */
	awe_poke(AWE_ENVVOL(i), 0x8000);
	awe_poke(AWE_ENVVAL(i), 0x8000);
	awe_poke(AWE_DCYSUS(i), 0x7F7F);
	awe_poke(AWE_ATKHLDV(i), 0x7F7F);
	awe_poke(AWE_ATKHLD(i), 0x7F7F);
	awe_poke(AWE_PEFE(i), 0);  /* mod envelope height to zero */
	awe_poke(AWE_LFO1VAL(i), 0x8000); /* no delay for LFO1 */
	awe_poke(AWE_LFO2VAL(i), 0x8000);
	awe_poke(AWE_IP(i), 0xE000);	/* no pitch shift */
	awe_poke(AWE_IFATN(i), 0xFF00);	/* volume to minimum */
	awe_poke(AWE_FMMOD(i), 0);
	awe_poke(AWE_TREMFRQ(i), 0);
	awe_poke(AWE_FM2FRQ2(i), 0);
}

static void
awe_tweak(void)
{
	int i;
	/* reset all channels */
	for (i = 0; i < awe_max_voices; i++)
		awe_tweak_voice(i);
}


/*
 *  initializes the FM section of AWE32;
 *   see Vince Vu's unofficial AWE32 programming guide
 */

static void
awe_init_fm(void)
{
#ifndef AWE_ALWAYS_INIT_FM
	/* if no extended memory is on board.. */
	if (memsize <= 0)
		return;
#endif
	DEBUG(3,printk("AWE32: initializing FM\n"));

	/* Initialize the last two channels for DRAM refresh and producing
	   the reverb and chorus effects for Yamaha OPL-3 synthesizer */

	/* 31: FM left channel, 0xffffe0-0xffffe8 */
	awe_poke(AWE_DCYSUSV(30), 0x80);
	awe_poke_dw(AWE_PSST(30), 0xFFFFFFE0); /* full left */
	awe_poke_dw(AWE_CSL(30), 0x00FFFFE8 |
		    (DEF_FM_CHORUS_DEPTH << 24));
	awe_poke_dw(AWE_PTRX(30), (DEF_FM_REVERB_DEPTH << 8));
	awe_poke_dw(AWE_CPF(30), 0);
	awe_poke_dw(AWE_CCCA(30), 0x00FFFFE3);

	/* 32: FM right channel, 0xfffff0-0xfffff8 */
	awe_poke(AWE_DCYSUSV(31), 0x80);
	awe_poke_dw(AWE_PSST(31), 0x00FFFFF0); /* full right */
	awe_poke_dw(AWE_CSL(31), 0x00FFFFF8 |
		    (DEF_FM_CHORUS_DEPTH << 24));
	awe_poke_dw(AWE_PTRX(31), (DEF_FM_REVERB_DEPTH << 8));
	awe_poke_dw(AWE_CPF(31), 0x8000);
	awe_poke_dw(AWE_CCCA(31), 0x00FFFFF3);

	/* skew volume & cutoff */
	awe_poke_dw(AWE_VTFT(30), 0x8000FFFF);
	awe_poke_dw(AWE_VTFT(31), 0x8000FFFF);

	voices[30].state = AWE_ST_FM;
	voices[31].state = AWE_ST_FM;

	/* change maximum channels to 30 */
	awe_max_voices = AWE_NORMAL_VOICES;
	if (playing_mode == AWE_PLAY_DIRECT)
		awe_info.nr_voices = awe_max_voices;
	else
		awe_info.nr_voices = AWE_MAX_CHANNELS;
	voice_alloc->max_voice = awe_max_voices;
}

/*
 *  AWE32 DRAM access routines
 */

/* open DRAM write accessing mode */
static int
awe_open_dram_for_write(int offset, int channels)
{
	int vidx[AWE_NORMAL_VOICES];
	int i;

	if (channels < 0 || channels >= AWE_NORMAL_VOICES) {
		channels = AWE_NORMAL_VOICES;
		for (i = 0; i < AWE_NORMAL_VOICES; i++)
			vidx[i] = i;
	} else {
		for (i = 0; i < channels; i++) {
			vidx[i] = awe_clear_voice();
			voices[vidx[i]].state = AWE_ST_MARK;
		}
	}

	/* use all channels for DMA transfer */
	for (i = 0; i < channels; i++) {
		if (vidx[i] < 0) continue;
		awe_poke(AWE_DCYSUSV(vidx[i]), 0x80);
		awe_poke_dw(AWE_VTFT(vidx[i]), 0);
		awe_poke_dw(AWE_CVCF(vidx[i]), 0);
		awe_poke_dw(AWE_PTRX(vidx[i]), 0x40000000);
		awe_poke_dw(AWE_CPF(vidx[i]), 0x40000000);
		awe_poke_dw(AWE_PSST(vidx[i]), 0);
		awe_poke_dw(AWE_CSL(vidx[i]), 0);
		awe_poke_dw(AWE_CCCA(vidx[i]), 0x06000000);
		voices[vidx[i]].state = AWE_ST_DRAM;
	}
	/* point channels 31 & 32 to ROM samples for DRAM refresh */
	awe_poke_dw(AWE_VTFT(30), 0);
	awe_poke_dw(AWE_PSST(30), 0x1d8);
	awe_poke_dw(AWE_CSL(30), 0x1e0);
	awe_poke_dw(AWE_CCCA(30), 0x1d8);
	awe_poke_dw(AWE_VTFT(31), 0);
	awe_poke_dw(AWE_PSST(31), 0x1d8);
	awe_poke_dw(AWE_CSL(31), 0x1e0);
	awe_poke_dw(AWE_CCCA(31), 0x1d8);
	voices[30].state = AWE_ST_FM;
	voices[31].state = AWE_ST_FM;

	/* if full bit is on, not ready to write on */
	if (awe_peek_dw(AWE_SMALW) & 0x80000000) {
		for (i = 0; i < channels; i++) {
			awe_poke_dw(AWE_CCCA(vidx[i]), 0);
			voices[vidx[i]].state = AWE_ST_OFF;
		}
		printk("awe: not ready to write..\n");
		return -EPERM;
	}

	/* set address to write */
	awe_poke_dw(AWE_SMALW, offset);

	return 0;
}

/* open DRAM for RAM size detection */
static void
awe_open_dram_for_check(void)
{
	int i;
	for (i = 0; i < AWE_NORMAL_VOICES; i++) {
		awe_poke(AWE_DCYSUSV(i), 0x80);
		awe_poke_dw(AWE_VTFT(i), 0);
		awe_poke_dw(AWE_CVCF(i), 0);
		awe_poke_dw(AWE_PTRX(i), 0x40000000);
		awe_poke_dw(AWE_CPF(i), 0x40000000);
		awe_poke_dw(AWE_PSST(i), 0);
		awe_poke_dw(AWE_CSL(i), 0);
		if (i & 1) /* DMA write */
			awe_poke_dw(AWE_CCCA(i), 0x06000000);
		else	   /* DMA read */
			awe_poke_dw(AWE_CCCA(i), 0x04000000);
		voices[i].state = AWE_ST_DRAM;
	}
}


/* close dram access */
static void
awe_close_dram(void)
{
	int i;
	/* wait until FULL bit in SMAxW register be false */
	for (i = 0; i < 10000; i++) {
		if (!(awe_peek_dw(AWE_SMALW) & 0x80000000))
			break;
		awe_wait(10);
	}

	for (i = 0; i < AWE_NORMAL_VOICES; i++) {
		if (voices[i].state == AWE_ST_DRAM) {
			awe_poke_dw(AWE_CCCA(i), 0);
			awe_poke(AWE_DCYSUSV(i), 0x807F);
			voices[i].state = AWE_ST_OFF;
		}
	}
}


/*
 * check dram size on AWE board
 */

/* any three numbers you like */
#define UNIQUE_ID1	0x1234
#define UNIQUE_ID2	0x4321
#define UNIQUE_ID3	0xABCD

static void __init
awe_check_dram(void)
{
	if (awe_present) /* already initialized */
		return;

	if (memsize >= 0) { /* given by config file or module option */
		memsize *= 1024; /* convert to Kbytes */
		return;
	}

	awe_open_dram_for_check();

	memsize = 0;

	/* set up unique two id numbers */
	awe_poke_dw(AWE_SMALW, AWE_DRAM_OFFSET);
	awe_poke(AWE_SMLD, UNIQUE_ID1);
	awe_poke(AWE_SMLD, UNIQUE_ID2);

	while (memsize < AWE_MAX_DRAM_SIZE) {
		awe_wait(5);
		/* read a data on the DRAM start address */
		awe_poke_dw(AWE_SMALR, AWE_DRAM_OFFSET);
		awe_peek(AWE_SMLD); /* discard stale data  */
		if (awe_peek(AWE_SMLD) != UNIQUE_ID1)
			break;
		if (awe_peek(AWE_SMLD) != UNIQUE_ID2)
			break;
		memsize += 512;  /* increment 512kbytes */
		/* Write a unique data on the test address;
		 * if the address is out of range, the data is written on
		 * 0x200000(=AWE_DRAM_OFFSET).  Then the two id words are
		 * broken by this data.
		 */
		awe_poke_dw(AWE_SMALW, AWE_DRAM_OFFSET + memsize*512L);
		awe_poke(AWE_SMLD, UNIQUE_ID3);
		awe_wait(5);
		/* read a data on the just written DRAM address */
		awe_poke_dw(AWE_SMALR, AWE_DRAM_OFFSET + memsize*512L);
		awe_peek(AWE_SMLD); /* discard stale data  */
		if (awe_peek(AWE_SMLD) != UNIQUE_ID3)
			break;
	}
	awe_close_dram();

	DEBUG(0,printk("AWE32: %d Kbytes memory detected\n", memsize));

	/* convert to Kbytes */
	memsize *= 1024;
}


/*----------------------------------------------------------------*/

/*
 * chorus and reverb controls; from VV's guide
 */

/* 5 parameters for each chorus mode; 3 x 16bit, 2 x 32bit */
static char chorus_defined[AWE_CHORUS_NUMBERS];
static awe_chorus_fx_rec chorus_parm[AWE_CHORUS_NUMBERS] = {
	{0xE600, 0x03F6, 0xBC2C ,0x00000000, 0x0000006D}, /* chorus 1 */
	{0xE608, 0x031A, 0xBC6E, 0x00000000, 0x0000017C}, /* chorus 2 */
	{0xE610, 0x031A, 0xBC84, 0x00000000, 0x00000083}, /* chorus 3 */
	{0xE620, 0x0269, 0xBC6E, 0x00000000, 0x0000017C}, /* chorus 4 */
	{0xE680, 0x04D3, 0xBCA6, 0x00000000, 0x0000005B}, /* feedback */
	{0xE6E0, 0x044E, 0xBC37, 0x00000000, 0x00000026}, /* flanger */
	{0xE600, 0x0B06, 0xBC00, 0x0000E000, 0x00000083}, /* short delay */
	{0xE6C0, 0x0B06, 0xBC00, 0x0000E000, 0x00000083}, /* short delay + feedback */
};

static int
awe_load_chorus_fx(awe_patch_info *patch, const char __user *addr, int count)
{
	if (patch->optarg < AWE_CHORUS_PREDEFINED || patch->optarg >= AWE_CHORUS_NUMBERS) {
		printk(KERN_WARNING "AWE32 Error: invalid chorus mode %d for uploading\n", patch->optarg);
		return -EINVAL;
	}
	if (count < sizeof(awe_chorus_fx_rec)) {
		printk(KERN_WARNING "AWE32 Error: too short chorus fx parameters\n");
		return -EINVAL;
	}
	if (copy_from_user(&chorus_parm[patch->optarg], addr + AWE_PATCH_INFO_SIZE,
			   sizeof(awe_chorus_fx_rec)))
		return -EFAULT;
	chorus_defined[patch->optarg] = TRUE;
	return 0;
}

static void
awe_set_chorus_mode(int effect)
{
	if (effect < 0 || effect >= AWE_CHORUS_NUMBERS ||
	    (effect >= AWE_CHORUS_PREDEFINED && !chorus_defined[effect]))
		return;
	awe_poke(AWE_INIT3(9), chorus_parm[effect].feedback);
	awe_poke(AWE_INIT3(12), chorus_parm[effect].delay_offset);
	awe_poke(AWE_INIT4(3), chorus_parm[effect].lfo_depth);
	awe_poke_dw(AWE_HWCF4, chorus_parm[effect].delay);
	awe_poke_dw(AWE_HWCF5, chorus_parm[effect].lfo_freq);
	awe_poke_dw(AWE_HWCF6, 0x8000);
	awe_poke_dw(AWE_HWCF7, 0x0000);
}

static void
awe_update_chorus_mode(void)
{
	awe_set_chorus_mode(ctrls[AWE_MD_CHORUS_MODE]);
}

/*----------------------------------------------------------------*/

/* reverb mode settings; write the following 28 data of 16 bit length
 *   on the corresponding ports in the reverb_cmds array
 */
static char reverb_defined[AWE_CHORUS_NUMBERS];
static awe_reverb_fx_rec reverb_parm[AWE_REVERB_NUMBERS] = {
{{  /* room 1 */
	0xB488, 0xA450, 0x9550, 0x84B5, 0x383A, 0x3EB5, 0x72F4,
	0x72A4, 0x7254, 0x7204, 0x7204, 0x7204, 0x4416, 0x4516,
	0xA490, 0xA590, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
}},
{{  /* room 2 */
	0xB488, 0xA458, 0x9558, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
}},
{{  /* room 3 */
	0xB488, 0xA460, 0x9560, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4416, 0x4516,
	0xA490, 0xA590, 0x842C, 0x852C, 0x842C, 0x852C, 0x842B,
	0x852B, 0x842B, 0x852B, 0x842A, 0x852A, 0x842A, 0x852A,
}},
{{  /* hall 1 */
	0xB488, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842B, 0x852B, 0x842B, 0x852B, 0x842A,
	0x852A, 0x842A, 0x852A, 0x8429, 0x8529, 0x8429, 0x8529,
}},
{{  /* hall 2 */
	0xB488, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7254,
	0x7234, 0x7224, 0x7254, 0x7264, 0x7294, 0x44C3, 0x45C3,
	0xA404, 0xA504, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
}},
{{  /* plate */
	0xB4FF, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7234,
	0x7234, 0x7234, 0x7234, 0x7234, 0x7234, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
}},
{{  /* delay */
	0xB4FF, 0xA470, 0x9500, 0x84B5, 0x333A, 0x39B5, 0x7204,
	0x7204, 0x7204, 0x7204, 0x7204, 0x72F4, 0x4400, 0x4500,
	0xA4FF, 0xA5FF, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420,
	0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520,
}},
{{  /* panning delay */
	0xB4FF, 0xA490, 0x9590, 0x8474, 0x333A, 0x39B5, 0x7204,
	0x7204, 0x7204, 0x7204, 0x7204, 0x72F4, 0x4400, 0x4500,
	0xA4FF, 0xA5FF, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420,
	0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520,
}},
};

static struct ReverbCmdPair {
	unsigned short cmd, port;
} reverb_cmds[28] = {
  {AWE_INIT1(0x03)}, {AWE_INIT1(0x05)}, {AWE_INIT4(0x1F)}, {AWE_INIT1(0x07)},
  {AWE_INIT2(0x14)}, {AWE_INIT2(0x16)}, {AWE_INIT1(0x0F)}, {AWE_INIT1(0x17)},
  {AWE_INIT1(0x1F)}, {AWE_INIT2(0x07)}, {AWE_INIT2(0x0F)}, {AWE_INIT2(0x17)},
  {AWE_INIT2(0x1D)}, {AWE_INIT2(0x1F)}, {AWE_INIT3(0x01)}, {AWE_INIT3(0x03)},
  {AWE_INIT1(0x09)}, {AWE_INIT1(0x0B)}, {AWE_INIT1(0x11)}, {AWE_INIT1(0x13)},
  {AWE_INIT1(0x19)}, {AWE_INIT1(0x1B)}, {AWE_INIT2(0x01)}, {AWE_INIT2(0x03)},
  {AWE_INIT2(0x09)}, {AWE_INIT2(0x0B)}, {AWE_INIT2(0x11)}, {AWE_INIT2(0x13)},
};

static int
awe_load_reverb_fx(awe_patch_info *patch, const char __user *addr, int count)
{
	if (patch->optarg < AWE_REVERB_PREDEFINED || patch->optarg >= AWE_REVERB_NUMBERS) {
		printk(KERN_WARNING "AWE32 Error: invalid reverb mode %d for uploading\n", patch->optarg);
		return -EINVAL;
	}
	if (count < sizeof(awe_reverb_fx_rec)) {
		printk(KERN_WARNING "AWE32 Error: too short reverb fx parameters\n");
		return -EINVAL;
	}
	if (copy_from_user(&reverb_parm[patch->optarg], addr + AWE_PATCH_INFO_SIZE,
			   sizeof(awe_reverb_fx_rec)))
		return -EFAULT;
	reverb_defined[patch->optarg] = TRUE;
	return 0;
}

static void
awe_set_reverb_mode(int effect)
{
	int i;
	if (effect < 0 || effect >= AWE_REVERB_NUMBERS ||
	    (effect >= AWE_REVERB_PREDEFINED && !reverb_defined[effect]))
		return;
	for (i = 0; i < 28; i++)
		awe_poke(reverb_cmds[i].cmd, reverb_cmds[i].port,
			 reverb_parm[effect].parms[i]);
}

static void
awe_update_reverb_mode(void)
{
	awe_set_reverb_mode(ctrls[AWE_MD_REVERB_MODE]);
}

/*
 * treble/bass equalizer control
 */

static unsigned short bass_parm[12][3] = {
	{0xD26A, 0xD36A, 0x0000}, /* -12 dB */
	{0xD25B, 0xD35B, 0x0000}, /*  -8 */
	{0xD24C, 0xD34C, 0x0000}, /*  -6 */
	{0xD23D, 0xD33D, 0x0000}, /*  -4 */
	{0xD21F, 0xD31F, 0x0000}, /*  -2 */
	{0xC208, 0xC308, 0x0001}, /*   0 (HW default) */
	{0xC219, 0xC319, 0x0001}, /*  +2 */
	{0xC22A, 0xC32A, 0x0001}, /*  +4 */
	{0xC24C, 0xC34C, 0x0001}, /*  +6 */
	{0xC26E, 0xC36E, 0x0001}, /*  +8 */
	{0xC248, 0xC348, 0x0002}, /* +10 */
	{0xC26A, 0xC36A, 0x0002}, /* +12 dB */
};

static unsigned short treble_parm[12][9] = {
	{0x821E, 0xC26A, 0x031E, 0xC36A, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001}, /* -12 dB */
	{0x821E, 0xC25B, 0x031E, 0xC35B, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001},
	{0x821E, 0xC24C, 0x031E, 0xC34C, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001},
	{0x821E, 0xC23D, 0x031E, 0xC33D, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001},
	{0x821E, 0xC21F, 0x031E, 0xC31F, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x021E, 0xD208, 0x831E, 0xD308, 0x0002},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x021D, 0xD219, 0x831D, 0xD319, 0x0002},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x021C, 0xD22A, 0x831C, 0xD32A, 0x0002},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x021A, 0xD24C, 0x831A, 0xD34C, 0x0002},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002}, /* +8 (HW default) */
	{0x821D, 0xD219, 0x031D, 0xD319, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002},
	{0x821C, 0xD22A, 0x031C, 0xD32A, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002}, /* +12 dB */
};


/*
 * set Emu8000 digital equalizer; from 0 to 11 [-12dB - 12dB]
 */
static void
awe_equalizer(int bass, int treble)
{
	unsigned short w;

	if (bass < 0 || bass > 11 || treble < 0 || treble > 11)
		return;
	awe_poke(AWE_INIT4(0x01), bass_parm[bass][0]);
	awe_poke(AWE_INIT4(0x11), bass_parm[bass][1]);
	awe_poke(AWE_INIT3(0x11), treble_parm[treble][0]);
	awe_poke(AWE_INIT3(0x13), treble_parm[treble][1]);
	awe_poke(AWE_INIT3(0x1B), treble_parm[treble][2]);
	awe_poke(AWE_INIT4(0x07), treble_parm[treble][3]);
	awe_poke(AWE_INIT4(0x0B), treble_parm[treble][4]);
	awe_poke(AWE_INIT4(0x0D), treble_parm[treble][5]);
	awe_poke(AWE_INIT4(0x17), treble_parm[treble][6]);
	awe_poke(AWE_INIT4(0x19), treble_parm[treble][7]);
	w = bass_parm[bass][2] + treble_parm[treble][8];
	awe_poke(AWE_INIT4(0x15), (unsigned short)(w + 0x0262));
	awe_poke(AWE_INIT4(0x1D), (unsigned short)(w + 0x8362));
}

static void awe_update_equalizer(void)
{
	awe_equalizer(ctrls[AWE_MD_BASS_LEVEL], ctrls[AWE_MD_TREBLE_LEVEL]);
}


/*----------------------------------------------------------------*/

#ifdef CONFIG_AWE32_MIDIEMU

/*
 * Emu8000 MIDI Emulation
 */

/*
 * midi queue record
 */

/* queue type */
enum { Q_NONE, Q_VARLEN, Q_READ, Q_SYSEX, };

#define MAX_MIDIBUF	64

/* midi status */
typedef struct MidiStatus {
	int queue;	/* queue type */
	int qlen;	/* queue length */
	int read;	/* chars read */
	int status;	/* current status */
	int chan;	/* current channel */
	unsigned char buf[MAX_MIDIBUF];
} MidiStatus;

/* MIDI mode type */
enum { MODE_GM, MODE_GS, MODE_XG, };

/* NRPN / CC -> Emu8000 parameter converter */
typedef struct {
	int control;
	int awe_effect;
	unsigned short (*convert)(int val);
} ConvTable;


/*
 * prototypes
 */

static int awe_midi_open(int dev, int mode, void (*input)(int,unsigned char), void (*output)(int));
static void awe_midi_close(int dev);
static int awe_midi_ioctl(int dev, unsigned cmd, void __user * arg);
static int awe_midi_outputc(int dev, unsigned char midi_byte);

static void init_midi_status(MidiStatus *st);
static void clear_rpn(void);
static void get_midi_char(MidiStatus *st, int c);
/*static void queue_varlen(MidiStatus *st, int c);*/
static void special_event(MidiStatus *st, int c);
static void queue_read(MidiStatus *st, int c);
static void midi_note_on(MidiStatus *st);
static void midi_note_off(MidiStatus *st);
static void midi_key_pressure(MidiStatus *st);
static void midi_channel_pressure(MidiStatus *st);
static void midi_pitch_wheel(MidiStatus *st);
static void midi_program_change(MidiStatus *st);
static void midi_control_change(MidiStatus *st);
static void midi_select_bank(MidiStatus *st, int val);
static void midi_nrpn_event(MidiStatus *st);
static void midi_rpn_event(MidiStatus *st);
static void midi_detune(int chan, int coarse, int fine);
static void midi_system_exclusive(MidiStatus *st);
static int send_converted_effect(ConvTable *table, int num_tables, MidiStatus *st, int type, int val);
static int add_converted_effect(ConvTable *table, int num_tables, MidiStatus *st, int type, int val);
static int xg_control_change(MidiStatus *st, int cmd, int val);

#define numberof(ary)	(sizeof(ary)/sizeof(ary[0]))


/*
 * OSS Midi device record
 */

static struct midi_operations awe_midi_operations =
{
	.owner		= THIS_MODULE,
	.info		= {"AWE Midi Emu", 0, 0, SNDCARD_SB},
	.in_info	= {0},
	.open		= awe_midi_open, /*open*/
	.close		= awe_midi_close, /*close*/
	.ioctl		= awe_midi_ioctl, /*ioctl*/
	.outputc	= awe_midi_outputc, /*outputc*/
};

static int my_mididev = -1;

static void __init attach_midiemu(void)
{
	if ((my_mididev = sound_alloc_mididev()) < 0)
		printk ("Sound: Too many midi devices detected\n");
	else
		midi_devs[my_mididev] = &awe_midi_operations;
}

static void unload_midiemu(void)
{
	if (my_mididev >= 0)
		sound_unload_mididev(my_mididev);
}


/*
 * open/close midi device
 */

static int midi_opened = FALSE;

static int midi_mode;
static int coarsetune, finetune;

static int xg_mapping = TRUE;
static int xg_bankmode;

/* effect sensitivity */

#define FX_CUTOFF	0
#define FX_RESONANCE	1
#define FX_ATTACK	2
#define FX_RELEASE	3
#define FX_VIBRATE	4
#define FX_VIBDEPTH	5
#define FX_VIBDELAY	6
#define FX_NUMS		7

#define DEF_FX_CUTOFF		170
#define DEF_FX_RESONANCE	6
#define DEF_FX_ATTACK		50
#define DEF_FX_RELEASE		50
#define DEF_FX_VIBRATE		30
#define DEF_FX_VIBDEPTH		4
#define DEF_FX_VIBDELAY		1500

/* effect sense: */
static int gs_sense[] = 
{
	DEF_FX_CUTOFF, DEF_FX_RESONANCE, DEF_FX_ATTACK, DEF_FX_RELEASE,
	DEF_FX_VIBRATE, DEF_FX_VIBDEPTH, DEF_FX_VIBDELAY
};
static int xg_sense[] = 
{
	DEF_FX_CUTOFF, DEF_FX_RESONANCE, DEF_FX_ATTACK, DEF_FX_RELEASE,
	DEF_FX_VIBRATE, DEF_FX_VIBDEPTH, DEF_FX_VIBDELAY
};


/* current status */
static MidiStatus curst;


static int
awe_midi_open (int dev, int mode,
	       void (*input)(int,unsigned char),
	       void (*output)(int))
{
	if (midi_opened)
		return -EBUSY;

	midi_opened = TRUE;

	midi_mode = MODE_GM;

	curst.queue = Q_NONE;
	curst.qlen = 0;
	curst.read = 0;
	curst.status = 0;
	curst.chan = 0;
	memset(curst.buf, 0, sizeof(curst.buf));

	init_midi_status(&curst);

	return 0;
}

static void
awe_midi_close (int dev)
{
	midi_opened = FALSE;
}


static int
awe_midi_ioctl (int dev, unsigned cmd, void __user *arg)
{
	return -EPERM;
}

static int
awe_midi_outputc (int dev, unsigned char midi_byte)
{
	if (! midi_opened)
		return 1;

	/* force to change playing mode */
	playing_mode = AWE_PLAY_MULTI;

	get_midi_char(&curst, midi_byte);
	return 1;
}


/*
 * initialize
 */

static void init_midi_status(MidiStatus *st)
{
	clear_rpn();
	coarsetune = 0;
	finetune = 0;
}


/*
 * RPN & NRPN
 */

#define MAX_MIDI_CHANNELS	16

/* RPN & NRPN */
static unsigned char nrpn[MAX_MIDI_CHANNELS];  /* current event is NRPN? */
static int msb_bit;  /* current event is msb for RPN/NRPN */
/* RPN & NRPN indeces */
static unsigned char rpn_msb[MAX_MIDI_CHANNELS], rpn_lsb[MAX_MIDI_CHANNELS];
/* RPN & NRPN values */
static int rpn_val[MAX_MIDI_CHANNELS];

static void clear_rpn(void)
{
	int i;
	for (i = 0; i < MAX_MIDI_CHANNELS; i++) {
		nrpn[i] = 0;
		rpn_msb[i] = 127;
		rpn_lsb[i] = 127;
		rpn_val[i] = 0;
	}
	msb_bit = 0;
}


/*
 * process midi queue
 */

/* status event types */
typedef void (*StatusEvent)(MidiStatus *st);
static struct StatusEventList {
	StatusEvent process;
	int qlen;
} status_event[8] = {
	{midi_note_off, 2},
	{midi_note_on, 2},
	{midi_key_pressure, 2},
	{midi_control_change, 2},
	{midi_program_change, 1},
	{midi_channel_pressure, 1},
	{midi_pitch_wheel, 2},
	{NULL, 0},
};


/* read a char from fifo and process it */
static void get_midi_char(MidiStatus *st, int c)
{
	if (c == 0xfe) {
		/* ignore active sense */
		st->queue = Q_NONE;
		return;
	}

	switch (st->queue) {
	/* case Q_VARLEN: queue_varlen(st, c); break;*/
	case Q_READ:
	case Q_SYSEX:
		queue_read(st, c);
		break;
	case Q_NONE:
		st->read = 0;
		if ((c & 0xf0) == 0xf0) {
			special_event(st, c);
		} else if (c & 0x80) { /* status change */
			st->status = (c >> 4) & 0x07;
			st->chan = c & 0x0f;
			st->queue = Q_READ;
			st->qlen = status_event[st->status].qlen;
			if (st->qlen == 0)
				st->queue = Q_NONE;
		}
		break;
	}
}

/* 0xfx events */
static void special_event(MidiStatus *st, int c)
{
	switch (c) {
	case 0xf0: /* system exclusive */
		st->queue = Q_SYSEX;
		st->qlen = 0;
		break;
	case 0xf1: /* MTC quarter frame */
	case 0xf3: /* song select */
		st->queue = Q_READ;
		st->qlen = 1;
		break;
	case 0xf2: /* song position */
		st->queue = Q_READ;
		st->qlen = 2;
		break;
	}
}

#if 0
/* read variable length value */
static void queue_varlen(MidiStatus *st, int c)
{
	st->qlen += (c & 0x7f);
	if (c & 0x80) {
		st->qlen <<= 7;
		return;
	}
	if (st->qlen <= 0) {
		st->qlen = 0;
		st->queue = Q_NONE;
	}
	st->queue = Q_READ;
	st->read = 0;
}
#endif


/* read a char */
static void queue_read(MidiStatus *st, int c)
{
	if (st->read < MAX_MIDIBUF) {
		if (st->queue != Q_SYSEX)
			c &= 0x7f;
		st->buf[st->read] = (unsigned char)c;
	}
	st->read++;
	if (st->queue == Q_SYSEX && c == 0xf7) {
		midi_system_exclusive(st);
		st->queue = Q_NONE;
	} else if (st->queue == Q_READ && st->read >= st->qlen) {
		if (status_event[st->status].process)
			status_event[st->status].process(st);
		st->queue = Q_NONE;
	}
}


/*
 * status events
 */

/* note on */
static void midi_note_on(MidiStatus *st)
{
	DEBUG(2,printk("midi: note_on (%d) %d %d\n", st->chan, st->buf[0], st->buf[1]));
	if (st->buf[1] == 0)
		midi_note_off(st);
	else
		awe_start_note(0, st->chan, st->buf[0], st->buf[1]);
}

/* note off */
static void midi_note_off(MidiStatus *st)
{
	DEBUG(2,printk("midi: note_off (%d) %d %d\n", st->chan, st->buf[0], st->buf[1]));
	awe_kill_note(0, st->chan, st->buf[0], st->buf[1]);
}

/* key pressure change */
static void midi_key_pressure(MidiStatus *st)
{
	awe_key_pressure(0, st->chan, st->buf[0], st->buf[1]);
}

/* channel pressure change */
static void midi_channel_pressure(MidiStatus *st)
{
	channels[st->chan].chan_press = st->buf[0];
	awe_modwheel_change(st->chan, st->buf[0]);
}

/* pitch wheel change */
static void midi_pitch_wheel(MidiStatus *st)
{
	int val = (int)st->buf[1] * 128 + st->buf[0];
	awe_bender(0, st->chan, val);
}

/* program change */
static void midi_program_change(MidiStatus *st)
{
	int preset;
	preset = st->buf[0];
	if (midi_mode == MODE_GS && IS_DRUM_CHANNEL(st->chan) && preset == 127)
		preset = 0;
	else if (midi_mode == MODE_XG && xg_mapping && IS_DRUM_CHANNEL(st->chan))
		preset += 64;

	awe_set_instr(0, st->chan, preset);
}

#define send_effect(chan,type,val) awe_send_effect(chan,-1,type,val)
#define add_effect(chan,type,val) awe_send_effect(chan,-1,(type)|0x80,val)
#define unset_effect(chan,type) awe_send_effect(chan,-1,(type)|0x40,0)

/* midi control change */
static void midi_control_change(MidiStatus *st)
{
	int cmd = st->buf[0];
	int val = st->buf[1];

	DEBUG(2,printk("midi: control (%d) %d %d\n", st->chan, cmd, val));
	if (midi_mode == MODE_XG) {
		if (xg_control_change(st, cmd, val))
			return;
	}

	/* controls #31 - #64 are LSB of #0 - #31 */
	msb_bit = 1;
	if (cmd >= 0x20 && cmd < 0x40) {
		msb_bit = 0;
		cmd -= 0x20;
	}

	switch (cmd) {
	case CTL_SOFT_PEDAL:
		if (val == 127)
			add_effect(st->chan, AWE_FX_CUTOFF, -160);
		else
			unset_effect(st->chan, AWE_FX_CUTOFF);
		break;

	case CTL_BANK_SELECT:
		midi_select_bank(st, val);
		break;
		
	/* set RPN/NRPN parameter */
	case CTL_REGIST_PARM_NUM_MSB:
		nrpn[st->chan]=0; rpn_msb[st->chan]=val;
		break;
	case CTL_REGIST_PARM_NUM_LSB:
		nrpn[st->chan]=0; rpn_lsb[st->chan]=val;
		break;
	case CTL_NONREG_PARM_NUM_MSB:
		nrpn[st->chan]=1; rpn_msb[st->chan]=val;
		break;
	case CTL_NONREG_PARM_NUM_LSB:
		nrpn[st->chan]=1; rpn_lsb[st->chan]=val;
		break;

	/* send RPN/NRPN entry */
	case CTL_DATA_ENTRY:
		if (msb_bit)
			rpn_val[st->chan] = val * 128;
		else
			rpn_val[st->chan] |= val;
		if (nrpn[st->chan])
			midi_nrpn_event(st);
		else
			midi_rpn_event(st);
		break;

	/* increase/decrease data entry */
	case CTL_DATA_INCREMENT:
		rpn_val[st->chan]++;
		midi_rpn_event(st);
		break;
	case CTL_DATA_DECREMENT:
		rpn_val[st->chan]--;
		midi_rpn_event(st);
		break;

	/* default */
	default:
		awe_controller(0, st->chan, cmd, val);
		break;
	}
}

/* tone bank change */
static void midi_select_bank(MidiStatus *st, int val)
{
	if (midi_mode == MODE_XG && msb_bit) {
		xg_bankmode = val;
		/* XG MSB value; not normal bank selection */
		switch (val) {
		case 127: /* remap to drum channel */
			awe_controller(0, st->chan, CTL_BANK_SELECT, 128);
			break;
		default: /* remap to normal channel */
			awe_controller(0, st->chan, CTL_BANK_SELECT, val);
			break;
		}
		return;
	} else if (midi_mode == MODE_GS && !msb_bit)
		/* ignore LSB bank in GS mode (used for mapping) */
		return;

	/* normal bank controls; accept both MSB and LSB */
	if (! IS_DRUM_CHANNEL(st->chan)) {
		if (midi_mode == MODE_XG) {
			if (xg_bankmode) return;
			if (val == 64 || val == 126)
				val = 0;
		} else if (midi_mode == MODE_GS && val == 127)
			val = 0;
		awe_controller(0, st->chan, CTL_BANK_SELECT, val);
	}
}


/*
 * RPN events
 */

static void midi_rpn_event(MidiStatus *st)
{
	int type;
	type = (rpn_msb[st->chan]<<8) | rpn_lsb[st->chan];
	switch (type) {
	case 0x0000: /* Pitch bend sensitivity */
		/* MSB only / 1 semitone per 128 */
		if (msb_bit) {
			channels[st->chan].bender_range = 
				rpn_val[st->chan] * 100 / 128;
		}
		break;
					
	case 0x0001: /* fine tuning: */
		/* MSB/LSB, 8192=center, 100/8192 cent step */
		finetune = rpn_val[st->chan] - 8192;
		midi_detune(st->chan, coarsetune, finetune);
		break;

	case 0x0002: /* coarse tuning */
		/* MSB only / 8192=center, 1 semitone per 128 */
		if (msb_bit) {
			coarsetune = rpn_val[st->chan] - 8192;
			midi_detune(st->chan, coarsetune, finetune);
		}
		break;

	case 0x7F7F: /* "lock-in" RPN */
		break;
	}
}


/* tuning:
 *   coarse = -8192 to 8192 (100 cent per 128)
 *   fine = -8192 to 8192 (max=100cent)
 */
static void midi_detune(int chan, int coarse, int fine)
{
	/* 4096 = 1200 cents in AWE parameter */
	int val;
	val = coarse * 4096 / (12 * 128);
	val += fine / 24;
	if (val)
		send_effect(chan, AWE_FX_INIT_PITCH, val);
	else
		unset_effect(chan, AWE_FX_INIT_PITCH);
}


/*
 * system exclusive message
 * GM/GS/XG macros are accepted
 */

static void midi_system_exclusive(MidiStatus *st)
{
	/* GM on */
	static unsigned char gm_on_macro[] = {
		0x7e,0x7f,0x09,0x01,
	};
	/* XG on */
	static unsigned char xg_on_macro[] = {
		0x43,0x10,0x4c,0x00,0x00,0x7e,0x00,
	};
	/* GS prefix
	 * drum channel: XX=0x1?(channel), YY=0x15, ZZ=on/off
	 * reverb mode: XX=0x01, YY=0x30, ZZ=0-7
	 * chorus mode: XX=0x01, YY=0x38, ZZ=0-7
	 */
	static unsigned char gs_pfx_macro[] = {
		0x41,0x10,0x42,0x12,0x40,/*XX,YY,ZZ*/
	};

#if 0
	/* SC88 system mode set
	 * single module mode: XX=1
	 * double module mode: XX=0
	 */
	static unsigned char gs_mode_macro[] = {
		0x41,0x10,0x42,0x12,0x00,0x00,0x7F,/*ZZ*/
	};
	/* SC88 display macro: XX=01:bitmap, 00:text
	 */
	static unsigned char gs_disp_macro[] = {
		0x41,0x10,0x45,0x12,0x10,/*XX,00*/
	};
#endif

	/* GM on */
	if (memcmp(st->buf, gm_on_macro, sizeof(gm_on_macro)) == 0) {
		if (midi_mode != MODE_GS && midi_mode != MODE_XG)
			midi_mode = MODE_GM;
		init_midi_status(st);
	}

	/* GS macros */
	else if (memcmp(st->buf, gs_pfx_macro, sizeof(gs_pfx_macro)) == 0) {
		if (midi_mode != MODE_GS && midi_mode != MODE_XG)
			midi_mode = MODE_GS;

		if (st->buf[5] == 0x00 && st->buf[6] == 0x7f && st->buf[7] == 0x00) {
			/* GS reset */
			init_midi_status(st);
		}

		else if ((st->buf[5] & 0xf0) == 0x10 && st->buf[6] == 0x15) {
			/* drum pattern */
			int p = st->buf[5] & 0x0f;
			if (p == 0) p = 9;
			else if (p < 10) p--;
			if (st->buf[7] == 0)
				DRUM_CHANNEL_OFF(p);
			else
				DRUM_CHANNEL_ON(p);

		} else if ((st->buf[5] & 0xf0) == 0x10 && st->buf[6] == 0x21) {
			/* program */
			int p = st->buf[5] & 0x0f;
			if (p == 0) p = 9;
			else if (p < 10) p--;
			if (! IS_DRUM_CHANNEL(p))
				awe_set_instr(0, p, st->buf[7]);

		} else if (st->buf[5] == 0x01 && st->buf[6] == 0x30) {
			/* reverb mode */
			awe_set_reverb_mode(st->buf[7]);

		} else if (st->buf[5] == 0x01 && st->buf[6] == 0x38) {
			/* chorus mode */
			awe_set_chorus_mode(st->buf[7]);

		} else if (st->buf[5] == 0x00 && st->buf[6] == 0x04) {
			/* master volume */
			awe_change_master_volume(st->buf[7]);

		}
	}

	/* XG on */
	else if (memcmp(st->buf, xg_on_macro, sizeof(xg_on_macro)) == 0) {
		midi_mode = MODE_XG;
		xg_mapping = TRUE;
		xg_bankmode = 0;
	}
}


/*----------------------------------------------------------------*/

/*
 * convert NRPN/control values
 */

static int send_converted_effect(ConvTable *table, int num_tables, MidiStatus *st, int type, int val)
{
	int i, cval;
	for (i = 0; i < num_tables; i++) {
		if (table[i].control == type) {
			cval = table[i].convert(val);
			send_effect(st->chan, table[i].awe_effect, cval);
			return TRUE;
		}
	}
	return FALSE;
}

static int add_converted_effect(ConvTable *table, int num_tables, MidiStatus *st, int type, int val)
{
	int i, cval;
	for (i = 0; i < num_tables; i++) {
		if (table[i].control == type) {
			cval = table[i].convert(val);
			add_effect(st->chan, table[i].awe_effect|0x80, cval);
			return TRUE;
		}
	}
	return FALSE;
}


/*
 * AWE32 NRPN effects
 */

static unsigned short fx_delay(int val);
static unsigned short fx_attack(int val);
static unsigned short fx_hold(int val);
static unsigned short fx_decay(int val);
static unsigned short fx_the_value(int val);
static unsigned short fx_twice_value(int val);
static unsigned short fx_conv_pitch(int val);
static unsigned short fx_conv_Q(int val);

/* function for each NRPN */		/* [range]  units */
#define fx_env1_delay	fx_delay	/* [0,5900] 4msec */
#define fx_env1_attack	fx_attack	/* [0,5940] 1msec */
#define fx_env1_hold	fx_hold		/* [0,8191] 1msec */
#define fx_env1_decay	fx_decay	/* [0,5940] 4msec */
#define fx_env1_release	fx_decay	/* [0,5940] 4msec */
#define fx_env1_sustain	fx_the_value	/* [0,127] 0.75dB */
#define fx_env1_pitch	fx_the_value	/* [-127,127] 9.375cents */
#define fx_env1_cutoff	fx_the_value	/* [-127,127] 56.25cents */

#define fx_env2_delay	fx_delay	/* [0,5900] 4msec */
#define fx_env2_attack	fx_attack	/* [0,5940] 1msec */
#define fx_env2_hold	fx_hold		/* [0,8191] 1msec */
#define fx_env2_decay	fx_decay	/* [0,5940] 4msec */
#define fx_env2_release	fx_decay	/* [0,5940] 4msec */
#define fx_env2_sustain	fx_the_value	/* [0,127] 0.75dB */

#define fx_lfo1_delay	fx_delay	/* [0,5900] 4msec */
#define fx_lfo1_freq	fx_twice_value	/* [0,127] 84mHz */
#define fx_lfo1_volume	fx_twice_value	/* [0,127] 0.1875dB */
#define fx_lfo1_pitch	fx_the_value	/* [-127,127] 9.375cents */
#define fx_lfo1_cutoff	fx_twice_value	/* [-64,63] 56.25cents */

#define fx_lfo2_delay	fx_delay	/* [0,5900] 4msec */
#define fx_lfo2_freq	fx_twice_value	/* [0,127] 84mHz */
#define fx_lfo2_pitch	fx_the_value	/* [-127,127] 9.375cents */

#define fx_init_pitch	fx_conv_pitch	/* [-8192,8192] cents */
#define fx_chorus	fx_the_value	/* [0,255] -- */
#define fx_reverb	fx_the_value	/* [0,255] -- */
#define fx_cutoff	fx_twice_value	/* [0,127] 62Hz */
#define fx_filterQ	fx_conv_Q	/* [0,127] -- */

static unsigned short fx_delay(int val)
{
	return (unsigned short)calc_parm_delay(val);
}

static unsigned short fx_attack(int val)
{
	return (unsigned short)calc_parm_attack(val);
}

static unsigned short fx_hold(int val)
{
	return (unsigned short)calc_parm_hold(val);
}

static unsigned short fx_decay(int val)
{
	return (unsigned short)calc_parm_decay(val);
}

static unsigned short fx_the_value(int val)
{
	return (unsigned short)(val & 0xff);
}

static unsigned short fx_twice_value(int val)
{
	return (unsigned short)((val * 2) & 0xff);
}

static unsigned short fx_conv_pitch(int val)
{
	return (short)(val * 4096 / 1200);
}

static unsigned short fx_conv_Q(int val)
{
	return (unsigned short)((val / 8) & 0xff);
}


static ConvTable awe_effects[] =
{
	{ 0, AWE_FX_LFO1_DELAY,	fx_lfo1_delay},
	{ 1, AWE_FX_LFO1_FREQ,	fx_lfo1_freq},
	{ 2, AWE_FX_LFO2_DELAY,	fx_lfo2_delay},
	{ 3, AWE_FX_LFO2_FREQ,	fx_lfo2_freq},

	{ 4, AWE_FX_ENV1_DELAY,	fx_env1_delay},
	{ 5, AWE_FX_ENV1_ATTACK,fx_env1_attack},
	{ 6, AWE_FX_ENV1_HOLD,	fx_env1_hold},
	{ 7, AWE_FX_ENV1_DECAY,	fx_env1_decay},
	{ 8, AWE_FX_ENV1_SUSTAIN,	fx_env1_sustain},
	{ 9, AWE_FX_ENV1_RELEASE,	fx_env1_release},

	{10, AWE_FX_ENV2_DELAY,	fx_env2_delay},
	{11, AWE_FX_ENV2_ATTACK,	fx_env2_attack},
	{12, AWE_FX_ENV2_HOLD,	fx_env2_hold},
	{13, AWE_FX_ENV2_DECAY,	fx_env2_decay},
	{14, AWE_FX_ENV2_SUSTAIN,	fx_env2_sustain},
	{15, AWE_FX_ENV2_RELEASE,	fx_env2_release},

	{16, AWE_FX_INIT_PITCH,	fx_init_pitch},
	{17, AWE_FX_LFO1_PITCH,	fx_lfo1_pitch},
	{18, AWE_FX_LFO2_PITCH,	fx_lfo2_pitch},
	{19, AWE_FX_ENV1_PITCH,	fx_env1_pitch},
	{20, AWE_FX_LFO1_VOLUME,	fx_lfo1_volume},
	{21, AWE_FX_CUTOFF,		fx_cutoff},
	{22, AWE_FX_FILTERQ,	fx_filterQ},
	{23, AWE_FX_LFO1_CUTOFF,	fx_lfo1_cutoff},
	{24, AWE_FX_ENV1_CUTOFF,	fx_env1_cutoff},
	{25, AWE_FX_CHORUS,		fx_chorus},
	{26, AWE_FX_REVERB,		fx_reverb},
};

static int num_awe_effects = numberof(awe_effects);


/*
 * GS(SC88) NRPN effects; still experimental
 */

/* cutoff: quarter semitone step, max=255 */
static unsigned short gs_cutoff(int val)
{
	return (val - 64) * gs_sense[FX_CUTOFF] / 50;
}

/* resonance: 0 to 15(max) */
static unsigned short gs_filterQ(int val)
{
	return (val - 64) * gs_sense[FX_RESONANCE] / 50;
}

/* attack: */
static unsigned short gs_attack(int val)
{
	return -(val - 64) * gs_sense[FX_ATTACK] / 50;
}

/* decay: */
static unsigned short gs_decay(int val)
{
	return -(val - 64) * gs_sense[FX_RELEASE] / 50;
}

/* release: */
static unsigned short gs_release(int val)
{
	return -(val - 64) * gs_sense[FX_RELEASE] / 50;
}

/* vibrato freq: 0.042Hz step, max=255 */
static unsigned short gs_vib_rate(int val)
{
	return (val - 64) * gs_sense[FX_VIBRATE] / 50;
}

/* vibrato depth: max=127, 1 octave */
static unsigned short gs_vib_depth(int val)
{
	return (val - 64) * gs_sense[FX_VIBDEPTH] / 50;
}

/* vibrato delay: -0.725msec step */
static unsigned short gs_vib_delay(int val)
{
	return -(val - 64) * gs_sense[FX_VIBDELAY] / 50;
}

static ConvTable gs_effects[] =
{
	{32, AWE_FX_CUTOFF,	gs_cutoff},
	{33, AWE_FX_FILTERQ,	gs_filterQ},
	{99, AWE_FX_ENV2_ATTACK, gs_attack},
	{100, AWE_FX_ENV2_DECAY, gs_decay},
	{102, AWE_FX_ENV2_RELEASE, gs_release},
	{8, AWE_FX_LFO1_FREQ, gs_vib_rate},
	{9, AWE_FX_LFO1_VOLUME, gs_vib_depth},
	{10, AWE_FX_LFO1_DELAY, gs_vib_delay},
};

static int num_gs_effects = numberof(gs_effects);


/*
 * NRPN events: accept as AWE32/SC88 specific controls
 */

static void midi_nrpn_event(MidiStatus *st)
{
	if (rpn_msb[st->chan] == 127 && rpn_lsb[st->chan] <= 26) {
		if (! msb_bit) /* both MSB/LSB necessary */
			send_converted_effect(awe_effects, num_awe_effects,
					      st, rpn_lsb[st->chan],
					      rpn_val[st->chan] - 8192);
	} else if (rpn_msb[st->chan] == 1) {
		if (msb_bit) /* only MSB is valid */
			add_converted_effect(gs_effects, num_gs_effects,
					     st, rpn_lsb[st->chan],
					     rpn_val[st->chan] / 128);
	}
}


/*
 * XG control effects; still experimental
 */

/* cutoff: quarter semitone step, max=255 */
static unsigned short xg_cutoff(int val)
{
	return (val - 64) * xg_sense[FX_CUTOFF] / 64;
}

/* resonance: 0(open) to 15(most nasal) */
static unsigned short xg_filterQ(int val)
{
	return (val - 64) * xg_sense[FX_RESONANCE] / 64;
}

/* attack: */
static unsigned short xg_attack(int val)
{
	return -(val - 64) * xg_sense[FX_ATTACK] / 64;
}

/* release: */
static unsigned short xg_release(int val)
{
	return -(val - 64) * xg_sense[FX_RELEASE] / 64;
}

static ConvTable xg_effects[] =
{
	{71, AWE_FX_CUTOFF,	xg_cutoff},
	{74, AWE_FX_FILTERQ,	xg_filterQ},
	{72, AWE_FX_ENV2_RELEASE, xg_release},
	{73, AWE_FX_ENV2_ATTACK, xg_attack},
};

static int num_xg_effects = numberof(xg_effects);

static int xg_control_change(MidiStatus *st, int cmd, int val)
{
	return add_converted_effect(xg_effects, num_xg_effects, st, cmd, val);
}

#endif /* CONFIG_AWE32_MIDIEMU */


/*----------------------------------------------------------------*/


/*
 * initialization of AWE driver
 */

static void
awe_initialize(void)
{
	DEBUG(0,printk("AWE32: initializing..\n"));

	/* initialize hardware configuration */
	awe_poke(AWE_HWCF1, 0x0059);
	awe_poke(AWE_HWCF2, 0x0020);

	/* disable audio; this seems to reduce a clicking noise a bit.. */
	awe_poke(AWE_HWCF3, 0);

	/* initialize audio channels */
	awe_init_audio();

	/* initialize DMA */
	awe_init_dma();

	/* initialize init array */
	awe_init_array();

	/* check DRAM memory size */
	awe_check_dram();

	/* initialize the FM section of the AWE32 */
	awe_init_fm();

	/* set up voice envelopes */
	awe_tweak();

	/* enable audio */
	awe_poke(AWE_HWCF3, 0x0004);

	/* set default values */
	awe_init_ctrl_parms(TRUE);

	/* set equalizer */
	awe_update_equalizer();

	/* set reverb & chorus modes */
	awe_update_reverb_mode();
	awe_update_chorus_mode();
}


/*
 * Core Device Management Functions
 */

/* store values to i/o port array */
static void setup_ports(int port1, int port2, int port3)
{
	awe_ports[0] = port1;
	if (port2 == 0)
		port2 = port1 + 0x400;
	awe_ports[1] = port2;
	awe_ports[2] = port2 + 2;
	if (port3 == 0)
		port3 = port1 + 0x800;
	awe_ports[3] = port3;
	awe_ports[4] = port3 + 2;

	port_setuped = TRUE;
}

/*
 * port request
 *  0x620-623, 0xA20-A23, 0xE20-E23
 */

static int
awe_request_region(void)
{
	if (! port_setuped)
		return 0;
	if (! request_region(awe_ports[0], 4, "sound driver (AWE32)"))
		return 0;
	if (! request_region(awe_ports[1], 4, "sound driver (AWE32)"))
		goto err_out;
	if (! request_region(awe_ports[3], 4, "sound driver (AWE32)"))
		goto err_out1;
	return 1;
err_out1:
	release_region(awe_ports[1], 4);
err_out:
	release_region(awe_ports[0], 4);
	return 0;
}

static void
awe_release_region(void)
{
	if (! port_setuped) return;
	release_region(awe_ports[0], 4);
	release_region(awe_ports[1], 4);
	release_region(awe_ports[3], 4);
}

static int awe_attach_device(void)
{
	if (awe_present) return 0; /* for OSS38.. called twice? */

	/* reserve I/O ports for awedrv */
	if (! awe_request_region()) {
		printk(KERN_ERR "AWE32: I/O area already used.\n");
		return 0;
	}

	/* set buffers to NULL */
	sfhead = sftail = NULL;

	my_dev = sound_alloc_synthdev();
	if (my_dev == -1) {
		printk(KERN_ERR "AWE32 Error: too many synthesizers\n");
		awe_release_region();
		return 0;
	}

	voice_alloc = &awe_operations.alloc;
	voice_alloc->max_voice = awe_max_voices;
	synth_devs[my_dev] = &awe_operations;

#ifdef CONFIG_AWE32_MIXER
	attach_mixer();
#endif
#ifdef CONFIG_AWE32_MIDIEMU
	attach_midiemu();
#endif

	/* clear all samples */
	awe_reset_samples();

	/* initialize AWE32 hardware */
	awe_initialize();

	sprintf(awe_info.name, "AWE32-%s (RAM%dk)",
		AWEDRV_VERSION, memsize/1024);
	printk(KERN_INFO "<SoundBlaster EMU8000 (RAM%dk)>\n", memsize/1024);

	awe_present = TRUE;

	return 1;
}

static void awe_dettach_device(void)
{
	if (awe_present) {
		awe_reset_samples();
		awe_release_region();
		free_tables();
#ifdef CONFIG_AWE32_MIXER
		unload_mixer();
#endif
#ifdef CONFIG_AWE32_MIDIEMU
		unload_midiemu();
#endif
		sound_unload_synthdev(my_dev);
		awe_present = FALSE;
	}
}


/*
 * Legacy device Probing
 */

/* detect emu8000 chip on the specified address; from VV's guide */

static int __init
awe_detect_base(int addr)
{
	setup_ports(addr, 0, 0);
	if ((awe_peek(AWE_U1) & 0x000F) != 0x000C)
		return 0;
	if ((awe_peek(AWE_HWCF1) & 0x007E) != 0x0058)
		return 0;
	if ((awe_peek(AWE_HWCF2) & 0x0003) != 0x0003)
		return 0;
        DEBUG(0,printk("AWE32 found at %x\n", addr));
	return 1;
}

static int __init awe_detect_legacy_devices(void)
{
	int base;
	for (base = 0x620; base <= 0x680; base += 0x20)
		if (awe_detect_base(base)) {
			awe_attach_device();
			return 1;
			}
	DEBUG(0,printk("AWE32 Legacy detection failed\n"));
	return 0;
}


/*
 * PnP device Probing
 */

static struct pnp_device_id awe_pnp_ids[] = {
	{.id = "CTL0021", .driver_data = 0}, /* AWE32 WaveTable */
	{.id = "CTL0022", .driver_data = 0}, /* AWE64 WaveTable */
	{.id = "CTL0023", .driver_data = 0}, /* AWE64 Gold WaveTable */
	{ } /* terminator */
};

MODULE_DEVICE_TABLE(pnp, awe_pnp_ids);

static int awe_pnp_probe(struct pnp_dev *dev, const struct pnp_device_id *dev_id)
{
	int io1, io2, io3;

	if (awe_present) {
		printk(KERN_ERR "AWE32: This driver only supports one AWE32 device, skipping.\n");
	}

	if (!pnp_port_valid(dev,0) ||
	    !pnp_port_valid(dev,1) ||
	    !pnp_port_valid(dev,2)) {
		printk(KERN_ERR "AWE32: The PnP device does not have the required resources.\n");
		return -EINVAL;
	}
	io1 = pnp_port_start(dev,0);
	io2 = pnp_port_start(dev,1);
	io3 = pnp_port_start(dev,2);
	printk(KERN_INFO "AWE32: A PnP Wave Table was detected at IO's %#x,%#x,%#x.\n",
	       io1, io2, io3);
	setup_ports(io1, io2, io3);

	awe_attach_device();
	return 0;
}

static void awe_pnp_remove(struct pnp_dev *dev)
{
	awe_dettach_device();
}

static struct pnp_driver awe_pnp_driver = {
	.name		= "AWE32",
	.id_table	= awe_pnp_ids,
	.probe		= awe_pnp_probe,
	.remove		= awe_pnp_remove,
};

static int __init awe_detect_pnp_devices(void)
{
	int ret;

	ret = pnp_register_driver(&awe_pnp_driver);
	if (ret<0)
		printk(KERN_ERR "AWE32: PnP support is unavailable.\n");
	return ret;
}


/*
 * device / lowlevel (module) interface
 */

static int __init
awe_detect(void)
{
	printk(KERN_INFO "AWE32: Probing for WaveTable...\n");
	if (isapnp) {
		if (awe_detect_pnp_devices()>=0)
			return 1;
	} else
		printk(KERN_INFO "AWE32: Skipping PnP detection.\n");

	if (awe_detect_legacy_devices())
		return 1;

	return 0;
}

static int __init attach_awe(void)
{
	return awe_detect() ? 0 : -ENODEV;
}

static void __exit unload_awe(void)
{
	pnp_unregister_driver(&awe_pnp_driver);
	awe_dettach_device();
}


module_init(attach_awe);
module_exit(unload_awe);

#ifndef MODULE
static int __init setup_awe(char *str)
{
	/* io, memsize, isapnp */
	int ints[4];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	io = ints[1];
	memsize = ints[2];
	isapnp = ints[3];

	return 1;
}

__setup("awe=", setup_awe);
#endif
