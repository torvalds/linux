#ifndef _ASM_GENERIC_GPIO_H
#define _ASM_GENERIC_GPIO_H

#ifdef CONFIG_HAVE_GPIO_LIB

/* Platforms may implement their GPIO interface with library code,
 * at a small performance cost for non-inlined operations and some
 * extra memory (for code and for per-GPIO table entries).
 *
 * While the GPIO programming interface defines valid GPIO numbers
 * to be in the range 0..MAX_INT, this library restricts them to the
 * smaller range 0..ARCH_NR_GPIOS.
 */

#ifndef ARCH_NR_GPIOS
#define ARCH_NR_GPIOS		256
#endif

struct seq_file;
struct module;

/**
 * struct gpio_chip - abstract a GPIO controller
 * @label: for diagnostics
 * @direction_input: configures signal "offset" as input, or returns error
 * @get: returns value for signal "offset"; for output signals this
 *	returns either the value actually sensed, or zero
 * @direction_output: configures signal "offset" as output, or returns error
 * @set: assigns output value for signal "offset"
 * @dbg_show: optional routine to show contents in debugfs; default code
 *	will be used when this is omitted, but custom code can show extra
 *	state (such as pullup/pulldown configuration).
 * @base: identifies the first GPIO number handled by this chip; or, if
 *	negative during registration, requests dynamic ID allocation.
 * @ngpio: the number of GPIOs handled by this controller; the last GPIO
 *	handled is (base + ngpio - 1).
 * @can_sleep: flag must be set iff get()/set() methods sleep, as they
 *	must while accessing GPIO expander chips over I2C or SPI
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
	char			*label;
	struct module		*owner;

	int			(*direction_input)(struct gpio_chip *chip,
						unsigned offset);
	int			(*get)(struct gpio_chip *chip,
						unsigned offset);
	int			(*direction_output)(struct gpio_chip *chip,
						unsigned offset, int value);
	void			(*set)(struct gpio_chip *chip,
						unsigned offset, int value);
	void			(*dbg_show)(struct seq_file *s,
						struct gpio_chip *chip);
	int			base;
	u16			ngpio;
	unsigned		can_sleep:1;
};

extern const char *gpiochip_is_requested(struct gpio_chip *chip,
			unsigned offset);

/* add/remove chips */
extern int gpiochip_add(struct gpio_chip *chip);
extern int __must_check gpiochip_remove(struct gpio_chip *chip);


/* Always use the library code for GPIO management calls,
 * or when sleeping may be involved.
 */
extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);

extern int gpio_direction_input(unsigned gpio);
extern int gpio_direction_output(unsigned gpio, int value);

extern int gpio_get_value_cansleep(unsigned gpio);
extern void gpio_set_value_cansleep(unsigned gpio, int value);


/* A platform's <asm/gpio.h> code may want to inline the I/O calls when
 * the GPIO is constant and refers to some always-present controller,
 * giving direct access to chip registers and tight bitbanging loops.
 */
extern int __gpio_get_value(unsigned gpio);
extern void __gpio_set_value(unsigned gpio, int value);

extern int __gpio_cansleep(unsigned gpio);


#else

/* platforms that don't directly support access to GPIOs through I2C, SPI,
 * or other blocking infrastructure can use these wrappers.
 */

static inline int gpio_cansleep(unsigned gpio)
{
	return 0;
}

static inline int gpio_get_value_cansleep(unsigned gpio)
{
	might_sleep();
	return gpio_get_value(gpio);
}

static inline void gpio_set_value_cansleep(unsigned gpio, int value)
{
	might_sleep();
	gpio_set_value(gpio, value);
}

#endif

#endif /* _ASM_GENERIC_GPIO_H */
