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

#ifdef CONFIG_COMMON_CLK

/**
 * struct clk_hw - handle for traversing from a struct clk to its corresponding
 * hardware-specific structure.  struct clk_hw should be declared within struct
 * clk_foo and then referenced by the struct clk instance that uses struct
 * clk_foo's clk_ops
 *
 * clk: pointer to the struct clk instance that points back to this struct
 * clk_hw instance
 */
struct clk_hw {
	struct clk *clk;
};

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
 * @enable:	Enable the clock atomically. This must not return until the
 * 		clock is generating a valid clock signal, usable by consumer
 * 		devices. Called with enable_lock held. This function must not
 * 		sleep.
 *
 * @disable:	Disable the clock atomically. Called with enable_lock held.
 * 		This function must not sleep.
 *
 * @recalc_rate	Recalculate the rate of this clock, by quering hardware.  The
 * 		parent rate is an input parameter.  It is up to the caller to
 * 		insure that the prepare_mutex is held across this call.
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
 * @set_rate:	Change the rate of this clock. If this callback returns
 * 		CLK_SET_RATE_PARENT, the rate change will be propagated to the
 * 		parent clock (which may propagate again if the parent clock
 * 		also sets this flag). The requested rate of the parent is
 * 		passed back from the callback in the second 'unsigned long *'
 * 		argument.  Note that it is up to the hardware clock's set_rate
 * 		implementation to insure that clocks do not run out of spec
 * 		when propgating the call to set_rate up to the parent.  One way
 * 		to do this is to gate the clock (via clk_disable and/or
 * 		clk_unprepare) before calling clk_set_rate, then ungating it
 * 		afterward.  If your clock also has the CLK_GATE_SET_RATE flag
 * 		set then this will insure safety.  Returns 0 on success,
 * 		-EERROR otherwise.
 *
 * The clk_enable/clk_disable and clk_prepare/clk_unprepare pairs allow
 * implementations to split any work between atomic (enable) and sleepable
 * (prepare) contexts.  If enabling a clock requires code that might sleep,
 * this must be done in clk_prepare.  Clock enable code that will never be
 * called in a sleepable context may be implement in clk_enable.
 *
 * Typically, drivers will call clk_prepare when a clock may be needed later
 * (eg. when a device is opened), and clk_enable when the clock is actually
 * required (eg. from an interrupt). Note that clk_prepare MUST have been
 * called before clk_enable.
 */
struct clk_ops {
	int		(*prepare)(struct clk_hw *hw);
	void		(*unprepare)(struct clk_hw *hw);
	int		(*enable)(struct clk_hw *hw);
	void		(*disable)(struct clk_hw *hw);
	int		(*is_enabled)(struct clk_hw *hw);
	unsigned long	(*recalc_rate)(struct clk_hw *hw,
					unsigned long parent_rate);
	long		(*round_rate)(struct clk_hw *hw, unsigned long,
					unsigned long *);
	int		(*set_parent)(struct clk_hw *hw, u8 index);
	u8		(*get_parent)(struct clk_hw *hw);
	int		(*set_rate)(struct clk_hw *hw, unsigned long);
	void		(*init)(struct clk_hw *hw);
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

struct clk *clk_register_fixed_rate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		unsigned long fixed_rate);

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
 * CLK_GATE_SET_DISABLE - by default this clock sets the bit at bit_idx to
 * 	enable the clock.  Setting this flag does the opposite: setting the bit
 * 	disable the clock and clearing it enables the clock
 */
struct clk_gate {
	struct clk_hw hw;
	void __iomem	*reg;
	u8		bit_idx;
	u8		flags;
	spinlock_t	*lock;
	char		*parent[1];
};

#define CLK_GATE_SET_TO_DISABLE		BIT(0)

struct clk *clk_register_gate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		u8 clk_gate_flags, spinlock_t *lock);

/**
 * struct clk_divider - adjustable divider clock
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @reg:	register containing the divider
 * @shift:	shift to the divider bit field
 * @width:	width of the divider bit field
 * @lock:	register lock
 *
 * Clock with an adjustable divider affecting its output frequency.  Implements
 * .recalc_rate, .set_rate and .round_rate
 *
 * Flags:
 * CLK_DIVIDER_ONE_BASED - by default the divisor is the value read from the
 * 	register plus one.  If CLK_DIVIDER_ONE_BASED is set then the divider is
 * 	the raw value read from the register, with the value of zero considered
 * 	invalid
 * CLK_DIVIDER_POWER_OF_TWO - clock divisor is 2 raised to the value read from
 * 	the hardware register
 */
struct clk_divider {
	struct clk_hw	hw;
	void __iomem	*reg;
	u8		shift;
	u8		width;
	u8		flags;
	spinlock_t	*lock;
	char		*parent[1];
};

#define CLK_DIVIDER_ONE_BASED		BIT(0)
#define CLK_DIVIDER_POWER_OF_TWO	BIT(1)

struct clk *clk_register_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u8 clk_divider_flags, spinlock_t *lock);

/**
 * struct clk_mux - multiplexer clock
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @reg:	register controlling multiplexer
 * @shift:	shift to multiplexer bit field
 * @width:	width of mutliplexer bit field
 * @num_clks:	number of parent clocks
 * @lock:	register lock
 *
 * Clock with multiple selectable parents.  Implements .get_parent, .set_parent
 * and .recalc_rate
 *
 * Flags:
 * CLK_MUX_INDEX_ONE - register index starts at 1, not 0
 * CLK_MUX_INDEX_BITWISE - register index is a single bit (power of two)
 */
struct clk_mux {
	struct clk_hw	hw;
	void __iomem	*reg;
	u8		shift;
	u8		width;
	u8		flags;
	spinlock_t	*lock;
};

#define CLK_MUX_INDEX_ONE		BIT(0)
#define CLK_MUX_INDEX_BIT		BIT(1)

struct clk *clk_register_mux(struct device *dev, const char *name,
		char **parent_names, u8 num_parents, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u8 clk_mux_flags, spinlock_t *lock);

/**
 * clk_register - allocate a new clock, register it and return an opaque cookie
 * @dev: device that is registering this clock
 * @name: clock name
 * @ops: operations this clock supports
 * @hw: link to hardware-specific clock data
 * @parent_names: array of string names for all possible parents
 * @num_parents: number of possible parents
 * @flags: framework-level hints and quirks
 *
 * clk_register is the primary interface for populating the clock tree with new
 * clock nodes.  It returns a pointer to the newly allocated struct clk which
 * cannot be dereferenced by driver code but may be used in conjuction with the
 * rest of the clock API.
 */
struct clk *clk_register(struct device *dev, const char *name,
		const struct clk_ops *ops, struct clk_hw *hw,
		char **parent_names, u8 num_parents, unsigned long flags);

/* helper functions */
const char *__clk_get_name(struct clk *clk);
struct clk_hw *__clk_get_hw(struct clk *clk);
u8 __clk_get_num_parents(struct clk *clk);
struct clk *__clk_get_parent(struct clk *clk);
inline int __clk_get_enable_count(struct clk *clk);
inline int __clk_get_prepare_count(struct clk *clk);
unsigned long __clk_get_rate(struct clk *clk);
unsigned long __clk_get_flags(struct clk *clk);
int __clk_is_enabled(struct clk *clk);
struct clk *__clk_lookup(const char *name);

/*
 * FIXME clock api without lock protection
 */
int __clk_prepare(struct clk *clk);
void __clk_unprepare(struct clk *clk);
void __clk_reparent(struct clk *clk, struct clk *new_parent);
unsigned long __clk_round_rate(struct clk *clk, unsigned long rate);

#endif /* CONFIG_COMMON_CLK */
#endif /* CLK_PROVIDER_H */
