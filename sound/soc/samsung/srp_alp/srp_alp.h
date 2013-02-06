#ifndef __SRP_ALP_H
#define __SRP_ALP_H

#define SRP_DEV_MINOR	(250)

/* Base address */
#define SRP_IRAM_BASE		(0x02020000)
#define SRP_DMEM_BASE		(0x03000000)
#define SRP_COMMBOX_BASE	(0x03820000)

/* SRAM information */
#define IRAM_SIZE	((soc_is_exynos4412() || soc_is_exynos4212()) ? \
			(0x40000) : (0x20000))
#define DMEM_SIZE	(soc_is_exynos5250() ? \
			(0x28000) : (0x20000))
#define ICACHE_SIZE	(soc_is_exynos5250() ? \
			(0x18000) : (0x10000))
#define CMEM_SIZE       (0x9000)

/* SRAM & Commbox base address */
#define SRP_ICACHE_ADDR		(SRP_DMEM_BASE + DMEM_SIZE)
#define SRP_CMEM_ADDR		(SRP_ICACHE_ADDR + ICACHE_SIZE)

/* IBUF/OBUF Size */
#define IBUF_SIZE	(0x4000)
#define WBUF_SIZE	(IBUF_SIZE * 4)
#define OBUF_SIZE	(soc_is_exynos5250() ? \
			(0x4000) : (0x8000))

/* IBUF Offset */
#if defined(CONFIG_ARCH_EXYNOS4)
#define IBUF_OFFSET	((soc_is_exynos4412() || soc_is_exynos4212()) ? \
			(0x30000) : (0x10000))
#elif defined(CONFIG_ARCH_EXYNOS5)
#define IBUF_OFFSET	(0x8104)
#endif

/* OBUF Offset */
#if defined(CONFIG_ARCH_EXYNOS4)
#define OBUF_OFFSET	(0x4)
#elif defined(CONFIG_ARCH_EXYNOS5)
#define OBUF_OFFSET	(0x10104)
#endif

/* SRP Input/Output buffer physical address */
#if defined(CONFIG_ARCH_EXYNOS4)
#define SRP_IBUF_PHY_ADDR	(SRP_IRAM_BASE + IBUF_OFFSET)
#elif defined(CONFIG_ARCH_EXYNOS5)
#define SRP_IBUF_PHY_ADDR	(SRP_DMEM_BASE + IBUF_OFFSET)
#endif
#define SRP_OBUF_PHY_ADDR	(SRP_DMEM_BASE + OBUF_OFFSET)

/* IBUF/OBUF NUM */
#define IBUF_NUM	(0x2)
#define OBUF_NUM	(0x2)
#define START_THRESHOLD	(IBUF_SIZE * 3)

/* Commbox & Etc information */
#define COMMBOX_SIZE	(0x308)

/* Reserved memory on DRAM */
#define BASE_MEM_SIZE	(CONFIG_AUDIO_SAMSUNG_MEMSIZE_SRP << 10)
#define BITSTREAM_SIZE_MAX	(0x7FFFFFFF)
#define DATA_OFFSET	(0x18104)

/* F/W Endian Configuration */
#ifdef USE_FW_ENDIAN_CONVERT
#define ENDIAN_CHK_CONV(VAL)		\
	(((VAL >> 24) & 0x000000FF) |	\
	((VAL >> 8) & 0x0000FF00) |	\
	((VAL << 8) & 0x00FF0000) |	\
	((VAL << 24) & 0xFF000000))
#else
#define ENDIAN_CHK_CONV(VAL)	(VAL)
#endif

/* For Debugging */
#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
#define srp_info(x...)	pr_info("SRP: " x)
#define srp_debug(x...)	pr_debug("SRP: " x)
#define srp_err(x...)	pr_err("SRP_ERR: " x)
#else
#define srp_info(x...)
#define srp_debug(x...)
#define srp_err(x...)
#endif

/* For SRP firmware */
struct srp_fw_info {
	unsigned char *vliw;		/* VLIW */
	unsigned char *cga;		/* CGA */
	unsigned char *data;		/* DATA */

	unsigned int mem_base;		/* Physical address of base */
	unsigned int vliw_pa;		/* Physical address of VLIW */
	unsigned int cga_pa;		/* Physical address of CGA */
	unsigned int data_pa;		/* Physical address of DATA */
	unsigned long vliw_size;	/* Size of VLIW */
	unsigned long cga_size;		/* Size of CGA */
	unsigned long data_size;	/* Size of DATA */
};

/* OBUF/IBUF information */
struct srp_buf_info {
	void		*mmapped_addr;
	void		*addr;
	unsigned int	mmapped_size;
	unsigned int	size;
	int		num;
};

/* Decoding information */
struct srp_dec_info {
	unsigned int sample_rate;
	unsigned int channels;
};

struct srp_for_suspend {
	unsigned char	*ibuf;
	unsigned char	*obuf;
	unsigned char	*commbox;
};

struct srp_info {
	struct srp_buf_info	ibuf_info;
	struct srp_buf_info	obuf_info;
	struct srp_buf_info	pcm_info;

	struct srp_fw_info	fw_info;
	struct srp_dec_info	dec_info;
	struct srp_for_suspend  sp_data;

	void __iomem	*iram;
	void __iomem	*dmem;
	void __iomem	*icache;
	void __iomem	*cmem;
	void __iomem	*commbox;

	/* MMAP base address */
	unsigned int	mmap_base;

	/* IBUF informaion */
	unsigned char	*ibuf0;
	unsigned char	*ibuf1;
	unsigned int	ibuf0_pa;
	unsigned int	ibuf1_pa;
	unsigned int	ibuf_num;
	unsigned long	ibuf_size;
	unsigned long	ibuf_offset;
	unsigned int	ibuf_next;
	unsigned int	ibuf_empty[2];

	/* OBUF informaion */
	unsigned char	*obuf0;
	unsigned char	*obuf1;
	unsigned int	obuf0_pa;
	unsigned int	obuf1_pa;
	unsigned int	obuf_num;
	unsigned long	obuf_size;
	unsigned long	obuf_offset;
	unsigned int	obuf_fill_done[2];
	unsigned int	obuf_copy_done[2];
	unsigned int	obuf_ready;
	unsigned int	obuf_next;

	/* For EVT0 : will be removed on EVT1 */
	unsigned char	*pcm_obuf0;
	unsigned char	*pcm_obuf1;
	unsigned int	pcm_obuf_pa;

	/* Temporary BUF informaion */
	unsigned char	*wbuf;
	unsigned long	wbuf_size;
	unsigned long	wbuf_pos;
	unsigned long	wbuf_fill_size;

	/* Decoding informaion */
	unsigned long	set_bitstream_size;
	unsigned long	pcm_size;

	/* SRP status information */
	unsigned int	first_init;
	unsigned int	decoding_started;
	unsigned int	is_opened;
	unsigned int	is_running;
	unsigned int	is_pending;
	unsigned int	block_mode;
	unsigned int	stop_after_eos;
	unsigned int	wait_for_eos;
	unsigned int	prepare_for_eos;
	unsigned int	play_done;

	bool	pm_suspended;
	bool	pm_resumed;
	bool	initialized;

	/* Function pointer for clock control */
	void	(*audss_clk_enable)(bool enable);
};

/* SRP Pending On/Off status */
enum {
	RUN = 0,
	STALL,
};

/* Request Suspend/Resume */
enum {
	SUSPEND = 0,
	RESUME,
	RESET,
};

#endif /* __SRP_ALP_H */
