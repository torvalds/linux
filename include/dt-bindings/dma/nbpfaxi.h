/*
 * Copyright (C) 2013-2014 Renesas Electronics Europe Ltd.
 * Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#ifndef DT_BINDINGS_NBPFAXI_H
#define DT_BINDINGS_NBPFAXI_H

/**
 * Use "#dma-cells = <2>;" with the second integer defining slave DMA flags:
 */
#define NBPF_SLAVE_RQ_HIGH	1
#define NBPF_SLAVE_RQ_LOW	2
#define NBPF_SLAVE_RQ_LEVEL	4

#endif
