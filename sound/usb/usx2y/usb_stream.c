/*
 * Copyright (C) 2007, 2008 Karsten Wiese <fzu@wemgehoertderstaat.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/usb.h>
#include <linux/gfp.h>

#include "usb_stream.h"


/*                             setup                                  */

static unsigned usb_stream_next_packet_size(struct usb_stream_kernel *sk)
{
	struct usb_stream *s = sk->s;
	sk->out_phase_peeked = (sk->out_phase & 0xffff) + sk->freqn;
	return (sk->out_phase_peeked >> 16) * s->cfg.frame_size;
}

static void playback_prep_freqn(struct usb_stream_kernel *sk, struct urb *urb)
{
	struct usb_stream *s = sk->s;
	int pack, lb = 0;

	for (pack = 0; pack < sk->n_o_ps; pack++) {
		int l = usb_stream_next_packet_size(sk);
		if (s->idle_outsize + lb + l > s->period_size)
			goto check;

		sk->out_phase = sk->out_phase_peeked;
		urb->iso_frame_desc[pack].offset = lb;
		urb->iso_frame_desc[pack].length = l;
		lb += l;
	}
	snd_printdd(KERN_DEBUG "%i\n", lb);

check:
	urb->number_of_packets = pack;
	urb->transfer_buffer_length = lb;
	s->idle_outsize += lb - s->period_size;
	snd_printdd(KERN_DEBUG "idle=%i ul=%i ps=%i\n", s->idle_outsize,
		    lb, s->period_size);
}

static int init_pipe_urbs(struct usb_stream_kernel *sk, unsigned use_packsize,
			   struct urb **urbs, char *transfer,
			   struct usb_device *dev, int pipe)
{
	int u, p;
	int maxpacket = use_packsize ?
		use_packsize : usb_maxpacket(dev, pipe, usb_pipeout(pipe));
	int transfer_length = maxpacket * sk->n_o_ps;

	for (u = 0; u < USB_STREAM_NURBS;
	     ++u, transfer += transfer_length) {
		struct urb *urb = urbs[u];
		struct usb_iso_packet_descriptor *desc;
		urb->transfer_buffer = transfer;
		urb->dev = dev;
		urb->pipe = pipe;
		urb->number_of_packets = sk->n_o_ps;
		urb->context = sk;
		urb->interval = 1;
		if (usb_pipeout(pipe))
			continue;
		if (usb_urb_ep_type_check(urb))
			return -EINVAL;

		urb->transfer_buffer_length = transfer_length;
		desc = urb->iso_frame_desc;
		desc->offset = 0;
		desc->length = maxpacket;
		for (p = 1; p < sk->n_o_ps; ++p) {
			desc[p].offset = desc[p - 1].offset + maxpacket;
			desc[p].length = maxpacket;
		}
	}

	return 0;
}

static int init_urbs(struct usb_stream_kernel *sk, unsigned use_packsize,
		      struct usb_device *dev, int in_pipe, int out_pipe)
{
	struct usb_stream	*s = sk->s;
	char			*indata = (char *)s + sizeof(*s) +
					sizeof(struct usb_stream_packet) *
					s->inpackets;
	int			u;

	for (u = 0; u < USB_STREAM_NURBS; ++u) {
		sk->inurb[u] = usb_alloc_urb(sk->n_o_ps, GFP_KERNEL);
		if (!sk->inurb[u])
			return -ENOMEM;

		sk->outurb[u] = usb_alloc_urb(sk->n_o_ps, GFP_KERNEL);
		if (!sk->outurb[u])
			return -ENOMEM;
	}

	if (init_pipe_urbs(sk, use_packsize, sk->inurb, indata, dev, in_pipe) ||
	    init_pipe_urbs(sk, use_packsize, sk->outurb, sk->write_page, dev,
			   out_pipe))
		return -EINVAL;

	return 0;
}


/*
 * convert a sampling rate into our full speed format (fs/1000 in Q16.16)
 * this will overflow at approx 524 kHz
 */
static inline unsigned get_usb_full_speed_rate(unsigned rate)
{
	return ((rate << 13) + 62) / 125;
}

/*
 * convert a sampling rate into USB high speed format (fs/8000 in Q16.16)
 * this will overflow at approx 4 MHz
 */
static inline unsigned get_usb_high_speed_rate(unsigned rate)
{
	return ((rate << 10) + 62) / 125;
}

void usb_stream_free(struct usb_stream_kernel *sk)
{
	struct usb_stream *s;
	unsigned u;

	for (u = 0; u < USB_STREAM_NURBS; ++u) {
		usb_free_urb(sk->inurb[u]);
		sk->inurb[u] = NULL;
		usb_free_urb(sk->outurb[u]);
		sk->outurb[u] = NULL;
	}

	s = sk->s;
	if (!s)
		return;

	free_pages((unsigned long)sk->write_page, get_order(s->write_size));
	sk->write_page = NULL;
	free_pages((unsigned long)s, get_order(s->read_size));
	sk->s = NULL;
}

struct usb_stream *usb_stream_new(struct usb_stream_kernel *sk,
				  struct usb_device *dev,
				  unsigned in_endpoint, unsigned out_endpoint,
				  unsigned sample_rate, unsigned use_packsize,
				  unsigned period_frames, unsigned frame_size)
{
	int packets, max_packsize;
	int in_pipe, out_pipe;
	int read_size = sizeof(struct usb_stream);
	int write_size;
	int usb_frames = dev->speed == USB_SPEED_HIGH ? 8000 : 1000;
	int pg;

	in_pipe = usb_rcvisocpipe(dev, in_endpoint);
	out_pipe = usb_sndisocpipe(dev, out_endpoint);

	max_packsize = use_packsize ?
		use_packsize : usb_maxpacket(dev, in_pipe, 0);

	/*
		t_period = period_frames / sample_rate
		iso_packs = t_period / t_iso_frame
			= (period_frames / sample_rate) * (1 / t_iso_frame)
	*/

	packets = period_frames * usb_frames / sample_rate + 1;

	if (dev->speed == USB_SPEED_HIGH)
		packets = (packets + 7) & ~7;

	read_size += packets * USB_STREAM_URBDEPTH *
		(max_packsize + sizeof(struct usb_stream_packet));

	max_packsize = usb_maxpacket(dev, out_pipe, 1);
	write_size = max_packsize * packets * USB_STREAM_URBDEPTH;

	if (read_size >= 256*PAGE_SIZE || write_size >= 256*PAGE_SIZE) {
		snd_printk(KERN_WARNING "a size exceeds 128*PAGE_SIZE\n");
		goto out;
	}

	pg = get_order(read_size);
	sk->s = (void *) __get_free_pages(GFP_KERNEL|__GFP_COMP|__GFP_ZERO|
					  __GFP_NOWARN, pg);
	if (!sk->s) {
		snd_printk(KERN_WARNING "couldn't __get_free_pages()\n");
		goto out;
	}
	sk->s->cfg.version = USB_STREAM_INTERFACE_VERSION;

	sk->s->read_size = read_size;

	sk->s->cfg.sample_rate = sample_rate;
	sk->s->cfg.frame_size = frame_size;
	sk->n_o_ps = packets;
	sk->s->inpackets = packets * USB_STREAM_URBDEPTH;
	sk->s->cfg.period_frames = period_frames;
	sk->s->period_size = frame_size * period_frames;

	sk->s->write_size = write_size;
	pg = get_order(write_size);

	sk->write_page =
		(void *)__get_free_pages(GFP_KERNEL|__GFP_COMP|__GFP_ZERO|
					 __GFP_NOWARN, pg);
	if (!sk->write_page) {
		snd_printk(KERN_WARNING "couldn't __get_free_pages()\n");
		usb_stream_free(sk);
		return NULL;
	}

	/* calculate the frequency in 16.16 format */
	if (dev->speed == USB_SPEED_FULL)
		sk->freqn = get_usb_full_speed_rate(sample_rate);
	else
		sk->freqn = get_usb_high_speed_rate(sample_rate);

	if (init_urbs(sk, use_packsize, dev, in_pipe, out_pipe) < 0) {
		usb_stream_free(sk);
		return NULL;
	}

	sk->s->state = usb_stream_stopped;
out:
	return sk->s;
}


/*                             start                                  */

static bool balance_check(struct usb_stream_kernel *sk, struct urb *urb)
{
	bool r;
	if (unlikely(urb->status)) {
		if (urb->status != -ESHUTDOWN && urb->status != -ENOENT)
			snd_printk(KERN_WARNING "status=%i\n", urb->status);
		sk->iso_frame_balance = 0x7FFFFFFF;
		return false;
	}
	r = sk->iso_frame_balance == 0;
	if (!r)
		sk->i_urb = urb;
	return r;
}

static bool balance_playback(struct usb_stream_kernel *sk, struct urb *urb)
{
	sk->iso_frame_balance += urb->number_of_packets;
	return balance_check(sk, urb);
}

static bool balance_capture(struct usb_stream_kernel *sk, struct urb *urb)
{
	sk->iso_frame_balance -= urb->number_of_packets;
	return balance_check(sk, urb);
}

static void subs_set_complete(struct urb **urbs, void (*complete)(struct urb *))
{
	int u;

	for (u = 0; u < USB_STREAM_NURBS; u++) {
		struct urb *urb = urbs[u];
		urb->complete = complete;
	}
}

static int usb_stream_prepare_playback(struct usb_stream_kernel *sk,
		struct urb *inurb)
{
	struct usb_stream *s = sk->s;
	struct urb *io;
	struct usb_iso_packet_descriptor *id, *od;
	int p = 0, lb = 0, l = 0;

	io = sk->idle_outurb;
	od = io->iso_frame_desc;

	for (; s->sync_packet < 0; ++p, ++s->sync_packet) {
		struct urb *ii = sk->completed_inurb;
		id = ii->iso_frame_desc +
			ii->number_of_packets + s->sync_packet;
		l = id->actual_length;

		od[p].length = l;
		od[p].offset = lb;
		lb += l;
	}

	for (;
	     s->sync_packet < inurb->number_of_packets && p < sk->n_o_ps;
	     ++p, ++s->sync_packet) {
		l = inurb->iso_frame_desc[s->sync_packet].actual_length;

		if (s->idle_outsize + lb + l > s->period_size)
			goto check_ok;

		od[p].length = l;
		od[p].offset = lb;
		lb += l;
	}

check_ok:
	s->sync_packet -= inurb->number_of_packets;
	if (unlikely(s->sync_packet < -2 || s->sync_packet > 0)) {
		snd_printk(KERN_WARNING "invalid sync_packet = %i;"
			   " p=%i nop=%i %i %x %x %x > %x\n",
			   s->sync_packet, p, inurb->number_of_packets,
			   s->idle_outsize + lb + l,
			   s->idle_outsize, lb,  l,
			   s->period_size);
		return -1;
	}
	if (unlikely(lb % s->cfg.frame_size)) {
		snd_printk(KERN_WARNING"invalid outsize = %i\n",
			   lb);
		return -1;
	}
	s->idle_outsize += lb - s->period_size;
	io->number_of_packets = p;
	io->transfer_buffer_length = lb;
	if (s->idle_outsize <= 0)
		return 0;

	snd_printk(KERN_WARNING "idle=%i\n", s->idle_outsize);
	return -1;
}

static void prepare_inurb(int number_of_packets, struct urb *iu)
{
	struct usb_iso_packet_descriptor *id;
	int p;

	iu->number_of_packets = number_of_packets;
	id = iu->iso_frame_desc;
	id->offset = 0;
	for (p = 0; p < iu->number_of_packets - 1; ++p)
		id[p + 1].offset = id[p].offset + id[p].length;

	iu->transfer_buffer_length =
		id[0].length * iu->number_of_packets;
}

static int submit_urbs(struct usb_stream_kernel *sk,
		       struct urb *inurb, struct urb *outurb)
{
	int err;
	prepare_inurb(sk->idle_outurb->number_of_packets, sk->idle_inurb);
	err = usb_submit_urb(sk->idle_inurb, GFP_ATOMIC);
	if (err < 0)
		goto report_failure;

	sk->idle_inurb = sk->completed_inurb;
	sk->completed_inurb = inurb;
	err = usb_submit_urb(sk->idle_outurb, GFP_ATOMIC);
	if (err < 0)
		goto report_failure;

	sk->idle_outurb = sk->completed_outurb;
	sk->completed_outurb = outurb;
	return 0;

report_failure:
	snd_printk(KERN_ERR "%i\n", err);
	return err;
}

#ifdef DEBUG_LOOP_BACK
/*
  This loop_back() shows how to read/write the period data.
 */
static void loop_back(struct usb_stream *s)
{
	char *i, *o;
	int il, ol, l, p;
	struct urb *iu;
	struct usb_iso_packet_descriptor *id;

	o = s->playback1st_to;
	ol = s->playback1st_size;
	l = 0;

	if (s->insplit_pack >= 0) {
		iu = sk->idle_inurb;
		id = iu->iso_frame_desc;
		p = s->insplit_pack;
	} else
		goto second;
loop:
	for (; p < iu->number_of_packets && l < s->period_size; ++p) {
		i = iu->transfer_buffer + id[p].offset;
		il = id[p].actual_length;
		if (l + il > s->period_size)
			il = s->period_size - l;
		if (il <= ol) {
			memcpy(o, i, il);
			o += il;
			ol -= il;
		} else {
			memcpy(o, i, ol);
			singen_6pack(o, ol);
			o = s->playback_to;
			memcpy(o, i + ol, il - ol);
			o += il - ol;
			ol = s->period_size - s->playback1st_size;
		}
		l += il;
	}
	if (iu == sk->completed_inurb) {
		if (l != s->period_size)
			printk(KERN_DEBUG"%s:%i %i\n", __func__, __LINE__,
			       l/(int)s->cfg.frame_size);

		return;
	}
second:
	iu = sk->completed_inurb;
	id = iu->iso_frame_desc;
	p = 0;
	goto loop;

}
#else
static void loop_back(struct usb_stream *s)
{
}
#endif

static void stream_idle(struct usb_stream_kernel *sk,
			struct urb *inurb, struct urb *outurb)
{
	struct usb_stream *s = sk->s;
	int l, p;
	int insize = s->idle_insize;
	int urb_size = 0;

	s->inpacket_split = s->next_inpacket_split;
	s->inpacket_split_at = s->next_inpacket_split_at;
	s->next_inpacket_split = -1;
	s->next_inpacket_split_at = 0;

	for (p = 0; p < inurb->number_of_packets; ++p) {
		struct usb_iso_packet_descriptor *id = inurb->iso_frame_desc;
		l = id[p].actual_length;
		if (unlikely(l == 0 || id[p].status)) {
			snd_printk(KERN_WARNING "underrun, status=%u\n",
				   id[p].status);
			goto err_out;
		}
		s->inpacket_head++;
		s->inpacket_head %= s->inpackets;
		if (s->inpacket_split == -1)
			s->inpacket_split = s->inpacket_head;

		s->inpacket[s->inpacket_head].offset =
			id[p].offset + (inurb->transfer_buffer - (void *)s);
		s->inpacket[s->inpacket_head].length = l;
		if (insize + l > s->period_size &&
		    s->next_inpacket_split == -1) {
			s->next_inpacket_split = s->inpacket_head;
			s->next_inpacket_split_at = s->period_size - insize;
		}
		insize += l;
		urb_size += l;
	}
	s->idle_insize += urb_size - s->period_size;
	if (s->idle_insize < 0) {
		snd_printk(KERN_WARNING "%i\n",
			   (s->idle_insize)/(int)s->cfg.frame_size);
		goto err_out;
	}
	s->insize_done += urb_size;

	l = s->idle_outsize;
	s->outpacket[0].offset = (sk->idle_outurb->transfer_buffer -
				  sk->write_page) - l;

	if (usb_stream_prepare_playback(sk, inurb) < 0)
		goto err_out;

	s->outpacket[0].length = sk->idle_outurb->transfer_buffer_length + l;
	s->outpacket[1].offset = sk->completed_outurb->transfer_buffer -
		sk->write_page;

	if (submit_urbs(sk, inurb, outurb) < 0)
		goto err_out;

	loop_back(s);
	s->periods_done++;
	wake_up_all(&sk->sleep);
	return;
err_out:
	s->state = usb_stream_xrun;
	wake_up_all(&sk->sleep);
}

static void i_capture_idle(struct urb *urb)
{
	struct usb_stream_kernel *sk = urb->context;
	if (balance_capture(sk, urb))
		stream_idle(sk, urb, sk->i_urb);
}

static void i_playback_idle(struct urb *urb)
{
	struct usb_stream_kernel *sk = urb->context;
	if (balance_playback(sk, urb))
		stream_idle(sk, sk->i_urb, urb);
}

static void stream_start(struct usb_stream_kernel *sk,
			 struct urb *inurb, struct urb *outurb)
{
	struct usb_stream *s = sk->s;
	if (s->state >= usb_stream_sync1) {
		int l, p, max_diff, max_diff_0;
		int urb_size = 0;
		unsigned frames_per_packet, min_frames = 0;
		frames_per_packet = (s->period_size - s->idle_insize);
		frames_per_packet <<= 8;
		frames_per_packet /=
			s->cfg.frame_size * inurb->number_of_packets;
		frames_per_packet++;

		max_diff_0 = s->cfg.frame_size;
		if (s->cfg.period_frames >= 256)
			max_diff_0 <<= 1;
		if (s->cfg.period_frames >= 1024)
			max_diff_0 <<= 1;
		max_diff = max_diff_0;
		for (p = 0; p < inurb->number_of_packets; ++p) {
			int diff;
			l = inurb->iso_frame_desc[p].actual_length;
			urb_size += l;

			min_frames += frames_per_packet;
			diff = urb_size -
				(min_frames >> 8) * s->cfg.frame_size;
			if (diff < max_diff) {
				snd_printdd(KERN_DEBUG "%i %i %i %i\n",
					    s->insize_done,
					    urb_size / (int)s->cfg.frame_size,
					    inurb->number_of_packets, diff);
				max_diff = diff;
			}
		}
		s->idle_insize -= max_diff - max_diff_0;
		s->idle_insize += urb_size - s->period_size;
		if (s->idle_insize < 0) {
			snd_printk(KERN_WARNING "%i %i %i\n",
				   s->idle_insize, urb_size, s->period_size);
			return;
		} else if (s->idle_insize == 0) {
			s->next_inpacket_split =
				(s->inpacket_head + 1) % s->inpackets;
			s->next_inpacket_split_at = 0;
		} else {
			unsigned split = s->inpacket_head;
			l = s->idle_insize;
			while (l > s->inpacket[split].length) {
				l -= s->inpacket[split].length;
				if (split == 0)
					split = s->inpackets - 1;
				else
					split--;
			}
			s->next_inpacket_split = split;
			s->next_inpacket_split_at =
				s->inpacket[split].length - l;
		}

		s->insize_done += urb_size;

		if (usb_stream_prepare_playback(sk, inurb) < 0)
			return;

	} else
		playback_prep_freqn(sk, sk->idle_outurb);

	if (submit_urbs(sk, inurb, outurb) < 0)
		return;

	if (s->state == usb_stream_sync1 && s->insize_done > 360000) {
		/* just guesswork                            ^^^^^^ */
		s->state = usb_stream_ready;
		subs_set_complete(sk->inurb, i_capture_idle);
		subs_set_complete(sk->outurb, i_playback_idle);
	}
}

static void i_capture_start(struct urb *urb)
{
	struct usb_iso_packet_descriptor *id = urb->iso_frame_desc;
	struct usb_stream_kernel *sk = urb->context;
	struct usb_stream *s = sk->s;
	int p;
	int empty = 0;

	if (urb->status) {
		snd_printk(KERN_WARNING "status=%i\n", urb->status);
		return;
	}

	for (p = 0; p < urb->number_of_packets; ++p) {
		int l = id[p].actual_length;
		if (l < s->cfg.frame_size) {
			++empty;
			if (s->state >= usb_stream_sync0) {
				snd_printk(KERN_WARNING "%i\n", l);
				return;
			}
		}
		s->inpacket_head++;
		s->inpacket_head %= s->inpackets;
		s->inpacket[s->inpacket_head].offset =
			id[p].offset + (urb->transfer_buffer - (void *)s);
		s->inpacket[s->inpacket_head].length = l;
	}
#ifdef SHOW_EMPTY
	if (empty) {
		printk(KERN_DEBUG"%s:%i: %i", __func__, __LINE__,
		       urb->iso_frame_desc[0].actual_length);
		for (pack = 1; pack < urb->number_of_packets; ++pack) {
			int l = urb->iso_frame_desc[pack].actual_length;
			printk(KERN_CONT " %i", l);
		}
		printk(KERN_CONT "\n");
	}
#endif
	if (!empty && s->state < usb_stream_sync1)
		++s->state;

	if (balance_capture(sk, urb))
		stream_start(sk, urb, sk->i_urb);
}

static void i_playback_start(struct urb *urb)
{
	struct usb_stream_kernel *sk = urb->context;
	if (balance_playback(sk, urb))
		stream_start(sk, sk->i_urb, urb);
}

int usb_stream_start(struct usb_stream_kernel *sk)
{
	struct usb_stream *s = sk->s;
	int frame = 0, iters = 0;
	int u, err;
	int try = 0;

	if (s->state != usb_stream_stopped)
		return -EAGAIN;

	subs_set_complete(sk->inurb, i_capture_start);
	subs_set_complete(sk->outurb, i_playback_start);
	memset(sk->write_page, 0, s->write_size);
dotry:
	s->insize_done = 0;
	s->idle_insize = 0;
	s->idle_outsize = 0;
	s->sync_packet = -1;
	s->inpacket_head = -1;
	sk->iso_frame_balance = 0;
	++try;
	for (u = 0; u < 2; u++) {
		struct urb *inurb = sk->inurb[u];
		struct urb *outurb = sk->outurb[u];
		playback_prep_freqn(sk, outurb);
		inurb->number_of_packets = outurb->number_of_packets;
		inurb->transfer_buffer_length =
			inurb->number_of_packets *
			inurb->iso_frame_desc[0].length;

		if (u == 0) {
			int now;
			struct usb_device *dev = inurb->dev;
			frame = usb_get_current_frame_number(dev);
			do {
				now = usb_get_current_frame_number(dev);
				++iters;
			} while (now > -1 && now == frame);
		}
		err = usb_submit_urb(inurb, GFP_ATOMIC);
		if (err < 0) {
			snd_printk(KERN_ERR"usb_submit_urb(sk->inurb[%i])"
				   " returned %i\n", u, err);
			return err;
		}
		err = usb_submit_urb(outurb, GFP_ATOMIC);
		if (err < 0) {
			snd_printk(KERN_ERR"usb_submit_urb(sk->outurb[%i])"
				   " returned %i\n", u, err);
			return err;
		}

		if (inurb->start_frame != outurb->start_frame) {
			snd_printd(KERN_DEBUG
				   "u[%i] start_frames differ in:%u out:%u\n",
				   u, inurb->start_frame, outurb->start_frame);
			goto check_retry;
		}
	}
	snd_printdd(KERN_DEBUG "%i %i\n", frame, iters);
	try = 0;
check_retry:
	if (try) {
		usb_stream_stop(sk);
		if (try < 5) {
			msleep(1500);
			snd_printd(KERN_DEBUG "goto dotry;\n");
			goto dotry;
		}
		snd_printk(KERN_WARNING"couldn't start"
			   " all urbs on the same start_frame.\n");
		return -EFAULT;
	}

	sk->idle_inurb = sk->inurb[USB_STREAM_NURBS - 2];
	sk->idle_outurb = sk->outurb[USB_STREAM_NURBS - 2];
	sk->completed_inurb = sk->inurb[USB_STREAM_NURBS - 1];
	sk->completed_outurb = sk->outurb[USB_STREAM_NURBS - 1];

/* wait, check */
	{
		int wait_ms = 3000;
		while (s->state != usb_stream_ready && wait_ms > 0) {
			snd_printdd(KERN_DEBUG "%i\n", s->state);
			msleep(200);
			wait_ms -= 200;
		}
	}

	return s->state == usb_stream_ready ? 0 : -EFAULT;
}


/*                             stop                                   */

void usb_stream_stop(struct usb_stream_kernel *sk)
{
	int u;
	if (!sk->s)
		return;
	for (u = 0; u < USB_STREAM_NURBS; ++u) {
		usb_kill_urb(sk->inurb[u]);
		usb_kill_urb(sk->outurb[u]);
	}
	sk->s->state = usb_stream_stopped;
	msleep(400);
}
