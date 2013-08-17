#ifndef __SRP_ALP_H
#define __SRP_ALP_H

#define SRP_DEV_MINOR	(250)

#define BITSTREAM_SIZE_MAX	(0x7FFFFFFF)

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
	const struct firmware *vliw;		/* VLIW */
	const struct firmware *cga;		/* CGA */
	const struct firmware *data;		/* DATA */

	unsigned char *base_va;		/* Virtual address of base */
	unsigned int base_pa;		/* Physical address of base */
	unsigned int vliw_pa;		/* Physical address of VLIW */
	unsigned int cga_pa;		/* Physical address of CGA */
	unsigned int data_pa;		/* Physical address of DATA */
	unsigned char *vliw_va;
	unsigned char *cga_va;
	unsigned char *data_va;

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
	struct exynos_srp_pdata *pdata;
	struct srp_buf_info	ibuf_info;
	struct srp_buf_info	obuf_info;
	struct srp_buf_info	pcm_info;

	struct srp_fw_info	fw_info;
	struct srp_dec_info	dec_info;
	struct srp_for_suspend  sp_data;
	struct clk		*clk;

	void __iomem	*iram;
	void __iomem	*dmem;
	void __iomem	*icache;
	void __iomem	*cmem;
	void __iomem	*commbox;

	/* IBUF informaion */
	unsigned char	*ibuf0;
	unsigned char	*ibuf1;
	unsigned int	ibuf0_pa;
	unsigned int	ibuf1_pa;
	unsigned int	ibuf_num;
	unsigned int	ibuf_next;
	unsigned int	ibuf_empty[2];

	/* OBUF informaion */
	unsigned char	*obuf0;
	unsigned char	*obuf1;
	unsigned int	obuf0_pa;
	unsigned int	obuf1_pa;
	unsigned int	obuf_num;
	unsigned int	obuf_fill_done[2];
	unsigned int	obuf_copy_done[2];
	unsigned int	obuf_ready;
	unsigned int	obuf_next;

	/* Temporary BUF informaion */
	unsigned char	*wbuf;
	unsigned long	wbuf_size;
	unsigned long	wbuf_pos;
	unsigned long	wbuf_fill_size;

	/* Decoding informaion */
	unsigned long	set_bitstream_size;
	unsigned long	pcm_size;

	/* SRP status information */
	unsigned int	decoding_started;
	unsigned int	is_opened;
	unsigned int	is_pending;
	unsigned int	block_mode;
	unsigned int	stop_after_eos;
	unsigned int	wait_for_eos;
	unsigned int	prepare_for_eos;
	unsigned int	play_done;
	unsigned int	idma_addr;
	unsigned int	data_offset;

	bool	pm_suspended;
	bool	hw_reset_stat;
	bool	is_loaded;
	bool	initialized;
	bool	idle;

	/* Parameter to control Runtime PM */
	void	*pm_info;
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
	SW_RESET,
};

/* Core suspend parameter */
enum {
	RUNTIME = 0,
	SLEEP,
};

#endif /* __SRP_ALP_H */
