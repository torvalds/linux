/* Hey EMACS -*- linux-c -*-
 *
 * tipar/tiser/tiusb - low level driver for handling link cables
 * designed for Texas Instruments graphing calculators.
 *
 * Copyright (C) 2000-2002, Romain Lievin <roms@lpg.ticalc.org>
 *
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL)
 */

#ifndef _TICABLE_H 
#define _TICABLE_H 1

/* Internal default constants for the kernel module */
#define TIMAXTIME 15      /* 1.5 seconds       */
#define IO_DELAY  10      /* 10 micro-seconds  */

/* Major & minor number for character devices */
#define TIPAR_MAJOR  115 /* 0 to 7 */
#define TIPAR_MINOR    0

#define TISER_MAJOR  115 /* 8 to 15 */
#define TISER_MINOR    8

#define TIUSB_MAJOR  115  /* 16 to 31 */
#define TIUSB_MINOR   16

/*
 * Request values for the 'ioctl' function.
 */
#define IOCTL_TIPAR_DELAY     _IOW('p', 0xa8, int) /* set delay   */
#define IOCTL_TIPAR_TIMEOUT   _IOW('p', 0xa9, int) /* set timeout */

#define IOCTL_TISER_DELAY     _IOW('p', 0xa0, int) /* set delay   */
#define IOCTL_TISER_TIMEOUT   _IOW('p', 0xa1, int) /* set timeout */

#define IOCTL_TIUSB_TIMEOUT        _IOW('N', 0x20, int) /* set timeout */
#define IOCTL_TIUSB_RESET_DEVICE   _IOW('N', 0x21, int) /* reset device */
#define IOCTL_TIUSB_RESET_PIPES    _IOW('N', 0x22, int) /* reset both pipes*/
#define IOCTL_TIUSB_GET_MAXPS      _IOR('N', 0x23, int) /* max packet size */
#define IOCTL_TIUSB_GET_DEVID      _IOR('N', 0x24, int) /* get device type */

#endif /* TICABLE_H */
