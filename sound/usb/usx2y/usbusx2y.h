#ifndef USBUSX2Y_H
#define USBUSX2Y_H
#include "../usbaudio.h"
#include "usbus428ctldefs.h" 

#define NRURBS	        2	


#define URBS_AsyncSeq 10
#define URB_DataLen_AsyncSeq 32
typedef struct {
	struct urb*	urb[URBS_AsyncSeq];
	char*   buffer;
} snd_usX2Y_AsyncSeq_t;

typedef struct {
	int	submitted;
	int	len;
	struct urb*	urb[0];
} snd_usX2Y_urbSeq_t;

typedef struct snd_usX2Y_substream snd_usX2Y_substream_t;
#include "usx2yhwdeppcm.h"

typedef struct {
	snd_usb_audio_t 	chip;
	int			stride;
	struct urb		*In04urb;
	void			*In04Buf;
	char			In04Last[24];
	unsigned		In04IntCalls;
	snd_usX2Y_urbSeq_t	*US04;
	wait_queue_head_t	In04WaitQueue;
	snd_usX2Y_AsyncSeq_t	AS04;
	unsigned int		rate,
				format;
	int			chip_status;
	struct semaphore	prepare_mutex;
	us428ctls_sharedmem_t	*us428ctls_sharedmem;
	int			wait_iso_frame;
	wait_queue_head_t	us428ctls_wait_queue_head;
	snd_usX2Y_hwdep_pcm_shm_t	*hwdep_pcm_shm;
	snd_usX2Y_substream_t	*subs[4];
	snd_usX2Y_substream_t	* volatile  prepare_subs;
	wait_queue_head_t	prepare_wait_queue;
} usX2Ydev_t;


struct snd_usX2Y_substream {
	usX2Ydev_t	*usX2Y;
	snd_pcm_substream_t *pcm_substream;

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


#define usX2Y(c) ((usX2Ydev_t*)(c)->private_data)

int usX2Y_audio_create(snd_card_t* card);

int usX2Y_AsyncSeq04_init(usX2Ydev_t* usX2Y);
int usX2Y_In04_init(usX2Ydev_t* usX2Y);

#define NAME_ALLCAPS "US-X2Y"

#endif
