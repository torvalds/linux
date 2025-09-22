/*	$OpenBSD: pciide_jmicron_reg.h,v 1.1 2007/03/21 12:20:30 jsg Exp $	*/

/*
 * Copyright (c) 2007 Jonathan Gray <jsg@openbsd.org>
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

#ifndef _DEV_PCI_PCIIDE_JMICRON_REG_H
#define _DEV_PCI_PCIIDE_JMICRON_REG_H

#define JMICRON_MASTER_UDMA		(1 << 2)
#define JMICRON_MASTER_SHIFT		4
#define JMICRON_SLAVE_SHIFT		12

#define JMICRON_CONF			0x40
#define JMICRON_CHAN_EN(chan)		((chan == 1) ? 4 : 0)

#define JMICRON_CONF_SWAP		(1 << 22)
#define JMICRON_CONF_40PIN		(1 << 3)

#endif
