/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_DEVLINK_H
#define __DSA_DEVLINK_H

struct dsa_port;

extern const struct devlink_ops dsa_devlink_ops;

int dsa_port_devlink_setup(struct dsa_port *dp);
void dsa_port_devlink_teardown(struct dsa_port *dp);

#endif
