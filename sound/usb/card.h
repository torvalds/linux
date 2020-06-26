/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBAUDIO_CARD_H
#define __USBAUDIO_CARD_H

#define MAX_NR_RATES	1024
#define MAX_PACKS	6		/* per URB */
#define MAX_PACKS_HS	(MAX_PACKS * 8)	/* in high speed mode */
#define MAX_URBS	12
#define SYNC_URBS	4	/* always four urbs for sync */
#define MAX_QUEUE	18	/* try not to exceed this queue length, in ms */

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
	unsigned char protocol;		/* UAC_VERSION_1/2/3 */
	unsigned int maxpacksize;	/* max. packet size */
	unsigned int rates;		/* rate bitmasks */
	unsigned int rate_min, rate_max;	/* min/max rates */
	unsigned int nr_rates;		/* number of rate table entries */
	unsigned int *rate_table;	/* rate table */
	unsigned char clock;		/* associated clock */
	struct snd_pcm_chmap_elem *chmap; /* (optional) channel map */
	bool dsd_dop;			/* add DOP headers in case of DSD samples */
	bool dsd_bitrev;		/* reverse the bits of each DSD sample */
	bool dsd_raw;			/* altsetting is raw DSD */
};

struct snd_usb_substream;
struct snd_usb_endpoint;
struct snd_usb_power_domain;

struct snd_urb_ctx {
	struct urb *urb;
	unsigned int buffer_size;	/* size of data buffer, if data URB */
	struct snd_usb_substream *subs;
	struct snd_usb_endpoint *ep;
	int index;	/* index for urb array */
	int packets;	/* number of packets per urb */
	int packet_size[MAX_PACKS_HS]; /* size of packets for next submission */
	struct list_head ready_list;
};

struct snd_usb_endpoint {
	struct snd_usb_audio *chip;

	int use_count;
	int ep_num;		/* the referenced endpoint number */
	int type;		/* SND_USB_ENDPOINT_TYPE_* */
	unsigned long flags;

	void (*prepare_data_urb) (struct snd_usb_substream *subs,
				  struct urb *urb);
	void (*retire_data_urb) (struct snd_usb_substream *subs,
				 struct urb *urb);

	struct snd_usb_substream *data_subs;
	struct snd_usb_endpoint *sync_master;
	struct snd_usb_endpoint *sync_slave;

	struct snd_urb_ctx urb[MAX_URBS];

	struct snd_usb_packet_info {
		uint32_t packet_size[MAX_PACKS_HS];
		int packets;
	} next_packet[MAX_URBS];
	int next_packet_read_pos, next_packet_write_pos;
	struct list_head ready_playback_urbs;

	unsigned int nurbs;		/* # urbs */
	unsigned long active_mask;	/* bitmask of active urbs */
	unsigned long unlink_mask;	/* bitmask of unlinked urbs */
	char *syncbuf;			/* sync buffer for all sync URBs */
	dma_addr_t sync_dma;		/* DMA address of syncbuf */

	unsigned int pipe;		/* the data i/o pipe */
	unsigned int freqn;		/* nominal sampling rate in fs/fps in Q16.16 format */
	unsigned int freqm;		/* momentary sampling rate in fs/fps in Q16.16 format */
	int	   freqshift;		/* how much to shift the feedback value to get Q16.16 */
	unsigned int freqmax;		/* maximum sampling rate, used for buffer management */
	unsigned int phase;		/* phase accumulator */
	unsigned int maxpacksize;	/* max packet size in bytes */
	unsigned int maxframesize;      /* max packet size in frames */
	unsigned int max_urb_frames;	/* max URB size in frames */
	unsigned int curpacksize;	/* current packet size in bytes (for capture) */
	unsigned int curframesize;      /* current packet size in frames (for capture) */
	unsigned int syncmaxsize;	/* sync endpoint packet size */
	unsigned int fill_max:1;	/* fill max packet size always */
	unsigned int tenor_fb_quirk:1;	/* corrupted feedback data */
	unsigned int datainterval;      /* log_2 of data packet interval */
	unsigned int syncinterval;	/* P for adaptive mode, 0 otherwise */
	unsigned char silence_value;
	unsigned int stride;
	int iface, altsetting;
	int skip_packets;		/* quirks for devices to ignore the first n packets
					   in a stream */

	spinlock_t lock;
	struct list_head list;
};

struct snd_usb_substream {
	struct snd_usb_stream *stream;
	struct usb_device *dev;
	struct snd_pcm_substream *pcm_substream;
	int direction;	/* playback or capture */
	int interface;	/* current interface */
	int endpoint;	/* assigned endpoint */
	struct audioformat *cur_audiofmt;	/* current audioformat pointer (for hw_params callback) */
	struct snd_usb_power_domain *str_pd;	/* UAC3 Power Domain for streaming path */
	snd_pcm_format_t pcm_format;	/* current audio format (for hw_params callback) */
	unsigned int channels;		/* current number of channels (for hw_params callback) */
	unsigned int channels_max;	/* max channels in the all audiofmts */
	unsigned int cur_rate;		/* current rate (for hw_params callback) */
	unsigned int period_bytes;	/* current period bytes (for hw_params callback) */
	unsigned int period_frames;	/* current frames per period */
	unsigned int buffer_periods;	/* current periods per buffer */
	unsigned int altset_idx;     /* USB data format: index of alternate setting */
	unsigned int txfr_quirk:1;	/* allow sub-frame alignment */
	unsigned int tx_length_quirk:1;	/* add length specifier to transfers */
	unsigned int fmt_type;		/* USB audio format type (1-3) */
	unsigned int pkt_offset_adj;	/* Bytes to drop from beginning of packets (for non-compliant devices) */

	unsigned int running: 1;	/* running status */

	unsigned int hwptr_done;	/* processed byte position in the buffer */
	unsigned int transfer_done;		/* processed frames since last period update */
	unsigned int frame_limit;	/* limits number of packets in URB */

	/* data and sync endpoints for this stream */
	unsigned int ep_num;		/* the endpoint number */
	struct snd_usb_endpoint *data_endpoint;
	struct snd_usb_endpoint *sync_endpoint;
	unsigned long flags;
	bool need_setup_ep;		/* (re)configure EP at prepare? */
	bool need_setup_fmt;		/* (re)configure fmt after resume? */
	unsigned int speed;		/* USB_SPEED_XXX */

	u64 formats;			/* format bitmasks (all or'ed) */
	unsigned int num_formats;		/* number of supported audio formats (list) */
	struct list_head fmt_list;	/* format list */
	struct snd_pcm_hw_constraint_list rate_list;	/* limited rates */
	spinlock_t lock;

	int last_frame_number;          /* stored frame number */
	int last_delay;                 /* stored delay */

	struct {
		int marker;
		int channel;
		int byte_idx;
	} dsd_dop;

	bool trigger_tstamp_pending_update; /* trigger timestamp being updated from initial estimate */
};

struct snd_usb_stream {
	struct snd_usb_audio *chip;
	struct snd_pcm *pcm;
	int pcm_index;
	unsigned int fmt_type;		/* USB audio format type (1-3) */
	struct snd_usb_substream substream[2];
	struct list_head list;
};

struct snd_usb_substream *find_snd_usb_substream(unsigned int card_num,
	unsigned int pcm_idx, unsigned int direction, struct snd_usb_audio
	**uchip, void (*disconnect_cb)(struct snd_usb_audio *chip));

#endif /* __USBAUDIO_CARD_H */
