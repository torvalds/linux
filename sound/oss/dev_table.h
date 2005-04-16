/*
 *	dev_table.h
 *
 *	Global definitions for device call tables
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


#ifndef _DEV_TABLE_H_
#define _DEV_TABLE_H_

#include <linux/spinlock.h>
/*
 * Sound card numbers 27 to 999. (1 to 26 are defined in soundcard.h)
 * Numbers 1000 to N are reserved for driver's internal use.
 */

#define SNDCARD_DESKPROXL		27	/* Compaq Deskpro XL */
#define SNDCARD_VIDC			28	/* ARMs VIDC */
#define SNDCARD_SBPNP			29
#define SNDCARD_SOFTOSS			36
#define SNDCARD_VMIDI			37
#define SNDCARD_OPL3SA1			38	/* Note: clash in msnd.h */
#define SNDCARD_OPL3SA1_SB		39
#define SNDCARD_OPL3SA1_MPU		40
#define SNDCARD_WAVEFRONT               41
#define SNDCARD_OPL3SA2                 42
#define SNDCARD_OPL3SA2_MPU             43
#define SNDCARD_WAVEARTIST              44	/* Waveartist */
#define SNDCARD_OPL3SA2_MSS             45	/* Originally missed */
#define SNDCARD_AD1816                  88

/*
 *	NOTE! 	NOTE!	NOTE!	NOTE!
 *
 *	If you modify this file, please check the dev_table.c also.
 *
 *	NOTE! 	NOTE!	NOTE!	NOTE!
 */

struct driver_info 
{
	char *driver_id;
	int card_subtype;	/* Driver specific. Usually 0 */
	int card_type;		/*	From soundcard.h	*/
	char *name;
	void (*attach) (struct address_info *hw_config);
	int (*probe) (struct address_info *hw_config);
	void (*unload) (struct address_info *hw_config);
};

struct card_info 
{
	int card_type;	/* Link (search key) to the driver list */
	struct address_info config;
	int enabled;
	void *for_driver_use;
};


/*
 * Device specific parameters (used only by dmabuf.c)
 */
#define MAX_SUB_BUFFERS		(32*MAX_REALTIME_FACTOR)

#define DMODE_NONE		0
#define DMODE_OUTPUT		PCM_ENABLE_OUTPUT
#define DMODE_INPUT		PCM_ENABLE_INPUT

struct dma_buffparms 
{
	int      dma_mode;	/* DMODE_INPUT, DMODE_OUTPUT or DMODE_NONE */
	int	 closing;

	/*
 	 * Pointers to raw buffers
 	 */

  	char     *raw_buf;
    	unsigned long   raw_buf_phys;
	int buffsize;

     	/*
         * Device state tables
         */

	unsigned long flags;
#define DMA_BUSY	0x00000001
#define DMA_RESTART	0x00000002
#define DMA_ACTIVE	0x00000004
#define DMA_STARTED	0x00000008
#define DMA_EMPTY	0x00000010	
#define DMA_ALLOC_DONE	0x00000020
#define DMA_SYNCING	0x00000040
#define DMA_DIRTY	0x00000080
#define DMA_POST	0x00000100
#define DMA_NODMA	0x00000200
#define DMA_NOTIMEOUT	0x00000400

	int      open_mode;

	/*
	 * Queue parameters.
	 */
	int      qlen;
	int      qhead;
	int      qtail;
	spinlock_t lock;
		
	int	 cfrag;	/* Current incomplete fragment (write) */

	int      nbufs;
	int      counts[MAX_SUB_BUFFERS];
	int      subdivision;

	int      fragment_size;
        int	 needs_reorg;
	int	 max_fragments;

	int	 bytes_in_use;

	int	 underrun_count;
	unsigned long	 byte_counter;
	unsigned long	 user_counter;
	unsigned long	 max_byte_counter;
	int	 data_rate; /* Bytes/second */

	int	 mapping_flags;
#define			DMA_MAP_MAPPED		0x00000001
	char	neutral_byte;
	int	dma;		/* DMA channel */

	int     applic_profile;	/* Application profile (APF_*) */
	/* Interrupt callback stuff */
	void (*audio_callback) (int dev, int parm);
	int callback_parm;

	int	 buf_flags[MAX_SUB_BUFFERS];
#define		 BUFF_EOF		0x00000001 /* Increment eof count */
#define		 BUFF_DIRTY		0x00000002 /* Buffer written */
};

/*
 * Structure for use with various microcontrollers and DSP processors 
 * in the recent sound cards.
 */
typedef struct coproc_operations 
{
	char name[64];
	struct module *owner;
	int (*open) (void *devc, int sub_device);
	void (*close) (void *devc, int sub_device);
	int (*ioctl) (void *devc, unsigned int cmd, void __user * arg, int local);
	void (*reset) (void *devc);

	void *devc;		/* Driver specific info */
} coproc_operations;

struct audio_driver 
{
	struct module *owner;
	int (*open) (int dev, int mode);
	void (*close) (int dev);
	void (*output_block) (int dev, unsigned long buf, 
			      int count, int intrflag);
	void (*start_input) (int dev, unsigned long buf, 
			     int count, int intrflag);
	int (*ioctl) (int dev, unsigned int cmd, void __user * arg);
	int (*prepare_for_input) (int dev, int bufsize, int nbufs);
	int (*prepare_for_output) (int dev, int bufsize, int nbufs);
	void (*halt_io) (int dev);
	int (*local_qlen)(int dev);
	void (*copy_user) (int dev,
			char *localbuf, int localoffs,
                        const char __user *userbuf, int useroffs,
                        int max_in, int max_out,
                        int *used, int *returned,
                        int len);
	void (*halt_input) (int dev);
	void (*halt_output) (int dev);
	void (*trigger) (int dev, int bits);
	int (*set_speed)(int dev, int speed);
	unsigned int (*set_bits)(int dev, unsigned int bits);
	short (*set_channels)(int dev, short channels);
	void (*postprocess_write)(int dev); 	/* Device spesific postprocessing for written data */
	void (*preprocess_read)(int dev); 	/* Device spesific preprocessing for read data */
	void (*mmap)(int dev);
};

struct audio_operations 
{
        char name[128];
	int flags;
#define NOTHING_SPECIAL 	0x00
#define NEEDS_RESTART		0x01
#define DMA_AUTOMODE		0x02
#define DMA_DUPLEX		0x04
#define DMA_PSEUDO_AUTOMODE	0x08
#define DMA_HARDSTOP		0x10
#define DMA_EXACT		0x40
#define DMA_NORESET		0x80
	int  format_mask;	/* Bitmask for supported audio formats */
	void *devc;		/* Driver specific info */
	struct audio_driver *d;
	void *portc;		/* Driver specific info */
	struct dma_buffparms *dmap_in, *dmap_out;
	struct coproc_operations *coproc;
	int mixer_dev;
	int enable_bits;
 	int open_mode;
	int go;
	int min_fragment;	/* 0 == unlimited */
	int max_fragment;	/* 0 == unlimited */
	int parent_dev;		/* 0 -> no parent, 1 to n -> parent=parent_dev+1 */

	/* fields formerly in dmabuf.c */
	wait_queue_head_t in_sleeper;
	wait_queue_head_t out_sleeper;
	wait_queue_head_t poll_sleeper;

	/* fields formerly in audio.c */
	int audio_mode;

#define		AM_NONE		0
#define		AM_WRITE	OPEN_WRITE
#define 	AM_READ		OPEN_READ

	int local_format;
	int audio_format;
	int local_conversion;
#define CNV_MU_LAW	0x00000001

	/* large structures at the end to keep offsets small */
	struct dma_buffparms dmaps[2];
};

int *load_mixer_volumes(char *name, int *levels, int present);

struct mixer_operations 
{
	struct module *owner;
	char id[16];
	char name[64];
	int (*ioctl) (int dev, unsigned int cmd, void __user * arg);
	
	void *devc;
	int modify_counter;
};

struct synth_operations 
{
	struct module *owner;
	char *id;	/* Unique identifier (ASCII) max 29 char */
	struct synth_info *info;
	int midi_dev;
	int synth_type;
	int synth_subtype;

	int (*open) (int dev, int mode);
	void (*close) (int dev);
	int (*ioctl) (int dev, unsigned int cmd, void __user * arg);
	int (*kill_note) (int dev, int voice, int note, int velocity);
	int (*start_note) (int dev, int voice, int note, int velocity);
	int (*set_instr) (int dev, int voice, int instr);
	void (*reset) (int dev);
	void (*hw_control) (int dev, unsigned char *event);
	int (*load_patch) (int dev, int format, const char __user *addr,
	     int offs, int count, int pmgr_flag);
	void (*aftertouch) (int dev, int voice, int pressure);
	void (*controller) (int dev, int voice, int ctrl_num, int value);
	void (*panning) (int dev, int voice, int value);
	void (*volume_method) (int dev, int mode);
	void (*bender) (int dev, int chn, int value);
	int (*alloc_voice) (int dev, int chn, int note, struct voice_alloc_info *alloc);
	void (*setup_voice) (int dev, int voice, int chn);
	int (*send_sysex)(int dev, unsigned char *bytes, int len);

 	struct voice_alloc_info alloc;
 	struct channel_info chn_info[16];
	int emulation;
#define	EMU_GM			1	/* General MIDI */
#define	EMU_XG			2	/* Yamaha XG */
#define MAX_SYSEX_BUF	64
	unsigned char sysex_buf[MAX_SYSEX_BUF];
	int sysex_ptr;
};

struct midi_input_info 
{
	/* MIDI input scanner variables */
#define MI_MAX	10
	volatile int             m_busy;
    	unsigned char   m_buf[MI_MAX];
	unsigned char	m_prev_status;	/* For running status */
    	int             m_ptr;
#define MST_INIT			0
#define MST_DATA			1
#define MST_SYSEX			2
    	int             m_state;
    	int             m_left;
};

struct midi_operations 
{
	struct module *owner;
	struct midi_info info;
	struct synth_operations *converter;
	struct midi_input_info in_info;
	int (*open) (int dev, int mode,
		void (*inputintr)(int dev, unsigned char data),
		void (*outputintr)(int dev)
		);
	void (*close) (int dev);
	int (*ioctl) (int dev, unsigned int cmd, void __user * arg);
	int (*outputc) (int dev, unsigned char data);
	int (*start_read) (int dev);
	int (*end_read) (int dev);
	void (*kick)(int dev);
	int (*command) (int dev, unsigned char *data);
	int (*buffer_status) (int dev);
	int (*prefix_cmd) (int dev, unsigned char status);
	struct coproc_operations *coproc;
	void *devc;
};

struct sound_lowlev_timer 
{
	int dev;
	int priority;
	unsigned int (*tmr_start)(int dev, unsigned int usecs);
	void (*tmr_disable)(int dev);
	void (*tmr_restart)(int dev);
};

struct sound_timer_operations 
{
	struct module *owner;
	struct sound_timer_info info;
	int priority;
	int devlink;
	int (*open)(int dev, int mode);
	void (*close)(int dev);
	int (*event)(int dev, unsigned char *ev);
	unsigned long (*get_time)(int dev);
	int (*ioctl) (int dev, unsigned int cmd, void __user * arg);
	void (*arm_timer)(int dev, long time);
};

#ifdef _DEV_TABLE_C_   
struct audio_operations *audio_devs[MAX_AUDIO_DEV];
int num_audiodevs;
struct mixer_operations *mixer_devs[MAX_MIXER_DEV];
int num_mixers;
struct synth_operations *synth_devs[MAX_SYNTH_DEV+MAX_MIDI_DEV];
int num_synths;
struct midi_operations *midi_devs[MAX_MIDI_DEV];
int num_midis;

extern struct sound_timer_operations default_sound_timer;
struct sound_timer_operations *sound_timer_devs[MAX_TIMER_DEV] = {
	&default_sound_timer, NULL
}; 
int num_sound_timers = 1;
#else
extern struct audio_operations *audio_devs[MAX_AUDIO_DEV];
extern int num_audiodevs;
extern struct mixer_operations *mixer_devs[MAX_MIXER_DEV];
extern int num_mixers;
extern struct synth_operations *synth_devs[MAX_SYNTH_DEV+MAX_MIDI_DEV];
extern int num_synths;
extern struct midi_operations *midi_devs[MAX_MIDI_DEV];
extern int num_midis;
extern struct sound_timer_operations * sound_timer_devs[MAX_TIMER_DEV];
extern int num_sound_timers;
#endif	/* _DEV_TABLE_C_ */

extern int sound_map_buffer (int dev, struct dma_buffparms *dmap, buffmem_desc *info);
void sound_timer_init (struct sound_lowlev_timer *t, char *name);
void sound_dma_intr (int dev, struct dma_buffparms *dmap, int chan);

#define AUDIO_DRIVER_VERSION	2
#define MIXER_DRIVER_VERSION	2
int sound_install_audiodrv(int vers, char *name, struct audio_driver *driver,
			int driver_size, int flags, unsigned int format_mask,
			void *devc, int dma1, int dma2);
int sound_install_mixer(int vers, char *name, struct mixer_operations *driver,
			int driver_size, void *devc);

void sound_unload_audiodev(int dev);
void sound_unload_mixerdev(int dev);
void sound_unload_mididev(int dev);
void sound_unload_synthdev(int dev);
void sound_unload_timerdev(int dev);
int sound_alloc_mixerdev(void);
int sound_alloc_timerdev(void);
int sound_alloc_synthdev(void);
int sound_alloc_mididev(void);
#endif	/* _DEV_TABLE_H_ */

