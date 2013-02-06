/* sound/soc/samsung/srp.c
 *
 * SRP Audio driver for Samsung Exynos4
 *
 * Copyright (c) 2010 Samsung Electronics
 * http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#if defined(CONFIG_S5P_MEM_CMA)
#include <linux/cma.h>
#elif defined(CONFIG_S5P_MEM_BOOTMEM)
#include <mach/media.h>
#include <plat/media.h>
#endif

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <plat/cpu.h>

#include "../audss.h"
#include "../idma.h"
#include "../srp-types.h"
#include "srp_reg.h"
#include "srp_fw.h"
#include "srp_ioctl.h"

#define _USE_START_WITH_BUF0_		/* Start SRP after IBUF0 fill */
#define _USE_AUTO_PAUSE_		/* Pause SRP, when two IBUF are empty */
#define _USE_PCM_DUMP_			/* PCM snoop for Android */
#define _USE_EOS_TIMEOUT_		/* Timeout option for EOS */

#if (defined CONFIG_ARCH_EXYNOS4)
	#define _IMEM_MAX_		(64 * 1024)	/* 64KBytes */
	#define _DMEM_MAX_		(128 * 1024)	/* 128KBytes */
	#define _IBUF_SIZE_		(128 * 1024)	/* 128KBytes in DRAM */
	#define _WBUF_SIZE_		(_IBUF_SIZE_ * 6)	/* in DRAM */
	#define _SBUF_SIZE_		(_IBUF_SIZE_ * 6)	/* in DRAM */
	#define _FWBUF_SIZE_		(4 * 1024)	/* 4KBytes in F/W */

#ifdef CONFIG_CPU_EXYNOS4210				/* Orion */
	#define _IRAM_SIZE_		(128 * 1024)	/* Total size in IRAM */
	#define _IMEM_OFFSET_		(0x00400)	/* 1KB offset ok */

	#define _OBUF_SIZE_AB_		(0x04000)	/* 9Frames */
	#define _OBUF0_OFFSET_AB_	(0x10000)
	#define _OBUF1_OFFSET_AB_	(_OBUF0_OFFSET_AB_ + _OBUF_SIZE_AB_)
	#define _OBUF_SIZE_C_		(4608 * 2)	/* 2Frames */
	#define _OBUF0_OFFSET_C_	(0x19800)
	#define _OBUF1_OFFSET_C_	(0x1CC00)
#else							/* Pegasus */
	#define _IRAM_SIZE_		(256 * 1024)	/* Total size in IRAM */
	#define _IMEM_OFFSET_		(0x20400)	/* Dummy, not used */

	#define _OBUF_SIZE_AB_		(0x04000)	/* 9Frames */
	#define _OBUF0_OFFSET_AB_	(0x31000)
	#define _OBUF1_OFFSET_AB_	(_OBUF_SIZE_AB_ + _OBUF0_OFFSET_AB_)
	#define _OBUF_SIZE_C_		(4608 * 2)	/* 2Frames */
	#define _OBUF0_OFFSET_C_	(0x39800)
	#define _OBUF1_OFFSET_C_	(0x3CC00)
#endif

	#define _VLIW_SIZE_		(128 * 1024)	/* 128KBytes */
	#define _DATA_SIZE_		(128 * 1024)	/* 128KBytes */
	#define _CGA_SIZE_		(36 * 1024)	/* 36KBytes */

	#define _PCM_DUMP_SIZE_		(4 * 1024)	/* 4KBytes */
	#define _AM_FILTER_SIZE_	(4 * 1024)	/* 4KBytes */

	/* Reserved memory on DRAM */
	#define _BASE_MEM_SIZE_		(CONFIG_AUDIO_SAMSUNG_MEMSIZE_SRP << 10)
	#define _VLIW_SIZE_MAX_		(256 * 1024)
	#define _CGA_SIZE_MAX_		(64 * 1024)
	#define _DATA_SIZE_MAX_		(128 * 1024)
#else
	#error CONFIG_ARCH not found
#endif

#define _BITSTREAM_SIZE_MAX_		(0x7FFFFFFF)

#ifdef _USE_FW_ENDIAN_CONVERT_
#define ENDIAN_CHK_CONV(VAL)		\
	(((VAL >> 24) & 0x000000FF) |	\
	((VAL >> 8) & 0x0000FF00) |	\
	((VAL << 8) & 0x00FF0000) |	\
	((VAL << 24) & 0xFF000000))
#else
#define ENDIAN_CHK_CONV(VAL)	(VAL)
#endif

#define SRP_DEV_MINOR		(250)
#define SRP_CTRL_DEV_MINOR	(251)

#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
#define s5pdbg(x...) printk(KERN_INFO "SRP: " x)
#else
#define s5pdbg(x...)
#endif

struct srp_info {
	void __iomem  *iram;
	void __iomem  *sram;
	void __iomem  *commbox;

	void __iomem  *iram_imem;
	void __iomem  *obuf0;
	void __iomem  *obuf1;

	void __iomem  *dmem;
	void __iomem  *icache;
	void __iomem  *cmem;
	void __iomem  *special;

	int ibuf_next;				/* IBUF index for next write */
	int ibuf_empty[2];			/* Empty flag of IBUF0/1 */
#ifdef _USE_START_WITH_BUF0_
	int ibuf_req_skip;			/* IBUF req can be skipped */
#endif
	unsigned long ibuf_fill_size[2];	/* Fill size */
	unsigned long ibuf_size;		/* IBUF size byte */
	unsigned long frame_size;		/* 1 frame size = 1152 or 576 */
	unsigned long frame_count;		/* Decoded frame counter */
	unsigned long frame_count_base;
	unsigned long channel;			/* Mono = 1, Stereo = 2 */

	int is_opened;				/* Running status of SRP */
	int is_running;				/* Open status of SRP */
	int block_mode;				/* Block Mode */
	int decoding_started;			/* Decoding started flag */
	int wait_for_eos;			/* Wait for End-Of-Stream */
	int stop_after_eos;			/* State for Stop-after-EOS */
	int pause_request;			/* Pause request from ioctl */
	int auto_paused;			/* Pause by IBUF underrun */
	int restart_after_resume;		/* Restart req. after resume */
#ifdef _USE_EOS_TIMEOUT_
	int timeout_eos_enabled;		/* Timeout switch during EOS */
	struct timeval timeout_eos;		/* Timeout at acctual EOS */
	unsigned long timeout_read_size;	/* Last READ_BITSTREAM_SIZE */
#endif
	unsigned long error_info;		/* Error Information */
	unsigned long gain;			/* Gain */
	unsigned long gain_subl;		/* Gain sub left */
	unsigned long gain_subr;		/* Gain sub right */
	int dram_in_use;			/* DRAM is accessed by SRP */
	int op_mode;				/* Operation mode: typeA/B/C */
	int early_suspend_entered;		/* Early suspend state */
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif

	unsigned char *fw_code_vliw;		/* VLIW */
	unsigned char *fw_code_cga;		/* CGA */
	unsigned char *fw_code_cga_sa;		/* CGA for SoundAlive */
	unsigned char *fw_data;			/* DATA */
	int alt_fw_loaded;                      /* Alt-Firmware State */

	dma_addr_t fw_mem_base;			/* Base memory for FW */
	unsigned long fw_mem_base_pa;		/* Physical address of base */
	unsigned long fw_code_vliw_pa;		/* Physical address of VLIW */
	unsigned long fw_code_cga_pa;		/* Physical address of CGA */
	unsigned long fw_code_cga_sa_pa;	/* Physical address of CGA_SA */
	unsigned long fw_data_pa;		/* Physical address of DATA */
	unsigned long fw_code_vliw_size;	/* Size of VLIW */
	unsigned long fw_code_cga_size;		/* Size of CGA */
	unsigned long fw_code_cga_sa_size;	/* Size of CGA for SoundAlive */
	unsigned long fw_data_size;		/* Size of DATA */

	unsigned char *ibuf0;			/* IBUF0 in DRAM */
	unsigned char *ibuf1;			/* IBUF1 in DRAM */
	unsigned long ibuf0_pa;			/* Physical address */
	unsigned long ibuf1_pa;			/* Physical address */
	unsigned long obuf0_pa;			/* Physical address */
	unsigned long obuf1_pa;			/* Physical address */
	unsigned long obuf_size;		/* Current OBUF size */
	unsigned long vliw_rp;			/* Current VLIW address */
	unsigned char *wbuf;			/* WBUF in DRAM */
	unsigned long wbuf_pa;			/* Physical address */
	unsigned long wbuf_pos;			/* Write pointer */
	unsigned long wbuf_fill_size;		/* Total size by user write() */
	unsigned char *sbuf;			/* SBUF in DRAM */
	unsigned long sbuf_pa;			/* Physical address */
	unsigned long sbuf_fill_size;		/* Fill size */

	unsigned char *pcm_dump;		/* PCM dump buffer in DRAM */
	unsigned long pcm_dump_pa;		/* Physical address */
	unsigned long pcm_dump_enabled;		/* PCM dump switch */
	unsigned long pcm_dump_idle;		/* PCM dump count from SRP */
	int pcm_dump_cnt;			/* PCM dump open count */

	unsigned long effect_enabled;		/* Effect enable switch */
	unsigned long effect_def;		/* Effect definition */
	unsigned long effect_eq_user;		/* Effect EQ user */
	unsigned long effect_speaker;		/* Effect Speaker mode */

	unsigned long force_mono_enabled;	/* Force MONO enable switch */

	unsigned char *am_filter;		/* AM filter setting in DRAM */
	unsigned long am_filter_pa;		/* Physical address */
	unsigned long am_filter_loaded;		/* AM filter switch */

	unsigned long sb_tablet_mode;		/* SB 0:Handphone, 1:Tablet */
	void	(*audss_clk_enable)(bool enable);
};

static struct srp_info srp;

static DEFINE_MUTEX(rp_mutex);

DECLARE_WAIT_QUEUE_HEAD(WaitQueue_Write);
DECLARE_WAIT_QUEUE_HEAD(WaitQueue_EOS);

#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
struct timeval time_irq, time_write;
static char rp_fw_name[4][16] = {
	"VLIW", "CGA", "CGA-SA", "DATA"
};

static char rp_op_level_str[][10] = {"LPA", "AFTR"};
#endif

int srp_get_status(int cmd)
{
	return (cmd == IS_RUNNING) ? srp.is_running : srp.is_opened;
}

int srp_get_op_level(void)
{
	int op_lvl;

	if (srp.is_running) {
#ifdef _USE_PCM_DUMP_
		if (srp.pcm_dump_enabled)
			op_lvl = 1;
		else
#endif
			op_lvl = srp.dram_in_use ? 1 : 0;
	} else {
		op_lvl = 0;
	}

#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
	s5pdbg("OP level [%s]\n", rp_op_level_str[op_lvl]);
#endif
	return op_lvl;
}
EXPORT_SYMBOL(srp_get_op_level);

static void srp_set_effect_apply(void)
{
	unsigned long arm_intr_code = readl(srp.commbox +
					SRP_ARM_INTERRUPT_CODE);

	writel(srp.effect_def | srp.effect_speaker,
		srp.commbox + SRP_EFFECT_DEF);
	writel(srp.effect_eq_user, srp.commbox + SRP_EQ_USER_DEF);

	arm_intr_code &= ~SRP_ARM_INTR_CODE_SA_ON;
	arm_intr_code |= srp.effect_enabled ? SRP_ARM_INTR_CODE_SA_ON : 0;
	writel(arm_intr_code, srp.commbox + SRP_ARM_INTERRUPT_CODE);
}

static void srp_effect_trigger(void)
{
	unsigned long arm_intr_code = readl(srp.commbox +
					SRP_ARM_INTERRUPT_CODE);

	writel(srp.effect_def | srp.effect_speaker,
		srp.commbox + SRP_EFFECT_DEF);
	writel(srp.effect_eq_user, srp.commbox + SRP_EQ_USER_DEF);

	arm_intr_code &= ~SRP_ARM_INTR_CODE_SA_ON;
	arm_intr_code |= srp.effect_enabled ? SRP_ARM_INTR_CODE_SA_ON : 0;
	writel(arm_intr_code, srp.commbox + SRP_ARM_INTERRUPT_CODE);
}

static void srp_set_gain_apply(void)
{
	writel((srp.gain * srp.gain_subl) / 100,
			srp.commbox + SRP_GAIN_CTRL_FACTOR_L);
	writel((srp.gain * srp.gain_subr) / 100,
			srp.commbox + SRP_GAIN_CTRL_FACTOR_R);
}

static void srp_set_force_mono_apply(void)
{
	unsigned long arm_intr_code = readl(srp.commbox +
					SRP_ARM_INTERRUPT_CODE);

	arm_intr_code &= ~SRP_ARM_INTR_CODE_FORCE_MONO;
	arm_intr_code |= srp.force_mono_enabled
			? SRP_ARM_INTR_CODE_FORCE_MONO : 0;
	writel(arm_intr_code, srp.commbox + SRP_ARM_INTERRUPT_CODE);
}

static void srp_commbox_init(void)
{
	unsigned int reg = 0x0;

	s5pdbg("Commbox initialized\n");

	writel(reg, srp.commbox + SRP_INTERRUPT);
	writel(reg, srp.commbox + SRP_ARM_INTERRUPT_CODE);
	writel(SRP_STALL, srp.commbox + SRP_PENDING);

	writel(reg, srp.commbox + SRP_FRAME_INDEX);
	writel(reg, srp.commbox + SRP_EFFECT_DEF);
	writel(reg, srp.commbox + SRP_EQ_USER_DEF);

	writel(srp.ibuf0_pa,
			srp.commbox + SRP_BITSTREAM_BUFF_DRAM_ADDR0);
	writel(srp.ibuf1_pa,
			srp.commbox + SRP_BITSTREAM_BUFF_DRAM_ADDR1);

	/* Output PCM control : 16bit */
	writel(SRP_CFGR_OUTPUT_PCM_16BIT, srp.commbox + SRP_CFGR);
	/* Clear VLIW address */
	writel(reg, srp.special + 0x007C);

	writel(srp.fw_data_pa, srp.commbox + SRP_DATA_START_ADDR);
	writel(srp.pcm_dump_pa, srp.commbox + SRP_PCM_DUMP_ADDR);
	writel(srp.fw_code_cga_pa, srp.commbox + SRP_CONF_START_ADDR);
	writel(srp.fw_code_cga_sa_pa, srp.commbox + SRP_LOAD_CGA_SA_ADDR);
	writel(srp.am_filter_pa, srp.commbox + SRP_INFORMATION);

	srp_set_effect_apply();
	srp_set_gain_apply();
	srp_set_force_mono_apply();
}

static void srp_commbox_deinit(void)
{
	unsigned int reg = 0x0;

	s5pdbg("Commbox deinitialized\n");

	/* Reset value */
	writel(SRP_STALL, srp.commbox + SRP_PENDING);
	writel(reg, srp.commbox + SRP_INTERRUPT);

	 /* Clear VLIW address */
	writel(reg, srp.special + 0x007C);
}

static void srp_fw_download(void)
{
	unsigned long n;
	unsigned long *pval;
	unsigned int reg = 0x0;

#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
	struct timeval begin, end;

	do_gettimeofday(&begin);
#endif

	/* Fill ICACHE with first 64KB area : ARM access I$ */
	pval = (unsigned long *)srp.fw_code_vliw;
	for (n = 0; n < 0x10000; n += 4, pval++)
		writel(ENDIAN_CHK_CONV(*pval), srp.icache + n);

	reg = readl(srp.commbox + SRP_CFGR);
	reg |= (SRP_CFGR_BOOT_INST_INT_CC |	/* Fetchs instruction from I$ */
		SRP_CFGR_USE_ICACHE_MEM |       /* SRP can access I$ */
		SRP_CFGR_FLOW_CTRL_ON);		/* Flow control on */

	/* SRP use i2s interrupt by wake-up source */
	if (srp.op_mode == SRP_ARM_INTR_CODE_ULP_BTYPE)
		reg |= SRP_CFGR_USE_I2S_INTR;
	else
		reg |= SRP_CFGR_NOTUSE_I2S_INTR;

	writel(reg, srp.commbox + SRP_CFGR);

	/* Copy VLIW code to iRAM (Operation mode C) */
	if (srp.op_mode == SRP_ARM_INTR_CODE_ULP_CTYPE) {
		pval = (unsigned long *)srp.fw_code_vliw;
		for (n = 0; n < srp.fw_code_vliw_size; n += 4, pval++)
			writel(ENDIAN_CHK_CONV(*pval), srp.iram_imem + n);
	}

#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
	do_gettimeofday(&end);

	s5pdbg("Firmware Download Time : %lu.%06lu seconds.\n",
		end.tv_sec - begin.tv_sec, end.tv_usec - begin.tv_usec);
#endif
}

static void srp_set_default_fw(void)
{
	/* Initialize Commbox & default parameters */
	srp_commbox_init();

	/* Download default Firmware */
	srp_fw_download();
}

static void srp_flush_ibuf(void)
{
	memset(srp.ibuf0, 0xFF, srp.ibuf_size);
	memset(srp.ibuf1, 0xFF, srp.ibuf_size);

	/* Next IBUF is IBUF0 */
	srp.ibuf_next = 0;
	srp.ibuf_empty[0] = 1;
	srp.ibuf_empty[1] = 1;
	srp.ibuf_fill_size[0] = 0;
	srp.ibuf_fill_size[1] = 0;
#ifdef _USE_START_WITH_BUF0_
	srp.ibuf_req_skip = 1;
#endif
	srp.wbuf_pos = 0;
	srp.wbuf_fill_size = 0;
}

static void srp_flush_obuf(void)
{
	int n;

	if (srp.obuf_size) {
		for (n = 0; n < srp.obuf_size; n += 4) {
			writel(0, srp.obuf0 + n);
			writel(0, srp.obuf1 + n);
		}
	}
}

static void srp_reset_frame_counter(void)
{
	srp.frame_count = 0;
	srp.frame_count_base = 0;
}

static unsigned long srp_get_frame_counter(void)
{
	unsigned long val;

	val = readl(srp.commbox + SRP_FRAME_INDEX);
	srp.frame_count = srp.frame_count_base + val;

	return srp.frame_count;
}

static void srp_check_stream_info(void)
{
	if (!srp.channel) {
		srp.channel = readl(srp.commbox
				+ SRP_ARM_INTERRUPT_CODE);
		srp.channel >>= SRP_ARM_INTR_CODE_CHINF_SHIFT;
		srp.channel &= SRP_ARM_INTR_CODE_CHINF_MASK;
		if (srp.channel)
			s5pdbg("Channel = %lu\n", srp.channel);
	}

	if (!srp.frame_size) {
		switch (readl(srp.commbox
			+ SRP_ARM_INTERRUPT_CODE)
			& SRP_ARM_INTR_CODE_FRAME_MASK) {
		case SRP_ARM_INTR_CODE_FRAME_1152:
			srp.frame_size = 1152;
			break;
		case SRP_ARM_INTR_CODE_FRAME_1024:
			srp.frame_size = 1024;
			break;
		case SRP_ARM_INTR_CODE_FRAME_576:
			srp.frame_size = 576;
			break;
		case SRP_ARM_INTR_CODE_FRAME_384:
			srp.frame_size = 384;
			break;
		default:
			srp.frame_size = 0;
			break;
		}
		if (srp.frame_size)
			s5pdbg("Frame size = %lu\n", srp.frame_size);
	}
}

static void srp_reset(void)
{
	unsigned int reg = 0x0;

	wake_up_interruptible(&WaitQueue_Write);

	writel(SRP_STALL, srp.commbox + SRP_PENDING);

	/* Operation mode (A/B/C) & PCM dump & AM filter load & SB tablet mode */
	writel((srp.pcm_dump_enabled ? SRP_ARM_INTR_CODE_PCM_DUMP_ON : 0) |
		(srp.am_filter_loaded ? SRP_ARM_INTR_CODE_AM_FILTER_LOAD : 0) |
		(srp.sb_tablet_mode ? SRP_ARM_INTR_CODE_SB_TABLET : 0) |
		srp.op_mode, srp.commbox + SRP_ARM_INTERRUPT_CODE);

	writel(srp.vliw_rp, srp.commbox + SRP_CODE_START_ADDR);
	writel(srp.obuf0_pa, srp.commbox + SRP_PCM_BUFF0);
	writel(srp.obuf1_pa, srp.commbox + SRP_PCM_BUFF1);
	writel(srp.obuf_size >> 2, srp.commbox + SRP_PCM_BUFF_SIZE);

	writel(reg, srp.commbox + SRP_FRAME_INDEX);
	writel(reg, srp.commbox + SRP_READ_BITSTREAM_SIZE);
	writel(srp.ibuf_size, srp.commbox + SRP_BITSTREAM_BUFF_DRAM_SIZE);
	writel(_BITSTREAM_SIZE_MAX_, srp.commbox + SRP_BITSTREAM_SIZE);
	srp_set_effect_apply();
	srp_set_force_mono_apply();

	/* RESET */
	writel(reg, srp.commbox + SRP_CONT);
	writel(reg, srp.commbox + SRP_INTERRUPT);

	/* VLIW address should be set after rp reset */
	writel(srp.vliw_rp, srp.special + 0x007C);

	/* Clear Error Info */
	srp.error_info = 0;
	/* Store Total Count */
	srp.frame_count_base = srp.frame_count;
	srp.wait_for_eos = 0;
	srp.stop_after_eos = 0;
	srp.pause_request = 0;
	srp.auto_paused = 0;
	srp.decoding_started = 0;
	srp.dram_in_use = 0;
	srp.pcm_dump_idle = 0;
#ifdef _USE_EOS_TIMEOUT_
	srp.timeout_eos_enabled = 0;
	srp.timeout_read_size = _BITSTREAM_SIZE_MAX_;
#endif
}

static void srp_pause(void)
{
	unsigned long arm_intr_code = readl(srp.commbox + SRP_ARM_INTERRUPT_CODE);

	arm_intr_code |= SRP_ARM_INTR_CODE_PAUSE_REQ;
	writel(arm_intr_code, srp.commbox + SRP_ARM_INTERRUPT_CODE);
}

static void srp_pause_request(void)
{
	int n;
	unsigned long arm_intr_code = readl(srp.commbox +
					SRP_ARM_INTERRUPT_CODE);

	s5pdbg("Pause requsted\n");
	if (!srp.is_running) {
		s5pdbg("Pause ignored\n");
		return;
	}

	arm_intr_code |= SRP_ARM_INTR_CODE_PAUSE_REQ;
	writel(arm_intr_code, srp.commbox + SRP_ARM_INTERRUPT_CODE);

	for (n = 0; n < 100; n++) {
		if (readl(srp.commbox + SRP_ARM_INTERRUPT_CODE) &
			SRP_ARM_INTR_CODE_PAUSE_STA)
			break;
		msleep_interruptible(10);
	}
	srp.is_running = 0;
	s5pdbg("Pause done\n");
}

static void srp_continue(void)
{
	unsigned long arm_intr_code = readl(srp.commbox +
					SRP_ARM_INTERRUPT_CODE);

	arm_intr_code &= ~(SRP_ARM_INTR_CODE_PAUSE_REQ |
				SRP_ARM_INTR_CODE_PAUSE_STA);
	writel(arm_intr_code, srp.commbox + SRP_ARM_INTERRUPT_CODE);
}

static void srp_stop(void)
{
	writel(SRP_STALL, srp.commbox + SRP_PENDING);
	idma_stop();
	srp_flush_obuf();
	srp.pause_request = 0;
}

#ifdef _USE_PCM_DUMP_
static void srp_set_pcm_dump(int on)
{
	unsigned long arm_intr_code;

	if (srp.pcm_dump_enabled != on) {
		s5pdbg("PCM Dump [%s]\n", on ? "ON" : "OFF");
		arm_intr_code = readl(srp.commbox + SRP_ARM_INTERRUPT_CODE);

		if (on)
			arm_intr_code |= SRP_ARM_INTR_CODE_PCM_DUMP_ON;
		else
			arm_intr_code &= ~SRP_ARM_INTR_CODE_PCM_DUMP_ON;

		srp.pcm_dump_enabled = on;
		writel(arm_intr_code, srp.commbox + SRP_ARM_INTERRUPT_CODE);

		/* Clear dump buffer */
		if (!srp.pcm_dump_enabled)
			memset(srp.pcm_dump, 0, _PCM_DUMP_SIZE_);
	}
}
#endif

static void srp_init_op_mode(void)
{
	if (soc_is_exynos4210())
		srp.op_mode = SRP_ARM_INTR_CODE_ULP_CTYPE;
	else
		srp.op_mode = SRP_ARM_INTR_CODE_ULP_BTYPE;

	if (srp.op_mode == SRP_ARM_INTR_CODE_ULP_CTYPE) {
		srp.vliw_rp   = SRP_IRAM_BASE + _IMEM_OFFSET_;
		srp.obuf0_pa  = SRP_IRAM_BASE + _OBUF0_OFFSET_C_;
		srp.obuf1_pa  = SRP_IRAM_BASE + _OBUF1_OFFSET_C_;
		srp.obuf0     = srp.iram  + _OBUF0_OFFSET_C_;
		srp.obuf1     = srp.iram  + _OBUF1_OFFSET_C_;
		srp.obuf_size = _OBUF_SIZE_C_;
	} else {
		srp.vliw_rp = srp.fw_code_vliw_pa;
		srp.obuf0_pa  = SRP_IRAM_BASE + _OBUF0_OFFSET_AB_;
		srp.obuf1_pa  = SRP_IRAM_BASE + _OBUF1_OFFSET_AB_;
		srp.obuf0     = srp.iram  + _OBUF0_OFFSET_AB_;
		srp.obuf1     = srp.iram  + _OBUF1_OFFSET_AB_;
		srp.obuf_size = _OBUF_SIZE_AB_;
	}
}

#ifdef _USE_EOS_TIMEOUT_
static void srp_setup_timeout_eos(void)
{
	unsigned long remaining_bytes;
	unsigned long remaining_msec;

	srp.timeout_eos_enabled = 1;
	srp.timeout_read_size = readl(srp.commbox + SRP_READ_BITSTREAM_SIZE);

	remaining_bytes = readl(srp.commbox + SRP_BITSTREAM_SIZE)
				- srp.timeout_read_size;

	if (remaining_bytes > (srp.ibuf_size * 3))
		remaining_bytes = srp.ibuf_size * 3;

	remaining_bytes += _FWBUF_SIZE_;

	/* 32kbps at worst */
	remaining_msec = remaining_bytes * 1000 / 4096;
	remaining_msec += (srp.obuf_size * 2) * 1000 / 44100 / 4;

	do_gettimeofday(&srp.timeout_eos);
	srp.timeout_eos.tv_sec += remaining_msec / 1000;
	srp.timeout_eos.tv_usec += (remaining_msec % 1000) * 1000;
	if (srp.timeout_eos.tv_usec >= 1000000) {
		srp.timeout_eos.tv_sec++;
		srp.timeout_eos.tv_usec -= 1000000;
	}
}

static int srp_is_timeout_eos(void)
{
	struct timeval time_now;

	do_gettimeofday(&time_now);
	if ((time_now.tv_sec > srp.timeout_eos.tv_sec) ||
		((time_now.tv_sec == srp.timeout_eos.tv_sec) &&
		(time_now.tv_usec > srp.timeout_eos.tv_usec))) {
		printk(KERN_INFO "EOS Timeout at %lu.%06lu seconds.\n",
			time_now.tv_sec, time_now.tv_usec);
		return 1;
	}

	return 0;
}
#endif

static void srp_fill_ibuf(void)
{
	unsigned long fill_size;

	if (!srp.wbuf_pos)		/* wbuf empty? */
		return;

	if (srp.wbuf_pos >= srp.ibuf_size) {
		fill_size = srp.ibuf_size;
		srp.wbuf_pos -= fill_size;
	} else {
		fill_size = srp.wbuf_pos;
		memset(&srp.wbuf[fill_size], 0xFF, srp.ibuf_size - fill_size);
		srp.wbuf_pos = 0;
	}

	if (srp.ibuf_next == 0) {
		memcpy(srp.sbuf, srp.ibuf0, srp.ibuf_size);
		memcpy(srp.ibuf0, srp.wbuf, srp.ibuf_size);
		s5pdbg("Fill IBUF0 (%lu)\n", fill_size);
		srp.ibuf_empty[0] = 0;
		srp.ibuf_next = 1;
		srp.sbuf_fill_size = srp.ibuf_fill_size[0];
		srp.ibuf_fill_size[0] = srp.ibuf_fill_size[1] + fill_size;
	} else {
		memcpy(srp.sbuf, srp.ibuf1, srp.ibuf_size);
		memcpy(srp.ibuf1, srp.wbuf, srp.ibuf_size);
		s5pdbg("Fill IBUF1 (%lu)\n", fill_size);
		srp.ibuf_empty[1] = 0;
		srp.ibuf_next = 0;
		srp.sbuf_fill_size = srp.ibuf_fill_size[1];
		srp.ibuf_fill_size[1] = srp.ibuf_fill_size[0] + fill_size;
	}

	if (srp.wbuf_pos)
		memcpy(srp.wbuf, &srp.wbuf[srp.ibuf_size], srp.wbuf_pos);
}

static void srp_set_stream_size(void)
{
	/* Leave stream size max, if data is available */
	if (srp.wbuf_pos)
		return;

	writel(srp.wbuf_fill_size, srp.commbox + SRP_BITSTREAM_SIZE);

#ifdef _USE_EOS_TIMEOUT_
	srp_setup_timeout_eos();
#endif
}

#ifdef _USE_POSTPROCESS_SKIP_TEST_
struct timeval time_open, time_release;
#endif
static int srp_open(struct inode *inode, struct file *file)
{
	mutex_lock(&rp_mutex);
	if (srp.is_opened) {
		s5pdbg("srp_open() - SRP is already opened.\n");
		mutex_unlock(&rp_mutex);
		return -1;
	}
#ifdef _USE_POSTPROCESS_SKIP_TEST_
	do_gettimeofday(&time_open);
#endif
	srp.is_opened = 1;
	mutex_unlock(&rp_mutex);

	srp.audss_clk_enable(true);

	if (!(file->f_flags & O_NONBLOCK)) {
		s5pdbg("srp_open() - Block Mode\n");
		srp.block_mode = 1;
	} else {
		s5pdbg("srp_open() - NonBlock Mode\n");
		srp.block_mode = 0;
	}

	srp.channel = 0;
	srp.frame_size = 0;
	srp_reset_frame_counter();
	srp_set_default_fw();

	return 0;
}

static int srp_release(struct inode *inode, struct file *file)
{
	s5pdbg("srp_release()\n");

	/* Still running? */
	if (srp.is_running) {
		s5pdbg("Stop (release)\n");
		srp_stop();
		srp.is_running = 0;
		srp.decoding_started = 0;
	}

	/* Reset commbox */
	srp_commbox_deinit();
	srp.is_opened = 0;

#ifdef _USE_POSTPROCESS_SKIP_TEST_
	do_gettimeofday(&time_release);
	printk(KERN_INFO "SRP: Usage period : %lu.%06lu seconds.\n",
		time_release.tv_sec - time_open.tv_sec,
		time_release.tv_usec - time_open.tv_usec);
#endif

	return 0;
}

static ssize_t srp_write(struct file *file, const char *buffer,
				size_t size, loff_t *pos)
{
	unsigned long frame_idx;

	s5pdbg("srp_write(%d bytes)\n", size);

	mutex_lock(&rp_mutex);
	if (srp.decoding_started &&
		(!srp.is_running || srp.auto_paused)) {
		s5pdbg("Resume SRP\n");
		srp_flush_obuf();
		srp_continue();
		srp.is_running = 1;
		srp.auto_paused = 0;
	}
	mutex_unlock(&rp_mutex);

	if (srp.wbuf_pos > srp.ibuf_size * 4) {
		printk(KERN_ERR "SRP: wbuf_pos is full (0x%08lX), frame(0x%08X)\n",
			srp.wbuf_pos, readl(srp.commbox + SRP_FRAME_INDEX));
		return 0;
	} else if (size > srp.ibuf_size) {
		printk(KERN_ERR "SRP: wr size error (0x%08X)\n", size);
		return -EFAULT;
	} else {
		if (copy_from_user(&srp.wbuf[srp.wbuf_pos], buffer, size))
			return -EFAULT;
	}

	srp.wbuf_pos += size;
	srp.wbuf_fill_size += size;
	if (srp.wbuf_pos < srp.ibuf_size) {
		frame_idx = readl(srp.commbox + SRP_FRAME_INDEX);
		while (!srp.early_suspend_entered &&
			srp.decoding_started && srp.is_running) {
			if (readl(srp.commbox + SRP_READ_BITSTREAM_SIZE)
				+ srp.ibuf_size * 2 >= srp.wbuf_fill_size)
				break;
			if (readl(srp.commbox + SRP_FRAME_INDEX)
						> frame_idx + 2)
				break;
			msleep_interruptible(2);
		}

		return size;
	}

	/* IBUF not available */
	if (!srp.ibuf_empty[srp.ibuf_next]) {
		if (file->f_flags & O_NONBLOCK)
			return -1;	/* return Error at NonBlock mode */

		/* Sleep until IBUF empty interrupt */
		s5pdbg("srp_write() enter to sleep until IBUF empty INT\n");
		interruptible_sleep_on_timeout(&WaitQueue_Write, HZ / 2);
		s5pdbg("srp_write() wake up\n");
		/* not ready? */
		if (!srp.ibuf_empty[srp.ibuf_next])
			return size;
	}

	mutex_lock(&rp_mutex);
	srp_fill_ibuf();

#ifndef _USE_START_WITH_BUF0_
	if (!srp.ibuf_empty[0] && !srp.ibuf_empty[1]) {
#endif
		if (!srp.decoding_started) {
			s5pdbg("Start SRP decoding!!\n");
			writel(SRP_RUN, srp.commbox + SRP_PENDING);
			srp.is_running = 1;
			srp.decoding_started = 1;
			srp.restart_after_resume = 0;
		}
#ifndef _USE_START_WITH_BUF0_
	}
#endif

#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
	do_gettimeofday(&time_write);
	s5pdbg("IRQ to write-func Time : %lu.%06lu seconds.\n",
		time_write.tv_sec - time_irq.tv_sec,
		time_write.tv_usec - time_irq.tv_usec);
#endif
	mutex_unlock(&rp_mutex);

	/* SRP Decoding Error occurred? */
	if (srp.error_info)
		return -1;

	return size;
}

static long srp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	long ret_val = 0;

	s5pdbg("srp_ioctl(cmd:: %08X)\n", cmd);

	mutex_lock(&rp_mutex);

	switch (cmd) {
	case SRP_INIT:
		val = arg;
		if ((val >= 4*1024) && (val <= _IBUF_SIZE_)) {
			s5pdbg("Init, IBUF size [%ld], OBUF size [%ld]\n",
				val, srp.obuf_size);
			srp.ibuf_size = val;
			srp_flush_ibuf();
			srp_reset();
			srp.is_running = 0;
		} else {
			s5pdbg("Init error, IBUF size [%ld]\n", val);
			ret_val = -1;
		}
		break;

	case SRP_DEINIT:
		s5pdbg("Deinit\n");
		writel(SRP_STALL, srp.commbox + SRP_PENDING);
		srp.is_running = 0;
		break;

	case SRP_PAUSE:
		s5pdbg("Pause\n");
#ifdef _USE_EOS_TIMEOUT_
		srp.timeout_eos_enabled = 0;
#endif
		srp_pause_request();
		break;

	case SRP_STOP:
		s5pdbg("Stop\n");
		srp_stop();
		srp.is_running = 0;
		srp.decoding_started = 0;
		break;

	case SRP_FLUSH:
		/* Do not change srp.is_running state during flush */
		s5pdbg("Flush\n");
		srp_stop();
		srp_flush_ibuf();
		srp_set_default_fw();
		srp_reset();
		break;

	case SRP_SEND_EOS:
		s5pdbg("Send EOS\n");
		/* No data? */
		if (srp.wbuf_fill_size == 0) {
			srp.stop_after_eos = 1;
		} else if (srp.wbuf_fill_size < srp.ibuf_size * 2) {
			srp_fill_ibuf();		/* Fill IBUF0 */
#ifdef _USE_START_WITH_BUF0_
			srp.ibuf_req_skip = 0;
#else
			if (srp.ibuf_empty[srp.ibuf_next])
				srp_fill_ibuf();	/* Fill IBUF1 */
#endif
			srp_set_stream_size();
			s5pdbg("Start SRP decoding!!\n");
			writel(SRP_RUN, srp.commbox + SRP_PENDING);
			srp.is_running = 1;
			srp.wait_for_eos = 1;
			srp.decoding_started = 1;
		} else if (srp.ibuf_empty[srp.ibuf_next]) {
			srp_fill_ibuf();		/* Last data */
			srp_set_stream_size();
			srp.wait_for_eos = 1;
		} else {
			srp.wait_for_eos = 1;
		}
#ifdef _USE_EOS_TIMEOUT_
		printk(KERN_INFO "S5P_RP: Send EOS with timeout\n");
		srp_setup_timeout_eos();
#endif
		break;

	case SRP_RESUME_EOS:
		s5pdbg("Resume after EOS pause\n");
		srp_flush_obuf();
		if (srp.restart_after_resume) {
			srp_fill_ibuf();		/* Fill IBUF0 */
#ifdef _USE_START_WITH_BUF0_
			srp.ibuf_req_skip = 0;
#else
			srp_fill_ibuf();		/* Fill IBUF1 */
#endif
			srp_set_stream_size();
			s5pdbg("Restart RP decoding!!\n");
			writel(SRP_RUN, srp.commbox + SRP_PENDING);
			srp.is_running = 1;
			srp.wait_for_eos = 1;
			srp.decoding_started = 1;
			srp.restart_after_resume = 0;
		} else {
			srp_continue();
			srp.is_running = 1;
			srp.auto_paused = 0;
		}
#ifdef _USE_EOS_TIMEOUT_
		srp_setup_timeout_eos();
#endif
		break;

	case SRP_STOP_EOS_STATE:
		val = srp.stop_after_eos;
#ifdef _USE_EOS_TIMEOUT_
		if (srp.wait_for_eos && srp.timeout_eos_enabled) {
			if (srp_is_timeout_eos()) {
				srp.stop_after_eos = 1;
				val = 1;
			} else if (readl(srp.commbox + SRP_READ_BITSTREAM_SIZE)
				!= srp.timeout_read_size) {
				/* update timeout */
				srp_setup_timeout_eos();
			}
		}
#endif
		s5pdbg("SRP Stop [%s]\n", val == 1 ? "ON" : "OFF");
		if (val) {
			printk(KERN_INFO "SRP: Stop at EOS [0x%08lX:0x%08X]\n",
			srp.wbuf_fill_size,
			readl(srp.commbox + SRP_READ_BITSTREAM_SIZE));
		}
		ret_val = copy_to_user((unsigned long *)arg,
			&val, sizeof(unsigned long));
		break;

	case SRP_PENDING_STATE:
		val = readl(srp.commbox + SRP_PENDING);
		s5pdbg("SRP Pending [%s]\n", val == 1 ? "ON" : "OFF");
		ret_val = copy_to_user((unsigned long *)arg,
			&val, sizeof(unsigned long));
		break;

	case SRP_ERROR_STATE:
		s5pdbg("Error Info [%08lX]\n", srp.error_info);
		ret_val = copy_to_user((unsigned long *)arg,
			&srp.error_info, sizeof(unsigned long));
		srp.error_info = 0;
		break;

	case SRP_DECODED_FRAME_NO:
		val = srp_get_frame_counter();
		s5pdbg("Decoded Frame No [%ld]\n", val);
		ret_val = copy_to_user((unsigned long *)arg,
			&val, sizeof(unsigned long));
		break;

	case SRP_DECODED_ONE_FRAME_SIZE:
		if (srp.frame_size) {
			s5pdbg("One Frame Size [%lu]\n", srp.frame_size);
			ret_val = copy_to_user((unsigned long *)arg,
				&srp.frame_size, sizeof(unsigned long));
		} else {
			s5pdbg("Frame not decoded yet...\n");
		}
		break;

	case SRP_DECODED_FRAME_SIZE:
		if (srp.frame_size) {
			val = srp_get_frame_counter() * srp.frame_size;
			s5pdbg("Decoded Frame Size [%lu]\n", val);
			ret_val = copy_to_user((unsigned long *)arg,
				&val, sizeof(unsigned long));
		} else {
			s5pdbg("Frame not decoded yet...\n");
		}
		break;

	case SRP_CHANNEL_COUNT:
		if (srp.channel) {
			s5pdbg("Channel Count [%lu]\n", srp.channel);
			ret_val = copy_to_user((unsigned long *)arg,
				&srp.channel, sizeof(unsigned long));
		}
		break;

	default:
		ret_val = -ENOIOCTLCMD;
		break;
	}

	mutex_unlock(&rp_mutex);

	return ret_val;
}

#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
static unsigned long elapsed_usec_old;
#endif
static irqreturn_t srp_irq(int irqno, void *dev_id)
{
	int wakeup_req = 0;
	int wakeupEOS_req = 0;
	int pendingoff_req = 0;
	unsigned long irq_code = readl(srp.commbox + SRP_INTERRUPT_CODE);
	unsigned long irq_info = readl(srp.commbox + SRP_INFORMATION);
	unsigned long irq_code_req;
#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
	unsigned long elapsed_usec;
#endif
	unsigned long read_bytes;

	read_bytes = readl(srp.commbox + SRP_READ_BITSTREAM_SIZE);

	s5pdbg("IRQ: Code [%08lX], Pending [%s], SPE [%08X], Decoded [%08lX]\n",
		irq_code, readl(srp.commbox + SRP_PENDING) ? "ON" : "OFF",
		readl(srp.special + 0x007C), read_bytes);
	irq_code &= SRP_INTR_CODE_MASK;
	irq_info &= SRP_INTR_INFO_MASK;

	if (irq_code & SRP_INTR_CODE_REQUEST) {
		irq_code_req = irq_code & SRP_INTR_CODE_REQUEST_MASK;
		switch (irq_code_req) {
		case SRP_INTR_CODE_NOTIFY_INFO:
			srp_check_stream_info();
			break;

		case SRP_INTR_CODE_IBUF_REQUEST:
		case SRP_INTR_CODE_IBUF_REQUEST_ULP:
			if (irq_code_req == SRP_INTR_CODE_IBUF_REQUEST_ULP)
				srp.dram_in_use = 0;
			else
				srp.dram_in_use = 1;

			srp_check_stream_info();
#ifdef _USE_START_WITH_BUF0_
			/* Ignoring first req */
			if (srp.ibuf_req_skip) {
				srp.ibuf_req_skip = 0;
				break;
			}
#endif
			if ((irq_code & SRP_INTR_CODE_IBUF_MASK) ==
				SRP_INTR_CODE_IBUF0_EMPTY)
				srp.ibuf_empty[0] = 1;
			else
				srp.ibuf_empty[1] = 1;

			if (srp.decoding_started) {
				if (srp.ibuf_empty[0] && srp.ibuf_empty[1]) {
					if (srp.wait_for_eos) {
						s5pdbg("Stop at EOS (buffer empty)\n");
						srp.stop_after_eos = 1;
						writel(SRP_INTR_CODE_POLLINGWAIT,
						srp.commbox + SRP_INTERRUPT_CODE);
						return IRQ_HANDLED;
#ifdef _USE_AUTO_PAUSE_
					} else if (srp.is_running) {
						s5pdbg("Auto-Pause\n");
						srp_pause();
						srp.auto_paused = 1;
#endif
					}
				} else {
					pendingoff_req = 1;
					if (srp.wait_for_eos && srp.wbuf_pos) {
						srp_fill_ibuf();
						srp_set_stream_size();
					}
				}
			}
#ifdef CONFIG_SND_SAMSUNG_RP_DEBUG
			do_gettimeofday(&time_irq);
			elapsed_usec = time_irq.tv_sec * 1000000 +
					time_irq.tv_usec;
			s5pdbg("IRQ: IBUF empty ------- Interval [%lu.%06lu]\n",
				(elapsed_usec - elapsed_usec_old) / 1000000,
				(elapsed_usec - elapsed_usec_old) % 1000000);
			elapsed_usec_old = elapsed_usec;
#endif
			if (srp.block_mode && !srp.stop_after_eos)
				wakeup_req = 1;
			break;

		case SRP_INTR_CODE_ULP:
			srp.dram_in_use = 0;
			srp_check_stream_info();
			break;

		case SRP_INTR_CODE_DRAM_REQUEST:
			if (srp.op_mode == SRP_ARM_INTR_CODE_ULP_CTYPE) {
				if (srp.block_mode && !srp.stop_after_eos)
					wakeup_req = 1;
			}
			srp.dram_in_use = 1;
			break;

		case SRP_INTR_CODE_PENDING_ULP:
			srp.dram_in_use = 0;
			srp_check_stream_info();
			break;

		default:
			break;
		}
	}

	if (irq_code & SRP_INTR_CODE_NOTIFY_OBUF) {
		if (!srp.obuf_size) {
			srp.obuf_size = readl(srp.commbox + SRP_PCM_BUFF_SIZE);
			srp.obuf0_pa = readl(srp.commbox + SRP_PCM_BUFF0);
			srp.obuf1_pa = readl(srp.commbox + SRP_PCM_BUFF1);
			srp.obuf0 = srp.sram + (srp.obuf0_pa & 0xffff);
			srp.obuf1 = srp.sram + (srp.obuf1_pa & 0xffff);

			s5pdbg("IRQ: OBUF0[PA:0x%lx], OBUF1[PA:0x%lx]\n",
				srp.obuf0_pa, srp.obuf1_pa);
			s5pdbg("IRQ: OBUF0[VA:0x%p], OBUF1[VA:0x%p]\n",
				srp.obuf0, srp.obuf1);
			s5pdbg("IRQ: OBUF[SIZE:%ld]\n", srp.obuf_size);
		}
	}

	if (irq_code & (SRP_INTR_CODE_PLAYDONE | SRP_INTR_CODE_ERROR)) {
		s5pdbg("IRQ: Stop at EOS\n");
		s5pdbg("Total decoded: %ld frames (SRP_read:%08X)\n",
			srp_get_frame_counter(),
			readl(srp.commbox + SRP_READ_BITSTREAM_SIZE));
		srp.stop_after_eos = 1;
		writel(SRP_INTR_CODE_POLLINGWAIT,
			srp.commbox + SRP_INTERRUPT_CODE);

		return IRQ_HANDLED;
	}

	if (irq_code & SRP_INTR_CODE_UART_OUTPUT) {
		printk(KERN_INFO "SRP: UART Code received [0x%08X]\n",
			readl(srp.commbox + SRP_UART_INFORMATION));
		pendingoff_req = 1;
	}

	writel(0x00000000, srp.commbox + SRP_INTERRUPT_CODE);
	writel(0x00000000, srp.commbox + SRP_INTERRUPT);

	if (pendingoff_req)
		writel(SRP_RUN, srp.commbox + SRP_PENDING);

	if (wakeup_req)
		wake_up_interruptible(&WaitQueue_Write);

	if (wakeupEOS_req)
		wake_up_interruptible(&WaitQueue_EOS);

	return IRQ_HANDLED;
}

static long srp_ctrl_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	long ret_val = 0;
	unsigned long val, index, size;

	switch (cmd) {
	case SRP_CTRL_SET_GAIN:
		s5pdbg("CTRL: Gain [0x%08lX]\n", arg);
		srp.gain = arg;

		/* Change gain immediately */
		if (srp.is_opened)
			srp_set_gain_apply();
		break;

	case SRP_CTRL_SET_GAIN_SUB_LR:
		srp.gain_subl = arg >> 16;
		if (srp.gain_subl > 100)
			srp.gain_subl = 100;

		srp.gain_subr = arg & 0xFFFF;
		if (srp.gain_subr > 100)
			srp.gain_subr = 100;

		s5pdbg("CTRL: Gain sub [L:%03ld, R:%03ld]\n",
			srp.gain_subl, srp.gain_subr);

		/* Change gain immediately */
		if (srp.is_opened)
			srp_set_gain_apply();
		break;

	case SRP_CTRL_GET_PCM_1KFRAME:
		s5pdbg("CTRL: Get PCM 1K Frame\n");
		ret_val = copy_to_user((unsigned long *)arg,
					srp.pcm_dump, _PCM_DUMP_SIZE_);
		break;

#ifdef _USE_PCM_DUMP_
	case SRP_CTRL_PCM_DUMP_OP:
		if (arg == 1 && srp.early_suspend_entered == 0) {
			srp.pcm_dump_cnt++;
			if (srp.pcm_dump_cnt == 1)
				srp_set_pcm_dump(1);
		} else {
			srp.pcm_dump_cnt--;
			if (srp.pcm_dump_cnt <= 0) {
				srp.pcm_dump_cnt = 0;
				srp_set_pcm_dump(0);
			}
		}
		break;
#endif

	case SRP_CTRL_EFFECT_ENABLE:
		arg &= 0x01;
		s5pdbg("CTRL: Effect switch %s\n", arg ? "ON" : "OFF");
		if (srp.effect_enabled != arg) {
			srp.effect_enabled = arg;
			if (srp.is_running)
				srp_effect_trigger();
			else if (srp.is_opened)
				srp_set_effect_apply();
		}
		break;

	case SRP_CTRL_EFFECT_DEF:
		s5pdbg("CTRL: Effect define\n");
		/* Mask Speaker mode */
		srp.effect_def = arg & 0xFFFFFFFE;
		if (srp.is_running) {
			writel(srp.effect_def | srp.effect_speaker,
				srp.commbox + SRP_EFFECT_DEF);
			s5pdbg("Effect [%s], EFFECT_DEF = 0x%08lX, EQ_USR = 0x%08lX\n",
				srp.effect_enabled ? "ON" : "OFF",
				srp.effect_def | srp.effect_speaker,
				srp.effect_eq_user);
		} else if (srp.is_opened) {
			srp_set_effect_apply();
		}
		break;

	case SRP_CTRL_EFFECT_EQ_USR:
		s5pdbg("CTRL: Effect EQ user\n");
		srp.effect_eq_user = arg;
		if (srp.is_running) {
			writel(srp.effect_eq_user,
				srp.commbox + SRP_EQ_USER_DEF);
		} else if (srp.is_opened) {
			srp_set_effect_apply();
		}
		break;

	case SRP_CTRL_EFFECT_SPEAKER:
		arg &= 0x01;
		s5pdbg("CTRL: Effect Speaker mode %s\n", arg ? "ON" : "OFF");
		if (srp.effect_speaker != arg) {
			srp.effect_speaker = arg;
			if (srp.is_running)
				srp_effect_trigger();
			else if (srp.is_opened)
				srp_set_effect_apply();
		}
		break;

	case SRP_CTRL_FORCE_MONO:
		arg &= 0x01;
		s5pdbg("CTRL: Force Mono mode %s\n", arg ? "ON" : "OFF");
		if (srp.force_mono_enabled != arg) {
			srp.force_mono_enabled = arg;
			if (srp.is_opened)
				srp_set_force_mono_apply();
		}
		break;

	case SRP_CTRL_AMFILTER_LOAD:
		srp.am_filter_loaded = 1;
		s5pdbg("CTRL: AM Filter Loading\n");
		ret_val = copy_from_user(srp.am_filter, (void *)arg, 60);
		break;

	case SRP_CTRL_SB_TABLET:
		srp.sb_tablet_mode = arg;
		s5pdbg("CTRL: SB Mode %s\n", arg ? "Tablet" : "Handphone");
		break;

	case SRP_CTRL_IS_OPENED:
		val = (unsigned long)srp.is_opened;
		s5pdbg("CTRL: SRP is [%s]\n",
			val == 1 ? "Opened" : "Not Opened");
		ret_val = copy_to_user((unsigned long *)arg,
					&val, sizeof(unsigned long));
		break;

	case SRP_CTRL_IS_RUNNING:
		val = (unsigned long)srp.is_running;
		s5pdbg("CTRL: SRP is [%s]\n", val == 1 ? "Running" : "Pending");
		ret_val = copy_to_user((unsigned long *)arg,
					&val, sizeof(unsigned long));
		break;

	case SRP_CTRL_GET_OP_LEVEL:
		val = (unsigned long)srp_get_op_level();
		s5pdbg("CTRL: SRP op-level [%s]\n", rp_op_level_str[val]);
		ret_val = copy_to_user((unsigned long *)arg,
					&val, sizeof(unsigned long));
		break;

	case SRP_CTRL_IS_PCM_DUMP:
		val = (unsigned long)srp.pcm_dump_enabled;
		ret_val = copy_to_user((unsigned long *)arg, &val,
				sizeof(unsigned long));
		break;

	case SRP_CTRL_IS_FORCE_MONO:
		val = (unsigned long)srp.force_mono_enabled;
		ret_val = copy_to_user((unsigned long *)arg,
					&val, sizeof(unsigned long));
		break;

	case SRP_CTRL_ALTFW_STATE:
		/* Alt-Firmware State */
		val = srp.alt_fw_loaded;
		s5pdbg("CTRL: Alt-Firmware %sLoaded\n", val ? "" : "Not ");
		ret_val = copy_to_user((unsigned long *)arg,
					&val, sizeof(unsigned long));
		break;

	/* Alt-Firmware Loading */
	case SRP_CTRL_ALTFW_LOAD:
		srp.alt_fw_loaded = 1;
		index = *(unsigned long *)(arg + (128 * 1024));
		size = *(unsigned long *)(arg + (129 * 1024));
		s5pdbg("CTRL: Alt-Firmware Loading: %s (%lu)\n",
			rp_fw_name[index], size);
		switch (index) {
		case SRP_FW_VLIW:
			srp.fw_code_vliw_size = size;
			ret_val = copy_from_user(srp.fw_code_vliw,
					(unsigned long *)arg, size);
			break;
		case SRP_FW_CGA:
			srp.fw_code_cga_size = size;
			ret_val = copy_from_user(srp.fw_code_cga,
					(unsigned long *)arg, size);
			break;
		case SRP_FW_CGA_SA:
			srp.fw_code_cga_sa_size = size;
			ret_val = copy_from_user(srp.fw_code_cga_sa,
					(unsigned long *)arg, size);
			break;
		case SRP_FW_DATA:
			srp.fw_data_size = size;
			ret_val = copy_from_user(srp.fw_data,
					(unsigned long *)arg, size);
			break;
		default:
			break;
		}
		break;

	default:
		ret_val = -ENOIOCTLCMD;
		break;
	}

	return ret_val;
}

static int srp_prepare_fw_buff(struct device *dev)
{
#if defined(CONFIG_S5P_MEM_CMA) || defined(CONFIG_S5P_MEM_BOOTMEM)
	unsigned long mem_paddr;

#ifdef CONFIG_S5P_MEM_CMA
	struct cma_info mem_info;
	int err;

	err = cma_info(&mem_info, dev, 0);
	if (err) {
		s5pdbg("Failed to get cma info\n");
		return -ENOMEM;
	}
	s5pdbg("cma_info\n\tstart_addr : 0x%08X\n\tend_addr   : 0x%08X"
		"\n\ttotal_size : 0x%08X\n\tfree_size  : 0x%08X\n",
		mem_info.lower_bound, mem_info.upper_bound,
		mem_info.total_size, mem_info.free_size);
	s5pdbg("Allocate memory %dbytes from CMA\n", _BASE_MEM_SIZE_);
	srp.fw_mem_base = cma_alloc(dev, "srp", _BASE_MEM_SIZE_, 0);
#else /* for CONFIG_S5P_MEM_BOOTMEM */
	s5pdbg("Allocate memory from BOOTMEM\n");
	srp.fw_mem_base = s5p_get_media_memory_bank(S5P_MDEV_SRP, 0);
#endif
	srp.fw_mem_base_pa = (unsigned long)srp.fw_mem_base;
	s5pdbg("fw_mem_base_pa = 0x%08lX\n", srp.fw_mem_base_pa);
	mem_paddr = srp.fw_mem_base_pa;

	if (IS_ERR_VALUE(srp.fw_mem_base_pa))
		return -ENOMEM;

	srp.fw_code_vliw_pa = mem_paddr;
	srp.fw_code_vliw = phys_to_virt(srp.fw_code_vliw_pa);
	mem_paddr += _VLIW_SIZE_MAX_;

	srp.fw_code_cga_pa = mem_paddr;
	srp.fw_code_cga = phys_to_virt(srp.fw_code_cga_pa);
	mem_paddr += _CGA_SIZE_MAX_;
	srp.fw_code_cga_sa_pa = mem_paddr;
	srp.fw_code_cga_sa = phys_to_virt(srp.fw_code_cga_sa_pa);
	mem_paddr += _CGA_SIZE_MAX_;

	srp.fw_data_pa = mem_paddr;
	srp.fw_data = phys_to_virt(srp.fw_data_pa);
	mem_paddr += _DATA_SIZE_MAX_;
#else	/* No CMA or BOOTMEM? */

	srp.fw_code_vliw = dma_alloc_writecombine(0, _VLIW_SIZE_,
			   (dma_addr_t *)&srp.fw_code_vliw_pa,
			   GFP_KERNEL);
	srp.fw_code_cga = dma_alloc_writecombine(0, _CGA_SIZE_,
			   (dma_addr_t *)&srp.fw_code_cga_pa,
			   GFP_KERNEL);
	srp.fw_code_cga_sa = dma_alloc_writecombine(0, _CGA_SIZE_,
			   (dma_addr_t *)&srp.fw_code_cga_sa_pa,
			   GFP_KERNEL);
	srp.fw_data = dma_alloc_writecombine(0, _DATA_SIZE_,
			   (dma_addr_t *)&srp.fw_data_pa,
			   GFP_KERNEL);
#endif

	srp.fw_code_vliw_size = rp_fw_vliw_len;
	srp.fw_code_cga_size = rp_fw_cga_len;
	srp.fw_code_cga_sa_size = rp_fw_cga_sa_len;
	srp.fw_data_size = rp_fw_data_len;

#ifdef _USE_IBUF_ON_IRAM_
	srp.ibuf0 = srp.iram_imem + rp_fw_vliw_len;
	srp.ibuf1 = srp.ibuf0 + _IBUF_SIZE_;
	srp.ibuf0_pa = SRP_IRAM_BASE + _IMEM_OFFSET_ + rp_fw_vliw_len;
	srp.ibuf1_pa = srp.ibuf0_pa + _IBUF_SIZE_;

	if ((_IMEM_OFFSET_ + rp_fw_vliw_len)
			> (_IRAM_SIZE_ - (_IBUF_SIZE_ * 2))) {
		s5pdbg(KERN_ERR "Cannot set ibuf address in iram\n");
		return -ENOMEM;
	}
#else
	srp.ibuf0 = dma_alloc_writecombine(0, _IBUF_SIZE_,
			   (dma_addr_t *)&srp.ibuf0_pa, GFP_KERNEL);
	srp.ibuf1 = dma_alloc_writecombine(0, _IBUF_SIZE_,
			   (dma_addr_t *)&srp.ibuf1_pa, GFP_KERNEL);
#endif
	srp.wbuf = dma_alloc_writecombine(0, _WBUF_SIZE_,
			   (dma_addr_t *)&srp.wbuf_pa, GFP_KERNEL);
	srp.sbuf = dma_alloc_writecombine(0, _SBUF_SIZE_,
			   (dma_addr_t *)&srp.sbuf_pa, GFP_KERNEL);
	srp.pcm_dump = dma_alloc_writecombine(0, _PCM_DUMP_SIZE_,
			   (dma_addr_t *)&srp.pcm_dump_pa, GFP_KERNEL);
	srp.am_filter = dma_alloc_writecombine(0, _AM_FILTER_SIZE_,
			   (dma_addr_t *)&srp.am_filter_pa, GFP_KERNEL);

	srp.fw_code_vliw_size = rp_fw_vliw_len;
	srp.fw_code_cga_size = rp_fw_cga_len;
	srp.fw_code_cga_sa_size = rp_fw_cga_sa_len;
	srp.fw_data_size = rp_fw_data_len;

	s5pdbg("VLIW,       VA = 0x%08lX, PA = 0x%08lX\n",
		(unsigned long)srp.fw_code_vliw,
		srp.fw_code_vliw_pa);
	s5pdbg("CGA,        VA = 0x%08lX, PA = 0x%08lX\n",
		(unsigned long)srp.fw_code_cga,
		srp.fw_code_cga_pa);
	s5pdbg("CGA_SA,     VA = 0x%08lX, PA = 0x%08lX\n",
		(unsigned long)srp.fw_code_cga_sa,
		srp.fw_code_cga_sa_pa);
	s5pdbg("DATA,       VA = 0x%08lX, PA = 0x%08lX\n",
		(unsigned long)srp.fw_data,
		srp.fw_data_pa);
	s5pdbg("DRAM IBUF0, VA = 0x%08lX, PA = 0x%08lX\n",
		(unsigned long)srp.ibuf0,
		srp.ibuf0_pa);
	s5pdbg("DRAM IBUF1, VA = 0x%08lX, PA = 0x%08lX\n",
		(unsigned long)srp.ibuf1,
		srp.ibuf1_pa);
	s5pdbg("PCM DUMP,   VA = 0x%08lX, PA = 0x%08lX\n",
		(unsigned long)srp.pcm_dump,
		srp.pcm_dump_pa);
	s5pdbg("AM FILTER,  VA = 0x%08lX, PA = 0x%08lX\n",
		(unsigned long)srp.am_filter, srp.am_filter_pa);

	/* Clear Firmware memory & IBUF */
	memset(srp.fw_code_vliw, 0, _VLIW_SIZE_);
	memset(srp.fw_code_cga, 0, _CGA_SIZE_);
	memset(srp.fw_code_cga_sa, 0, _CGA_SIZE_);
	memset(srp.fw_data, 0, _DATA_SIZE_);
	memset(srp.ibuf0, 0xFF, _IBUF_SIZE_);
	memset(srp.ibuf1, 0xFF, _IBUF_SIZE_);

	/* Copy Firmware */
	memcpy(srp.fw_code_vliw, rp_fw_vliw,
			srp.fw_code_vliw_size);
	memcpy(srp.fw_code_cga, rp_fw_cga,
			srp.fw_code_cga_size);
	memcpy(srp.fw_code_cga_sa, rp_fw_cga_sa,
			srp.fw_code_cga_sa_size);
	memcpy(srp.fw_data, rp_fw_data,
			srp.fw_data_size);

	/* Clear AM filter setting */
	memset(srp.am_filter, 0, _AM_FILTER_SIZE_);

	return 0;
}

static int srp_remove_fw_buff(void)
{
#if defined CONFIG_S5P_MEM_CMA
#elif defined CONFIG_S5P_MEM_BOOTMEM
#else	/* No CMA or BOOTMEM? */
	dma_free_writecombine(0, _VLIW_SIZE_, srp.fw_code_vliw,
						srp.fw_code_vliw_pa);
	dma_free_writecombine(0, _CGA_SIZE_, srp.fw_code_cga,
						srp.fw_code_cga_pa);
	dma_free_writecombine(0, _CGA_SIZE_, srp.fw_code_cga_sa,
						srp.fw_code_cga_sa_pa);
	dma_free_writecombine(0, _DATA_SIZE_, srp.fw_data,
						srp.fw_data_pa);
#endif
#ifdef _USE_IBUF_ON_IRAM_
	/* Nothing to do */
#else
	dma_free_writecombine(0, _IBUF_SIZE_, srp.ibuf0, srp.ibuf0_pa);
	dma_free_writecombine(0, _IBUF_SIZE_, srp.ibuf1, srp.ibuf1_pa);
#endif
	dma_free_writecombine(0, _WBUF_SIZE_, srp.wbuf, srp.wbuf_pa);
	dma_free_writecombine(0, _SBUF_SIZE_, srp.sbuf, srp.sbuf_pa);
	dma_free_writecombine(0, _AM_FILTER_SIZE_,
			srp.am_filter, srp.am_filter_pa);

	srp.fw_code_vliw_pa = 0;
	srp.fw_code_cga_pa = 0;
	srp.fw_code_cga_sa_pa = 0;
	srp.fw_data_pa = 0;
	srp.ibuf0_pa = 0;
	srp.ibuf1_pa = 0;
	srp.wbuf_pa = 0;
	srp.sbuf_pa = 0;
	srp.am_filter_pa = 0;

	return 0;
}

static const struct file_operations srp_fops = {
	.owner		= THIS_MODULE,
	.write		= srp_write,
	.unlocked_ioctl	= srp_ioctl,
	.open		= srp_open,
	.release	= srp_release,
};

static struct miscdevice srp_miscdev = {
	.minor		= SRP_DEV_MINOR,
	.name		= "srp",
	.fops		= &srp_fops,
};

static const struct file_operations srp_ctrl_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= srp_ctrl_ioctl,
};

static struct miscdevice srp_ctrl_miscdev = {
	.minor		= SRP_CTRL_DEV_MINOR,
	.name		= "srp_ctrl",
	.fops		= &srp_ctrl_fops,
};

#ifdef CONFIG_HAS_EARLYSUSPEND
void srp_early_suspend(struct early_suspend *h)
{
	s5pdbg("early_suspend\n");

	srp.early_suspend_entered = 1;
	if (srp.is_opened) {
		if (srp.pcm_dump_cnt > 0)
			srp_set_pcm_dump(0);
	}
}

void srp_late_resume(struct early_suspend *h)
{
	s5pdbg("late_resume\n");

	srp.early_suspend_entered = 0;

	if (srp.is_opened) {
		if (srp.pcm_dump_cnt > 0)
			srp_set_pcm_dump(1);
	}
}
#endif

/*
 * The functions for inserting/removing us as a module.
 */
static int __init srp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	srp.iram = ioremap(SRP_IRAM_BASE, _IRAM_SIZE_);
	if (srp.iram == NULL) {
		printk(KERN_ERR "SRP: ioremap error for iram space\n");
		ret = -ENOMEM;
		return ret;
	}

	srp.sram = ioremap(SRP_SRAM_BASE, 0x40000);
	if (srp.sram == NULL) {
		printk(KERN_ERR "SRP: ioremap error for sram space\n");
		ret = -ENOMEM;
		goto err1;
	}

	srp.commbox = ioremap(SRP_COMMBOX_BASE, 0x0200);
	if (srp.commbox == NULL) {
		printk(KERN_ERR "SRP: ioremap error for sram space\n");
		ret = -ENOMEM;
		goto err2;
	}

	/* Hidden register */
	srp.special = ioremap(0x030F0000, 0x0100);
	if (srp.special == NULL) {
		printk(KERN_ERR "SRP: ioremap error for special register\n");
		ret = -ENOMEM;
		goto err3;
	}

	/* VLIW iRAM offset */
	srp.iram_imem = srp.iram + _IMEM_OFFSET_;
	srp.dmem = srp.sram + SRP_DMEM;
	srp.icache = srp.sram + SRP_ICACHE;
	srp.cmem = srp.sram + SRP_CMEM;

	ret = request_irq(IRQ_AUDIO_SS, srp_irq, 0, "samsung-rp", pdev);
	if (ret < 0) {
		printk(KERN_ERR "SRP: Fail to claim SRP(AUDIO_SS) irq\n");
		goto err4;
	}

	srp.early_suspend_entered = 0;
#ifdef CONFIG_HAS_EARLYSUSPEND
	srp.early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	srp.early_suspend.suspend = srp_early_suspend;
	srp.early_suspend.resume = srp_late_resume;
	register_early_suspend(&srp.early_suspend);
#endif

	/* Allocate Firmware buffer */
	ret = srp_prepare_fw_buff(dev);
	if (ret < 0) {
		printk(KERN_ERR "SRP: Fail to allocate memory\n");
		goto err5;
	}

	/* Information for I2S driver */
	srp.is_opened = 0;
	srp.is_running = 0;
	/* Default C type (iRAM instruction)*/
	srp_init_op_mode();
	/* Set Default Gain to 1.0 */
	srp.gain = 1<<24;
	srp.gain_subl = 100;
	srp.gain_subr = 100;

	/* Operation level for idle framework */
	srp.dram_in_use = 0;

	srp.restart_after_resume = 0;

	/* PCM dump mode */
	srp.pcm_dump_enabled = 0;
	srp.pcm_dump_cnt = 0;

	/* Set Sound Alive Off */
	srp.effect_def = 0;
	srp.effect_eq_user = 0;
	srp.effect_speaker = 1;

	/* Disable force mono */
	srp.force_mono_enabled = 0;

	/* Disable AM filter load */
	srp.am_filter_loaded = 0;

	/* Set SB Handphone mode (0:Handphone, 1:Tablet)*/
	srp.sb_tablet_mode = 0;

	/* Clear alternate Firmware */
	srp.alt_fw_loaded = 0;

	/* Clock control of Audio Subsystem */
	srp.audss_clk_enable = audss_clk_enable;

	ret = misc_register(&srp_miscdev);
	if (ret) {
		printk(KERN_ERR "SRP: Cannot register miscdev on minor=%d\n",
			SRP_DEV_MINOR);
		goto err6;
	}

	ret = misc_register(&srp_ctrl_miscdev);
	if (ret) {
		printk(KERN_ERR "SRP: Cannot register miscdev on minor=%d\n",
			SRP_CTRL_DEV_MINOR);
		goto err7;
	}

	printk(KERN_INFO "SRP: Driver successfully probed\n");

	return 0;

err7:
	misc_deregister(&srp_miscdev);
err6:
	srp_remove_fw_buff();
err5:
	free_irq(IRQ_AUDIO_SS, pdev);
err4:
	iounmap(srp.special);
err3:
	iounmap(srp.commbox);
err2:
	iounmap(srp.sram);
err1:
	iounmap(srp.iram);

	return ret;
}

static int srp_remove(struct platform_device *pdev)
{
	s5pdbg("srp_remove() called !\n");

	free_irq(IRQ_AUDIO_SS, pdev);
	srp_remove_fw_buff();

	misc_deregister(&srp_miscdev);
	misc_deregister(&srp_ctrl_miscdev);

	iounmap(srp.special);
	iounmap(srp.commbox);
	iounmap(srp.sram);
	iounmap(srp.iram);

	return 0;
}

#ifdef CONFIG_PM

static void srp_sbuf_fill(unsigned char *buf, unsigned long buf_size)
{
	memcpy(&srp.sbuf[srp.sbuf_fill_size], buf, buf_size);
	srp.sbuf_fill_size += buf_size;

	s5pdbg("SBUF Fill (0x%08lX) Total = 0x%08lX\n",
		buf_size, srp.sbuf_fill_size);
}

static void srp_sbuf_fill_ibuf_frac(int ibuf_index)
{
	unsigned long frac;

	if (!srp.ibuf_fill_size[ibuf_index])		/* empty? */
		return;

	frac = srp.ibuf_fill_size[ibuf_index] % srp.ibuf_size;
	if (ibuf_index) {
		s5pdbg("Backup IBUF1\n");
		srp_sbuf_fill(srp.ibuf1, frac ? frac : srp.ibuf_size);
	} else {
		s5pdbg("Backup IBUF0\n");
		srp_sbuf_fill(srp.ibuf0, frac ? frac : srp.ibuf_size);
	}
}

static void srp_sbuf_fill_ibuf_skip_rp(int ibuf_index,
					unsigned long rp_frac)
{
	unsigned long len;

	len = srp.ibuf_size - rp_frac;
	if (ibuf_index) {
		s5pdbg("Backup IBUF1\n");
		srp_sbuf_fill(&srp.ibuf1[rp_frac], len);
	} else {
		s5pdbg("Backup IBUF0\n");
		srp_sbuf_fill(&srp.ibuf0[rp_frac], len);
	}
}

static void srp_sbuf_fill_ibuf_frac_skip_rp(int ibuf_index,
					unsigned long rp_frac)
{
	unsigned long frac, len;

	frac = srp.ibuf_fill_size[ibuf_index] % srp.ibuf_size;
	len = frac ? frac - rp_frac : srp.ibuf_size - rp_frac;
	if (ibuf_index) {
		s5pdbg("Backup IBUF1\n");
		srp_sbuf_fill(&srp.ibuf1[rp_frac], len);
	} else {
		s5pdbg("Backup IBUF0\n");
		srp_sbuf_fill(&srp.ibuf0[rp_frac], len);
	}
}

static void srp_sbuf_fill_ibuf(void)
{
	unsigned long rp_read, rp_frac;

	rp_read = readl(srp.commbox + SRP_READ_BITSTREAM_SIZE);
	rp_frac = rp_read % srp.ibuf_size;

	if (rp_read < srp.sbuf_fill_size) {	/* SBUF */
		s5pdbg("Backup SBUF\n");
		srp.sbuf_fill_size = srp.ibuf_size - rp_frac;
		memcpy(srp.sbuf, &srp.sbuf[rp_frac], srp.sbuf_fill_size);

		if (srp.ibuf_fill_size[0] < srp.ibuf_fill_size[1]) {
			srp_sbuf_fill_ibuf_frac(0);
			srp_sbuf_fill_ibuf_frac(1);
		} else {
			srp_sbuf_fill_ibuf_frac(1);
			srp_sbuf_fill_ibuf_frac(0);
		}
	} else {
		srp.sbuf_fill_size = 0;	/* Clear sbuf */
		if (srp.ibuf_fill_size[0] < srp.ibuf_fill_size[1]) {
			if (rp_read < srp.ibuf_fill_size[0]) {
				srp_sbuf_fill_ibuf_skip_rp(0, rp_frac);
				srp_sbuf_fill_ibuf_frac(1);
			} else {		/* Skip IBUF0 */
				srp_sbuf_fill_ibuf_frac_skip_rp(1, rp_frac);
			}
		} else {
			if (rp_read < srp.ibuf_fill_size[1]) {
				srp_sbuf_fill_ibuf_skip_rp(1, rp_frac);
				srp_sbuf_fill_ibuf_frac(0);
			} else {		/* Skip IBUF1 */
				srp_sbuf_fill_ibuf_frac_skip_rp(0, rp_frac);
			}
		}
	}
}

static void srp_backup_sbuf(void)
{
	if (!srp.ibuf_fill_size[0] && !srp.ibuf_fill_size[1])
		srp.sbuf_fill_size = 0;	/* Clear sbuf */
	else
		srp_sbuf_fill_ibuf();

	if (srp.wbuf_pos) {
		s5pdbg("Backup WBUF\n");
		srp_sbuf_fill(srp.wbuf, srp.wbuf_pos);
	}

	s5pdbg("WBUF fill_size   = 0x%08lX\n", srp.wbuf_fill_size);
	s5pdbg("Backup size      = 0x%08lX\n", srp.sbuf_fill_size);

	if (srp.decoding_started) {
		s5pdbg("restart_after_resume required\n");
		srp.restart_after_resume = 1;
	}
}

static void srp_restore_sbuf(void)
{
	if (srp.sbuf_fill_size) {
		memcpy(srp.wbuf, srp.sbuf, srp.sbuf_fill_size);
		srp.wbuf_pos = srp.sbuf_fill_size;
		srp.wbuf_fill_size = srp.sbuf_fill_size;
	}
}

static int srp_suspend(struct platform_device *pdev, pm_message_t state)
{
	s5pdbg("suspend\n");

	if (srp.is_opened) {
		srp_backup_sbuf();
		srp.audss_clk_enable(false);
	}

	return 0;
}

static int srp_resume(struct platform_device *pdev)
{
	s5pdbg("resume\n");

	if (srp.is_opened) {
		srp.audss_clk_enable(true);
		srp_set_default_fw();

		srp_flush_ibuf();
		s5pdbg("Init, IBUF size [%ld]\n", srp.ibuf_size);
		srp_reset();
		srp.is_running = 0;

		srp_restore_sbuf();
	}

	return 0;
}
#else
#define srp_suspend NULL
#define srp_resume  NULL
#endif

static struct platform_driver srp_driver = {
	.probe		= srp_probe,
	.remove		= srp_remove,
	.suspend	= srp_suspend,
	.resume		= srp_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "samsung-rp",
	},
};

static char banner[] __initdata =
	KERN_INFO "Samsung SRP driver, (c) 2010 Samsung Electronics\n";

int __init srp_init(void)
{
	printk(banner);

	return platform_driver_register(&srp_driver);
}

void __exit srp_exit(void)
{
	platform_driver_unregister(&srp_driver);
}

module_init(srp_init);
module_exit(srp_exit);

MODULE_AUTHOR("Yeongman Seo <yman.seo@samsung.com>");
MODULE_DESCRIPTION("Samsung SRP driver");
MODULE_LICENSE("GPL");
