#undef TRACE_SYSTEM
#define TRACE_SYSTEM regmap

#if !defined(_TRACE_REGMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_REGMAP_H

#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/tracepoint.h>

struct regmap;

/*
 * Log register events
 */
DECLARE_EVENT_CLASS(regmap_reg,

	TP_PROTO(struct device *dev, unsigned int reg,
		 unsigned int val),

	TP_ARGS(dev, reg, val),

	TP_STRUCT__entry(
		__string(	name,		dev_name(dev)	)
		__field(	unsigned int,	reg		)
		__field(	unsigned int,	val		)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(dev));
		__entry->reg = reg;
		__entry->val = val;
	),

	TP_printk("%s reg=%x val=%x", __get_str(name),
		  (unsigned int)__entry->reg,
		  (unsigned int)__entry->val)
);

DEFINE_EVENT(regmap_reg, regmap_reg_write,

	TP_PROTO(struct device *dev, unsigned int reg,
		 unsigned int val),

	TP_ARGS(dev, reg, val)

);

DEFINE_EVENT(regmap_reg, regmap_reg_read,

	TP_PROTO(struct device *dev, unsigned int reg,
		 unsigned int val),

	TP_ARGS(dev, reg, val)

);

DECLARE_EVENT_CLASS(regmap_block,

	TP_PROTO(struct device *dev, unsigned int reg, size_t count),

	TP_ARGS(dev, reg, count),

	TP_STRUCT__entry(
		__string(	name,		dev_name(dev)	)
		__field(	unsigned int,	reg		)
		__field(	size_t,		count		)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(dev));
		__entry->reg = reg;
		__entry->count = count;
	),

	TP_printk("%s reg=%x count=%d", __get_str(name),
		  (unsigned int)__entry->reg,
		  (size_t)__entry->count)
);

DEFINE_EVENT(regmap_block, regmap_hw_read_start,

	TP_PROTO(struct device *dev, unsigned int reg, size_t count),

	TP_ARGS(dev, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_read_done,

	TP_PROTO(struct device *dev, unsigned int reg, size_t count),

	TP_ARGS(dev, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_write_start,

	TP_PROTO(struct device *dev, unsigned int reg, size_t count),

	TP_ARGS(dev, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_write_done,

	TP_PROTO(struct device *dev, unsigned int reg, size_t count),

	TP_ARGS(dev, reg, count)
);

#endif /* _TRACE_REGMAP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
