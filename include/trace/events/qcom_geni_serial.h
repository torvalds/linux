/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_geni_serial

#if !defined(_TRACE_QCOM_GENI_SERIAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QCOM_GENI_SERIAL_H

#include <linux/device.h>
#include <linux/tracepoint.h>

TRACE_EVENT(geni_serial_set_termios,
	    TP_PROTO(struct device *dev, unsigned int baud,
		     unsigned int bits_per_char, u32 tx_trans_cfg,
		     u32 tx_parity_cfg, u32 rx_trans_cfg,
		     u32 rx_parity_cfg, u32 stop_bit_len),
	    TP_ARGS(dev, baud, bits_per_char, tx_trans_cfg, tx_parity_cfg,
		    rx_trans_cfg, rx_parity_cfg, stop_bit_len),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(unsigned int, baud)
			     __field(unsigned int, bits_per_char)
			     __field(u32, tx_trans_cfg)
			     __field(u32, tx_parity_cfg)
			     __field(u32, rx_trans_cfg)
			     __field(u32, rx_parity_cfg)
			     __field(u32, stop_bit_len)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->baud = baud;
			   __entry->bits_per_char = bits_per_char;
			   __entry->tx_trans_cfg = tx_trans_cfg;
			   __entry->tx_parity_cfg = tx_parity_cfg;
			   __entry->rx_trans_cfg = rx_trans_cfg;
			   __entry->rx_parity_cfg = rx_parity_cfg;
			   __entry->stop_bit_len = stop_bit_len;
	    ),

	    TP_printk("%s: baud=%u bpc=%u tx_trans=0x%08x tx_par=0x%08x rx_trans=0x%08x rx_par=0x%08x stop=%u",
		      __get_str(name), __entry->baud, __entry->bits_per_char,
		      __entry->tx_trans_cfg, __entry->tx_parity_cfg,
		      __entry->rx_trans_cfg, __entry->rx_parity_cfg,
		      __entry->stop_bit_len)
);

TRACE_EVENT(geni_serial_clk_cfg,
	    TP_PROTO(struct device *dev, unsigned int desired_rate,
		     unsigned long clk_rate, unsigned int clk_div,
		     unsigned int clk_idx),
	    TP_ARGS(dev, desired_rate, clk_rate, clk_div, clk_idx),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(unsigned int, desired_rate)
			     __field(unsigned long, clk_rate)
			     __field(unsigned int, clk_div)
			     __field(unsigned int, clk_idx)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->desired_rate = desired_rate;
			   __entry->clk_rate = clk_rate;
			   __entry->clk_div = clk_div;
			   __entry->clk_idx = clk_idx;
	    ),

	    TP_printk("%s: desired_rate=%u clk_rate=%lu clk_div=%u clk_idx=%u",
		      __get_str(name), __entry->desired_rate, __entry->clk_rate,
		      __entry->clk_div, __entry->clk_idx)
);

TRACE_EVENT(geni_serial_irq,
	    TP_PROTO(struct device *dev, u32 m_irq, u32 s_irq,
		     u32 dma_tx, u32 dma_rx),
	    TP_ARGS(dev, m_irq, s_irq, dma_tx, dma_rx),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(u32, m_irq)
			     __field(u32, s_irq)
			     __field(u32, dma_tx)
			     __field(u32, dma_rx)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->m_irq = m_irq;
			   __entry->s_irq = s_irq;
			   __entry->dma_tx = dma_tx;
			   __entry->dma_rx = dma_rx;
	    ),

	    TP_printk("%s: m_irq=0x%08x s_irq=0x%08x dma_tx=0x%08x dma_rx=0x%08x",
		      __get_str(name), __entry->m_irq, __entry->s_irq,
		      __entry->dma_tx, __entry->dma_rx)
);

DECLARE_EVENT_CLASS(geni_serial_data,
		    TP_PROTO(struct device *dev, const u8 *buf, unsigned int len),
		    TP_ARGS(dev, buf, len),

		    TP_STRUCT__entry(__string(name, dev_name(dev))
				     __field(unsigned int, len)
				     __dynamic_array(u8, data, len)
		    ),

		    TP_fast_assign(__assign_str(name);
				   __entry->len = len;
				   memcpy(__get_dynamic_array(data), buf, len);
		    ),

		    TP_printk("%s: len=%u data=%s",
			      __get_str(name), __entry->len,
			      __print_hex(__get_dynamic_array(data), __entry->len))
);

DEFINE_EVENT(geni_serial_data, geni_serial_tx_data,
	     TP_PROTO(struct device *dev, const u8 *buf, unsigned int len),
	     TP_ARGS(dev, buf, len)
);

DEFINE_EVENT(geni_serial_data, geni_serial_rx_data,
	     TP_PROTO(struct device *dev, const u8 *buf, unsigned int len),
	     TP_ARGS(dev, buf, len)
);

TRACE_EVENT(geni_serial_set_mctrl,
	    TP_PROTO(struct device *dev, unsigned int mctrl,
		     u32 uart_manual_rfr),
	    TP_ARGS(dev, mctrl, uart_manual_rfr),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(unsigned int, mctrl)
			     __field(u32, uart_manual_rfr)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->mctrl = mctrl;
			   __entry->uart_manual_rfr = uart_manual_rfr;
	    ),

	    TP_printk("%s: mctrl=0x%04x uart_manual_rfr=0x%08x",
		      __get_str(name), __entry->mctrl, __entry->uart_manual_rfr)
);

TRACE_EVENT(geni_serial_get_mctrl,
	    TP_PROTO(struct device *dev, unsigned int mctrl, u32 geni_ios),
	    TP_ARGS(dev, mctrl, geni_ios),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(unsigned int, mctrl)
			     __field(u32, geni_ios)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->mctrl = mctrl;
			   __entry->geni_ios = geni_ios;
	    ),

	    TP_printk("%s: mctrl=0x%04x geni_ios=0x%08x",
		      __get_str(name), __entry->mctrl, __entry->geni_ios)
);

#endif /* _TRACE_QCOM_GENI_SERIAL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
