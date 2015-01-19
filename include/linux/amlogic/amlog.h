#ifndef __AMLOG_H
#define __AMLOG_H

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

#ifdef AMLOG

#define AMLOG_DEFAULT_LEVEL 0
#define AMLOG_DEFAULT_MASK 0xffffffffUL
#define AMLOG_DEFAULT_LEVEL_DESC "log_level."
#define AMLOG_DEFAULT_MASK_DESC "log_mask."

#define MODULE_AMLOG(def_level, def_mask, desc_level, desc_mask) \
u32 LOG_LEVEL_VAR = def_level; \
module_param(LOG_LEVEL_VAR, uint, S_IRWXU); \
MODULE_PARM_DESC(LOG_LEVEL_VAR, desc_level); \
u32 LOG_MASK_VAR = def_mask; \
module_param(LOG_MASK_VAR, uint, S_IRWXU); \
MODULE_PARM_DESC(LOG_MASK_VAR, desc_mask)

#ifndef LOG_LEVEL_VAR
#error LOG_LEVEL_VAR undefined.
#endif

#ifndef LOG_MASK_VAR
#error LOG_MASK_VAR undefined.
#endif

extern u32 LOG_LEVEL_VAR, LOG_MASK_VAR;

#define amlog(x...) printk(x)

#define amlog_level(level, x...) \
	do { \
		if (level >= LOG_LEVEL_VAR) \
			printk(x); \
	} while (0);

#define amlog_mask(mask, x...) \
	do { \
		if (mask & LOG_MASK_VAR) \
			printk(x); \
	} while (0);

#define amlog_mask_level(mask, level, x...) \
	do { \
		if ((level >= LOG_LEVEL_VAR) && (mask & LOG_MASK_VAR)) \
			printk(x); \
	} while (0);

#define amlog_if(cond, x...) do {if (cond) printk(x);} while {0};

#define amlog_level_if(cond, level, x...) \
	do { \
		if ((cond) && (level >= LOG_LEVEL_VAR)) \
			printk(x); \
	} while (0);

#define amlog_mask_if(cond, mask, x...) \
	do { \
		if ((cond) && (mask & LOG_MASK_VAR)) \
			printk(x); \
	} while (0);

#define amlog_mask_levelif(cond, mask, level, x...) \
	do { \
		if ((cond) && (level >= LOG_LEVEL_VAR) && (mask & LOG_MASK_VAR)) \
			printk(x...); \
	} while (0);

#else
#define MODULE_AMLOG(def_level, def_mask, desc_level, desc_mask)
#define amlog(x...)
#define amlog_level(level, x...)
#define amlog_mask(mask, x...)
#define amlog_mask_level(mask, level, x...)
#define amlog_if(cond, x...)
#define amlog_level_if(cond, level, x...)
#define amlog_mask_if(cond, mask, x...)
#define amlog_mask_level_if(cond, mask, level, x...)
#endif 

#endif /* __AMLOG_H */
