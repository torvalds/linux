/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_SWITCH_H
#define __DSA_SWITCH_H

struct dsa_switch;

int dsa_switch_register_notifier(struct dsa_switch *ds);
void dsa_switch_unregister_notifier(struct dsa_switch *ds);

#endif
