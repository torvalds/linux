#ifndef _DT_BINDINGS_CLOCK_ROCKCHIP_H
#define _DT_BINDINGS_CLOCK_ROCKCHIP_H

#ifndef BIT
#define BIT(nr)			(1 << (nr))
#endif

#define CLK_DIVIDER_PLUS_ONE		(0)
#define CLK_DIVIDER_ONE_BASED		BIT(0)
#define CLK_DIVIDER_POWER_OF_TWO	BIT(1)
#define CLK_DIVIDER_ALLOW_ZERO		BIT(2)
#define CLK_DIVIDER_HIWORD_MASK		BIT(3)

/* Rockchip special defined */
//#define CLK_DIVIDER_FIXED		BIT(6)
#define CLK_DIVIDER_USER_DEFINE		BIT(7)

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


#endif /* _DT_BINDINGS_CLOCK_ROCKCHIP_H */
