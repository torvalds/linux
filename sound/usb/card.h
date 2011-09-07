#ifndef __USBAUDIO_CARD_H
#define __USBAUDIO_CARD_H

#define MAX_PACKS	20
#define MAX_PACKS_HS	(MAX_PACKS * 8)	/* in high speed mode */
#define MAX_URBS	8
#define SYNC_URBS	4	/* always four urbs for sync */
#define MAX_QUEUE	24	/* try not to exceed this queue length, in ms */

struct audioformat {
	struct list_head list;
	u64 formats;			/* ALSA format bits */
	unsigned int channels;		/* # channels */
	unsigned int fmt_type;		/* USB audio format type (1-3) */
	unsigned int frame_size;	/* samples per frame for non-audio */
	int iface;			/* interface number */
	unsigned char altsetting;	/* corresponding alternate setting */
	unsigned char altset_idx;	/* array index of altenate setting */
	unsigned char attributes;	/* corresponding attributes of cs endpoint */
	unsigned char endpoint;		/* endpoint */
	unsigned char ep_attr;		/* endpoint attributes */
	unsigned char datainterval;	/* log_2 of data packet interval */
	unsigned int maxpacksize;	/* max. packet size */
	unsigned int rates;		/* rate bitmasks */
	unsigned int rate_min, rate_max;	/* min/max rates */
	unsigned int nr_rates;		/* number of rate table entries */
	unsigned int *rate_table;	/* rate table */
	unsigned char clock;		/* associated clock */
};

struct snd_usb_substream;

struct snd_urb_ctx {
	struct urb *urb;
	unsigned int buffer_size;	/* size of data buffer, if data URB */
	struct snd_usb_substream *subs;
	int index;	/* index for urb array */
	int packets;	/* number of packets per urb */
};

struct snd_urb_ops {
	int (*prepare)(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime, struct urb *u);
	int (*retire)(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime, struct urb *u);
	int (*prepare_sync)(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime, struct urb *u);
	int (*retire_sync)(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime, struct urb *u);
};

struct snd_usb_substream {
	struct snd_usb_stream *stream;
	struct usb_device *dev;
	struct snd_pcm_substream *pcm_substream;
	int direction;	/* playback or capture */
	int interface;	/* current interface */
	int endpoint;	/* assigned endpoint */
	struct audioformat *cur_audiofmt;	/* current audioformat pointer (for hw_params callback) */
	unsigned int cur_rate;		/* current rate (for hw_params callback) */
	unsigned int period_bytes;	/* current period bytes (for hw_params callback) */
	unsigned int altset_idx;     /* USB data format: index of alternate setting */
	unsigned int datapipe;   /* the data i/o pipe */
	unsigned int syncpipe;   /* 1 - async out or adaptive in */
	unsigned int datainterval;	/* log_2 of data packet interval */
	unsigned int syncinterval;  /* P for adaptive mode, 0 otherwise */
	unsigned int freqn;      /* nominal sampling rate in fs/fps in Q16.16 format */
	unsigned int freqm;      /* momentary sampling rate in fs/fps in Q16.16 format */
	int          freqshift;  /* how much to shift the feedback value to get Q16.16 */
	unsigned int freqmax;    /* maximum sampling rate, used for buffer management */
	unsigned int phase;      /* phase accumulator */
	unsigned int maxpacksize;	/* max packet size in bytes */
	unsigned int maxframesize;	/* max packet size in frames */
	unsigned int curpacksize;	/* current packet size in bytes (for capture) */
	unsigned int curframesize;	/* current packet size in frames (for capture) */
	unsigned int syncmaxsize;	/* sync endpoint packet size */
	unsigned int fill_max: 1;	/* fill max packet size always */
	unsigned int txfr_quirk:1;	/* allow sub-frame alignment */
	unsigned int fmt_type;		/* USB audio format type (1-3) */

	unsigned int running: 1;	/* running status */

	unsigned int hwptr_done;	/* processed byte position in the buffer */
	unsigned int transfer_done;		/* processed frames since last period update */
	unsigned long active_mask;	/* bitmask of active urbs */
	unsigned long unlink_mask;	/* bitmask of unlinked urbs */

	unsigned int nurbs;			/* # urbs */
	struct snd_urb_ctx dataurb[MAX_URBS];	/* data urb table */
	struct snd_urb_ctx syncurb[SYNC_URBS];	/* sync urb table */
	char *syncbuf;				/* sync buffer for all sync URBs */
	dma_addr_t sync_dma;			/* DMA address of syncbuf */

	u64 formats;			/* format bitmasks (all or'ed) */
	unsigned int num_formats;		/* number of supported audio formats (list) */
	struct list_head fmt_list;	/* format list */
	struct snd_pcm_hw_constraint_list rate_list;	/* limited rates */
	spinlock_t lock;

	struct snd_urb_ops ops;		/* callbacks (must be filled at init) */
	int last_frame_number;          /* stored frame number */
	int last_delay;                 /* stored delay */
};

struct snd_usb_stream {
	struct snd_usb_audio *chip;
	struct snd_pcm *pcm;
	int pcm_index;
	unsigned int fmt_type;		/* USB audio format type (1-3) */
	struct snd_usb_substream substream[2];
	struct list_head list;
};

#endif /* __USBAUDIO_CARD_H */
