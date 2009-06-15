/*
 * Freescale MPC5200 Audio DMA driver
 */

#ifndef __SOUND_SOC_FSL_MPC5200_DMA_H__
#define __SOUND_SOC_FSL_MPC5200_DMA_H__

#define PSC_STREAM_NAME_LEN 32

/**
 * psc_ac97_stream - Data specific to a single stream (playback or capture)
 * @active:		flag indicating if the stream is active
 * @psc_dma:		pointer back to parent psc_dma data structure
 * @bcom_task:		bestcomm task structure
 * @irq:		irq number for bestcomm task
 * @period_start:	physical address of start of DMA region
 * @period_end:		physical address of end of DMA region
 * @period_next_pt:	physical address of next DMA buffer to enqueue
 * @period_bytes:	size of DMA period in bytes
 */
struct psc_dma_stream {
	struct snd_pcm_runtime *runtime;
	snd_pcm_uframes_t appl_ptr;

	int active;
	struct psc_dma *psc_dma;
	struct bcom_task *bcom_task;
	int irq;
	struct snd_pcm_substream *stream;
	dma_addr_t period_start;
	dma_addr_t period_end;
	dma_addr_t period_next_pt;
	dma_addr_t period_current_pt;
	int period_bytes;
	int period_size;
};

/**
 * psc_dma - Private driver data
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
struct psc_dma {
	char name[32];
	struct mpc52xx_psc __iomem *psc_regs;
	struct mpc52xx_psc_fifo __iomem *fifo_regs;
	unsigned int irq;
	struct device *dev;
	spinlock_t lock;
	u32 sicr;
	uint sysclk;
	int imr;
	int id;
	unsigned int slots;

	/* per-stream data */
	struct psc_dma_stream playback;
	struct psc_dma_stream capture;

	/* Statistics */
	struct {
		unsigned long overrun_count;
		unsigned long underrun_count;
	} stats;
};

int mpc5200_audio_dma_create(struct of_device *op);
int mpc5200_audio_dma_destroy(struct of_device *op);

extern struct snd_soc_platform mpc5200_audio_dma_platform;

#endif /* __SOUND_SOC_FSL_MPC5200_DMA_H__ */
