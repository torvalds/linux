/* SPDX-License-Identifier: GPL-2.0 */
#ifndef USBUSX2Y_H
#define USBUSX2Y_H
#include "../usbaudio.h"
#include "../midi.h"
#include "usbus428ctldefs.h" 

#define NRURBS	        2	


#define URBS_AsyncSeq 10
#define URB_DataLen_AsyncSeq 32
struct snd_usX2Y_AsyncSeq {
	struct urb	*urb[URBS_AsyncSeq];
	char		*buffer;
};

struct snd_usX2Y_urbSeq {
	int	submitted;
	int	len;
	struct urb	*urb[0];
};

#include "usx2yhwdeppcm.h"

struct usX2Ydev {
	struct usb_device	*dev;
	int			card_index;
	int			stride;
	struct urb		*In04urb;
	void			*In04Buf;
	char			In04Last[24];
	unsigned		In04IntCalls;
	struct snd_usX2Y_urbSeq	*US04;
	wait_queue_head_t	In04WaitQueue;
	struct snd_usX2Y_AsyncSeq	AS04;
	unsigned int		rate,
				format;
	int			chip_status;
	struct mutex		pcm_mutex;
	struct us428ctls_sharedmem	*us428ctls_sharedmem;
	int			wait_iso_frame;
	wait_queue_head_t	us428ctls_wait_queue_head;
	struct snd_usX2Y_hwdep_pcm_shm	*hwdep_pcm_shm;
	struct snd_usX2Y_substream	*subs[4];
	struct snd_usX2Y_substream	* volatile  prepare_subs;
	wait_queue_head_t	prepare_wait_queue;
	struct list_head	midi_list;
	struct list_head	pcm_list;
	int			pcm_devs;
};


struct snd_usX2Y_substream {
	struct usX2Ydev	*usX2Y;
	struct snd_pcm_substream *pcm_substream;

	int			endpoint;		
	unsigned int		maxpacksize;		/* max packet size in bytes */

	atomic_t		state;
#define state_STOPPED	0
#define state_STARTING1 1
#define state_STARTING2 2
#define state_STARTING3 3
#define state_PREPARED	4
#define state_PRERUNNING  6
#define state_RUNNING	8

	int			hwptr;			/* free frame position in the buffer (only for playback) */
	int			hwptr_done;		/* processed frame position in the buffer */
	int			transfer_done;		/* processed frames since last period update */

	struct urb		*urb[NRURBS];	/* data urb table */
	struct urb		*completed_urb;
	char			*tmpbuf;			/* temporary buffer for playback */
};


#define usX2Y(c) ((struct usX2Ydev *)(c)->private_data)

int usX2Y_audio_create(struct snd_card *card);

int usX2Y_AsyncSeq04_init(struct usX2Ydev *usX2Y);
int usX2Y_In04_init(struct usX2Ydev *usX2Y);

#define NAME_ALLCAPS "US-X2Y"

#endif
