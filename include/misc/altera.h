/*
 * altera.h
 *
 * altera FPGA driver
 *
 * Copyright (C) Altera Corporation 1998-2001
 * Copyright (C) 2010 NetUP Inc.
 * Copyright (C) 2010 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _ALTERA_H_
#define _ALTERA_H_

struct altera_config {
	void *dev;
	u8 *action;
	int (*jtag_io) (void *dev, int tms, int tdi, int tdo);
};

#if defined(CONFIG_ALTERA_STAPL) || \
		(defined(CONFIG_ALTERA_STAPL_MODULE) && defined(MODULE))

extern int altera_init(struct altera_config *config, const struct firmware *fw);
#else

static inline int altera_init(struct altera_config *config,
						const struct firmware *fw)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return 0;
}
#endif /* CONFIG_ALTERA_STAPL */

#endif /* _ALTERA_H_ */
