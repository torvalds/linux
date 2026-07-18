/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cx20442.h  --  audio driver for CX20442
 *
 * Copyright 2009 Janusz Krzysztofik <jkrzyszt@tis.icnet.pl>
 */

#ifndef _CX20442_CODEC_H
#define _CX20442_CODEC_H

struct cx20442_codec {
	struct snd_soc_component *component;
	bool ready;
};

extern struct tty_ldisc_ops v253_ops;

#endif
