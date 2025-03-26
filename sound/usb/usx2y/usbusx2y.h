/* SPDX-License-Identifier: GPL-2.0 */
#ifndef USBUSX2Y_H
#define USBUSX2Y_H
#include "../usbaudio.h"
#include "../midi.h"
#include "usbus428ctldefs.h"

#define NRURBS	        2

/* Default value used for nr of packs per urb.
 * 1 to 4 have been tested ok on uhci.
 * To use 3 on ohci, you'd need a patch:
 * look for "0000425-linux-2.6.9-rc4-mm1_ohci-hcd.patch.gz" on
 * "https://bugtrack.alsa-project.org/alsa-bug/bug_view_page.php?bug_id=0000425"
 *
 * 1, 2 and 4 work out of the box on ohci, if I recall correctly.
 * Bigger is safer operation, smaller gives lower latencies.
 */
#define USX2Y_NRPACKS 4

#define USX2Y_NRPACKS_MAX 1024

/* If your system works ok with this module's parameter
 * nrpacks set to 1, you might as well comment
 * this define out, and thereby produce smaller, faster code.
 * You'd also set USX2Y_NRPACKS to 1 then.
 */
#define USX2Y_NRPACKS_VARIABLE 1

#ifdef USX2Y_NRPACKS_VARIABLE
extern int nrpacks;
#define nr_of_packs() nrpacks
#else
#define nr_of_packs() USX2Y_NRPACKS
#endif

#define URBS_ASYNC_SEQ 10
#define URB_DATA_LEN_ASYNC_SEQ 32
struct snd_usx2y_async_seq {
	struct urb	*urb[URBS_ASYNC_SEQ];
	char		*buffer;
};

struct snd_usx2y_urb_seq {
	int	submitted;
	int	len;
	struct urb	*urb[] __counted_by(len);
};

#include "usx2yhwdeppcm.h"

struct usx2ydev {
	struct usb_device	*dev;
	int			card_index;
	int			stride;
	struct urb		*in04_urb;
	void			*in04_buf;
	char			in04_last[24];
	unsigned int		in04_int_calls;
	struct snd_usx2y_urb_seq	*us04;
	wait_queue_head_t	in04_wait_queue;
	struct snd_usx2y_async_seq	as04;
	unsigned int		rate,
				format;
	int			chip_status;
	struct mutex		pcm_mutex;
	struct us428ctls_sharedmem	*us428ctls_sharedmem;
	int			wait_iso_frame;
	wait_queue_head_t	us428ctls_wait_queue_head;
	struct snd_usx2y_hwdep_pcm_shm	*hwdep_pcm_shm;
	struct snd_usx2y_substream	*subs[4];
	struct snd_usx2y_substream	* volatile  prepare_subs;
	wait_queue_head_t	prepare_wait_queue;
	struct list_head	midi_list;
	int			pcm_devs;
};


struct snd_usx2y_substream {
	struct usx2ydev	*usx2y;
	struct snd_pcm_substream *pcm_substream;

	int			endpoint;
	unsigned int		maxpacksize;		/* max packet size in bytes */

	atomic_t		state;
#define STATE_STOPPED	0
#define STATE_STARTING1 1
#define STATE_STARTING2 2
#define STATE_STARTING3 3
#define STATE_PREPARED	4
#define STATE_PRERUNNING  6
#define STATE_RUNNING	8

	int			hwptr;			/* free frame position in the buffer (only for playback) */
	int			hwptr_done;		/* processed frame position in the buffer */
	int			transfer_done;		/* processed frames since last period update */

	struct urb		*urb[NRURBS];	/* data urb table */
	struct urb		*completed_urb;
	char			*tmpbuf;			/* temporary buffer for playback */
};


#define usx2y(c) ((struct usx2ydev *)(c)->private_data)

int usx2y_audio_create(struct snd_card *card);

int usx2y_async_seq04_init(struct usx2ydev *usx2y);
int usx2y_in04_init(struct usx2ydev *usx2y);

#define NAME_ALLCAPS "US-X2Y"

#endif
