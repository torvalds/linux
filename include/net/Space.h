/* SPDX-License-Identifier: GPL-2.0 */
/* A unified ethernet device probe.  This is the easiest way to have every
 * ethernet adaptor have the name "eth[0123...]".
 */

struct net_device *ultra_probe(int unit);
struct net_device *wd_probe(int unit);
struct net_device *ne_probe(int unit);
struct net_device *smc_init(int unit);
struct net_device *cs89x0_probe(int unit);
struct net_device *tc515_probe(int unit);
struct net_device *lance_probe(int unit);
