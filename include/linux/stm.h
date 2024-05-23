// SPDX-License-Identifier: GPL-2.0
/*
 * System Trace Module (STM) infrastructure apis
 * Copyright (C) 2014 Intel Corporation.
 */

#ifndef _STM_H_
#define _STM_H_

#include <linux/device.h>

/**
 * enum stp_packet_type - STP packets that an STM driver sends
 */
enum stp_packet_type {
	STP_PACKET_DATA = 0,
	STP_PACKET_FLAG,
	STP_PACKET_USER,
	STP_PACKET_MERR,
	STP_PACKET_GERR,
	STP_PACKET_TRIG,
	STP_PACKET_XSYNC,
};

/**
 * enum stp_packet_flags - STP packet modifiers
 */
enum stp_packet_flags {
	STP_PACKET_MARKED	= 0x1,
	STP_PACKET_TIMESTAMPED	= 0x2,
};

/**
 * enum stm_source_type - STM source driver
 * @STM_USER: any STM trace source
 * @STM_FTRACE: ftrace STM source
 */
enum stm_source_type {
	STM_USER,
	STM_FTRACE,
};

struct stp_policy;

struct stm_device;

/**
 * struct stm_data - STM device description and callbacks
 * @name:		device name
 * @stm:		internal structure, only used by stm class code
 * @sw_start:		first STP master available to software
 * @sw_end:		last STP master available to software
 * @sw_nchannels:	number of STP channels per master
 * @sw_mmiosz:		size of one channel's IO space, for mmap, optional
 * @hw_override:	masters in the STP stream will not match the ones
 *			assigned by software, but are up to the STM hardware
 * @packet:		callback that sends an STP packet
 * @mmio_addr:		mmap callback, optional
 * @link:		called when a new stm_source gets linked to us, optional
 * @unlink:		likewise for unlinking, again optional
 * @set_options:	set device-specific options on a channel
 *
 * Fill out this structure before calling stm_register_device() to create
 * an STM device and stm_unregister_device() to destroy it. It will also be
 * passed back to @packet(), @mmio_addr(), @link(), @unlink() and @set_options()
 * callbacks.
 *
 * Normally, an STM device will have a range of masters available to software
 * and the rest being statically assigned to various hardware trace sources.
 * The former is defined by the range [@sw_start..@sw_end] of the device
 * description. That is, the lowest master that can be allocated to software
 * writers is @sw_start and data from this writer will appear is @sw_start
 * master in the STP stream.
 *
 * The @packet callback should adhere to the following rules:
 *   1) it must return the number of bytes it consumed from the payload;
 *   2) therefore, if it sent a packet that does not have payload (like FLAG),
 *      it must return zero;
 *   3) if it does not support the requested packet type/flag combination,
 *      it must return -ENOTSUPP.
 *
 * The @unlink callback is called when there are no more active writers so
 * that the master/channel can be quiesced.
 */
struct stm_data {
	const char		*name;
	struct stm_device	*stm;
	unsigned int		sw_start;
	unsigned int		sw_end;
	unsigned int		sw_nchannels;
	unsigned int		sw_mmiosz;
	unsigned int		hw_override;
	ssize_t			(*packet)(struct stm_data *, unsigned int,
					  unsigned int, unsigned int,
					  unsigned int, unsigned int,
					  const unsigned char *);
	phys_addr_t		(*mmio_addr)(struct stm_data *, unsigned int,
					     unsigned int, unsigned int);
	int			(*link)(struct stm_data *, unsigned int,
					unsigned int);
	void			(*unlink)(struct stm_data *, unsigned int,
					  unsigned int);
	long			(*set_options)(struct stm_data *, unsigned int,
					       unsigned int, unsigned int,
					       unsigned long);
};

int stm_register_device(struct device *parent, struct stm_data *stm_data,
			struct module *owner);
void stm_unregister_device(struct stm_data *stm_data);

struct stm_source_device;

/**
 * struct stm_source_data - STM source device description and callbacks
 * @name:	device name, will be used for policy lookup
 * @src:	internal structure, only used by stm class code
 * @nr_chans:	number of channels to allocate
 * @type:	type of STM source driver represented by stm_source_type
 * @link:	called when this source gets linked to an STM device
 * @unlink:	called when this source is about to get unlinked from its STM
 *
 * Fill in this structure before calling stm_source_register_device() to
 * register a source device. Also pass it to unregister and write calls.
 */
struct stm_source_data {
	const char		*name;
	struct stm_source_device *src;
	unsigned int		percpu;
	unsigned int		nr_chans;
	unsigned int		type;
	int			(*link)(struct stm_source_data *data);
	void			(*unlink)(struct stm_source_data *data);
};

int stm_source_register_device(struct device *parent,
			       struct stm_source_data *data);
void stm_source_unregister_device(struct stm_source_data *data);

int notrace stm_source_write(struct stm_source_data *data, unsigned int chan,
			     const char *buf, size_t count);

#endif /* _STM_H_ */
