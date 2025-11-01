/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_GPIO_GENERIC_H
#define __LINUX_GPIO_GENERIC_H

#include <linux/cleanup.h>
#include <linux/gpio/driver.h>
#include <linux/spinlock.h>

struct device;

#define GPIO_GENERIC_BIG_ENDIAN			BIT(0)
#define GPIO_GENERIC_UNREADABLE_REG_SET		BIT(1) /* reg_set is unreadable */
#define GPIO_GENERIC_UNREADABLE_REG_DIR		BIT(2) /* reg_dir is unreadable */
#define GPIO_GENERIC_BIG_ENDIAN_BYTE_ORDER	BIT(3)
#define GPIO_GENERIC_READ_OUTPUT_REG_SET	BIT(4) /* reg_set stores output value */
#define GPIO_GENERIC_NO_OUTPUT			BIT(5) /* only input */
#define GPIO_GENERIC_NO_SET_ON_INPUT		BIT(6)
#define GPIO_GENERIC_PINCTRL_BACKEND		BIT(7) /* Call pinctrl direction setters */
#define GPIO_GENERIC_NO_INPUT			BIT(8) /* only output */

/**
 * struct gpio_generic_chip_config - Generic GPIO chip configuration data
 * @dev: Parent device of the new GPIO chip (compulsory).
 * @sz: Size (width) of the MMIO registers in bytes, typically 1, 2 or 4.
 * @dat: MMIO address for the register to READ the value of the GPIO lines, it
 *       is expected that a 1 in the corresponding bit in this register means
 *       the line is asserted.
 * @set: MMIO address for the register to SET the value of the GPIO lines, it
 *       is expected that we write the line with 1 in this register to drive
 *       the GPIO line high.
 * @clr: MMIO address for the register to CLEAR the value of the GPIO lines,
 *       it is expected that we write the line with 1 in this register to
 *       drive the GPIO line low. It is allowed to leave this address as NULL,
 *       in that case the SET register will be assumed to also clear the GPIO
 *       lines, by actively writing the line with 0.
 * @dirout: MMIO address for the register to set the line as OUTPUT. It is
 *          assumed that setting a line to 1 in this register will turn that
 *          line into an output line. Conversely, setting the line to 0 will
 *          turn that line into an input.
 * @dirin: MMIO address for the register to set this line as INPUT. It is
 *         assumed that setting a line to 1 in this register will turn that
 *         line into an input line. Conversely, setting the line to 0 will
 *         turn that line into an output.
 * @flags: Different flags that will affect the behaviour of the device, such
 *         as endianness etc.
 */
struct gpio_generic_chip_config {
	struct device *dev;
	unsigned long sz;
	void __iomem *dat;
	void __iomem *set;
	void __iomem *clr;
	void __iomem *dirout;
	void __iomem *dirin;
	unsigned long flags;
};

/**
 * struct gpio_generic_chip - Generic GPIO chip implementation.
 * @gc: The underlying struct gpio_chip object, implementing low-level GPIO
 *      chip routines.
 * @read_reg: reader function for generic GPIO
 * @write_reg: writer function for generic GPIO
 * @be_bits: if the generic GPIO has big endian bit order (bit 31 is
 *           representing line 0, bit 30 is line 1 ... bit 0 is line 31) this
 *           is set to true by the generic GPIO core. It is for internal
 *           housekeeping only.
 * @reg_dat: data (in) register for generic GPIO
 * @reg_set: output set register (out=high) for generic GPIO
 * @reg_clr: output clear register (out=low) for generic GPIO
 * @reg_dir_out: direction out setting register for generic GPIO
 * @reg_dir_in: direction in setting register for generic GPIO
 * @dir_unreadable: indicates that the direction register(s) cannot be read and
 *                  we need to rely on out internal state tracking.
 * @pinctrl: the generic GPIO uses a pin control backend.
 * @bits: number of register bits used for a generic GPIO
 *        i.e. <register width> * 8
 * @lock: used to lock chip->sdata. Also, this is needed to keep
 *        shadowed and real data registers writes together.
 * @sdata: shadowed data register for generic GPIO to clear/set bits safely.
 * @sdir: shadowed direction register for generic GPIO to clear/set direction
 *        safely. A "1" in this word means the line is set as output.
 */
struct gpio_generic_chip {
	struct gpio_chip gc;
	unsigned long (*read_reg)(void __iomem *reg);
	void (*write_reg)(void __iomem *reg, unsigned long data);
	bool be_bits;
	void __iomem *reg_dat;
	void __iomem *reg_set;
	void __iomem *reg_clr;
	void __iomem *reg_dir_out;
	void __iomem *reg_dir_in;
	bool dir_unreadable;
	bool pinctrl;
	int bits;
	raw_spinlock_t lock;
	unsigned long sdata;
	unsigned long sdir;
};

static inline struct gpio_generic_chip *
to_gpio_generic_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct gpio_generic_chip, gc);
}

int gpio_generic_chip_init(struct gpio_generic_chip *chip,
			   const struct gpio_generic_chip_config *cfg);

/**
 * gpio_generic_chip_set() - Set the GPIO line value of the generic GPIO chip.
 * @chip: Generic GPIO chip to use.
 * @offset: Hardware offset of the line to set.
 * @value: New GPIO line value.
 *
 * Some modules using the generic GPIO chip, need to set line values in their
 * direction setters but they don't have access to the gpio-mmio symbols so
 * they use the function pointer in struct gpio_chip directly. This is not
 * optimal and can lead to crashes at run-time in some instances. This wrapper
 * provides a safe interface for users.
 *
 * Returns: 0 on success, negative error number of failure.
 */
static inline int
gpio_generic_chip_set(struct gpio_generic_chip *chip, unsigned int offset,
		      int value)
{
	if (WARN_ON(!chip->gc.set))
		return -EOPNOTSUPP;

	return chip->gc.set(&chip->gc, offset, value);
}

/**
 * gpio_generic_read_reg() - Read a register using the underlying callback.
 * @chip: Generic GPIO chip to use.
 * @reg: Register to read.
 *
 * Returns: value read from register.
 */
static inline unsigned long
gpio_generic_read_reg(struct gpio_generic_chip *chip, void __iomem *reg)
{
	if (WARN_ON(!chip->read_reg))
		return 0;

	return chip->read_reg(reg);
}

/**
 * gpio_generic_write_reg() - Write a register using the underlying callback.
 * @chip: Generic GPIO chip to use.
 * @reg: Register to write to.
 * @val: New value to write.
 */
static inline void gpio_generic_write_reg(struct gpio_generic_chip *chip,
					  void __iomem *reg, unsigned long val)
{
	if (WARN_ON(!chip->write_reg))
		return;

	chip->write_reg(reg, val);
}

#define gpio_generic_chip_lock(gen_gc) \
	raw_spin_lock(&(gen_gc)->lock)

#define gpio_generic_chip_unlock(gen_gc) \
	raw_spin_unlock(&(gen_gc)->lock)

#define gpio_generic_chip_lock_irqsave(gen_gc, flags) \
	raw_spin_lock_irqsave(&(gen_gc)->lock, flags)

#define gpio_generic_chip_unlock_irqrestore(gen_gc, flags) \
	raw_spin_unlock_irqrestore(&(gen_gc)->lock, flags)

DEFINE_LOCK_GUARD_1(gpio_generic_lock,
		    struct gpio_generic_chip,
		    gpio_generic_chip_lock(_T->lock),
		    gpio_generic_chip_unlock(_T->lock))

DEFINE_LOCK_GUARD_1(gpio_generic_lock_irqsave,
		    struct gpio_generic_chip,
		    gpio_generic_chip_lock_irqsave(_T->lock, _T->flags),
		    gpio_generic_chip_unlock_irqrestore(_T->lock, _T->flags),
		    unsigned long flags)

#endif /* __LINUX_GPIO_GENERIC_H */
