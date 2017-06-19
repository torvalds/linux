/*
 * Intel Code Loader DMA support
 *
 * Copyright (C) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef SKL_SST_CLDMA_H_
#define SKL_SST_CLDMA_H_

#define FW_CL_STREAM_NUMBER		0x1

#define DMA_ADDRESS_128_BITS_ALIGNMENT	7
#define BDL_ALIGN(x)			(x >> DMA_ADDRESS_128_BITS_ALIGNMENT)

#define SKL_ADSPIC_CL_DMA			0x2
#define SKL_ADSPIS_CL_DMA			0x2
#define SKL_CL_DMA_SD_INT_DESC_ERR		0x10 /* Descriptor error interrupt */
#define SKL_CL_DMA_SD_INT_FIFO_ERR		0x08 /* FIFO error interrupt */
#define SKL_CL_DMA_SD_INT_COMPLETE		0x04 /* Buffer completion interrupt */

/* Intel HD Audio Code Loader DMA Registers */

#define HDA_ADSP_LOADER_BASE		0x80

/* Stream Registers */
#define SKL_ADSP_REG_CL_SD_CTL			(HDA_ADSP_LOADER_BASE + 0x00)
#define SKL_ADSP_REG_CL_SD_STS			(HDA_ADSP_LOADER_BASE + 0x03)
#define SKL_ADSP_REG_CL_SD_LPIB			(HDA_ADSP_LOADER_BASE + 0x04)
#define SKL_ADSP_REG_CL_SD_CBL			(HDA_ADSP_LOADER_BASE + 0x08)
#define SKL_ADSP_REG_CL_SD_LVI			(HDA_ADSP_LOADER_BASE + 0x0c)
#define SKL_ADSP_REG_CL_SD_FIFOW		(HDA_ADSP_LOADER_BASE + 0x0e)
#define SKL_ADSP_REG_CL_SD_FIFOSIZE		(HDA_ADSP_LOADER_BASE + 0x10)
#define SKL_ADSP_REG_CL_SD_FORMAT		(HDA_ADSP_LOADER_BASE + 0x12)
#define SKL_ADSP_REG_CL_SD_FIFOL		(HDA_ADSP_LOADER_BASE + 0x14)
#define SKL_ADSP_REG_CL_SD_BDLPL		(HDA_ADSP_LOADER_BASE + 0x18)
#define SKL_ADSP_REG_CL_SD_BDLPU		(HDA_ADSP_LOADER_BASE + 0x1c)

/* CL: Software Position Based FIFO Capability Registers */
#define SKL_ADSP_REG_CL_SPBFIFO			(HDA_ADSP_LOADER_BASE + 0x20)
#define SKL_ADSP_REG_CL_SPBFIFO_SPBFCH		(SKL_ADSP_REG_CL_SPBFIFO + 0x0)
#define SKL_ADSP_REG_CL_SPBFIFO_SPBFCCTL	(SKL_ADSP_REG_CL_SPBFIFO + 0x4)
#define SKL_ADSP_REG_CL_SPBFIFO_SPIB		(SKL_ADSP_REG_CL_SPBFIFO + 0x8)
#define SKL_ADSP_REG_CL_SPBFIFO_MAXFIFOS	(SKL_ADSP_REG_CL_SPBFIFO + 0xc)

/* CL: Stream Descriptor x Control */

/* Stream Reset */
#define CL_SD_CTL_SRST_SHIFT		0
#define CL_SD_CTL_SRST_MASK		(1 << CL_SD_CTL_SRST_SHIFT)
#define CL_SD_CTL_SRST(x)		\
			((x << CL_SD_CTL_SRST_SHIFT) & CL_SD_CTL_SRST_MASK)

/* Stream Run */
#define CL_SD_CTL_RUN_SHIFT		1
#define CL_SD_CTL_RUN_MASK		(1 << CL_SD_CTL_RUN_SHIFT)
#define CL_SD_CTL_RUN(x)		\
			((x << CL_SD_CTL_RUN_SHIFT) & CL_SD_CTL_RUN_MASK)

/* Interrupt On Completion Enable */
#define CL_SD_CTL_IOCE_SHIFT		2
#define CL_SD_CTL_IOCE_MASK		(1 << CL_SD_CTL_IOCE_SHIFT)
#define CL_SD_CTL_IOCE(x)		\
			((x << CL_SD_CTL_IOCE_SHIFT) & CL_SD_CTL_IOCE_MASK)

/* FIFO Error Interrupt Enable */
#define CL_SD_CTL_FEIE_SHIFT		3
#define CL_SD_CTL_FEIE_MASK		(1 << CL_SD_CTL_FEIE_SHIFT)
#define CL_SD_CTL_FEIE(x)		\
			((x << CL_SD_CTL_FEIE_SHIFT) & CL_SD_CTL_FEIE_MASK)

/* Descriptor Error Interrupt Enable */
#define CL_SD_CTL_DEIE_SHIFT		4
#define CL_SD_CTL_DEIE_MASK		(1 << CL_SD_CTL_DEIE_SHIFT)
#define CL_SD_CTL_DEIE(x)		\
			((x << CL_SD_CTL_DEIE_SHIFT) & CL_SD_CTL_DEIE_MASK)

/* FIFO Limit Change */
#define CL_SD_CTL_FIFOLC_SHIFT		5
#define CL_SD_CTL_FIFOLC_MASK		(1 << CL_SD_CTL_FIFOLC_SHIFT)
#define CL_SD_CTL_FIFOLC(x)		\
			((x << CL_SD_CTL_FIFOLC_SHIFT) & CL_SD_CTL_FIFOLC_MASK)

/* Stripe Control */
#define CL_SD_CTL_STRIPE_SHIFT		16
#define CL_SD_CTL_STRIPE_MASK		(0x3 << CL_SD_CTL_STRIPE_SHIFT)
#define CL_SD_CTL_STRIPE(x)		\
			((x << CL_SD_CTL_STRIPE_SHIFT) & CL_SD_CTL_STRIPE_MASK)

/* Traffic Priority */
#define CL_SD_CTL_TP_SHIFT		18
#define CL_SD_CTL_TP_MASK		(1 << CL_SD_CTL_TP_SHIFT)
#define CL_SD_CTL_TP(x)			\
			((x << CL_SD_CTL_TP_SHIFT) & CL_SD_CTL_TP_MASK)

/* Bidirectional Direction Control */
#define CL_SD_CTL_DIR_SHIFT		19
#define CL_SD_CTL_DIR_MASK		(1 << CL_SD_CTL_DIR_SHIFT)
#define CL_SD_CTL_DIR(x)		\
			((x << CL_SD_CTL_DIR_SHIFT) & CL_SD_CTL_DIR_MASK)

/* Stream Number */
#define CL_SD_CTL_STRM_SHIFT		20
#define CL_SD_CTL_STRM_MASK		(0xf << CL_SD_CTL_STRM_SHIFT)
#define CL_SD_CTL_STRM(x)		\
			((x << CL_SD_CTL_STRM_SHIFT) & CL_SD_CTL_STRM_MASK)

/* CL: Stream Descriptor x Status */

/* Buffer Completion Interrupt Status */
#define CL_SD_STS_BCIS(x)		CL_SD_CTL_IOCE(x)

/* FIFO Error */
#define CL_SD_STS_FIFOE(x)		CL_SD_CTL_FEIE(x)

/* Descriptor Error */
#define CL_SD_STS_DESE(x)		CL_SD_CTL_DEIE(x)

/* FIFO Ready */
#define CL_SD_STS_FIFORDY(x)	CL_SD_CTL_FIFOLC(x)


/* CL: Stream Descriptor x Last Valid Index */
#define CL_SD_LVI_SHIFT			0
#define CL_SD_LVI_MASK			(0xff << CL_SD_LVI_SHIFT)
#define CL_SD_LVI(x)			((x << CL_SD_LVI_SHIFT) & CL_SD_LVI_MASK)

/* CL: Stream Descriptor x FIFO Eviction Watermark */
#define CL_SD_FIFOW_SHIFT		0
#define CL_SD_FIFOW_MASK		(0x7 << CL_SD_FIFOW_SHIFT)
#define CL_SD_FIFOW(x)			\
			((x << CL_SD_FIFOW_SHIFT) & CL_SD_FIFOW_MASK)

/* CL: Stream Descriptor x Buffer Descriptor List Pointer Lower Base Address */

/* Protect Bits */
#define CL_SD_BDLPLBA_PROT_SHIFT	0
#define CL_SD_BDLPLBA_PROT_MASK		(1 << CL_SD_BDLPLBA_PROT_SHIFT)
#define CL_SD_BDLPLBA_PROT(x)		\
		((x << CL_SD_BDLPLBA_PROT_SHIFT) & CL_SD_BDLPLBA_PROT_MASK)

/* Buffer Descriptor List Lower Base Address */
#define CL_SD_BDLPLBA_SHIFT		7
#define CL_SD_BDLPLBA_MASK		(0x1ffffff << CL_SD_BDLPLBA_SHIFT)
#define CL_SD_BDLPLBA(x)		\
	((BDL_ALIGN(lower_32_bits(x)) << CL_SD_BDLPLBA_SHIFT) & CL_SD_BDLPLBA_MASK)

/* Buffer Descriptor List Upper Base Address */
#define CL_SD_BDLPUBA_SHIFT		0
#define CL_SD_BDLPUBA_MASK		(0xffffffff << CL_SD_BDLPUBA_SHIFT)
#define CL_SD_BDLPUBA(x)		\
		((upper_32_bits(x) << CL_SD_BDLPUBA_SHIFT) & CL_SD_BDLPUBA_MASK)

/*
 * Code Loader - Software Position Based FIFO
 * Capability Registers x Software Position Based FIFO Header
 */

/* Next Capability Pointer */
#define CL_SPBFIFO_SPBFCH_PTR_SHIFT	0
#define CL_SPBFIFO_SPBFCH_PTR_MASK	(0xff << CL_SPBFIFO_SPBFCH_PTR_SHIFT)
#define CL_SPBFIFO_SPBFCH_PTR(x)	\
		((x << CL_SPBFIFO_SPBFCH_PTR_SHIFT) & CL_SPBFIFO_SPBFCH_PTR_MASK)

/* Capability Identifier */
#define CL_SPBFIFO_SPBFCH_ID_SHIFT	16
#define CL_SPBFIFO_SPBFCH_ID_MASK	(0xfff << CL_SPBFIFO_SPBFCH_ID_SHIFT)
#define CL_SPBFIFO_SPBFCH_ID(x)		\
		((x << CL_SPBFIFO_SPBFCH_ID_SHIFT) & CL_SPBFIFO_SPBFCH_ID_MASK)

/* Capability Version */
#define CL_SPBFIFO_SPBFCH_VER_SHIFT	28
#define CL_SPBFIFO_SPBFCH_VER_MASK	(0xf << CL_SPBFIFO_SPBFCH_VER_SHIFT)
#define CL_SPBFIFO_SPBFCH_VER(x)	\
	((x << CL_SPBFIFO_SPBFCH_VER_SHIFT) & CL_SPBFIFO_SPBFCH_VER_MASK)

/* Software Position in Buffer Enable */
#define CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT	0
#define CL_SPBFIFO_SPBFCCTL_SPIBE_MASK	(1 << CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT)
#define CL_SPBFIFO_SPBFCCTL_SPIBE(x)	\
	((x << CL_SPBFIFO_SPBFCCTL_SPIBE_SHIFT) & CL_SPBFIFO_SPBFCCTL_SPIBE_MASK)

/* SST IPC SKL defines */
#define SKL_WAIT_TIMEOUT		500	/* 500 msec */
#define SKL_MAX_BUFFER_SIZE		(32 * PAGE_SIZE)

enum skl_cl_dma_wake_states {
	SKL_CL_DMA_STATUS_NONE = 0,
	SKL_CL_DMA_BUF_COMPLETE,
	SKL_CL_DMA_ERR,	/* TODO: Expand the error states */
};

struct sst_dsp;

struct skl_cl_dev_ops {
	void (*cl_setup_bdle)(struct sst_dsp *ctx,
			struct snd_dma_buffer *dmab_data,
			u32 **bdlp, int size, int with_ioc);
	void (*cl_setup_controller)(struct sst_dsp *ctx,
			struct snd_dma_buffer *dmab_bdl,
			unsigned int max_size, u32 page_count);
	void (*cl_setup_spb)(struct sst_dsp  *ctx,
			unsigned int size, bool enable);
	void (*cl_cleanup_spb)(struct sst_dsp  *ctx);
	void (*cl_trigger)(struct sst_dsp  *ctx, bool enable);
	void (*cl_cleanup_controller)(struct sst_dsp  *ctx);
	int (*cl_copy_to_dmabuf)(struct sst_dsp *ctx,
			const void *bin, u32 size, bool wait);
	void (*cl_stop_dma)(struct sst_dsp *ctx);
};

/**
 * skl_cl_dev - holds information for code loader dma transfer
 *
 * @dmab_data: buffer pointer
 * @dmab_bdl: buffer descriptor list
 * @bufsize: ring buffer size
 * @frags: Last valid buffer descriptor index in the BDL
 * @curr_spib_pos: Current position in ring buffer
 * @dma_buffer_offset: dma buffer offset
 * @ops: operations supported on CL dma
 * @wait_queue: wait queue to wake for wake event
 * @wake_status: DMA wake status
 * @wait_condition: condition to wait on wait queue
 * @cl_dma_lock: for synchronized access to cldma
 */
struct skl_cl_dev {
	struct snd_dma_buffer dmab_data;
	struct snd_dma_buffer dmab_bdl;

	unsigned int bufsize;
	unsigned int frags;

	unsigned int curr_spib_pos;
	unsigned int dma_buffer_offset;
	struct skl_cl_dev_ops ops;

	wait_queue_head_t wait_queue;
	int wake_status;
	bool wait_condition;
};

#endif /* SKL_SST_CLDMA_H_ */
