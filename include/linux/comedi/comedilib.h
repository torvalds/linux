/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * comedilib.h
 * Header file for kcomedilib
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998-2001 David A. Schleef <ds@schleef.org>
 */

#ifndef _LINUX_COMEDILIB_H
#define _LINUX_COMEDILIB_H

struct comedi_device *comedi_open_from(const char *path, int from);

/**
 * comedi_open() - Open a COMEDI device from the kernel
 * @filename: Fake pathname of the form "/dev/comediN".
 *
 * Converts @filename to a COMEDI device number and "opens" it if it exists
 * and is attached to a low-level COMEDI driver.
 *
 * Return: A pointer to the COMEDI device on success.
 * Return %NULL on failure.
 */
static inline struct comedi_device *comedi_open(const char *path)
{
	return comedi_open_from(path, -1);
}

int comedi_close_from(struct comedi_device *dev, int from);

/**
 * comedi_close() - Close a COMEDI device from the kernel
 * @dev: COMEDI device.
 *
 * Closes a COMEDI device previously opened by comedi_open().
 *
 * Returns: 0
 */
static inline int comedi_close(struct comedi_device *dev)
{
	return comedi_close_from(dev, -1);
}

int comedi_dio_get_config(struct comedi_device *dev, unsigned int subdev,
			  unsigned int chan, unsigned int *io);
int comedi_dio_config(struct comedi_device *dev, unsigned int subdev,
		      unsigned int chan, unsigned int io);
int comedi_dio_bitfield2(struct comedi_device *dev, unsigned int subdev,
			 unsigned int mask, unsigned int *bits,
			 unsigned int base_channel);
int comedi_find_subdevice_by_type(struct comedi_device *dev, int type,
				  unsigned int subd);
int comedi_get_n_channels(struct comedi_device *dev, unsigned int subdevice);

#endif
