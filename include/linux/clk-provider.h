/*
 *  linux/include/linux/clk-provider.h
 *
 *  Copyright (c) 2010-2011 Jeremy Kerr <jeremy.kerr@canonical.com>
 *  Copyright (C) 2011-2012 Linaro Ltd <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LINUX_CLK_PROVIDER_H
#define __LINUX_CLK_PROVIDER_H

#include <linux/clk.h>
#include <linux/io.h>

#ifdef CONFIG_COMMON_CLK

/*
 * flags used across common struct clk.  these flags should only affect the
 * top-level framework.  custom flags for dealing with hardware specifics
 * belong in struct clk_foo
 */
#define CLK_SET_RATE_GATE	BIT(0) /* must be gated across rate change */
#define CLK_SET_PARENT_GATE	BIT(1) /* must be gated across re-parent */
#define CLK_SET_RATE_PARENT	BIT(2) /* propagate rate change up one level */
#define CLK_IGNORE_UNUSED	BIT(3) /* do not gate even if unused */
#define CLK_IS_ROOT		BIT(4) /* root clk, has no parent */
#define CLK_IS_BASIC		BIT(5) /* Basic clk, can't do a to_clk_foo() */
#define CLK_GET_RATE_NOCACHE	BIT(6) /* do not use the cached clk rate */
#define CLK_SET_RATE_NO_REPARENT BIT(7) /* don't re-parent on rate change */

struct clk_hw;

/**
 * struct clk_ops -  Callback operations for hardware clocks; these are to
 * be provided by the clock implementation, and will be called by drivers
 * through the clk_* api.
 *
 * @prepare:	Prepare the clock for enabling. This must not return until
 * 		the clock is fully prepared, and it's safe to call clk_enable.
 * 		This callback is intended to allow clock implementations to
 * 		do any initialisation that may sleep. Called with
 * 		prepare_lock held.
 *
 * @unprepare:	Release the clock from its prepared state. This will typically
 * 		undo any work done in the @prepare callback. Called with
 * 		prepare_lock held.
 *
 * @is_prepared: Queries the hardware to determine if the clock is prepared.
 *		This function is allowed to sleep. Optional, if this op is not
 *		set then the prepare count will be used.
 *
 * @unprepare_unused: Unprepare the clock atomically.  Only called from
 *		clk_disable_unused for prepare clocks with special needs.
 *		Called with prepare mutex held. This function may sleep.
 *
 * @enable:	Enable the clock atomically. This must not return until the
 * 		clock is generating a valid clock signal, usable by consumer
 * 		devices. Called with enable_lock held. This function must not
 * 		sleep.
 *
 * @disable:	Disable the clock atomically. Called with enable_lock held.
 * 		This function must not sleep.
 *
 * @is_enabled:	Queries the hardware to determine if the clock is enabled.
 * 		This function must not sleep. Optional, if this op is not
 * 		set then the enable count will be used.
 *
 * @disable_unused: Disable the clock atomically.  Only called from
 *		clk_disable_unused for gate clocks with special needs.
 *		Called with enable_lock held.  This function must not
 *		sleep.
 *
 * @recalc_rate	Recalculate the rate of this clock, by querying hardware. The
 * 		parent rate is an input parameter.  It is up to the caller to
 * 		ensure that the prepare_mutex is held across this call.
 * 		Returns the calculated rate.  Optional, but recommended - if
 * 		this op is not set then clock rate will be initialized to 0.
 *
 * @round_rate:	Given a target rate as input, returns the closest rate actually
 * 		supported by the clock.
 *
 * @get_parent:	Queries the hardware to determine the parent of a clock.  The
 * 		return value is a u8 which specifies the index corresponding to
 * 		the parent clock.  This index can be applied to either the
 * 		.parent_names or .parents arrays.  In short, this function
 * 		translates the parent value read from hardware into an array
 * 		index.  Currently only called when the clock is initialized by
 * 		__clk_init.  This callback is mandatory for clocks with
 * 		multiple parents.  It is optional (and unnecessary) for clocks
 * 		with 0 or 1 parents.
 *
 * @set_parent:	Change the input source of this clock; for clocks with multiple
 * 		possible parents specify a new parent by passing in the index
 * 		as a u8 corresponding to the parent in either the .parent_names
 * 		or .parents arrays.  This function in affect translates an
 * 		array index into the value programmed into the hardware.
 * 		Returns 0 on success, -EERROR otherwise.
 *
 * @set_rate:	Change the rate of this clock. The requested rate is specified
 *		by the second argument, which should typically be the return
 *		of .round_rate call.  The third argument gives the parent rate
 *		which is likely helpful for most .set_rate implementation.
 *		Returns 0 on success, -EERROR otherwise.
 *
 * The clk_enable/clk_disable and clk_prepare/clk_unprepare pairs allow
 * implementations to split any work between atomic (enable) and sleepable
 * (prepare) contexts.  If enabling a clock requires code that might sleep,
 * this must be done in clk_prepare.  Clock enable code that will never be
 * called in a sleepable context may be implemented in clk_enable.
 *
 * Typically, drivers will call clk_prepare when a clock may be needed later
 * (eg. when a device is opened), and clk_enable when the clock is actually
 * required (eg. from an interrupt). Note that clk_prepare MUST have been
 * called before clk_enable.
 */
struct clk_ops {
	int		(*prepare)(struct clk_hw *hw);
	void		(*unprepare)(struct clk_hw *hw);
	int		(*is_prepared)(struct clk_hw *hw);
	void		(*unprepare_unused)(struct clk_hw *hw);
	int		(*enable)(struct clk_hw *hw);
	void		(*disable)(struct clk_hw *hw);
	int		(*is_enabled)(struct clk_hw *hw);
	void		(*disable_unused)(struct clk_hw *hw);
	unsigned long	(*recalc_rate)(struct clk_hw *hw,
					unsigned long parent_rate);
	long		(*round_rate)(struct clk_hw *hw, unsigned long,
					unsigned long *);
	int		(*set_parent)(struct clk_hw *hw, u8 index);
	u8		(*get_parent)(struct clk_hw *hw);
	int		(*set_rate)(struct clk_hw *hw, unsigned long,
				    unsigned long);
	void		(*init)(struct clk_hw *hw);
};

/**
 * struct clk_init_data - holds init data that's common to all clocks and is
 * shared between the clock provider and the common clock framework.
 *
 * @name: clock name
 * @ops: operations this clock supports
 * @parent_names: array of string names for all possible parents
 * @num_parents: number of possible parents
 * @flags: framework-level hints and quirks
 */
struct clk_init_data {
	const char		*name;
	const struct clk_ops	*ops;
	const char		**parent_names;
	u8			num_parents;
	unsigned long		flags;
};

/**
 * struct clk_hw - handle for traversing from a struct clk to its corresponding
 * hardware-specific structure.  struct clk_hw should be declared within struct
 * clk_foo and then referenced by the struct clk instance that uses struct
 * clk_foo's clk_ops
 *
 * @clk: pointer to the struct clk instance that points back to this struct
 * clk_hw instance
 *
 * @init: pointer to struct clk_init_data that contains the init data shared
 * with the common clock framework.
 */
struct clk_hw {
	struct clk *clk;
	const struct clk_init_data *init;
};

/*
 * DOC: Basic clock implementations common to many platforms
 *
 * Each basic clock hardware type is comprised of a structure describing the
 * clock hardware, implementations of the relevant callbacks in struct clk_ops,
 * unique flags for that hardware type, a registration function and an
 * alternative macro for static initialization
 */

/**
 * struct clk_fixed_rate - fixed-rate clock
 * @hw:		handle between common and hardware-specific interfaces
 * @fixed_rate:	constant frequency of clock
 */
struct clk_fixed_rate {
	struct		clk_hw hw;
	unsigned long	fixed_rate;
	u8		flags;
};

extern const struct clk_ops clk_fixed_rate_ops;
struct clk *clk_register_fixed_rate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		unsigned long fixed_rate);

void of_fixed_clk_setup(struct device_node *np);

/**
 * struct clk_gate - gating clock
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @reg:	register controlling gate
 * @bit_idx:	single bit controlling gate
 * @flags:	hardware-specific flags
 * @lock:	register lock
 *
 * Clock which can gate its output.  Implements .enable & .disable
 *
 * Flags:
 * CLK_GATE_SET_TO_DISABLE - by default this clock sets the bit at bit_idx to
 * 	enable the clock.  Setting this flag does the opposite: setting the bit
 * 	disable the clock and clearing it enables the clock
 */
struct clk_gate {
	struct clk_hw hw;
	void __iomem	*reg;
	u8		bit_idx;
	u8		flags;
	spinlock_t	*lock;
};

#define CLK_GATE_SET_TO_DISABLE		BIT(0)

extern const struct clk_ops clk_gate_ops;
struct clk *clk_register_gate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		u8 clk_gate_flags, spinlock_t *lock);

struct clk_div_table {
	unsigned int	val;
	unsigned int	div;
};

/**
 * struct clk_divider - adjustable divider clock
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @reg:	register containing the divider
 * @shift:	shift to the divider bit field
 * @width:	width of the divider bit field
 * @table:	array of value/divider pairs, last entry should have div = 0
 * @lock:	register lock
 *
 * Clock with an adjustable divider affecting its output frequency.  Implements
 * .recalc_rate, .set_rate and .round_rate
 *
 * Flags:
 * CLK_DIVIDER_ONE_BASED - by default the divisor is the value read from the
 * 	register plus one.  If CLK_DIVIDER_ONE_BASED is set then the divider is
 * 	the raw value read from the register, with the value of zero considered
 *	invalid, unless CLK_DIVIDER_ALLOW_ZERO is set.
 * CLK_DIVIDER_POWER_OF_TWO - clock divisor is 2 raised to the value read from
 * 	the hardware register
 * CLK_DIVIDER_ALLOW_ZERO - Allow zero divisors.  For dividers which have
 *	CLK_DIVIDER_ONE_BASED set, it is possible to end up with a zero divisor.
 *	Some hardware implementations gracefully handle this case and allow a
 *	zero divisor by not modifying their input clock
 *	(divide by one / bypass).
 */
struct clk_divider {
	struct clk_hw	hw;
	void __iomem	*reg;
	u8		shift;
	u8		width;
	u8		flags;
	const struct clk_div_table	*table;
	spinlock_t	*lock;
};

#define CLK_DIVIDER_ONE_BASED		BIT(0)
#define CLK_DIVIDER_POWER_OF_TWO	BIT(1)
#define CLK_DIVIDER_ALLOW_ZERO		BIT(2)

extern const struct clk_ops clk_divider_ops;
struct clk *clk_register_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u8 clk_divider_flags, spinlock_t *lock);
struct clk *clk_register_divider_table(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u8 clk_divider_flags, const struct clk_div_table *table,
		spinlock_t *lock);

/**
 * struct clk_mux - multiplexer clock
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @reg:	register controlling multiplexer
 * @shift:	shift to multiplexer bit field
 * @width:	width of mutliplexer bit field
 * @flags:	hardware-specific flags
 * @lock:	register lock
 *
 * Clock with multiple selectable parents.  Implements .get_parent, .set_parent
 * and .recalc_rate
 *
 * Flags:
 * CLK_MUX_INDEX_ONE - register index starts at 1, not 0
 * CLK_MUX_INDEX_BIT - register index is a single bit (power of two)
 */
struct clk_mux {
	struct clk_hw	hw;
	void __iomem	*reg;
	u32		*table;
	u32		mask;
	u8		shift;
	u8		flags;
	spinlock_t	*lock;
};

#define CLK_MUX_INDEX_ONE		BIT(0)
#define CLK_MUX_INDEX_BIT		BIT(1)

extern const struct clk_ops clk_mux_ops;

struct clk *clk_register_mux(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u8 clk_mux_flags, spinlock_t *lock);

struct clk *clk_register_mux_table(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents, unsigned long flags,
		void __iomem *reg, u8 shift, u32 mask,
		u8 clk_mux_flags, u32 *table, spinlock_t *lock);

void of_fixed_factor_clk_setup(struct device_node *node);

/**
 * struct clk_fixed_factor - fixed multiplier and divider clock
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @mult:	multiplier
 * @div:	divider
 *
 * Clock with a fixed multiplier and divider. The output frequency is the
 * parent clock rate divided by div and multiplied by mult.
 * Implements .recalc_rate, .set_rate and .round_rate
 */

struct clk_fixed_factor {
	struct clk_hw	hw;
	unsigned int	mult;
	unsigned int	div;
};

extern struct clk_ops clk_fixed_factor_ops;
struct clk *clk_register_fixed_factor(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		unsigned int mult, unsigned int div);

/***
 * struct clk_composite - aggregate clock of mux, divider and gate clocks
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @mux_hw:	handle between composite and hardware-specific mux clock
 * @rate_hw:	handle between composite and hardware-specific rate clock
 * @gate_hw:	handle between composite and hardware-specific gate clock
 * @mux_ops:	clock ops for mux
 * @rate_ops:	clock ops for rate
 * @gate_ops:	clock ops for gate
 */
struct clk_composite {
	struct clk_hw	hw;
	struct clk_ops	ops;

	struct clk_hw	*mux_hw;
	struct clk_hw	*rate_hw;
	struct clk_hw	*gate_hw;

	const struct clk_ops	*mux_ops;
	const struct clk_ops	*rate_ops;
	const struct clk_ops	*gate_ops;
};

struct clk *clk_register_composite(struct device *dev, const char *name,
		const char **parent_names, int num_parents,
		struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
		struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
		struct clk_hw *gate_hw, const struct clk_ops *gate_ops,
		unsigned long flags);

/**
 * clk_register - allocate a new clock, register it and return an opaque cookie
 * @dev: device that is registering this clock
 * @hw: link to hardware-specific clock data
 *
 * clk_register is the primary interface for populating the clock tree with new
 * clock nodes.  It returns a pointer to the newly allocated struct clk which
 * cannot be dereferenced by driver code but may be used in conjuction with the
 * rest of the clock API.  In the event of an error clk_register will return an
 * error code; drivers must test for an error code after calling clk_register.
 */
struct clk *clk_register(struct device *dev, struct clk_hw *hw);
struct clk *devm_clk_register(struct device *dev, struct clk_hw *hw);

void clk_unregister(struct clk *clk);
void devm_clk_unregister(struct device *dev, struct clk *clk);

/* helper functions */
const char *__clk_get_name(struct clk *clk);
struct clk_hw *__clk_get_hw(struct clk *clk);
u8 __clk_get_num_parents(struct clk *clk);
struct clk *__clk_get_parent(struct clk *clk);
unsigned int __clk_get_enable_count(struct clk *clk);
unsigned int __clk_get_prepare_count(struct clk *clk);
unsigned long __clk_get_rate(struct clk *clk);
unsigned long __clk_get_flags(struct clk *clk);
bool __clk_is_prepared(struct clk *clk);
bool __clk_is_enabled(struct clk *clk);
struct clk *__clk_lookup(const char *name);

/*
 * FIXME clock api without lock protection
 */
int __clk_prepare(struct clk *clk);
void __clk_unprepare(struct clk *clk);
void __clk_reparent(struct clk *clk, struct clk *new_parent);
unsigned long __clk_round_rate(struct clk *clk, unsigned long rate);

struct of_device_id;

typedef void (*of_clk_init_cb_t)(struct device_node *);

int of_clk_add_provider(struct device_node *np,
			struct clk *(*clk_src_get)(struct of_phandle_args *args,
						   void *data),
			void *data);
void of_clk_del_provider(struct device_node *np);
struct clk *of_clk_src_simple_get(struct of_phandle_args *clkspec,
				  void *data);
struct clk_onecell_data {
	struct clk **clks;
	unsigned int clk_num;
};
struct clk *of_clk_src_onecell_get(struct of_phandle_args *clkspec, void *data);
const char *of_clk_get_parent_name(struct device_node *np, int index);

void of_clk_init(const struct of_device_id *matches);

#define CLK_OF_DECLARE(name, compat, fn)			\
	static const struct of_device_id __clk_of_table_##name	\
		__used __section(__clk_of_table)		\
		= { .compatible = compat, .data = fn };

/*
 * wrap access to peripherals in accessor routines
 * for improved portability across platforms
 */

static inline u32 clk_readl(u32 __iomem *reg)
{
	return readl(reg);
}

static inline void clk_writel(u32 val, u32 __iomem *reg)
{
	writel(val, reg);
}

#endif /* CONFIG_COMMON_CLK */
#endif /* CLK_PROVIDER_H */
