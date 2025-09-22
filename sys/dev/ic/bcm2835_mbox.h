/*     $OpenBSD: bcm2835_mbox.h,v 1.1 2020/04/19 14:51:52 tobhe Exp $ */

/*
 * Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2019 Neil Ashford <ashfordneil0@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BCM2835_MBOX_H
#define BCM2835_MBOX_H

#define BCMMBOX_NUM_CHANNELS 16
#define BCMMBOX_CHANNEL_MASK 0xf

/* mailbox 0 (from VC) and mailbox 1 (to VC) */
#define BCMMBOX_SIZE 0x80

#define BCMMBOX_READ 0x00
#define BCMMBOX_WRITE 0x00
#define BCMMBOX_POLL 0x10   /* read without popping the fifo */
#define BCMMBOX_ID 0x14     /* sender ID (bottom two bits) */
#define BCMMBOX_STATUS 0x18 /* status */
#define  BCMMBOX_STATUS_FULL 0x80000000
#define  BCMMBOX_STATUS_EMPTY 0x40000000
#define  BCMMBOX_STATUS_LEVEL 0x400000FF
#define BCMMBOX_CFG 0x1C /* configuration */
#define  BCMMBOX_CFG_DATA_IRQ_EN 0x00000001
#define  BCMMBOX_CFG_SPACE_IRQ_EN 0x00000002
#define  BCMMBOX_CFG_EMPTYOP_IRQ_EN 0x00000004
#define  BCMMBOX_CFG_MAIL_CLEAR 0x00000008
#define  BCMMBOX_CFG_DATA_PENDING 0x00000010
#define  BCMMBOX_CFG_SPACE_PENDING 0x00000020
#define  BCMMBOX_CFG_EMPTY_OP_PENDING 0x00000040
#define  BCMMBOX_CFG_E_NO_OWN 0x00000100
#define  BCMMBOX_CFG_E_OVERFLOW 0x00000200
#define  BCMMBOX_CFG_E_UNDERFLOW 0x00000400

#define BCMMBOX0_BASE 0x00
#define BCMMBOX1_BASE 0x20

#define BCMMBOX0_READ (BCMMBOX0_BASE + BCMMBOX_READ)
#define BCMMBOX0_WRITE (BCMMBOX0_BASE + BCMMBOX_WRITE)
#define BCMMBOX0_POLL (BCMMBOX0_BASE + BCMMBOX_POLL)
#define BCMMBOX0_ID (BCMMBOX0_BASE + BCMMBOX_ID)
#define BCMMBOX0_STATUS (BCMMBOX0_BASE + BCMMBOX_STATUS)
#define BCMMBOX0_CFG (BCMMBOX0_BASE + BCMMBOX_READ)

#define BCMMBOX1_READ (BCMMBOX1_BASE + BCMMBOX_READ)
#define BCMMBOX1_WRITE (BCMMBOX1_BASE + BCMMBOX_WRITE)
#define BCMMBOX1_POLL (BCMMBOX1_BASE + BCMMBOX_POLL)
#define BCMMBOX1_ID (BCMMBOX1_BASE + BCMMBOX_ID)
#define BCMMBOX1_STATUS (BCMMBOX1_BASE + BCMMBOX_STATUS)
#define BCMMBOX1_CFG (BCMMBOX1_BASE + BCMMBOX_READ)

void bcmmbox_read(uint8_t chan, uint32_t *data);
void bcmmbox_write(uint8_t chan, uint32_t data);

int bcmmbox_post(uint8_t, void *, size_t, uint32_t *);

#endif /* BCM2835_MBOX_H */
