/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOUND_SND_WAVEFRONT_H__
#define __SOUND_SND_WAVEFRONT_H__

#include <sound/mpu401.h>
#include <sound/hwdep.h>
#include <sound/rawmidi.h>
#include <sound/wavefront.h>  /* generic OSS/ALSA/user-level wavefront header */

/* MIDI interface */

struct _snd_wavefront_midi;
struct _snd_wavefront_card;
struct _snd_wavefront;

typedef struct _snd_wavefront_midi snd_wavefront_midi_t;
typedef struct _snd_wavefront_card snd_wavefront_card_t;
typedef struct _snd_wavefront snd_wavefront_t;

typedef enum { internal_mpu = 0, external_mpu = 1 } snd_wavefront_mpu_id;

struct _snd_wavefront_midi {
        unsigned long            base;        /* I/O port address */
	char                     isvirtual;   /* doing virtual MIDI stuff ? */
	char			 istimer;     /* timer is used */
        snd_wavefront_mpu_id     output_mpu;  /* most-recently-used */
        snd_wavefront_mpu_id     input_mpu;   /* most-recently-used */
        unsigned int             mode[2];     /* MPU401_MODE_XXX */
	struct snd_rawmidi_substream	 *substream_output[2];
	struct snd_rawmidi_substream	 *substream_input[2];
	struct timer_list	 timer;
	snd_wavefront_card_t	 *timer_card;
        spinlock_t               open;
        spinlock_t               virtual;     /* protects isvirtual */
};

#define	OUTPUT_READY	0x40
#define	INPUT_AVAIL	0x80
#define	MPU_ACK		0xFE
#define	UART_MODE_ON	0x3F

extern const struct snd_rawmidi_ops snd_wavefront_midi_output;
extern const struct snd_rawmidi_ops snd_wavefront_midi_input;

extern void   snd_wavefront_midi_enable_virtual (snd_wavefront_card_t *);
extern void   snd_wavefront_midi_disable_virtual (snd_wavefront_card_t *);
extern void   snd_wavefront_midi_interrupt (snd_wavefront_card_t *);
extern int    snd_wavefront_midi_start (snd_wavefront_card_t *);

struct _snd_wavefront {
	unsigned long    irq;   /* "you were one, one of the few ..." */
	unsigned long    base;  /* low i/o port address */
	struct resource	 *res_base; /* i/o port resource allocation */

#define mpu_data_port    base 
#define mpu_command_port base + 1 /* write semantics */
#define mpu_status_port  base + 1 /* read semantics */
#define data_port        base + 2 
#define status_port      base + 3 /* read semantics */
#define control_port     base + 3 /* write semantics  */
#define block_port       base + 4 /* 16 bit, writeonly */
#define last_block_port  base + 6 /* 16 bit, writeonly */

	/* FX ports. These are mapped through the ICS2115 to the YS225.
	   The ICS2115 takes care of flipping the relevant pins on the
	   YS225 so that access to each of these ports does the right
	   thing. Note: these are NOT documented by Turtle Beach.
	*/

#define fx_status       base + 8 
#define fx_op           base + 8 
#define fx_lcr          base + 9 
#define fx_dsp_addr     base + 0xa
#define fx_dsp_page     base + 0xb 
#define fx_dsp_lsb      base + 0xc 
#define fx_dsp_msb      base + 0xd 
#define fx_mod_addr     base + 0xe
#define fx_mod_data     base + 0xf 

	volatile int irq_ok;               /* set by interrupt handler */
        volatile int irq_cnt;              /* ditto */
	char debug;                        /* debugging flags */
	int freemem;                       /* installed RAM, in bytes */ 

	char fw_version[2];                /* major = [0], minor = [1] */
	char hw_version[2];                /* major = [0], minor = [1] */
	char israw;                        /* needs Motorola microcode */
	char has_fx;                       /* has FX processor (Tropez+) */
	char fx_initialized;               /* FX's register pages initialized */
	char prog_status[WF_MAX_PROGRAM];  /* WF_SLOT_* */
	char patch_status[WF_MAX_PATCH];   /* WF_SLOT_* */
	char sample_status[WF_MAX_SAMPLE]; /* WF_ST_* | WF_SLOT_* */
	int samples_used;                  /* how many */
	char interrupts_are_midi;          /* h/w MPU interrupts enabled ? */
	char rom_samples_rdonly;           /* can we write on ROM samples */
	spinlock_t irq_lock;
	wait_queue_head_t interrupt_sleeper; 
	snd_wavefront_midi_t midi;         /* ICS2115 MIDI interface */
	struct snd_card *card;
};

struct _snd_wavefront_card {
	snd_wavefront_t wavefront;
#ifdef CONFIG_PNP
	struct pnp_dev *wss;
	struct pnp_dev *ctrl;
	struct pnp_dev *mpu;
	struct pnp_dev *synth;
#endif /* CONFIG_PNP */
};

extern void snd_wavefront_internal_interrupt (snd_wavefront_card_t *card);
extern int  snd_wavefront_start (snd_wavefront_t *dev);
extern int  snd_wavefront_detect (snd_wavefront_card_t *card);
extern int  snd_wavefront_cmd (snd_wavefront_t *, int, unsigned char *,
			       unsigned char *);

extern int snd_wavefront_synth_ioctl   (struct snd_hwdep *, 
					struct file *,
					unsigned int cmd, 
					unsigned long arg);
extern int  snd_wavefront_synth_open    (struct snd_hwdep *, struct file *);
extern int  snd_wavefront_synth_release (struct snd_hwdep *, struct file *);

/* FX processor - see also yss225.[ch] */

extern int  snd_wavefront_fx_start  (snd_wavefront_t *);
extern int  snd_wavefront_fx_detect (snd_wavefront_t *);
extern int  snd_wavefront_fx_ioctl  (struct snd_hwdep *, 
				     struct file *,
				     unsigned int cmd, 
				     unsigned long arg);
extern int snd_wavefront_fx_open    (struct snd_hwdep *, struct file *);
extern int snd_wavefront_fx_release (struct snd_hwdep *, struct file *);

#endif  /* __SOUND_SND_WAVEFRONT_H__ */
