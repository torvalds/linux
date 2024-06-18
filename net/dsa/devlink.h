/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_DEVLINK_H
#define __DSA_DEVLINK_H

struct dsa_port;
struct dsa_switch;

int dsa_port_devlink_setup(struct dsa_port *dp);
void dsa_port_devlink_teardown(struct dsa_port *dp);
void dsa_switch_devlink_register(struct dsa_switch *ds);
void dsa_switch_devlink_unregister(struct dsa_switch *ds);
int dsa_switch_devlink_alloc(struct dsa_switch *ds);
void dsa_switch_devlink_free(struct dsa_switch *ds);

#endif
