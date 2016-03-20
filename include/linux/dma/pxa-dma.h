#ifndef _PXA_DMA_H_
#define _PXA_DMA_H_

enum pxad_chan_prio {
	PXAD_PRIO_HIGHEST = 0,
	PXAD_PRIO_NORMAL,
	PXAD_PRIO_LOW,
	PXAD_PRIO_LOWEST,
};

struct pxad_param {
	unsigned int drcmr;
	enum pxad_chan_prio prio;
};

struct dma_chan;

#ifdef CONFIG_PXA_DMA
bool pxad_filter_fn(struct dma_chan *chan, void *param);
#else
static inline bool pxad_filter_fn(struct dma_chan *chan, void *param)
{
	return false;
}
#endif

#endif /* _PXA_DMA_H_ */
