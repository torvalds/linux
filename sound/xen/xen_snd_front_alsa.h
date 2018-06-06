/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_SND_FRONT_ALSA_H
#define __XEN_SND_FRONT_ALSA_H

struct xen_snd_front_info;

int xen_snd_front_alsa_init(struct xen_snd_front_info *front_info);

void xen_snd_front_alsa_fini(struct xen_snd_front_info *front_info);

void xen_snd_front_alsa_handle_cur_pos(struct xen_snd_front_evtchnl *evtchnl,
				       u64 pos_bytes);

#endif /* __XEN_SND_FRONT_ALSA_H */
