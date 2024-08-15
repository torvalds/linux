/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PISMO memory driver - http://www.pismoworld.org/
 */
#ifndef __LINUX_MTD_PISMO_H
#define __LINUX_MTD_PISMO_H

struct pismo_pdata {
	void			(*set_vpp)(void *, int);
	void			*vpp_data;
	phys_addr_t		cs_addrs[5];
};

#endif
