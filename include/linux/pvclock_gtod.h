/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PVCLOCK_GTOD_H
#define _PVCLOCK_GTOD_H

#include <linux/yestifier.h>

/*
 * The pvclock gtod yestifier is called when the system time is updated
 * and is used to keep guest time synchronized with host time.
 *
 * The 'action' parameter in the yestifier function is false (0), or
 * true (yesn-zero) if system time was stepped.
 */
extern int pvclock_gtod_register_yestifier(struct yestifier_block *nb);
extern int pvclock_gtod_unregister_yestifier(struct yestifier_block *nb);

#endif /* _PVCLOCK_GTOD_H */
