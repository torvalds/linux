/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_SND_FRONT_EVTCHNL_H
#define __XEN_SND_FRONT_EVTCHNL_H

#include <xen/interface/io/sndif.h>

struct xen_snd_front_info;

/* Timeout in ms to wait for backend to respond. */
#define VSND_WAIT_BACK_MS	3000

enum xen_snd_front_evtchnl_state {
	EVTCHNL_STATE_DISCONNECTED,
	EVTCHNL_STATE_CONNECTED,
};

enum xen_snd_front_evtchnl_type {
	EVTCHNL_TYPE_REQ,
	EVTCHNL_TYPE_EVT,
};

struct xen_snd_front_evtchnl {
	struct xen_snd_front_info *front_info;
	int gref;
	int port;
	int irq;
	int index;
	/* State of the event channel. */
	enum xen_snd_front_evtchnl_state state;
	enum xen_snd_front_evtchnl_type type;
	/* Either response id or incoming event id. */
	u16 evt_id;
	/* Next request id or next expected event id. */
	u16 evt_next_id;
	/* Shared ring access lock. */
	struct mutex ring_io_lock;
	union {
		struct {
			struct xen_sndif_front_ring ring;
			struct completion completion;
			/* Serializer for backend IO: request/response. */
			struct mutex req_io_lock;

			/* Latest response status. */
			int resp_status;
			union {
				struct xensnd_query_hw_param hw_param;
			} resp;
		} req;
		struct {
			struct xensnd_event_page *page;
			/* This is needed to handle XENSND_EVT_CUR_POS event. */
			struct snd_pcm_substream *substream;
		} evt;
	} u;
};

struct xen_snd_front_evtchnl_pair {
	struct xen_snd_front_evtchnl req;
	struct xen_snd_front_evtchnl evt;
};

int xen_snd_front_evtchnl_create_all(struct xen_snd_front_info *front_info,
				     int num_streams);

void xen_snd_front_evtchnl_free_all(struct xen_snd_front_info *front_info);

int xen_snd_front_evtchnl_publish_all(struct xen_snd_front_info *front_info);

void xen_snd_front_evtchnl_flush(struct xen_snd_front_evtchnl *evtchnl);

void xen_snd_front_evtchnl_pair_set_connected(struct xen_snd_front_evtchnl_pair *evt_pair,
					      bool is_connected);

void xen_snd_front_evtchnl_pair_clear(struct xen_snd_front_evtchnl_pair *evt_pair);

#endif /* __XEN_SND_FRONT_EVTCHNL_H */
