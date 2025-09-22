/*
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2008,2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 */

#include <linux/export.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>

#include <drm/display/drm_hdcp_helper.h>

#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_gmbus.h"
#include "intel_gmbus_regs.h"

struct intel_gmbus {
	struct i2c_adapter adapter;
#define GMBUS_FORCE_BIT_RETRY (1U << 31)
	u32 force_bit;
	u32 reg0;
	i915_reg_t gpio_reg;
	struct i2c_algo_bit_data bit_algo;
	struct drm_i915_private *i915;
};

enum gmbus_gpio {
	GPIOA,
	GPIOB,
	GPIOC,
	GPIOD,
	GPIOE,
	GPIOF,
	GPIOG,
	GPIOH,
	__GPIOI_UNUSED,
	GPIOJ,
	GPIOK,
	GPIOL,
	GPIOM,
	GPION,
	GPIOO,
};

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/i2c_bitbang.h>

struct gmbus_pin {
	const char *name;
	enum gmbus_gpio gpio;
};

/* Map gmbus pin pairs to names and registers. */
static const struct gmbus_pin gmbus_pins[] = {
	[GMBUS_PIN_SSC] = { "ssc", GPIOB },
	[GMBUS_PIN_VGADDC] = { "vga", GPIOA },
	[GMBUS_PIN_PANEL] = { "panel", GPIOC },
	[GMBUS_PIN_DPC] = { "dpc", GPIOD },
	[GMBUS_PIN_DPB] = { "dpb", GPIOE },
	[GMBUS_PIN_DPD] = { "dpd", GPIOF },
};

static const struct gmbus_pin gmbus_pins_bdw[] = {
	[GMBUS_PIN_VGADDC] = { "vga", GPIOA },
	[GMBUS_PIN_DPC] = { "dpc", GPIOD },
	[GMBUS_PIN_DPB] = { "dpb", GPIOE },
	[GMBUS_PIN_DPD] = { "dpd", GPIOF },
};

static const struct gmbus_pin gmbus_pins_skl[] = {
	[GMBUS_PIN_DPC] = { "dpc", GPIOD },
	[GMBUS_PIN_DPB] = { "dpb", GPIOE },
	[GMBUS_PIN_DPD] = { "dpd", GPIOF },
};

static const struct gmbus_pin gmbus_pins_bxt[] = {
	[GMBUS_PIN_1_BXT] = { "dpb", GPIOB },
	[GMBUS_PIN_2_BXT] = { "dpc", GPIOC },
	[GMBUS_PIN_3_BXT] = { "misc", GPIOD },
};

static const struct gmbus_pin gmbus_pins_cnp[] = {
	[GMBUS_PIN_1_BXT] = { "dpb", GPIOB },
	[GMBUS_PIN_2_BXT] = { "dpc", GPIOC },
	[GMBUS_PIN_3_BXT] = { "misc", GPIOD },
	[GMBUS_PIN_4_CNP] = { "dpd", GPIOE },
};

static const struct gmbus_pin gmbus_pins_icp[] = {
	[GMBUS_PIN_1_BXT] = { "dpa", GPIOB },
	[GMBUS_PIN_2_BXT] = { "dpb", GPIOC },
	[GMBUS_PIN_3_BXT] = { "dpc", GPIOD },
	[GMBUS_PIN_9_TC1_ICP] = { "tc1", GPIOJ },
	[GMBUS_PIN_10_TC2_ICP] = { "tc2", GPIOK },
	[GMBUS_PIN_11_TC3_ICP] = { "tc3", GPIOL },
	[GMBUS_PIN_12_TC4_ICP] = { "tc4", GPIOM },
	[GMBUS_PIN_13_TC5_TGP] = { "tc5", GPION },
	[GMBUS_PIN_14_TC6_TGP] = { "tc6", GPIOO },
};

static const struct gmbus_pin gmbus_pins_dg1[] = {
	[GMBUS_PIN_1_BXT] = { "dpa", GPIOB },
	[GMBUS_PIN_2_BXT] = { "dpb", GPIOC },
	[GMBUS_PIN_3_BXT] = { "dpc", GPIOD },
	[GMBUS_PIN_4_CNP] = { "dpd", GPIOE },
};

static const struct gmbus_pin gmbus_pins_dg2[] = {
	[GMBUS_PIN_1_BXT] = { "dpa", GPIOB },
	[GMBUS_PIN_2_BXT] = { "dpb", GPIOC },
	[GMBUS_PIN_3_BXT] = { "dpc", GPIOD },
	[GMBUS_PIN_4_CNP] = { "dpd", GPIOE },
	[GMBUS_PIN_9_TC1_ICP] = { "tc1", GPIOJ },
};

static const struct gmbus_pin gmbus_pins_mtp[] = {
	[GMBUS_PIN_1_BXT] = { "dpa", GPIOB },
	[GMBUS_PIN_2_BXT] = { "dpb", GPIOC },
	[GMBUS_PIN_3_BXT] = { "dpc", GPIOD },
	[GMBUS_PIN_4_CNP] = { "dpd", GPIOE },
	[GMBUS_PIN_5_MTP] = { "dpe", GPIOF },
	[GMBUS_PIN_9_TC1_ICP] = { "tc1", GPIOJ },
	[GMBUS_PIN_10_TC2_ICP] = { "tc2", GPIOK },
	[GMBUS_PIN_11_TC3_ICP] = { "tc3", GPIOL },
	[GMBUS_PIN_12_TC4_ICP] = { "tc4", GPIOM },
};

static const struct gmbus_pin *get_gmbus_pin(struct drm_i915_private *i915,
					     unsigned int pin)
{
	const struct gmbus_pin *pins;
	size_t size;

	if (INTEL_PCH_TYPE(i915) >= PCH_MTL) {
		pins = gmbus_pins_mtp;
		size = ARRAY_SIZE(gmbus_pins_mtp);
	} else if (INTEL_PCH_TYPE(i915) >= PCH_DG2) {
		pins = gmbus_pins_dg2;
		size = ARRAY_SIZE(gmbus_pins_dg2);
	} else if (INTEL_PCH_TYPE(i915) >= PCH_DG1) {
		pins = gmbus_pins_dg1;
		size = ARRAY_SIZE(gmbus_pins_dg1);
	} else if (INTEL_PCH_TYPE(i915) >= PCH_ICP) {
		pins = gmbus_pins_icp;
		size = ARRAY_SIZE(gmbus_pins_icp);
	} else if (HAS_PCH_CNP(i915)) {
		pins = gmbus_pins_cnp;
		size = ARRAY_SIZE(gmbus_pins_cnp);
	} else if (IS_GEMINILAKE(i915) || IS_BROXTON(i915)) {
		pins = gmbus_pins_bxt;
		size = ARRAY_SIZE(gmbus_pins_bxt);
	} else if (DISPLAY_VER(i915) == 9) {
		pins = gmbus_pins_skl;
		size = ARRAY_SIZE(gmbus_pins_skl);
	} else if (IS_BROADWELL(i915)) {
		pins = gmbus_pins_bdw;
		size = ARRAY_SIZE(gmbus_pins_bdw);
	} else {
		pins = gmbus_pins;
		size = ARRAY_SIZE(gmbus_pins);
	}

	if (pin >= size || !pins[pin].name)
		return NULL;

	return &pins[pin];
}

bool intel_gmbus_is_valid_pin(struct drm_i915_private *i915, unsigned int pin)
{
	return get_gmbus_pin(i915, pin);
}

/* Intel GPIO access functions */

#define I2C_RISEFALL_TIME 10

static inline struct intel_gmbus *
to_intel_gmbus(struct i2c_adapter *i2c)
{
	return container_of(i2c, struct intel_gmbus, adapter);
}

void
intel_gmbus_reset(struct drm_i915_private *i915)
{
	intel_de_write(i915, GMBUS0(i915), 0);
	intel_de_write(i915, GMBUS4(i915), 0);
}

static void pnv_gmbus_clock_gating(struct drm_i915_private *i915,
				   bool enable)
{
	/* When using bit bashing for I2C, this bit needs to be set to 1 */
	intel_de_rmw(i915, DSPCLK_GATE_D(i915), PNV_GMBUSUNIT_CLOCK_GATE_DISABLE,
		     !enable ? PNV_GMBUSUNIT_CLOCK_GATE_DISABLE : 0);
}

static void pch_gmbus_clock_gating(struct drm_i915_private *i915,
				   bool enable)
{
	intel_de_rmw(i915, SOUTH_DSPCLK_GATE_D, PCH_GMBUSUNIT_CLOCK_GATE_DISABLE,
		     !enable ? PCH_GMBUSUNIT_CLOCK_GATE_DISABLE : 0);
}

static void bxt_gmbus_clock_gating(struct drm_i915_private *i915,
				   bool enable)
{
	intel_de_rmw(i915, GEN9_CLKGATE_DIS_4, BXT_GMBUS_GATING_DIS,
		     !enable ? BXT_GMBUS_GATING_DIS : 0);
}

static u32 get_reserved(struct intel_gmbus *bus)
{
	struct drm_i915_private *i915 = bus->i915;
	u32 reserved = 0;

	/* On most chips, these bits must be preserved in software. */
	if (!IS_I830(i915) && !IS_I845G(i915))
		reserved = intel_de_read_notrace(i915, bus->gpio_reg) &
			(GPIO_DATA_PULLUP_DISABLE | GPIO_CLOCK_PULLUP_DISABLE);

	return reserved;
}

static int get_clock(void *data)
{
	struct intel_gmbus *bus = data;
	struct drm_i915_private *i915 = bus->i915;
	u32 reserved = get_reserved(bus);

	intel_de_write_notrace(i915, bus->gpio_reg, reserved | GPIO_CLOCK_DIR_MASK);
	intel_de_write_notrace(i915, bus->gpio_reg, reserved);

	return (intel_de_read_notrace(i915, bus->gpio_reg) & GPIO_CLOCK_VAL_IN) != 0;
}

static int get_data(void *data)
{
	struct intel_gmbus *bus = data;
	struct drm_i915_private *i915 = bus->i915;
	u32 reserved = get_reserved(bus);

	intel_de_write_notrace(i915, bus->gpio_reg, reserved | GPIO_DATA_DIR_MASK);
	intel_de_write_notrace(i915, bus->gpio_reg, reserved);

	return (intel_de_read_notrace(i915, bus->gpio_reg) & GPIO_DATA_VAL_IN) != 0;
}

static void set_clock(void *data, int state_high)
{
	struct intel_gmbus *bus = data;
	struct drm_i915_private *i915 = bus->i915;
	u32 reserved = get_reserved(bus);
	u32 clock_bits;

	if (state_high)
		clock_bits = GPIO_CLOCK_DIR_IN | GPIO_CLOCK_DIR_MASK;
	else
		clock_bits = GPIO_CLOCK_DIR_OUT | GPIO_CLOCK_DIR_MASK |
			     GPIO_CLOCK_VAL_MASK;

	intel_de_write_notrace(i915, bus->gpio_reg, reserved | clock_bits);
	intel_de_posting_read(i915, bus->gpio_reg);
}

static void set_data(void *data, int state_high)
{
	struct intel_gmbus *bus = data;
	struct drm_i915_private *i915 = bus->i915;
	u32 reserved = get_reserved(bus);
	u32 data_bits;

	if (state_high)
		data_bits = GPIO_DATA_DIR_IN | GPIO_DATA_DIR_MASK;
	else
		data_bits = GPIO_DATA_DIR_OUT | GPIO_DATA_DIR_MASK |
			GPIO_DATA_VAL_MASK;

	intel_de_write_notrace(i915, bus->gpio_reg, reserved | data_bits);
	intel_de_posting_read(i915, bus->gpio_reg);
}

static int
intel_gpio_pre_xfer(struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;

	intel_gmbus_reset(i915);

	if (IS_PINEVIEW(i915))
		pnv_gmbus_clock_gating(i915, false);

	set_data(bus, 1);
	set_clock(bus, 1);
	udelay(I2C_RISEFALL_TIME);
	return 0;
}

static void
intel_gpio_post_xfer(struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;

	set_data(bus, 1);
	set_clock(bus, 1);

	if (IS_PINEVIEW(i915))
		pnv_gmbus_clock_gating(i915, true);
}

void	intel_bb_set_bits(void *, uint32_t);
void	intel_bb_set_dir(void *, uint32_t);
uint32_t intel_bb_read_bits(void *);

int	intel_acquire_bus(void *, int);
void	intel_release_bus(void *, int);
int	intel_send_start(void *, int);
int	intel_send_stop(void *, int);
int	intel_initiate_xfer(void *, i2c_addr_t, int);
int	intel_read_byte(void *, u_int8_t *, int);
int	intel_write_byte(void *, u_int8_t, int);

#define INTEL_BB_SDA		(1 << I2C_BIT_SDA)
#define INTEL_BB_SCL		(1 << I2C_BIT_SCL)

struct i2c_bitbang_ops intel_bbops = {
	intel_bb_set_bits,
	intel_bb_set_dir,
	intel_bb_read_bits,
	{ INTEL_BB_SDA, INTEL_BB_SCL, 0, 0 }
};

void
intel_bb_set_bits(void *cookie, uint32_t bits)
{
	set_clock(cookie, bits & INTEL_BB_SCL);
	set_data(cookie, bits & INTEL_BB_SDA);
}

void
intel_bb_set_dir(void *cookie, uint32_t bits)
{
}

uint32_t
intel_bb_read_bits(void *cookie)
{
	uint32_t bits = 0;

	if (get_clock(cookie))
		bits |= INTEL_BB_SCL;
	if (get_data(cookie))
		bits |= INTEL_BB_SDA;

	return bits;
}

int
intel_acquire_bus(void *cookie, int flags)
{
	struct intel_gmbus *bus = cookie;

	intel_gpio_pre_xfer(&bus->adapter);
	return (0);
}

void
intel_release_bus(void *cookie, int flags)
{
	struct intel_gmbus *bus = cookie;

	intel_gpio_post_xfer(&bus->adapter);
}

int
intel_send_start(void *cookie, int flags)
{
	return (i2c_bitbang_send_start(cookie, flags, &intel_bbops));
}

int
intel_send_stop(void *cookie, int flags)
{
	return (i2c_bitbang_send_stop(cookie, flags, &intel_bbops));
}

int
intel_initiate_xfer(void *cookie, i2c_addr_t addr, int flags)
{
	return (i2c_bitbang_initiate_xfer(cookie, addr, flags, &intel_bbops));
}

int
intel_read_byte(void *cookie, u_int8_t *bytep, int flags)
{
	return (i2c_bitbang_read_byte(cookie, bytep, flags, &intel_bbops));
}

int
intel_write_byte(void *cookie, u_int8_t byte, int flags)
{
	return (i2c_bitbang_write_byte(cookie, byte, flags, &intel_bbops));
}

static void
intel_gpio_setup(struct intel_gmbus *bus, i915_reg_t gpio_reg)
{
	struct i2c_algo_bit_data *algo;

	algo = &bus->bit_algo;

	bus->gpio_reg = gpio_reg;
	bus->adapter.algo_data = algo;
#ifdef __linux__
	algo->setsda = set_data;
	algo->setscl = set_clock;
	algo->getsda = get_data;
	algo->getscl = get_clock;
	algo->pre_xfer = intel_gpio_pre_xfer;
	algo->post_xfer = intel_gpio_post_xfer;
	algo->udelay = I2C_RISEFALL_TIME;
	algo->timeout = usecs_to_jiffies(2200);
	algo->data = bus;
#else
	algo->ic.ic_cookie = bus;
	algo->ic.ic_acquire_bus = intel_acquire_bus;
	algo->ic.ic_release_bus = intel_release_bus;
	algo->ic.ic_send_start = intel_send_start;
	algo->ic.ic_send_stop = intel_send_stop;
	algo->ic.ic_initiate_xfer = intel_initiate_xfer;
	algo->ic.ic_read_byte = intel_read_byte;
	algo->ic.ic_write_byte = intel_write_byte;
#endif
}

static bool has_gmbus_irq(struct drm_i915_private *i915)
{
	/*
	 * encoder->shutdown() may want to use GMBUS
	 * after irqs have already been disabled.
	 */
	return HAS_GMBUS_IRQ(i915) && intel_irqs_enabled(i915);
}

static int gmbus_wait(struct drm_i915_private *i915, u32 status, u32 irq_en)
{
	DEFINE_WAIT(wait);
	u32 gmbus2;
	int ret;

	/* Important: The hw handles only the first bit, so set only one! Since
	 * we also need to check for NAKs besides the hw ready/idle signal, we
	 * need to wake up periodically and check that ourselves.
	 */
	if (!has_gmbus_irq(i915) || cold)
		irq_en = 0;

	add_wait_queue(&i915->display.gmbus.wait_queue, &wait);
	intel_de_write_fw(i915, GMBUS4(i915), irq_en);

	status |= GMBUS_SATOER;
	ret = wait_for_us((gmbus2 = intel_de_read_fw(i915, GMBUS2(i915))) & status,
			  2);
	if (ret)
		ret = wait_for((gmbus2 = intel_de_read_fw(i915, GMBUS2(i915))) & status,
			       50);

	intel_de_write_fw(i915, GMBUS4(i915), 0);
	remove_wait_queue(&i915->display.gmbus.wait_queue, &wait);

	if (gmbus2 & GMBUS_SATOER)
		return -ENXIO;

	return ret;
}

static int
gmbus_wait_idle(struct drm_i915_private *i915)
{
	DEFINE_WAIT(wait);
	u32 irq_enable;
	int ret;

	/* Important: The hw handles only the first bit, so set only one! */
	irq_enable = 0;
	if (has_gmbus_irq(i915) && !cold)
		irq_enable = GMBUS_IDLE_EN;

	add_wait_queue(&i915->display.gmbus.wait_queue, &wait);
	intel_de_write_fw(i915, GMBUS4(i915), irq_enable);

	ret = intel_de_wait_fw(i915, GMBUS2(i915), GMBUS_ACTIVE, 0, 10);

	intel_de_write_fw(i915, GMBUS4(i915), 0);
	remove_wait_queue(&i915->display.gmbus.wait_queue, &wait);

	return ret;
}

static unsigned int gmbus_max_xfer_size(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) >= 9 ? GEN9_GMBUS_BYTE_COUNT_MAX :
	       GMBUS_BYTE_COUNT_MAX;
}

static int
gmbus_xfer_read_chunk(struct drm_i915_private *i915,
		      unsigned short addr, u8 *buf, unsigned int len,
		      u32 gmbus0_reg, u32 gmbus1_index)
{
	unsigned int size = len;
	bool burst_read = len > gmbus_max_xfer_size(i915);
	bool extra_byte_added = false;

	if (burst_read) {
		/*
		 * As per HW Spec, for 512Bytes need to read extra Byte and
		 * Ignore the extra byte read.
		 */
		if (len == 512) {
			extra_byte_added = true;
			len++;
		}
		size = len % 256 + 256;
		intel_de_write_fw(i915, GMBUS0(i915),
				  gmbus0_reg | GMBUS_BYTE_CNT_OVERRIDE);
	}

	intel_de_write_fw(i915, GMBUS1(i915),
			  gmbus1_index | GMBUS_CYCLE_WAIT | (size << GMBUS_BYTE_COUNT_SHIFT) | (addr << GMBUS_SLAVE_ADDR_SHIFT) | GMBUS_SLAVE_READ | GMBUS_SW_RDY);
	while (len) {
		int ret;
		u32 val, loop = 0;

		ret = gmbus_wait(i915, GMBUS_HW_RDY, GMBUS_HW_RDY_EN);
		if (ret)
			return ret;

		val = intel_de_read_fw(i915, GMBUS3(i915));
		do {
			if (extra_byte_added && len == 1)
				break;

			*buf++ = val & 0xff;
			val >>= 8;
		} while (--len && ++loop < 4);

		if (burst_read && len == size - 4)
			/* Reset the override bit */
			intel_de_write_fw(i915, GMBUS0(i915), gmbus0_reg);
	}

	return 0;
}

/*
 * HW spec says that 512Bytes in Burst read need special treatment.
 * But it doesn't talk about other multiple of 256Bytes. And couldn't locate
 * an I2C target, which supports such a lengthy burst read too for experiments.
 *
 * So until things get clarified on HW support, to avoid the burst read length
 * in fold of 256Bytes except 512, max burst read length is fixed at 767Bytes.
 */
#define INTEL_GMBUS_BURST_READ_MAX_LEN		767U

static int
gmbus_xfer_read(struct drm_i915_private *i915, struct i2c_msg *msg,
		u32 gmbus0_reg, u32 gmbus1_index)
{
	u8 *buf = msg->buf;
	unsigned int rx_size = msg->len;
	unsigned int len;
	int ret;

	do {
		if (HAS_GMBUS_BURST_READ(i915))
			len = min(rx_size, INTEL_GMBUS_BURST_READ_MAX_LEN);
		else
			len = min(rx_size, gmbus_max_xfer_size(i915));

		ret = gmbus_xfer_read_chunk(i915, msg->addr, buf, len,
					    gmbus0_reg, gmbus1_index);
		if (ret)
			return ret;

		rx_size -= len;
		buf += len;
	} while (rx_size != 0);

	return 0;
}

static int
gmbus_xfer_write_chunk(struct drm_i915_private *i915,
		       unsigned short addr, u8 *buf, unsigned int len,
		       u32 gmbus1_index)
{
	unsigned int chunk_size = len;
	u32 val, loop;

	val = loop = 0;
	while (len && loop < 4) {
		val |= *buf++ << (8 * loop++);
		len -= 1;
	}

	intel_de_write_fw(i915, GMBUS3(i915), val);
	intel_de_write_fw(i915, GMBUS1(i915),
			  gmbus1_index | GMBUS_CYCLE_WAIT | (chunk_size << GMBUS_BYTE_COUNT_SHIFT) | (addr << GMBUS_SLAVE_ADDR_SHIFT) | GMBUS_SLAVE_WRITE | GMBUS_SW_RDY);
	while (len) {
		int ret;

		val = loop = 0;
		do {
			val |= *buf++ << (8 * loop);
		} while (--len && ++loop < 4);

		intel_de_write_fw(i915, GMBUS3(i915), val);

		ret = gmbus_wait(i915, GMBUS_HW_RDY, GMBUS_HW_RDY_EN);
		if (ret)
			return ret;
	}

	return 0;
}

static int
gmbus_xfer_write(struct drm_i915_private *i915, struct i2c_msg *msg,
		 u32 gmbus1_index)
{
	u8 *buf = msg->buf;
	unsigned int tx_size = msg->len;
	unsigned int len;
	int ret;

	do {
		len = min(tx_size, gmbus_max_xfer_size(i915));

		ret = gmbus_xfer_write_chunk(i915, msg->addr, buf, len,
					     gmbus1_index);
		if (ret)
			return ret;

		buf += len;
		tx_size -= len;
	} while (tx_size != 0);

	return 0;
}

/*
 * The gmbus controller can combine a 1 or 2 byte write with another read/write
 * that immediately follows it by using an "INDEX" cycle.
 */
static bool
gmbus_is_index_xfer(struct i2c_msg *msgs, int i, int num)
{
	return (i + 1 < num &&
		msgs[i].addr == msgs[i + 1].addr &&
		!(msgs[i].flags & I2C_M_RD) &&
		(msgs[i].len == 1 || msgs[i].len == 2) &&
		msgs[i + 1].len > 0);
}

static int
gmbus_index_xfer(struct drm_i915_private *i915, struct i2c_msg *msgs,
		 u32 gmbus0_reg)
{
	u32 gmbus1_index = 0;
	u32 gmbus5 = 0;
	int ret;

	if (msgs[0].len == 2)
		gmbus5 = GMBUS_2BYTE_INDEX_EN |
			 msgs[0].buf[1] | (msgs[0].buf[0] << 8);
	if (msgs[0].len == 1)
		gmbus1_index = GMBUS_CYCLE_INDEX |
			       (msgs[0].buf[0] << GMBUS_SLAVE_INDEX_SHIFT);

	/* GMBUS5 holds 16-bit index */
	if (gmbus5)
		intel_de_write_fw(i915, GMBUS5(i915), gmbus5);

	if (msgs[1].flags & I2C_M_RD)
		ret = gmbus_xfer_read(i915, &msgs[1], gmbus0_reg,
				      gmbus1_index);
	else
		ret = gmbus_xfer_write(i915, &msgs[1], gmbus1_index);

	/* Clear GMBUS5 after each index transfer */
	if (gmbus5)
		intel_de_write_fw(i915, GMBUS5(i915), 0);

	return ret;
}

static int
do_gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num,
	      u32 gmbus0_source)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;
	int i = 0, inc, try = 0;
	int ret = 0;

	/* Display WA #0868: skl,bxt,kbl,cfl,glk */
	if (IS_GEMINILAKE(i915) || IS_BROXTON(i915))
		bxt_gmbus_clock_gating(i915, false);
	else if (HAS_PCH_SPT(i915) || HAS_PCH_CNP(i915))
		pch_gmbus_clock_gating(i915, false);

retry:
	intel_de_write_fw(i915, GMBUS0(i915), gmbus0_source | bus->reg0);

	for (; i < num; i += inc) {
		inc = 1;
		if (gmbus_is_index_xfer(msgs, i, num)) {
			ret = gmbus_index_xfer(i915, &msgs[i],
					       gmbus0_source | bus->reg0);
			inc = 2; /* an index transmission is two msgs */
		} else if (msgs[i].flags & I2C_M_RD) {
			ret = gmbus_xfer_read(i915, &msgs[i],
					      gmbus0_source | bus->reg0, 0);
		} else {
			ret = gmbus_xfer_write(i915, &msgs[i], 0);
		}

		if (!ret)
			ret = gmbus_wait(i915,
					 GMBUS_HW_WAIT_PHASE, GMBUS_HW_WAIT_EN);
		if (ret == -ETIMEDOUT)
			goto timeout;
		else if (ret)
			goto clear_err;
	}

	/* Generate a STOP condition on the bus. Note that gmbus can't generata
	 * a STOP on the very first cycle. To simplify the code we
	 * unconditionally generate the STOP condition with an additional gmbus
	 * cycle. */
	intel_de_write_fw(i915, GMBUS1(i915), GMBUS_CYCLE_STOP | GMBUS_SW_RDY);

	/* Mark the GMBUS interface as disabled after waiting for idle.
	 * We will re-enable it at the start of the next xfer,
	 * till then let it sleep.
	 */
	if (gmbus_wait_idle(i915)) {
		drm_dbg_kms(&i915->drm,
			    "GMBUS [%s] timed out waiting for idle\n",
			    adapter->name);
		ret = -ETIMEDOUT;
	}
	intel_de_write_fw(i915, GMBUS0(i915), 0);
	ret = ret ?: i;
	goto out;

clear_err:
	/*
	 * Wait for bus to IDLE before clearing NAK.
	 * If we clear the NAK while bus is still active, then it will stay
	 * active and the next transaction may fail.
	 *
	 * If no ACK is received during the address phase of a transaction, the
	 * adapter must report -ENXIO. It is not clear what to return if no ACK
	 * is received at other times. But we have to be careful to not return
	 * spurious -ENXIO because that will prevent i2c and drm edid functions
	 * from retrying. So return -ENXIO only when gmbus properly quiescents -
	 * timing out seems to happen when there _is_ a ddc chip present, but
	 * it's slow responding and only answers on the 2nd retry.
	 */
	ret = -ENXIO;
	if (gmbus_wait_idle(i915)) {
		drm_dbg_kms(&i915->drm,
			    "GMBUS [%s] timed out after NAK\n",
			    adapter->name);
		ret = -ETIMEDOUT;
	}

	/* Toggle the Software Clear Interrupt bit. This has the effect
	 * of resetting the GMBUS controller and so clearing the
	 * BUS_ERROR raised by the target's NAK.
	 */
	intel_de_write_fw(i915, GMBUS1(i915), GMBUS_SW_CLR_INT);
	intel_de_write_fw(i915, GMBUS1(i915), 0);
	intel_de_write_fw(i915, GMBUS0(i915), 0);

	drm_dbg_kms(&i915->drm, "GMBUS [%s] NAK for addr: %04x %c(%d)\n",
		    adapter->name, msgs[i].addr,
		    (msgs[i].flags & I2C_M_RD) ? 'r' : 'w', msgs[i].len);

	/*
	 * Passive adapters sometimes NAK the first probe. Retry the first
	 * message once on -ENXIO for GMBUS transfers; the bit banging algorithm
	 * has retries internally. See also the retry loop in
	 * drm_do_probe_ddc_edid, which bails out on the first -ENXIO.
	 */
	if (ret == -ENXIO && i == 0 && try++ == 0) {
		drm_dbg_kms(&i915->drm,
			    "GMBUS [%s] NAK on first message, retry\n",
			    adapter->name);
		goto retry;
	}

	goto out;

timeout:
	drm_dbg_kms(&i915->drm,
		    "GMBUS [%s] timed out, falling back to bit banging on pin %d\n",
		    bus->adapter.name, bus->reg0 & 0xff);
	intel_de_write_fw(i915, GMBUS0(i915), 0);

	/*
	 * Hardware may not support GMBUS over these pins? Try GPIO bitbanging
	 * instead. Use EAGAIN to have i2c core retry.
	 */
	ret = -EAGAIN;

out:
	/* Display WA #0868: skl,bxt,kbl,cfl,glk */
	if (IS_GEMINILAKE(i915) || IS_BROXTON(i915))
		bxt_gmbus_clock_gating(i915, true);
	else if (HAS_PCH_SPT(i915) || HAS_PCH_CNP(i915))
		pch_gmbus_clock_gating(i915, true);

	return ret;
}

static int
gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;
	intel_wakeref_t wakeref;
	int ret;

	wakeref = intel_display_power_get(i915, POWER_DOMAIN_GMBUS);

	if (bus->force_bit) {
		ret = i2c_bit_algo.master_xfer(adapter, msgs, num);
		if (ret < 0)
			bus->force_bit &= ~GMBUS_FORCE_BIT_RETRY;
	} else {
		ret = do_gmbus_xfer(adapter, msgs, num, 0);
		if (ret == -EAGAIN)
			bus->force_bit |= GMBUS_FORCE_BIT_RETRY;
	}

	intel_display_power_put(i915, POWER_DOMAIN_GMBUS, wakeref);

	return ret;
}

int intel_gmbus_output_aksv(struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;
	u8 cmd = DRM_HDCP_DDC_AKSV;
	u8 buf[DRM_HDCP_KSV_LEN] = {};
	struct i2c_msg msgs[] = {
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = 0,
			.len = sizeof(cmd),
			.buf = &cmd,
		},
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = 0,
			.len = sizeof(buf),
			.buf = buf,
		}
	};
	intel_wakeref_t wakeref;
	int ret;

	wakeref = intel_display_power_get(i915, POWER_DOMAIN_GMBUS);
	mutex_lock(&i915->display.gmbus.mutex);

	/*
	 * In order to output Aksv to the receiver, use an indexed write to
	 * pass the i2c command, and tell GMBUS to use the HW-provided value
	 * instead of sourcing GMBUS3 for the data.
	 */
	ret = do_gmbus_xfer(adapter, msgs, ARRAY_SIZE(msgs), GMBUS_AKSV_SELECT);

	mutex_unlock(&i915->display.gmbus.mutex);
	intel_display_power_put(i915, POWER_DOMAIN_GMBUS, wakeref);

	return ret;
}

static u32 gmbus_func(struct i2c_adapter *adapter)
{
	return i2c_bit_algo.functionality(adapter) &
		(I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
		/* I2C_FUNC_10BIT_ADDR | */
		I2C_FUNC_SMBUS_READ_BLOCK_DATA |
		I2C_FUNC_SMBUS_BLOCK_PROC_CALL);
}

static const struct i2c_algorithm gmbus_algorithm = {
	.master_xfer	= gmbus_xfer,
	.functionality	= gmbus_func
};

static void gmbus_lock_bus(struct i2c_adapter *adapter,
			   unsigned int flags)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;

	mutex_lock(&i915->display.gmbus.mutex);
}

static int gmbus_trylock_bus(struct i2c_adapter *adapter,
			     unsigned int flags)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;

	return mutex_trylock(&i915->display.gmbus.mutex);
}

static void gmbus_unlock_bus(struct i2c_adapter *adapter,
			     unsigned int flags)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;

	mutex_unlock(&i915->display.gmbus.mutex);
}

static const struct i2c_lock_operations gmbus_lock_ops = {
	.lock_bus =    gmbus_lock_bus,
	.trylock_bus = gmbus_trylock_bus,
	.unlock_bus =  gmbus_unlock_bus,
};

/**
 * intel_gmbus_setup - instantiate all Intel i2c GMBuses
 * @i915: i915 device private
 */
int intel_gmbus_setup(struct drm_i915_private *i915)
{
#ifdef notyet
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
#endif
	unsigned int pin;
	int ret;

	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		i915->display.gmbus.mmio_base = VLV_DISPLAY_BASE;
	else if (!HAS_GMCH(i915))
		/*
		 * Broxton uses the same PCH offsets for South Display Engine,
		 * even though it doesn't have a PCH.
		 */
		i915->display.gmbus.mmio_base = PCH_DISPLAY_BASE;

	rw_init(&i915->display.gmbus.mutex, "gmbus");
	init_waitqueue_head(&i915->display.gmbus.wait_queue);

	for (pin = 0; pin < ARRAY_SIZE(i915->display.gmbus.bus); pin++) {
		const struct gmbus_pin *gmbus_pin;
		struct intel_gmbus *bus;

		gmbus_pin = get_gmbus_pin(i915, pin);
		if (!gmbus_pin)
			continue;

		bus = kzalloc(sizeof(*bus), GFP_KERNEL);
		if (!bus) {
			ret = -ENOMEM;
			goto err;
		}

#ifdef notyet
		bus->adapter.owner = THIS_MODULE;
#endif
		snprintf(bus->adapter.name,
			 sizeof(bus->adapter.name),
			 "i915 gmbus %s", gmbus_pin->name);

#ifdef notyet
		bus->adapter.dev.parent = &pdev->dev;
#endif
		bus->i915 = i915;

		bus->adapter.algo = &gmbus_algorithm;
		bus->adapter.lock_ops = &gmbus_lock_ops;

		/*
		 * We wish to retry with bit banging
		 * after a timed out GMBUS attempt.
		 */
		bus->adapter.retries = 1;

		/* By default use a conservative clock rate */
		bus->reg0 = pin | GMBUS_RATE_100KHZ;

		/* gmbus seems to be broken on i830 */
		if (IS_I830(i915))
			bus->force_bit = 1;

		intel_gpio_setup(bus, GPIO(i915, gmbus_pin->gpio));

		ret = i2c_add_adapter(&bus->adapter);
		if (ret) {
			kfree(bus);
			goto err;
		}

		i915->display.gmbus.bus[pin] = bus;
	}

	intel_gmbus_reset(i915);

	return 0;

err:
	intel_gmbus_teardown(i915);

	return ret;
}

struct i2c_adapter *intel_gmbus_get_adapter(struct drm_i915_private *i915,
					    unsigned int pin)
{
	if (drm_WARN_ON(&i915->drm, pin >= ARRAY_SIZE(i915->display.gmbus.bus) ||
			!i915->display.gmbus.bus[pin]))
		return NULL;

	return &i915->display.gmbus.bus[pin]->adapter;
}

void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *i915 = bus->i915;

	mutex_lock(&i915->display.gmbus.mutex);

	bus->force_bit += force_bit ? 1 : -1;
	drm_dbg_kms(&i915->drm,
		    "%sabling bit-banging on %s. force bit now %d\n",
		    force_bit ? "en" : "dis", adapter->name,
		    bus->force_bit);

	mutex_unlock(&i915->display.gmbus.mutex);
}

bool intel_gmbus_is_forced_bit(struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);

	return bus->force_bit;
}

void intel_gmbus_teardown(struct drm_i915_private *i915)
{
	unsigned int pin;

	for (pin = 0; pin < ARRAY_SIZE(i915->display.gmbus.bus); pin++) {
		struct intel_gmbus *bus;

		bus = i915->display.gmbus.bus[pin];
		if (!bus)
			continue;

		i2c_del_adapter(&bus->adapter);

		kfree(bus);
		i915->display.gmbus.bus[pin] = NULL;
	}
}

void intel_gmbus_irq_handler(struct drm_i915_private *i915)
{
	wake_up_all(&i915->display.gmbus.wait_queue);
}
