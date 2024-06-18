/*
 * dvb_net.h
 *
 * Copyright (C) 2001 Ralph Metzler for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DVB_NET_H_
#define _DVB_NET_H_

#include <linux/module.h>

#include <media/dvbdev.h>

struct net_device;

#define DVB_NET_DEVICES_MAX 10

#ifdef CONFIG_DVB_NET

/**
 * struct dvb_net - describes a DVB network interface
 *
 * @dvbdev:		pointer to &struct dvb_device.
 * @device:		array of pointers to &struct net_device.
 * @state:		array of integers to each net device. A value
 *			different than zero means that the interface is
 *			in usage.
 * @exit:		flag to indicate when the device is being removed.
 * @demux:		pointer to &struct dmx_demux.
 * @ioctl_mutex:	protect access to this struct.
 * @remove_mutex:	mutex that avoids a race condition between a callback
 *			called when the hardware is disconnected and the
 *			file_operations of dvb_net.
 *
 * Currently, the core supports up to %DVB_NET_DEVICES_MAX (10) network
 * devices.
 */

struct dvb_net {
	struct dvb_device *dvbdev;
	struct net_device *device[DVB_NET_DEVICES_MAX];
	int state[DVB_NET_DEVICES_MAX];
	unsigned int exit:1;
	struct dmx_demux *demux;
	struct mutex ioctl_mutex;
	struct mutex remove_mutex;
};

/**
 * dvb_net_init - nitializes a digital TV network device and registers it.
 *
 * @adap:	pointer to &struct dvb_adapter.
 * @dvbnet:	pointer to &struct dvb_net.
 * @dmxdemux:	pointer to &struct dmx_demux.
 */
int dvb_net_init(struct dvb_adapter *adap, struct dvb_net *dvbnet,
		  struct dmx_demux *dmxdemux);

/**
 * dvb_net_release - releases a digital TV network device and unregisters it.
 *
 * @dvbnet:	pointer to &struct dvb_net.
 */
void dvb_net_release(struct dvb_net *dvbnet);

#else

struct dvb_net {
	struct dvb_device *dvbdev;
};

static inline void dvb_net_release(struct dvb_net *dvbnet)
{
}

static inline int dvb_net_init(struct dvb_adapter *adap,
			       struct dvb_net *dvbnet, struct dmx_demux *dmx)
{
	return 0;
}

#endif /* ifdef CONFIG_DVB_NET */

#endif
