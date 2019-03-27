/*-
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BEN GRAY ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BEN GRAY BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef OMAP4_SCM_PADCONF_H
#define	OMAP4_SCM_PADCONF_H

#define CONTROL_PADCONF_WAKEUP_EVENT     (1UL << 15)
#define CONTROL_PADCONF_WAKEUP_ENABLE    (1UL << 14)
#define CONTROL_PADCONF_OFF_PULL_UP      (1UL << 13)
#define CONTROL_PADCONF_OFF_PULL_ENABLE  (1UL << 12)
#define CONTROL_PADCONF_OFF_OUT_HIGH     (1UL << 11)
#define CONTROL_PADCONF_OFF_OUT_ENABLE   (1UL << 10)
#define CONTROL_PADCONF_OFF_ENABLE       (1UL << 9)
#define CONTROL_PADCONF_INPUT_ENABLE     (1UL << 8)
#define CONTROL_PADCONF_PULL_UP          (1UL << 4)
#define CONTROL_PADCONF_PULL_ENABLE      (1UL << 3)
#define CONTROL_PADCONF_MUXMODE_MASK     (0x7)

#define CONTROL_PADCONF_SATE_MASK        ( CONTROL_PADCONF_WAKEUP_EVENT \
                                         | CONTROL_PADCONF_WAKEUP_ENABLE \
                                         | CONTROL_PADCONF_OFF_PULL_UP \
                                         | CONTROL_PADCONF_OFF_PULL_ENABLE \
                                         | CONTROL_PADCONF_OFF_OUT_HIGH \
                                         | CONTROL_PADCONF_OFF_OUT_ENABLE \
                                         | CONTROL_PADCONF_OFF_ENABLE \
                                         | CONTROL_PADCONF_INPUT_ENABLE \
                                         | CONTROL_PADCONF_PULL_UP \
                                         | CONTROL_PADCONF_PULL_ENABLE )

/* Active pin states */
#define PADCONF_PIN_OUTPUT              0
#define PADCONF_PIN_INPUT               CONTROL_PADCONF_INPUT_ENABLE
#define PADCONF_PIN_INPUT_PULLUP        ( CONTROL_PADCONF_INPUT_ENABLE \
                                        | CONTROL_PADCONF_PULL_ENABLE \
                                        | CONTROL_PADCONF_PULL_UP)
#define PADCONF_PIN_INPUT_PULLDOWN      ( CONTROL_PADCONF_INPUT_ENABLE \
                                        | CONTROL_PADCONF_PULL_ENABLE )

/* Off mode states */
#define PADCONF_PIN_OFF_NONE            0
#define PADCONF_PIN_OFF_OUTPUT_HIGH	    ( CONTROL_PADCONF_OFF_ENABLE \
                                        | CONTROL_PADCONF_OFF_OUT_ENABLE \
                                        | CONTROL_PADCONF_OFF_OUT_HIGH)
#define PADCONF_PIN_OFF_OUTPUT_LOW      ( CONTROL_PADCONF_OFF_ENABLE \
                                        | CONTROL_PADCONF_OFF_OUT_ENABLE)
#define PADCONF_PIN_OFF_INPUT_PULLUP    ( CONTROL_PADCONF_OFF_ENABLE \
                                        | CONTROL_PADCONF_OFF_PULL_ENABLE \
                                        | CONTROL_PADCONF_OFF_PULL_UP)
#define PADCONF_PIN_OFF_INPUT_PULLDOWN  ( CONTROL_PADCONF_OFF_ENABLE \
                                        | CONTROL_PADCONF_OFF_PULL_ENABLE)
#define PADCONF_PIN_OFF_WAKEUPENABLE	CONTROL_PADCONF_WAKEUP_ENABLE

extern const struct ti_pinmux_device omap4_pinmux_dev;

#endif /* OMAP4_SCM_PADCONF_H */
