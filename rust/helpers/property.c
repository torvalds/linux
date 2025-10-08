// SPDX-License-Identifier: GPL-2.0

#include <linux/property.h>

void rust_helper_fwnode_handle_put(struct fwnode_handle *fwnode)
{
	fwnode_handle_put(fwnode);
}
