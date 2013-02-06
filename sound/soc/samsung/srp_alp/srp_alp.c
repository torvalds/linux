/* sound/soc/samsung/alp/srp_alp.c
 *
 * SRP Audio driver for Samsung Exynos
 *
 * Copyright (c) 2011 Samsung Electronics
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
#include <linux/mm.h>
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
#include <linux/cma.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/map.h>
#include <plat/cpu.h>

#include "../srp-types.h"
#include "srp_alp.h"
#include "srp_alp_reg.h"
#include "srp_alp_fw.h"
#include "srp_alp_ioctl.h"
#include "srp_alp_error.h"

#include "../idma.h"
#include "../audss.h"

static struct srp_info srp;
static DEFINE_MUTEX(srp_mutex);
static DECLARE_WAIT_QUEUE_HEAD(read_wq);
static DECLARE_WAIT_QUEUE_HEAD(decinfo_wq);

int srp_get_status(int cmd)
{
	return (cmd == IS_RUNNING) ? srp.is_running : srp.is_opened;
}

inline bool srp_fw_use_memcpy(void)
{
	return soc_is_exynos4210() ? false : true;
}

static void srp_obuf_elapsed(void)
{
	srp.obuf_ready = srp.obuf_ready ? 0 : 1;
	srp.obuf_next = srp.obuf_next ? 0 : 1;
}

static void srp_wait_for_pending(void)
{
	unsigned long deadline = jiffies + HZ / 10;

	do {
		/* Wait for SRP Pending */
		if (readl(srp.commbox + SRP_PENDING))
			break;

		msleep_interruptible(5);
	} while (time_before(jiffies, deadline));

	srp_info("Pending status[%s]\n",
			readl(srp.commbox + SRP_PENDING) ? "STALL" : "RUN");
}

static void srp_pending_ctrl(int ctrl)
{
	unsigned int srp_ctrl = readl(srp.commbox + SRP_PENDING);

	if (ctrl == srp_ctrl) {
		srp_info("Already set SRP pending control[%d]\n", ctrl);

		if (ctrl)
			return;
		else
			srp_wait_for_pending();
	}

	writel(ctrl, srp.commbox + SRP_PENDING);
	srp.is_pending = ctrl;

	srp_debug("Current SRP Status[%s]\n",
			readl(srp.commbox + SRP_PENDING) ? "STALL" : "RUN");
}

static void srp_request_intr_mode(int mode)
{
	unsigned long deadline;
	unsigned int pwr_mode = readl(srp.commbox + SRP_POWER_MODE);
	unsigned int intr_en = readl(srp.commbox + SRP_INTREN);
	unsigned int intr_msk = readl(srp.commbox + SRP_INTRMASK);
	unsigned int intr_src = readl(srp.commbox + SRP_INTRSRC);
	unsigned int intr_irq = readl(srp.commbox + SRP_INTRIRQ);
	unsigned int check_mode = 0;

	pwr_mode &= ~SRP_POWER_MODE_MASK;
	intr_en &= ~SRP_INTR_DI;
	intr_msk |= (SRP_ARM_INTR_MASK | SRP_DMA_INTR_MASK | SRP_TMR_INTR_MASK);
	intr_src &= ~(SRP_INTRSRC_MASK);
	intr_irq &= ~(SRP_INTRIRQ_MASK);

	switch (mode) {
	case SUSPEND:
		pwr_mode &= ~SRP_POWER_MODE_TRIGGER;
		check_mode = SRP_SUSPENED_CHECKED;
		break;
	case RESUME:
		pwr_mode |= SRP_POWER_MODE_TRIGGER;
		check_mode = 0;
		break;
	case RESET:
		pwr_mode |= SRP_SW_RESET_TRIGGER;
		check_mode = SRP_SW_RESET_DONE;
		break;
	default:
		srp_err("Not support request mode to srp\n");
		break;
	}

	intr_en |= SRP_INTR_EN;
	intr_msk &= ~SRP_ARM_INTR_MASK;
	intr_src |= SRP_ARM_INTR_SRC;

	if (soc_is_exynos5250() && (samsung_rev() >= EXYNOS5250_REV_1_0)) {
		intr_irq |= SRP_INTRIRQ_CONF;
		writel(intr_irq, srp.commbox + SRP_INTRIRQ);
	}

	writel(pwr_mode, srp.commbox + SRP_POWER_MODE);
	writel(intr_en, srp.commbox + SRP_INTREN);
	writel(intr_msk, srp.commbox + SRP_INTRMASK);
	writel(intr_src, srp.commbox + SRP_INTRSRC);

	srp_debug("PWR_MODE[0x%x], INTREN[0x%x], INTRMSK[0x%x], INTRSRC[0x%x]\n",
						readl(srp.commbox + SRP_POWER_MODE),
						readl(srp.commbox + SRP_INTREN),
						readl(srp.commbox + SRP_INTRMASK),
						readl(srp.commbox + SRP_INTRSRC));

	if (check_mode) {
		deadline = jiffies + (HZ / 50);
		srp_pending_ctrl(RUN);
		do {
			/* Waiting for completed suspended mode */
			if ((readl(srp.commbox + SRP_POWER_MODE)
					& check_mode))
				break;
		} while (time_before(jiffies, deadline));
		srp_pending_ctrl(STALL);

		/* Clear Suspend mode */
		pwr_mode = readl(srp.commbox + SRP_POWER_MODE);
		pwr_mode &= ~check_mode;
		writel(pwr_mode, srp.commbox + SRP_POWER_MODE);
	}
}

static void srp_check_stream_info(void)
{
	if (!srp.dec_info.channels) {
		srp.dec_info.channels = readl(srp.commbox
				+ SRP_ARM_INTERRUPT_CODE);
		srp.dec_info.channels >>= SRP_ARM_INTR_CODE_CHINF_SHIFT;
		srp.dec_info.channels &= SRP_ARM_INTR_CODE_CHINF_MASK;
	}

	if (!srp.dec_info.sample_rate) {
		srp.dec_info.sample_rate = readl(srp.commbox
				+ SRP_ARM_INTERRUPT_CODE);
		srp.dec_info.sample_rate >>= SRP_ARM_INTR_CODE_SRINF_SHIFT;
		srp.dec_info.sample_rate &= SRP_ARM_INTR_CODE_SRINF_MASK;
	}
}

static void srp_flush_ibuf(void)
{
	memset(srp.ibuf0, 0xFF, srp.ibuf_size);
	memset(srp.ibuf1, 0xFF, srp.ibuf_size);
}

static void srp_flush_obuf(void)
{
	void __iomem *obuf0, *obuf1;
	unsigned long n;

	if (soc_is_exynos5250()) {
		memset(srp.obuf0, 0, srp.obuf_size);
		memset(srp.obuf1, 0, srp.obuf_size);
	} else {
		obuf0 = srp.dmem + srp.obuf_offset;
		obuf1 = obuf0 + srp.obuf_size;

		for (n = 0; n < srp.obuf_size; n += 4) {
			writel(0, obuf0 + n);
			writel(0, obuf1 + n);
		}
	}
}
static void srp_commbox_init(void)
{
	unsigned int pwr_mode = readl(srp.commbox + SRP_POWER_MODE);
	unsigned int intr_en = readl(srp.commbox + SRP_INTREN);
	unsigned int intr_msk = readl(srp.commbox + SRP_INTRMASK);
	unsigned int intr_src = readl(srp.commbox + SRP_INTRSRC);

	unsigned int reg = 0;

	writel(reg, srp.commbox + SRP_FRAME_INDEX);
	writel(reg, srp.commbox + SRP_INTERRUPT);

	/* Support Mono Decoding */
	writel(SRP_ARM_INTR_CODE_SUPPORT_MONO, srp.commbox + SRP_ARM_INTERRUPT_CODE);

	/* Init Ibuf information */
	if (!soc_is_exynos5250()) {
		writel(srp.ibuf0_pa, srp.commbox + SRP_BITSTREAM_BUFF_DRAM_ADDR0);
		writel(srp.ibuf1_pa, srp.commbox + SRP_BITSTREAM_BUFF_DRAM_ADDR1);
		writel(srp.ibuf_size, srp.commbox + SRP_BITSTREAM_BUFF_DRAM_SIZE);
	}

	/* Output PCM control : 16bit */
	writel(SRP_CFGR_OUTPUT_PCM_16BIT, srp.commbox + SRP_CFGR);

	/* Bit stream size : Max */
	writel(BITSTREAM_SIZE_MAX, srp.commbox + SRP_BITSTREAM_SIZE);

	/* Init Read bitstream size */
	writel(reg, srp.commbox + SRP_READ_BITSTREAM_SIZE);

	/* Configure fw address */
	writel(srp.fw_info.vliw_pa, srp.commbox + SRP_CODE_START_ADDR);
	writel(srp.fw_info.cga_pa, srp.commbox + SRP_CONF_START_ADDR);
	writel(srp.fw_info.data_pa, srp.commbox + SRP_DATA_START_ADDR);

	/* Initialize Suspended mode */
	pwr_mode &= ~SRP_POWER_MODE_MASK;
	intr_en &= ~SRP_INTR_EN;
	intr_msk |= SRP_INTR_MASK;
	intr_src &= ~SRP_INTRSRC_MASK;

	writel(pwr_mode, srp.commbox + SRP_POWER_MODE);
	writel(intr_en, srp.commbox + SRP_INTREN);
	writel(intr_msk, srp.commbox + SRP_INTRMASK);
	writel(intr_src, srp.commbox + SRP_INTRSRC);
}

static void srp_commbox_deinit(void)
{
	unsigned int reg = 0;

	srp.audss_clk_enable(true);

	srp_wait_for_pending();
	srp.decoding_started = 0;
	writel(reg, srp.commbox + SRP_INTERRUPT);
}

static void srp_clr_fw_data(void)
{
	memset(srp.fw_info.data, 0, DMEM_SIZE);
	memcpy(srp.fw_info.data, srp_fw_data, sizeof(srp_fw_data));
}

static void srp_fw_download(void)
{
	void *pdmem;
	unsigned long *psrc;
	unsigned long dmemsz;
	unsigned long n;
	unsigned int reg = 0;

	/* Fill I-CACHE/DMEM */
	switch (srp_fw_use_memcpy()) {
	case false: /* Should be write by the 1word */
		psrc = (unsigned long *) srp.fw_info.vliw;
		for (n = 0; n < ICACHE_SIZE; n += 4, psrc++)
			writel(ENDIAN_CHK_CONV(*psrc), srp.icache + n);

		psrc = (unsigned long *) srp.fw_info.data;
		for (n = 0; n < DMEM_SIZE; n += 4, psrc++)
			writel(ENDIAN_CHK_CONV(*psrc), srp.dmem + n);
		break;
	case true: /* Support to memcpy */
		psrc = (unsigned long *) srp.fw_info.vliw;
		memcpy(srp.icache, psrc, ICACHE_SIZE);

		psrc = !soc_is_exynos5250() ? (unsigned long *) srp.fw_info.data
					    : (unsigned long *) (srp.fw_info.data
								   + DATA_OFFSET);
		pdmem = !soc_is_exynos5250() ? (void *) srp.dmem
					     : (void *) (srp.dmem + DATA_OFFSET);

		dmemsz = !soc_is_exynos5250() ? DMEM_SIZE : (DMEM_SIZE - DATA_OFFSET);
		memcpy(pdmem, psrc, dmemsz);
		break;
	}

	/* Fill CMEM : Should be write by the 1word(32bit) */
	psrc = (unsigned long *) srp.fw_info.cga;
	for (n = 0; n < CMEM_SIZE; n += 4, psrc++)
		writel(ENDIAN_CHK_CONV(*psrc), srp.cmem + n);

	reg = readl(srp.commbox + SRP_CFGR);
	reg |= (SRP_CFGR_BOOT_INST_INT_CC |	/* Fetchs instruction from I$ */
		SRP_CFGR_USE_ICACHE_MEM	|	/* SRP can access I$ */
		SRP_CFGR_USE_I2S_INTR	|
		SRP_CFGR_FLOW_CTRL_OFF);

	writel(reg, srp.commbox + SRP_CFGR);
}

static void srp_set_default_fw(void)
{
	/* Initialize Commbox & default parameters */
	srp_commbox_init();

	srp_clr_fw_data();

	/* Download default Firmware */
	srp_fw_download();
}

static void srp_reset(void)
{
	unsigned int reg = 0;

	srp_debug("Reset\n");

	/* RESET */
	if (soc_is_exynos5250()) {
		if (!srp.first_init) {
			writel(reg, srp.commbox + SRP_CONT);
			srp.first_init = 1;
		} else {
			/* Request sw reset */
			srp_request_intr_mode(RESET);
		}
	} else
		writel(reg, srp.commbox + SRP_CONT);

	writel(reg, srp.commbox + SRP_INTERRUPT);

	/* Store Total Count */
	srp.decoding_started = 0;

	/* Next IBUF is IBUF0 */
	srp.ibuf_next = 0;
	srp.ibuf_empty[0] = 1;
	srp.ibuf_empty[1] = 1;

	srp.obuf_next = 1;
	srp.obuf_ready = 0;
	srp.obuf_fill_done[0] = 0;
	srp.obuf_fill_done[1] = 0;
	srp.obuf_copy_done[0] = 0;
	srp.obuf_copy_done[1] = 0;

	srp.wbuf_pos = 0;
	srp.wbuf_fill_size = 0;

	srp.set_bitstream_size = 0;
	srp.stop_after_eos = 0;
	srp.wait_for_eos = 0;
	srp.prepare_for_eos = 0;
	srp.play_done = 0;
	srp.pcm_size = 0;
}

static void srp_fill_ibuf(void)
{
	unsigned long fill_size = 0;

	if (!srp.wbuf_pos)
		return;

	if (srp.wbuf_pos >= srp.ibuf_size) {
		fill_size = srp.ibuf_size;
		srp.wbuf_pos -= fill_size;
	} else {
		if (srp.wait_for_eos) {
			fill_size = srp.wbuf_pos;
			memset(&srp.wbuf[fill_size], 0xFF,
				srp.ibuf_size - fill_size);
			srp.wbuf_pos = 0;
		}
	}

	if (srp.ibuf_next == 0) {
		memcpy(srp.ibuf0, srp.wbuf, srp.ibuf_size);
		srp_debug("Fill IBUF0 (%lu)\n", fill_size);
		srp.ibuf_empty[0] = 0;
		srp.ibuf_next = 1;
	} else {
		memcpy(srp.ibuf1, srp.wbuf, srp.ibuf_size);
		srp_debug("Fill IBUF1 (%lu)\n", fill_size);
		srp.ibuf_empty[1] = 0;
		srp.ibuf_next = 0;
	}

	if (srp.wbuf_pos)
		memcpy(srp.wbuf, &srp.wbuf[srp.ibuf_size], srp.wbuf_pos);
}

static ssize_t srp_write(struct file *file, const char *buffer,
					size_t size, loff_t *pos)
{
	unsigned long start_threshold = 0;
	ssize_t ret = 0;

	srp_debug("Write(%d bytes)\n", size);

	srp.audss_clk_enable(true);

	if (!srp.initialized) {
		srp_set_default_fw();
		srp_flush_ibuf();
		srp_flush_obuf();
		srp_reset();
		srp.initialized = true;
	}

	if (srp.obuf_fill_done[srp.obuf_ready]
		&& srp.obuf_copy_done[srp.obuf_ready]) {
		srp.obuf_fill_done[srp.obuf_ready] = 0;
		srp.obuf_copy_done[srp.obuf_ready] = 0;
		srp_obuf_elapsed();
	}

	if (srp.wbuf_pos + size > WBUF_SIZE) {
		srp_debug("Occured Ibuf Overflow!!\n");
		ret = SRP_ERROR_IBUF_OVERFLOW;
		goto exit_func;
	}

	if (copy_from_user(&srp.wbuf[srp.wbuf_pos], buffer, size)) {
		srp_err("Failed to copy_from_user!!\n");
		ret = -EFAULT;
		goto exit_func;
	}

	srp.wbuf_pos += size;
	srp.wbuf_fill_size += size;

	start_threshold = srp.decoding_started
			? srp.ibuf_size : START_THRESHOLD;

	if (srp.wbuf_pos < start_threshold) {
		ret = size;
		goto exit_func;
	}

	mutex_lock(&srp_mutex);
	if (!srp.decoding_started) {
		srp_fill_ibuf();
		srp_info("First Start decoding!!\n");
		srp_pending_ctrl(RUN);
		srp.decoding_started = 1;
	}
	mutex_unlock(&srp_mutex);

exit_func:
	return ret;
}

static ssize_t srp_read(struct file *file, char *buffer,
				size_t size, loff_t *pos)
{
	struct srp_buf_info *argp = (struct srp_buf_info *)buffer;
	unsigned char *mmapped_obuf0 = srp.obuf_info.addr;
	unsigned char *mmapped_obuf1 = srp.obuf_info.addr + srp.obuf_size;
	int ret = 0;

	srp_debug("Entered Get Obuf in PCM function\n");

	if (srp.prepare_for_eos) {
		srp.obuf_fill_done[srp.obuf_ready] = 0;
		srp_debug("Elapsed Obuf[%d] after Send EOS\n", srp.obuf_ready);
		if (srp.pm_resumed)
			srp.pm_suspended = false;

		srp_pending_ctrl(RUN);
		srp_obuf_elapsed();
	}

	if (srp.wait_for_eos)
		srp.prepare_for_eos = 1;

	if (srp.decoding_started) {
		if (srp.obuf_copy_done[srp.obuf_ready] && !srp.wait_for_eos) {
			srp_debug("Wrong ordering read() OBUF[%d] int!!!\n", srp.obuf_ready);
			srp.pcm_info.size = 0;
			return copy_to_user(argp, &srp.pcm_info, sizeof(struct srp_buf_info));
		}

		if (srp.obuf_fill_done[srp.obuf_ready]) {
			srp_debug("Already filled OBUF[%d]\n", srp.obuf_ready);
		} else {
			srp_debug("Waiting for filling OBUF[%d]\n", srp.obuf_ready);
			ret = wait_event_interruptible_timeout(read_wq,
				srp.obuf_fill_done[srp.obuf_ready], HZ / 2);
			if (!ret) {
				srp_err("Couldn't occurred OBUF[%d] int!!!\n", srp.obuf_ready);
				srp.pcm_info.size = 0;
				return copy_to_user(argp, &srp.pcm_info, sizeof(struct srp_buf_info));
			}
		}
	} else {
		srp_debug("not prepared not yet! OBUF[%d]\n", srp.obuf_ready);
		srp.pcm_info.size = 0;
		return copy_to_user(argp, &srp.pcm_info, sizeof(struct srp_buf_info));
	}

	srp.pcm_info.addr = srp.obuf_ready ? mmapped_obuf1 : mmapped_obuf0;
	srp.pcm_info.size = srp.pcm_size;
	srp.pcm_info.num = srp.obuf_info.num;

	if (srp.play_done && !srp.pcm_info.size) {
		srp_info("Stop EOS by play done\n");
		srp.pcm_info.size = 0;
		srp.stop_after_eos = 1;
	}

	ret = copy_to_user(argp, &srp.pcm_info, sizeof(struct srp_buf_info));
	srp_debug("Return OBUF Num[%d] fill size %d\n",
			srp.obuf_ready, srp.pcm_info.size);

	srp.obuf_copy_done[srp.obuf_ready] = 1;

	if (!srp.obuf_fill_done[srp.obuf_next] && !srp.wait_for_eos) {
		srp_debug("Decoding start for filling OBUF[%d]\n", srp.obuf_next);
		if (srp.pm_resumed)
			srp.pm_suspended = false;

		srp_pending_ctrl(RUN);
	}

	return ret;
}

static void srp_set_stream_size(void)
{
	/* Leave stream size max, if data is available */
	if (srp.wbuf_pos || srp.set_bitstream_size)
		return;

	srp.set_bitstream_size = srp.wbuf_fill_size;
	writel(srp.set_bitstream_size, srp.commbox + SRP_BITSTREAM_SIZE);

	srp_info("Set bitstream size = %ld\n", srp.set_bitstream_size);
}

static long srp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct srp_buf_info *argp = (struct srp_buf_info *)arg;
	unsigned long val = 0;
	long ret = 0;

	mutex_lock(&srp_mutex);

	switch (cmd) {
	case SRP_INIT:
		srp.initialized = false;
		srp_debug("SRP_INIT\n");
		break;

	case SRP_DEINIT:
		srp_debug("SRP DEINIT\n");
		srp_commbox_deinit();
		break;

	case SRP_GET_MMAP_SIZE:
		srp.obuf_info.mmapped_size = srp.obuf_size * srp.obuf_num + srp.obuf_offset;
		val = srp.obuf_info.mmapped_size;
		ret = copy_to_user((unsigned long *)arg,
					&val, sizeof(unsigned long));

		srp_debug("OBUF_MMAP_SIZE = %ld\n", val);
		break;

	case SRP_FLUSH:
		srp_debug("SRP_FLUSH\n");
		srp_commbox_deinit();
		srp_set_default_fw();
		srp_flush_ibuf();
		srp_flush_obuf();
		srp_reset();
		break;

	case SRP_GET_IBUF_INFO:
		srp.ibuf_info.addr = (void *) srp.wbuf;
		srp.ibuf_info.size = srp.ibuf_size * 2;
		srp.ibuf_info.num  = srp.ibuf_num;

		ret = copy_to_user(argp, &srp.ibuf_info,
						sizeof(struct srp_buf_info));
		break;

	case SRP_GET_OBUF_INFO:
		ret = copy_from_user(&srp.obuf_info, argp,
				sizeof(struct srp_buf_info));
		if (!ret) {
			srp.obuf_info.addr = srp.obuf_info.mmapped_addr
							+ srp.obuf_offset;
			srp.obuf_info.size = srp.obuf_size;
			srp.obuf_info.num = srp.obuf_num;
		}

		ret = copy_to_user(argp, &srp.obuf_info,
					sizeof(struct srp_buf_info));
		break;

	case SRP_SEND_EOS:
		srp_info("Send End-Of-Stream\n");
		if (srp.wbuf_fill_size == 0) {
			srp.stop_after_eos = 1;
		} else if (srp.wbuf_fill_size < srp.ibuf_size * 3) {
			srp_debug("%ld, smaller than ibuf_size * 3\n", srp.wbuf_fill_size);
			srp.wait_for_eos = 1;
			srp_fill_ibuf();
			srp_set_stream_size();
			srp_pending_ctrl(RUN);
			srp.decoding_started = 1;
		} else if (srp.wbuf_fill_size >= srp.ibuf_size * 3) {
			srp_debug("%ld Bigger than ibuf * 3!!\n", srp.wbuf_fill_size);
			srp.wait_for_eos = 1;
		}
		break;

	case SRP_STOP_EOS_STATE:
		val = srp.stop_after_eos;

		srp_debug("Stop [%s]\n", val == 1 ? "ON" : "OFF");
		if (val) {
			srp_info("Stop at EOS [0x%08lX:0x%08X]\n",
			srp.wbuf_pos,
			readl(srp.commbox + SRP_READ_BITSTREAM_SIZE));

			if (!soc_is_exynos5250())
				srp.decoding_started = 0;
		}
		val = copy_to_user((unsigned long *)arg,
			&val, sizeof(unsigned long));
		break;

	case SRP_GET_DEC_INFO:
		if (!srp.decoding_started) {
			srp.dec_info.sample_rate = 0;
			srp.dec_info.channels = 0;
		} else {
			if (srp.dec_info.sample_rate && srp.dec_info.channels) {
				srp_info("Already get dec info!\n");
			} else {
				ret = wait_event_interruptible_timeout(decinfo_wq,
						srp.dec_info.channels != 0, HZ / 2);
				if (!ret) {
					srp_err("Couldn't Get Decoding info!!!\n");
					ret = SRP_ERROR_GETINFO_FAIL;
				}
			}

			srp_info("SampleRate[%d], Channels[%d]\n",
					srp.dec_info.sample_rate,
					srp.dec_info.channels);
		}

		val = copy_to_user((struct srp_dec_info *)arg, &srp.dec_info,
						sizeof(struct srp_dec_info));
		break;
	}

	mutex_unlock(&srp_mutex);

	return ret;
}

static int srp_open(struct inode *inode, struct file *file)
{
	srp_info("Opened!\n");

	mutex_lock(&srp_mutex);
	if (srp.is_opened) {
		srp_err("Already opened.\n");
		mutex_unlock(&srp_mutex);
		return -ENXIO;
	}
	srp.is_opened = 1;
	mutex_unlock(&srp_mutex);

	if (!(file->f_flags & O_NONBLOCK))
		srp.block_mode = 1;
	else
		srp.block_mode = 0;

	srp.dec_info.channels = 0;
	srp.dec_info.sample_rate = 0;

	srp.pm_suspended = false;
	srp.pm_resumed = false;
	srp.initialized = false;

	return 0;
}

static int srp_release(struct inode *inode, struct file *file)
{
	srp_info("Released\n");

	mutex_lock(&srp_mutex);
	srp.is_opened = 0;
	mutex_unlock(&srp_mutex);

	return 0;
}

static int srp_mmap(struct file *filep, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned int pfn;

	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	pfn = __phys_to_pfn(srp.mmap_base);

	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
		srp_err("failed to mmap for Obuf\n");
		return -EAGAIN;
	}

	return 0;
}

static void srp_check_obuf_info(void)
{
	unsigned int buf0 = readl(srp.commbox + SRP_PCM_BUFF0);
	unsigned int buf1 = readl(srp.commbox + SRP_PCM_BUFF1);
	unsigned int size = readl(srp.commbox + SRP_PCM_BUFF_SIZE);

	if (srp.obuf0_pa != buf0)
		srp_err("Wrong PCM BUF0[0x%x], OBUF0[0x%x]\n",
						buf0, srp.obuf0_pa);
	if (srp.obuf1_pa != buf1)
		srp_err("Wrong PCM BUF1[0x%x], OBUF1[0x%x]\n",
						buf1, srp.obuf1_pa);
	if ((srp.obuf_size >> 2) != size)
		srp_err("Wrong OBUF SIZE[%d]\n", size);
}

static irqreturn_t srp_irq(int irqno, void *dev_id)
{
	unsigned int irq_code = readl(srp.commbox + SRP_INTERRUPT_CODE);
	unsigned int irq_code_req;
	unsigned int wakeup_read = 0;
	unsigned int wakeup_decinfo = 0;
	unsigned long i;

	srp_debug("IRQ: Code [0x%x], Pending [%s], CFGR [0x%x]", irq_code,
			readl(srp.commbox + SRP_PENDING) ? "STALL" : "RUN",
			readl(srp.commbox + SRP_CFGR));

	irq_code &= SRP_INTR_CODE_MASK;
	if (irq_code & SRP_INTR_CODE_REQUEST) {
		irq_code_req = irq_code & SRP_INTR_CODE_REQUEST_MASK;
		switch (irq_code_req) {
		case SRP_INTR_CODE_NOTIFY_INFO:
			srp_info("Notify SRP interrupt!\n");
			srp_check_stream_info();
			wakeup_decinfo = 1;
			break;

		case SRP_INTR_CODE_IBUF_REQUEST:
			if ((irq_code & SRP_INTR_CODE_IBUF_MASK)
				== SRP_INTR_CODE_IBUF0_EMPTY) {
				srp_debug("IBUF0 empty\n");
				srp.ibuf_empty[0] = 1;
			} else {
				srp_debug("IBUF1 empty\n");
				srp.ibuf_empty[1] = 1;
			}

			srp_fill_ibuf();
			if (srp.decoding_started) {
				if (srp.wait_for_eos && !srp.wbuf_pos)
					srp_set_stream_size();
			}
			break;

		case SRP_INTR_CODE_OBUF_FULL:
			if ((irq_code & SRP_INTR_CODE_OBUF_MASK)
				==  SRP_INTR_CODE_OBUF0_FULL) {
				srp_debug("OBUF0 FULL\n");
				srp.obuf_fill_done[0] = 1;
				/* For EVT0 : will be removed on EVT1 */
				if (soc_is_exynos5250() && (samsung_rev() < EXYNOS5250_REV_1_0)) {
					for (i = 0; i < srp.obuf_size; i += 4)
						memcpy(&srp.pcm_obuf0[i], &srp.obuf0[i], 0x4);
				}
			} else {
				srp_debug("OBUF1 FULL\n");
				srp.obuf_fill_done[1] = 1;
				if (soc_is_exynos5250() && (samsung_rev() < EXYNOS5250_REV_1_0)) {
					for (i = 0; i < srp.obuf_size; i += 4)
						memcpy(&srp.pcm_obuf1[i], &srp.obuf1[i], 0x4);
				}
			}

			wakeup_read = 1;
			srp.pcm_size = readl(srp.commbox + SRP_PCM_DUMP_ADDR);
			writel(0, srp.commbox + SRP_PCM_DUMP_ADDR);
			break;

		default:
			break;
		}
	}

	if (irq_code & SRP_INTR_CODE_NOTIFY_OBUF)
		srp_check_obuf_info();


	if ((irq_code & (SRP_INTR_CODE_PLAYDONE | SRP_INTR_CODE_ERROR))
				&& (irq_code & SRP_INTR_CODE_PLAYEND)) {
		srp_info("Play Done interrupt!!\n");
		srp.pcm_size = 0;
		srp.play_done = 1;

		srp.obuf_fill_done[0] = 1;
		srp.obuf_fill_done[1] = 1;
		wakeup_read = 1;
	}

	if (irq_code & SRP_INTR_CODE_UART_OUTPUT) {
		srp_debug("UART Code received [0x%08X]\n",
		readl(srp.commbox + SRP_UART_INFORMATION));
	}

	writel(0, srp.commbox + SRP_INTERRUPT_CODE);
	writel(0, srp.commbox + SRP_INTERRUPT);

	if (wakeup_read) {
		if (waitqueue_active(&read_wq))
			wake_up_interruptible(&read_wq);
	}

	if (wakeup_decinfo) {
		if (waitqueue_active(&decinfo_wq))
			wake_up_interruptible(&decinfo_wq);
	}

	srp_debug("IRQ Exited!\n");

	return IRQ_HANDLED;
}

static void srp_prepare_buff(struct device *dev)
{
	srp.ibuf_size = IBUF_SIZE;
	srp.obuf_size = OBUF_SIZE;
	srp.wbuf_size = WBUF_SIZE;
	srp.ibuf_offset = IBUF_OFFSET;
	srp.obuf_offset = OBUF_OFFSET;

	srp.ibuf0 = soc_is_exynos5250() ? srp.dmem + srp.ibuf_offset
					: srp.iram + srp.ibuf_offset;
	srp.ibuf1 = srp.ibuf0 + srp.ibuf_size;

	srp.obuf0 = srp.dmem + srp.obuf_offset;
	srp.obuf1 = srp.obuf0 + srp.obuf_size;

	/* For EVT0 : will be removed on EVT1 */
	if (soc_is_exynos5250() && (samsung_rev() < EXYNOS5250_REV_1_0)) {
		srp.pcm_obuf0 = dma_alloc_writecombine(dev, srp.obuf_size * 2,
						&srp.pcm_obuf_pa, GFP_KERNEL);
		srp.pcm_obuf1 = srp.pcm_obuf0 + srp.obuf_size;
		srp.obuf_offset = 0;
	}

	if (!srp.ibuf0_pa)
		srp.ibuf0_pa = SRP_IBUF_PHY_ADDR;

	if (!srp.obuf0_pa)
		srp.obuf0_pa = SRP_OBUF_PHY_ADDR;

	srp.ibuf1_pa = srp.ibuf0_pa + srp.ibuf_size;
	srp.obuf1_pa = srp.obuf0_pa + srp.obuf_size;

	srp.ibuf_num = IBUF_NUM;
	srp.obuf_num = OBUF_NUM;

	/* For EVT0 : will be removed on EVT1 */
	if (soc_is_exynos5250() && (samsung_rev() < EXYNOS5250_REV_1_0))
		srp.mmap_base = srp.pcm_obuf_pa;
	else
		srp.mmap_base = SRP_DMEM_BASE;

	srp_info("[VA]IBUF0[0x%p], [PA]IBUF0[0x%x]\n",
						srp.ibuf0, srp.ibuf0_pa);
	srp_info("[VA]IBUF1[0x%p], [PA]IBUF1[0x%x]\n",
						srp.ibuf1, srp.ibuf1_pa);
	srp_info("[VA]OBUF0[0x%p], [PA]OBUF0[0x%x]\n",
						srp.obuf0, srp.obuf0_pa);
	srp_info("[VA]OBUF1[0x%p], [PA]OBUF1[0x%x]\n",
						srp.obuf1, srp.obuf1_pa);
	srp_info("IBUF SIZE [%ld]Bytes, OBUF SIZE [%ld]Bytes\n",
						srp.ibuf_size, srp.obuf_size);
}

static int srp_prepare_fw_buff(struct device *dev)
{
#if defined(CONFIG_S5P_MEM_CMA)
	unsigned long mem_paddr;

	srp.fw_info.mem_base = cma_alloc(dev, "srp", BASE_MEM_SIZE, 0);
	if (IS_ERR_VALUE(srp.fw_info.mem_base)) {
		srp_err("Failed to cma alloc for srp\n");
		return -ENOMEM;
	}

	mem_paddr = srp.fw_info.mem_base;
	srp.fw_info.vliw_pa = mem_paddr;
	srp.fw_info.vliw = phys_to_virt(srp.fw_info.vliw_pa);
	mem_paddr += ICACHE_SIZE;

	srp.fw_info.cga_pa = mem_paddr;
	srp.fw_info.cga = phys_to_virt(srp.fw_info.cga_pa);
	mem_paddr += CMEM_SIZE;

	srp.fw_info.data_pa = mem_paddr;
	srp.fw_info.data = phys_to_virt(srp.fw_info.data_pa);
	mem_paddr += DMEM_SIZE;

	srp.wbuf = phys_to_virt(mem_paddr);
	mem_paddr += WBUF_SIZE;

	srp.sp_data.ibuf = phys_to_virt(mem_paddr);
	mem_paddr += IBUF_SIZE * 2;

	srp.sp_data.obuf = phys_to_virt(mem_paddr);
	mem_paddr += OBUF_SIZE * 2;

	srp.sp_data.commbox = phys_to_virt(mem_paddr);
	mem_paddr += COMMBOX_SIZE;
#else
	srp.fw_info.vliw = dma_alloc_writecombine(dev, ICACHE_SIZE,
				&srp.fw_info.vliw_pa, GFP_KERNEL);
	if (!srp.fw_info.vliw) {
		srp_err("Failed to alloc for vliw\n");
		return -ENOMEM;
	}

	srp.fw_info.cga = dma_alloc_writecombine(dev, CMEM_SIZE,
				&srp.fw_info.cga_pa, GFP_KERNEL);
	if (!srp.fw_info.cga) {
		srp_err("Failed to alloc for cga\n");
		return -ENOMEM;
	}

	srp.fw_info.data = dma_alloc_writecombine(dev, DMEM_SIZE,
					&srp.fw_info.data_pa, GFP_KERNEL);
	if (!srp.fw_info.data) {
		srp_err("Failed to alloc for data\n");
		return -ENOMEM;
	}

	srp.wbuf = kzalloc(WBUF_SIZE, GFP_KERNEL);
	if (!srp.wbuf) {
		srp_err("Failed to allocation for WBUF!\n");
		return -ENOMEM;
	}

	srp.sp_data.ibuf = kzalloc(IBUF_SIZE * 2, GFP_KERNEL);
	if (!srp.sp_data.ibuf) {
		srp_err("Failed to alloc ibuf for suspend/resume!\n");
		return -ENOMEM;
	}

	srp.sp_data.obuf = kzalloc(OBUF_SIZE * 2, GFP_KERNEL);
	if (!srp.sp_data.obuf) {
		srp_err("Failed to alloc obuf for suspend/resume!\n");
		return -ENOMEM;
	}

	srp.sp_data.commbox = kzalloc(COMMBOX_SIZE, GFP_KERNEL);
	if (!srp.sp_data.commbox) {
		srp_err("Failed to alloc commbox for suspend/resume\n");
		return -ENOMEM;
	}
#endif

	srp.fw_info.vliw_size = sizeof(srp_fw_vliw);
	srp.fw_info.cga_size = sizeof(srp_fw_cga);
	srp.fw_info.data_size = sizeof(srp_fw_data);

	memset(srp.fw_info.vliw, 0, ICACHE_SIZE);
	memset(srp.fw_info.cga, 0, CMEM_SIZE);
	memset(srp.fw_info.data, 0, DMEM_SIZE);

	memcpy(srp.fw_info.vliw, srp_fw_vliw, srp.fw_info.vliw_size);
	memcpy(srp.fw_info.cga, srp_fw_cga, srp.fw_info.cga_size);
	memcpy(srp.fw_info.data, srp_fw_data, srp.fw_info.data_size);

	srp_info("VLIW[%lu]Bytes\n", srp.fw_info.vliw_size);
	srp_info("CGA[%lu]Bytes\n", srp.fw_info.cga_size);
	srp_info("DATA[%lu]Bytes\n", srp.fw_info.data_size);

	return 0;
}

static int srp_remove_fw_buff(struct device *dev)
{
#if defined(CONFIG_S5P_MEM_CMA)
	cma_free(srp.fw_info.mem_base);
#else
	dma_free_writecombine(dev, ICACHE_SIZE, srp.fw_info.vliw,
					srp.fw_info.vliw_pa);
	dma_free_writecombine(dev, CMEM_SIZE, srp.fw_info.cga,
					srp.fw_info.cga_pa);
	dma_free_writecombine(dev, DMEM_SIZE, srp.fw_info.data,
					srp.fw_info.data_pa);
	kfree(srp.wbuf);
	kfree(srp.sp_data.ibuf);
	kfree(srp.sp_data.obuf);
	kfree(srp.sp_data.commbox);
#endif
	srp.fw_info.vliw = NULL;
	srp.fw_info.cga = NULL;
	srp.fw_info.data = NULL;

	srp.fw_info.vliw_pa = 0;
	srp.fw_info.cga_pa = 0;
	srp.fw_info.data_pa = 0;
	srp.ibuf0_pa = 0;
	srp.ibuf1_pa = 0;
	srp.obuf0_pa = 0;
	srp.obuf1_pa = 0;

	return 0;
}

static const struct file_operations srp_fops = {
	.owner		= THIS_MODULE,
	.open		= srp_open,
	.release	= srp_release,
	.read		= srp_read,
	.write		= srp_write,
	.unlocked_ioctl	= srp_ioctl,
	.mmap		= srp_mmap,
};

static struct miscdevice srp_miscdev = {
	.minor		= SRP_DEV_MINOR,
	.name		= "srp",
	.fops		= &srp_fops,
};

#ifdef CONFIG_PM
static int srp_suspend(struct platform_device *pdev, pm_message_t state)
{
	unsigned long i;

	srp_info("Suspend\n");

	srp.audss_clk_enable(true);

	if (srp.is_opened) {
		if (srp.decoding_started && !srp.pm_suspended) {

			/* IBUF/OBUF Save */
			if (soc_is_exynos5250() && (samsung_rev() < EXYNOS5250_REV_1_0)) {
				/* EVT0 : Work around code */
				for (i = 0; i < srp.ibuf_size * 2; i += 4)
					writel(readl(srp.ibuf0 + i), srp.sp_data.ibuf + i);

				for (i = 0; i < srp.obuf_size * 2; i += 4)
					writel(readl(srp.obuf0 + i), srp.sp_data.obuf + i);
			} else {
				memcpy(srp.sp_data.ibuf, srp.ibuf0, IBUF_SIZE * 2);
				memcpy(srp.sp_data.obuf, srp.obuf0, OBUF_SIZE * 2);
			}

			/* Request Suspend mode */
			srp_request_intr_mode(SUSPEND);

			if (soc_is_exynos5250() && (samsung_rev() < EXYNOS5250_REV_1_0)) {
				/* EVT0 : Work around code */
				for (i = DATA_OFFSET; i < DMEM_SIZE; i += 4)
					writel(readl(srp.dmem + i), srp.fw_info.data + i);
			} else
				memcpy(srp.fw_info.data, srp.dmem, DMEM_SIZE);

			memcpy(srp.sp_data.commbox, srp.commbox, COMMBOX_SIZE);
			srp.pm_suspended = true;
		}
	} else if (soc_is_exynos5250()) {
		/* Request Suspend mode */
		srp_request_intr_mode(SUSPEND);
	}

	srp.audss_clk_enable(false);

	return 0;
}

static int srp_resume(struct platform_device *pdev)
{
	srp_info("Resume\n");

	srp.audss_clk_enable(true);

	if (srp.is_opened) {
		if (!srp.decoding_started) {
			srp_set_default_fw();
			srp_flush_ibuf();
			srp_flush_obuf();
			srp_reset();
		} else if (srp.decoding_started && srp.pm_suspended) {
			srp_fw_download();

			memcpy(srp.commbox, srp.sp_data.commbox, COMMBOX_SIZE);
			memcpy(srp.ibuf0, srp.sp_data.ibuf, IBUF_SIZE * 2);
			memcpy(srp.obuf0, srp.sp_data.obuf, OBUF_SIZE * 2);

			/* RESET */
			writel(0x0, srp.commbox + SRP_CONT);
			srp_request_intr_mode(RESUME);

			srp.pm_resumed = true;
		}
	} else if (soc_is_exynos5250()) {
			srp_fw_download();
			/* RESET */
			writel(0x0, srp.commbox + SRP_CONT);
			srp_request_intr_mode(RESUME);
	}

	return 0;
}

#else
#define srp_suspend NULL
#define srp_resume  NULL
#endif

static __devinit int srp_probe(struct platform_device *pdev)
{
	int ret = 0;

	srp.iram = ioremap(SRP_IRAM_BASE, IRAM_SIZE);
	if (srp.iram == NULL) {
		srp_err("Failed to ioremap for sram area\n");
		ret = -ENOMEM;
		return ret;

	}

	srp.dmem = ioremap(SRP_DMEM_BASE, DMEM_SIZE);
	if (srp.dmem == NULL) {
		srp_err("Failed to ioremap for dmem\n");
		ret = -ENOMEM;
		goto err1;

	}

	srp.icache = ioremap(SRP_ICACHE_ADDR, ICACHE_SIZE);
	if (srp.icache == NULL) {
		srp_err("Failed to ioremap for icache\n");
		ret = -ENOMEM;
		goto err2;
	}

	srp.cmem = ioremap(SRP_CMEM_ADDR, CMEM_SIZE);
	if (srp.cmem == NULL) {
		srp_err("Failed to ioremap for cmem\n");
		ret = -ENOMEM;
		goto err3;
	}

	srp.commbox = ioremap(SRP_COMMBOX_BASE, COMMBOX_SIZE);
	if (srp.commbox == NULL) {
		srp_err("Failed to ioremap for audio subsystem\n");
		ret = -ENOMEM;
		goto err4;
	}

	ret = srp_prepare_fw_buff(&pdev->dev);
	if (ret) {
		srp_err("SRP: Can't prepare memory for srp\n");
		goto err5;
	}

	ret = request_irq(IRQ_AUDIO_SS, srp_irq, IRQF_DISABLED, "samsung-rp", pdev);
	if (ret < 0) {
		srp_err("SRP: Fail to claim SRP(AUDIO_SS) irq\n");
		goto err6;
	}

	ret = misc_register(&srp_miscdev);
	if (ret) {
		srp_err("SRP: Cannot register miscdev on minor=%d\n",
			SRP_DEV_MINOR);
		goto err7;
	}

	srp.first_init = 0;
	srp_prepare_buff(&pdev->dev);
	srp.audss_clk_enable = audss_clk_enable;

	return 0;

err7:
	free_irq(IRQ_AUDIO_SS, pdev);
err6:
	srp_remove_fw_buff(&pdev->dev);
err5:
	iounmap(srp.commbox);
err4:
	iounmap(srp.cmem);
err3:
	iounmap(srp.icache);
err2:
	iounmap(srp.dmem);
err1:
	iounmap(srp.iram);

	return ret;
}

static __devexit int srp_remove(struct platform_device *pdev)
{
	free_irq(IRQ_AUDIO_SS, pdev);
	srp_remove_fw_buff(&pdev->dev);

	misc_deregister(&srp_miscdev);

	iounmap(srp.commbox);
	iounmap(srp.icache);
	iounmap(srp.dmem);
	iounmap(srp.iram);

	return 0;
}

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
	KERN_INFO "Samsung SRP driver, (c)2011 Samsung Electronics\n";

static int __init srp_init(void)
{
	printk(banner);

	return platform_driver_register(&srp_driver);
}

static void __exit srp_exit(void)
{
	platform_driver_unregister(&srp_driver);
}

module_init(srp_init);
module_exit(srp_exit);

MODULE_AUTHOR("Yeongman Seo <yman.seo@samsung.com>");
MODULE_DESCRIPTION("Samsung SRP driver");
MODULE_LICENSE("GPL");
