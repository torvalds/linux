/*-
 * Copyright (c) 2016 Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * Copyright (c) 2015-2016 Vladimir Kondratyev <wulf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _EVDEV_UINPUT_H_
#define	_EVDEV_UINPUT_H_

#include <sys/types.h>
#include <dev/evdev/input.h>

#define UINPUT_VERSION		5
#define	UINPUT_MAX_NAME_SIZE	80

struct uinput_ff_upload {
	uint32_t		request_id;
	int32_t			retval;
	struct ff_effect	effect;
	struct ff_effect	old;
};

struct uinput_ff_erase {
	uint32_t		request_id;
	int32_t			retval;
	uint32_t		effect_id;
};

/* ioctl */
#define UINPUT_IOCTL_BASE	'U'

#define UI_DEV_CREATE		_IO(UINPUT_IOCTL_BASE, 1)
#define UI_DEV_DESTROY		_IO(UINPUT_IOCTL_BASE, 2)

struct uinput_setup {
	struct input_id	id;
	char		name[UINPUT_MAX_NAME_SIZE];
	uint32_t	ff_effects_max;
};

#define UI_DEV_SETUP _IOW(UINPUT_IOCTL_BASE, 3, struct uinput_setup)

struct uinput_abs_setup {
	uint16_t		code; /* axis code */
	struct input_absinfo	absinfo;
};

#define UI_ABS_SETUP		_IOW(UINPUT_IOCTL_BASE, 4, struct uinput_abs_setup)

#define UI_GET_SYSNAME(len)	_IOC(IOC_OUT, UINPUT_IOCTL_BASE, 44, len)
#define UI_GET_VERSION		_IOR(UINPUT_IOCTL_BASE, 45, unsigned int)

#define UI_SET_EVBIT		_IOWINT(UINPUT_IOCTL_BASE, 100)
#define UI_SET_KEYBIT		_IOWINT(UINPUT_IOCTL_BASE, 101)
#define UI_SET_RELBIT		_IOWINT(UINPUT_IOCTL_BASE, 102)
#define UI_SET_ABSBIT		_IOWINT(UINPUT_IOCTL_BASE, 103)
#define UI_SET_MSCBIT		_IOWINT(UINPUT_IOCTL_BASE, 104)
#define UI_SET_LEDBIT		_IOWINT(UINPUT_IOCTL_BASE, 105)
#define UI_SET_SNDBIT		_IOWINT(UINPUT_IOCTL_BASE, 106)
#define UI_SET_FFBIT		_IOWINT(UINPUT_IOCTL_BASE, 107)
#define UI_SET_PHYS		_IO(UINPUT_IOCTL_BASE, 108)
#define UI_SET_SWBIT		_IOWINT(UINPUT_IOCTL_BASE, 109)
#define UI_SET_PROPBIT		_IOWINT(UINPUT_IOCTL_BASE, 110)

#define UI_BEGIN_FF_UPLOAD	_IOWR(UINPUT_IOCTL_BASE, 200, struct uinput_ff_upload)
#define UI_END_FF_UPLOAD	_IOW(UINPUT_IOCTL_BASE, 201, struct uinput_ff_upload)
#define UI_BEGIN_FF_ERASE	_IOWR(UINPUT_IOCTL_BASE, 202, struct uinput_ff_erase)
#define UI_END_FF_ERASE		_IOW(UINPUT_IOCTL_BASE, 203, struct uinput_ff_erase)

/*
 * FreeBSD specific. Set unique identifier of input device.
 * Name and magic are chosen to reduce chances of clashing
 * with possible future Linux extensions.
 */
#define UI_SET_BSDUNIQ		_IO(UINPUT_IOCTL_BASE, 109)

#define EV_UINPUT		0x0101
#define UI_FF_UPLOAD		1
#define UI_FF_ERASE		2

struct uinput_user_dev {
	char		name[UINPUT_MAX_NAME_SIZE];
	struct input_id	id;
	uint32_t	ff_effects_max;
	int32_t		absmax[ABS_CNT];
	int32_t		absmin[ABS_CNT];
	int32_t		absfuzz[ABS_CNT];
	int32_t		absflat[ABS_CNT];
};

#endif /* _EVDEV_UINPUT_H_ */
