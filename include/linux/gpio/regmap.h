/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _LINUX_GPIO_REGMAP_H
#define _LINUX_GPIO_REGMAP_H

#include <linux/types.h>

struct device;
struct fwnode_handle;
struct gpio_regmap;
struct gpio_chip;
struct irq_domain;
struct regmap;

#define GPIO_REGMAP_ADDR_ZERO ((unsigned int)(-1))
#define GPIO_REGMAP_ADDR(addr) ((addr) ? : GPIO_REGMAP_ADDR_ZERO)

/**
 * enum gpio_regmap_operation - Operation type for gpio_regmap callbacks
 *
 * Traditionally, the operation type was inferred from the base register.
 * However, that approach does not always work — for example, when all control
 * bits of a single GPIO reside in the same register. This enum allows the
 * callbacks (reg_mask_xlate and value_xlate) to explicitly distinguish between
 * operation types. The user is free to choose which method to use.
 *
 * Read operation:
 * @GPIO_REGMAP_GET_OP: Indicates a read operation to get the current GPIO value.
 *
 * Write operation:
 * @GPIO_REGMAP_SET_OP: Indicates a write operation to set the GPIO output value.
 *
 * Direction operations:
 * @GPIO_REGMAP_GET_DIR_OP: Indicates a read operation to get the GPIO direction.
 * @GPIO_REGMAP_SET_DIR_OP: Indicates a write operation to set the GPIO direction.
 */
enum gpio_regmap_operation {
	GPIO_REGMAP_GET_OP,
	GPIO_REGMAP_SET_OP,
	GPIO_REGMAP_GET_DIR_OP,
	GPIO_REGMAP_SET_DIR_OP,
};

/**
 * struct gpio_regmap_config - Description of a generic regmap gpio_chip.
 * @parent:		The parent device
 * @regmap:		The regmap used to access the registers
 *			given, the name of the device is used
 * @fwnode:		(Optional) The firmware node.
 *			If not given, the fwnode of the parent is used.
 * @label:		(Optional) Descriptive name for GPIO controller.
 *			If not given, the name of the device is used.
 * @ngpio:		(Optional) Number of GPIOs
 * @names:		(Optional) Array of names for gpios
 * @reg_dat_base:	(Optional) (in) register base address
 * @reg_set_base:	(Optional) set register base address
 * @reg_clr_base:	(Optional) clear register base address
 * @reg_dir_in_base:	(Optional) in setting register base address
 * @reg_dir_out_base:	(Optional) out setting register base address
 * @reg_stride:		(Optional) May be set if the registers (of the
 *			same type, dat, set, etc) are not consecutive.
 * @ngpio_per_reg:	(Optional) Number of GPIOs per register
 * @irq_domain:		(Optional) IRQ domain if the controller is
 *			interrupt-capable
 * @fixed_direction_mask:
 *			(Optional) Bitmap representing the GPIO lines that
 *			make use of the @fixed_direction_output list to
 *			enforce direction of the GPIO. If this is NULL
 *			and @fixed_direction_output is defined, ALL GPIOs
 *			are assumed to be fixed direction (out or in).
 * @fixed_direction_output:
 *			(Optional) Bitmap representing the fixed direction of
 *			the GPIO lines. Useful when there are GPIO lines with a
 *			fixed direction mixed together in the same register.
 * @regmap_irq_chip:	(Optional) Pointer on an regmap_irq_chip structure. If
 *			set, a regmap-irq device will be created and the IRQ
 *			domain will be set accordingly.
 * @regmap_irq_line:	(Optional) The IRQ the device uses to signal interrupts.
 * @regmap_irq_flags:	(Optional) The IRQF_ flags to use for the interrupt.
 * @reg_mask_xlate:	(Optional) Translates base address and GPIO
 *			offset to a register/bitmask pair. If not
 *			given the default gpio_regmap_simple_xlate()
 *			is used.
 * @init_valid_mask:	(Optional) Routine to initialize @valid_mask, to be used
 *			if not all GPIOs are valid.
 * @value_xlate:	(Optional) Routine to translate the register value and
 *			mask before writing. This allows driver-specific logic
 *			to append additional bits (like write-enable masks)
 *			dynamically based on the current operation
 *			(GPIO_REGMAP_SET_OP and GPIO_REGMAP_SET_DIR_OP).
 * @set_config:		(Optional) Callback for setting GPIO configuration such
 *			as debounce, drive strength, or other hardware specific
 *			settings.
 * @drvdata:		(Optional) Pointer to driver specific data which is
 *			not used by gpio-remap but is provided "as is" to the
 *			driver callback(s).
 *
 * The ->reg_mask_xlate translates a given base address and GPIO offset to
 * register and mask pair. The base address is one of the given register
 * base addresses in this structure.
 *
 * Although all register base addresses are marked as optional, there are
 * several rules:
 *     1. if you only have @reg_dat_base set, then it is input-only
 *     2. if you only have @reg_set_base set, then it is output-only
 *     3. if you have either @reg_dir_in_base or @reg_dir_out_base set, then
 *        you have to set both @reg_dat_base and @reg_set_base
 *     4. if you have @reg_set_base set, you may also set @reg_clr_base to have
 *        two different registers for setting and clearing the output. This is
 *        also valid for the output-only case.
 *     5. @reg_dir_in_base and @reg_dir_out_base are exclusive; is there really
 *        hardware which has redundant registers?
 *
 * Note: All base addresses may have the special value %GPIO_REGMAP_ADDR_ZERO
 * which forces the address to the value 0.
 */
struct gpio_regmap_config {
	struct device *parent;
	struct regmap *regmap;
	struct fwnode_handle *fwnode;

	const char *label;
	int ngpio;
	const char *const *names;

	unsigned int reg_dat_base;
	unsigned int reg_set_base;
	unsigned int reg_clr_base;
	unsigned int reg_dir_in_base;
	unsigned int reg_dir_out_base;
	int reg_stride;
	int ngpio_per_reg;
	struct irq_domain *irq_domain;
	unsigned long *fixed_direction_mask;
	unsigned long *fixed_direction_output;

#ifdef CONFIG_REGMAP_IRQ
	struct regmap_irq_chip *regmap_irq_chip;
	int regmap_irq_line;
	unsigned long regmap_irq_flags;
#endif

	int (*reg_mask_xlate)(struct gpio_regmap *gpio, enum gpio_regmap_operation,
			      unsigned int base, unsigned int offset,
			      unsigned int *reg, unsigned int *mask);

	int (*init_valid_mask)(struct gpio_chip *gc,
			       unsigned long *valid_mask,
			       unsigned int ngpios);

	int (*value_xlate)(struct gpio_regmap *gpio, enum gpio_regmap_operation,
			   unsigned int base, unsigned int offset, unsigned int reg,
			   unsigned int *mask, unsigned int *val);

	int (*set_config)(struct gpio_regmap *gpio, struct gpio_chip *chip,
			  unsigned int offset, unsigned long config);

	void *drvdata;
};

struct gpio_regmap *gpio_regmap_register(const struct gpio_regmap_config *config);
void gpio_regmap_unregister(struct gpio_regmap *gpio);
struct gpio_regmap *devm_gpio_regmap_register(struct device *dev,
					      const struct gpio_regmap_config *config);
void *gpio_regmap_get_drvdata(struct gpio_regmap *gpio);

int gpio_regmap_reqres_irq(struct gpio_regmap *gpio, unsigned int offset);
void gpio_regmap_relres_irq(struct gpio_regmap *gpio, unsigned int offset);

void gpio_regmap_enable_irq(struct gpio_regmap *gpio, irq_hw_number_t hwirq);
void gpio_regmap_disable_irq(struct gpio_regmap *gpio, irq_hw_number_t hwirq);

#endif /* _LINUX_GPIO_REGMAP_H */
