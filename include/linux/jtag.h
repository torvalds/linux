/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved. */
/* Copyright (c) 2018 Oleksandr Shamray <oleksandrs@mellanox.com> */
/* Copyright (c) 2019 Intel Corporation */

#ifndef __LINUX_JTAG_H
#define __LINUX_JTAG_H

#include <linux/types.h>
#include <uapi/linux/jtag.h>

#define JTAG_MAX_XFER_DATA_LEN 65535

struct jtag;
/**
 * struct jtag_ops - callbacks for JTAG control functions:
 *
 * @freq_get: get frequency function. Filled by dev driver
 * @freq_set: set frequency function. Filled by dev driver
 * @status_get: get JTAG TAPC state function. Mandatory, Filled by dev driver
 * @status_set: set JTAG TAPC state function. Mandatory, Filled by dev driver
 * @xfer: send JTAG xfer function. Mandatory func. Filled by dev driver
 * @mode_set: set specific work mode for JTAG. Filled by dev driver
 * @bitbang: set low level bitbang operations. Filled by dev driver
 * @enable: enables JTAG interface in master mode. Filled by dev driver
 * @disable: disables JTAG interface master mode. Filled by dev driver
 */
struct jtag_ops {
	int (*freq_get)(struct jtag *jtag, u32 *freq);
	int (*freq_set)(struct jtag *jtag, u32 freq);
	int (*status_get)(struct jtag *jtag, u32 *state);
	int (*status_set)(struct jtag *jtag, struct jtag_tap_state *endst);
	int (*xfer)(struct jtag *jtag, struct jtag_xfer *xfer, u8 *xfer_data);
	int (*mode_set)(struct jtag *jtag, struct jtag_mode *jtag_mode);
	int (*bitbang)(struct jtag *jtag, struct bitbang_packet *bitbang,
		       struct tck_bitbang *bitbang_data);
	int (*enable)(struct jtag *jtag);
	int (*disable)(struct jtag *jtag);
};

void *jtag_priv(struct jtag *jtag);
int devm_jtag_register(struct device *dev, struct jtag *jtag);
struct jtag *jtag_alloc(struct device *host, size_t priv_size,
			const struct jtag_ops *ops);
void jtag_free(struct jtag *jtag);

#endif /* __LINUX_JTAG_H */
