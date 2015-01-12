/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2005-2008 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef USBDEFS_H
#define USBDEFS_H

#define USB_INTERVALS_PER_SECOND 1000

/* device supports settings parameter via USB */
#define LINE6_CAP_CONTROL (1 << 0)
/* device supports PCM input/output via USB */
#define LINE6_CAP_PCM (1 << 1)
/* device support hardware monitoring */
#define LINE6_CAP_HWMON (1 << 2)

#define LINE6_FALLBACK_INTERVAL 10
#define LINE6_FALLBACK_MAXPACKETSIZE 16

#endif
