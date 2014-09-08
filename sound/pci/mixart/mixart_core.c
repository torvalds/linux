/*
 * Driver for Digigram miXart soundcards
 *
 * low level interface with interrupt handling and mail box implementation
 *
 * Copyright (c) 2003 by Digigram <alsa@digigram.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <sound/core.h>
#include "mixart.h"
#include "mixart_hwdep.h"
#include "mixart_core.h"


#define MSG_TIMEOUT_JIFFIES         (400 * HZ) / 1000 /* 400 ms */

#define MSG_DESCRIPTOR_SIZE         0x24
#define MSG_HEADER_SIZE             (MSG_DESCRIPTOR_SIZE + 4)

#define MSG_DEFAULT_SIZE            512

#define MSG_TYPE_MASK               0x00000003    /* mask for following types */
#define MSG_TYPE_NOTIFY             0             /* embedded -> driver (only notification, do not get_msg() !) */
#define MSG_TYPE_COMMAND            1             /* driver <-> embedded (a command has no answer) */
#define MSG_TYPE_REQUEST            2             /* driver -> embedded (request will get an answer back) */
#define MSG_TYPE_ANSWER             3             /* embedded -> driver */
#define MSG_CANCEL_NOTIFY_MASK      0x80000000    /* this bit is set for a notification that has been canceled */


static int retrieve_msg_frame(struct mixart_mgr *mgr, u32 *msg_frame)
{
	/* read the message frame fifo */
	u32 headptr, tailptr;

	tailptr = readl_be(MIXART_MEM(mgr, MSG_OUTBOUND_POST_TAIL));
	headptr = readl_be(MIXART_MEM(mgr, MSG_OUTBOUND_POST_HEAD));

	if (tailptr == headptr)
		return 0; /* no message posted */

	if (tailptr < MSG_OUTBOUND_POST_STACK)
		return 0; /* error */
	if (tailptr >= MSG_OUTBOUND_POST_STACK + MSG_BOUND_STACK_SIZE)
		return 0; /* error */

	*msg_frame = readl_be(MIXART_MEM(mgr, tailptr));

	/* increment the tail index */
	tailptr += 4;
	if( tailptr >= (MSG_OUTBOUND_POST_STACK+MSG_BOUND_STACK_SIZE) )
		tailptr = MSG_OUTBOUND_POST_STACK;
	writel_be(tailptr, MIXART_MEM(mgr, MSG_OUTBOUND_POST_TAIL));

	return 1;
}

static int get_msg(struct mixart_mgr *mgr, struct mixart_msg *resp,
		   u32 msg_frame_address )
{
	unsigned long flags;
	u32  headptr;
	u32  size;
	int  err;
#ifndef __BIG_ENDIAN
	unsigned int i;
#endif

	spin_lock_irqsave(&mgr->msg_lock, flags);
	err = 0;

	/* copy message descriptor from miXart to driver */
	size                =  readl_be(MIXART_MEM(mgr, msg_frame_address));       /* size of descriptor + response */
	resp->message_id    =  readl_be(MIXART_MEM(mgr, msg_frame_address + 4));   /* dwMessageID */
	resp->uid.object_id =  readl_be(MIXART_MEM(mgr, msg_frame_address + 8));   /* uidDest */
	resp->uid.desc      =  readl_be(MIXART_MEM(mgr, msg_frame_address + 12));  /* */

	if( (size < MSG_DESCRIPTOR_SIZE) || (resp->size < (size - MSG_DESCRIPTOR_SIZE))) {
		err = -EINVAL;
		dev_err(&mgr->pci->dev,
			"problem with response size = %d\n", size);
		goto _clean_exit;
	}
	size -= MSG_DESCRIPTOR_SIZE;

	memcpy_fromio(resp->data, MIXART_MEM(mgr, msg_frame_address + MSG_HEADER_SIZE ), size);
	resp->size = size;

	/* swap if necessary */
#ifndef __BIG_ENDIAN
	size /= 4; /* u32 size */
	for(i=0; i < size; i++) {
		((u32*)resp->data)[i] = be32_to_cpu(((u32*)resp->data)[i]);
	}
#endif

	/*
	 * free message frame address
	 */
	headptr = readl_be(MIXART_MEM(mgr, MSG_OUTBOUND_FREE_HEAD));

	if( (headptr < MSG_OUTBOUND_FREE_STACK) || ( headptr >= (MSG_OUTBOUND_FREE_STACK+MSG_BOUND_STACK_SIZE))) {
		err = -EINVAL;
		goto _clean_exit;
	}

	/* give address back to outbound fifo */
	writel_be(msg_frame_address, MIXART_MEM(mgr, headptr));

	/* increment the outbound free head */
	headptr += 4;
	if( headptr >= (MSG_OUTBOUND_FREE_STACK+MSG_BOUND_STACK_SIZE) )
		headptr = MSG_OUTBOUND_FREE_STACK;

	writel_be(headptr, MIXART_MEM(mgr, MSG_OUTBOUND_FREE_HEAD));

 _clean_exit:
	spin_unlock_irqrestore(&mgr->msg_lock, flags);

	return err;
}


/*
 * send a message to miXart. return: the msg_frame used for this message
 */
/* call with mgr->msg_lock held! */
static int send_msg( struct mixart_mgr *mgr,
		     struct mixart_msg *msg,
		     int max_answersize,
		     int mark_pending,
		     u32 *msg_event)
{
	u32 headptr, tailptr;
	u32 msg_frame_address;
	int i;

	if (snd_BUG_ON(msg->size % 4))
		return -EINVAL;

	/* get message frame address */
	tailptr = readl_be(MIXART_MEM(mgr, MSG_INBOUND_FREE_TAIL));
	headptr = readl_be(MIXART_MEM(mgr, MSG_INBOUND_FREE_HEAD));

	if (tailptr == headptr) {
		dev_err(&mgr->pci->dev, "error: no message frame available\n");
		return -EBUSY;
	}

	if( (tailptr < MSG_INBOUND_FREE_STACK) || (tailptr >= (MSG_INBOUND_FREE_STACK+MSG_BOUND_STACK_SIZE))) {
		return -EINVAL;
	}

	msg_frame_address = readl_be(MIXART_MEM(mgr, tailptr));
	writel(0, MIXART_MEM(mgr, tailptr)); /* set address to zero on this fifo position */

	/* increment the inbound free tail */
	tailptr += 4;
	if( tailptr >= (MSG_INBOUND_FREE_STACK+MSG_BOUND_STACK_SIZE) )
		tailptr = MSG_INBOUND_FREE_STACK;

	writel_be(tailptr, MIXART_MEM(mgr, MSG_INBOUND_FREE_TAIL));

	/* TODO : use memcpy_toio() with intermediate buffer to copy the message */

	/* copy message descriptor to card memory */
	writel_be( msg->size + MSG_DESCRIPTOR_SIZE,      MIXART_MEM(mgr, msg_frame_address) );      /* size of descriptor + request */
	writel_be( msg->message_id ,                     MIXART_MEM(mgr, msg_frame_address + 4) );  /* dwMessageID */
	writel_be( msg->uid.object_id,                   MIXART_MEM(mgr, msg_frame_address + 8) );  /* uidDest */
	writel_be( msg->uid.desc,                        MIXART_MEM(mgr, msg_frame_address + 12) ); /* */
	writel_be( MSG_DESCRIPTOR_SIZE,                  MIXART_MEM(mgr, msg_frame_address + 16) ); /* SizeHeader */
	writel_be( MSG_DESCRIPTOR_SIZE,                  MIXART_MEM(mgr, msg_frame_address + 20) ); /* OffsetDLL_T16 */
	writel_be( msg->size,                            MIXART_MEM(mgr, msg_frame_address + 24) ); /* SizeDLL_T16 */
	writel_be( MSG_DESCRIPTOR_SIZE,                  MIXART_MEM(mgr, msg_frame_address + 28) ); /* OffsetDLL_DRV */
	writel_be( 0,                                    MIXART_MEM(mgr, msg_frame_address + 32) ); /* SizeDLL_DRV */
	writel_be( MSG_DESCRIPTOR_SIZE + max_answersize, MIXART_MEM(mgr, msg_frame_address + 36) ); /* dwExpectedAnswerSize */

	/* copy message data to card memory */
	for( i=0; i < msg->size; i+=4 ) {
		writel_be( *(u32*)(msg->data + i), MIXART_MEM(mgr, MSG_HEADER_SIZE + msg_frame_address + i)  );
	}

	if( mark_pending ) {
		if( *msg_event ) {
			/* the pending event is the notification we wait for ! */
			mgr->pending_event = *msg_event;
		}
		else {
			/* the pending event is the answer we wait for (same address than the request)! */
			mgr->pending_event = msg_frame_address;

			/* copy address back to caller */
			*msg_event = msg_frame_address;
		}
	}

	/* mark the frame as a request (will have an answer) */
	msg_frame_address |= MSG_TYPE_REQUEST;

	/* post the frame */
	headptr = readl_be(MIXART_MEM(mgr, MSG_INBOUND_POST_HEAD));

	if( (headptr < MSG_INBOUND_POST_STACK) || (headptr >= (MSG_INBOUND_POST_STACK+MSG_BOUND_STACK_SIZE))) {
		return -EINVAL;
	}

	writel_be(msg_frame_address, MIXART_MEM(mgr, headptr));

	/* increment the inbound post head */
	headptr += 4;
	if( headptr >= (MSG_INBOUND_POST_STACK+MSG_BOUND_STACK_SIZE) )
		headptr = MSG_INBOUND_POST_STACK;

	writel_be(headptr, MIXART_MEM(mgr, MSG_INBOUND_POST_HEAD));

	return 0;
}


int snd_mixart_send_msg(struct mixart_mgr *mgr, struct mixart_msg *request, int max_resp_size, void *resp_data)
{
	struct mixart_msg resp;
	u32 msg_frame = 0; /* set to 0, so it's no notification to wait for, but the answer */
	int err;
	wait_queue_t wait;
	long timeout;

	mutex_lock(&mgr->msg_mutex);

	init_waitqueue_entry(&wait, current);

	spin_lock_irq(&mgr->msg_lock);
	/* send the message */
	err = send_msg(mgr, request, max_resp_size, 1, &msg_frame);  /* send and mark the answer pending */
	if (err) {
		spin_unlock_irq(&mgr->msg_lock);
		mutex_unlock(&mgr->msg_mutex);
		return err;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&mgr->msg_sleep, &wait);
	spin_unlock_irq(&mgr->msg_lock);
	timeout = schedule_timeout(MSG_TIMEOUT_JIFFIES);
	remove_wait_queue(&mgr->msg_sleep, &wait);

	if (! timeout) {
		/* error - no ack */
		mutex_unlock(&mgr->msg_mutex);
		dev_err(&mgr->pci->dev,
			"error: no response on msg %x\n", msg_frame);
		return -EIO;
	}

	/* retrieve the answer into the same struct mixart_msg */
	resp.message_id = 0;
	resp.uid = (struct mixart_uid){0,0};
	resp.data = resp_data;
	resp.size = max_resp_size;

	err = get_msg(mgr, &resp, msg_frame);

	if( request->message_id != resp.message_id )
		dev_err(&mgr->pci->dev, "RESPONSE ERROR!\n");

	mutex_unlock(&mgr->msg_mutex);
	return err;
}


int snd_mixart_send_msg_wait_notif(struct mixart_mgr *mgr,
				   struct mixart_msg *request, u32 notif_event)
{
	int err;
	wait_queue_t wait;
	long timeout;

	if (snd_BUG_ON(!notif_event))
		return -EINVAL;
	if (snd_BUG_ON((notif_event & MSG_TYPE_MASK) != MSG_TYPE_NOTIFY))
		return -EINVAL;
	if (snd_BUG_ON(notif_event & MSG_CANCEL_NOTIFY_MASK))
		return -EINVAL;

	mutex_lock(&mgr->msg_mutex);

	init_waitqueue_entry(&wait, current);

	spin_lock_irq(&mgr->msg_lock);
	/* send the message */
	err = send_msg(mgr, request, MSG_DEFAULT_SIZE, 1, &notif_event);  /* send and mark the notification event pending */
	if(err) {
		spin_unlock_irq(&mgr->msg_lock);
		mutex_unlock(&mgr->msg_mutex);
		return err;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&mgr->msg_sleep, &wait);
	spin_unlock_irq(&mgr->msg_lock);
	timeout = schedule_timeout(MSG_TIMEOUT_JIFFIES);
	remove_wait_queue(&mgr->msg_sleep, &wait);

	if (! timeout) {
		/* error - no ack */
		mutex_unlock(&mgr->msg_mutex);
		dev_err(&mgr->pci->dev,
			"error: notification %x not received\n", notif_event);
		return -EIO;
	}

	mutex_unlock(&mgr->msg_mutex);
	return 0;
}


int snd_mixart_send_msg_nonblock(struct mixart_mgr *mgr, struct mixart_msg *request)
{
	u32 message_frame;
	unsigned long flags;
	int err;

	/* just send the message (do not mark it as a pending one) */
	spin_lock_irqsave(&mgr->msg_lock, flags);
	err = send_msg(mgr, request, MSG_DEFAULT_SIZE, 0, &message_frame);
	spin_unlock_irqrestore(&mgr->msg_lock, flags);

	/* the answer will be handled by snd_struct mixart_msgasklet()  */
	atomic_inc(&mgr->msg_processed);

	return err;
}


/* common buffer of tasklet and interrupt to send/receive messages */
static u32 mixart_msg_data[MSG_DEFAULT_SIZE / 4];


void snd_mixart_msg_tasklet(unsigned long arg)
{
	struct mixart_mgr *mgr = ( struct mixart_mgr*)(arg);
	struct mixart_msg resp;
	u32 msg, addr, type;
	int err;

	spin_lock(&mgr->lock);

	while (mgr->msg_fifo_readptr != mgr->msg_fifo_writeptr) {
		msg = mgr->msg_fifo[mgr->msg_fifo_readptr];
		mgr->msg_fifo_readptr++;
		mgr->msg_fifo_readptr %= MSG_FIFO_SIZE;

		/* process the message ... */
		addr = msg & ~MSG_TYPE_MASK;
		type = msg & MSG_TYPE_MASK;

		switch (type) {
		case MSG_TYPE_ANSWER:
			/* answer to a message on that we did not wait for (send_msg_nonblock) */
			resp.message_id = 0;
			resp.data = mixart_msg_data;
			resp.size = sizeof(mixart_msg_data);
			err = get_msg(mgr, &resp, addr);
			if( err < 0 ) {
				dev_err(&mgr->pci->dev,
					"tasklet: error(%d) reading mf %x\n",
					err, msg);
				break;
			}

			switch(resp.message_id) {
			case MSG_STREAM_START_INPUT_STAGE_PACKET:
			case MSG_STREAM_START_OUTPUT_STAGE_PACKET:
			case MSG_STREAM_STOP_INPUT_STAGE_PACKET:
			case MSG_STREAM_STOP_OUTPUT_STAGE_PACKET:
				if(mixart_msg_data[0])
					dev_err(&mgr->pci->dev,
						"tasklet : error MSG_STREAM_ST***_***PUT_STAGE_PACKET status=%x\n",
						mixart_msg_data[0]);
				break;
			default:
				dev_dbg(&mgr->pci->dev,
					"tasklet received mf(%x) : msg_id(%x) uid(%x, %x) size(%zd)\n",
					   msg, resp.message_id, resp.uid.object_id, resp.uid.desc, resp.size);
				break;
			}
			break;
 		case MSG_TYPE_NOTIFY:
			/* msg contains no address ! do not get_msg() ! */
		case MSG_TYPE_COMMAND:
			/* get_msg() necessary */
		default:
			dev_err(&mgr->pci->dev,
				"tasklet doesn't know what to do with message %x\n",
				msg);
		} /* switch type */

		/* decrement counter */
		atomic_dec(&mgr->msg_processed);

	} /* while there is a msg in fifo */

	spin_unlock(&mgr->lock);
}


irqreturn_t snd_mixart_interrupt(int irq, void *dev_id)
{
	struct mixart_mgr *mgr = dev_id;
	int err;
	struct mixart_msg resp;

	u32 msg;
	u32 it_reg;

	spin_lock(&mgr->lock);

	it_reg = readl_le(MIXART_REG(mgr, MIXART_PCI_OMISR_OFFSET));
	if( !(it_reg & MIXART_OIDI) ) {
		/* this device did not cause the interrupt */
		spin_unlock(&mgr->lock);
		return IRQ_NONE;
	}

	/* mask all interrupts */
	writel_le(MIXART_HOST_ALL_INTERRUPT_MASKED, MIXART_REG(mgr, MIXART_PCI_OMIMR_OFFSET));

	/* outdoorbell register clear */
	it_reg = readl(MIXART_REG(mgr, MIXART_PCI_ODBR_OFFSET));
	writel(it_reg, MIXART_REG(mgr, MIXART_PCI_ODBR_OFFSET));

	/* clear interrupt */
	writel_le( MIXART_OIDI, MIXART_REG(mgr, MIXART_PCI_OMISR_OFFSET) );

	/* process interrupt */
	while (retrieve_msg_frame(mgr, &msg)) {

		switch (msg & MSG_TYPE_MASK) {
		case MSG_TYPE_COMMAND:
			resp.message_id = 0;
			resp.data = mixart_msg_data;
			resp.size = sizeof(mixart_msg_data);
			err = get_msg(mgr, &resp, msg & ~MSG_TYPE_MASK);
			if( err < 0 ) {
				dev_err(&mgr->pci->dev,
					"interrupt: error(%d) reading mf %x\n",
					err, msg);
				break;
			}

			if(resp.message_id == MSG_SERVICES_TIMER_NOTIFY) {
				int i;
				struct mixart_timer_notify *notify;
				notify = (struct mixart_timer_notify *)mixart_msg_data;

				for(i=0; i<notify->stream_count; i++) {

					u32 buffer_id = notify->streams[i].buffer_id;
					unsigned int chip_number =  (buffer_id & MIXART_NOTIFY_CARD_MASK) >> MIXART_NOTIFY_CARD_OFFSET; /* card0 to 3 */
					unsigned int pcm_number  =  (buffer_id & MIXART_NOTIFY_PCM_MASK ) >> MIXART_NOTIFY_PCM_OFFSET;  /* pcm0 to 3  */
					unsigned int sub_number  =   buffer_id & MIXART_NOTIFY_SUBS_MASK;             /* 0 to MIXART_PLAYBACK_STREAMS */
					unsigned int is_capture  = ((buffer_id & MIXART_NOTIFY_CAPT_MASK) != 0);      /* playback == 0 / capture == 1 */

					struct snd_mixart *chip  = mgr->chip[chip_number];
					struct mixart_stream *stream;

					if ((chip_number >= mgr->num_cards) || (pcm_number >= MIXART_PCM_TOTAL) || (sub_number >= MIXART_PLAYBACK_STREAMS)) {
						dev_err(&mgr->pci->dev,
							"error MSG_SERVICES_TIMER_NOTIFY buffer_id (%x) pos(%d)\n",
							   buffer_id, notify->streams[i].sample_pos_low_part);
						break;
					}

					if (is_capture)
						stream = &chip->capture_stream[pcm_number];
					else
						stream = &chip->playback_stream[pcm_number][sub_number];

					if (stream->substream && (stream->status == MIXART_STREAM_STATUS_RUNNING)) {
						struct snd_pcm_runtime *runtime = stream->substream->runtime;
						int elapsed = 0;
						u64 sample_count = ((u64)notify->streams[i].sample_pos_high_part) << 32;
						sample_count |= notify->streams[i].sample_pos_low_part;

						while (1) {
							u64 new_elapse_pos = stream->abs_period_elapsed +  runtime->period_size;

							if (new_elapse_pos > sample_count) {
								break; /* while */
							}
							else {
								elapsed = 1;
								stream->buf_periods++;
								if (stream->buf_periods >= runtime->periods)
									stream->buf_periods = 0;

								stream->abs_period_elapsed = new_elapse_pos;
							}
						}
						stream->buf_period_frag = (u32)( sample_count - stream->abs_period_elapsed );

						if(elapsed) {
							spin_unlock(&mgr->lock);
							snd_pcm_period_elapsed(stream->substream);
							spin_lock(&mgr->lock);
						}
					}
				}
				break;
			}
			if(resp.message_id == MSG_SERVICES_REPORT_TRACES) {
				if(resp.size > 1) {
#ifndef __BIG_ENDIAN
					/* Traces are text: the swapped msg_data has to be swapped back ! */
					int i;
					for(i=0; i<(resp.size/4); i++) {
						(mixart_msg_data)[i] = cpu_to_be32((mixart_msg_data)[i]);
					}
#endif
					((char*)mixart_msg_data)[resp.size - 1] = 0;
					dev_dbg(&mgr->pci->dev,
						"MIXART TRACE : %s\n",
						(char *)mixart_msg_data);
				}
				break;
			}

			dev_dbg(&mgr->pci->dev, "command %x not handled\n",
				resp.message_id);
			break;

		case MSG_TYPE_NOTIFY:
			if(msg & MSG_CANCEL_NOTIFY_MASK) {
				msg &= ~MSG_CANCEL_NOTIFY_MASK;
				dev_err(&mgr->pci->dev,
					"canceled notification %x !\n", msg);
			}
			/* no break, continue ! */
		case MSG_TYPE_ANSWER:
			/* answer or notification to a message we are waiting for*/
			spin_lock(&mgr->msg_lock);
			if( (msg & ~MSG_TYPE_MASK) == mgr->pending_event ) {
				wake_up(&mgr->msg_sleep);
				mgr->pending_event = 0;
			}
			/* answer to a message we did't want to wait for */
			else {
				mgr->msg_fifo[mgr->msg_fifo_writeptr] = msg;
				mgr->msg_fifo_writeptr++;
				mgr->msg_fifo_writeptr %= MSG_FIFO_SIZE;
				tasklet_schedule(&mgr->msg_taskq);
			}
			spin_unlock(&mgr->msg_lock);
			break;
		case MSG_TYPE_REQUEST:
		default:
			dev_dbg(&mgr->pci->dev,
				"interrupt received request %x\n", msg);
			/* TODO : are there things to do here ? */
			break;
		} /* switch on msg type */
	} /* while there are msgs */

	/* allow interrupt again */
	writel_le( MIXART_ALLOW_OUTBOUND_DOORBELL, MIXART_REG( mgr, MIXART_PCI_OMIMR_OFFSET));

	spin_unlock(&mgr->lock);

	return IRQ_HANDLED;
}


void snd_mixart_init_mailbox(struct mixart_mgr *mgr)
{
	writel( 0, MIXART_MEM( mgr, MSG_HOST_RSC_PROTECTION ) );
	writel( 0, MIXART_MEM( mgr, MSG_AGENT_RSC_PROTECTION ) );

	/* allow outbound messagebox to generate interrupts */
	if(mgr->irq >= 0) {
		writel_le( MIXART_ALLOW_OUTBOUND_DOORBELL, MIXART_REG( mgr, MIXART_PCI_OMIMR_OFFSET));
	}
	return;
}

void snd_mixart_exit_mailbox(struct mixart_mgr *mgr)
{
	/* no more interrupts on outbound messagebox */
	writel_le( MIXART_HOST_ALL_INTERRUPT_MASKED, MIXART_REG( mgr, MIXART_PCI_OMIMR_OFFSET));
	return;
}

void snd_mixart_reset_board(struct mixart_mgr *mgr)
{
	/* reset miXart */
	writel_be( 1, MIXART_REG(mgr, MIXART_BA1_BRUTAL_RESET_OFFSET) );
	return;
}
