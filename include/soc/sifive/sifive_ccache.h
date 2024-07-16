/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SiFive Composable Cache Controller header file
 *
 */

#ifndef __SOC_SIFIVE_CCACHE_H
#define __SOC_SIFIVE_CCACHE_H

extern int register_sifive_ccache_error_notifier(struct notifier_block *nb);
extern int unregister_sifive_ccache_error_notifier(struct notifier_block *nb);

#define SIFIVE_CCACHE_ERR_TYPE_CE 0
#define SIFIVE_CCACHE_ERR_TYPE_UE 1

#endif /* __SOC_SIFIVE_CCACHE_H */
