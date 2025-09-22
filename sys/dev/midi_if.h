/*	$OpenBSD: midi_if.h,v 1.10 2022/03/21 19:22:40 miod Exp $	*/
/*	$NetBSD: midi_if.h,v 1.3 1998/11/25 22:17:07 augustss Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@netbsd.org).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_DEV_MIDI_IF_H_
#define _SYS_DEV_MIDI_IF_H_

struct midi_info {
	char	*name;		/* Name of MIDI hardware */
	int	props;
};
#define MIDI_PROP_OUT_INTR  1
#define MIDI_PROP_CAN_INPUT 2

struct midi_hw_if {
	int	(*open)(void *, int, 	/* open hardware */
			void (*)(void *, int), /* input callback */
			void (*)(void *), /* output callback */
			void *);
	void	(*close)(void *);	/* close hardware */
	int	(*output)(void *, int);	/* output a byte */
	void	(*flush)(void *);	/* flush the output */
	void	(*getinfo)(void *, struct midi_info *);
	int	(*ioctl)(void *, u_long, caddr_t, int, struct proc *);
};

struct device *midi_attach_mi(const struct midi_hw_if *, void *,
	    struct device *);

#endif /* _SYS_DEV_MIDI_IF_H_ */
