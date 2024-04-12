#ifndef __MDIO_BCM_UNIMAC_PDATA_H
#define __MDIO_BCM_UNIMAC_PDATA_H

struct clk;

struct unimac_mdio_pdata {
	u32 phy_mask;
	int (*wait_func)(void *data);
	void *wait_func_data;
	const char *bus_name;
	struct clk *clk;
};

#define UNIMAC_MDIO_DRV_NAME	"unimac-mdio"

#endif /* __MDIO_BCM_UNIMAC_PDATA_H */
