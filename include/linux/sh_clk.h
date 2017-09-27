#ifndef __SH_CLOCK_H
#define __SH_CLOCK_H

#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/cpufreq.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/clk.h>
#include <linux/err.h>

struct clk;

struct clk_mapping {
	phys_addr_t		phys;
	void __iomem		*base;
	unsigned long		len;
	struct kref		ref;
};

struct sh_clk_ops {
#ifdef CONFIG_SH_CLK_CPG_LEGACY
	void (*init)(struct clk *clk);
#endif
	int (*enable)(struct clk *clk);
	void (*disable)(struct clk *clk);
	unsigned long (*recalc)(struct clk *clk);
	int (*set_rate)(struct clk *clk, unsigned long rate);
	int (*set_parent)(struct clk *clk, struct clk *parent);
	long (*round_rate)(struct clk *clk, unsigned long rate);
};

#define SH_CLK_DIV_MSK(div)	((1 << (div)) - 1)
#define SH_CLK_DIV4_MSK		SH_CLK_DIV_MSK(4)
#define SH_CLK_DIV6_MSK		SH_CLK_DIV_MSK(6)

struct clk {
	struct list_head	node;
	struct clk		*parent;
	struct clk		**parent_table;	/* list of parents to */
	unsigned short		parent_num;	/* choose between */
	unsigned char		src_shift;	/* source clock field in the */
	unsigned char		src_width;	/* configuration register */
	struct sh_clk_ops	*ops;

	struct list_head	children;
	struct list_head	sibling;	/* node for children */

	int			usecount;

	unsigned long		rate;
	unsigned long		flags;

	void __iomem		*enable_reg;
	void __iomem		*status_reg;
	unsigned int		enable_bit;
	void __iomem		*mapped_reg;

	unsigned int		div_mask;
	unsigned long		arch_flags;
	void			*priv;
	struct clk_mapping	*mapping;
	struct cpufreq_frequency_table *freq_table;
	unsigned int		nr_freqs;
};

#define CLK_ENABLE_ON_INIT	BIT(0)

#define CLK_ENABLE_REG_32BIT	BIT(1)	/* default access size */
#define CLK_ENABLE_REG_16BIT	BIT(2)
#define CLK_ENABLE_REG_8BIT	BIT(3)

#define CLK_MASK_DIV_ON_DISABLE	BIT(4)

#define CLK_ENABLE_REG_MASK	(CLK_ENABLE_REG_32BIT | \
				 CLK_ENABLE_REG_16BIT | \
				 CLK_ENABLE_REG_8BIT)

/* drivers/sh/clk.c */
unsigned long followparent_recalc(struct clk *);
void recalculate_root_clocks(void);
void propagate_rate(struct clk *);
int clk_reparent(struct clk *child, struct clk *parent);
int clk_register(struct clk *);
void clk_unregister(struct clk *);
void clk_enable_init_clocks(void);

struct clk_div_mult_table {
	unsigned int *divisors;
	unsigned int nr_divisors;
	unsigned int *multipliers;
	unsigned int nr_multipliers;
};

struct cpufreq_frequency_table;
void clk_rate_table_build(struct clk *clk,
			  struct cpufreq_frequency_table *freq_table,
			  int nr_freqs,
			  struct clk_div_mult_table *src_table,
			  unsigned long *bitmap);

long clk_rate_table_round(struct clk *clk,
			  struct cpufreq_frequency_table *freq_table,
			  unsigned long rate);

int clk_rate_table_find(struct clk *clk,
			struct cpufreq_frequency_table *freq_table,
			unsigned long rate);

long clk_rate_div_range_round(struct clk *clk, unsigned int div_min,
			      unsigned int div_max, unsigned long rate);

long clk_rate_mult_range_round(struct clk *clk, unsigned int mult_min,
			       unsigned int mult_max, unsigned long rate);

#define SH_CLK_MSTP(_parent, _enable_reg, _enable_bit, _status_reg, _flags) \
{									\
	.parent		= _parent,					\
	.enable_reg	= (void __iomem *)_enable_reg,			\
	.enable_bit	= _enable_bit,					\
	.status_reg	= _status_reg,					\
	.flags		= _flags,					\
}

#define SH_CLK_MSTP32(_p, _r, _b, _f)				\
	SH_CLK_MSTP(_p, _r, _b, 0, _f | CLK_ENABLE_REG_32BIT)

#define SH_CLK_MSTP32_STS(_p, _r, _b, _s, _f)			\
	SH_CLK_MSTP(_p, _r, _b, _s, _f | CLK_ENABLE_REG_32BIT)

#define SH_CLK_MSTP16(_p, _r, _b, _f)				\
	SH_CLK_MSTP(_p, _r, _b, 0, _f | CLK_ENABLE_REG_16BIT)

#define SH_CLK_MSTP8(_p, _r, _b, _f)				\
	SH_CLK_MSTP(_p, _r, _b, 0, _f | CLK_ENABLE_REG_8BIT)

int sh_clk_mstp_register(struct clk *clks, int nr);

/*
 * MSTP registration never really cared about access size, despite the
 * original enable/disable pairs assuming a 32-bit access. Clocks are
 * responsible for defining their access sizes either directly or via the
 * clock definition wrappers.
 */
static inline int __deprecated sh_clk_mstp32_register(struct clk *clks, int nr)
{
	return sh_clk_mstp_register(clks, nr);
}

#define SH_CLK_DIV4(_parent, _reg, _shift, _div_bitmap, _flags)	\
{								\
	.parent = _parent,					\
	.enable_reg = (void __iomem *)_reg,			\
	.enable_bit = _shift,					\
	.arch_flags = _div_bitmap,				\
	.div_mask = SH_CLK_DIV4_MSK,				\
	.flags = _flags,					\
}

struct clk_div_table {
	struct clk_div_mult_table *div_mult_table;
	void (*kick)(struct clk *clk);
};

#define clk_div4_table clk_div_table

int sh_clk_div4_register(struct clk *clks, int nr,
			 struct clk_div4_table *table);
int sh_clk_div4_enable_register(struct clk *clks, int nr,
			 struct clk_div4_table *table);
int sh_clk_div4_reparent_register(struct clk *clks, int nr,
			 struct clk_div4_table *table);

#define SH_CLK_DIV6_EXT(_reg, _flags, _parents,			\
			_num_parents, _src_shift, _src_width)	\
{								\
	.enable_reg = (void __iomem *)_reg,			\
	.enable_bit = 0, /* unused */				\
	.flags = _flags | CLK_MASK_DIV_ON_DISABLE,		\
	.div_mask = SH_CLK_DIV6_MSK,				\
	.parent_table = _parents,				\
	.parent_num = _num_parents,				\
	.src_shift = _src_shift,				\
	.src_width = _src_width,				\
}

#define SH_CLK_DIV6(_parent, _reg, _flags)			\
{								\
	.parent		= _parent,				\
	.enable_reg	= (void __iomem *)_reg,			\
	.enable_bit	= 0,	/* unused */			\
	.div_mask	= SH_CLK_DIV6_MSK,			\
	.flags		= _flags | CLK_MASK_DIV_ON_DISABLE,	\
}

int sh_clk_div6_register(struct clk *clks, int nr);
int sh_clk_div6_reparent_register(struct clk *clks, int nr);

#define CLKDEV_CON_ID(_id, _clk) { .con_id = _id, .clk = _clk }
#define CLKDEV_DEV_ID(_id, _clk) { .dev_id = _id, .clk = _clk }
#define CLKDEV_ICK_ID(_cid, _did, _clk) { .con_id = _cid, .dev_id = _did, .clk = _clk }

/* .enable_reg will be updated to .mapping on sh_clk_fsidiv_register() */
#define SH_CLK_FSIDIV(_reg, _parent)		\
{						\
	.enable_reg = (void __iomem *)_reg,	\
	.parent		= _parent,		\
}

int sh_clk_fsidiv_register(struct clk *clks, int nr);

#endif /* __SH_CLOCK_H */
