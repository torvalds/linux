#ifndef __PHY_FIXED_H
#define __PHY_FIXED_H

#define MII_REGS_NUM	29

/* max number of virtual phy stuff */
#define MAX_PHY_AMNT	10
/*
    The idea is to emulate normal phy behavior by responding with
    pre-defined values to mii BMCR read, so that read_status hook could
    take all the needed info.
*/

struct fixed_phy_status {
	u8 link;
	u16 speed;
	u8 duplex;
};

/*-----------------------------------------------------------------------------
 *  Private information hoder for mii_bus
 *-----------------------------------------------------------------------------*/
struct fixed_info {
	u16 *regs;
	u8 regs_num;
	struct fixed_phy_status phy_status;
	struct phy_device *phydev;	/* pointer to the container */
	/* link & speed cb */
	int (*link_update) (struct net_device *, struct fixed_phy_status *);

};


int fixed_mdio_set_link_update(struct phy_device *,
       int (*link_update) (struct net_device *, struct fixed_phy_status *));
struct fixed_info *fixed_mdio_get_phydev (int phydev_ind);

#endif /* __PHY_FIXED_H */
