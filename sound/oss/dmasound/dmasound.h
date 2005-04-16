#ifndef _dmasound_h_
/*
 *  linux/sound/oss/dmasound/dmasound.h
 *
 *
 *  Minor numbers for the sound driver.
 *
 *  Unfortunately Creative called the codec chip of SB as a DSP. For this
 *  reason the /dev/dsp is reserved for digitized audio use. There is a
 *  device for true DSP processors but it will be called something else.
 *  In v3.0 it's /dev/sndproc but this could be a temporary solution.
 */
#define _dmasound_h_

#include <linux/types.h>
#include <linux/config.h>

#define SND_NDEVS	256	/* Number of supported devices */
#define SND_DEV_CTL	0	/* Control port /dev/mixer */
#define SND_DEV_SEQ	1	/* Sequencer output /dev/sequencer (FM
				   synthesizer and MIDI output) */
#define SND_DEV_MIDIN	2	/* Raw midi access */
#define SND_DEV_DSP	3	/* Digitized voice /dev/dsp */
#define SND_DEV_AUDIO	4	/* Sparc compatible /dev/audio */
#define SND_DEV_DSP16	5	/* Like /dev/dsp but 16 bits/sample */
#define SND_DEV_STATUS	6	/* /dev/sndstat */
/* #7 not in use now. Was in 2.4. Free for use after v3.0. */
#define SND_DEV_SEQ2	8	/* /dev/sequencer, level 2 interface */
#define SND_DEV_SNDPROC 9	/* /dev/sndproc for programmable devices */
#define SND_DEV_PSS	SND_DEV_SNDPROC

/* switch on various prinks */
#define DEBUG_DMASOUND 1

#define MAX_AUDIO_DEV	5
#define MAX_MIXER_DEV	4
#define MAX_SYNTH_DEV	3
#define MAX_MIDI_DEV	6
#define MAX_TIMER_DEV	3

#define MAX_CATCH_RADIUS	10

#define le2be16(x)	(((x)<<8 & 0xff00) | ((x)>>8 & 0x00ff))
#define le2be16dbl(x)	(((x)<<8 & 0xff00ff00) | ((x)>>8 & 0x00ff00ff))

#define IOCTL_IN(arg, ret) \
	do { int error = get_user(ret, (int __user *)(arg)); \
		if (error) return error; \
	} while (0)
#define IOCTL_OUT(arg, ret)	ioctl_return((int __user *)(arg), ret)

static inline int ioctl_return(int __user *addr, int value)
{
	return value < 0 ? value : put_user(value, addr);
}


    /*
     *  Configuration
     */

#undef HAS_8BIT_TABLES
#undef HAS_RECORD

#if defined(CONFIG_DMASOUND_ATARI) || defined(CONFIG_DMASOUND_ATARI_MODULE) ||\
    defined(CONFIG_DMASOUND_PAULA) || defined(CONFIG_DMASOUND_PAULA_MODULE) ||\
    defined(CONFIG_DMASOUND_Q40) || defined(CONFIG_DMASOUND_Q40_MODULE)
#define HAS_8BIT_TABLES
#define MIN_BUFFERS	4
#define MIN_BUFSIZE	(1<<12)	/* in bytes (- where does this come from ?) */
#define MIN_FRAG_SIZE	8	/* not 100% sure about this */
#define MAX_BUFSIZE	(1<<17)	/* Limit for Amiga is 128 kb */
#define MAX_FRAG_SIZE	15	/* allow *4 for mono-8 => stereo-16 (for multi) */

#else /* is pmac and multi is off */

#define MIN_BUFFERS	2
#define MIN_BUFSIZE	(1<<8)	/* in bytes */
#define MIN_FRAG_SIZE	8
#define MAX_BUFSIZE	(1<<18)	/* this is somewhat arbitrary for pmac */
#define MAX_FRAG_SIZE	16	/* need to allow *4 for mono-8 => stereo-16 */
#endif

#define DEFAULT_N_BUFFERS 4
#define DEFAULT_BUFF_SIZE (1<<15)

#if defined(CONFIG_DMASOUND_PMAC) || defined(CONFIG_DMASOUND_PMAC_MODULE)
#define HAS_RECORD
#endif

    /*
     *  Initialization
     */

extern int dmasound_init(void);
#ifdef MODULE
extern void dmasound_deinit(void);
#else
#define dmasound_deinit()	do { } while (0)
#endif

/* description of the set-up applies to either hard or soft settings */

typedef struct {
    int format;		/* AFMT_* */
    int stereo;		/* 0 = mono, 1 = stereo */
    int size;		/* 8/16 bit*/
    int speed;		/* speed */
} SETTINGS;

    /*
     *  Machine definitions
     */

typedef struct {
    const char *name;
    const char *name2;
    struct module *owner;
    void *(*dma_alloc)(unsigned int, int);
    void (*dma_free)(void *, unsigned int);
    int (*irqinit)(void);
#ifdef MODULE
    void (*irqcleanup)(void);
#endif
    void (*init)(void);
    void (*silence)(void);
    int (*setFormat)(int);
    int (*setVolume)(int);
    int (*setBass)(int);
    int (*setTreble)(int);
    int (*setGain)(int);
    void (*play)(void);
    void (*record)(void);		/* optional */
    void (*mixer_init)(void);		/* optional */
    int (*mixer_ioctl)(u_int, u_long);	/* optional */
    int (*write_sq_setup)(void);	/* optional */
    int (*read_sq_setup)(void);		/* optional */
    int (*sq_open)(mode_t);		/* optional */
    int (*state_info)(char *, size_t);	/* optional */
    void (*abort_read)(void);		/* optional */
    int min_dsp_speed;
    int max_dsp_speed;
    int version ;
    int hardware_afmts ;		/* OSS says we only return h'ware info */
					/* when queried via SNDCTL_DSP_GETFMTS */
    int capabilities ;		/* low-level reply to SNDCTL_DSP_GETCAPS */
    SETTINGS default_hard ;	/* open() or init() should set something valid */
    SETTINGS default_soft ;	/* you can make it look like old OSS, if you want to */
} MACHINE;

    /*
     *  Low level stuff
     */

typedef struct {
    ssize_t (*ct_ulaw)(const u_char __user *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_alaw)(const u_char __user *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s8)(const u_char __user *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u8)(const u_char __user *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s16be)(const u_char __user *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u16be)(const u_char __user *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s16le)(const u_char __user *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u16le)(const u_char __user *, size_t, u_char *, ssize_t *, ssize_t);
} TRANS;

struct sound_settings {
    MACHINE mach;	/* machine dependent things */
    SETTINGS hard;	/* hardware settings */
    SETTINGS soft;	/* software settings */
    SETTINGS dsp;	/* /dev/dsp default settings */
    TRANS *trans_write;	/* supported translations */
#ifdef HAS_RECORD
    TRANS *trans_read;	/* supported translations */
#endif
    int volume_left;	/* volume (range is machine dependent) */
    int volume_right;
    int bass;		/* tone (range is machine dependent) */
    int treble;
    int gain;
    int minDev;		/* minor device number currently open */
    spinlock_t lock;
};

extern struct sound_settings dmasound;

#ifdef HAS_8BIT_TABLES
extern char dmasound_ulaw2dma8[];
extern char dmasound_alaw2dma8[];
#endif

    /*
     *  Mid level stuff
     */

static inline int dmasound_set_volume(int volume)
{
	return dmasound.mach.setVolume(volume);
}

static inline int dmasound_set_bass(int bass)
{
	return dmasound.mach.setBass ? dmasound.mach.setBass(bass) : 50;
}

static inline int dmasound_set_treble(int treble)
{
	return dmasound.mach.setTreble ? dmasound.mach.setTreble(treble) : 50;
}

static inline int dmasound_set_gain(int gain)
{
	return dmasound.mach.setGain ? dmasound.mach.setGain(gain) : 100;
}


    /*
     * Sound queue stuff, the heart of the driver
     */

struct sound_queue {
    /* buffers allocated for this queue */
    int numBufs;		/* real limits on what the user can have */
    int bufSize;		/* in bytes */
    char **buffers;

    /* current parameters */
    int locked ;		/* params cannot be modified when != 0 */
    int user_frags ;		/* user requests this many */
    int user_frag_size ;	/* of this size */
    int max_count;		/* actual # fragments <= numBufs */
    int block_size;		/* internal block size in bytes */
    int max_active;		/* in-use fragments <= max_count */

    /* it shouldn't be necessary to declare any of these volatile */
    int front, rear, count;
    int rear_size;
    /*
     *	The use of the playing field depends on the hardware
     *
     *	Atari, PMac: The number of frames that are loaded/playing
     *
     *	Amiga: Bit 0 is set: a frame is loaded
     *	       Bit 1 is set: a frame is playing
     */
    int active;
    wait_queue_head_t action_queue, open_queue, sync_queue;
    int open_mode;
    int busy, syncing, xruns, died;
};

#define SLEEP(queue)		interruptible_sleep_on_timeout(&queue, HZ)
#define WAKE_UP(queue)		(wake_up_interruptible(&queue))

extern struct sound_queue dmasound_write_sq;
#define write_sq	dmasound_write_sq

#ifdef HAS_RECORD
extern struct sound_queue dmasound_read_sq;
#define read_sq		dmasound_read_sq
#endif

extern int dmasound_catchRadius;
#define catchRadius	dmasound_catchRadius

/* define the value to be put in the byte-swap reg in mac-io
   when we want it to swap for us.
*/
#define BS_VAL 1

#define SW_INPUT_VOLUME_SCALE	4
#define SW_INPUT_VOLUME_DEFAULT	(128 / SW_INPUT_VOLUME_SCALE)

extern int expand_bal;	/* Balance factor for expanding (not volume!) */
extern int expand_read_bal;	/* Balance factor for reading */
extern uint software_input_volume; /* software implemented recording volume! */

#endif /* _dmasound_h_ */
