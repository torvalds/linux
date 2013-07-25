#undef TRACE_SYSTEM
#define TRACE_SYSTEM regmap

#if !defined(_TRACE_REGMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_REGMAP_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

struct device;
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

DEFINE_EVENT(regmap_reg, regmap_reg_read_cache,

	TP_PROTO(struct device *dev, unsigned int reg,
		 unsigned int val),

	TP_ARGS(dev, reg, val)

);

DECLARE_EVENT_CLASS(regmap_block,

	TP_PROTO(struct device *dev, unsigned int reg, int count),

	TP_ARGS(dev, reg, count),

	TP_STRUCT__entry(
		__string(	name,		dev_name(dev)	)
		__field(	unsigned int,	reg		)
		__field(	int,		count		)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(dev));
		__entry->reg = reg;
		__entry->count = count;
	),

	TP_printk("%s reg=%x count=%d", __get_str(name),
		  (unsigned int)__entry->reg,
		  (int)__entry->count)
);

DEFINE_EVENT(regmap_block, regmap_hw_read_start,

	TP_PROTO(struct device *dev, unsigned int reg, int count),

	TP_ARGS(dev, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_read_done,

	TP_PROTO(struct device *dev, unsigned int reg, int count),

	TP_ARGS(dev, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_write_start,

	TP_PROTO(struct device *dev, unsigned int reg, int count),

	TP_ARGS(dev, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_write_done,

	TP_PROTO(struct device *dev, unsigned int reg, int count),

	TP_ARGS(dev, reg, count)
);

TRACE_EVENT(regcache_sync,

	TP_PROTO(struct device *dev, const char *type,
		 const char *status),

	TP_ARGS(dev, type, status),

	TP_STRUCT__entry(
		__string(       name,           dev_name(dev)   )
		__string(	status,		status		)
		__string(	type,		type		)
		__field(	int,		type		)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(dev));
		__assign_str(status, status);
		__assign_str(type, type);
	),

	TP_printk("%s type=%s status=%s", __get_str(name),
		  __get_str(type), __get_str(status))
);

DECLARE_EVENT_CLASS(regmap_bool,

	TP_PROTO(struct device *dev, bool flag),

	TP_ARGS(dev, flag),

	TP_STRUCT__entry(
		__string(	name,		dev_name(dev)	)
		__field(	int,		flag		)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(dev));
		__entry->flag = flag;
	),

	TP_printk("%s flag=%d", __get_str(name),
		  (int)__entry->flag)
);

DEFINE_EVENT(regmap_bool, regmap_cache_only,

	TP_PROTO(struct device *dev, bool flag),

	TP_ARGS(dev, flag)

);

DEFINE_EVENT(regmap_bool, regmap_cache_bypass,

	TP_PROTO(struct device *dev, bool flag),

	TP_ARGS(dev, flag)

);

DECLARE_EVENT_CLASS(regmap_async,

	TP_PROTO(struct device *dev),

	TP_ARGS(dev),

	TP_STRUCT__entry(
		__string(	name,		dev_name(dev)	)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(dev));
	),

	TP_printk("%s", __get_str(name))
);

DEFINE_EVENT(regmap_block, regmap_async_write_start,

	TP_PROTO(struct device *dev, unsigned int reg, int count),

	TP_ARGS(dev, reg, count)
);

DEFINE_EVENT(regmap_async, regmap_async_io_complete,

	TP_PROTO(struct device *dev),

	TP_ARGS(dev)

);

DEFINE_EVENT(regmap_async, regmap_async_complete_start,

	TP_PROTO(struct device *dev),

	TP_ARGS(dev)

);

DEFINE_EVENT(regmap_async, regmap_async_complete_done,

	TP_PROTO(struct device *dev),

	TP_ARGS(dev)

);

TRACE_EVENT(regcache_drop_region,

	TP_PROTO(struct device *dev, unsigned int from,
		 unsigned int to),

	TP_ARGS(dev, from, to),

	TP_STRUCT__entry(
		__string(       name,           dev_name(dev)   )
		__field(	unsigned int,	from		)
		__field(	unsigned int,	to		)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(dev));
		__entry->from = from;
		__entry->to = to;
	),

	TP_printk("%s %u-%u", __get_str(name), (unsigned int)__entry->from,
		  (unsigned int)__entry->to)
);

#endif /* _TRACE_REGMAP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
