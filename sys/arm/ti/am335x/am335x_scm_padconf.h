/*-
 * Copyright (c) 2012 Damjan Marion <dmarion@FreeBSD.org>
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

#ifndef AM335X_SCM_PADCONF_H
#define AM335X_SCM_PADCONF_H

#define SLEWCTRL	(0x01 << 6) /* faster(0) or slower(1) slew rate. */
#define RXACTIVE	(0x01 << 5) /* Input enable value for the Pad */
#define PULLTYPESEL	(0x01 << 4) /* Pad pullup/pulldown type selection */
#define PULLUDEN	(0x01 << 3) /* Pullup/pulldown disabled */

#define PADCONF_OUTPUT			(PULLUDEN)
#define PADCONF_OUTPUT_PULLUP		(PULLTYPESEL)
#define PADCONF_OUTPUT_PULLDOWN		(0)
#define PADCONF_INPUT			(RXACTIVE | PULLUDEN)
#define PADCONF_INPUT_PULLUP		(RXACTIVE | PULLTYPESEL)
#define PADCONF_INPUT_PULLDOWN		(RXACTIVE)
#define PADCONF_INPUT_PULLUP_SLOW	(PADCONF_INPUT_PULLUP | SLEWCTRL)

extern const struct ti_pinmux_device ti_am335x_pinmux_dev;

#endif /* AM335X_SCM_PADCONF_H */
