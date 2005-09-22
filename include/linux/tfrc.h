#ifndef _LINUX_TFRC_H_
#define _LINUX_TFRC_H_
/*
 *  include/linux/tfrc.h
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005 Ian McDonald <iam4@cs.waikato.ac.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *  Copyright (c) 2003 Nils-Erik Mattsson, Joacim Haggmark, Magnus Erixzon
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/types.h>

struct tfrc_rx_info {
  	__u32 tfrcrx_x_recv;
	__u32 tfrcrx_rtt;
  	__u32 tfrcrx_p;
};

struct tfrc_tx_info {
	__u32 tfrctx_x;
	__u32 tfrctx_x_recv;
	__u32 tfrctx_x_calc;
	__u32 tfrctx_rtt;
	__u32 tfrctx_p;
	__u32 tfrctx_rto;
	__u32 tfrctx_ipi;
};

#endif /* _LINUX_TFRC_H_ */
