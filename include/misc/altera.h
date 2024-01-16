/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * altera.h
 *
 * altera FPGA driver
 *
 * Copyright (C) Altera Corporation 1998-2001
 * Copyright (C) 2010 NetUP Inc.
 * Copyright (C) 2010 Igor M. Liplianin <liplianin@netup.ru>
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
