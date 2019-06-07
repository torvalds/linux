/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for talking to the CUDA.  The CUDA is a microcontroller
 * which controls the ADB, system power, RTC, and various other things.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#ifndef _LINUX_CUDA_H
#define _LINUX_CUDA_H

#include <linux/rtc.h>
#include <uapi/linux/cuda.h>


extern int find_via_cuda(void);
extern int cuda_request(struct adb_request *req,
			void (*done)(struct adb_request *), int nbytes, ...);
extern void cuda_poll(void);

extern time64_t cuda_get_time(void);
extern int cuda_set_rtc_time(struct rtc_time *tm);

#endif /* _LINUX_CUDA_H */
