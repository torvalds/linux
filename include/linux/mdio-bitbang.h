/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MDIO_BITBANG_H
#define __LINUX_MDIO_BITBANG_H

#include <linux/phy.h>

struct module;

struct mdiobb_ctrl;

struct mdiobb_ops {
	struct module *owner;

	/* Set the Management Data Clock high if level is one,
	 * low if level is zero.
	 */
	void (*set_mdc)(struct mdiobb_ctrl *ctrl, int level);

	/* Configure the Management Data I/O pin as an input if
	 * "output" is zero, or an output if "output" is one.
	 */
	void (*set_mdio_dir)(struct mdiobb_ctrl *ctrl, int output);

	/* Set the Management Data I/O pin high if value is one,
	 * low if "value" is zero.  This may only be called
	 * when the MDIO pin is configured as an output.
	 */
	void (*set_mdio_data)(struct mdiobb_ctrl *ctrl, int value);

	/* Retrieve the state Management Data I/O pin. */
	int (*get_mdio_data)(struct mdiobb_ctrl *ctrl);
};

struct mdiobb_ctrl {
	const struct mdiobb_ops *ops;
};

/* The returned bus is not yet registered with the phy layer. */
struct mii_bus *alloc_mdio_bitbang(struct mdiobb_ctrl *ctrl);

/* The bus must already have been unregistered. */
void free_mdio_bitbang(struct mii_bus *bus);

#endif
