#ifndef __LINUX_GPIO_DRIVER_H
#define __LINUX_GPIO_DRIVER_H

#include <linux/types.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/lockdep.h>
#include <linux/pinctrl/pinctrl.h>

struct device;
struct gpio_desc;
struct of_phandle_args;
struct device_node;
struct seq_file;

#ifdef CONFIG_GPIOLIB

/**
 * struct gpio_chip - abstract a GPIO controller
 * @label: for diagnostics
 * @parent: optional parent device providing the GPIOs
 * @cdev: class device used by sysfs interface (may be NULL)
 * @owner: helps prevent removal of modules exporting active GPIOs
 * @data: per-instance data assigned by the driver
 * @list: links gpio_chips together for traversal
 * @request: optional hook for chip-specific activation, such as
 *	enabling module power and clock; may sleep
 * @free: optional hook for chip-specific deactivation, such as
 *	disabling module power and clock; may sleep
 * @get_direction: returns direction for signal "offset", 0=out, 1=in,
 *	(same as GPIOF_DIR_XXX), or negative error
 * @direction_input: configures signal "offset" as input, or returns error
 * @direction_output: configures signal "offset" as output, or returns error
 * @get: returns value for signal "offset"; for output signals this
 *	returns either the value actually sensed, or zero
 * @set: assigns output value for signal "offset"
 * @set_multiple: assigns output values for multiple signals defined by "mask"
 * @set_debounce: optional hook for setting debounce time for specified gpio in
 *      interrupt triggered gpio chips
 * @to_irq: optional hook supporting non-static gpio_to_irq() mappings;
 *	implementation may not sleep
 * @dbg_show: optional routine to show contents in debugfs; default code
 *	will be used when this is omitted, but custom code can show extra
 *	state (such as pullup/pulldown configuration).
 * @base: identifies the first GPIO number handled by this chip;
 *	or, if negative during registration, requests dynamic ID allocation.
 *	DEPRECATION: providing anything non-negative and nailing the base
 *	offset of GPIO chips is deprecated. Please pass -1 as base to
 *	let gpiolib select the chip base in all possible cases. We want to
 *	get rid of the static GPIO number space in the long run.
 * @ngpio: the number of GPIOs handled by this controller; the last GPIO
 *	handled is (base + ngpio - 1).
 * @desc: array of ngpio descriptors. Private.
 * @names: if set, must be an array of strings to use as alternative
 *      names for the GPIOs in this chip. Any entry in the array
 *      may be NULL if there is no alias for the GPIO, however the
 *      array must be @ngpio entries long.  A name can include a single printk
 *      format specifier for an unsigned int.  It is substituted by the actual
 *      number of the gpio.
 * @can_sleep: flag must be set iff get()/set() methods sleep, as they
 *	must while accessing GPIO expander chips over I2C or SPI. This
 *	implies that if the chip supports IRQs, these IRQs need to be threaded
 *	as the chip access may sleep when e.g. reading out the IRQ status
 *	registers.
 * @irq_not_threaded: flag must be set if @can_sleep is set but the
 *	IRQs don't need to be threaded
 * @irqchip: GPIO IRQ chip impl, provided by GPIO driver
 * @irqdomain: Interrupt translation domain; responsible for mapping
 *	between GPIO hwirq number and linux irq number
 * @irq_base: first linux IRQ number assigned to GPIO IRQ chip (deprecated)
 * @irq_handler: the irq handler to use (often a predefined irq core function)
 *	for GPIO IRQs, provided by GPIO driver
 * @irq_default_type: default IRQ triggering type applied during GPIO driver
 *	initialization, provided by GPIO driver
 * @irq_parent: GPIO IRQ chip parent/bank linux irq number,
 *	provided by GPIO driver
 * @lock_key: per GPIO IRQ chip lockdep class
 *
 * A gpio_chip can help platforms abstract various sources of GPIOs so
 * they can all be accessed through a common programing interface.
 * Example sources would be SOC controllers, FPGAs, multifunction
 * chips, dedicated GPIO expanders, and so on.
 *
 * Each chip controls a number of signals, identified in method calls
 * by "offset" values in the range 0..(@ngpio - 1).  When those signals
 * are referenced through calls like gpio_get_value(gpio), the offset
 * is calculated by subtracting @base from the gpio number.
 */
struct gpio_chip {
	const char		*label;
	struct device		*parent;
	struct device		*cdev;
	struct module		*owner;
	void			*data;
	struct list_head        list;

	int			(*request)(struct gpio_chip *chip,
						unsigned offset);
	void			(*free)(struct gpio_chip *chip,
						unsigned offset);
	int			(*get_direction)(struct gpio_chip *chip,
						unsigned offset);
	int			(*direction_input)(struct gpio_chip *chip,
						unsigned offset);
	int			(*direction_output)(struct gpio_chip *chip,
						unsigned offset, int value);
	int			(*get)(struct gpio_chip *chip,
						unsigned offset);
	void			(*set)(struct gpio_chip *chip,
						unsigned offset, int value);
	void			(*set_multiple)(struct gpio_chip *chip,
						unsigned long *mask,
						unsigned long *bits);
	int			(*set_debounce)(struct gpio_chip *chip,
						unsigned offset,
						unsigned debounce);

	int			(*to_irq)(struct gpio_chip *chip,
						unsigned offset);

	void			(*dbg_show)(struct seq_file *s,
						struct gpio_chip *chip);
	int			base;
	u16			ngpio;
	struct gpio_desc	*desc;
	const char		*const *names;
	bool			can_sleep;
	bool			irq_not_threaded;

#ifdef CONFIG_GPIOLIB_IRQCHIP
	/*
	 * With CONFIG_GPIOLIB_IRQCHIP we get an irqchip inside the gpiolib
	 * to handle IRQs for most practical cases.
	 */
	struct irq_chip		*irqchip;
	struct irq_domain	*irqdomain;
	unsigned int		irq_base;
	irq_flow_handler_t	irq_handler;
	unsigned int		irq_default_type;
	int			irq_parent;
	struct lock_class_key	*lock_key;
#endif

#if defined(CONFIG_OF_GPIO)
	/*
	 * If CONFIG_OF is enabled, then all GPIO controllers described in the
	 * device tree automatically may have an OF translation
	 */
	struct device_node *of_node;
	int of_gpio_n_cells;
	int (*of_xlate)(struct gpio_chip *gc,
			const struct of_phandle_args *gpiospec, u32 *flags);
#endif
#ifdef CONFIG_PINCTRL
	/*
	 * If CONFIG_PINCTRL is enabled, then gpio controllers can optionally
	 * describe the actual pin range which they serve in an SoC. This
	 * information would be used by pinctrl subsystem to configure
	 * corresponding pins for gpio usage.
	 */
	struct list_head pin_ranges;
#endif
};

extern const char *gpiochip_is_requested(struct gpio_chip *chip,
			unsigned offset);

/* add/remove chips */
extern int gpiochip_add_data(struct gpio_chip *chip, void *data);
static inline int gpiochip_add(struct gpio_chip *chip)
{
	return gpiochip_add_data(chip, NULL);
}
extern void gpiochip_remove(struct gpio_chip *chip);
extern struct gpio_chip *gpiochip_find(void *data,
			      int (*match)(struct gpio_chip *chip, void *data));

/* lock/unlock as IRQ */
int gpiochip_lock_as_irq(struct gpio_chip *chip, unsigned int offset);
void gpiochip_unlock_as_irq(struct gpio_chip *chip, unsigned int offset);

/* get driver data */
static inline void *gpiochip_get_data(struct gpio_chip *chip)
{
	return chip->data;
}

struct gpio_chip *gpiod_to_chip(const struct gpio_desc *desc);

#ifdef CONFIG_GPIOLIB_IRQCHIP

void gpiochip_set_chained_irqchip(struct gpio_chip *gpiochip,
		struct irq_chip *irqchip,
		int parent_irq,
		irq_flow_handler_t parent_handler);

int _gpiochip_irqchip_add(struct gpio_chip *gpiochip,
			  struct irq_chip *irqchip,
			  unsigned int first_irq,
			  irq_flow_handler_t handler,
			  unsigned int type,
			  struct lock_class_key *lock_key);

#ifdef CONFIG_LOCKDEP
#define gpiochip_irqchip_add(...)				\
(								\
	({							\
		static struct lock_class_key _key;		\
		_gpiochip_irqchip_add(__VA_ARGS__, &_key);	\
	})							\
)
#else
#define gpiochip_irqchip_add(...)				\
	_gpiochip_irqchip_add(__VA_ARGS__, NULL)
#endif

#endif /* CONFIG_GPIOLIB_IRQCHIP */

int gpiochip_generic_request(struct gpio_chip *chip, unsigned offset);
void gpiochip_generic_free(struct gpio_chip *chip, unsigned offset);

#ifdef CONFIG_PINCTRL

/**
 * struct gpio_pin_range - pin range controlled by a gpio chip
 * @head: list for maintaining set of pin ranges, used internally
 * @pctldev: pinctrl device which handles corresponding pins
 * @range: actual range of pins controlled by a gpio controller
 */

struct gpio_pin_range {
	struct list_head node;
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range range;
};

int gpiochip_add_pin_range(struct gpio_chip *chip, const char *pinctl_name,
			   unsigned int gpio_offset, unsigned int pin_offset,
			   unsigned int npins);
int gpiochip_add_pingroup_range(struct gpio_chip *chip,
			struct pinctrl_dev *pctldev,
			unsigned int gpio_offset, const char *pin_group);
void gpiochip_remove_pin_ranges(struct gpio_chip *chip);

#else

static inline int
gpiochip_add_pin_range(struct gpio_chip *chip, const char *pinctl_name,
		       unsigned int gpio_offset, unsigned int pin_offset,
		       unsigned int npins)
{
	return 0;
}
static inline int
gpiochip_add_pingroup_range(struct gpio_chip *chip,
			struct pinctrl_dev *pctldev,
			unsigned int gpio_offset, const char *pin_group)
{
	return 0;
}

static inline void
gpiochip_remove_pin_ranges(struct gpio_chip *chip)
{
}

#endif /* CONFIG_PINCTRL */

struct gpio_desc *gpiochip_request_own_desc(struct gpio_chip *chip, u16 hwnum,
					    const char *label);
void gpiochip_free_own_desc(struct gpio_desc *desc);

#else /* CONFIG_GPIOLIB */

static inline struct gpio_chip *gpiod_to_chip(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return ERR_PTR(-ENODEV);
}

#endif /* CONFIG_GPIOLIB */

#endif
