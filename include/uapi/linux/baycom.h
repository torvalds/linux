/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * The Linux BAYCOM driver for the Baycom serial 1200 baud modem
 * and the parallel 9600 baud modem
 * (C) 1997-1998 by Thomas Sailer, HB9JNX/AE4WA
 */

#ifndef _BAYCOM_H
#define _BAYCOM_H

/* -------------------------------------------------------------------- */
/*
 * structs for the IOCTL commands
 */

struct baycom_de_data {
	unsigned long de1;
	unsigned long de2;
	long de3;
};

struct baycom_ioctl {
	int cmd;
	union {
		struct baycom_de_data dbg;
	} data;
};

/* -------------------------------------------------------------------- */

/*
 * ioctl values change for baycom
 */
#define BAYCOMCTL_GETDE       0x92

/* -------------------------------------------------------------------- */

#endif /* _BAYCOM_H */

/* --------------------------------------------------------------------- */
