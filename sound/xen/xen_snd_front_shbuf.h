/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_SND_FRONT_SHBUF_H
#define __XEN_SND_FRONT_SHBUF_H

#include <xen/grant_table.h>

#include "xen_snd_front_evtchnl.h"

struct xen_snd_front_shbuf {
	int num_grefs;
	grant_ref_t *grefs;
	u8 *directory;
	u8 *buffer;
	size_t buffer_sz;
};

grant_ref_t xen_snd_front_shbuf_get_dir_start(struct xen_snd_front_shbuf *buf);

int xen_snd_front_shbuf_alloc(struct xenbus_device *xb_dev,
			      struct xen_snd_front_shbuf *buf,
			      unsigned int buffer_sz);

void xen_snd_front_shbuf_clear(struct xen_snd_front_shbuf *buf);

void xen_snd_front_shbuf_free(struct xen_snd_front_shbuf *buf);

#endif /* __XEN_SND_FRONT_SHBUF_H */
