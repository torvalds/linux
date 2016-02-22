/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _TRACER_PKT_H_
#define _TRACER_PKT_H_

#include <linux/err.h>
#include <linux/types.h>

#ifdef CONFIG_TRACER_PKT

/**
 * tracer_pkt_init() - initialize the tracer packet
 * @data:		Pointer to the buffer to be initialized with a tracer
 *			packet.
 * @data_len:		Length of the buffer.
 * @client_event_cfg:	Client-specific event configuration mask.
 * @glink_event_cfg:	G-Link-specific event configuration mask.
 * @pkt_priv:		Private/Cookie information to be added to the tracer
 *			packet.
 * @pkt_priv_len:	Length of the private data.
 *
 * This function is used to initialize a buffer with the tracer packet header.
 * The tracer packet header includes the data as passed by the elements in the
 * parameters.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int tracer_pkt_init(void *data, size_t data_len,
		    uint16_t client_event_cfg, uint32_t glink_event_cfg,
		    void *pkt_priv, size_t pkt_priv_len);

/**
 * tracer_pkt_set_event_cfg() - set the event configuration mask in the tracer
 *				packet
 * @data:		Pointer to the buffer to be initialized with event
 *			configuration mask.
 * @client_event_cfg:	Client-specific event configuration mask.
 * @glink_event_cfg:	G-Link-specific event configuration mask.
 *
 * This function is used to initialize a buffer with the event configuration
 * mask as passed by the elements in the parameters.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int tracer_pkt_set_event_cfg(void *data, uint16_t client_event_cfg,
			     uint32_t glink_event_cfg);

/**
 * tracer_pkt_log_event() - log an event specific to the tracer packet
 * @data:	Pointer to the buffer containing tracer packet.
 * @event_id:	Event ID to be logged.
 *
 * This function is used to log an event specific to the tracer packet.
 * The event is logged either into the tracer packet itself or a different
 * tracing mechanism as configured.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int tracer_pkt_log_event(void *data, uint32_t event_id);

/**
 * tracer_pkt_calc_hex_dump_size() - calculate the hex dump size of a tracer
 *				     packet
 * @data:	Pointer to the buffer containing tracer packet.
 * @data_len:	Length of the tracer packet buffer.
 *
 * This function is used to calculate the length of the buffer required to
 * hold the hex dump of the tracer packet.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
size_t tracer_pkt_calc_hex_dump_size(void *data, size_t data_len);

/**
 * tracer_pkt_hex_dump() - hex dump the tracer packet into a buffer
 * @buf:	Buffer to contain the hex dump of the tracer packet.
 * @buf_len:	Length of the hex dump buffer.
 * @data:	Buffer containing the tracer packet.
 * @data_len:	Length of the buffer containing the tracer packet.
 *
 * This function is used to dump the contents of the tracer packet into
 * a buffer in a specific hexadecimal format. The hex dump buffer can then
 * be dumped through debugfs.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int tracer_pkt_hex_dump(void *buf, size_t buf_len, void *data, size_t data_len);

#else

static inline int tracer_pkt_init(void *data, size_t data_len,
		    uint16_t client_event_cfg, uint32_t glink_event_cfg,
		    void *pkt_priv, size_t pkt_priv_len)
{
	return -EOPNOTSUPP;
}

static inline int tracer_pkt_set_event_cfg(uint16_t client_event_cfg,
					   uint32_t glink_event_cfg)
{
	return -EOPNOTSUPP;
}

static inline int tracer_pkt_log_event(void *data, uint32_t event_id)
{
	return -EOPNOTSUPP;
}

static inline size_t tracer_pkt_calc_hex_dump_size(void *data, size_t data_len)
{
	return -EOPNOTSUPP;
}

static inline int tracer_pkt_hex_dump(void *buf, size_t buf_len, void *data,
				      size_t data_len)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_TRACER_PKT */
#endif /* _TRACER_PKT_H_ */
