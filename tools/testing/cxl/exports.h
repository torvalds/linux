/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2025 Intel Corporation */
#ifndef __MOCK_CXL_EXPORTS_H_
#define __MOCK_CXL_EXPORTS_H_

typedef struct cxl_dport *(*cxl_add_dport_by_dev_fn)(struct cxl_port *port,
							  struct device *dport_dev);
extern cxl_add_dport_by_dev_fn _devm_cxl_add_dport_by_dev;

#endif
