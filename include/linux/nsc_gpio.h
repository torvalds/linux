/**
   nsc_gpio.c

   National Semiconductor GPIO common access methods.

   struct nsc_gpio_ops abstracts the low-level access
   operations for the GPIO units on 2 NSC chip families; the GEODE
   integrated CPU, and the PC-8736[03456] integrated PC-peripheral
   chips.

   The GPIO units on these chips have the same pin architecture, but
   the access methods differ.  Thus, scx200_gpio and pc8736x_gpio
   implement their own versions of these routines; and use the common
   file-operations routines implemented in nsc_gpio module.

   Copyright (c) 2005 Jim Cromie <jim.cromie@gmail.com>

   NB: this work was tested on the Geode SC-1100 and PC-87366 chips.
   NSC sold the GEODE line to AMD, and the PC-8736x line to Winbond.
*/

struct nsc_gpio_ops {
	struct module*	owner;
	u32	(*gpio_config)	(unsigned iminor, u32 mask, u32 bits);
	void	(*gpio_dump)	(struct nsc_gpio_ops *amp, unsigned iminor);
	int	(*gpio_get)	(unsigned iminor);
	void	(*gpio_set)	(unsigned iminor, int state);
	void	(*gpio_change)	(unsigned iminor);
	int	(*gpio_current)	(unsigned iminor);
	struct device*	dev;	/* for dev_dbg() support, set in init  */
};

extern ssize_t nsc_gpio_write(struct file *file, const char __user *data,
			      size_t len, loff_t *ppos);

extern ssize_t nsc_gpio_read(struct file *file, char __user *buf,
			     size_t len, loff_t *ppos);

extern void nsc_gpio_dump(struct nsc_gpio_ops *amp, unsigned index);

