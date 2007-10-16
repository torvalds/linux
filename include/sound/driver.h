#ifndef __SOUND_DRIVER_H
#define __SOUND_DRIVER_H

/*
 *  Main header file for the ALSA driver
 *  Copyright (c) 1994-2000 by Jaroslav Kysela <perex@perex.cz>
 *
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
 *
 */

#ifdef ALSA_BUILD
#include "config.h"
#endif


/* number of supported soundcards */
#ifdef CONFIG_SND_DYNAMIC_MINORS
#define SNDRV_CARDS 32
#else
#define SNDRV_CARDS 8		/* don't change - minor numbers */
#endif

#ifndef CONFIG_SND_MAJOR	/* standard configuration */
#define CONFIG_SND_MAJOR	116
#endif

#ifndef CONFIG_SND_DEBUG
#undef CONFIG_SND_DEBUG_MEMORY
#endif

#ifdef ALSA_BUILD
#include "adriver.h"
#endif

#include <linux/module.h>

#endif /* __SOUND_DRIVER_H */
