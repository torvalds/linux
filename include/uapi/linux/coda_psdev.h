/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__CODA_PSDEV_H
#define _UAPI__CODA_PSDEV_H

#include <linux/magic.h>

#define CODA_PSDEV_MAJOR 67
#define MAX_CODADEVS  5	   /* how many do we allow */

#define CODA_REQ_ASYNC  0x1
#define CODA_REQ_READ   0x2
#define CODA_REQ_WRITE  0x4
#define CODA_REQ_ABORT  0x8

#endif /* _UAPI__CODA_PSDEV_H */
