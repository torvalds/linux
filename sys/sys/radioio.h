/* $OpenBSD: radioio.h,v 1.5 2015/01/14 21:15:36 deraadt Exp $ */
/* $RuOBSD: radioio.h,v 1.4 2001/10/18 16:51:36 pva Exp $ */

/*
 * Copyright (c) 2001 Maxim Tsyplakov <tm@oganer.net>,
 *                    Vladimir Popov <jumbo@narod.ru>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_RADIOIO_H_
#define _SYS_RADIOIO_H_

#include <sys/types.h>

#define MIN_FM_FREQ	87500
#define MAX_FM_FREQ	108000

#define MIN_TV_CHAN	0
#define MAX_TV_CHAN	150

#define IF_FREQ	10700

struct radio_info {
	int	mute;
	int	volume;
	int	stereo;
	int	rfreq;	/* reference frequency */
	int	lock;	/* locking field strength during an automatic search */
	u_int32_t	freq;	/* in kHz */
	u_int32_t	caps;	/* card capabilities */
#define RADIO_CAPS_DETECT_STEREO	(1<<0)
#define RADIO_CAPS_DETECT_SIGNAL	(1<<1)
#define RADIO_CAPS_SET_MONO		(1<<2)
#define RADIO_CAPS_HW_SEARCH		(1<<3)
#define RADIO_CAPS_HW_AFC		(1<<4)
#define RADIO_CAPS_REFERENCE_FREQ	(1<<5)
#define RADIO_CAPS_LOCK_SENSITIVITY	(1<<6)
#define RADIO_CAPS_RESERVED1		(1<<7)
#define RADIO_CAPS_RESERVED2		(0xFF<<8)
#define RADIO_CARD_TYPE			(0xFF<<16)
	u_int32_t	info;
#define RADIO_INFO_STEREO		(1<<0)
#define RADIO_INFO_SIGNAL		(1<<1)
	u_int32_t	tuner_mode;
#define RADIO_TUNER_MODE_RADIO         (1<<0)
#define RADIO_TUNER_MODE_TV            (1<<1)
	u_int32_t	chan;
	u_int32_t	chnlset;
};

/* Radio device operations */
#define RIOCGINFO	_IOR('R', 21, struct radio_info) /* get info */
#define RIOCSINFO	_IOWR('R', 22, struct radio_info) /* set info */
#define RIOCSSRCH	_IOW('R', 23, int) /* search up/down */

#endif /* _SYS_RADIOIO_H_ */
