/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IEEE802154_NL802154_H
#define __IEEE802154_NL802154_H

int nl802154_init(void);
void nl802154_exit(void);
int nl802154_scan_event(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
			struct ieee802154_coord_desc *desc);
int nl802154_scan_started(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev);
int nl802154_scan_done(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		       enum nl802154_scan_done_reasons reason);
void nl802154_beaconing_done(struct wpan_dev *wpan_dev);

#endif /* __IEEE802154_NL802154_H */
