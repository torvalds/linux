/* Public domain. */

#ifndef _LINUX_PHY_PHY_H
#define _LINUX_PHY_PHY_H

struct phy_configure_opts_dp {
	u_int link_rate;
	u_int lanes;
	int set_rate : 1;
	int set_lanes : 1;
	int set_voltages : 1;
};

union phy_configure_opts {
	struct phy_configure_opts_dp dp;
};

enum phy_mode {
	PHY_MODE_INVALID,
	PHY_MODE_DP,
};

struct phy;

struct phy *devm_phy_optional_get(struct device *, const char *);

static inline int
phy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	return 0;
}

static inline int
phy_set_mode_ext(struct phy *phy, enum phy_mode mode, int submode)
{
	return 0;
}

#endif
