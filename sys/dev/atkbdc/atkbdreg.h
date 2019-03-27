/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_ATKBDC_ATKBDREG_H_
#define _DEV_ATKBDC_ATKBDREG_H_

#define ATKBD_DRIVER_NAME	"atkbd"

/* device configuration flags (atkbdprobe, atkbdattach) */
#define KB_CONF_FAIL_IF_NO_KBD	(1 << 0) /* don't install if no kbd is found */
#define KB_CONF_NO_RESET	(1 << 1) /* don't reset the keyboard */
#define KB_CONF_ALT_SCANCODESET	(1 << 2) /* assume the XT type keyboard */
#define	KB_CONF_NO_PROBE_TEST	(1 << 3) /* don't test keyboard during probe */

#ifdef _KERNEL

int		atkbd_probe_unit(device_t dev, int irq, int flags);
int		atkbd_attach_unit(device_t dev, keyboard_t **kbd, int irq, int flags);

#endif

#endif /* !_DEV_ATKBDC_ATKBDREG_H_ */
