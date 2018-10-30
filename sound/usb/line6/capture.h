/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef CAPTURE_H
#define CAPTURE_H

#include <sound/pcm.h>

#include "driver.h"
#include "pcm.h"

extern const struct snd_pcm_ops snd_line6_capture_ops;

extern void line6_capture_copy(struct snd_line6_pcm *line6pcm, char *fbuf,
			       int fsize);
extern void line6_capture_check_period(struct snd_line6_pcm *line6pcm,
				       int length);
extern int line6_create_audio_in_urbs(struct snd_line6_pcm *line6pcm);
extern int line6_submit_audio_in_all_urbs(struct snd_line6_pcm *line6pcm);

#endif
