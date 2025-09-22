/*	$OpenBSD: hotplug.h,v 1.5 2006/05/28 16:52:34 mk Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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

#ifndef _SYS_HOTPLUG_H_
#define _SYS_HOTPLUG_H_

/*
 * Public interface for enqueuing and dequeueing device
 * attachment and detachment notifications.
 */

#define HOTPLUG_DEVAT		0x01	/* device attached	*/
#define HOTPLUG_DEVDT		0x02	/* device detached	*/

struct hotplug_event {
	int		he_type;	/* event type		*/
	enum devclass	he_devclass;	/* device class		*/
	char		he_devname[16];	/* device name		*/
};

#ifdef _KERNEL
void	hotplug_device_attach(enum devclass, char *);
void	hotplug_device_detach(enum devclass, char *);
#endif

#endif	/* _SYS_HOTPLUG_H_ */
