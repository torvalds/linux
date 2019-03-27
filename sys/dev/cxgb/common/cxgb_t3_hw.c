/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007-2009, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include <cxgb_include.h>

#undef msleep
#define msleep t3_os_sleep

/**
 *	t3_wait_op_done_val - wait until an operation is completed
 *	@adapter: the adapter performing the operation
 *	@reg: the register to check for completion
 *	@mask: a single-bit field within @reg that indicates completion
 *	@polarity: the value of the field when the operation is completed
 *	@attempts: number of check iterations
 *	@delay: delay in usecs between iterations
 *	@valp: where to store the value of the register at completion time
 *
 *	Wait until an operation is completed by checking a bit in a register
 *	up to @attempts times.  If @valp is not NULL the value of the register
 *	at the time it indicated completion is stored there.  Returns 0 if the
 *	operation completes and	-EAGAIN	otherwise.
 */
int t3_wait_op_done_val(adapter_t *adapter, int reg, u32 mask, int polarity,
			int attempts, int delay, u32 *valp)
{
	while (1) {
		u32 val = t3_read_reg(adapter, reg);

		if (!!(val & mask) == polarity) {
			if (valp)
				*valp = val;
			return 0;
		}
		if (--attempts == 0)
			return -EAGAIN;
		if (delay)
			udelay(delay);
	}
}

/**
 *	t3_write_regs - write a bunch of registers
 *	@adapter: the adapter to program
 *	@p: an array of register address/register value pairs
 *	@n: the number of address/value pairs
 *	@offset: register address offset
 *
 *	Takes an array of register address/register value pairs and writes each
 *	value to the corresponding register.  Register addresses are adjusted
 *	by the supplied offset.
 */
void t3_write_regs(adapter_t *adapter, const struct addr_val_pair *p, int n,
		   unsigned int offset)
{
	while (n--) {
		t3_write_reg(adapter, p->reg_addr + offset, p->val);
		p++;
	}
}

/**
 *	t3_set_reg_field - set a register field to a value
 *	@adapter: the adapter to program
 *	@addr: the register address
 *	@mask: specifies the portion of the register to modify
 *	@val: the new value for the register field
 *
 *	Sets a register field specified by the supplied mask to the
 *	given value.
 */
void t3_set_reg_field(adapter_t *adapter, unsigned int addr, u32 mask, u32 val)
{
	u32 v = t3_read_reg(adapter, addr) & ~mask;

	t3_write_reg(adapter, addr, v | val);
	(void) t3_read_reg(adapter, addr);      /* flush */
}

/**
 *	t3_read_indirect - read indirectly addressed registers
 *	@adap: the adapter
 *	@addr_reg: register holding the indirect address
 *	@data_reg: register holding the value of the indirect register
 *	@vals: where the read register values are stored
 *	@start_idx: index of first indirect register to read
 *	@nregs: how many indirect registers to read
 *
 *	Reads registers that are accessed indirectly through an address/data
 *	register pair.
 */
static void t3_read_indirect(adapter_t *adap, unsigned int addr_reg,
		      unsigned int data_reg, u32 *vals, unsigned int nregs,
		      unsigned int start_idx)
{
	while (nregs--) {
		t3_write_reg(adap, addr_reg, start_idx);
		*vals++ = t3_read_reg(adap, data_reg);
		start_idx++;
	}
}

/**
 *	t3_mc7_bd_read - read from MC7 through backdoor accesses
 *	@mc7: identifies MC7 to read from
 *	@start: index of first 64-bit word to read
 *	@n: number of 64-bit words to read
 *	@buf: where to store the read result
 *
 *	Read n 64-bit words from MC7 starting at word start, using backdoor
 *	accesses.
 */
int t3_mc7_bd_read(struct mc7 *mc7, unsigned int start, unsigned int n,
                   u64 *buf)
{
	static int shift[] = { 0, 0, 16, 24 };
	static int step[]  = { 0, 32, 16, 8 };

	unsigned int size64 = mc7->size / 8;  /* # of 64-bit words */
	adapter_t *adap = mc7->adapter;

	if (start >= size64 || start + n > size64)
		return -EINVAL;

	start *= (8 << mc7->width);
	while (n--) {
		int i;
		u64 val64 = 0;

		for (i = (1 << mc7->width) - 1; i >= 0; --i) {
			int attempts = 10;
			u32 val;

			t3_write_reg(adap, mc7->offset + A_MC7_BD_ADDR,
				       start);
			t3_write_reg(adap, mc7->offset + A_MC7_BD_OP, 0);
			val = t3_read_reg(adap, mc7->offset + A_MC7_BD_OP);
			while ((val & F_BUSY) && attempts--)
				val = t3_read_reg(adap,
						  mc7->offset + A_MC7_BD_OP);
			if (val & F_BUSY)
				return -EIO;

			val = t3_read_reg(adap, mc7->offset + A_MC7_BD_DATA1);
			if (mc7->width == 0) {
				val64 = t3_read_reg(adap,
						mc7->offset + A_MC7_BD_DATA0);
				val64 |= (u64)val << 32;
			} else {
				if (mc7->width > 1)
					val >>= shift[mc7->width];
				val64 |= (u64)val << (step[mc7->width] * i);
			}
			start += 8;
		}
		*buf++ = val64;
	}
	return 0;
}

/*
 * Low-level I2C read and write routines.  These simply read and write a
 * single byte with the option of indicating a "continue" if another operation
 * is to be chained.  Generally most code will use higher-level routines to
 * read and write to I2C Slave Devices.
 */
#define I2C_ATTEMPTS 100

/*
 * Read an 8-bit value from the I2C bus.  If the "chained" parameter is
 * non-zero then a STOP bit will not be written after the read command.  On
 * error (the read timed out, etc.), a negative errno will be returned (e.g.
 * -EAGAIN, etc.).  On success, the 8-bit value read from the I2C bus is
 * stored into the buffer *valp and the value of the I2C ACK bit is returned
 * as a 0/1 value.
 */
int t3_i2c_read8(adapter_t *adapter, int chained, u8 *valp)
{
	int ret;
	u32 opval;
	MDIO_LOCK(adapter);
	t3_write_reg(adapter, A_I2C_OP,
		     F_I2C_READ | (chained ? F_I2C_CONT : 0));
	ret = t3_wait_op_done_val(adapter, A_I2C_OP, F_I2C_BUSY, 0,
				  I2C_ATTEMPTS, 10, &opval);
	if (ret >= 0) {
		ret = ((opval & F_I2C_ACK) == F_I2C_ACK);
		*valp = G_I2C_DATA(t3_read_reg(adapter, A_I2C_DATA));
	}
	MDIO_UNLOCK(adapter);
	return ret;
}

/*
 * Write an 8-bit value to the I2C bus.  If the "chained" parameter is
 * non-zero, then a STOP bit will not be written after the write command.  On
 * error (the write timed out, etc.), a negative errno will be returned (e.g.
 * -EAGAIN, etc.).  On success, the value of the I2C ACK bit is returned as a
 * 0/1 value.
 */
int t3_i2c_write8(adapter_t *adapter, int chained, u8 val)
{
	int ret;
	u32 opval;
	MDIO_LOCK(adapter);
	t3_write_reg(adapter, A_I2C_DATA, V_I2C_DATA(val));
	t3_write_reg(adapter, A_I2C_OP,
		     F_I2C_WRITE | (chained ? F_I2C_CONT : 0));
	ret = t3_wait_op_done_val(adapter, A_I2C_OP, F_I2C_BUSY, 0,
				  I2C_ATTEMPTS, 10, &opval);
	if (ret >= 0)
		ret = ((opval & F_I2C_ACK) == F_I2C_ACK);
	MDIO_UNLOCK(adapter);
	return ret;
}

/*
 * Initialize MI1.
 */
static void mi1_init(adapter_t *adap, const struct adapter_info *ai)
{
        u32 clkdiv = adap->params.vpd.cclk / (2 * adap->params.vpd.mdc) - 1;
        u32 val = F_PREEN | V_CLKDIV(clkdiv);

        t3_write_reg(adap, A_MI1_CFG, val);
}

#define MDIO_ATTEMPTS 20

/*
 * MI1 read/write operations for clause 22 PHYs.
 */
int t3_mi1_read(adapter_t *adapter, int phy_addr, int mmd_addr,
		int reg_addr, unsigned int *valp)
{
	int ret;
	u32 addr = V_REGADDR(reg_addr) | V_PHYADDR(phy_addr);

	if (mmd_addr)
		return -EINVAL;

	MDIO_LOCK(adapter);
	t3_set_reg_field(adapter, A_MI1_CFG, V_ST(M_ST), V_ST(1));
	t3_write_reg(adapter, A_MI1_ADDR, addr);
	t3_write_reg(adapter, A_MI1_OP, V_MDI_OP(2));
	ret = t3_wait_op_done(adapter, A_MI1_OP, F_BUSY, 0, MDIO_ATTEMPTS, 10);
	if (!ret)
		*valp = t3_read_reg(adapter, A_MI1_DATA);
	MDIO_UNLOCK(adapter);
	return ret;
}

int t3_mi1_write(adapter_t *adapter, int phy_addr, int mmd_addr,
		 int reg_addr, unsigned int val)
{
	int ret;
	u32 addr = V_REGADDR(reg_addr) | V_PHYADDR(phy_addr);

	if (mmd_addr)
		return -EINVAL;

	MDIO_LOCK(adapter);
	t3_set_reg_field(adapter, A_MI1_CFG, V_ST(M_ST), V_ST(1));
	t3_write_reg(adapter, A_MI1_ADDR, addr);
	t3_write_reg(adapter, A_MI1_DATA, val);
	t3_write_reg(adapter, A_MI1_OP, V_MDI_OP(1));
	ret = t3_wait_op_done(adapter, A_MI1_OP, F_BUSY, 0, MDIO_ATTEMPTS, 10);
	MDIO_UNLOCK(adapter);
	return ret;
}

static struct mdio_ops mi1_mdio_ops = {
	t3_mi1_read,
	t3_mi1_write
};

/*
 * MI1 read/write operations for clause 45 PHYs.
 */
static int mi1_ext_read(adapter_t *adapter, int phy_addr, int mmd_addr,
			int reg_addr, unsigned int *valp)
{
	int ret;
	u32 addr = V_REGADDR(mmd_addr) | V_PHYADDR(phy_addr);

	MDIO_LOCK(adapter);
	t3_set_reg_field(adapter, A_MI1_CFG, V_ST(M_ST), 0);
	t3_write_reg(adapter, A_MI1_ADDR, addr);
	t3_write_reg(adapter, A_MI1_DATA, reg_addr);
	t3_write_reg(adapter, A_MI1_OP, V_MDI_OP(0));
	ret = t3_wait_op_done(adapter, A_MI1_OP, F_BUSY, 0, MDIO_ATTEMPTS, 10);
	if (!ret) {
		t3_write_reg(adapter, A_MI1_OP, V_MDI_OP(3));
		ret = t3_wait_op_done(adapter, A_MI1_OP, F_BUSY, 0,
				      MDIO_ATTEMPTS, 10);
		if (!ret)
			*valp = t3_read_reg(adapter, A_MI1_DATA);
	}
	MDIO_UNLOCK(adapter);
	return ret;
}

static int mi1_ext_write(adapter_t *adapter, int phy_addr, int mmd_addr,
			 int reg_addr, unsigned int val)
{
	int ret;
	u32 addr = V_REGADDR(mmd_addr) | V_PHYADDR(phy_addr);

	MDIO_LOCK(adapter);
	t3_set_reg_field(adapter, A_MI1_CFG, V_ST(M_ST), 0);
	t3_write_reg(adapter, A_MI1_ADDR, addr);
	t3_write_reg(adapter, A_MI1_DATA, reg_addr);
	t3_write_reg(adapter, A_MI1_OP, V_MDI_OP(0));
	ret = t3_wait_op_done(adapter, A_MI1_OP, F_BUSY, 0, MDIO_ATTEMPTS, 10);
	if (!ret) {
		t3_write_reg(adapter, A_MI1_DATA, val);
		t3_write_reg(adapter, A_MI1_OP, V_MDI_OP(1));
		ret = t3_wait_op_done(adapter, A_MI1_OP, F_BUSY, 0,
				      MDIO_ATTEMPTS, 10);
	}
	MDIO_UNLOCK(adapter);
	return ret;
}

static struct mdio_ops mi1_mdio_ext_ops = {
	mi1_ext_read,
	mi1_ext_write
};

/**
 *	t3_mdio_change_bits - modify the value of a PHY register
 *	@phy: the PHY to operate on
 *	@mmd: the device address
 *	@reg: the register address
 *	@clear: what part of the register value to mask off
 *	@set: what part of the register value to set
 *
 *	Changes the value of a PHY register by applying a mask to its current
 *	value and ORing the result with a new value.
 */
int t3_mdio_change_bits(struct cphy *phy, int mmd, int reg, unsigned int clear,
			unsigned int set)
{
	int ret;
	unsigned int val;

	ret = mdio_read(phy, mmd, reg, &val);
	if (!ret) {
		val &= ~clear;
		ret = mdio_write(phy, mmd, reg, val | set);
	}
	return ret;
}

/**
 *	t3_phy_reset - reset a PHY block
 *	@phy: the PHY to operate on
 *	@mmd: the device address of the PHY block to reset
 *	@wait: how long to wait for the reset to complete in 1ms increments
 *
 *	Resets a PHY block and optionally waits for the reset to complete.
 *	@mmd should be 0 for 10/100/1000 PHYs and the device address to reset
 *	for 10G PHYs.
 */
int t3_phy_reset(struct cphy *phy, int mmd, int wait)
{
	int err;
	unsigned int ctl;

	err = t3_mdio_change_bits(phy, mmd, MII_BMCR, BMCR_PDOWN, BMCR_RESET);
	if (err || !wait)
		return err;

	do {
		err = mdio_read(phy, mmd, MII_BMCR, &ctl);
		if (err)
			return err;
		ctl &= BMCR_RESET;
		if (ctl)
			msleep(1);
	} while (ctl && --wait);

	return ctl ? -1 : 0;
}

/**
 *	t3_phy_advertise - set the PHY advertisement registers for autoneg
 *	@phy: the PHY to operate on
 *	@advert: bitmap of capabilities the PHY should advertise
 *
 *	Sets a 10/100/1000 PHY's advertisement registers to advertise the
 *	requested capabilities.
 */
int t3_phy_advertise(struct cphy *phy, unsigned int advert)
{
	int err;
	unsigned int val = 0;

	err = mdio_read(phy, 0, MII_CTRL1000, &val);
	if (err)
		return err;

	val &= ~(ADVERTISE_1000HALF | ADVERTISE_1000FULL);
	if (advert & ADVERTISED_1000baseT_Half)
		val |= ADVERTISE_1000HALF;
	if (advert & ADVERTISED_1000baseT_Full)
		val |= ADVERTISE_1000FULL;

	err = mdio_write(phy, 0, MII_CTRL1000, val);
	if (err)
		return err;

	val = 1;
	if (advert & ADVERTISED_10baseT_Half)
		val |= ADVERTISE_10HALF;
	if (advert & ADVERTISED_10baseT_Full)
		val |= ADVERTISE_10FULL;
	if (advert & ADVERTISED_100baseT_Half)
		val |= ADVERTISE_100HALF;
	if (advert & ADVERTISED_100baseT_Full)
		val |= ADVERTISE_100FULL;
	if (advert & ADVERTISED_Pause)
		val |= ADVERTISE_PAUSE_CAP;
	if (advert & ADVERTISED_Asym_Pause)
		val |= ADVERTISE_PAUSE_ASYM;
	return mdio_write(phy, 0, MII_ADVERTISE, val);
}

/**
 *	t3_phy_advertise_fiber - set fiber PHY advertisement register
 *	@phy: the PHY to operate on
 *	@advert: bitmap of capabilities the PHY should advertise
 *
 *	Sets a fiber PHY's advertisement register to advertise the
 *	requested capabilities.
 */
int t3_phy_advertise_fiber(struct cphy *phy, unsigned int advert)
{
	unsigned int val = 0;

	if (advert & ADVERTISED_1000baseT_Half)
		val |= ADVERTISE_1000XHALF;
	if (advert & ADVERTISED_1000baseT_Full)
		val |= ADVERTISE_1000XFULL;
	if (advert & ADVERTISED_Pause)
		val |= ADVERTISE_1000XPAUSE;
	if (advert & ADVERTISED_Asym_Pause)
		val |= ADVERTISE_1000XPSE_ASYM;
	return mdio_write(phy, 0, MII_ADVERTISE, val);
}

/**
 *	t3_set_phy_speed_duplex - force PHY speed and duplex
 *	@phy: the PHY to operate on
 *	@speed: requested PHY speed
 *	@duplex: requested PHY duplex
 *
 *	Force a 10/100/1000 PHY's speed and duplex.  This also disables
 *	auto-negotiation except for GigE, where auto-negotiation is mandatory.
 */
int t3_set_phy_speed_duplex(struct cphy *phy, int speed, int duplex)
{
	int err;
	unsigned int ctl;

	err = mdio_read(phy, 0, MII_BMCR, &ctl);
	if (err)
		return err;

	if (speed >= 0) {
		ctl &= ~(BMCR_SPEED100 | BMCR_SPEED1000 | BMCR_ANENABLE);
		if (speed == SPEED_100)
			ctl |= BMCR_SPEED100;
		else if (speed == SPEED_1000)
			ctl |= BMCR_SPEED1000;
	}
	if (duplex >= 0) {
		ctl &= ~(BMCR_FULLDPLX | BMCR_ANENABLE);
		if (duplex == DUPLEX_FULL)
			ctl |= BMCR_FULLDPLX;
	}
	if (ctl & BMCR_SPEED1000)  /* auto-negotiation required for GigE */
		ctl |= BMCR_ANENABLE;
	return mdio_write(phy, 0, MII_BMCR, ctl);
}

int t3_phy_lasi_intr_enable(struct cphy *phy)
{
	return mdio_write(phy, MDIO_DEV_PMA_PMD, LASI_CTRL, 1);
}

int t3_phy_lasi_intr_disable(struct cphy *phy)
{
	return mdio_write(phy, MDIO_DEV_PMA_PMD, LASI_CTRL, 0);
}

int t3_phy_lasi_intr_clear(struct cphy *phy)
{
	u32 val;

	return mdio_read(phy, MDIO_DEV_PMA_PMD, LASI_STAT, &val);
}

int t3_phy_lasi_intr_handler(struct cphy *phy)
{
	unsigned int status;
	int err = mdio_read(phy, MDIO_DEV_PMA_PMD, LASI_STAT, &status);

	if (err)
		return err;
	return (status & 1) ?  cphy_cause_link_change : 0;
}

static struct adapter_info t3_adap_info[] = {
	{ 1, 1, 0,
	  F_GPIO2_OEN | F_GPIO4_OEN |
	  F_GPIO2_OUT_VAL | F_GPIO4_OUT_VAL, { S_GPIO3, S_GPIO5 }, 0,
	  &mi1_mdio_ops, "Chelsio PE9000" },
	{ 1, 1, 0,
	  F_GPIO2_OEN | F_GPIO4_OEN |
	  F_GPIO2_OUT_VAL | F_GPIO4_OUT_VAL, { S_GPIO3, S_GPIO5 }, 0,
	  &mi1_mdio_ops, "Chelsio T302" },
	{ 1, 0, 0,
	  F_GPIO1_OEN | F_GPIO6_OEN | F_GPIO7_OEN | F_GPIO10_OEN |
	  F_GPIO11_OEN | F_GPIO1_OUT_VAL | F_GPIO6_OUT_VAL | F_GPIO10_OUT_VAL,
	  { 0 }, SUPPORTED_10000baseT_Full | SUPPORTED_AUI,
	  &mi1_mdio_ext_ops, "Chelsio T310" },
	{ 1, 1, 0,
	  F_GPIO1_OEN | F_GPIO2_OEN | F_GPIO4_OEN | F_GPIO5_OEN | F_GPIO6_OEN |
	  F_GPIO7_OEN | F_GPIO10_OEN | F_GPIO11_OEN | F_GPIO1_OUT_VAL |
	  F_GPIO5_OUT_VAL | F_GPIO6_OUT_VAL | F_GPIO10_OUT_VAL,
	  { S_GPIO9, S_GPIO3 }, SUPPORTED_10000baseT_Full | SUPPORTED_AUI,
	  &mi1_mdio_ext_ops, "Chelsio T320" },
	{ 4, 0, 0,
	  F_GPIO5_OEN | F_GPIO6_OEN | F_GPIO7_OEN | F_GPIO5_OUT_VAL |
	  F_GPIO6_OUT_VAL | F_GPIO7_OUT_VAL,
	  { S_GPIO1, S_GPIO2, S_GPIO3, S_GPIO4 }, SUPPORTED_AUI,
	  &mi1_mdio_ops, "Chelsio T304" },
	{ 0 },
	{ 1, 0, 0,
	  F_GPIO1_OEN | F_GPIO2_OEN | F_GPIO4_OEN | F_GPIO6_OEN | F_GPIO7_OEN |
	  F_GPIO10_OEN | F_GPIO1_OUT_VAL | F_GPIO6_OUT_VAL | F_GPIO10_OUT_VAL,
	  { S_GPIO9 }, SUPPORTED_10000baseT_Full | SUPPORTED_AUI,
	  &mi1_mdio_ext_ops, "Chelsio T310" },
	{ 1, 0, 0,
	  F_GPIO1_OEN | F_GPIO6_OEN | F_GPIO7_OEN | 
	  F_GPIO1_OUT_VAL | F_GPIO6_OUT_VAL,
	  { S_GPIO9 }, SUPPORTED_10000baseT_Full | SUPPORTED_AUI,
	  &mi1_mdio_ext_ops, "Chelsio N320E-G2" },
};

/*
 * Return the adapter_info structure with a given index.  Out-of-range indices
 * return NULL.
 */
const struct adapter_info *t3_get_adapter_info(unsigned int id)
{
	return id < ARRAY_SIZE(t3_adap_info) ? &t3_adap_info[id] : NULL;
}

struct port_type_info {
	int (*phy_prep)(pinfo_t *pinfo, int phy_addr,
			const struct mdio_ops *ops);
};

static struct port_type_info port_types[] = {
	{ NULL },
	{ t3_ael1002_phy_prep },
	{ t3_vsc8211_phy_prep },
	{ t3_mv88e1xxx_phy_prep },
	{ t3_xaui_direct_phy_prep },
	{ t3_ael2005_phy_prep },
	{ t3_qt2045_phy_prep },
	{ t3_ael1006_phy_prep },
	{ t3_tn1010_phy_prep },
	{ t3_aq100x_phy_prep },
	{ t3_ael2020_phy_prep },
};

#define VPD_ENTRY(name, len) \
	u8 name##_kword[2]; u8 name##_len; u8 name##_data[len]

/*
 * Partial EEPROM Vital Product Data structure.  Includes only the ID and
 * VPD-R sections.
 */
struct t3_vpd {
	u8  id_tag;
	u8  id_len[2];
	u8  id_data[16];
	u8  vpdr_tag;
	u8  vpdr_len[2];
	VPD_ENTRY(pn, 16);                     /* part number */
	VPD_ENTRY(ec, ECNUM_LEN);              /* EC level */
	VPD_ENTRY(sn, SERNUM_LEN);             /* serial number */
	VPD_ENTRY(na, 12);                     /* MAC address base */
	VPD_ENTRY(cclk, 6);                    /* core clock */
	VPD_ENTRY(mclk, 6);                    /* mem clock */
	VPD_ENTRY(uclk, 6);                    /* uP clk */
	VPD_ENTRY(mdc, 6);                     /* MDIO clk */
	VPD_ENTRY(mt, 2);                      /* mem timing */
	VPD_ENTRY(xaui0cfg, 6);                /* XAUI0 config */
	VPD_ENTRY(xaui1cfg, 6);                /* XAUI1 config */
	VPD_ENTRY(port0, 2);                   /* PHY0 complex */
	VPD_ENTRY(port1, 2);                   /* PHY1 complex */
	VPD_ENTRY(port2, 2);                   /* PHY2 complex */
	VPD_ENTRY(port3, 2);                   /* PHY3 complex */
	VPD_ENTRY(rv, 1);                      /* csum */
	u32 pad;                  /* for multiple-of-4 sizing and alignment */
};

#define EEPROM_MAX_POLL   40
#define EEPROM_STAT_ADDR  0x4000
#define VPD_BASE          0xc00

/**
 *	t3_seeprom_read - read a VPD EEPROM location
 *	@adapter: adapter to read
 *	@addr: EEPROM address
 *	@data: where to store the read data
 *
 *	Read a 32-bit word from a location in VPD EEPROM using the card's PCI
 *	VPD ROM capability.  A zero is written to the flag bit when the
 *	address is written to the control register.  The hardware device will
 *	set the flag to 1 when 4 bytes have been read into the data register.
 */
int t3_seeprom_read(adapter_t *adapter, u32 addr, u32 *data)
{
	u16 val;
	int attempts = EEPROM_MAX_POLL;
	unsigned int base = adapter->params.pci.vpd_cap_addr;

	if ((addr >= EEPROMSIZE && addr != EEPROM_STAT_ADDR) || (addr & 3))
		return -EINVAL;

	t3_os_pci_write_config_2(adapter, base + PCI_VPD_ADDR, (u16)addr);
	do {
		udelay(10);
		t3_os_pci_read_config_2(adapter, base + PCI_VPD_ADDR, &val);
	} while (!(val & PCI_VPD_ADDR_F) && --attempts);

	if (!(val & PCI_VPD_ADDR_F)) {
		CH_ERR(adapter, "reading EEPROM address 0x%x failed\n", addr);
		return -EIO;
	}
	t3_os_pci_read_config_4(adapter, base + PCI_VPD_DATA, data);
	*data = le32_to_cpu(*data);
	return 0;
}

/**
 *	t3_seeprom_write - write a VPD EEPROM location
 *	@adapter: adapter to write
 *	@addr: EEPROM address
 *	@data: value to write
 *
 *	Write a 32-bit word to a location in VPD EEPROM using the card's PCI
 *	VPD ROM capability.
 */
int t3_seeprom_write(adapter_t *adapter, u32 addr, u32 data)
{
	u16 val;
	int attempts = EEPROM_MAX_POLL;
	unsigned int base = adapter->params.pci.vpd_cap_addr;

	if ((addr >= EEPROMSIZE && addr != EEPROM_STAT_ADDR) || (addr & 3))
		return -EINVAL;

	t3_os_pci_write_config_4(adapter, base + PCI_VPD_DATA,
				 cpu_to_le32(data));
	t3_os_pci_write_config_2(adapter, base + PCI_VPD_ADDR,
				 (u16)addr | PCI_VPD_ADDR_F);
	do {
		msleep(1);
		t3_os_pci_read_config_2(adapter, base + PCI_VPD_ADDR, &val);
	} while ((val & PCI_VPD_ADDR_F) && --attempts);

	if (val & PCI_VPD_ADDR_F) {
		CH_ERR(adapter, "write to EEPROM address 0x%x failed\n", addr);
		return -EIO;
	}
	return 0;
}

/**
 *	t3_seeprom_wp - enable/disable EEPROM write protection
 *	@adapter: the adapter
 *	@enable: 1 to enable write protection, 0 to disable it
 *
 *	Enables or disables write protection on the serial EEPROM.
 */
int t3_seeprom_wp(adapter_t *adapter, int enable)
{
	return t3_seeprom_write(adapter, EEPROM_STAT_ADDR, enable ? 0xc : 0);
}

/*
 * Convert a character holding a hex digit to a number.
 */
static unsigned int hex2int(unsigned char c)
{
	return isdigit(c) ? c - '0' : toupper(c) - 'A' + 10;
}

/**
 * 	get_desc_len - get the length of a vpd descriptor.
 *	@adapter: the adapter
 *	@offset: first byte offset of the vpd descriptor
 *
 *	Retrieves the length of the small/large resource
 *	data type starting at offset.
 */
static int get_desc_len(adapter_t *adapter, u32 offset)
{
	u32 read_offset, tmp, shift, len = 0;
	u8 tag, buf[8];
	int ret;

	read_offset = offset & 0xfffffffc;
	shift = offset & 0x03;

	ret = t3_seeprom_read(adapter, read_offset, &tmp);
	if (ret < 0)
		return ret;

	*((u32 *)buf) = cpu_to_le32(tmp);

	tag = buf[shift];
	if (tag & 0x80) {
		ret = t3_seeprom_read(adapter, read_offset + 4, &tmp);
		if (ret < 0)
			return ret;

		*((u32 *)(&buf[4])) = cpu_to_le32(tmp);
		len = (buf[shift + 1] & 0xff) +
		      ((buf[shift+2] << 8) & 0xff00) + 3;
	} else
		len = (tag & 0x07) + 1;

	return len;
}

/**
 *	is_end_tag - Check if a vpd tag is the end tag.
 *	@adapter: the adapter
 *	@offset: first byte offset of the tag
 *
 *	Checks if the tag located at offset is the end tag.
 */
static int is_end_tag(adapter_t * adapter, u32 offset)
{
	u32 read_offset, shift, ret, tmp;
	u8 buf[4];

	read_offset = offset & 0xfffffffc;
	shift = offset & 0x03;

	ret = t3_seeprom_read(adapter, read_offset, &tmp);
	if (ret)
		return ret;
	*((u32 *)buf) = cpu_to_le32(tmp);

	if (buf[shift] == 0x78)
		return 1;
	else
		return 0;
}

/**
 *	t3_get_vpd_len - computes the length of a vpd structure
 *	@adapter: the adapter
 *	@vpd: contains the offset of first byte of vpd
 *
 *	Computes the lentgh of the vpd structure starting at vpd->offset.
 */

int t3_get_vpd_len(adapter_t * adapter, struct generic_vpd *vpd)
{
	u32 len=0, offset;
	int inc, ret;

	offset = vpd->offset;

	while (offset < (vpd->offset + MAX_VPD_BYTES)) {
		ret = is_end_tag(adapter, offset);
		if (ret < 0)
			return ret;
		else if (ret == 1)
			break;

		inc = get_desc_len(adapter, offset);
		if (inc < 0)
			return inc;
		len += inc;
		offset += inc;
	}
	return (len + 1);
}

/**
 *	t3_read_vpd - reads the stream of bytes containing a vpd structure
 *	@adapter: the adapter
 *	@vpd: contains a buffer that would hold the stream of bytes
 *
 *	Reads the vpd structure starting at vpd->offset into vpd->data,
 *	the length of the byte stream to read is vpd->len.
 */

int t3_read_vpd(adapter_t *adapter, struct generic_vpd *vpd)
{
	u32 i, ret;

	for (i = 0; i < vpd->len; i += 4) {
		ret = t3_seeprom_read(adapter, vpd->offset + i,
				      (u32 *) &(vpd->data[i]));
		if (ret)
			return ret;
	}

	return 0;
}


/**
 *	get_vpd_params - read VPD parameters from VPD EEPROM
 *	@adapter: adapter to read
 *	@p: where to store the parameters
 *
 *	Reads card parameters stored in VPD EEPROM.
 */
static int get_vpd_params(adapter_t *adapter, struct vpd_params *p)
{
	int i, addr, ret;
	struct t3_vpd vpd;

	/*
	 * Card information is normally at VPD_BASE but some early cards had
	 * it at 0.
	 */
	ret = t3_seeprom_read(adapter, VPD_BASE, (u32 *)&vpd);
	if (ret)
		return ret;
	addr = vpd.id_tag == 0x82 ? VPD_BASE : 0;

	for (i = 0; i < sizeof(vpd); i += 4) {
		ret = t3_seeprom_read(adapter, addr + i,
				      (u32 *)((u8 *)&vpd + i));
		if (ret)
			return ret;
	}

	p->cclk = simple_strtoul(vpd.cclk_data, NULL, 10);
	p->mclk = simple_strtoul(vpd.mclk_data, NULL, 10);
	p->uclk = simple_strtoul(vpd.uclk_data, NULL, 10);
	p->mdc = simple_strtoul(vpd.mdc_data, NULL, 10);
	p->mem_timing = simple_strtoul(vpd.mt_data, NULL, 10);
	memcpy(p->sn, vpd.sn_data, SERNUM_LEN);
	memcpy(p->ec, vpd.ec_data, ECNUM_LEN);

	/* Old eeproms didn't have port information */
	if (adapter->params.rev == 0 && !vpd.port0_data[0]) {
		p->port_type[0] = uses_xaui(adapter) ? 1 : 2;
		p->port_type[1] = uses_xaui(adapter) ? 6 : 2;
	} else {
		p->port_type[0] = (u8)hex2int(vpd.port0_data[0]);
		p->port_type[1] = (u8)hex2int(vpd.port1_data[0]);
		p->port_type[2] = (u8)hex2int(vpd.port2_data[0]);
		p->port_type[3] = (u8)hex2int(vpd.port3_data[0]);
		p->xauicfg[0] = simple_strtoul(vpd.xaui0cfg_data, NULL, 16);
		p->xauicfg[1] = simple_strtoul(vpd.xaui1cfg_data, NULL, 16);
	}

	for (i = 0; i < 6; i++)
		p->eth_base[i] = hex2int(vpd.na_data[2 * i]) * 16 +
				 hex2int(vpd.na_data[2 * i + 1]);
	return 0;
}

/* BIOS boot header */
typedef struct boot_header_s {
	u8	signature[2];	/* signature */
	u8	length;		/* image length (include header) */
	u8	offset[4];	/* initialization vector */
	u8	reserved[19];	/* reserved */
	u8	exheader[2];	/* offset to expansion header */
} boot_header_t;

/* serial flash and firmware constants */
enum {
	SF_ATTEMPTS = 5,           /* max retries for SF1 operations */
	SF_SEC_SIZE = 64 * 1024,   /* serial flash sector size */
	SF_SIZE = SF_SEC_SIZE * 8, /* serial flash size */

	/* flash command opcodes */
	SF_PROG_PAGE    = 2,       /* program page */
	SF_WR_DISABLE   = 4,       /* disable writes */
	SF_RD_STATUS    = 5,       /* read status register */
	SF_WR_ENABLE    = 6,       /* enable writes */
	SF_RD_DATA_FAST = 0xb,     /* read flash */
	SF_ERASE_SECTOR = 0xd8,    /* erase sector */

	FW_FLASH_BOOT_ADDR = 0x70000, /* start address of FW in flash */
	FW_VERS_ADDR = 0x7fffc,    /* flash address holding FW version */
	FW_VERS_ADDR_PRE8 = 0x77ffc,/* flash address holding FW version pre8 */
	FW_MIN_SIZE = 8,           /* at least version and csum */
	FW_MAX_SIZE = FW_VERS_ADDR - FW_FLASH_BOOT_ADDR,
	FW_MAX_SIZE_PRE8 = FW_VERS_ADDR_PRE8 - FW_FLASH_BOOT_ADDR,

	BOOT_FLASH_BOOT_ADDR = 0x0,/* start address of boot image in flash */
	BOOT_SIGNATURE = 0xaa55,   /* signature of BIOS boot ROM */
	BOOT_SIZE_INC = 512,       /* image size measured in 512B chunks */
	BOOT_MIN_SIZE = sizeof(boot_header_t), /* at least basic header */
	BOOT_MAX_SIZE = 1024*BOOT_SIZE_INC /* 1 byte * length increment  */
};

/**
 *	sf1_read - read data from the serial flash
 *	@adapter: the adapter
 *	@byte_cnt: number of bytes to read
 *	@cont: whether another operation will be chained
 *	@valp: where to store the read data
 *
 *	Reads up to 4 bytes of data from the serial flash.  The location of
 *	the read needs to be specified prior to calling this by issuing the
 *	appropriate commands to the serial flash.
 */
static int sf1_read(adapter_t *adapter, unsigned int byte_cnt, int cont,
		    u32 *valp)
{
	int ret;

	if (!byte_cnt || byte_cnt > 4)
		return -EINVAL;
	if (t3_read_reg(adapter, A_SF_OP) & F_BUSY)
		return -EBUSY;
	t3_write_reg(adapter, A_SF_OP, V_CONT(cont) | V_BYTECNT(byte_cnt - 1));
	ret = t3_wait_op_done(adapter, A_SF_OP, F_BUSY, 0, SF_ATTEMPTS, 10);
	if (!ret)
		*valp = t3_read_reg(adapter, A_SF_DATA);
	return ret;
}

/**
 *	sf1_write - write data to the serial flash
 *	@adapter: the adapter
 *	@byte_cnt: number of bytes to write
 *	@cont: whether another operation will be chained
 *	@val: value to write
 *
 *	Writes up to 4 bytes of data to the serial flash.  The location of
 *	the write needs to be specified prior to calling this by issuing the
 *	appropriate commands to the serial flash.
 */
static int sf1_write(adapter_t *adapter, unsigned int byte_cnt, int cont,
		     u32 val)
{
	if (!byte_cnt || byte_cnt > 4)
		return -EINVAL;
	if (t3_read_reg(adapter, A_SF_OP) & F_BUSY)
		return -EBUSY;
	t3_write_reg(adapter, A_SF_DATA, val);
	t3_write_reg(adapter, A_SF_OP,
		     V_CONT(cont) | V_BYTECNT(byte_cnt - 1) | V_OP(1));
	return t3_wait_op_done(adapter, A_SF_OP, F_BUSY, 0, SF_ATTEMPTS, 10);
}

/**
 *	flash_wait_op - wait for a flash operation to complete
 *	@adapter: the adapter
 *	@attempts: max number of polls of the status register
 *	@delay: delay between polls in ms
 *
 *	Wait for a flash operation to complete by polling the status register.
 */
static int flash_wait_op(adapter_t *adapter, int attempts, int delay)
{
	int ret;
	u32 status;

	while (1) {
		if ((ret = sf1_write(adapter, 1, 1, SF_RD_STATUS)) != 0 ||
		    (ret = sf1_read(adapter, 1, 0, &status)) != 0)
			return ret;
		if (!(status & 1))
			return 0;
		if (--attempts == 0)
			return -EAGAIN;
		if (delay)
			msleep(delay);
	}
}

/**
 *	t3_read_flash - read words from serial flash
 *	@adapter: the adapter
 *	@addr: the start address for the read
 *	@nwords: how many 32-bit words to read
 *	@data: where to store the read data
 *	@byte_oriented: whether to store data as bytes or as words
 *
 *	Read the specified number of 32-bit words from the serial flash.
 *	If @byte_oriented is set the read data is stored as a byte array
 *	(i.e., big-endian), otherwise as 32-bit words in the platform's
 *	natural endianness.
 */
int t3_read_flash(adapter_t *adapter, unsigned int addr, unsigned int nwords,
		  u32 *data, int byte_oriented)
{
	int ret;

	if (addr + nwords * sizeof(u32) > SF_SIZE || (addr & 3))
		return -EINVAL;

	addr = swab32(addr) | SF_RD_DATA_FAST;

	if ((ret = sf1_write(adapter, 4, 1, addr)) != 0 ||
	    (ret = sf1_read(adapter, 1, 1, data)) != 0)
		return ret;

	for ( ; nwords; nwords--, data++) {
		ret = sf1_read(adapter, 4, nwords > 1, data);
		if (ret)
			return ret;
		if (byte_oriented)
			*data = htonl(*data);
	}
	return 0;
}

/**
 *	t3_write_flash - write up to a page of data to the serial flash
 *	@adapter: the adapter
 *	@addr: the start address to write
 *	@n: length of data to write
 *	@data: the data to write
 *	@byte_oriented: whether to store data as bytes or as words
 *
 *	Writes up to a page of data (256 bytes) to the serial flash starting
 *	at the given address.
 *	If @byte_oriented is set the write data is stored as a 32-bit
 *	big-endian array, otherwise in the processor's native endianness.
 *
 */
static int t3_write_flash(adapter_t *adapter, unsigned int addr,
			  unsigned int n, const u8 *data,
			  int byte_oriented)
{
	int ret;
	u32 buf[64];
	unsigned int c, left, val, offset = addr & 0xff;

	if (addr + n > SF_SIZE || offset + n > 256)
		return -EINVAL;

	val = swab32(addr) | SF_PROG_PAGE;

	if ((ret = sf1_write(adapter, 1, 0, SF_WR_ENABLE)) != 0 ||
	    (ret = sf1_write(adapter, 4, 1, val)) != 0)
		return ret;

	for (left = n; left; left -= c) {
		c = min(left, 4U);
		val = *(const u32*)data;
		data += c;
		if (byte_oriented)
			val = htonl(val);

		ret = sf1_write(adapter, c, c != left, val);
		if (ret)
			return ret;
	}
	if ((ret = flash_wait_op(adapter, 5, 1)) != 0)
		return ret;

	/* Read the page to verify the write succeeded */
	ret = t3_read_flash(adapter, addr & ~0xff, ARRAY_SIZE(buf), buf,
			    byte_oriented);
	if (ret)
		return ret;

	if (memcmp(data - n, (u8 *)buf + offset, n))
		return -EIO;
	return 0;
}

/**
 *	t3_get_tp_version - read the tp sram version
 *	@adapter: the adapter
 *	@vers: where to place the version
 *
 *	Reads the protocol sram version from sram.
 */
int t3_get_tp_version(adapter_t *adapter, u32 *vers)
{
	int ret;

	/* Get version loaded in SRAM */
	t3_write_reg(adapter, A_TP_EMBED_OP_FIELD0, 0);
	ret = t3_wait_op_done(adapter, A_TP_EMBED_OP_FIELD0,
			      1, 1, 5, 1);
	if (ret)
		return ret;

	*vers = t3_read_reg(adapter, A_TP_EMBED_OP_FIELD1);

	return 0;
}

/**
 *	t3_check_tpsram_version - read the tp sram version
 *	@adapter: the adapter
 *
 */
int t3_check_tpsram_version(adapter_t *adapter)
{
	int ret;
	u32 vers;
	unsigned int major, minor;

	if (adapter->params.rev == T3_REV_A)
		return 0;


	ret = t3_get_tp_version(adapter, &vers);
	if (ret)
		return ret;

	vers = t3_read_reg(adapter, A_TP_EMBED_OP_FIELD1);

	major = G_TP_VERSION_MAJOR(vers);
	minor = G_TP_VERSION_MINOR(vers);

	if (major == TP_VERSION_MAJOR && minor == TP_VERSION_MINOR)
		return 0;
	else {
		CH_ERR(adapter, "found wrong TP version (%u.%u), "
		       "driver compiled for version %d.%d\n", major, minor,
		       TP_VERSION_MAJOR, TP_VERSION_MINOR);
	}
	return -EINVAL;
}

/**
 *	t3_check_tpsram - check if provided protocol SRAM
 *			  is compatible with this driver
 *	@adapter: the adapter
 *	@tp_sram: the firmware image to write
 *	@size: image size
 *
 *	Checks if an adapter's tp sram is compatible with the driver.
 *	Returns 0 if the versions are compatible, a negative error otherwise.
 */
int t3_check_tpsram(adapter_t *adapter, const u8 *tp_sram, unsigned int size)
{
	u32 csum;
	unsigned int i;
	const u32 *p = (const u32 *)tp_sram;

	/* Verify checksum */
	for (csum = 0, i = 0; i < size / sizeof(csum); i++)
		csum += ntohl(p[i]);
	if (csum != 0xffffffff) {
		CH_ERR(adapter, "corrupted protocol SRAM image, checksum %u\n",
		       csum);
		return -EINVAL;
	}

	return 0;
}

enum fw_version_type {
	FW_VERSION_N3,
	FW_VERSION_T3
};

/**
 *	t3_get_fw_version - read the firmware version
 *	@adapter: the adapter
 *	@vers: where to place the version
 *
 *	Reads the FW version from flash. Note that we had to move the version
 *	due to FW size. If we don't find a valid FW version in the new location
 *	we fall back and read the old location.
 */
int t3_get_fw_version(adapter_t *adapter, u32 *vers)
{
	int ret = t3_read_flash(adapter, FW_VERS_ADDR, 1, vers, 0);
	if (!ret && *vers != 0xffffffff)
		return 0;
	else
		return t3_read_flash(adapter, FW_VERS_ADDR_PRE8, 1, vers, 0);
}

/**
 *	t3_check_fw_version - check if the FW is compatible with this driver
 *	@adapter: the adapter
 *
 *	Checks if an adapter's FW is compatible with the driver.  Returns 0
 *	if the versions are compatible, a negative error otherwise.
 */
int t3_check_fw_version(adapter_t *adapter)
{
	int ret;
	u32 vers;
	unsigned int type, major, minor;

	ret = t3_get_fw_version(adapter, &vers);
	if (ret)
		return ret;

	type = G_FW_VERSION_TYPE(vers);
	major = G_FW_VERSION_MAJOR(vers);
	minor = G_FW_VERSION_MINOR(vers);

	if (type == FW_VERSION_T3 && major == FW_VERSION_MAJOR &&
	    minor == FW_VERSION_MINOR)
		return 0;

	else if (major != FW_VERSION_MAJOR || minor < FW_VERSION_MINOR)
		CH_WARN(adapter, "found old FW minor version(%u.%u), "
		        "driver compiled for version %u.%u\n", major, minor,
			FW_VERSION_MAJOR, FW_VERSION_MINOR);
	else {
		CH_WARN(adapter, "found newer FW version(%u.%u), "
		        "driver compiled for version %u.%u\n", major, minor,
			FW_VERSION_MAJOR, FW_VERSION_MINOR);
			return 0;
	}
	return -EINVAL;
}

/**
 *	t3_flash_erase_sectors - erase a range of flash sectors
 *	@adapter: the adapter
 *	@start: the first sector to erase
 *	@end: the last sector to erase
 *
 *	Erases the sectors in the given range.
 */
static int t3_flash_erase_sectors(adapter_t *adapter, int start, int end)
{
	while (start <= end) {
		int ret;

		if ((ret = sf1_write(adapter, 1, 0, SF_WR_ENABLE)) != 0 ||
		    (ret = sf1_write(adapter, 4, 0,
				     SF_ERASE_SECTOR | (start << 8))) != 0 ||
		    (ret = flash_wait_op(adapter, 5, 500)) != 0)
			return ret;
		start++;
	}
	return 0;
}

/*
 *	t3_load_fw - download firmware
 *	@adapter: the adapter
 *	@fw_data: the firmware image to write
 *	@size: image size
 *
 *	Write the supplied firmware image to the card's serial flash.
 *	The FW image has the following sections: @size - 8 bytes of code and
 *	data, followed by 4 bytes of FW version, followed by the 32-bit
 *	1's complement checksum of the whole image.
 */
int t3_load_fw(adapter_t *adapter, const u8 *fw_data, unsigned int size)
{
	u32 version, csum, fw_version_addr;
	unsigned int i;
	const u32 *p = (const u32 *)fw_data;
	int ret, addr, fw_sector = FW_FLASH_BOOT_ADDR >> 16;

	if ((size & 3) || size < FW_MIN_SIZE)
		return -EINVAL;
	if (size - 8 > FW_MAX_SIZE)
		return -EFBIG;

	version = ntohl(*(const u32 *)(fw_data + size - 8));
	if (G_FW_VERSION_MAJOR(version) < 8) {

		fw_version_addr = FW_VERS_ADDR_PRE8;

		if (size - 8 > FW_MAX_SIZE_PRE8)
			return -EFBIG;
	} else
		fw_version_addr = FW_VERS_ADDR;

	for (csum = 0, i = 0; i < size / sizeof(csum); i++)
		csum += ntohl(p[i]);
	if (csum != 0xffffffff) {
		CH_ERR(adapter, "corrupted firmware image, checksum %u\n",
		       csum);
		return -EINVAL;
	}

	ret = t3_flash_erase_sectors(adapter, fw_sector, fw_sector);
	if (ret)
		goto out;

	size -= 8;  /* trim off version and checksum */
	for (addr = FW_FLASH_BOOT_ADDR; size; ) {
		unsigned int chunk_size = min(size, 256U);

		ret = t3_write_flash(adapter, addr, chunk_size, fw_data, 1);
		if (ret)
			goto out;

		addr += chunk_size;
		fw_data += chunk_size;
		size -= chunk_size;
	}

	ret = t3_write_flash(adapter, fw_version_addr, 4, fw_data, 1);
out:
	if (ret)
		CH_ERR(adapter, "firmware download failed, error %d\n", ret);
	return ret;
}

/*
 *	t3_load_boot - download boot flash
 *	@adapter: the adapter
 *	@boot_data: the boot image to write
 *	@size: image size
 *
 *	Write the supplied boot image to the card's serial flash.
 *	The boot image has the following sections: a 28-byte header and the
 *	boot image.
 */
int t3_load_boot(adapter_t *adapter, u8 *boot_data, unsigned int size)
{
	boot_header_t *header = (boot_header_t *)boot_data;
	int ret;
	unsigned int addr;
	unsigned int boot_sector = BOOT_FLASH_BOOT_ADDR >> 16;
	unsigned int boot_end = (BOOT_FLASH_BOOT_ADDR + size - 1) >> 16;

	/*
	 * Perform some primitive sanity testing to avoid accidentally
	 * writing garbage over the boot sectors.  We ought to check for
	 * more but it's not worth it for now ...
	 */
	if (size < BOOT_MIN_SIZE || size > BOOT_MAX_SIZE) {
		CH_ERR(adapter, "boot image too small/large\n");
		return -EFBIG;
	}
	if (le16_to_cpu(*(u16*)header->signature) != BOOT_SIGNATURE) {
		CH_ERR(adapter, "boot image missing signature\n");
		return -EINVAL;
	}
	if (header->length * BOOT_SIZE_INC != size) {
		CH_ERR(adapter, "boot image header length != image length\n");
		return -EINVAL;
	}

	ret = t3_flash_erase_sectors(adapter, boot_sector, boot_end);
	if (ret)
		goto out;

	for (addr = BOOT_FLASH_BOOT_ADDR; size; ) {
		unsigned int chunk_size = min(size, 256U);

		ret = t3_write_flash(adapter, addr, chunk_size, boot_data, 0);
		if (ret)
			goto out;

		addr += chunk_size;
		boot_data += chunk_size;
		size -= chunk_size;
	}

out:
	if (ret)
		CH_ERR(adapter, "boot image download failed, error %d\n", ret);
	return ret;
}

#define CIM_CTL_BASE 0x2000

/**
 *	t3_cim_ctl_blk_read - read a block from CIM control region
 *	@adap: the adapter
 *	@addr: the start address within the CIM control region
 *	@n: number of words to read
 *	@valp: where to store the result
 *
 *	Reads a block of 4-byte words from the CIM control region.
 */
int t3_cim_ctl_blk_read(adapter_t *adap, unsigned int addr, unsigned int n,
			unsigned int *valp)
{
	int ret = 0;

	if (t3_read_reg(adap, A_CIM_HOST_ACC_CTRL) & F_HOSTBUSY)
		return -EBUSY;

	for ( ; !ret && n--; addr += 4) {
		t3_write_reg(adap, A_CIM_HOST_ACC_CTRL, CIM_CTL_BASE + addr);
		ret = t3_wait_op_done(adap, A_CIM_HOST_ACC_CTRL, F_HOSTBUSY,
				      0, 5, 2);
		if (!ret)
			*valp++ = t3_read_reg(adap, A_CIM_HOST_ACC_DATA);
	}
	return ret;
}

static void t3_gate_rx_traffic(struct cmac *mac, u32 *rx_cfg,
			       u32 *rx_hash_high, u32 *rx_hash_low)
{
	/* stop Rx unicast traffic */
	t3_mac_disable_exact_filters(mac);

	/* stop broadcast, multicast, promiscuous mode traffic */
	*rx_cfg = t3_read_reg(mac->adapter, A_XGM_RX_CFG + mac->offset);
	t3_set_reg_field(mac->adapter, A_XGM_RX_CFG + mac->offset, 
			 F_ENHASHMCAST | F_DISBCAST | F_COPYALLFRAMES,
			 F_DISBCAST);

	*rx_hash_high = t3_read_reg(mac->adapter, A_XGM_RX_HASH_HIGH +
	    mac->offset);
	t3_write_reg(mac->adapter, A_XGM_RX_HASH_HIGH + mac->offset, 0);

	*rx_hash_low = t3_read_reg(mac->adapter, A_XGM_RX_HASH_LOW +
	    mac->offset);
	t3_write_reg(mac->adapter, A_XGM_RX_HASH_LOW + mac->offset, 0);

	/* Leave time to drain max RX fifo */
	msleep(1);
}

static void t3_open_rx_traffic(struct cmac *mac, u32 rx_cfg,
			       u32 rx_hash_high, u32 rx_hash_low)
{
	t3_mac_enable_exact_filters(mac);
	t3_set_reg_field(mac->adapter, A_XGM_RX_CFG + mac->offset,
			 F_ENHASHMCAST | F_DISBCAST | F_COPYALLFRAMES,
			 rx_cfg);
	t3_write_reg(mac->adapter, A_XGM_RX_HASH_HIGH + mac->offset,
	    rx_hash_high);
	t3_write_reg(mac->adapter, A_XGM_RX_HASH_LOW + mac->offset,
	    rx_hash_low);
}

static int t3_detect_link_fault(adapter_t *adapter, int port_id)
{
	struct port_info *pi = adap2pinfo(adapter, port_id);
	struct cmac *mac = &pi->mac;
	uint32_t rx_cfg, rx_hash_high, rx_hash_low;
	int link_fault;

	/* stop rx */
	t3_gate_rx_traffic(mac, &rx_cfg, &rx_hash_high, &rx_hash_low);
	t3_write_reg(adapter, A_XGM_RX_CTRL + mac->offset, 0);

	/* clear status and make sure intr is enabled */
	(void) t3_read_reg(adapter, A_XGM_INT_STATUS + mac->offset);
	t3_xgm_intr_enable(adapter, port_id);

	/* restart rx */
	t3_write_reg(adapter, A_XGM_RX_CTRL + mac->offset, F_RXEN);
	t3_open_rx_traffic(mac, rx_cfg, rx_hash_high, rx_hash_low);

	link_fault = t3_read_reg(adapter, A_XGM_INT_STATUS + mac->offset);
	return (link_fault & F_LINKFAULTCHANGE ? 1 : 0);
}

static void t3_clear_faults(adapter_t *adapter, int port_id)
{
	struct port_info *pi = adap2pinfo(adapter, port_id);
	struct cmac *mac = &pi->mac;

	if (adapter->params.nports <= 2) {
		t3_xgm_intr_disable(adapter, pi->port_id);
		t3_read_reg(adapter, A_XGM_INT_STATUS + mac->offset);
		t3_write_reg(adapter, A_XGM_INT_CAUSE + mac->offset, F_XGM_INT);
		t3_set_reg_field(adapter, A_XGM_INT_ENABLE + mac->offset,
				 F_XGM_INT, F_XGM_INT);
		t3_xgm_intr_enable(adapter, pi->port_id);
	}
}

/**
 *	t3_link_changed - handle interface link changes
 *	@adapter: the adapter
 *	@port_id: the port index that changed link state
 *
 *	Called when a port's link settings change to propagate the new values
 *	to the associated PHY and MAC.  After performing the common tasks it
 *	invokes an OS-specific handler.
 */
void t3_link_changed(adapter_t *adapter, int port_id)
{
	int link_ok, speed, duplex, fc, link_fault, link_state;
	struct port_info *pi = adap2pinfo(adapter, port_id);
	struct cphy *phy = &pi->phy;
	struct cmac *mac = &pi->mac;
	struct link_config *lc = &pi->link_config;

	link_ok = lc->link_ok;
	speed = lc->speed;
	duplex = lc->duplex;
	fc = lc->fc;
	link_fault = 0;

	phy->ops->get_link_status(phy, &link_state, &speed, &duplex, &fc);
	link_ok = (link_state == PHY_LINK_UP);
	if (link_state != PHY_LINK_PARTIAL)
		phy->rst = 0;
	else if (++phy->rst == 3) {
		phy->ops->reset(phy, 0);
		phy->rst = 0;
	}

	if (link_ok == 0)
		pi->link_fault = LF_NO;

	if (lc->requested_fc & PAUSE_AUTONEG)
		fc &= lc->requested_fc;
	else
		fc = lc->requested_fc & (PAUSE_RX | PAUSE_TX);

	/* Update mac speed before checking for link fault. */
	if (link_ok && speed >= 0 && lc->autoneg == AUTONEG_ENABLE &&
	    (speed != lc->speed || duplex != lc->duplex || fc != lc->fc))
		t3_mac_set_speed_duplex_fc(mac, speed, duplex, fc);

	/*
	 * Check for link faults if any of these is true:
	 * a) A link fault is suspected, and PHY says link ok
	 * b) PHY link transitioned from down -> up
	 */
	if (adapter->params.nports <= 2 &&
	    ((pi->link_fault && link_ok) || (!lc->link_ok && link_ok))) {

		link_fault = t3_detect_link_fault(adapter, port_id);
		if (link_fault) {
			if (pi->link_fault != LF_YES) {
				mac->stats.link_faults++;
				pi->link_fault = LF_YES;
			}

			if (uses_xaui(adapter)) {
				if (adapter->params.rev >= T3_REV_C)
					t3c_pcs_force_los(mac);
				else
					t3b_pcs_reset(mac);
			}

			/* Don't report link up */
			link_ok = 0;
		} else {
			/* clear faults here if this was a false alarm. */
			if (pi->link_fault == LF_MAYBE &&
			    link_ok && lc->link_ok)
				t3_clear_faults(adapter, port_id);

			pi->link_fault = LF_NO;
		}
	}

	if (link_ok == lc->link_ok && speed == lc->speed &&
	    duplex == lc->duplex && fc == lc->fc)
		return;                            /* nothing changed */

	lc->link_ok = (unsigned char)link_ok;
	lc->speed = speed < 0 ? SPEED_INVALID : speed;
	lc->duplex = duplex < 0 ? DUPLEX_INVALID : duplex;
	lc->fc = fc;

	if (link_ok) {

		/* down -> up, or up -> up with changed settings */

		if (adapter->params.rev > 0 && uses_xaui(adapter)) {

			if (adapter->params.rev >= T3_REV_C)
				t3c_pcs_force_los(mac);
			else
				t3b_pcs_reset(mac);

			t3_write_reg(adapter, A_XGM_XAUI_ACT_CTRL + mac->offset,
				     F_TXACTENABLE | F_RXEN);
		}

		/* disable TX FIFO drain */
		t3_set_reg_field(adapter, A_XGM_TXFIFO_CFG + mac->offset,
				 F_ENDROPPKT, 0);

		t3_mac_enable(mac, MAC_DIRECTION_TX | MAC_DIRECTION_RX);
		t3_set_reg_field(adapter, A_XGM_STAT_CTRL + mac->offset,
				 F_CLRSTATS, 1);
		t3_clear_faults(adapter, port_id);

	} else {

		/* up -> down */

		if (adapter->params.rev > 0 && uses_xaui(adapter)) {
			t3_write_reg(adapter,
				     A_XGM_XAUI_ACT_CTRL + mac->offset, 0);
		}

		t3_xgm_intr_disable(adapter, pi->port_id);
		if (adapter->params.nports <= 2) {
			t3_set_reg_field(adapter,
					 A_XGM_INT_ENABLE + mac->offset,
					 F_XGM_INT, 0);

			t3_mac_disable(mac, MAC_DIRECTION_RX);

			/*
			 * Make sure Tx FIFO continues to drain, even as rxen is
			 * left high to help detect and indicate remote faults.
			 */
			t3_set_reg_field(adapter,
			    A_XGM_TXFIFO_CFG + mac->offset, 0, F_ENDROPPKT);
			t3_write_reg(adapter, A_XGM_RX_CTRL + mac->offset, 0);
			t3_write_reg(adapter,
			    A_XGM_TX_CTRL + mac->offset, F_TXEN);
			t3_write_reg(adapter,
			    A_XGM_RX_CTRL + mac->offset, F_RXEN);
		}
	}

	t3_os_link_changed(adapter, port_id, link_ok, speed, duplex, fc,
	    mac->was_reset);
	mac->was_reset = 0;
}

/**
 *	t3_link_start - apply link configuration to MAC/PHY
 *	@phy: the PHY to setup
 *	@mac: the MAC to setup
 *	@lc: the requested link configuration
 *
 *	Set up a port's MAC and PHY according to a desired link configuration.
 *	- If the PHY can auto-negotiate first decide what to advertise, then
 *	  enable/disable auto-negotiation as desired, and reset.
 *	- If the PHY does not auto-negotiate just reset it.
 *	- If auto-negotiation is off set the MAC to the proper speed/duplex/FC,
 *	  otherwise do it later based on the outcome of auto-negotiation.
 */
int t3_link_start(struct cphy *phy, struct cmac *mac, struct link_config *lc)
{
	unsigned int fc = lc->requested_fc & (PAUSE_RX | PAUSE_TX);

	lc->link_ok = 0;
	if (lc->supported & SUPPORTED_Autoneg) {
		lc->advertising &= ~(ADVERTISED_Asym_Pause | ADVERTISED_Pause);
		if (fc) {
			lc->advertising |= ADVERTISED_Asym_Pause;
			if (fc & PAUSE_RX)
				lc->advertising |= ADVERTISED_Pause;
		}

		phy->ops->advertise(phy, lc->advertising);

		if (lc->autoneg == AUTONEG_DISABLE) {
			lc->speed = lc->requested_speed;
			lc->duplex = lc->requested_duplex;
			lc->fc = (unsigned char)fc;
			t3_mac_set_speed_duplex_fc(mac, lc->speed, lc->duplex,
						   fc);
			/* Also disables autoneg */
			phy->ops->set_speed_duplex(phy, lc->speed, lc->duplex);
			/* PR 5666. Power phy up when doing an ifup */
			if (!is_10G(phy->adapter))
				phy->ops->power_down(phy, 0);
		} else
			phy->ops->autoneg_enable(phy);
	} else {
		t3_mac_set_speed_duplex_fc(mac, -1, -1, fc);
		lc->fc = (unsigned char)fc;
		phy->ops->reset(phy, 0);
	}
	return 0;
}

/**
 *	t3_set_vlan_accel - control HW VLAN extraction
 *	@adapter: the adapter
 *	@ports: bitmap of adapter ports to operate on
 *	@on: enable (1) or disable (0) HW VLAN extraction
 *
 *	Enables or disables HW extraction of VLAN tags for the given port.
 */
void t3_set_vlan_accel(adapter_t *adapter, unsigned int ports, int on)
{
	t3_set_reg_field(adapter, A_TP_OUT_CONFIG,
			 ports << S_VLANEXTRACTIONENABLE,
			 on ? (ports << S_VLANEXTRACTIONENABLE) : 0);
}

struct intr_info {
	unsigned int mask;       /* bits to check in interrupt status */
	const char *msg;         /* message to print or NULL */
	short stat_idx;          /* stat counter to increment or -1 */
	unsigned short fatal;    /* whether the condition reported is fatal */
};

/**
 *	t3_handle_intr_status - table driven interrupt handler
 *	@adapter: the adapter that generated the interrupt
 *	@reg: the interrupt status register to process
 *	@mask: a mask to apply to the interrupt status
 *	@acts: table of interrupt actions
 *	@stats: statistics counters tracking interrupt occurrences
 *
 *	A table driven interrupt handler that applies a set of masks to an
 *	interrupt status word and performs the corresponding actions if the
 *	interrupts described by the mask have occurred.  The actions include
 *	optionally printing a warning or alert message, and optionally
 *	incrementing a stat counter.  The table is terminated by an entry
 *	specifying mask 0.  Returns the number of fatal interrupt conditions.
 */
static int t3_handle_intr_status(adapter_t *adapter, unsigned int reg,
				 unsigned int mask,
				 const struct intr_info *acts,
				 unsigned long *stats)
{
	int fatal = 0;
	unsigned int status = t3_read_reg(adapter, reg) & mask;

	for ( ; acts->mask; ++acts) {
		if (!(status & acts->mask)) continue;
		if (acts->fatal) {
			fatal++;
			CH_ALERT(adapter, "%s (0x%x)\n",
				 acts->msg, status & acts->mask);
			status &= ~acts->mask;
		} else if (acts->msg)
			CH_WARN(adapter, "%s (0x%x)\n",
				acts->msg, status & acts->mask);
		if (acts->stat_idx >= 0)
			stats[acts->stat_idx]++;
	}
	if (status)                           /* clear processed interrupts */
		t3_write_reg(adapter, reg, status);
	return fatal;
}

#define SGE_INTR_MASK (F_RSPQDISABLED | \
		       F_UC_REQ_FRAMINGERROR | F_R_REQ_FRAMINGERROR | \
		       F_CPPARITYERROR | F_OCPARITYERROR | F_RCPARITYERROR | \
		       F_IRPARITYERROR | V_ITPARITYERROR(M_ITPARITYERROR) | \
		       V_FLPARITYERROR(M_FLPARITYERROR) | F_LODRBPARITYERROR | \
		       F_HIDRBPARITYERROR | F_LORCQPARITYERROR | \
		       F_HIRCQPARITYERROR)
#define MC5_INTR_MASK (F_PARITYERR | F_ACTRGNFULL | F_UNKNOWNCMD | \
		       F_REQQPARERR | F_DISPQPARERR | F_DELACTEMPTY | \
		       F_NFASRCHFAIL)
#define MC7_INTR_MASK (F_AE | F_UE | F_CE | V_PE(M_PE))
#define XGM_INTR_MASK (V_TXFIFO_PRTY_ERR(M_TXFIFO_PRTY_ERR) | \
		       V_RXFIFO_PRTY_ERR(M_RXFIFO_PRTY_ERR) | \
		       F_TXFIFO_UNDERRUN)
#define PCIX_INTR_MASK (F_MSTDETPARERR | F_SIGTARABT | F_RCVTARABT | \
			F_RCVMSTABT | F_SIGSYSERR | F_DETPARERR | \
			F_SPLCMPDIS | F_UNXSPLCMP | F_RCVSPLCMPERR | \
			F_DETCORECCERR | F_DETUNCECCERR | F_PIOPARERR | \
			V_WFPARERR(M_WFPARERR) | V_RFPARERR(M_RFPARERR) | \
			V_CFPARERR(M_CFPARERR) /* | V_MSIXPARERR(M_MSIXPARERR) */)
#define PCIE_INTR_MASK (F_UNXSPLCPLERRR | F_UNXSPLCPLERRC | F_PCIE_PIOPARERR |\
			F_PCIE_WFPARERR | F_PCIE_RFPARERR | F_PCIE_CFPARERR | \
			/* V_PCIE_MSIXPARERR(M_PCIE_MSIXPARERR) | */ \
			F_RETRYBUFPARERR | F_RETRYLUTPARERR | F_RXPARERR | \
			F_TXPARERR | V_BISTERR(M_BISTERR))
#define ULPRX_INTR_MASK (F_PARERRDATA | F_PARERRPCMD | F_ARBPF1PERR | \
			 F_ARBPF0PERR | F_ARBFPERR | F_PCMDMUXPERR | \
			 F_DATASELFRAMEERR1 | F_DATASELFRAMEERR0)
#define ULPTX_INTR_MASK 0xfc
#define CPLSW_INTR_MASK (F_CIM_OP_MAP_PERR | F_TP_FRAMING_ERROR | \
			 F_SGE_FRAMING_ERROR | F_CIM_FRAMING_ERROR | \
			 F_ZERO_SWITCH_ERROR)
#define CIM_INTR_MASK (F_BLKWRPLINT | F_BLKRDPLINT | F_BLKWRCTLINT | \
		       F_BLKRDCTLINT | F_BLKWRFLASHINT | F_BLKRDFLASHINT | \
		       F_SGLWRFLASHINT | F_WRBLKFLASHINT | F_BLKWRBOOTINT | \
	 	       F_FLASHRANGEINT | F_SDRAMRANGEINT | F_RSVDSPACEINT | \
		       F_DRAMPARERR | F_ICACHEPARERR | F_DCACHEPARERR | \
		       F_OBQSGEPARERR | F_OBQULPHIPARERR | F_OBQULPLOPARERR | \
		       F_IBQSGELOPARERR | F_IBQSGEHIPARERR | F_IBQULPPARERR | \
		       F_IBQTPPARERR | F_ITAGPARERR | F_DTAGPARERR)
#define PMTX_INTR_MASK (F_ZERO_C_CMD_ERROR | ICSPI_FRM_ERR | OESPI_FRM_ERR | \
			V_ICSPI_PAR_ERROR(M_ICSPI_PAR_ERROR) | \
			V_OESPI_PAR_ERROR(M_OESPI_PAR_ERROR))
#define PMRX_INTR_MASK (F_ZERO_E_CMD_ERROR | IESPI_FRM_ERR | OCSPI_FRM_ERR | \
			V_IESPI_PAR_ERROR(M_IESPI_PAR_ERROR) | \
			V_OCSPI_PAR_ERROR(M_OCSPI_PAR_ERROR))
#define MPS_INTR_MASK (V_TX0TPPARERRENB(M_TX0TPPARERRENB) | \
		       V_TX1TPPARERRENB(M_TX1TPPARERRENB) | \
		       V_RXTPPARERRENB(M_RXTPPARERRENB) | \
		       V_MCAPARERRENB(M_MCAPARERRENB))
#define XGM_EXTRA_INTR_MASK (F_LINKFAULTCHANGE)
#define PL_INTR_MASK (F_T3DBG | F_XGMAC0_0 | F_XGMAC0_1 | F_MC5A | F_PM1_TX | \
		      F_PM1_RX | F_ULP2_TX | F_ULP2_RX | F_TP1 | F_CIM | \
		      F_MC7_CM | F_MC7_PMTX | F_MC7_PMRX | F_SGE3 | F_PCIM0 | \
		      F_MPS0 | F_CPL_SWITCH)
/*
 * Interrupt handler for the PCIX1 module.
 */
static void pci_intr_handler(adapter_t *adapter)
{
	static struct intr_info pcix1_intr_info[] = {
		{ F_MSTDETPARERR, "PCI master detected parity error", -1, 1 },
		{ F_SIGTARABT, "PCI signaled target abort", -1, 1 },
		{ F_RCVTARABT, "PCI received target abort", -1, 1 },
		{ F_RCVMSTABT, "PCI received master abort", -1, 1 },
		{ F_SIGSYSERR, "PCI signaled system error", -1, 1 },
		{ F_DETPARERR, "PCI detected parity error", -1, 1 },
		{ F_SPLCMPDIS, "PCI split completion discarded", -1, 1 },
		{ F_UNXSPLCMP, "PCI unexpected split completion error", -1, 1 },
		{ F_RCVSPLCMPERR, "PCI received split completion error", -1,
		  1 },
		{ F_DETCORECCERR, "PCI correctable ECC error",
		  STAT_PCI_CORR_ECC, 0 },
		{ F_DETUNCECCERR, "PCI uncorrectable ECC error", -1, 1 },
		{ F_PIOPARERR, "PCI PIO FIFO parity error", -1, 1 },
		{ V_WFPARERR(M_WFPARERR), "PCI write FIFO parity error", -1,
		  1 },
		{ V_RFPARERR(M_RFPARERR), "PCI read FIFO parity error", -1,
		  1 },
		{ V_CFPARERR(M_CFPARERR), "PCI command FIFO parity error", -1,
		  1 },
		{ V_MSIXPARERR(M_MSIXPARERR), "PCI MSI-X table/PBA parity "
		  "error", -1, 1 },
		{ 0 }
	};

	if (t3_handle_intr_status(adapter, A_PCIX_INT_CAUSE, PCIX_INTR_MASK,
				  pcix1_intr_info, adapter->irq_stats))
		t3_fatal_err(adapter);
}

/*
 * Interrupt handler for the PCIE module.
 */
static void pcie_intr_handler(adapter_t *adapter)
{
	static struct intr_info pcie_intr_info[] = {
		{ F_PEXERR, "PCI PEX error", -1, 1 },
		{ F_UNXSPLCPLERRR,
		  "PCI unexpected split completion DMA read error", -1, 1 },
		{ F_UNXSPLCPLERRC,
		  "PCI unexpected split completion DMA command error", -1, 1 },
		{ F_PCIE_PIOPARERR, "PCI PIO FIFO parity error", -1, 1 },
		{ F_PCIE_WFPARERR, "PCI write FIFO parity error", -1, 1 },
		{ F_PCIE_RFPARERR, "PCI read FIFO parity error", -1, 1 },
		{ F_PCIE_CFPARERR, "PCI command FIFO parity error", -1, 1 },
		{ V_PCIE_MSIXPARERR(M_PCIE_MSIXPARERR),
		  "PCI MSI-X table/PBA parity error", -1, 1 },
		{ F_RETRYBUFPARERR, "PCI retry buffer parity error", -1, 1 },
		{ F_RETRYLUTPARERR, "PCI retry LUT parity error", -1, 1 },
		{ F_RXPARERR, "PCI Rx parity error", -1, 1 },
		{ F_TXPARERR, "PCI Tx parity error", -1, 1 },
		{ V_BISTERR(M_BISTERR), "PCI BIST error", -1, 1 },
		{ 0 }
	};

	if (t3_read_reg(adapter, A_PCIE_INT_CAUSE) & F_PEXERR)
		CH_ALERT(adapter, "PEX error code 0x%x\n",
			 t3_read_reg(adapter, A_PCIE_PEX_ERR));

	if (t3_handle_intr_status(adapter, A_PCIE_INT_CAUSE, PCIE_INTR_MASK,
				  pcie_intr_info, adapter->irq_stats))
		t3_fatal_err(adapter);
}

/*
 * TP interrupt handler.
 */
static void tp_intr_handler(adapter_t *adapter)
{
	static struct intr_info tp_intr_info[] = {
		{ 0xffffff,  "TP parity error", -1, 1 },
		{ 0x1000000, "TP out of Rx pages", -1, 1 },
		{ 0x2000000, "TP out of Tx pages", -1, 1 },
		{ 0 }
	};
	static struct intr_info tp_intr_info_t3c[] = {
		{ 0x1fffffff,  "TP parity error", -1, 1 },
		{ F_FLMRXFLSTEMPTY, "TP out of Rx pages", -1, 1 },
		{ F_FLMTXFLSTEMPTY, "TP out of Tx pages", -1, 1 },
		{ 0 }
	};

	if (t3_handle_intr_status(adapter, A_TP_INT_CAUSE, 0xffffffff,
				  adapter->params.rev < T3_REV_C ?
					tp_intr_info : tp_intr_info_t3c, NULL))
		t3_fatal_err(adapter);
}

/*
 * CIM interrupt handler.
 */
static void cim_intr_handler(adapter_t *adapter)
{
	static struct intr_info cim_intr_info[] = {
		{ F_RSVDSPACEINT, "CIM reserved space write", -1, 1 },
		{ F_SDRAMRANGEINT, "CIM SDRAM address out of range", -1, 1 },
		{ F_FLASHRANGEINT, "CIM flash address out of range", -1, 1 },
		{ F_BLKWRBOOTINT, "CIM block write to boot space", -1, 1 },
		{ F_WRBLKFLASHINT, "CIM write to cached flash space", -1, 1 },
		{ F_SGLWRFLASHINT, "CIM single write to flash space", -1, 1 },
		{ F_BLKRDFLASHINT, "CIM block read from flash space", -1, 1 },
		{ F_BLKWRFLASHINT, "CIM block write to flash space", -1, 1 },
		{ F_BLKRDCTLINT, "CIM block read from CTL space", -1, 1 },
		{ F_BLKWRCTLINT, "CIM block write to CTL space", -1, 1 },
		{ F_BLKRDPLINT, "CIM block read from PL space", -1, 1 },
		{ F_BLKWRPLINT, "CIM block write to PL space", -1, 1 },
		{ F_DRAMPARERR, "CIM DRAM parity error", -1, 1 },
		{ F_ICACHEPARERR, "CIM icache parity error", -1, 1 },
		{ F_DCACHEPARERR, "CIM dcache parity error", -1, 1 },
		{ F_OBQSGEPARERR, "CIM OBQ SGE parity error", -1, 1 },
		{ F_OBQULPHIPARERR, "CIM OBQ ULPHI parity error", -1, 1 },
		{ F_OBQULPLOPARERR, "CIM OBQ ULPLO parity error", -1, 1 },
		{ F_IBQSGELOPARERR, "CIM IBQ SGELO parity error", -1, 1 },
		{ F_IBQSGEHIPARERR, "CIM IBQ SGEHI parity error", -1, 1 },
		{ F_IBQULPPARERR, "CIM IBQ ULP parity error", -1, 1 },
		{ F_IBQTPPARERR, "CIM IBQ TP parity error", -1, 1 },
		{ F_ITAGPARERR, "CIM itag parity error", -1, 1 },
		{ F_DTAGPARERR, "CIM dtag parity error", -1, 1 },
		{ 0 }
        };

	if (t3_handle_intr_status(adapter, A_CIM_HOST_INT_CAUSE, CIM_INTR_MASK,
				  cim_intr_info, NULL))
		t3_fatal_err(adapter);
}

/*
 * ULP RX interrupt handler.
 */
static void ulprx_intr_handler(adapter_t *adapter)
{
	static struct intr_info ulprx_intr_info[] = {
		{ F_PARERRDATA, "ULP RX data parity error", -1, 1 },
		{ F_PARERRPCMD, "ULP RX command parity error", -1, 1 },
		{ F_ARBPF1PERR, "ULP RX ArbPF1 parity error", -1, 1 },
		{ F_ARBPF0PERR, "ULP RX ArbPF0 parity error", -1, 1 },
		{ F_ARBFPERR, "ULP RX ArbF parity error", -1, 1 },
		{ F_PCMDMUXPERR, "ULP RX PCMDMUX parity error", -1, 1 },
		{ F_DATASELFRAMEERR1, "ULP RX frame error", -1, 1 },
		{ F_DATASELFRAMEERR0, "ULP RX frame error", -1, 1 },
		{ 0 }
        };

	if (t3_handle_intr_status(adapter, A_ULPRX_INT_CAUSE, 0xffffffff,
				  ulprx_intr_info, NULL))
		t3_fatal_err(adapter);
}

/*
 * ULP TX interrupt handler.
 */
static void ulptx_intr_handler(adapter_t *adapter)
{
	static struct intr_info ulptx_intr_info[] = {
		{ F_PBL_BOUND_ERR_CH0, "ULP TX channel 0 PBL out of bounds",
		  STAT_ULP_CH0_PBL_OOB, 0 },
		{ F_PBL_BOUND_ERR_CH1, "ULP TX channel 1 PBL out of bounds",
		  STAT_ULP_CH1_PBL_OOB, 0 },
		{ 0xfc, "ULP TX parity error", -1, 1 },
		{ 0 }
        };

	if (t3_handle_intr_status(adapter, A_ULPTX_INT_CAUSE, 0xffffffff,
				  ulptx_intr_info, adapter->irq_stats))
		t3_fatal_err(adapter);
}

#define ICSPI_FRM_ERR (F_ICSPI0_FIFO2X_RX_FRAMING_ERROR | \
	F_ICSPI1_FIFO2X_RX_FRAMING_ERROR | F_ICSPI0_RX_FRAMING_ERROR | \
	F_ICSPI1_RX_FRAMING_ERROR | F_ICSPI0_TX_FRAMING_ERROR | \
	F_ICSPI1_TX_FRAMING_ERROR)
#define OESPI_FRM_ERR (F_OESPI0_RX_FRAMING_ERROR | \
	F_OESPI1_RX_FRAMING_ERROR | F_OESPI0_TX_FRAMING_ERROR | \
	F_OESPI1_TX_FRAMING_ERROR | F_OESPI0_OFIFO2X_TX_FRAMING_ERROR | \
	F_OESPI1_OFIFO2X_TX_FRAMING_ERROR)

/*
 * PM TX interrupt handler.
 */
static void pmtx_intr_handler(adapter_t *adapter)
{
	static struct intr_info pmtx_intr_info[] = {
		{ F_ZERO_C_CMD_ERROR, "PMTX 0-length pcmd", -1, 1 },
		{ ICSPI_FRM_ERR, "PMTX ispi framing error", -1, 1 },
		{ OESPI_FRM_ERR, "PMTX ospi framing error", -1, 1 },
		{ V_ICSPI_PAR_ERROR(M_ICSPI_PAR_ERROR),
		  "PMTX ispi parity error", -1, 1 },
		{ V_OESPI_PAR_ERROR(M_OESPI_PAR_ERROR),
		  "PMTX ospi parity error", -1, 1 },
		{ 0 }
        };

	if (t3_handle_intr_status(adapter, A_PM1_TX_INT_CAUSE, 0xffffffff,
				  pmtx_intr_info, NULL))
		t3_fatal_err(adapter);
}

#define IESPI_FRM_ERR (F_IESPI0_FIFO2X_RX_FRAMING_ERROR | \
	F_IESPI1_FIFO2X_RX_FRAMING_ERROR | F_IESPI0_RX_FRAMING_ERROR | \
	F_IESPI1_RX_FRAMING_ERROR | F_IESPI0_TX_FRAMING_ERROR | \
	F_IESPI1_TX_FRAMING_ERROR)
#define OCSPI_FRM_ERR (F_OCSPI0_RX_FRAMING_ERROR | \
	F_OCSPI1_RX_FRAMING_ERROR | F_OCSPI0_TX_FRAMING_ERROR | \
	F_OCSPI1_TX_FRAMING_ERROR | F_OCSPI0_OFIFO2X_TX_FRAMING_ERROR | \
	F_OCSPI1_OFIFO2X_TX_FRAMING_ERROR)

/*
 * PM RX interrupt handler.
 */
static void pmrx_intr_handler(adapter_t *adapter)
{
	static struct intr_info pmrx_intr_info[] = {
		{ F_ZERO_E_CMD_ERROR, "PMRX 0-length pcmd", -1, 1 },
		{ IESPI_FRM_ERR, "PMRX ispi framing error", -1, 1 },
		{ OCSPI_FRM_ERR, "PMRX ospi framing error", -1, 1 },
		{ V_IESPI_PAR_ERROR(M_IESPI_PAR_ERROR),
		  "PMRX ispi parity error", -1, 1 },
		{ V_OCSPI_PAR_ERROR(M_OCSPI_PAR_ERROR),
		  "PMRX ospi parity error", -1, 1 },
		{ 0 }
        };

	if (t3_handle_intr_status(adapter, A_PM1_RX_INT_CAUSE, 0xffffffff,
				  pmrx_intr_info, NULL))
		t3_fatal_err(adapter);
}

/*
 * CPL switch interrupt handler.
 */
static void cplsw_intr_handler(adapter_t *adapter)
{
	static struct intr_info cplsw_intr_info[] = {
		{ F_CIM_OP_MAP_PERR, "CPL switch CIM parity error", -1, 1 },
		{ F_CIM_OVFL_ERROR, "CPL switch CIM overflow", -1, 1 },
		{ F_TP_FRAMING_ERROR, "CPL switch TP framing error", -1, 1 },
		{ F_SGE_FRAMING_ERROR, "CPL switch SGE framing error", -1, 1 },
		{ F_CIM_FRAMING_ERROR, "CPL switch CIM framing error", -1, 1 },
		{ F_ZERO_SWITCH_ERROR, "CPL switch no-switch error", -1, 1 },
		{ 0 }
        };

	if (t3_handle_intr_status(adapter, A_CPL_INTR_CAUSE, 0xffffffff,
				  cplsw_intr_info, NULL))
		t3_fatal_err(adapter);
}

/*
 * MPS interrupt handler.
 */
static void mps_intr_handler(adapter_t *adapter)
{
	static struct intr_info mps_intr_info[] = {
		{ 0x1ff, "MPS parity error", -1, 1 },
		{ 0 }
	};

	if (t3_handle_intr_status(adapter, A_MPS_INT_CAUSE, 0xffffffff,
				  mps_intr_info, NULL))
		t3_fatal_err(adapter);
}

#define MC7_INTR_FATAL (F_UE | V_PE(M_PE) | F_AE)

/*
 * MC7 interrupt handler.
 */
static void mc7_intr_handler(struct mc7 *mc7)
{
	adapter_t *adapter = mc7->adapter;
	u32 cause = t3_read_reg(adapter, mc7->offset + A_MC7_INT_CAUSE);

	if (cause & F_CE) {
		mc7->stats.corr_err++;
		CH_WARN(adapter, "%s MC7 correctable error at addr 0x%x, "
			"data 0x%x 0x%x 0x%x\n", mc7->name,
			t3_read_reg(adapter, mc7->offset + A_MC7_CE_ADDR),
			t3_read_reg(adapter, mc7->offset + A_MC7_CE_DATA0),
			t3_read_reg(adapter, mc7->offset + A_MC7_CE_DATA1),
			t3_read_reg(adapter, mc7->offset + A_MC7_CE_DATA2));
	}

	if (cause & F_UE) {
		mc7->stats.uncorr_err++;
		CH_ALERT(adapter, "%s MC7 uncorrectable error at addr 0x%x, "
			 "data 0x%x 0x%x 0x%x\n", mc7->name,
			 t3_read_reg(adapter, mc7->offset + A_MC7_UE_ADDR),
			 t3_read_reg(adapter, mc7->offset + A_MC7_UE_DATA0),
			 t3_read_reg(adapter, mc7->offset + A_MC7_UE_DATA1),
			 t3_read_reg(adapter, mc7->offset + A_MC7_UE_DATA2));
	}

	if (G_PE(cause)) {
		mc7->stats.parity_err++;
		CH_ALERT(adapter, "%s MC7 parity error 0x%x\n",
			 mc7->name, G_PE(cause));
	}

	if (cause & F_AE) {
		u32 addr = 0;

		if (adapter->params.rev > 0)
			addr = t3_read_reg(adapter,
					   mc7->offset + A_MC7_ERR_ADDR);
		mc7->stats.addr_err++;
		CH_ALERT(adapter, "%s MC7 address error: 0x%x\n",
			 mc7->name, addr);
	}

	if (cause & MC7_INTR_FATAL)
		t3_fatal_err(adapter);

	t3_write_reg(adapter, mc7->offset + A_MC7_INT_CAUSE, cause);
}

#define XGM_INTR_FATAL (V_TXFIFO_PRTY_ERR(M_TXFIFO_PRTY_ERR) | \
			V_RXFIFO_PRTY_ERR(M_RXFIFO_PRTY_ERR))
/*
 * XGMAC interrupt handler.
 */
static int mac_intr_handler(adapter_t *adap, unsigned int idx)
{
	u32 cause;
	struct port_info *pi;
	struct cmac *mac;

	idx = idx == 0 ? 0 : adapter_info(adap)->nports0; /* MAC idx -> port */
	pi = adap2pinfo(adap, idx);
	mac = &pi->mac;

	/*
	 * We mask out interrupt causes for which we're not taking interrupts.
	 * This allows us to use polling logic to monitor some of the other
	 * conditions when taking interrupts would impose too much load on the
	 * system.
	 */
	cause = (t3_read_reg(adap, A_XGM_INT_CAUSE + mac->offset)
		 & ~(F_RXFIFO_OVERFLOW));

	if (cause & V_TXFIFO_PRTY_ERR(M_TXFIFO_PRTY_ERR)) {
		mac->stats.tx_fifo_parity_err++;
		CH_ALERT(adap, "port%d: MAC TX FIFO parity error\n", idx);
	}
	if (cause & V_RXFIFO_PRTY_ERR(M_RXFIFO_PRTY_ERR)) {
		mac->stats.rx_fifo_parity_err++;
		CH_ALERT(adap, "port%d: MAC RX FIFO parity error\n", idx);
	}
	if (cause & F_TXFIFO_UNDERRUN)
		mac->stats.tx_fifo_urun++;
	if (cause & F_RXFIFO_OVERFLOW)
		mac->stats.rx_fifo_ovfl++;
	if (cause & V_SERDES_LOS(M_SERDES_LOS))
		mac->stats.serdes_signal_loss++;
	if (cause & F_XAUIPCSCTCERR)
		mac->stats.xaui_pcs_ctc_err++;
	if (cause & F_XAUIPCSALIGNCHANGE)
		mac->stats.xaui_pcs_align_change++;
	if (cause & F_XGM_INT &
	    t3_read_reg(adap, A_XGM_INT_ENABLE + mac->offset)) {
		t3_set_reg_field(adap, A_XGM_INT_ENABLE + mac->offset,
		    F_XGM_INT, 0);

		/* link fault suspected */
		pi->link_fault = LF_MAYBE;
		t3_os_link_intr(pi);
	}

	if (cause & XGM_INTR_FATAL)
		t3_fatal_err(adap);

	t3_write_reg(adap, A_XGM_INT_CAUSE + mac->offset, cause);
	return cause != 0;
}

/*
 * Interrupt handler for PHY events.
 */
static int phy_intr_handler(adapter_t *adapter)
{
	u32 i, cause = t3_read_reg(adapter, A_T3DBG_INT_CAUSE);

	for_each_port(adapter, i) {
		struct port_info *p = adap2pinfo(adapter, i);

		if (!(p->phy.caps & SUPPORTED_IRQ))
			continue;

		if (cause & (1 << adapter_info(adapter)->gpio_intr[i])) {
			int phy_cause = p->phy.ops->intr_handler(&p->phy);

			if (phy_cause & cphy_cause_link_change)
				t3_os_link_intr(p);
			if (phy_cause & cphy_cause_fifo_error)
				p->phy.fifo_errors++;
			if (phy_cause & cphy_cause_module_change)
				t3_os_phymod_changed(adapter, i);
			if (phy_cause & cphy_cause_alarm)
				CH_WARN(adapter, "Operation affected due to "
				    "adverse environment.  Check the spec "
				    "sheet for corrective action.");
		}
	}

	t3_write_reg(adapter, A_T3DBG_INT_CAUSE, cause);
	return 0;
}

/**
 *	t3_slow_intr_handler - control path interrupt handler
 *	@adapter: the adapter
 *
 *	T3 interrupt handler for non-data interrupt events, e.g., errors.
 *	The designation 'slow' is because it involves register reads, while
 *	data interrupts typically don't involve any MMIOs.
 */
int t3_slow_intr_handler(adapter_t *adapter)
{
	u32 cause = t3_read_reg(adapter, A_PL_INT_CAUSE0);

	cause &= adapter->slow_intr_mask;
	if (!cause)
		return 0;
	if (cause & F_PCIM0) {
		if (is_pcie(adapter))
			pcie_intr_handler(adapter);
		else
			pci_intr_handler(adapter);
	}
	if (cause & F_SGE3)
		t3_sge_err_intr_handler(adapter);
	if (cause & F_MC7_PMRX)
		mc7_intr_handler(&adapter->pmrx);
	if (cause & F_MC7_PMTX)
		mc7_intr_handler(&adapter->pmtx);
	if (cause & F_MC7_CM)
		mc7_intr_handler(&adapter->cm);
	if (cause & F_CIM)
		cim_intr_handler(adapter);
	if (cause & F_TP1)
		tp_intr_handler(adapter);
	if (cause & F_ULP2_RX)
		ulprx_intr_handler(adapter);
	if (cause & F_ULP2_TX)
		ulptx_intr_handler(adapter);
	if (cause & F_PM1_RX)
		pmrx_intr_handler(adapter);
	if (cause & F_PM1_TX)
		pmtx_intr_handler(adapter);
	if (cause & F_CPL_SWITCH)
		cplsw_intr_handler(adapter);
	if (cause & F_MPS0)
		mps_intr_handler(adapter);
	if (cause & F_MC5A)
		t3_mc5_intr_handler(&adapter->mc5);
	if (cause & F_XGMAC0_0)
		mac_intr_handler(adapter, 0);
	if (cause & F_XGMAC0_1)
		mac_intr_handler(adapter, 1);
	if (cause & F_T3DBG)
		phy_intr_handler(adapter);

	/* Clear the interrupts just processed. */
	t3_write_reg(adapter, A_PL_INT_CAUSE0, cause);
	(void) t3_read_reg(adapter, A_PL_INT_CAUSE0); /* flush */
	return 1;
}

static unsigned int calc_gpio_intr(adapter_t *adap)
{
	unsigned int i, gpi_intr = 0;

	for_each_port(adap, i)
		if ((adap2pinfo(adap, i)->phy.caps & SUPPORTED_IRQ) &&
		    adapter_info(adap)->gpio_intr[i])
			gpi_intr |= 1 << adapter_info(adap)->gpio_intr[i];
	return gpi_intr;
}

/**
 *	t3_intr_enable - enable interrupts
 *	@adapter: the adapter whose interrupts should be enabled
 *
 *	Enable interrupts by setting the interrupt enable registers of the
 *	various HW modules and then enabling the top-level interrupt
 *	concentrator.
 */
void t3_intr_enable(adapter_t *adapter)
{
	static struct addr_val_pair intr_en_avp[] = {
		{ A_MC7_INT_ENABLE, MC7_INTR_MASK },
		{ A_MC7_INT_ENABLE - MC7_PMRX_BASE_ADDR + MC7_PMTX_BASE_ADDR,
			MC7_INTR_MASK },
		{ A_MC7_INT_ENABLE - MC7_PMRX_BASE_ADDR + MC7_CM_BASE_ADDR,
			MC7_INTR_MASK },
		{ A_MC5_DB_INT_ENABLE, MC5_INTR_MASK },
		{ A_ULPRX_INT_ENABLE, ULPRX_INTR_MASK },
		{ A_PM1_TX_INT_ENABLE, PMTX_INTR_MASK },
		{ A_PM1_RX_INT_ENABLE, PMRX_INTR_MASK },
		{ A_CIM_HOST_INT_ENABLE, CIM_INTR_MASK },
		{ A_MPS_INT_ENABLE, MPS_INTR_MASK },
	};

	adapter->slow_intr_mask = PL_INTR_MASK;

	t3_write_regs(adapter, intr_en_avp, ARRAY_SIZE(intr_en_avp), 0);
	t3_write_reg(adapter, A_TP_INT_ENABLE,
		     adapter->params.rev >= T3_REV_C ? 0x2bfffff : 0x3bfffff);
	t3_write_reg(adapter, A_SG_INT_ENABLE, SGE_INTR_MASK);

	if (adapter->params.rev > 0) {
		t3_write_reg(adapter, A_CPL_INTR_ENABLE,
			     CPLSW_INTR_MASK | F_CIM_OVFL_ERROR);
		t3_write_reg(adapter, A_ULPTX_INT_ENABLE,
			     ULPTX_INTR_MASK | F_PBL_BOUND_ERR_CH0 |
			     F_PBL_BOUND_ERR_CH1);
	} else {
		t3_write_reg(adapter, A_CPL_INTR_ENABLE, CPLSW_INTR_MASK);
		t3_write_reg(adapter, A_ULPTX_INT_ENABLE, ULPTX_INTR_MASK);
	}

	t3_write_reg(adapter, A_T3DBG_INT_ENABLE, calc_gpio_intr(adapter));

	if (is_pcie(adapter))
		t3_write_reg(adapter, A_PCIE_INT_ENABLE, PCIE_INTR_MASK);
	else
		t3_write_reg(adapter, A_PCIX_INT_ENABLE, PCIX_INTR_MASK);
	t3_write_reg(adapter, A_PL_INT_ENABLE0, adapter->slow_intr_mask);
	(void) t3_read_reg(adapter, A_PL_INT_ENABLE0);          /* flush */
}

/**
 *	t3_intr_disable - disable a card's interrupts
 *	@adapter: the adapter whose interrupts should be disabled
 *
 *	Disable interrupts.  We only disable the top-level interrupt
 *	concentrator and the SGE data interrupts.
 */
void t3_intr_disable(adapter_t *adapter)
{
	t3_write_reg(adapter, A_PL_INT_ENABLE0, 0);
	(void) t3_read_reg(adapter, A_PL_INT_ENABLE0);  /* flush */
	adapter->slow_intr_mask = 0;
}

/**
 *	t3_intr_clear - clear all interrupts
 *	@adapter: the adapter whose interrupts should be cleared
 *
 *	Clears all interrupts.
 */
void t3_intr_clear(adapter_t *adapter)
{
	static const unsigned int cause_reg_addr[] = {
		A_SG_INT_CAUSE,
		A_SG_RSPQ_FL_STATUS,
		A_PCIX_INT_CAUSE,
		A_MC7_INT_CAUSE,
		A_MC7_INT_CAUSE - MC7_PMRX_BASE_ADDR + MC7_PMTX_BASE_ADDR,
		A_MC7_INT_CAUSE - MC7_PMRX_BASE_ADDR + MC7_CM_BASE_ADDR,
		A_CIM_HOST_INT_CAUSE,
		A_TP_INT_CAUSE,
		A_MC5_DB_INT_CAUSE,
		A_ULPRX_INT_CAUSE,
		A_ULPTX_INT_CAUSE,
		A_CPL_INTR_CAUSE,
		A_PM1_TX_INT_CAUSE,
		A_PM1_RX_INT_CAUSE,
		A_MPS_INT_CAUSE,
		A_T3DBG_INT_CAUSE,
	};
	unsigned int i;

	/* Clear PHY and MAC interrupts for each port. */
	for_each_port(adapter, i)
		t3_port_intr_clear(adapter, i);

	for (i = 0; i < ARRAY_SIZE(cause_reg_addr); ++i)
		t3_write_reg(adapter, cause_reg_addr[i], 0xffffffff);

	if (is_pcie(adapter))
		t3_write_reg(adapter, A_PCIE_PEX_ERR, 0xffffffff);
	t3_write_reg(adapter, A_PL_INT_CAUSE0, 0xffffffff);
	(void) t3_read_reg(adapter, A_PL_INT_CAUSE0);          /* flush */
}

void t3_xgm_intr_enable(adapter_t *adapter, int idx)
{
	struct port_info *pi = adap2pinfo(adapter, idx);

	t3_write_reg(adapter, A_XGM_XGM_INT_ENABLE + pi->mac.offset,
		     XGM_EXTRA_INTR_MASK);
}

void t3_xgm_intr_disable(adapter_t *adapter, int idx)
{
	struct port_info *pi = adap2pinfo(adapter, idx);

	t3_write_reg(adapter, A_XGM_XGM_INT_DISABLE + pi->mac.offset,
		     0x7ff);
}

/**
 *	t3_port_intr_enable - enable port-specific interrupts
 *	@adapter: associated adapter
 *	@idx: index of port whose interrupts should be enabled
 *
 *	Enable port-specific (i.e., MAC and PHY) interrupts for the given
 *	adapter port.
 */
void t3_port_intr_enable(adapter_t *adapter, int idx)
{
	struct port_info *pi = adap2pinfo(adapter, idx);

	t3_write_reg(adapter, A_XGM_INT_ENABLE + pi->mac.offset, XGM_INTR_MASK);
	pi->phy.ops->intr_enable(&pi->phy);
}

/**
 *	t3_port_intr_disable - disable port-specific interrupts
 *	@adapter: associated adapter
 *	@idx: index of port whose interrupts should be disabled
 *
 *	Disable port-specific (i.e., MAC and PHY) interrupts for the given
 *	adapter port.
 */
void t3_port_intr_disable(adapter_t *adapter, int idx)
{
	struct port_info *pi = adap2pinfo(adapter, idx);

	t3_write_reg(adapter, A_XGM_INT_ENABLE + pi->mac.offset, 0);
	pi->phy.ops->intr_disable(&pi->phy);
}

/**
 *	t3_port_intr_clear - clear port-specific interrupts
 *	@adapter: associated adapter
 *	@idx: index of port whose interrupts to clear
 *
 *	Clear port-specific (i.e., MAC and PHY) interrupts for the given
 *	adapter port.
 */
void t3_port_intr_clear(adapter_t *adapter, int idx)
{
	struct port_info *pi = adap2pinfo(adapter, idx);

	t3_write_reg(adapter, A_XGM_INT_CAUSE + pi->mac.offset, 0xffffffff);
	pi->phy.ops->intr_clear(&pi->phy);
}

#define SG_CONTEXT_CMD_ATTEMPTS 100

/**
 * 	t3_sge_write_context - write an SGE context
 * 	@adapter: the adapter
 * 	@id: the context id
 * 	@type: the context type
 *
 * 	Program an SGE context with the values already loaded in the
 * 	CONTEXT_DATA? registers.
 */
static int t3_sge_write_context(adapter_t *adapter, unsigned int id,
				unsigned int type)
{
	if (type == F_RESPONSEQ) {
		/*
		 * Can't write the Response Queue Context bits for
		 * Interrupt Armed or the Reserve bits after the chip
		 * has been initialized out of reset.  Writing to these
		 * bits can confuse the hardware.
		 */
		t3_write_reg(adapter, A_SG_CONTEXT_MASK0, 0xffffffff);
		t3_write_reg(adapter, A_SG_CONTEXT_MASK1, 0xffffffff);
		t3_write_reg(adapter, A_SG_CONTEXT_MASK2, 0x17ffffff);
		t3_write_reg(adapter, A_SG_CONTEXT_MASK3, 0xffffffff);
	} else {
		t3_write_reg(adapter, A_SG_CONTEXT_MASK0, 0xffffffff);
		t3_write_reg(adapter, A_SG_CONTEXT_MASK1, 0xffffffff);
		t3_write_reg(adapter, A_SG_CONTEXT_MASK2, 0xffffffff);
		t3_write_reg(adapter, A_SG_CONTEXT_MASK3, 0xffffffff);
	}
	t3_write_reg(adapter, A_SG_CONTEXT_CMD,
		     V_CONTEXT_CMD_OPCODE(1) | type | V_CONTEXT(id));
	return t3_wait_op_done(adapter, A_SG_CONTEXT_CMD, F_CONTEXT_CMD_BUSY,
			       0, SG_CONTEXT_CMD_ATTEMPTS, 1);
}

/**
 *	clear_sge_ctxt - completely clear an SGE context
 *	@adapter: the adapter
 *	@id: the context id
 *	@type: the context type
 *
 *	Completely clear an SGE context.  Used predominantly at post-reset
 *	initialization.  Note in particular that we don't skip writing to any
 *	"sensitive bits" in the contexts the way that t3_sge_write_context()
 *	does ...
 */
static int clear_sge_ctxt(adapter_t *adap, unsigned int id, unsigned int type)
{
	t3_write_reg(adap, A_SG_CONTEXT_DATA0, 0);
	t3_write_reg(adap, A_SG_CONTEXT_DATA1, 0);
	t3_write_reg(adap, A_SG_CONTEXT_DATA2, 0);
	t3_write_reg(adap, A_SG_CONTEXT_DATA3, 0);
	t3_write_reg(adap, A_SG_CONTEXT_MASK0, 0xffffffff);
	t3_write_reg(adap, A_SG_CONTEXT_MASK1, 0xffffffff);
	t3_write_reg(adap, A_SG_CONTEXT_MASK2, 0xffffffff);
	t3_write_reg(adap, A_SG_CONTEXT_MASK3, 0xffffffff);
	t3_write_reg(adap, A_SG_CONTEXT_CMD,
		     V_CONTEXT_CMD_OPCODE(1) | type | V_CONTEXT(id));
	return t3_wait_op_done(adap, A_SG_CONTEXT_CMD, F_CONTEXT_CMD_BUSY,
			       0, SG_CONTEXT_CMD_ATTEMPTS, 1);
}

/**
 *	t3_sge_init_ecntxt - initialize an SGE egress context
 *	@adapter: the adapter to configure
 *	@id: the context id
 *	@gts_enable: whether to enable GTS for the context
 *	@type: the egress context type
 *	@respq: associated response queue
 *	@base_addr: base address of queue
 *	@size: number of queue entries
 *	@token: uP token
 *	@gen: initial generation value for the context
 *	@cidx: consumer pointer
 *
 *	Initialize an SGE egress context and make it ready for use.  If the
 *	platform allows concurrent context operations, the caller is
 *	responsible for appropriate locking.
 */
int t3_sge_init_ecntxt(adapter_t *adapter, unsigned int id, int gts_enable,
		       enum sge_context_type type, int respq, u64 base_addr,
		       unsigned int size, unsigned int token, int gen,
		       unsigned int cidx)
{
	unsigned int credits = type == SGE_CNTXT_OFLD ? 0 : FW_WR_NUM;

	if (base_addr & 0xfff)     /* must be 4K aligned */
		return -EINVAL;
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	base_addr >>= 12;
	t3_write_reg(adapter, A_SG_CONTEXT_DATA0, V_EC_INDEX(cidx) |
		     V_EC_CREDITS(credits) | V_EC_GTS(gts_enable));
	t3_write_reg(adapter, A_SG_CONTEXT_DATA1, V_EC_SIZE(size) |
		     V_EC_BASE_LO((u32)base_addr & 0xffff));
	base_addr >>= 16;
	t3_write_reg(adapter, A_SG_CONTEXT_DATA2, (u32)base_addr);
	base_addr >>= 32;
	t3_write_reg(adapter, A_SG_CONTEXT_DATA3,
		     V_EC_BASE_HI((u32)base_addr & 0xf) | V_EC_RESPQ(respq) |
		     V_EC_TYPE(type) | V_EC_GEN(gen) | V_EC_UP_TOKEN(token) |
		     F_EC_VALID);
	return t3_sge_write_context(adapter, id, F_EGRESS);
}

/**
 *	t3_sge_init_flcntxt - initialize an SGE free-buffer list context
 *	@adapter: the adapter to configure
 *	@id: the context id
 *	@gts_enable: whether to enable GTS for the context
 *	@base_addr: base address of queue
 *	@size: number of queue entries
 *	@bsize: size of each buffer for this queue
 *	@cong_thres: threshold to signal congestion to upstream producers
 *	@gen: initial generation value for the context
 *	@cidx: consumer pointer
 *
 *	Initialize an SGE free list context and make it ready for use.  The
 *	caller is responsible for ensuring only one context operation occurs
 *	at a time.
 */
int t3_sge_init_flcntxt(adapter_t *adapter, unsigned int id, int gts_enable,
			u64 base_addr, unsigned int size, unsigned int bsize,
			unsigned int cong_thres, int gen, unsigned int cidx)
{
	if (base_addr & 0xfff)     /* must be 4K aligned */
		return -EINVAL;
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	base_addr >>= 12;
	t3_write_reg(adapter, A_SG_CONTEXT_DATA0, (u32)base_addr);
	base_addr >>= 32;
	t3_write_reg(adapter, A_SG_CONTEXT_DATA1,
		     V_FL_BASE_HI((u32)base_addr) |
		     V_FL_INDEX_LO(cidx & M_FL_INDEX_LO));
	t3_write_reg(adapter, A_SG_CONTEXT_DATA2, V_FL_SIZE(size) |
		     V_FL_GEN(gen) | V_FL_INDEX_HI(cidx >> 12) |
		     V_FL_ENTRY_SIZE_LO(bsize & M_FL_ENTRY_SIZE_LO));
	t3_write_reg(adapter, A_SG_CONTEXT_DATA3,
		     V_FL_ENTRY_SIZE_HI(bsize >> (32 - S_FL_ENTRY_SIZE_LO)) |
		     V_FL_CONG_THRES(cong_thres) | V_FL_GTS(gts_enable));
	return t3_sge_write_context(adapter, id, F_FREELIST);
}

/**
 *	t3_sge_init_rspcntxt - initialize an SGE response queue context
 *	@adapter: the adapter to configure
 *	@id: the context id
 *	@irq_vec_idx: MSI-X interrupt vector index, 0 if no MSI-X, -1 if no IRQ
 *	@base_addr: base address of queue
 *	@size: number of queue entries
 *	@fl_thres: threshold for selecting the normal or jumbo free list
 *	@gen: initial generation value for the context
 *	@cidx: consumer pointer
 *
 *	Initialize an SGE response queue context and make it ready for use.
 *	The caller is responsible for ensuring only one context operation
 *	occurs at a time.
 */
int t3_sge_init_rspcntxt(adapter_t *adapter, unsigned int id, int irq_vec_idx,
			 u64 base_addr, unsigned int size,
			 unsigned int fl_thres, int gen, unsigned int cidx)
{
	unsigned int ctrl, intr = 0;

	if (base_addr & 0xfff)     /* must be 4K aligned */
		return -EINVAL;
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	base_addr >>= 12;
	t3_write_reg(adapter, A_SG_CONTEXT_DATA0, V_CQ_SIZE(size) |
		     V_CQ_INDEX(cidx));
	t3_write_reg(adapter, A_SG_CONTEXT_DATA1, (u32)base_addr);
	base_addr >>= 32;
        ctrl = t3_read_reg(adapter, A_SG_CONTROL);
        if ((irq_vec_idx > 0) ||
		((irq_vec_idx == 0) && !(ctrl & F_ONEINTMULTQ)))
                	intr = F_RQ_INTR_EN;
        if (irq_vec_idx >= 0)
                intr |= V_RQ_MSI_VEC(irq_vec_idx);
	t3_write_reg(adapter, A_SG_CONTEXT_DATA2,
		     V_CQ_BASE_HI((u32)base_addr) | intr | V_RQ_GEN(gen));
	t3_write_reg(adapter, A_SG_CONTEXT_DATA3, fl_thres);
	return t3_sge_write_context(adapter, id, F_RESPONSEQ);
}

/**
 *	t3_sge_init_cqcntxt - initialize an SGE completion queue context
 *	@adapter: the adapter to configure
 *	@id: the context id
 *	@base_addr: base address of queue
 *	@size: number of queue entries
 *	@rspq: response queue for async notifications
 *	@ovfl_mode: CQ overflow mode
 *	@credits: completion queue credits
 *	@credit_thres: the credit threshold
 *
 *	Initialize an SGE completion queue context and make it ready for use.
 *	The caller is responsible for ensuring only one context operation
 *	occurs at a time.
 */
int t3_sge_init_cqcntxt(adapter_t *adapter, unsigned int id, u64 base_addr,
			unsigned int size, int rspq, int ovfl_mode,
			unsigned int credits, unsigned int credit_thres)
{
	if (base_addr & 0xfff)     /* must be 4K aligned */
		return -EINVAL;
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	base_addr >>= 12;
	t3_write_reg(adapter, A_SG_CONTEXT_DATA0, V_CQ_SIZE(size));
	t3_write_reg(adapter, A_SG_CONTEXT_DATA1, (u32)base_addr);
	base_addr >>= 32;
	t3_write_reg(adapter, A_SG_CONTEXT_DATA2,
		     V_CQ_BASE_HI((u32)base_addr) | V_CQ_RSPQ(rspq) |
		     V_CQ_GEN(1) | V_CQ_OVERFLOW_MODE(ovfl_mode) |
		     V_CQ_ERR(ovfl_mode));
	t3_write_reg(adapter, A_SG_CONTEXT_DATA3, V_CQ_CREDITS(credits) |
		     V_CQ_CREDIT_THRES(credit_thres));
	return t3_sge_write_context(adapter, id, F_CQ);
}

/**
 *	t3_sge_enable_ecntxt - enable/disable an SGE egress context
 *	@adapter: the adapter
 *	@id: the egress context id
 *	@enable: enable (1) or disable (0) the context
 *
 *	Enable or disable an SGE egress context.  The caller is responsible for
 *	ensuring only one context operation occurs at a time.
 */
int t3_sge_enable_ecntxt(adapter_t *adapter, unsigned int id, int enable)
{
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	t3_write_reg(adapter, A_SG_CONTEXT_MASK0, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK1, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK2, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK3, F_EC_VALID);
	t3_write_reg(adapter, A_SG_CONTEXT_DATA3, V_EC_VALID(enable));
	t3_write_reg(adapter, A_SG_CONTEXT_CMD,
		     V_CONTEXT_CMD_OPCODE(1) | F_EGRESS | V_CONTEXT(id));
	return t3_wait_op_done(adapter, A_SG_CONTEXT_CMD, F_CONTEXT_CMD_BUSY,
			       0, SG_CONTEXT_CMD_ATTEMPTS, 1);
}

/**
 *	t3_sge_disable_fl - disable an SGE free-buffer list
 *	@adapter: the adapter
 *	@id: the free list context id
 *
 *	Disable an SGE free-buffer list.  The caller is responsible for
 *	ensuring only one context operation occurs at a time.
 */
int t3_sge_disable_fl(adapter_t *adapter, unsigned int id)
{
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	t3_write_reg(adapter, A_SG_CONTEXT_MASK0, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK1, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK2, V_FL_SIZE(M_FL_SIZE));
	t3_write_reg(adapter, A_SG_CONTEXT_MASK3, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_DATA2, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_CMD,
		     V_CONTEXT_CMD_OPCODE(1) | F_FREELIST | V_CONTEXT(id));
	return t3_wait_op_done(adapter, A_SG_CONTEXT_CMD, F_CONTEXT_CMD_BUSY,
			       0, SG_CONTEXT_CMD_ATTEMPTS, 1);
}

/**
 *	t3_sge_disable_rspcntxt - disable an SGE response queue
 *	@adapter: the adapter
 *	@id: the response queue context id
 *
 *	Disable an SGE response queue.  The caller is responsible for
 *	ensuring only one context operation occurs at a time.
 */
int t3_sge_disable_rspcntxt(adapter_t *adapter, unsigned int id)
{
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	t3_write_reg(adapter, A_SG_CONTEXT_MASK0, V_CQ_SIZE(M_CQ_SIZE));
	t3_write_reg(adapter, A_SG_CONTEXT_MASK1, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK2, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK3, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_DATA0, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_CMD,
		     V_CONTEXT_CMD_OPCODE(1) | F_RESPONSEQ | V_CONTEXT(id));
	return t3_wait_op_done(adapter, A_SG_CONTEXT_CMD, F_CONTEXT_CMD_BUSY,
			       0, SG_CONTEXT_CMD_ATTEMPTS, 1);
}

/**
 *	t3_sge_disable_cqcntxt - disable an SGE completion queue
 *	@adapter: the adapter
 *	@id: the completion queue context id
 *
 *	Disable an SGE completion queue.  The caller is responsible for
 *	ensuring only one context operation occurs at a time.
 */
int t3_sge_disable_cqcntxt(adapter_t *adapter, unsigned int id)
{
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	t3_write_reg(adapter, A_SG_CONTEXT_MASK0, V_CQ_SIZE(M_CQ_SIZE));
	t3_write_reg(adapter, A_SG_CONTEXT_MASK1, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK2, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_MASK3, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_DATA0, 0);
	t3_write_reg(adapter, A_SG_CONTEXT_CMD,
		     V_CONTEXT_CMD_OPCODE(1) | F_CQ | V_CONTEXT(id));
	return t3_wait_op_done(adapter, A_SG_CONTEXT_CMD, F_CONTEXT_CMD_BUSY,
			       0, SG_CONTEXT_CMD_ATTEMPTS, 1);
}

/**
 *	t3_sge_cqcntxt_op - perform an operation on a completion queue context
 *	@adapter: the adapter
 *	@id: the context id
 *	@op: the operation to perform
 *	@credits: credits to return to the CQ
 *
 *	Perform the selected operation on an SGE completion queue context.
 *	The caller is responsible for ensuring only one context operation
 *	occurs at a time.
 *
 *	For most operations the function returns the current HW position in
 *	the completion queue.
 */
int t3_sge_cqcntxt_op(adapter_t *adapter, unsigned int id, unsigned int op,
		      unsigned int credits)
{
	u32 val;

	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	t3_write_reg(adapter, A_SG_CONTEXT_DATA0, credits << 16);
	t3_write_reg(adapter, A_SG_CONTEXT_CMD, V_CONTEXT_CMD_OPCODE(op) |
		     V_CONTEXT(id) | F_CQ);
	if (t3_wait_op_done_val(adapter, A_SG_CONTEXT_CMD, F_CONTEXT_CMD_BUSY,
				0, SG_CONTEXT_CMD_ATTEMPTS, 1, &val))
		return -EIO;

	if (op >= 2 && op < 7) {
		if (adapter->params.rev > 0)
			return G_CQ_INDEX(val);

		t3_write_reg(adapter, A_SG_CONTEXT_CMD,
			     V_CONTEXT_CMD_OPCODE(0) | F_CQ | V_CONTEXT(id));
		if (t3_wait_op_done(adapter, A_SG_CONTEXT_CMD,
				    F_CONTEXT_CMD_BUSY, 0,
				    SG_CONTEXT_CMD_ATTEMPTS, 1))
			return -EIO;
		return G_CQ_INDEX(t3_read_reg(adapter, A_SG_CONTEXT_DATA0));
	}
	return 0;
}

/**
 * 	t3_sge_read_context - read an SGE context
 * 	@type: the context type
 * 	@adapter: the adapter
 * 	@id: the context id
 * 	@data: holds the retrieved context
 *
 * 	Read an SGE egress context.  The caller is responsible for ensuring
 * 	only one context operation occurs at a time.
 */
static int t3_sge_read_context(unsigned int type, adapter_t *adapter,
			       unsigned int id, u32 data[4])
{
	if (t3_read_reg(adapter, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	t3_write_reg(adapter, A_SG_CONTEXT_CMD,
		     V_CONTEXT_CMD_OPCODE(0) | type | V_CONTEXT(id));
	if (t3_wait_op_done(adapter, A_SG_CONTEXT_CMD, F_CONTEXT_CMD_BUSY, 0,
			    SG_CONTEXT_CMD_ATTEMPTS, 1))
		return -EIO;
	data[0] = t3_read_reg(adapter, A_SG_CONTEXT_DATA0);
	data[1] = t3_read_reg(adapter, A_SG_CONTEXT_DATA1);
	data[2] = t3_read_reg(adapter, A_SG_CONTEXT_DATA2);
	data[3] = t3_read_reg(adapter, A_SG_CONTEXT_DATA3);
	return 0;
}

/**
 * 	t3_sge_read_ecntxt - read an SGE egress context
 * 	@adapter: the adapter
 * 	@id: the context id
 * 	@data: holds the retrieved context
 *
 * 	Read an SGE egress context.  The caller is responsible for ensuring
 * 	only one context operation occurs at a time.
 */
int t3_sge_read_ecntxt(adapter_t *adapter, unsigned int id, u32 data[4])
{
	if (id >= 65536)
		return -EINVAL;
	return t3_sge_read_context(F_EGRESS, adapter, id, data);
}

/**
 * 	t3_sge_read_cq - read an SGE CQ context
 * 	@adapter: the adapter
 * 	@id: the context id
 * 	@data: holds the retrieved context
 *
 * 	Read an SGE CQ context.  The caller is responsible for ensuring
 * 	only one context operation occurs at a time.
 */
int t3_sge_read_cq(adapter_t *adapter, unsigned int id, u32 data[4])
{
	if (id >= 65536)
		return -EINVAL;
	return t3_sge_read_context(F_CQ, adapter, id, data);
}

/**
 * 	t3_sge_read_fl - read an SGE free-list context
 * 	@adapter: the adapter
 * 	@id: the context id
 * 	@data: holds the retrieved context
 *
 * 	Read an SGE free-list context.  The caller is responsible for ensuring
 * 	only one context operation occurs at a time.
 */
int t3_sge_read_fl(adapter_t *adapter, unsigned int id, u32 data[4])
{
	if (id >= SGE_QSETS * 2)
		return -EINVAL;
	return t3_sge_read_context(F_FREELIST, adapter, id, data);
}

/**
 * 	t3_sge_read_rspq - read an SGE response queue context
 * 	@adapter: the adapter
 * 	@id: the context id
 * 	@data: holds the retrieved context
 *
 * 	Read an SGE response queue context.  The caller is responsible for
 * 	ensuring only one context operation occurs at a time.
 */
int t3_sge_read_rspq(adapter_t *adapter, unsigned int id, u32 data[4])
{
	if (id >= SGE_QSETS)
		return -EINVAL;
	return t3_sge_read_context(F_RESPONSEQ, adapter, id, data);
}

/**
 *	t3_config_rss - configure Rx packet steering
 *	@adapter: the adapter
 *	@rss_config: RSS settings (written to TP_RSS_CONFIG)
 *	@cpus: values for the CPU lookup table (0xff terminated)
 *	@rspq: values for the response queue lookup table (0xffff terminated)
 *
 *	Programs the receive packet steering logic.  @cpus and @rspq provide
 *	the values for the CPU and response queue lookup tables.  If they
 *	provide fewer values than the size of the tables the supplied values
 *	are used repeatedly until the tables are fully populated.
 */
void t3_config_rss(adapter_t *adapter, unsigned int rss_config, const u8 *cpus,
		   const u16 *rspq)
{
	int i, j, cpu_idx = 0, q_idx = 0;

	if (cpus)
		for (i = 0; i < RSS_TABLE_SIZE; ++i) {
			u32 val = i << 16;

			for (j = 0; j < 2; ++j) {
				val |= (cpus[cpu_idx++] & 0x3f) << (8 * j);
				if (cpus[cpu_idx] == 0xff)
					cpu_idx = 0;
			}
			t3_write_reg(adapter, A_TP_RSS_LKP_TABLE, val);
		}

	if (rspq)
		for (i = 0; i < RSS_TABLE_SIZE; ++i) {
			t3_write_reg(adapter, A_TP_RSS_MAP_TABLE,
				     (i << 16) | rspq[q_idx++]);
			if (rspq[q_idx] == 0xffff)
				q_idx = 0;
		}

	t3_write_reg(adapter, A_TP_RSS_CONFIG, rss_config);
}

/**
 *	t3_read_rss - read the contents of the RSS tables
 *	@adapter: the adapter
 *	@lkup: holds the contents of the RSS lookup table
 *	@map: holds the contents of the RSS map table
 *
 *	Reads the contents of the receive packet steering tables.
 */
int t3_read_rss(adapter_t *adapter, u8 *lkup, u16 *map)
{
	int i;
	u32 val;

	if (lkup)
		for (i = 0; i < RSS_TABLE_SIZE; ++i) {
			t3_write_reg(adapter, A_TP_RSS_LKP_TABLE,
				     0xffff0000 | i);
			val = t3_read_reg(adapter, A_TP_RSS_LKP_TABLE);
			if (!(val & 0x80000000))
				return -EAGAIN;
			*lkup++ = (u8)val;
			*lkup++ = (u8)(val >> 8);
		}

	if (map)
		for (i = 0; i < RSS_TABLE_SIZE; ++i) {
			t3_write_reg(adapter, A_TP_RSS_MAP_TABLE,
				     0xffff0000 | i);
			val = t3_read_reg(adapter, A_TP_RSS_MAP_TABLE);
			if (!(val & 0x80000000))
				return -EAGAIN;
			*map++ = (u16)val;
		}
	return 0;
}

/**
 *	t3_tp_set_offload_mode - put TP in NIC/offload mode
 *	@adap: the adapter
 *	@enable: 1 to select offload mode, 0 for regular NIC
 *
 *	Switches TP to NIC/offload mode.
 */
void t3_tp_set_offload_mode(adapter_t *adap, int enable)
{
	if (is_offload(adap) || !enable)
		t3_set_reg_field(adap, A_TP_IN_CONFIG, F_NICMODE,
				 V_NICMODE(!enable));
}

/**
 *	tp_wr_bits_indirect - set/clear bits in an indirect TP register
 *	@adap: the adapter
 *	@addr: the indirect TP register address
 *	@mask: specifies the field within the register to modify
 *	@val: new value for the field
 *
 *	Sets a field of an indirect TP register to the given value.
 */
static void tp_wr_bits_indirect(adapter_t *adap, unsigned int addr,
				unsigned int mask, unsigned int val)
{
	t3_write_reg(adap, A_TP_PIO_ADDR, addr);
	val |= t3_read_reg(adap, A_TP_PIO_DATA) & ~mask;
	t3_write_reg(adap, A_TP_PIO_DATA, val);
}

/**
 *	t3_enable_filters - enable the HW filters
 *	@adap: the adapter
 *
 *	Enables the HW filters for NIC traffic.
 */
void t3_enable_filters(adapter_t *adap)
{
	t3_set_reg_field(adap, A_TP_IN_CONFIG, F_NICMODE, 0);
	t3_set_reg_field(adap, A_MC5_DB_CONFIG, 0, F_FILTEREN);
	t3_set_reg_field(adap, A_TP_GLOBAL_CONFIG, 0, V_FIVETUPLELOOKUP(3));
	tp_wr_bits_indirect(adap, A_TP_INGRESS_CONFIG, 0, F_LOOKUPEVERYPKT);
}

/**
 *	t3_disable_filters - disable the HW filters
 *	@adap: the adapter
 *
 *	Disables the HW filters for NIC traffic.
 */
void t3_disable_filters(adapter_t *adap)
{
	/* note that we don't want to revert to NIC-only mode */
	t3_set_reg_field(adap, A_MC5_DB_CONFIG, F_FILTEREN, 0);
	t3_set_reg_field(adap, A_TP_GLOBAL_CONFIG,
			 V_FIVETUPLELOOKUP(M_FIVETUPLELOOKUP), 0);
	tp_wr_bits_indirect(adap, A_TP_INGRESS_CONFIG, F_LOOKUPEVERYPKT, 0);
}

/**
 *	pm_num_pages - calculate the number of pages of the payload memory
 *	@mem_size: the size of the payload memory
 *	@pg_size: the size of each payload memory page
 *
 *	Calculate the number of pages, each of the given size, that fit in a
 *	memory of the specified size, respecting the HW requirement that the
 *	number of pages must be a multiple of 24.
 */
static inline unsigned int pm_num_pages(unsigned int mem_size,
					unsigned int pg_size)
{
	unsigned int n = mem_size / pg_size;

	return n - n % 24;
}

#define mem_region(adap, start, size, reg) \
	t3_write_reg((adap), A_ ## reg, (start)); \
	start += size

/**
 *	partition_mem - partition memory and configure TP memory settings
 *	@adap: the adapter
 *	@p: the TP parameters
 *
 *	Partitions context and payload memory and configures TP's memory
 *	registers.
 */
static void partition_mem(adapter_t *adap, const struct tp_params *p)
{
	unsigned int m, pstructs, tids = t3_mc5_size(&adap->mc5);
	unsigned int timers = 0, timers_shift = 22;

	if (adap->params.rev > 0) {
		if (tids <= 16 * 1024) {
			timers = 1;
			timers_shift = 16;
		} else if (tids <= 64 * 1024) {
			timers = 2;
			timers_shift = 18;
		} else if (tids <= 256 * 1024) {
			timers = 3;
			timers_shift = 20;
		}
	}

	t3_write_reg(adap, A_TP_PMM_SIZE,
		     p->chan_rx_size | (p->chan_tx_size >> 16));

	t3_write_reg(adap, A_TP_PMM_TX_BASE, 0);
	t3_write_reg(adap, A_TP_PMM_TX_PAGE_SIZE, p->tx_pg_size);
	t3_write_reg(adap, A_TP_PMM_TX_MAX_PAGE, p->tx_num_pgs);
	t3_set_reg_field(adap, A_TP_PARA_REG3, V_TXDATAACKIDX(M_TXDATAACKIDX),
			 V_TXDATAACKIDX(fls(p->tx_pg_size) - 12));

	t3_write_reg(adap, A_TP_PMM_RX_BASE, 0);
	t3_write_reg(adap, A_TP_PMM_RX_PAGE_SIZE, p->rx_pg_size);
	t3_write_reg(adap, A_TP_PMM_RX_MAX_PAGE, p->rx_num_pgs);

	pstructs = p->rx_num_pgs + p->tx_num_pgs;
	/* Add a bit of headroom and make multiple of 24 */
	pstructs += 48;
	pstructs -= pstructs % 24;
	t3_write_reg(adap, A_TP_CMM_MM_MAX_PSTRUCT, pstructs);

	m = tids * TCB_SIZE;
	mem_region(adap, m, (64 << 10) * 64, SG_EGR_CNTX_BADDR);
	mem_region(adap, m, (64 << 10) * 64, SG_CQ_CONTEXT_BADDR);
	t3_write_reg(adap, A_TP_CMM_TIMER_BASE, V_CMTIMERMAXNUM(timers) | m);
	m += ((p->ntimer_qs - 1) << timers_shift) + (1 << 22);
	mem_region(adap, m, pstructs * 64, TP_CMM_MM_BASE);
	mem_region(adap, m, 64 * (pstructs / 24), TP_CMM_MM_PS_FLST_BASE);
	mem_region(adap, m, 64 * (p->rx_num_pgs / 24), TP_CMM_MM_RX_FLST_BASE);
	mem_region(adap, m, 64 * (p->tx_num_pgs / 24), TP_CMM_MM_TX_FLST_BASE);

	m = (m + 4095) & ~0xfff;
	t3_write_reg(adap, A_CIM_SDRAM_BASE_ADDR, m);
	t3_write_reg(adap, A_CIM_SDRAM_ADDR_SIZE, p->cm_size - m);

	tids = (p->cm_size - m - (3 << 20)) / 3072 - 32;
	m = t3_mc5_size(&adap->mc5) - adap->params.mc5.nservers -
	    adap->params.mc5.nfilters - adap->params.mc5.nroutes;
	if (tids < m)
		adap->params.mc5.nservers += m - tids;
}

static inline void tp_wr_indirect(adapter_t *adap, unsigned int addr, u32 val)
{
	t3_write_reg(adap, A_TP_PIO_ADDR, addr);
	t3_write_reg(adap, A_TP_PIO_DATA, val);
}

static inline u32 tp_rd_indirect(adapter_t *adap, unsigned int addr)
{
	t3_write_reg(adap, A_TP_PIO_ADDR, addr);
	return t3_read_reg(adap, A_TP_PIO_DATA);
}

static void tp_config(adapter_t *adap, const struct tp_params *p)
{
	t3_write_reg(adap, A_TP_GLOBAL_CONFIG, F_TXPACINGENABLE | F_PATHMTU |
		     F_IPCHECKSUMOFFLOAD | F_UDPCHECKSUMOFFLOAD |
		     F_TCPCHECKSUMOFFLOAD | V_IPTTL(64));
	t3_write_reg(adap, A_TP_TCP_OPTIONS, V_MTUDEFAULT(576) |
		     F_MTUENABLE | V_WINDOWSCALEMODE(1) |
		     V_TIMESTAMPSMODE(1) | V_SACKMODE(1) | V_SACKRX(1));
	t3_write_reg(adap, A_TP_DACK_CONFIG, V_AUTOSTATE3(1) |
		     V_AUTOSTATE2(1) | V_AUTOSTATE1(0) |
		     V_BYTETHRESHOLD(26880) | V_MSSTHRESHOLD(2) |
		     F_AUTOCAREFUL | F_AUTOENABLE | V_DACK_MODE(1));
	t3_set_reg_field(adap, A_TP_IN_CONFIG, F_RXFBARBPRIO | F_TXFBARBPRIO,
			 F_IPV6ENABLE | F_NICMODE);
	t3_write_reg(adap, A_TP_TX_RESOURCE_LIMIT, 0x18141814);
	t3_write_reg(adap, A_TP_PARA_REG4, 0x5050105);
	t3_set_reg_field(adap, A_TP_PARA_REG6, 0,
			 adap->params.rev > 0 ? F_ENABLEESND :
			 			F_T3A_ENABLEESND);
	t3_set_reg_field(adap, A_TP_PC_CONFIG,
			 F_ENABLEEPCMDAFULL,
			 F_ENABLEOCSPIFULL |F_TXDEFERENABLE | F_HEARBEATDACK |
			 F_TXCONGESTIONMODE | F_RXCONGESTIONMODE);
	t3_set_reg_field(adap, A_TP_PC_CONFIG2, F_CHDRAFULL,
			 F_ENABLEIPV6RSS | F_ENABLENONOFDTNLSYN |
			 F_ENABLEARPMISS | F_DISBLEDAPARBIT0);
	t3_write_reg(adap, A_TP_PROXY_FLOW_CNTL, 1080);
	t3_write_reg(adap, A_TP_PROXY_FLOW_CNTL, 1000);

	if (adap->params.rev > 0) {
		tp_wr_indirect(adap, A_TP_EGRESS_CONFIG, F_REWRITEFORCETOSIZE);
		t3_set_reg_field(adap, A_TP_PARA_REG3, 0,
				 F_TXPACEAUTO | F_TXPACEAUTOSTRICT);
		t3_set_reg_field(adap, A_TP_PC_CONFIG, F_LOCKTID, F_LOCKTID);
		tp_wr_indirect(adap, A_TP_VLAN_PRI_MAP, 0xfa50);
		tp_wr_indirect(adap, A_TP_MAC_MATCH_MAP0, 0xfac688);
		tp_wr_indirect(adap, A_TP_MAC_MATCH_MAP1, 0xfac688);
	} else
		t3_set_reg_field(adap, A_TP_PARA_REG3, 0, F_TXPACEFIXED);

	if (adap->params.rev == T3_REV_C)
		t3_set_reg_field(adap, A_TP_PC_CONFIG,
				 V_TABLELATENCYDELTA(M_TABLELATENCYDELTA),
				 V_TABLELATENCYDELTA(4));

	t3_write_reg(adap, A_TP_TX_MOD_QUEUE_WEIGHT1, 0);
	t3_write_reg(adap, A_TP_TX_MOD_QUEUE_WEIGHT0, 0);
	t3_write_reg(adap, A_TP_MOD_CHANNEL_WEIGHT, 0);
	t3_write_reg(adap, A_TP_MOD_RATE_LIMIT, 0xf2200000);

	if (adap->params.nports > 2) {
		t3_set_reg_field(adap, A_TP_PC_CONFIG2, 0,
				 F_ENABLETXPORTFROMDA2 | F_ENABLETXPORTFROMDA |
				 F_ENABLERXPORTFROMADDR);
		tp_wr_bits_indirect(adap, A_TP_QOS_RX_MAP_MODE,
				    V_RXMAPMODE(M_RXMAPMODE), 0);
		tp_wr_indirect(adap, A_TP_INGRESS_CONFIG, V_BITPOS0(48) |
			       V_BITPOS1(49) | V_BITPOS2(50) | V_BITPOS3(51) |
			       F_ENABLEEXTRACT | F_ENABLEEXTRACTIONSFD |
			       F_ENABLEINSERTION | F_ENABLEINSERTIONSFD);
		tp_wr_indirect(adap, A_TP_PREAMBLE_MSB, 0xfb000000);
		tp_wr_indirect(adap, A_TP_PREAMBLE_LSB, 0xd5);
		tp_wr_indirect(adap, A_TP_INTF_FROM_TX_PKT, F_INTFFROMTXPKT);
	}
}

/* TCP timer values in ms */
#define TP_DACK_TIMER 50
#define TP_RTO_MIN    250

/**
 *	tp_set_timers - set TP timing parameters
 *	@adap: the adapter to set
 *	@core_clk: the core clock frequency in Hz
 *
 *	Set TP's timing parameters, such as the various timer resolutions and
 *	the TCP timer values.
 */
static void tp_set_timers(adapter_t *adap, unsigned int core_clk)
{
	unsigned int tre = adap->params.tp.tre;
	unsigned int dack_re = adap->params.tp.dack_re;
	unsigned int tstamp_re = fls(core_clk / 1000);     /* 1ms, at least */
	unsigned int tps = core_clk >> tre;

	t3_write_reg(adap, A_TP_TIMER_RESOLUTION, V_TIMERRESOLUTION(tre) |
		     V_DELAYEDACKRESOLUTION(dack_re) |
		     V_TIMESTAMPRESOLUTION(tstamp_re));
	t3_write_reg(adap, A_TP_DACK_TIMER,
		     (core_clk >> dack_re) / (1000 / TP_DACK_TIMER));
	t3_write_reg(adap, A_TP_TCP_BACKOFF_REG0, 0x3020100);
	t3_write_reg(adap, A_TP_TCP_BACKOFF_REG1, 0x7060504);
	t3_write_reg(adap, A_TP_TCP_BACKOFF_REG2, 0xb0a0908);
	t3_write_reg(adap, A_TP_TCP_BACKOFF_REG3, 0xf0e0d0c);
	t3_write_reg(adap, A_TP_SHIFT_CNT, V_SYNSHIFTMAX(6) |
		     V_RXTSHIFTMAXR1(4) | V_RXTSHIFTMAXR2(15) |
		     V_PERSHIFTBACKOFFMAX(8) | V_PERSHIFTMAX(8) |
		     V_KEEPALIVEMAX(9));

#define SECONDS * tps

	t3_write_reg(adap, A_TP_MSL,
		     adap->params.rev > 0 ? 0 : 2 SECONDS);
	t3_write_reg(adap, A_TP_RXT_MIN, tps / (1000 / TP_RTO_MIN));
	t3_write_reg(adap, A_TP_RXT_MAX, 64 SECONDS);
	t3_write_reg(adap, A_TP_PERS_MIN, 5 SECONDS);
	t3_write_reg(adap, A_TP_PERS_MAX, 64 SECONDS);
	t3_write_reg(adap, A_TP_KEEP_IDLE, 7200 SECONDS);
	t3_write_reg(adap, A_TP_KEEP_INTVL, 75 SECONDS);
	t3_write_reg(adap, A_TP_INIT_SRTT, 3 SECONDS);
	t3_write_reg(adap, A_TP_FINWAIT2_TIMER, 600 SECONDS);

#undef SECONDS
}

/**
 *	t3_tp_set_coalescing_size - set receive coalescing size
 *	@adap: the adapter
 *	@size: the receive coalescing size
 *	@psh: whether a set PSH bit should deliver coalesced data
 *
 *	Set the receive coalescing size and PSH bit handling.
 */
int t3_tp_set_coalescing_size(adapter_t *adap, unsigned int size, int psh)
{
	u32 val;

	if (size > MAX_RX_COALESCING_LEN)
		return -EINVAL;

	val = t3_read_reg(adap, A_TP_PARA_REG3);
	val &= ~(F_RXCOALESCEENABLE | F_RXCOALESCEPSHEN);

	if (size) {
		val |= F_RXCOALESCEENABLE;
		if (psh)
			val |= F_RXCOALESCEPSHEN;
		size = min(MAX_RX_COALESCING_LEN, size);
		t3_write_reg(adap, A_TP_PARA_REG2, V_RXCOALESCESIZE(size) |
			     V_MAXRXDATA(MAX_RX_COALESCING_LEN));
	}
	t3_write_reg(adap, A_TP_PARA_REG3, val);
	return 0;
}

/**
 *	t3_tp_set_max_rxsize - set the max receive size
 *	@adap: the adapter
 *	@size: the max receive size
 *
 *	Set TP's max receive size.  This is the limit that applies when
 *	receive coalescing is disabled.
 */
void t3_tp_set_max_rxsize(adapter_t *adap, unsigned int size)
{
	t3_write_reg(adap, A_TP_PARA_REG7,
		     V_PMMAXXFERLEN0(size) | V_PMMAXXFERLEN1(size));
}

static void __devinit init_mtus(unsigned short mtus[])
{
	/*
	 * See draft-mathis-plpmtud-00.txt for the values.  The min is 88 so
	 * it can accommodate max size TCP/IP headers when SACK and timestamps
	 * are enabled and still have at least 8 bytes of payload.
	 */
	mtus[0] = 88;
	mtus[1] = 88;
	mtus[2] = 256;
	mtus[3] = 512;
	mtus[4] = 576;
	mtus[5] = 1024;
	mtus[6] = 1280;
	mtus[7] = 1492;
	mtus[8] = 1500;
	mtus[9] = 2002;
	mtus[10] = 2048;
	mtus[11] = 4096;
	mtus[12] = 4352;
	mtus[13] = 8192;
	mtus[14] = 9000;
	mtus[15] = 9600;
}

/**
 *	init_cong_ctrl - initialize congestion control parameters
 *	@a: the alpha values for congestion control
 *	@b: the beta values for congestion control
 *
 *	Initialize the congestion control parameters.
 */
static void __devinit init_cong_ctrl(unsigned short *a, unsigned short *b)
{
	a[0] = a[1] = a[2] = a[3] = a[4] = a[5] = a[6] = a[7] = a[8] = 1;
	a[9] = 2;
	a[10] = 3;
	a[11] = 4;
	a[12] = 5;
	a[13] = 6;
	a[14] = 7;
	a[15] = 8;
	a[16] = 9;
	a[17] = 10;
	a[18] = 14;
	a[19] = 17;
	a[20] = 21;
	a[21] = 25;
	a[22] = 30;
	a[23] = 35;
	a[24] = 45;
	a[25] = 60;
	a[26] = 80;
	a[27] = 100;
	a[28] = 200;
	a[29] = 300;
	a[30] = 400;
	a[31] = 500;

	b[0] = b[1] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = b[8] = 0;
	b[9] = b[10] = 1;
	b[11] = b[12] = 2;
	b[13] = b[14] = b[15] = b[16] = 3;
	b[17] = b[18] = b[19] = b[20] = b[21] = 4;
	b[22] = b[23] = b[24] = b[25] = b[26] = b[27] = 5;
	b[28] = b[29] = 6;
	b[30] = b[31] = 7;
}

/* The minimum additive increment value for the congestion control table */
#define CC_MIN_INCR 2U

/**
 *	t3_load_mtus - write the MTU and congestion control HW tables
 *	@adap: the adapter
 *	@mtus: the unrestricted values for the MTU table
 *	@alpha: the values for the congestion control alpha parameter
 *	@beta: the values for the congestion control beta parameter
 *	@mtu_cap: the maximum permitted effective MTU
 *
 *	Write the MTU table with the supplied MTUs capping each at &mtu_cap.
 *	Update the high-speed congestion control table with the supplied alpha,
 * 	beta, and MTUs.
 */
void t3_load_mtus(adapter_t *adap, unsigned short mtus[NMTUS],
		  unsigned short alpha[NCCTRL_WIN],
		  unsigned short beta[NCCTRL_WIN], unsigned short mtu_cap)
{
	static const unsigned int avg_pkts[NCCTRL_WIN] = {
		2, 6, 10, 14, 20, 28, 40, 56, 80, 112, 160, 224, 320, 448, 640,
		896, 1281, 1792, 2560, 3584, 5120, 7168, 10240, 14336, 20480,
		28672, 40960, 57344, 81920, 114688, 163840, 229376 };

	unsigned int i, w;

	for (i = 0; i < NMTUS; ++i) {
		unsigned int mtu = min(mtus[i], mtu_cap);
		unsigned int log2 = fls(mtu);

		if (!(mtu & ((1 << log2) >> 2)))     /* round */
			log2--;
		t3_write_reg(adap, A_TP_MTU_TABLE,
			     (i << 24) | (log2 << 16) | mtu);

		for (w = 0; w < NCCTRL_WIN; ++w) {
			unsigned int inc;

			inc = max(((mtu - 40) * alpha[w]) / avg_pkts[w],
				  CC_MIN_INCR);

			t3_write_reg(adap, A_TP_CCTRL_TABLE, (i << 21) |
				     (w << 16) | (beta[w] << 13) | inc);
		}
	}
}

/**
 *	t3_read_hw_mtus - returns the values in the HW MTU table
 *	@adap: the adapter
 *	@mtus: where to store the HW MTU values
 *
 *	Reads the HW MTU table.
 */
void t3_read_hw_mtus(adapter_t *adap, unsigned short mtus[NMTUS])
{
	int i;

	for (i = 0; i < NMTUS; ++i) {
		unsigned int val;

		t3_write_reg(adap, A_TP_MTU_TABLE, 0xff000000 | i);
		val = t3_read_reg(adap, A_TP_MTU_TABLE);
		mtus[i] = val & 0x3fff;
	}
}

/**
 *	t3_get_cong_cntl_tab - reads the congestion control table
 *	@adap: the adapter
 *	@incr: where to store the alpha values
 *
 *	Reads the additive increments programmed into the HW congestion
 *	control table.
 */
void t3_get_cong_cntl_tab(adapter_t *adap,
			  unsigned short incr[NMTUS][NCCTRL_WIN])
{
	unsigned int mtu, w;

	for (mtu = 0; mtu < NMTUS; ++mtu)
		for (w = 0; w < NCCTRL_WIN; ++w) {
			t3_write_reg(adap, A_TP_CCTRL_TABLE,
				     0xffff0000 | (mtu << 5) | w);
			incr[mtu][w] = (unsigned short)t3_read_reg(adap,
				        A_TP_CCTRL_TABLE) & 0x1fff;
		}
}

/**
 *	t3_tp_get_mib_stats - read TP's MIB counters
 *	@adap: the adapter
 *	@tps: holds the returned counter values
 *
 *	Returns the values of TP's MIB counters.
 */
void t3_tp_get_mib_stats(adapter_t *adap, struct tp_mib_stats *tps)
{
	t3_read_indirect(adap, A_TP_MIB_INDEX, A_TP_MIB_RDATA, (u32 *)tps,
			 sizeof(*tps) / sizeof(u32), 0);
}

/**
 *	t3_read_pace_tbl - read the pace table
 *	@adap: the adapter
 *	@pace_vals: holds the returned values
 *
 *	Returns the values of TP's pace table in nanoseconds.
 */
void t3_read_pace_tbl(adapter_t *adap, unsigned int pace_vals[NTX_SCHED])
{
	unsigned int i, tick_ns = dack_ticks_to_usec(adap, 1000);

	for (i = 0; i < NTX_SCHED; i++) {
		t3_write_reg(adap, A_TP_PACE_TABLE, 0xffff0000 + i);
		pace_vals[i] = t3_read_reg(adap, A_TP_PACE_TABLE) * tick_ns;
	}
}

/**
 *	t3_set_pace_tbl - set the pace table
 *	@adap: the adapter
 *	@pace_vals: the pace values in nanoseconds
 *	@start: index of the first entry in the HW pace table to set
 *	@n: how many entries to set
 *
 *	Sets (a subset of the) HW pace table.
 */
void t3_set_pace_tbl(adapter_t *adap, unsigned int *pace_vals,
		     unsigned int start, unsigned int n)
{
	unsigned int tick_ns = dack_ticks_to_usec(adap, 1000);

	for ( ; n; n--, start++, pace_vals++)
		t3_write_reg(adap, A_TP_PACE_TABLE, (start << 16) |
			     ((*pace_vals + tick_ns / 2) / tick_ns));
}

#define ulp_region(adap, name, start, len) \
	t3_write_reg((adap), A_ULPRX_ ## name ## _LLIMIT, (start)); \
	t3_write_reg((adap), A_ULPRX_ ## name ## _ULIMIT, \
		     (start) + (len) - 1); \
	start += len

#define ulptx_region(adap, name, start, len) \
	t3_write_reg((adap), A_ULPTX_ ## name ## _LLIMIT, (start)); \
	t3_write_reg((adap), A_ULPTX_ ## name ## _ULIMIT, \
		     (start) + (len) - 1)

static void ulp_config(adapter_t *adap, const struct tp_params *p)
{
	unsigned int m = p->chan_rx_size;

	ulp_region(adap, ISCSI, m, p->chan_rx_size / 8);
	ulp_region(adap, TDDP, m, p->chan_rx_size / 8);
	ulptx_region(adap, TPT, m, p->chan_rx_size / 4);
	ulp_region(adap, STAG, m, p->chan_rx_size / 4);
	ulp_region(adap, RQ, m, p->chan_rx_size / 4);
	ulptx_region(adap, PBL, m, p->chan_rx_size / 4);
	ulp_region(adap, PBL, m, p->chan_rx_size / 4);
	t3_write_reg(adap, A_ULPRX_TDDP_TAGMASK, 0xffffffff);
}


/**
 *	t3_set_proto_sram - set the contents of the protocol sram
 *	@adapter: the adapter
 *	@data: the protocol image
 *
 *	Write the contents of the protocol SRAM.
 */
int t3_set_proto_sram(adapter_t *adap, const u8 *data)
{
	int i;
	const u32 *buf = (const u32 *)data;

	for (i = 0; i < PROTO_SRAM_LINES; i++) {
		t3_write_reg(adap, A_TP_EMBED_OP_FIELD5, cpu_to_be32(*buf++));
		t3_write_reg(adap, A_TP_EMBED_OP_FIELD4, cpu_to_be32(*buf++));
		t3_write_reg(adap, A_TP_EMBED_OP_FIELD3, cpu_to_be32(*buf++));
		t3_write_reg(adap, A_TP_EMBED_OP_FIELD2, cpu_to_be32(*buf++));
		t3_write_reg(adap, A_TP_EMBED_OP_FIELD1, cpu_to_be32(*buf++));

		t3_write_reg(adap, A_TP_EMBED_OP_FIELD0, i << 1 | 1 << 31);
		if (t3_wait_op_done(adap, A_TP_EMBED_OP_FIELD0, 1, 1, 5, 1))
			return -EIO;
	}
	return 0;
}

/**
 *	t3_config_trace_filter - configure one of the tracing filters
 *	@adapter: the adapter
 *	@tp: the desired trace filter parameters
 *	@filter_index: which filter to configure
 *	@invert: if set non-matching packets are traced instead of matching ones
 *	@enable: whether to enable or disable the filter
 *
 *	Configures one of the tracing filters available in HW.
 */
void t3_config_trace_filter(adapter_t *adapter, const struct trace_params *tp,
			    int filter_index, int invert, int enable)
{
	u32 addr, key[4], mask[4];

	key[0] = tp->sport | (tp->sip << 16);
	key[1] = (tp->sip >> 16) | (tp->dport << 16);
	key[2] = tp->dip;
	key[3] = tp->proto | (tp->vlan << 8) | (tp->intf << 20);

	mask[0] = tp->sport_mask | (tp->sip_mask << 16);
	mask[1] = (tp->sip_mask >> 16) | (tp->dport_mask << 16);
	mask[2] = tp->dip_mask;
	mask[3] = tp->proto_mask | (tp->vlan_mask << 8) | (tp->intf_mask << 20);

	if (invert)
		key[3] |= (1 << 29);
	if (enable)
		key[3] |= (1 << 28);

	addr = filter_index ? A_TP_RX_TRC_KEY0 : A_TP_TX_TRC_KEY0;
	tp_wr_indirect(adapter, addr++, key[0]);
	tp_wr_indirect(adapter, addr++, mask[0]);
	tp_wr_indirect(adapter, addr++, key[1]);
	tp_wr_indirect(adapter, addr++, mask[1]);
	tp_wr_indirect(adapter, addr++, key[2]);
	tp_wr_indirect(adapter, addr++, mask[2]);
	tp_wr_indirect(adapter, addr++, key[3]);
	tp_wr_indirect(adapter, addr,   mask[3]);
	(void) t3_read_reg(adapter, A_TP_PIO_DATA);
}

/**
 *	t3_query_trace_filter - query a tracing filter
 *	@adapter: the adapter
 *	@tp: the current trace filter parameters
 *	@filter_index: which filter to query
 *	@inverted: non-zero if the filter is inverted
 *	@enabled: non-zero if the filter is enabled
 *
 *	Returns the current settings of the specified HW tracing filter.
 */
void t3_query_trace_filter(adapter_t *adapter, struct trace_params *tp,
			   int filter_index, int *inverted, int *enabled)
{
	u32 addr, key[4], mask[4];

	addr = filter_index ? A_TP_RX_TRC_KEY0 : A_TP_TX_TRC_KEY0;
	key[0]  = tp_rd_indirect(adapter, addr++);
	mask[0] = tp_rd_indirect(adapter, addr++);
	key[1]  = tp_rd_indirect(adapter, addr++);
	mask[1] = tp_rd_indirect(adapter, addr++);
	key[2]  = tp_rd_indirect(adapter, addr++);
	mask[2] = tp_rd_indirect(adapter, addr++);
	key[3]  = tp_rd_indirect(adapter, addr++);
	mask[3] = tp_rd_indirect(adapter, addr);

	tp->sport = key[0] & 0xffff;
	tp->sip   = (key[0] >> 16) | ((key[1] & 0xffff) << 16);
	tp->dport = key[1] >> 16;
	tp->dip   = key[2];
	tp->proto = key[3] & 0xff;
	tp->vlan  = key[3] >> 8;
	tp->intf  = key[3] >> 20;

	tp->sport_mask = mask[0] & 0xffff;
	tp->sip_mask   = (mask[0] >> 16) | ((mask[1] & 0xffff) << 16);
	tp->dport_mask = mask[1] >> 16;
	tp->dip_mask   = mask[2];
	tp->proto_mask = mask[3] & 0xff;
	tp->vlan_mask  = mask[3] >> 8;
	tp->intf_mask  = mask[3] >> 20;

	*inverted = key[3] & (1 << 29);
	*enabled  = key[3] & (1 << 28);
}

/**
 *	t3_config_sched - configure a HW traffic scheduler
 *	@adap: the adapter
 *	@kbps: target rate in Kbps
 *	@sched: the scheduler index
 *
 *	Configure a Tx HW scheduler for the target rate.
 */
int t3_config_sched(adapter_t *adap, unsigned int kbps, int sched)
{
	unsigned int v, tps, cpt, bpt, delta, mindelta = ~0;
	unsigned int clk = adap->params.vpd.cclk * 1000;
	unsigned int selected_cpt = 0, selected_bpt = 0;

	if (kbps > 0) {
		kbps *= 125;     /* -> bytes */
		for (cpt = 1; cpt <= 255; cpt++) {
			tps = clk / cpt;
			bpt = (kbps + tps / 2) / tps;
			if (bpt > 0 && bpt <= 255) {
				v = bpt * tps;
				delta = v >= kbps ? v - kbps : kbps - v;
				if (delta < mindelta) {
					mindelta = delta;
					selected_cpt = cpt;
					selected_bpt = bpt;
				}
			} else if (selected_cpt)
				break;
		}
		if (!selected_cpt)
			return -EINVAL;
	}
	t3_write_reg(adap, A_TP_TM_PIO_ADDR,
		     A_TP_TX_MOD_Q1_Q0_RATE_LIMIT - sched / 2);
	v = t3_read_reg(adap, A_TP_TM_PIO_DATA);
	if (sched & 1)
		v = (v & 0xffff) | (selected_cpt << 16) | (selected_bpt << 24);
	else
		v = (v & 0xffff0000) | selected_cpt | (selected_bpt << 8);
	t3_write_reg(adap, A_TP_TM_PIO_DATA, v);
	return 0;
}

/**
 *	t3_set_sched_ipg - set the IPG for a Tx HW packet rate scheduler
 *	@adap: the adapter
 *	@sched: the scheduler index
 *	@ipg: the interpacket delay in tenths of nanoseconds
 *
 *	Set the interpacket delay for a HW packet rate scheduler.
 */
int t3_set_sched_ipg(adapter_t *adap, int sched, unsigned int ipg)
{
	unsigned int v, addr = A_TP_TX_MOD_Q1_Q0_TIMER_SEPARATOR - sched / 2;

	/* convert ipg to nearest number of core clocks */
	ipg *= core_ticks_per_usec(adap);
	ipg = (ipg + 5000) / 10000;
	if (ipg > 0xffff)
		return -EINVAL;

	t3_write_reg(adap, A_TP_TM_PIO_ADDR, addr);
	v = t3_read_reg(adap, A_TP_TM_PIO_DATA);
	if (sched & 1)
		v = (v & 0xffff) | (ipg << 16);
	else
		v = (v & 0xffff0000) | ipg;
	t3_write_reg(adap, A_TP_TM_PIO_DATA, v);
	t3_read_reg(adap, A_TP_TM_PIO_DATA);
	return 0;
}

/**
 *	t3_get_tx_sched - get the configuration of a Tx HW traffic scheduler
 *	@adap: the adapter
 *	@sched: the scheduler index
 *	@kbps: the byte rate in Kbps
 *	@ipg: the interpacket delay in tenths of nanoseconds
 *
 *	Return the current configuration of a HW Tx scheduler.
 */
void t3_get_tx_sched(adapter_t *adap, unsigned int sched, unsigned int *kbps,
		     unsigned int *ipg)
{
	unsigned int v, addr, bpt, cpt;

	if (kbps) {
		addr = A_TP_TX_MOD_Q1_Q0_RATE_LIMIT - sched / 2;
		t3_write_reg(adap, A_TP_TM_PIO_ADDR, addr);
		v = t3_read_reg(adap, A_TP_TM_PIO_DATA);
		if (sched & 1)
			v >>= 16;
		bpt = (v >> 8) & 0xff;
		cpt = v & 0xff;
		if (!cpt)
			*kbps = 0;        /* scheduler disabled */
		else {
			v = (adap->params.vpd.cclk * 1000) / cpt;
			*kbps = (v * bpt) / 125;
		}
	}
	if (ipg) {
		addr = A_TP_TX_MOD_Q1_Q0_TIMER_SEPARATOR - sched / 2;
		t3_write_reg(adap, A_TP_TM_PIO_ADDR, addr);
		v = t3_read_reg(adap, A_TP_TM_PIO_DATA);
		if (sched & 1)
			v >>= 16;
		v &= 0xffff;
		*ipg = (10000 * v) / core_ticks_per_usec(adap);
	}
}

/**
 *	tp_init - configure TP
 *	@adap: the adapter
 *	@p: TP configuration parameters
 *
 *	Initializes the TP HW module.
 */
static int tp_init(adapter_t *adap, const struct tp_params *p)
{
	int busy = 0;

	tp_config(adap, p);
	t3_set_vlan_accel(adap, 3, 0);

	if (is_offload(adap)) {
		tp_set_timers(adap, adap->params.vpd.cclk * 1000);
		t3_write_reg(adap, A_TP_RESET, F_FLSTINITENABLE);
		busy = t3_wait_op_done(adap, A_TP_RESET, F_FLSTINITENABLE,
				       0, 1000, 5);
		if (busy)
			CH_ERR(adap, "TP initialization timed out\n");
	}

	if (!busy)
		t3_write_reg(adap, A_TP_RESET, F_TPRESET);
	return busy;
}

/**
 *	t3_mps_set_active_ports - configure port failover
 *	@adap: the adapter
 *	@port_mask: bitmap of active ports
 *
 *	Sets the active ports according to the supplied bitmap.
 */
int t3_mps_set_active_ports(adapter_t *adap, unsigned int port_mask)
{
	if (port_mask & ~((1 << adap->params.nports) - 1))
		return -EINVAL;
	t3_set_reg_field(adap, A_MPS_CFG, F_PORT1ACTIVE | F_PORT0ACTIVE,
			 port_mask << S_PORT0ACTIVE);
	return 0;
}

/**
 * 	chan_init_hw - channel-dependent HW initialization
 *	@adap: the adapter
 *	@chan_map: bitmap of Tx channels being used
 *
 *	Perform the bits of HW initialization that are dependent on the Tx
 *	channels being used.
 */
static void chan_init_hw(adapter_t *adap, unsigned int chan_map)
{
	int i;

	if (chan_map != 3) {                                 /* one channel */
		t3_set_reg_field(adap, A_ULPRX_CTL, F_ROUND_ROBIN, 0);
		t3_set_reg_field(adap, A_ULPTX_CONFIG, F_CFG_RR_ARB, 0);
		t3_write_reg(adap, A_MPS_CFG, F_TPRXPORTEN | F_ENFORCEPKT |
			     (chan_map == 1 ? F_TPTXPORT0EN | F_PORT0ACTIVE :
					      F_TPTXPORT1EN | F_PORT1ACTIVE));
		t3_write_reg(adap, A_PM1_TX_CFG,
			     chan_map == 1 ? 0xffffffff : 0);
		if (chan_map == 2)
			t3_write_reg(adap, A_TP_TX_MOD_QUEUE_REQ_MAP,
				     V_TX_MOD_QUEUE_REQ_MAP(0xff));
		t3_write_reg(adap, A_TP_TX_MOD_QUE_TABLE, (12 << 16) | 0xd9c8);
		t3_write_reg(adap, A_TP_TX_MOD_QUE_TABLE, (13 << 16) | 0xfbea);
	} else {                                             /* two channels */
		t3_set_reg_field(adap, A_ULPRX_CTL, 0, F_ROUND_ROBIN);
		t3_set_reg_field(adap, A_ULPTX_CONFIG, 0, F_CFG_RR_ARB);
		t3_write_reg(adap, A_ULPTX_DMA_WEIGHT,
			     V_D1_WEIGHT(16) | V_D0_WEIGHT(16));
		t3_write_reg(adap, A_MPS_CFG, F_TPTXPORT0EN | F_TPTXPORT1EN |
			     F_TPRXPORTEN | F_PORT0ACTIVE | F_PORT1ACTIVE |
			     F_ENFORCEPKT);
		t3_write_reg(adap, A_PM1_TX_CFG, 0x80008000);
		t3_set_reg_field(adap, A_TP_PC_CONFIG, 0, F_TXTOSQUEUEMAPMODE);
		t3_write_reg(adap, A_TP_TX_MOD_QUEUE_REQ_MAP,
			     V_TX_MOD_QUEUE_REQ_MAP(0xaa));
		for (i = 0; i < 16; i++)
			t3_write_reg(adap, A_TP_TX_MOD_QUE_TABLE,
				     (i << 16) | 0x1010);
		t3_write_reg(adap, A_TP_TX_MOD_QUE_TABLE, (12 << 16) | 0xba98);
		t3_write_reg(adap, A_TP_TX_MOD_QUE_TABLE, (13 << 16) | 0xfedc);
	}
}

static int calibrate_xgm(adapter_t *adapter)
{
	if (uses_xaui(adapter)) {
		unsigned int v, i;

		for (i = 0; i < 5; ++i) {
			t3_write_reg(adapter, A_XGM_XAUI_IMP, 0);
			(void) t3_read_reg(adapter, A_XGM_XAUI_IMP);
			msleep(1);
			v = t3_read_reg(adapter, A_XGM_XAUI_IMP);
			if (!(v & (F_XGM_CALFAULT | F_CALBUSY))) {
				t3_write_reg(adapter, A_XGM_XAUI_IMP,
					     V_XAUIIMP(G_CALIMP(v) >> 2));
				return 0;
			}
		}
		CH_ERR(adapter, "MAC calibration failed\n");
		return -1;
	} else {
		t3_write_reg(adapter, A_XGM_RGMII_IMP,
			     V_RGMIIIMPPD(2) | V_RGMIIIMPPU(3));
		t3_set_reg_field(adapter, A_XGM_RGMII_IMP, F_XGM_IMPSETUPDATE,
				 F_XGM_IMPSETUPDATE);
	}
	return 0;
}

static void calibrate_xgm_t3b(adapter_t *adapter)
{
	if (!uses_xaui(adapter)) {
		t3_write_reg(adapter, A_XGM_RGMII_IMP, F_CALRESET |
			     F_CALUPDATE | V_RGMIIIMPPD(2) | V_RGMIIIMPPU(3));
		t3_set_reg_field(adapter, A_XGM_RGMII_IMP, F_CALRESET, 0);
		t3_set_reg_field(adapter, A_XGM_RGMII_IMP, 0,
				 F_XGM_IMPSETUPDATE);
		t3_set_reg_field(adapter, A_XGM_RGMII_IMP, F_XGM_IMPSETUPDATE,
				 0);
		t3_set_reg_field(adapter, A_XGM_RGMII_IMP, F_CALUPDATE, 0);
		t3_set_reg_field(adapter, A_XGM_RGMII_IMP, 0, F_CALUPDATE);
	}
}

struct mc7_timing_params {
	unsigned char ActToPreDly;
	unsigned char ActToRdWrDly;
	unsigned char PreCyc;
	unsigned char RefCyc[5];
	unsigned char BkCyc;
	unsigned char WrToRdDly;
	unsigned char RdToWrDly;
};

/*
 * Write a value to a register and check that the write completed.  These
 * writes normally complete in a cycle or two, so one read should suffice.
 * The very first read exists to flush the posted write to the device.
 */
static int wrreg_wait(adapter_t *adapter, unsigned int addr, u32 val)
{
	t3_write_reg(adapter,	addr, val);
	(void) t3_read_reg(adapter, addr);                   /* flush */
	if (!(t3_read_reg(adapter, addr) & F_BUSY))
		return 0;
	CH_ERR(adapter, "write to MC7 register 0x%x timed out\n", addr);
	return -EIO;
}

static int mc7_init(struct mc7 *mc7, unsigned int mc7_clock, int mem_type)
{
	static const unsigned int mc7_mode[] = {
		0x632, 0x642, 0x652, 0x432, 0x442
	};
	static const struct mc7_timing_params mc7_timings[] = {
		{ 12, 3, 4, { 20, 28, 34, 52, 0 }, 15, 6, 4 },
		{ 12, 4, 5, { 20, 28, 34, 52, 0 }, 16, 7, 4 },
		{ 12, 5, 6, { 20, 28, 34, 52, 0 }, 17, 8, 4 },
		{ 9,  3, 4, { 15, 21, 26, 39, 0 }, 12, 6, 4 },
		{ 9,  4, 5, { 15, 21, 26, 39, 0 }, 13, 7, 4 }
	};

	u32 val;
	unsigned int width, density, slow, attempts;
	adapter_t *adapter = mc7->adapter;
	const struct mc7_timing_params *p = &mc7_timings[mem_type];

	if (!mc7->size)
		return 0;

	val = t3_read_reg(adapter, mc7->offset + A_MC7_CFG);
	slow = val & F_SLOW;
	width = G_WIDTH(val);
	density = G_DEN(val);

	t3_write_reg(adapter, mc7->offset + A_MC7_CFG, val | F_IFEN);
	val = t3_read_reg(adapter, mc7->offset + A_MC7_CFG);  /* flush */
	msleep(1);

	if (!slow) {
		t3_write_reg(adapter, mc7->offset + A_MC7_CAL, F_SGL_CAL_EN);
		(void) t3_read_reg(adapter, mc7->offset + A_MC7_CAL);
		msleep(1);
		if (t3_read_reg(adapter, mc7->offset + A_MC7_CAL) &
		    (F_BUSY | F_SGL_CAL_EN | F_CAL_FAULT)) {
			CH_ERR(adapter, "%s MC7 calibration timed out\n",
			       mc7->name);
			goto out_fail;
		}
	}

	t3_write_reg(adapter, mc7->offset + A_MC7_PARM,
		     V_ACTTOPREDLY(p->ActToPreDly) |
		     V_ACTTORDWRDLY(p->ActToRdWrDly) | V_PRECYC(p->PreCyc) |
		     V_REFCYC(p->RefCyc[density]) | V_BKCYC(p->BkCyc) |
		     V_WRTORDDLY(p->WrToRdDly) | V_RDTOWRDLY(p->RdToWrDly));

	t3_write_reg(adapter, mc7->offset + A_MC7_CFG,
		     val | F_CLKEN | F_TERM150);
	(void) t3_read_reg(adapter, mc7->offset + A_MC7_CFG); /* flush */

	if (!slow)
		t3_set_reg_field(adapter, mc7->offset + A_MC7_DLL, F_DLLENB,
				 F_DLLENB);
	udelay(1);

	val = slow ? 3 : 6;
	if (wrreg_wait(adapter, mc7->offset + A_MC7_PRE, 0) ||
	    wrreg_wait(adapter, mc7->offset + A_MC7_EXT_MODE2, 0) ||
	    wrreg_wait(adapter, mc7->offset + A_MC7_EXT_MODE3, 0) ||
	    wrreg_wait(adapter, mc7->offset + A_MC7_EXT_MODE1, val))
		goto out_fail;

	if (!slow) {
		t3_write_reg(adapter, mc7->offset + A_MC7_MODE, 0x100);
		t3_set_reg_field(adapter, mc7->offset + A_MC7_DLL,
				 F_DLLRST, 0);
		udelay(5);
	}

	if (wrreg_wait(adapter, mc7->offset + A_MC7_PRE, 0) ||
	    wrreg_wait(adapter, mc7->offset + A_MC7_REF, 0) ||
	    wrreg_wait(adapter, mc7->offset + A_MC7_REF, 0) ||
	    wrreg_wait(adapter, mc7->offset + A_MC7_MODE,
		       mc7_mode[mem_type]) ||
	    wrreg_wait(adapter, mc7->offset + A_MC7_EXT_MODE1, val | 0x380) ||
	    wrreg_wait(adapter, mc7->offset + A_MC7_EXT_MODE1, val))
		goto out_fail;

	/* clock value is in KHz */
	mc7_clock = mc7_clock * 7812 + mc7_clock / 2;  /* ns */
	mc7_clock /= 1000000;                          /* KHz->MHz, ns->us */

	t3_write_reg(adapter, mc7->offset + A_MC7_REF,
		     F_PERREFEN | V_PREREFDIV(mc7_clock));
	(void) t3_read_reg(adapter, mc7->offset + A_MC7_REF); /* flush */

	t3_write_reg(adapter, mc7->offset + A_MC7_ECC,
		     F_ECCGENEN | F_ECCCHKEN);
	t3_write_reg(adapter, mc7->offset + A_MC7_BIST_DATA, 0);
	t3_write_reg(adapter, mc7->offset + A_MC7_BIST_ADDR_BEG, 0);
	t3_write_reg(adapter, mc7->offset + A_MC7_BIST_ADDR_END,
		     (mc7->size << width) - 1);
	t3_write_reg(adapter, mc7->offset + A_MC7_BIST_OP, V_OP(1));
	(void) t3_read_reg(adapter, mc7->offset + A_MC7_BIST_OP); /* flush */

	attempts = 50;
	do {
		msleep(250);
		val = t3_read_reg(adapter, mc7->offset + A_MC7_BIST_OP);
	} while ((val & F_BUSY) && --attempts);
	if (val & F_BUSY) {
		CH_ERR(adapter, "%s MC7 BIST timed out\n", mc7->name);
		goto out_fail;
	}

	/* Enable normal memory accesses. */
	t3_set_reg_field(adapter, mc7->offset + A_MC7_CFG, 0, F_RDY);
	return 0;

 out_fail:
	return -1;
}

static void config_pcie(adapter_t *adap)
{
	static const u16 ack_lat[4][6] = {
		{ 237, 416, 559, 1071, 2095, 4143 },
		{ 128, 217, 289, 545, 1057, 2081 },
		{ 73, 118, 154, 282, 538, 1050 },
		{ 67, 107, 86, 150, 278, 534 }
	};
	static const u16 rpl_tmr[4][6] = {
		{ 711, 1248, 1677, 3213, 6285, 12429 },
		{ 384, 651, 867, 1635, 3171, 6243 },
		{ 219, 354, 462, 846, 1614, 3150 },
		{ 201, 321, 258, 450, 834, 1602 }
	};

	u16 val, devid;
	unsigned int log2_width, pldsize;
	unsigned int fst_trn_rx, fst_trn_tx, acklat, rpllmt;

	t3_os_pci_read_config_2(adap,
				adap->params.pci.pcie_cap_addr + PCI_EXP_DEVCTL,
				&val);
	pldsize = (val & PCI_EXP_DEVCTL_PAYLOAD) >> 5;

	/*
	 * Gen2 adapter pcie bridge compatibility requires minimum
	 * Max_Read_Request_size
	 */
	t3_os_pci_read_config_2(adap, 0x2, &devid);
	if (devid == 0x37) {
		t3_os_pci_write_config_2(adap,
		    adap->params.pci.pcie_cap_addr + PCI_EXP_DEVCTL,
		    val & ~PCI_EXP_DEVCTL_READRQ & ~PCI_EXP_DEVCTL_PAYLOAD);
		pldsize = 0;
	}

	t3_os_pci_read_config_2(adap,
				adap->params.pci.pcie_cap_addr + PCI_EXP_LNKCTL,
			       	&val);

	fst_trn_tx = G_NUMFSTTRNSEQ(t3_read_reg(adap, A_PCIE_PEX_CTRL0));
	fst_trn_rx = adap->params.rev == 0 ? fst_trn_tx :
			G_NUMFSTTRNSEQRX(t3_read_reg(adap, A_PCIE_MODE));
	log2_width = fls(adap->params.pci.width) - 1;
	acklat = ack_lat[log2_width][pldsize];
	if (val & 1)                            /* check LOsEnable */
		acklat += fst_trn_tx * 4;
	rpllmt = rpl_tmr[log2_width][pldsize] + fst_trn_rx * 4;

	if (adap->params.rev == 0)
		t3_set_reg_field(adap, A_PCIE_PEX_CTRL1,
				 V_T3A_ACKLAT(M_T3A_ACKLAT),
				 V_T3A_ACKLAT(acklat));
	else
		t3_set_reg_field(adap, A_PCIE_PEX_CTRL1, V_ACKLAT(M_ACKLAT),
				 V_ACKLAT(acklat));

	t3_set_reg_field(adap, A_PCIE_PEX_CTRL0, V_REPLAYLMT(M_REPLAYLMT),
			 V_REPLAYLMT(rpllmt));

	t3_write_reg(adap, A_PCIE_PEX_ERR, 0xffffffff);
	t3_set_reg_field(adap, A_PCIE_CFG, 0,
			 F_ENABLELINKDWNDRST | F_ENABLELINKDOWNRST |
			 F_PCIE_DMASTOPEN | F_PCIE_CLIDECEN);
}

/**
 * 	t3_init_hw - initialize and configure T3 HW modules
 * 	@adapter: the adapter
 * 	@fw_params: initial parameters to pass to firmware (optional)
 *
 *	Initialize and configure T3 HW modules.  This performs the
 *	initialization steps that need to be done once after a card is reset.
 *	MAC and PHY initialization is handled separarely whenever a port is
 *	enabled.
 *
 *	@fw_params are passed to FW and their value is platform dependent.
 *	Only the top 8 bits are available for use, the rest must be 0.
 */
int t3_init_hw(adapter_t *adapter, u32 fw_params)
{
	int err = -EIO, attempts, i;
	const struct vpd_params *vpd = &adapter->params.vpd;

	if (adapter->params.rev > 0)
		calibrate_xgm_t3b(adapter);
	else if (calibrate_xgm(adapter))
		goto out_err;

	if (adapter->params.nports > 2)
		t3_mac_init(&adap2pinfo(adapter, 0)->mac);

	if (vpd->mclk) {
		partition_mem(adapter, &adapter->params.tp);

		if (mc7_init(&adapter->pmrx, vpd->mclk, vpd->mem_timing) ||
		    mc7_init(&adapter->pmtx, vpd->mclk, vpd->mem_timing) ||
		    mc7_init(&adapter->cm, vpd->mclk, vpd->mem_timing) ||
		    t3_mc5_init(&adapter->mc5, adapter->params.mc5.nservers,
			        adapter->params.mc5.nfilters,
			       	adapter->params.mc5.nroutes))
			goto out_err;

		for (i = 0; i < 32; i++)
			if (clear_sge_ctxt(adapter, i, F_CQ))
				goto out_err;
	}

	if (tp_init(adapter, &adapter->params.tp))
		goto out_err;

	t3_tp_set_coalescing_size(adapter,
				  min(adapter->params.sge.max_pkt_size,
				      MAX_RX_COALESCING_LEN), 1);
	t3_tp_set_max_rxsize(adapter,
			     min(adapter->params.sge.max_pkt_size, 16384U));
	ulp_config(adapter, &adapter->params.tp);
	if (is_pcie(adapter))
		config_pcie(adapter);
	else
		t3_set_reg_field(adapter, A_PCIX_CFG, 0,
				 F_DMASTOPEN | F_CLIDECEN);

	if (adapter->params.rev == T3_REV_C)
		t3_set_reg_field(adapter, A_ULPTX_CONFIG, 0,
				 F_CFG_CQE_SOP_MASK);

	t3_write_reg(adapter, A_PM1_RX_CFG, 0xffffffff);
	t3_write_reg(adapter, A_PM1_RX_MODE, 0);
	t3_write_reg(adapter, A_PM1_TX_MODE, 0);
	chan_init_hw(adapter, adapter->params.chan_map);
	t3_sge_init(adapter, &adapter->params.sge);
	t3_set_reg_field(adapter, A_PL_RST, 0, F_FATALPERREN);

	t3_write_reg(adapter, A_T3DBG_GPIO_ACT_LOW, calc_gpio_intr(adapter));

	t3_write_reg(adapter, A_CIM_HOST_ACC_DATA, vpd->uclk | fw_params);
	t3_write_reg(adapter, A_CIM_BOOT_CFG,
		     V_BOOTADDR(FW_FLASH_BOOT_ADDR >> 2));
	(void) t3_read_reg(adapter, A_CIM_BOOT_CFG);    /* flush */

	attempts = 100;
	do {                          /* wait for uP to initialize */
		msleep(20);
	} while (t3_read_reg(adapter, A_CIM_HOST_ACC_DATA) && --attempts);
	if (!attempts) {
		CH_ERR(adapter, "uP initialization timed out\n");
		goto out_err;
	}

	err = 0;
 out_err:
	return err;
}

/**
 *	get_pci_mode - determine a card's PCI mode
 *	@adapter: the adapter
 *	@p: where to store the PCI settings
 *
 *	Determines a card's PCI mode and associated parameters, such as speed
 *	and width.
 */
static void __devinit get_pci_mode(adapter_t *adapter, struct pci_params *p)
{
	static unsigned short speed_map[] = { 33, 66, 100, 133 };
	u32 pci_mode, pcie_cap;

	pcie_cap = t3_os_find_pci_capability(adapter, PCI_CAP_ID_EXP);
	if (pcie_cap) {
		u16 val;

		p->variant = PCI_VARIANT_PCIE;
		p->pcie_cap_addr = pcie_cap;
		t3_os_pci_read_config_2(adapter, pcie_cap + PCI_EXP_LNKSTA,
					&val);
		p->width = (val >> 4) & 0x3f;
		return;
	}

	pci_mode = t3_read_reg(adapter, A_PCIX_MODE);
	p->speed = speed_map[G_PCLKRANGE(pci_mode)];
	p->width = (pci_mode & F_64BIT) ? 64 : 32;
	pci_mode = G_PCIXINITPAT(pci_mode);
	if (pci_mode == 0)
		p->variant = PCI_VARIANT_PCI;
	else if (pci_mode < 4)
		p->variant = PCI_VARIANT_PCIX_MODE1_PARITY;
	else if (pci_mode < 8)
		p->variant = PCI_VARIANT_PCIX_MODE1_ECC;
	else
		p->variant = PCI_VARIANT_PCIX_266_MODE2;
}

/**
 *	init_link_config - initialize a link's SW state
 *	@lc: structure holding the link state
 *	@caps: link capabilities
 *
 *	Initializes the SW state maintained for each link, including the link's
 *	capabilities and default speed/duplex/flow-control/autonegotiation
 *	settings.
 */
static void __devinit init_link_config(struct link_config *lc,
				       unsigned int caps)
{
	lc->supported = caps;
	lc->requested_speed = lc->speed = SPEED_INVALID;
	lc->requested_duplex = lc->duplex = DUPLEX_INVALID;
	lc->requested_fc = lc->fc = PAUSE_RX | PAUSE_TX;
	if (lc->supported & SUPPORTED_Autoneg) {
		lc->advertising = lc->supported;
		lc->autoneg = AUTONEG_ENABLE;
		lc->requested_fc |= PAUSE_AUTONEG;
	} else {
		lc->advertising = 0;
		lc->autoneg = AUTONEG_DISABLE;
	}
}

/**
 *	mc7_calc_size - calculate MC7 memory size
 *	@cfg: the MC7 configuration
 *
 *	Calculates the size of an MC7 memory in bytes from the value of its
 *	configuration register.
 */
static unsigned int __devinit mc7_calc_size(u32 cfg)
{
	unsigned int width = G_WIDTH(cfg);
	unsigned int banks = !!(cfg & F_BKS) + 1;
	unsigned int org = !!(cfg & F_ORG) + 1;
	unsigned int density = G_DEN(cfg);
	unsigned int MBs = ((256 << density) * banks) / (org << width);

	return MBs << 20;
}

static void __devinit mc7_prep(adapter_t *adapter, struct mc7 *mc7,
			       unsigned int base_addr, const char *name)
{
	u32 cfg;

	mc7->adapter = adapter;
	mc7->name = name;
	mc7->offset = base_addr - MC7_PMRX_BASE_ADDR;
	cfg = t3_read_reg(adapter, mc7->offset + A_MC7_CFG);
	mc7->size = G_DEN(cfg) == M_DEN ? 0 : mc7_calc_size(cfg);
	mc7->width = G_WIDTH(cfg);
}

void mac_prep(struct cmac *mac, adapter_t *adapter, int index)
{
	u16 devid;

	mac->adapter = adapter;
	mac->multiport = adapter->params.nports > 2;
	if (mac->multiport) {
		mac->ext_port = (unsigned char)index;
		mac->nucast = 8;
	} else
		mac->nucast = 1;

	/* Gen2 adapter uses VPD xauicfg[] to notify driver which MAC
	   is connected to each port, its suppose to be using xgmac0 for both ports
	 */
	t3_os_pci_read_config_2(adapter, 0x2, &devid);

	if (mac->multiport ||
		(!adapter->params.vpd.xauicfg[1] && (devid==0x37)))
			index  = 0;

	mac->offset = (XGMAC0_1_BASE_ADDR - XGMAC0_0_BASE_ADDR) * index;

	if (adapter->params.rev == 0 && uses_xaui(adapter)) {
		t3_write_reg(adapter, A_XGM_SERDES_CTRL + mac->offset,
			     is_10G(adapter) ? 0x2901c04 : 0x2301c04);
		t3_set_reg_field(adapter, A_XGM_PORT_CFG + mac->offset,
				 F_ENRGMII, 0);
	}
}

/**
 *	early_hw_init - HW initialization done at card detection time
 *	@adapter: the adapter
 *	@ai: contains information about the adapter type and properties
 *
 *	Perfoms the part of HW initialization that is done early on when the
 *	driver first detecs the card.  Most of the HW state is initialized
 *	lazily later on when a port or an offload function are first used.
 */
void early_hw_init(adapter_t *adapter, const struct adapter_info *ai)
{
	u32 val = V_PORTSPEED(is_10G(adapter) || adapter->params.nports > 2 ?
			      3 : 2);
	u32 gpio_out = ai->gpio_out;

	mi1_init(adapter, ai);
	t3_write_reg(adapter, A_I2C_CFG,                  /* set for 80KHz */
		     V_I2C_CLKDIV(adapter->params.vpd.cclk / 80 - 1));
	t3_write_reg(adapter, A_T3DBG_GPIO_EN,
		     gpio_out | F_GPIO0_OEN | F_GPIO0_OUT_VAL);
	t3_write_reg(adapter, A_MC5_DB_SERVER_INDEX, 0);
	t3_write_reg(adapter, A_SG_OCO_BASE, V_BASE1(0xfff));

	if (adapter->params.rev == 0 || !uses_xaui(adapter))
		val |= F_ENRGMII;

	/* Enable MAC clocks so we can access the registers */
	t3_write_reg(adapter, A_XGM_PORT_CFG, val);
	(void) t3_read_reg(adapter, A_XGM_PORT_CFG);

	val |= F_CLKDIVRESET_;
	t3_write_reg(adapter, A_XGM_PORT_CFG, val);
	(void) t3_read_reg(adapter, A_XGM_PORT_CFG);
	t3_write_reg(adapter, XGM_REG(A_XGM_PORT_CFG, 1), val);
	(void) t3_read_reg(adapter, A_XGM_PORT_CFG);
}

/**
 *	t3_reset_adapter - reset the adapter
 *	@adapter: the adapter
 *
 * 	Reset the adapter.
 */
int t3_reset_adapter(adapter_t *adapter)
{
	int i, save_and_restore_pcie =
	    adapter->params.rev < T3_REV_B2 && is_pcie(adapter);
	uint16_t devid = 0;

	if (save_and_restore_pcie)
		t3_os_pci_save_state(adapter);
	t3_write_reg(adapter, A_PL_RST, F_CRSTWRM | F_CRSTWRMMODE);

 	/*
	 * Delay. Give Some time to device to reset fully.
	 * XXX The delay time should be modified.
	 */
	for (i = 0; i < 10; i++) {
		msleep(50);
		t3_os_pci_read_config_2(adapter, 0x00, &devid);
		if (devid == 0x1425)
			break;
	}

	if (devid != 0x1425)
		return -1;

	if (save_and_restore_pcie)
		t3_os_pci_restore_state(adapter);
	return 0;
}

static int init_parity(adapter_t *adap)
{
	int i, err, addr;

	if (t3_read_reg(adap, A_SG_CONTEXT_CMD) & F_CONTEXT_CMD_BUSY)
		return -EBUSY;

	for (err = i = 0; !err && i < 16; i++)
		err = clear_sge_ctxt(adap, i, F_EGRESS);
	for (i = 0xfff0; !err && i <= 0xffff; i++)
		err = clear_sge_ctxt(adap, i, F_EGRESS);
	for (i = 0; !err && i < SGE_QSETS; i++)
		err = clear_sge_ctxt(adap, i, F_RESPONSEQ);
	if (err)
		return err;

	t3_write_reg(adap, A_CIM_IBQ_DBG_DATA, 0);
	for (i = 0; i < 4; i++)
		for (addr = 0; addr <= M_IBQDBGADDR; addr++) {
			t3_write_reg(adap, A_CIM_IBQ_DBG_CFG, F_IBQDBGEN |
				     F_IBQDBGWR | V_IBQDBGQID(i) |
				     V_IBQDBGADDR(addr));
			err = t3_wait_op_done(adap, A_CIM_IBQ_DBG_CFG,
					      F_IBQDBGBUSY, 0, 2, 1);
			if (err)
				return err;
		}
	return 0;
}

/**
 *	t3_prep_adapter - prepare SW and HW for operation
 *	@adapter: the adapter
 *	@ai: contains information about the adapter type and properties
 *
 *	Initialize adapter SW state for the various HW modules, set initial
 *	values for some adapter tunables, take PHYs out of reset, and
 *	initialize the MDIO interface.
 */
int __devinit t3_prep_adapter(adapter_t *adapter,
			      const struct adapter_info *ai, int reset)
{
	int ret;
	unsigned int i, j = 0;

	get_pci_mode(adapter, &adapter->params.pci);

	adapter->params.info = ai;
	adapter->params.nports = ai->nports0 + ai->nports1;
	adapter->params.chan_map = (!!ai->nports0) | (!!ai->nports1 << 1);
	adapter->params.rev = t3_read_reg(adapter, A_PL_REV);

	/*
	 * We used to only run the "adapter check task" once a second if
	 * we had PHYs which didn't support interrupts (we would check
	 * their link status once a second).  Now we check other conditions
	 * in that routine which would [potentially] impose a very high
	 * interrupt load on the system.  As such, we now always scan the
	 * adapter state once a second ...
	 */
	adapter->params.linkpoll_period = 10;

	if (adapter->params.nports > 2)
		adapter->params.stats_update_period = VSC_STATS_ACCUM_SECS;
	else
		adapter->params.stats_update_period = is_10G(adapter) ?
			MAC_STATS_ACCUM_SECS : (MAC_STATS_ACCUM_SECS * 10);
	adapter->params.pci.vpd_cap_addr =
		t3_os_find_pci_capability(adapter, PCI_CAP_ID_VPD);

	ret = get_vpd_params(adapter, &adapter->params.vpd);
	if (ret < 0)
		return ret;

	if (reset && t3_reset_adapter(adapter))
		return -1;

	if (adapter->params.vpd.mclk) {
		struct tp_params *p = &adapter->params.tp;

		mc7_prep(adapter, &adapter->pmrx, MC7_PMRX_BASE_ADDR, "PMRX");
		mc7_prep(adapter, &adapter->pmtx, MC7_PMTX_BASE_ADDR, "PMTX");
		mc7_prep(adapter, &adapter->cm, MC7_CM_BASE_ADDR, "CM");

		p->nchan = adapter->params.chan_map == 3 ? 2 : 1;
		p->pmrx_size = t3_mc7_size(&adapter->pmrx);
		p->pmtx_size = t3_mc7_size(&adapter->pmtx);
		p->cm_size = t3_mc7_size(&adapter->cm);
		p->chan_rx_size = p->pmrx_size / 2;     /* only 1 Rx channel */
		p->chan_tx_size = p->pmtx_size / p->nchan;
		p->rx_pg_size = 64 * 1024;
		p->tx_pg_size = is_10G(adapter) ? 64 * 1024 : 16 * 1024;
		p->rx_num_pgs = pm_num_pages(p->chan_rx_size, p->rx_pg_size);
		p->tx_num_pgs = pm_num_pages(p->chan_tx_size, p->tx_pg_size);
		p->ntimer_qs = p->cm_size >= (128 << 20) ||
			       adapter->params.rev > 0 ? 12 : 6;
		p->tre = fls(adapter->params.vpd.cclk / (1000 / TP_TMR_RES)) -
			 1;
		p->dack_re = fls(adapter->params.vpd.cclk / 10) - 1; /* 100us */
	}

	adapter->params.offload = t3_mc7_size(&adapter->pmrx) &&
				  t3_mc7_size(&adapter->pmtx) &&
				  t3_mc7_size(&adapter->cm);

	t3_sge_prep(adapter, &adapter->params.sge);

	if (is_offload(adapter)) {
		adapter->params.mc5.nservers = DEFAULT_NSERVERS;
		/* PR 6487. TOE and filtering are mutually exclusive */
		adapter->params.mc5.nfilters = 0;
		adapter->params.mc5.nroutes = 0;
		t3_mc5_prep(adapter, &adapter->mc5, MC5_MODE_144_BIT);

		init_mtus(adapter->params.mtus);
		init_cong_ctrl(adapter->params.a_wnd, adapter->params.b_wnd);
	}

	early_hw_init(adapter, ai);
	ret = init_parity(adapter);
	if (ret)
		return ret;

	if (adapter->params.nports > 2 &&
	    (ret = t3_vsc7323_init(adapter, adapter->params.nports)))
		return ret;

	for_each_port(adapter, i) {
		u8 hw_addr[6];
		const struct port_type_info *pti;
		struct port_info *p = adap2pinfo(adapter, i);

		for (;;) {
			unsigned port_type = adapter->params.vpd.port_type[j];
			if (port_type) {
				if (port_type < ARRAY_SIZE(port_types)) {
					pti = &port_types[port_type];
					break;
				} else
					return -EINVAL;
			}
			j++;
			if (j >= ARRAY_SIZE(adapter->params.vpd.port_type))
				return -EINVAL;
		}
		ret = pti->phy_prep(p, ai->phy_base_addr + j,
				    ai->mdio_ops);
		if (ret)
			return ret;
		mac_prep(&p->mac, adapter, j);
		++j;

		/*
		 * The VPD EEPROM stores the base Ethernet address for the
		 * card.  A port's address is derived from the base by adding
		 * the port's index to the base's low octet.
		 */
		memcpy(hw_addr, adapter->params.vpd.eth_base, 5);
		hw_addr[5] = adapter->params.vpd.eth_base[5] + i;

		t3_os_set_hw_addr(adapter, i, hw_addr);
		init_link_config(&p->link_config, p->phy.caps);
		p->phy.ops->power_down(&p->phy, 1);

		/*
		 * If the PHY doesn't support interrupts for link status
		 * changes, schedule a scan of the adapter links at least
		 * once a second.
		 */
		if (!(p->phy.caps & SUPPORTED_IRQ) &&
		    adapter->params.linkpoll_period > 10)
			adapter->params.linkpoll_period = 10;
	}

	return 0;
}

/**
 *	t3_reinit_adapter - prepare HW for operation again
 *	@adapter: the adapter
 *
 *	Put HW in the same state as @t3_prep_adapter without any changes to
 *	SW state.  This is a cut down version of @t3_prep_adapter intended
 *	to be used after events that wipe out HW state but preserve SW state,
 *	e.g., EEH.  The device must be reset before calling this.
 */
int t3_reinit_adapter(adapter_t *adap)
{
	unsigned int i;
	int ret, j = 0;

	early_hw_init(adap, adap->params.info);
	ret = init_parity(adap);
	if (ret)
		return ret;

	if (adap->params.nports > 2 &&
	    (ret = t3_vsc7323_init(adap, adap->params.nports)))
		return ret;

	for_each_port(adap, i) {
		const struct port_type_info *pti;
		struct port_info *p = adap2pinfo(adap, i);

		for (;;) {
			unsigned port_type = adap->params.vpd.port_type[j];
			if (port_type) {
				if (port_type < ARRAY_SIZE(port_types)) {
					pti = &port_types[port_type];
					break;
				} else
					return -EINVAL;
			}
			j++;
			if (j >= ARRAY_SIZE(adap->params.vpd.port_type))
				return -EINVAL;
		}
		ret = pti->phy_prep(p, p->phy.addr, NULL);
		if (ret)
			return ret;
		p->phy.ops->power_down(&p->phy, 1);
	}
	return 0;
}

void t3_led_ready(adapter_t *adapter)
{
	t3_set_reg_field(adapter, A_T3DBG_GPIO_EN, F_GPIO0_OUT_VAL,
			 F_GPIO0_OUT_VAL);
}

void t3_port_failover(adapter_t *adapter, int port)
{
	u32 val;

	val = port ? F_PORT1ACTIVE : F_PORT0ACTIVE;
	t3_set_reg_field(adapter, A_MPS_CFG, F_PORT0ACTIVE | F_PORT1ACTIVE,
			 val);
}

void t3_failover_done(adapter_t *adapter, int port)
{
	t3_set_reg_field(adapter, A_MPS_CFG, F_PORT0ACTIVE | F_PORT1ACTIVE,
			 F_PORT0ACTIVE | F_PORT1ACTIVE);
}

void t3_failover_clear(adapter_t *adapter)
{
	t3_set_reg_field(adapter, A_MPS_CFG, F_PORT0ACTIVE | F_PORT1ACTIVE,
			 F_PORT0ACTIVE | F_PORT1ACTIVE);
}

static int t3_cim_hac_read(adapter_t *adapter, u32 addr, u32 *val)
{
	u32 v;

	t3_write_reg(adapter, A_CIM_HOST_ACC_CTRL, addr);
	if (t3_wait_op_done_val(adapter, A_CIM_HOST_ACC_CTRL,
				F_HOSTBUSY, 0, 10, 10, &v))
		return -EIO;

	*val = t3_read_reg(adapter, A_CIM_HOST_ACC_DATA);

	return 0;
}

static int t3_cim_hac_write(adapter_t *adapter, u32 addr, u32 val)
{
	u32 v;

	t3_write_reg(adapter, A_CIM_HOST_ACC_DATA, val);

	addr |= F_HOSTWRITE;
	t3_write_reg(adapter, A_CIM_HOST_ACC_CTRL, addr);

	if (t3_wait_op_done_val(adapter, A_CIM_HOST_ACC_CTRL,
				F_HOSTBUSY, 0, 10, 5, &v))
		return -EIO;
	return 0;
}

int t3_get_up_la(adapter_t *adapter, u32 *stopped, u32 *index,
		 u32 *size, void *data)
{
	u32 v, *buf = data;
	int i, cnt,  ret;

	if (*size < LA_ENTRIES * 4)
		return -EINVAL;

	ret = t3_cim_hac_read(adapter, LA_CTRL, &v);
	if (ret)
		goto out;

	*stopped = !(v & 1);

	/* Freeze LA */
	if (!*stopped) {
		ret = t3_cim_hac_write(adapter, LA_CTRL, 0);
		if (ret)
			goto out;
	}

	for (i = 0; i < LA_ENTRIES; i++) {
		v = (i << 2) | (1 << 1);
		ret = t3_cim_hac_write(adapter, LA_CTRL, v);
		if (ret)
			goto out;

		ret = t3_cim_hac_read(adapter, LA_CTRL, &v);
		if (ret)
			goto out;

		cnt = 20;
		while ((v & (1 << 1)) && cnt) {
			udelay(5);
			--cnt;
			ret = t3_cim_hac_read(adapter, LA_CTRL, &v);
			if (ret)
				goto out;
		}

		if (v & (1 << 1))
			return -EIO;

		ret = t3_cim_hac_read(adapter, LA_DATA, &v);
		if (ret)
			goto out;

		*buf++ = v;
	}

	ret = t3_cim_hac_read(adapter, LA_CTRL, &v);
	if (ret)
		goto out;

	*index = (v >> 16) + 4;
	*size = LA_ENTRIES * 4;
out:
	/* Unfreeze LA */
	t3_cim_hac_write(adapter, LA_CTRL, 1);
	return ret;
}

int t3_get_up_ioqs(adapter_t *adapter, u32 *size, void *data)
{
	u32 v, *buf = data;
	int i, j, ret;

	if (*size < IOQ_ENTRIES * sizeof(struct t3_ioq_entry))
		return -EINVAL;

	for (i = 0; i < 4; i++) {
		ret = t3_cim_hac_read(adapter, (4 * i), &v);
		if (ret)
			goto out;

		*buf++ = v;
	}

	for (i = 0; i < IOQ_ENTRIES; i++) {
		u32 base_addr = 0x10 * (i + 1);		

		for (j = 0; j < 4; j++) {
			ret = t3_cim_hac_read(adapter, base_addr + 4 * j, &v);
			if (ret)
				goto out;

			*buf++ = v;
		}
	}
	
	*size = IOQ_ENTRIES * sizeof(struct t3_ioq_entry);

out:
	return ret;
}
	
