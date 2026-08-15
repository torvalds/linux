/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_geni_spi

#if !defined(_TRACE_QCOM_GENI_SPI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QCOM_GENI_SPI_H

#include <linux/tracepoint.h>

TRACE_EVENT(geni_spi_setup_params,
	    TP_PROTO(struct device *dev, u8 cs, u32 mode,
		     u32 mode_changed, bool cs_changed),
	    TP_ARGS(dev, cs, mode, mode_changed, cs_changed),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(u8, cs)
			     __field(u32, mode)
			     __field(u32, mode_changed)
			     __field(bool, cs_changed)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->cs = cs;
			   __entry->mode = mode;
			   __entry->mode_changed = mode_changed;
			   __entry->cs_changed = cs_changed;
	    ),

	    TP_printk("%s: cs=%u mode=0x%08x mode_changed=0x%08x cs_changed=%d",
		      __get_str(name), __entry->cs, __entry->mode,
		      __entry->mode_changed, __entry->cs_changed)
);

TRACE_EVENT(geni_spi_clk_cfg,
	    TP_PROTO(struct device *dev, unsigned long req_hz,
		     unsigned long sclk_hz, unsigned int clk_idx,
		     unsigned int clk_div, unsigned int bpw),
	    TP_ARGS(dev, req_hz, sclk_hz, clk_idx, clk_div, bpw),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(unsigned long, req_hz)
			     __field(unsigned long, sclk_hz)
			     __field(unsigned int, clk_idx)
			     __field(unsigned int, clk_div)
			     __field(unsigned int, bpw)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->req_hz = req_hz;
			   __entry->sclk_hz = sclk_hz;
			   __entry->clk_idx = clk_idx;
			   __entry->clk_div = clk_div;
			   __entry->bpw = bpw;
	    ),

	    TP_printk("%s: req_hz=%lu sclk_hz=%lu clk_idx=%u clk_div=%u bpw=%u",
		      __get_str(name), __entry->req_hz, __entry->sclk_hz,
		      __entry->clk_idx, __entry->clk_div, __entry->bpw)
);

TRACE_EVENT(geni_spi_transfer,
	    TP_PROTO(struct device *dev, unsigned int len, u32 m_cmd),
	    TP_ARGS(dev, len, m_cmd),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(unsigned int, len)
			     __field(u32, m_cmd)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->len = len;
			   __entry->m_cmd = m_cmd;
	    ),

	    TP_printk("%s: len=%u m_cmd=0x%08x",
		      __get_str(name), __entry->len, __entry->m_cmd)
);

TRACE_EVENT(geni_spi_irq,
	    TP_PROTO(struct device *dev, u32 m_irq, u32 dma_tx, u32 dma_rx),
	    TP_ARGS(dev, m_irq, dma_tx, dma_rx),

	    TP_STRUCT__entry(__string(name, dev_name(dev))
			     __field(u32, m_irq)
			     __field(u32, dma_tx)
			     __field(u32, dma_rx)
	    ),

	    TP_fast_assign(__assign_str(name);
			   __entry->m_irq = m_irq;
			   __entry->dma_tx = dma_tx;
			   __entry->dma_rx = dma_rx;
	    ),

	    TP_printk("%s: m_irq=0x%08x dma_tx=0x%08x dma_rx=0x%08x",
		      __get_str(name), __entry->m_irq, __entry->dma_tx,
		      __entry->dma_rx)
);

#endif /* _TRACE_QCOM_GENI_SPI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
