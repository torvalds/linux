/*	$OpenBSD: midivar.h,v 1.15 2024/10/14 00:47:36 jsg Exp $	*/

/*
 * Copyright (c) 2003, 2004 Alexandre Ratchov
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SYS_DEV_MIDIVAR_H_
#define _SYS_DEV_MIDIVAR_H_

#include <dev/midi_if.h>
#include <sys/device.h>
#include <sys/event.h>
#include <sys/timeout.h>

#define MIDI_RATE	3125	/* midi uart baud rate in bytes/second */

/*
 * simple ring buffer
 */
#define MIDIBUF_SIZE		(1 << 10)
#define MIDIBUF_MASK		(MIDIBUF_SIZE - 1)

struct midi_buffer {
	struct	      klist klist;	/* to record & wakeup poll(2) */
	int	      blocking;		/* read/write blocking */
	unsigned char data[MIDIBUF_SIZE]; 
	unsigned      start, used;
};

#define MIDIBUF_START(buf)	((buf)->start)
#define MIDIBUF_END(buf)	(((buf)->start + (buf)->used) & MIDIBUF_MASK)
#define MIDIBUF_USED(buf)	((buf)->used)
#define MIDIBUF_AVAIL(buf)	(MIDIBUF_SIZE - (buf)->used)
#define MIDIBUF_ISFULL(buf)	((buf)->used >= MIDIBUF_SIZE)
#define MIDIBUF_ISEMPTY(buf)	((buf)->used == 0)
#define MIDIBUF_WRITE(buf, byte) 				\
	do {							\
		(buf)->data[MIDIBUF_END(buf)] = (byte);		\
		(buf)->used++;					\
	} while(0)
#define MIDIBUF_READ(buf, byte)					\
	do {							\
		(byte) = (buf)->data[(buf)->start++];		\
		(buf)->start &= MIDIBUF_MASK;			\
		(buf)->used--;					\
	} while(0)
#define MIDIBUF_REMOVE(buf, count)				\
	do {							\
		(buf)->start += (count);			\
		(buf)->start &= MIDIBUF_MASK;			\
		(buf)->used  -= (count);			\
	} while(0)
#define MIDIBUF_INIT(buf) 					\
	do {							\
		(buf)->start = (buf)->used = 0;			\
	} while(0)
	

struct midi_softc {
	struct device	    dev;
	const struct midi_hw_if *hw_if;
	void		   *hw_hdl;
	int		    isbusy;		/* concerns only the output */
	int		    flags;		/* open flags */
	int		    props;		/* midi hw proprieties */
	struct timeout	    timeo;
	struct midi_buffer  inbuf;
	struct midi_buffer  outbuf;
};

#endif /* _SYS_DEV_MIDIVAR_H_ */
