/*
 * MV-643XX ethernet platform device data definition file.
 */
#ifndef __LINUX_MV643XX_ETH_H
#define __LINUX_MV643XX_ETH_H

#define MV643XX_ETH_SHARED_NAME		"mv643xx_eth_shared"
#define MV643XX_ETH_NAME		"mv643xx_eth"
#define MV643XX_ETH_SHARED_REGS		0x2000
#define MV643XX_ETH_SHARED_REGS_SIZE	0x2000
#define MV643XX_ETH_BAR_4		0x2220
#define MV643XX_ETH_SIZE_REG_4		0x2224
#define MV643XX_ETH_BASE_ADDR_ENABLE_REG	0x2290

struct mv643xx_eth_platform_data {
	int		port_number;
	u16		force_phy_addr;	/* force override if phy_addr == 0 */
	u16		phy_addr;

	/* If speed is 0, then speed and duplex are autonegotiated. */
	int		speed;		/* 0, SPEED_10, SPEED_100, SPEED_1000 */
	int		duplex;		/* DUPLEX_HALF or DUPLEX_FULL */

	/* non-zero values of the following fields override defaults */
	u32		tx_queue_size;
	u32		rx_queue_size;
	u32		tx_sram_addr;
	u32		tx_sram_size;
	u32		rx_sram_addr;
	u32		rx_sram_size;
	u8		mac_addr[6];	/* mac address if non-zero*/
};

#endif /* __LINUX_MV643XX_ETH_H */
