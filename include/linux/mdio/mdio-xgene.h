/* SPDX-License-Identifier: GPL-2.0+ */
/* Applied Micro X-Gene SoC MDIO Driver
 *
 * Copyright (c) 2016, Applied Micro Circuits Corporation
 * Author: Iyappan Subramanian <isubramanian@apm.com>
 */

#ifndef __MDIO_XGENE_H__
#define __MDIO_XGENE_H__

#include <linux/bits.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define BLOCK_XG_MDIO_CSR_OFFSET	0x5000
#define BLOCK_DIAG_CSR_OFFSET		0xd000
#define XGENET_CONFIG_REG_ADDR		0x20

#define MAC_ADDR_REG_OFFSET		0x00
#define MAC_COMMAND_REG_OFFSET		0x04
#define MAC_WRITE_REG_OFFSET		0x08
#define MAC_READ_REG_OFFSET		0x0c
#define MAC_COMMAND_DONE_REG_OFFSET	0x10

#define CLKEN_OFFSET			0x08
#define SRST_OFFSET			0x00

#define MENET_CFG_MEM_RAM_SHUTDOWN_ADDR	0x70
#define MENET_BLOCK_MEM_RDY_ADDR	0x74

#define MAC_CONFIG_1_ADDR		0x00
#define MII_MGMT_COMMAND_ADDR		0x24
#define MII_MGMT_ADDRESS_ADDR		0x28
#define MII_MGMT_CONTROL_ADDR		0x2c
#define MII_MGMT_STATUS_ADDR		0x30
#define MII_MGMT_INDICATORS_ADDR	0x34
#define SOFT_RESET			BIT(31)

#define MII_MGMT_CONFIG_ADDR            0x20
#define MII_MGMT_COMMAND_ADDR           0x24
#define MII_MGMT_ADDRESS_ADDR           0x28
#define MII_MGMT_CONTROL_ADDR           0x2c
#define MII_MGMT_STATUS_ADDR            0x30
#define MII_MGMT_INDICATORS_ADDR        0x34

#define MIIM_COMMAND_ADDR               0x20
#define MIIM_FIELD_ADDR                 0x24
#define MIIM_CONFIGURATION_ADDR         0x28
#define MIIM_LINKFAILVECTOR_ADDR        0x2c
#define MIIM_INDICATOR_ADDR             0x30
#define MIIMRD_FIELD_ADDR               0x34

#define MDIO_CSR_OFFSET			0x5000

#define REG_ADDR_POS			0
#define REG_ADDR_LEN			5
#define PHY_ADDR_POS			8
#define PHY_ADDR_LEN			5

#define HSTMIIMWRDAT_POS		0
#define HSTMIIMWRDAT_LEN		16
#define HSTPHYADX_POS			23
#define HSTPHYADX_LEN			5
#define HSTREGADX_POS			18
#define HSTREGADX_LEN			5
#define HSTLDCMD			BIT(3)
#define HSTMIIMCMD_POS			0
#define HSTMIIMCMD_LEN			3

#define BUSY_MASK			BIT(0)
#define READ_CYCLE_MASK			BIT(0)

enum xgene_enet_cmd {
	XGENE_ENET_WR_CMD = BIT(31),
	XGENE_ENET_RD_CMD = BIT(30)
};

enum {
	MIIM_CMD_IDLE,
	MIIM_CMD_LEGACY_WRITE,
	MIIM_CMD_LEGACY_READ,
};

enum xgene_mdio_id {
	XGENE_MDIO_RGMII = 1,
	XGENE_MDIO_XFI
};

struct xgene_mdio_pdata {
	struct clk *clk;
	struct device *dev;
	void __iomem *mac_csr_addr;
	void __iomem *diag_csr_addr;
	void __iomem *mdio_csr_addr;
	struct mii_bus *mdio_bus;
	int mdio_id;
	spinlock_t mac_lock; /* mac lock */
};

/* Set the specified value into a bit-field defined by its starting position
 * and length within a single u64.
 */
static inline u64 xgene_enet_set_field_value(int pos, int len, u64 val)
{
	return (val & ((1ULL << len) - 1)) << pos;
}

#define SET_VAL(field, val) \
		xgene_enet_set_field_value(field ## _POS, field ## _LEN, val)

#define SET_BIT(field) \
		xgene_enet_set_field_value(field ## _POS, 1, 1)

/* Get the value from a bit-field defined by its starting position
 * and length within the specified u64.
 */
static inline u64 xgene_enet_get_field_value(int pos, int len, u64 src)
{
	return (src >> pos) & ((1ULL << len) - 1);
}

#define GET_VAL(field, src) \
		xgene_enet_get_field_value(field ## _POS, field ## _LEN, src)

#define GET_BIT(field, src) \
		xgene_enet_get_field_value(field ## _POS, 1, src)

u32 xgene_mdio_rd_mac(struct xgene_mdio_pdata *pdata, u32 rd_addr);
void xgene_mdio_wr_mac(struct xgene_mdio_pdata *pdata, u32 wr_addr, u32 data);
int xgene_mdio_rgmii_read(struct mii_bus *bus, int phy_id, int reg);
int xgene_mdio_rgmii_write(struct mii_bus *bus, int phy_id, int reg, u16 data);
struct phy_device *xgene_enet_phy_register(struct mii_bus *bus, int phy_addr);

#endif  /* __MDIO_XGENE_H__ */
