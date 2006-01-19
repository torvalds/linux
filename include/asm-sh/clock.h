#ifndef __ASM_SH_CLOCK_H
#define __ASM_SH_CLOCK_H

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/seq_file.h>

struct clk;

struct clk_ops {
	void (*init)(struct clk *clk);
	void (*enable)(struct clk *clk);
	void (*disable)(struct clk *clk);
	void (*recalc)(struct clk *clk);
	int (*set_rate)(struct clk *clk, unsigned long rate);
};

struct clk {
	struct list_head	node;
	const char		*name;

	struct module		*owner;

	struct clk		*parent;
	struct clk_ops		*ops;

	struct kref		kref;

	unsigned long		rate;
	unsigned long		flags;
};

#define CLK_ALWAYS_ENABLED	(1 << 0)
#define CLK_RATE_PROPAGATES	(1 << 1)

/* Should be defined by processor-specific code */
void arch_init_clk_ops(struct clk_ops **, int type);

/* arch/sh/kernel/cpu/clock.c */
int clk_init(void);

int __clk_enable(struct clk *);
int clk_enable(struct clk *);

void __clk_disable(struct clk *);
void clk_disable(struct clk *);

int clk_set_rate(struct clk *, unsigned long rate);
unsigned long clk_get_rate(struct clk *);
void clk_recalc_rate(struct clk *);

struct clk *clk_get(const char *id);
void clk_put(struct clk *);

int clk_register(struct clk *);
void clk_unregister(struct clk *);

int show_clocks(struct seq_file *m);

#endif /* __ASM_SH_CLOCK_H */

