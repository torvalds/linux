/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2020, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef PHY_TEGRA_XUSB_H
#define PHY_TEGRA_XUSB_H

struct tegra_xusb_padctl;
struct device;
enum usb_device_speed;

struct tegra_xusb_padctl *tegra_xusb_padctl_get(struct device *dev);
void tegra_xusb_padctl_put(struct tegra_xusb_padctl *padctl);

int tegra_xusb_padctl_usb3_save_context(struct tegra_xusb_padctl *padctl,
					unsigned int port);
int tegra_xusb_padctl_hsic_set_idle(struct tegra_xusb_padctl *padctl,
				    unsigned int port, bool idle);
int tegra_xusb_padctl_usb3_set_lfps_detect(struct tegra_xusb_padctl *padctl,
					   unsigned int port, bool enable);
int tegra_xusb_padctl_set_vbus_override(struct tegra_xusb_padctl *padctl,
					bool val);
int tegra_phy_xusb_utmi_port_reset(struct phy *phy);
int tegra_xusb_padctl_get_usb3_companion(struct tegra_xusb_padctl *padctl,
					 unsigned int port);
int tegra_xusb_padctl_enable_phy_sleepwalk(struct tegra_xusb_padctl *padctl, struct phy *phy,
					   enum usb_device_speed speed);
int tegra_xusb_padctl_disable_phy_sleepwalk(struct tegra_xusb_padctl *padctl, struct phy *phy);
int tegra_xusb_padctl_enable_phy_wake(struct tegra_xusb_padctl *padctl, struct phy *phy);
int tegra_xusb_padctl_disable_phy_wake(struct tegra_xusb_padctl *padctl, struct phy *phy);
bool tegra_xusb_padctl_remote_wake_detected(struct tegra_xusb_padctl *padctl, struct phy *phy);

#endif /* PHY_TEGRA_XUSB_H */
