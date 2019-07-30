#if !defined(_TRACE_TEGRA_APB_DMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TEGRA_APM_DMA_H

#include <linux/tracepoint.h>
#include <linux/dmaengine.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tegra_apb_dma

TRACE_EVENT(tegra_dma_tx_status,
	TP_PROTO(struct dma_chan *dc, dma_cookie_t cookie, struct dma_tx_state *state),
	TP_ARGS(dc, cookie, state),
	TP_STRUCT__entry(
		__string(chan,	dev_name(&dc->dev->device))
		__field(dma_cookie_t, cookie)
		__field(__u32,	residue)
	),
	TP_fast_assign(
		__assign_str(chan, dev_name(&dc->dev->device));
		__entry->cookie = cookie;
		__entry->residue = state ? state->residue : (u32)-1;
	),
	TP_printk("channel %s: dma cookie %d, residue %u",
		  __get_str(chan), __entry->cookie, __entry->residue)
);

TRACE_EVENT(tegra_dma_complete_cb,
	TP_PROTO(struct dma_chan *dc, int count, void *ptr),
	TP_ARGS(dc, count, ptr),
	TP_STRUCT__entry(
		__string(chan,	dev_name(&dc->dev->device))
		__field(int,	count)
		__field(void *,	ptr)
		),
	TP_fast_assign(
		__assign_str(chan, dev_name(&dc->dev->device));
		__entry->count = count;
		__entry->ptr = ptr;
		),
	TP_printk("channel %s: done %d, ptr %p",
		  __get_str(chan), __entry->count, __entry->ptr)
);

TRACE_EVENT(tegra_dma_isr,
	TP_PROTO(struct dma_chan *dc, int irq),
	TP_ARGS(dc, irq),
	TP_STRUCT__entry(
		__string(chan,	dev_name(&dc->dev->device))
		__field(int,	irq)
	),
	TP_fast_assign(
		__assign_str(chan, dev_name(&dc->dev->device));
		__entry->irq = irq;
	),
	TP_printk("%s: irq %d\n",  __get_str(chan), __entry->irq)
);

#endif /*  _TRACE_TEGRADMA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
