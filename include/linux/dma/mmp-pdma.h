#ifndef _MMP_PDMA_H_
#define _MMP_PDMA_H_

struct dma_chan;

#ifdef CONFIG_MMP_PDMA
bool mmp_pdma_filter_fn(struct dma_chan *chan, void *param);
#else
static inline bool mmp_pdma_filter_fn(struct dma_chan *chan, void *param)
{
	return false;
}
#endif

#endif /* _MMP_PDMA_H_ */
