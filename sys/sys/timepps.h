/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Julien Ridoux at the University
 * of Melbourne under sponsorship from the FreeBSD Foundation.
 *
 * $FreeBSD$
 *
 * The is a FreeBSD version of the RFC 2783 API for Pulse Per Second 
 * timing interfaces.  
 */

#ifndef _SYS_TIMEPPS_H_
#define _SYS_TIMEPPS_H_

#include <sys/_ffcounter.h>
#include <sys/ioccom.h>
#include <sys/time.h>

#define PPS_API_VERS_1	1

typedef int pps_handle_t;	

typedef unsigned pps_seq_t;

typedef struct ntp_fp {
	unsigned int	integral;
	unsigned int	fractional;
} ntp_fp_t;

typedef union pps_timeu {
	struct timespec	tspec;
	ntp_fp_t	ntpfp;
	unsigned long	longpad[3];
} pps_timeu_t;

typedef struct {
	pps_seq_t	assert_sequence;	/* assert event seq # */
	pps_seq_t	clear_sequence;		/* clear event seq # */
	pps_timeu_t	assert_tu;
	pps_timeu_t	clear_tu;
	int		current_mode;		/* current mode bits */
} pps_info_t;

typedef struct {
	pps_seq_t	assert_sequence;	/* assert event seq # */
	pps_seq_t	clear_sequence;		/* clear event seq # */
	pps_timeu_t	assert_tu;
	pps_timeu_t	clear_tu;
	ffcounter	assert_ffcount;		/* ffcounter on assert event */
	ffcounter	clear_ffcount;		/* ffcounter on clear event */
	int		current_mode;		/* current mode bits */
} pps_info_ffc_t;

#define assert_timestamp        assert_tu.tspec
#define clear_timestamp         clear_tu.tspec

#define assert_timestamp_ntpfp  assert_tu.ntpfp
#define clear_timestamp_ntpfp   clear_tu.ntpfp

typedef struct {
	int api_version;			/* API version # */
	int mode;				/* mode bits */
	pps_timeu_t assert_off_tu;
	pps_timeu_t clear_off_tu;
} pps_params_t;

#define assert_offset   assert_off_tu.tspec
#define clear_offset    clear_off_tu.tspec

#define assert_offset_ntpfp     assert_off_tu.ntpfp
#define clear_offset_ntpfp      clear_off_tu.ntpfp


#define PPS_CAPTUREASSERT	0x01
#define PPS_CAPTURECLEAR	0x02
#define PPS_CAPTUREBOTH		0x03

#define PPS_OFFSETASSERT	0x10
#define PPS_OFFSETCLEAR		0x20

#define PPS_ECHOASSERT		0x40
#define PPS_ECHOCLEAR		0x80

#define PPS_CANWAIT		0x100
#define PPS_CANPOLL		0x200

#define PPS_TSFMT_TSPEC		0x1000
#define PPS_TSFMT_NTPFP		0x2000

#define	PPS_TSCLK_FBCK		0x10000
#define	PPS_TSCLK_FFWD		0x20000
#define	PPS_TSCLK_MASK		0x30000

#define PPS_KC_HARDPPS		0
#define PPS_KC_HARDPPS_PLL	1
#define PPS_KC_HARDPPS_FLL	2

struct pps_fetch_args {
	int tsformat;
	pps_info_t	pps_info_buf;
	struct timespec	timeout;
};

struct pps_fetch_ffc_args {
	int		tsformat;
	pps_info_ffc_t	pps_info_buf_ffc;
	struct timespec	timeout;
};

struct pps_kcbind_args {
	int kernel_consumer;
	int edge;
	int tsformat;
};

#define PPS_IOC_CREATE		_IO('1', 1)
#define PPS_IOC_DESTROY		_IO('1', 2)
#define PPS_IOC_SETPARAMS	_IOW('1', 3, pps_params_t)
#define PPS_IOC_GETPARAMS	_IOR('1', 4, pps_params_t)
#define PPS_IOC_GETCAP		_IOR('1', 5, int)
#define PPS_IOC_FETCH		_IOWR('1', 6, struct pps_fetch_args)
#define PPS_IOC_KCBIND		_IOW('1', 7, struct pps_kcbind_args)
#define	PPS_IOC_FETCH_FFCOUNTER	_IOWR('1', 8, struct pps_fetch_ffc_args)

#ifdef _KERNEL

struct mtx;

#define	KCMODE_EDGEMASK		0x03
#define	KCMODE_ABIFLAG		0x80000000 /* Internal use: abi-aware driver. */

#define	PPS_ABI_VERSION		1

#define	PPSFLAG_MTX_SPIN	0x01	/* Driver mtx is MTX_SPIN type. */

struct pps_state {
	/* Capture information. */
	struct timehands *capth;
	struct fftimehands *capffth;
	unsigned	capgen;
	unsigned	capcount;

	/* State information. */
	pps_params_t	ppsparam;
	pps_info_t	ppsinfo;
	pps_info_ffc_t	ppsinfo_ffc;
	int		kcmode;
	int		ppscap;
	struct timecounter *ppstc;
	unsigned	ppscount[3];
	/*
	 * The following fields are valid if the driver calls pps_init_abi().
	 */
	uint16_t	driver_abi;	/* Driver sets before pps_init_abi(). */
	uint16_t	kernel_abi;	/* Kernel sets during pps_init_abi(). */
	struct mtx	*driver_mtx;	/* Optional, valid if non-NULL. */
	uint32_t	flags;
};

void pps_capture(struct pps_state *pps);
void pps_event(struct pps_state *pps, int event);
void pps_init(struct pps_state *pps);
void pps_init_abi(struct pps_state *pps);
int pps_ioctl(unsigned long cmd, caddr_t data, struct pps_state *pps);
void hardpps(struct timespec *tsp, long nsec);

#else /* !_KERNEL */

static __inline int
time_pps_create(int filedes, pps_handle_t *handle)
{
	int error;

	*handle = -1;
	error = ioctl(filedes, PPS_IOC_CREATE, 0);
	if (error < 0) 
		return (-1);
	*handle = filedes;
	return (0);
}

static __inline int
time_pps_destroy(pps_handle_t handle)
{
	return (ioctl(handle, PPS_IOC_DESTROY, 0));
}

static __inline int
time_pps_setparams(pps_handle_t handle, const pps_params_t *ppsparams)
{
	return (ioctl(handle, PPS_IOC_SETPARAMS, ppsparams));
}

static __inline int
time_pps_getparams(pps_handle_t handle, pps_params_t *ppsparams)
{
	return (ioctl(handle, PPS_IOC_GETPARAMS, ppsparams));
}

static __inline int 
time_pps_getcap(pps_handle_t handle, int *mode)
{
	return (ioctl(handle, PPS_IOC_GETCAP, mode));
}

static __inline int
time_pps_fetch(pps_handle_t handle, const int tsformat,
	pps_info_t *ppsinfobuf, const struct timespec *timeout)
{
	int error;
	struct pps_fetch_args arg;

	arg.tsformat = tsformat;
	if (timeout == NULL) {
		arg.timeout.tv_sec = -1;
		arg.timeout.tv_nsec = -1;
	} else
		arg.timeout = *timeout;
	error = ioctl(handle, PPS_IOC_FETCH, &arg);
	*ppsinfobuf = arg.pps_info_buf;
	return (error);
}

static __inline int
time_pps_fetch_ffc(pps_handle_t handle, const int tsformat,
	pps_info_ffc_t *ppsinfobuf, const struct timespec *timeout)
{
	struct pps_fetch_ffc_args arg;
	int error;

	arg.tsformat = tsformat;
	if (timeout == NULL) {
		arg.timeout.tv_sec = -1;
		arg.timeout.tv_nsec = -1;
	} else {
		arg.timeout = *timeout;
	}
	error = ioctl(handle, PPS_IOC_FETCH_FFCOUNTER, &arg);
	*ppsinfobuf = arg.pps_info_buf_ffc;
	return (error);
}

static __inline int
time_pps_kcbind(pps_handle_t handle, const int kernel_consumer,
	const int edge, const int tsformat)
{
	struct pps_kcbind_args arg;

	arg.kernel_consumer = kernel_consumer;
	arg.edge = edge;
	arg.tsformat = tsformat;
	return (ioctl(handle, PPS_IOC_KCBIND, &arg));
}

#endif /* KERNEL */

#endif /* !_SYS_TIMEPPS_H_ */
