/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_geni_i2c

#if !defined(_TRACE_QCOM_GENI_I2C_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QCOM_GENI_I2C_H

#include <linux/tracepoint.h>

TRACE_EVENT(geni_i2c_bus_setup,
	    TP_PROTO(struct device *dev, u32 clk_freq, u8 clk_div,
		     u8 t_high_cnt, u8 t_low_cnt, u8 t_cycle_cnt),
	    TP_ARGS(dev, clk_freq, clk_div, t_high_cnt, t_low_cnt, t_cycle_cnt),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(u32, clk_freq)
			     __field(u8,  clk_div)
			     __field(u8,  t_high_cnt)
			     __field(u8,  t_low_cnt)
			     __field(u8,  t_cycle_cnt)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->clk_freq   = clk_freq;
			   __entry->clk_div    = clk_div;
			   __entry->t_high_cnt = t_high_cnt;
			   __entry->t_low_cnt  = t_low_cnt;
			   __entry->t_cycle_cnt = t_cycle_cnt;
	    ),

	    TP_printk("%s: clk_freq=%u clk_div=%u t_high=%u t_low=%u t_cycle=%u",
		      __get_str(name), __entry->clk_freq, __entry->clk_div,
		      __entry->t_high_cnt, __entry->t_low_cnt,
		      __entry->t_cycle_cnt)
);

TRACE_EVENT(geni_i2c_irq,
	    TP_PROTO(struct device *dev, u32 m_stat, u32 rx_st,
		     u32 dm_tx_st, u32 dm_rx_st),
	    TP_ARGS(dev, m_stat, rx_st, dm_tx_st, dm_rx_st),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(u32, m_stat)
			     __field(u32, rx_st)
			     __field(u32, dm_tx_st)
			     __field(u32, dm_rx_st)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->m_stat = m_stat;
			   __entry->rx_st = rx_st;
			   __entry->dm_tx_st = dm_tx_st;
			   __entry->dm_rx_st = dm_rx_st;
	    ),

	    TP_printk("%s: m_stat=0x%08x rx_st=0x%08x dm_tx=0x%08x dm_rx=0x%08x",
		      __get_str(name), __entry->m_stat, __entry->rx_st,
		      __entry->dm_tx_st, __entry->dm_rx_st)
);

TRACE_EVENT(geni_i2c_err,
	    TP_PROTO(struct device *dev, int err, const char *msg),
	    TP_ARGS(dev, err, msg),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(int, err)
			     __string(msg, msg)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->err = err;
			   __assign_str(msg);
	    ),

	    TP_printk("%s: err=%d msg=%s",
		      __get_str(name), __entry->err, __get_str(msg))
);

#endif /* _TRACE_QCOM_GENI_I2C_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
