/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Sony Programmable I/O Control Device driver for VAIO
 *
 * Copyright (C) 2001-2005 Stelian Pop <stelian@popies.net>
 *
 * Copyright (C) 2005 Narayanan R S <nars@kadamba.org>

 * Copyright (C) 2001-2002 Alcôve <www.alcove.com>
 *
 * Copyright (C) 2001 Michael Ashley <m.ashley@unsw.edu.au>
 *
 * Copyright (C) 2001 Junichi Morita <jun1m@mars.dti.ne.jp>
 *
 * Copyright (C) 2000 Takaya Kinjo <t-kinjo@tc4.so-net.ne.jp>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
 */
#ifndef _SONYPI_H_
#define _SONYPI_H_

#include <uapi/linux/sonypi.h>


/* used only for communication between v4l and sonypi */

#define SONYPI_COMMAND_GETCAMERA		 1	/* obsolete */
#define SONYPI_COMMAND_SETCAMERA		 2
#define SONYPI_COMMAND_GETCAMERABRIGHTNESS	 3	/* obsolete */
#define SONYPI_COMMAND_SETCAMERABRIGHTNESS	 4
#define SONYPI_COMMAND_GETCAMERACONTRAST	 5	/* obsolete */
#define SONYPI_COMMAND_SETCAMERACONTRAST	 6
#define SONYPI_COMMAND_GETCAMERAHUE		 7	/* obsolete */
#define SONYPI_COMMAND_SETCAMERAHUE		 8
#define SONYPI_COMMAND_GETCAMERACOLOR		 9	/* obsolete */
#define SONYPI_COMMAND_SETCAMERACOLOR		10
#define SONYPI_COMMAND_GETCAMERASHARPNESS	11	/* obsolete */
#define SONYPI_COMMAND_SETCAMERASHARPNESS	12
#define SONYPI_COMMAND_GETCAMERAPICTURE		13	/* obsolete */
#define SONYPI_COMMAND_SETCAMERAPICTURE		14
#define SONYPI_COMMAND_GETCAMERAAGC		15	/* obsolete */
#define SONYPI_COMMAND_SETCAMERAAGC		16
#define SONYPI_COMMAND_GETCAMERADIRECTION	17	/* obsolete */
#define SONYPI_COMMAND_GETCAMERAROMVERSION	18	/* obsolete */
#define SONYPI_COMMAND_GETCAMERAREVISION	19	/* obsolete */

#endif				/* _SONYPI_H_ */
