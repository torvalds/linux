// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */

#include "cxl.h"
#include "exports.h"

/* Exporting of cxl_core symbols that are only used by cxl_test */
EXPORT_SYMBOL_NS_GPL(cxl_num_decoders_committed, "CXL");

cxl_add_dport_by_dev_fn _devm_cxl_add_dport_by_dev =
	__devm_cxl_add_dport_by_dev;
EXPORT_SYMBOL_NS_GPL(_devm_cxl_add_dport_by_dev, "CXL");

struct cxl_dport *devm_cxl_add_dport_by_dev(struct cxl_port *port,
					    struct device *dport_dev)
{
	return _devm_cxl_add_dport_by_dev(port, dport_dev);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_dport_by_dev, "CXL");

cxl_switch_decoders_setup_fn _devm_cxl_switch_port_decoders_setup =
	__devm_cxl_switch_port_decoders_setup;
EXPORT_SYMBOL_NS_GPL(_devm_cxl_switch_port_decoders_setup, "CXL");

int devm_cxl_switch_port_decoders_setup(struct cxl_port *port)
{
	return _devm_cxl_switch_port_decoders_setup(port);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_switch_port_decoders_setup, "CXL");
