/*
 * Freescale MPC5200 Audio DMA driver
 */

#ifndef __SOUND_SOC_FSL_MPC5200_DMA_H__
#define __SOUND_SOC_FSL_MPC5200_DMA_H__

/**
 * psc_i2s_stream - Data specific to a single stream (playback or capture)
 * @active:		flag indicating if the stream is active
 * @psc_i2s:		pointer back to parent psc_i2s data structure
 * @bcom_task:		bestcomm task structure
 * @irq:		irq number for bestcomm task
 * @period_start:	physical address of start of DMA region
 * @period_end:		physical address of end of DMA region
 * @period_next_pt:	physical address of next DMA buffer to enqueue
 * @period_bytes:	size of DMA period in bytes
 */
struct psc_i2s_stream {
	int active;
	struct psc_i2s *psc_i2s;
	struct bcom_task *bcom_task;
	int irq;
	struct snd_pcm_substream *stream;
	dma_addr_t period_start;
	dma_addr_t period_end;
	dma_addr_t period_next_pt;
	dma_addr_t period_current_pt;
	int period_bytes;
};

/**
 * psc_i2s - Private driver data
 * @name: short name for this device ("PSC0", "PSC1", etc)
 * @psc_regs: pointer to the PSC's registers
 * @fifo_regs: pointer to the PSC's FIFO registers
 * @irq: IRQ of this PSC
 * @dev: struct device pointer
 * @dai: the CPU DAI for this device
 * @sicr: Base value used in serial interface control register; mode is ORed
 *        with this value.
 * @playback: Playback stream context data
 * @capture: Capture stream context data
 */
struct psc_i2s {
	char name[32];
	struct mpc52xx_psc __iomem *psc_regs;
	struct mpc52xx_psc_fifo __iomem *fifo_regs;
	unsigned int irq;
	struct device *dev;
	struct snd_soc_dai dai;
	spinlock_t lock;
	u32 sicr;

	/* per-stream data */
	struct psc_i2s_stream playback;
	struct psc_i2s_stream capture;

	/* Statistics */
	struct {
		int overrun_count;
		int underrun_count;
	} stats;
};


int psc_i2s_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai);

int psc_i2s_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai);

void psc_i2s_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai);

int psc_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai);

extern struct snd_soc_platform psc_i2s_pcm_soc_platform;

#endif /* __SOUND_SOC_FSL_MPC5200_DMA_H__ */
