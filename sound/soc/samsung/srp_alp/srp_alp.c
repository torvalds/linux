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
#include <linux/firmware.h>
#include <sound/soc.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/map.h>
#include <plat/cpu.h>
#include <plat/srp.h>

#include "srp_alp.h"
#include "srp_alp_reg.h"
#include "srp_alp_ioctl.h"
#include "srp_alp_error.h"

#define VLIW_NAME	"srp_vliw.bin"
#define CGA_NAME	"srp_cga.bin"
#define DATA_NAME	"srp_data.bin"

static struct srp_info srp;
static DEFINE_MUTEX(srp_mutex);
static DEFINE_SPINLOCK(lock);
static DEFINE_SPINLOCK(lock_intr);
static DECLARE_WAIT_QUEUE_HEAD(reset_wq);
static DECLARE_WAIT_QUEUE_HEAD(read_wq);
static DECLARE_WAIT_QUEUE_HEAD(decinfo_wq);
bool srp_fw_ready_done;

void srp_core_reset(void);
extern void i2s_enable(struct snd_soc_dai *dai);
extern void i2s_disable(struct snd_soc_dai *dai);

static int srp_check_sound_list(void)
{
	int idx, ok = 0;

	pr_info("ALSA device list:\n");
	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		if (snd_cards[idx] != NULL)
			ok++;
	}
	if (ok == 0) {
		pr_info("  No soundcards found.\n");
		return 0;
	}
	return ok;
}

void srp_pm_control(bool enable)
{
	if (!srp.pm_info) {
		srp_debug("Couldn't ready to control pm.\n");
		return;
	}

	if (enable)
		i2s_enable(srp.pm_info);
	else
		i2s_disable(srp.pm_info);
}

void srp_prepare_pm(void *info)
{
	srp.pm_info = info;
	srp.initialized = false;
}

unsigned int srp_get_idma_addr(void)
{
	struct exynos_srp_buf idma = srp.pdata->idma;
	srp.idma_addr = idma.base + idma.offset;
	return srp.idma_addr;
}

static void srp_obuf_elapsed(void)
{
	srp.obuf_ready = srp.obuf_ready ? 0 : 1;
	srp.obuf_next = srp.obuf_next ? 0 : 1;
}

void srp_wait_for_pending(void)
{
	unsigned long deadline = jiffies + HZ;

	do {
		/* Wait for SRP Pending */
		if (readl(srp.commbox + SRP_PENDING))
			break;

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
	unsigned int reset_type = srp.pdata->type;
	unsigned long deadline;
	u32 pwr_mode = readl(srp.commbox + SRP_POWER_MODE);
	u32 intr_en = readl(srp.commbox + SRP_INTREN);
	u32 intr_msk = readl(srp.commbox + SRP_INTRMASK);
	u32 intr_src = readl(srp.commbox + SRP_INTRSRC);
	u32 intr_irq;
	u32 check_mode = 0x0;

	pwr_mode &= ~SRP_POWER_MODE_MASK;
	intr_en &= ~SRP_INTR_DI;
	intr_msk |= (SRP_ARM_INTR_MASK | SRP_DMA_INTR_MASK | SRP_TMR_INTR_MASK);
	intr_src &= ~(SRP_INTRSRC_MASK);

	switch (mode) {
	case SUSPEND:
		srp_info("Request Suspend to SRP\n");
		pwr_mode &= ~SRP_POWER_MODE_TRIGGER;
		check_mode = SRP_SUSPEND_CHECKED;
		break;
	case RESUME:
		srp_info("Request Resume to SRP\n");
		pwr_mode |= SRP_POWER_MODE_TRIGGER;
		check_mode = 0x0;
		break;
	case SW_RESET:
		srp_info("Request Reset to SRP\n");
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

	if (reset_type == SRP_SW_RESET) {
		intr_irq = readl(srp.commbox + SRP_INTRIRQ);
		intr_irq &= ~(SRP_INTRIRQ_MASK);
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
		srp_pending_ctrl(RUN);
		deadline = jiffies + (HZ / 2);
		do {
			/* Waiting for completed suspend mode */
			if ((readl(srp.commbox + SRP_POWER_MODE) & check_mode)) {
				srp_info("Success! requested power[%s] mode!\n",
					mode == SUSPEND ? "SUSPEND" : "SW_RESET");
				break;
			}
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
	unsigned long size = srp.pdata->ibuf.size;

	memset(srp.ibuf0, 0xFF, size);
	memset(srp.ibuf1, 0xFF, size);
}

static void srp_flush_obuf(void)
{
	unsigned long size = srp.pdata->obuf.size;

	memset(srp.obuf0, 0, size);
	memset(srp.obuf1, 0, size);
}

static void srp_reset(void)
{
	unsigned int reset_type = srp.pdata->type;
	unsigned int reg = 0;

	srp_debug("Reset\n");

	if (reset_type == SRP_HW_RESET) {
		/* HW Reset */
		writel(reg, srp.commbox + SRP_CONT);
	} else if (reset_type == SRP_SW_RESET) {
		/* SW Reset */
		srp_request_intr_mode(SW_RESET);
	}

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

static void srp_commbox_init(void)
{
	unsigned long ibuf_size = srp.pdata->ibuf.size;
	unsigned int reset_type = srp.pdata->type;
	u32 pwr_mode = readl(srp.commbox + SRP_POWER_MODE);
	u32 intr_en = readl(srp.commbox + SRP_INTREN);
	u32 intr_msk = readl(srp.commbox + SRP_INTRMASK);
	u32 intr_src = readl(srp.commbox + SRP_INTRSRC);
	u32 intr_irq;
	u32 reg = 0x0;

	writel(reg, srp.commbox + SRP_FRAME_INDEX);
	writel(reg, srp.commbox + SRP_INTERRUPT);

	/* Support Mono Decoding */
	writel(SRP_ARM_INTR_CODE_SUPPORT_MONO, srp.commbox + SRP_ARM_INTERRUPT_CODE);

	if (reset_type == SRP_HW_RESET) {
		/* Init Ibuf information */
		writel(srp.ibuf0_pa, srp.commbox + SRP_BITSTREAM_BUFF_DRAM_ADDR0);
		writel(srp.ibuf1_pa, srp.commbox + SRP_BITSTREAM_BUFF_DRAM_ADDR1);
		writel(ibuf_size, srp.commbox + SRP_BITSTREAM_BUFF_DRAM_SIZE);
	}

	/* Output PCM control : 16bit */
	writel(SRP_CFGR_OUTPUT_PCM_16BIT, srp.commbox + SRP_CFGR);

	/* Bit stream size : Max */
	writel(BITSTREAM_SIZE_MAX, srp.commbox + SRP_BITSTREAM_SIZE);

	/* Init Read bitstream size */
	writel(reg, srp.commbox + SRP_READ_BITSTREAM_SIZE);

	if (reset_type == SRP_HW_RESET) {
		/* Configure fw address */
		writel(srp.fw_info.vliw_pa, srp.commbox + SRP_CODE_START_ADDR);
		writel(srp.fw_info.cga_pa, srp.commbox + SRP_CONF_START_ADDR);
		writel(srp.fw_info.data_pa, srp.commbox + SRP_DATA_START_ADDR);
	}

	if (reset_type == SRP_SW_RESET) {
		intr_irq = readl(srp.commbox + SRP_INTRIRQ);
		intr_irq &= ~(SRP_INTRIRQ_MASK);
		writel(intr_irq, srp.commbox + SRP_INTRIRQ);
	}

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

	srp_pm_control(true);
	srp_wait_for_pending();
	srp_pending_ctrl(STALL);

	srp.decoding_started = 0;
	writel(reg, srp.commbox + SRP_INTERRUPT);
}

static void srp_fw_download(void)
{
	unsigned long icache_size = srp.pdata->icache_size;
	unsigned long dmem_size = srp.pdata->dmem_size;
	unsigned long cmem_size = srp.pdata->cmem_size;
	unsigned long n;
	unsigned long *pval;
	unsigned int reg = 0;

	/* Fill ICACHE with first 64KB area : ARM access I$ */
	memcpy(srp.icache, srp.fw_info.vliw_va, icache_size);

	/* Fill DMEM */
	memcpy(srp.dmem + srp.data_offset, srp.fw_info.data_va,
	       dmem_size - srp.data_offset);

	/* Fill CMEM : Should be write by the 1word(32bit) */
	pval = (unsigned long *)srp.fw_info.cga_va;
	for (n = 0; n < cmem_size; n += 4, pval++)
		writel(ENDIAN_CHK_CONV(*pval), srp.cmem + n);

	reg = readl(srp.commbox + SRP_CFGR);
	reg |= (SRP_CFGR_BOOT_INST_INT_CC |	/* Fetchs instruction from I$ */
		SRP_CFGR_USE_ICACHE_MEM	|	/* SRP can access I$ */
		SRP_CFGR_USE_I2S_INTR	|
		SRP_CFGR_FLOW_CTRL_OFF);

	writel(reg, srp.commbox + SRP_CFGR);
}

static void srp_set_default_fw(void)
{
	const u8 *org_data = srp.fw_info.data->data;
	unsigned char *old_data = srp.fw_info.data_va;
	size_t size = srp.fw_info.data->size;

	/* Initialize Commbox & default parameters */
	srp_commbox_init();

	/* Init data firmware */
	memcpy(old_data, org_data, size);

	/* Download default Firmware */
	srp_fw_download();
}

void srp_core_reset(void)
{
	unsigned int reset_type = srp.pdata->type;
	unsigned long deadline;
	int ret = 0;

	if (!srp.is_loaded || srp.hw_reset_stat)
		return;

	if (reset_type == SRP_HW_RESET)
		return;

	deadline = jiffies + (HZ / 4);
	do {
		srp_commbox_init();
		srp_fw_download();

		/* RESET */
		writel(0x0, srp.commbox + SRP_CONT);
		srp_pending_ctrl(RUN);

		ret = wait_event_interruptible_timeout(reset_wq,
				srp.hw_reset_stat, HZ / 20);
		if (ret)
			break;

		srp_pending_ctrl(STALL);
	} while(time_before(jiffies, deadline));

	if (!ret) {
		srp_err("Not ready to sw reset.\n");
		srp.is_loaded = false;
	}
}

int srp_core_suspend(int num)
{
	unsigned long ibuf_size = srp.pdata->ibuf.size;
	unsigned long obuf_size = srp.pdata->obuf.size;
	unsigned long dmem_size = srp.pdata->dmem_size;
	unsigned long commbox_size = srp.pdata->commbox_size;
	unsigned int reset_type = srp.pdata->type;
	unsigned char *data;
	size_t size;

	if (!srp.is_loaded)
		return -1;

	if ((reset_type == SRP_HW_RESET && !srp.decoding_started)
		|| (reset_type == SRP_HW_RESET && num == RUNTIME))
		return -1;

	if (!srp.idle)
		return -1;

#ifdef CONFIG_PM_RUNTIME
	if (srp.pm_suspended)
		goto exit_func;
#endif

	data = srp.fw_info.data_va;
	size = dmem_size - srp.data_offset;

	/* IBUF/OBUF Save */
	memcpy(srp.sp_data.ibuf, srp.ibuf0, ibuf_size * 2);
	memcpy(srp.sp_data.obuf, srp.obuf0, obuf_size * 2);

	/* Request Suspend mode */
	srp_request_intr_mode(SUSPEND);

	memcpy(data, srp.dmem + srp.data_offset, size);
	memcpy(srp.sp_data.commbox, srp.commbox, commbox_size);
	srp.pm_suspended = true;

#ifdef CONFIG_PM_RUNTIME
exit_func:
	if (reset_type == SRP_SW_RESET)
		srp.hw_reset_stat = false;
#endif

	return 0;
}

void srp_core_resume(void)
{
	unsigned long ibuf_size = srp.pdata->ibuf.size;
	unsigned long obuf_size = srp.pdata->obuf.size;
	unsigned long commbox_size = srp.pdata->commbox_size;
	unsigned int reset_type = srp.pdata->type;

	if (reset_type == SRP_HW_RESET && !srp.decoding_started)
		return;

	if (!srp.pm_suspended)
		return;

	srp_fw_download();
	memcpy(srp.commbox, srp.sp_data.commbox, commbox_size);
	memcpy(srp.ibuf0, srp.sp_data.ibuf, ibuf_size * 2);
	memcpy(srp.obuf0, srp.sp_data.obuf, obuf_size * 2);

	if (reset_type == SRP_HW_RESET) {
		/* RESET */
		writel(0x0, srp.commbox + SRP_CONT);
	}
#ifndef CONFIG_PM_RUNTIME
	else if (reset_type == SRP_SW_RESET) {
		/* RESET */
		writel(0x0, srp.commbox + SRP_CONT);
	}
#endif

	srp_request_intr_mode(RESUME);
	srp.pm_suspended = false;
}

static void srp_fill_ibuf(void)
{
	unsigned long ibuf_size = srp.pdata->ibuf.size;
	unsigned long fill_size = 0;

	if (!srp.wbuf_pos)
		return;

	if (srp.wbuf_pos >= ibuf_size) {
		fill_size = ibuf_size;
		srp.wbuf_pos -= fill_size;
	} else {
		if (srp.wait_for_eos) {
			fill_size = srp.wbuf_pos;
			memset(&srp.wbuf[fill_size], 0xFF,
				ibuf_size - fill_size);
			srp.wbuf_pos = 0;
		}
	}

	if (srp.ibuf_next == 0) {
		memcpy(srp.ibuf0, srp.wbuf, ibuf_size);
		srp_debug("Fill IBUF0 (%lu)\n", fill_size);
		srp.ibuf_empty[0] = 0;
		srp.ibuf_next = 1;
	} else {
		memcpy(srp.ibuf1, srp.wbuf, ibuf_size);
		srp_debug("Fill IBUF1 (%lu)\n", fill_size);
		srp.ibuf_empty[1] = 0;
		srp.ibuf_next = 0;
	}

	if (srp.wbuf_pos)
		memcpy(srp.wbuf, &srp.wbuf[ibuf_size], srp.wbuf_pos);
}

static ssize_t srp_write(struct file *file, const char *buffer,
					size_t size, loff_t *pos)
{
	unsigned long ibuf_size = srp.pdata->ibuf.size;
	unsigned long start_threshold = 0;
	ssize_t ret = 0;
#ifdef CONFIG_PM_RUNTIME
	unsigned int reset_type = srp.pdata->type;
#endif

	srp_debug("Write(%d bytes)\n", size);

	srp.idle = false;
	srp_pm_control(true);

	if (srp.pm_suspended) {
#ifdef CONFIG_PM_RUNTIME
		if (reset_type == SRP_SW_RESET) {
			ret = wait_event_interruptible_timeout(reset_wq,
					    srp.hw_reset_stat, HZ / 20);
			if (!ret) {
				srp_err("Not ready to resume srp core.\n");
				return -EFAULT;
			}
		}
#endif
		spin_lock(&lock);
		srp_core_resume();
		spin_unlock(&lock);
	}

	spin_lock(&lock);
	if (srp.initialized) {
		srp.initialized = false;
		srp.pm_suspended = false;
		srp_flush_ibuf();
		srp_flush_obuf();
		srp_set_default_fw();
		srp_reset();
	}

	spin_lock(&lock_intr);
	if (srp.obuf_fill_done[srp.obuf_ready]
		&& srp.obuf_copy_done[srp.obuf_ready]) {
		srp.obuf_fill_done[srp.obuf_ready] = 0;
		srp.obuf_copy_done[srp.obuf_ready] = 0;
		srp_obuf_elapsed();
	}

	if (srp.wbuf_pos + size > srp.wbuf_size) {
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
			? ibuf_size : ibuf_size * 3;

	if (srp.wbuf_pos < start_threshold) {
		ret = size;
		goto exit_func;
	}

	if (!srp.decoding_started) {
		srp_fill_ibuf();
		srp_info("First Start decoding!!\n");
		srp_pending_ctrl(RUN);
		srp.decoding_started = 1;
	}

exit_func:
	spin_unlock(&lock_intr);
	spin_unlock(&lock);
	return ret;
}

static ssize_t srp_read(struct file *file, char *buffer,
				size_t size, loff_t *pos)
{
	struct srp_buf_info *argp = (struct srp_buf_info *)buffer;
	unsigned long obuf_size = srp.pdata->obuf.size;
	unsigned char *mmapped_obuf0 = srp.obuf_info.addr;
	unsigned char *mmapped_obuf1 = srp.obuf_info.addr + obuf_size;
	int ret = 0;
#ifdef CONFIG_PM_RUNTIME
	unsigned int reset_type = srp.pdata->type;
#endif

	srp_debug("Entered Get Obuf in PCM function\n");

	srp.idle = false;
	if (srp.prepare_for_eos) {
		srp_pm_control(true);
		if (srp.pm_suspended) {
#ifdef CONFIG_PM_RUNTIME
			if (reset_type == SRP_SW_RESET) {
				ret = wait_event_interruptible_timeout(reset_wq,
					    srp.hw_reset_stat, HZ / 20);
				if (!ret) {
					srp_err("Not ready to resume srp core.\n");
					return -EFAULT;
				}
			}
#endif
			spin_lock(&lock);
			srp_core_resume();
			spin_unlock(&lock);
		}

		spin_lock(&lock);
		srp.obuf_fill_done[srp.obuf_ready] = 0;
		srp_debug("Elapsed Obuf[%d] after Send EOS\n", srp.obuf_ready);

		if (srp.play_done && srp.ibuf_empty[0] && srp.ibuf_empty[1]) {
			srp_info("Protect read operation after play done.\n");
			srp.pcm_info.size = 0;
			srp.stop_after_eos = 1;
			spin_unlock(&lock);
			return copy_to_user(argp, &srp.pcm_info, sizeof(struct srp_buf_info));
		}

		srp_pending_ctrl(RUN);
		srp_obuf_elapsed();
		spin_unlock(&lock);
	}

	if (srp.wait_for_eos)
		srp.prepare_for_eos = 1;

	if (srp.decoding_started) {
		if (srp.obuf_copy_done[srp.obuf_ready] && !srp.wait_for_eos) {
			srp_debug("Wrong ordering read() OBUF[%d] int!!!\n", srp.obuf_ready);
			srp.pcm_info.size = 0;
			ret = copy_to_user(argp, &srp.pcm_info, sizeof(struct srp_buf_info));
			goto exit_func;
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
				ret = copy_to_user(argp, &srp.pcm_info, sizeof(struct srp_buf_info));
				goto exit_func;
			}
		}
	} else {
		srp_debug("not prepared not yet! OBUF[%d]\n", srp.obuf_ready);
		srp.pcm_info.size = 0;
		ret = copy_to_user(argp, &srp.pcm_info, sizeof(struct srp_buf_info));
		goto exit_func;
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
		srp_pending_ctrl(RUN);
	}

exit_func:
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
	struct exynos_srp_buf obuf = srp.pdata->obuf;
	unsigned long ibuf_size = srp.pdata->ibuf.size;
	unsigned long ibuf_num = srp.pdata->ibuf.num;
	unsigned long val = 0;
	long ret = 0;
#ifdef CONFIG_PM_RUNTIME
	unsigned int reset_type = srp.pdata->type;
#endif

	mutex_lock(&srp_mutex);

	switch (cmd) {
	case SRP_INIT:
		srp_debug("SRP_INIT\n");
		srp.initialized = true;
		break;

	case SRP_DEINIT:
		srp_debug("SRP DEINIT\n");
		srp_commbox_deinit();
		srp.initialized = false;
		break;

	case SRP_GET_MMAP_SIZE:
		srp.obuf_info.mmapped_size = obuf.size * obuf.num + obuf.offset;
		val = srp.obuf_info.mmapped_size;
		ret = copy_to_user((unsigned long *)arg,
					&val, sizeof(unsigned long));

		srp_debug("OBUF_MMAP_SIZE = %ld\n", val);
		break;

	case SRP_FLUSH:
		srp_debug("SRP_FLUSH\n");
#ifdef CONFIG_PM_RUNTIME
		if (reset_type == SRP_SW_RESET) {
			ret = wait_event_interruptible_timeout(reset_wq,
					    srp.hw_reset_stat, HZ / 20);
			if (!ret)
				srp_pm_control(true);
		}
#endif
		spin_lock(&lock_intr);
		srp_wait_for_pending();
		srp_flush_ibuf();
		srp_flush_obuf();
		srp_set_default_fw();
		srp_core_resume();
		srp_reset();
		spin_unlock(&lock_intr);
		break;

	case SRP_GET_IBUF_INFO:
		srp.ibuf_info.addr = (void *) srp.wbuf;
		srp.ibuf_info.size = ibuf_size * 2;
		srp.ibuf_info.num  = ibuf_num;

		ret = copy_to_user(argp, &srp.ibuf_info,
						sizeof(struct srp_buf_info));
		break;

	case SRP_GET_OBUF_INFO:
		ret = copy_from_user(&srp.obuf_info, argp,
				sizeof(struct srp_buf_info));
		if (!ret) {
			srp.obuf_info.addr = srp.obuf_info.mmapped_addr
							+ obuf.offset;
			srp.obuf_info.size = obuf.size;
			srp.obuf_info.num = obuf.num;
		}

		ret = copy_to_user(argp, &srp.obuf_info,
					sizeof(struct srp_buf_info));
		break;

	case SRP_SEND_EOS:
		srp_info("Send End-Of-Stream\n");
		if (srp.wbuf_fill_size == 0) {
			srp.stop_after_eos = 1;
		} else if (srp.wbuf_fill_size < ibuf_size * 3) {
			srp_debug("%ld, smaller than ibuf_size * 3\n", srp.wbuf_fill_size);
			srp.wait_for_eos = 1;
			srp_fill_ibuf();
			srp_set_stream_size();
			srp_pending_ctrl(RUN);
			srp.decoding_started = 1;
		} else if (srp.wbuf_fill_size >= ibuf_size * 3) {
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
	if (!srp.is_loaded) {
		srp_err("Not loaded srp firmware.\n");
		mutex_unlock(&srp_mutex);
		return -ENXIO;
	}

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
	unsigned int base = srp.pdata->obuf.base;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long size_max;
	unsigned int pfn;
	unsigned int mmap_addr;

	size_max = (srp.obuf_info.mmapped_size + PAGE_SIZE - 1) &
			~(PAGE_SIZE - 1);
	if (size > size_max)
		return -EINVAL;

	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	mmap_addr = base;
	pfn = __phys_to_pfn(mmap_addr);

	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
		srp_err("failed to mmap for Obuf\n");
		return -EAGAIN;
	}

	return 0;
}

static void srp_check_obuf_info(void)
{
	unsigned long obuf_size = srp.pdata->obuf.size;
	unsigned int buf0 = readl(srp.commbox + SRP_PCM_BUFF0);
	unsigned int buf1 = readl(srp.commbox + SRP_PCM_BUFF1);
	unsigned int size = readl(srp.commbox + SRP_PCM_BUFF_SIZE);

	if (srp.obuf0_pa != buf0)
		srp_err("Wrong PCM BUF0[0x%x], OBUF0[0x%x]\n",
					buf0, srp.obuf0_pa);
	if (srp.obuf1_pa != buf1)
		srp_err("Wrong PCM BUF1[0x%x], OBUF1[0x%x]\n",
					buf1, srp.obuf1_pa);
	if ((obuf_size >> 2) != size)
		srp_err("Wrong OBUF SIZE[%d]\n", size);
}

static void srp_get_buf_info(void)
{
	struct exynos_srp_buf ibuf = srp.pdata->ibuf;
	struct exynos_srp_buf obuf = srp.pdata->obuf;
	bool use_iram = srp.pdata->use_iram;

	/* Get I/O Buffer virtual address */
	srp.ibuf0 = use_iram ? srp.iram + ibuf.offset
			     : srp.dmem + ibuf.offset;
	srp.ibuf1 = srp.ibuf0 + ibuf.size;
	srp.obuf0 = srp.dmem + obuf.offset;
	srp.obuf1 = srp.obuf0 + obuf.size;

	/* Get I/O Buffer Physical address */
	srp.ibuf0_pa = ibuf.base + ibuf.offset;
	srp.ibuf1_pa = srp.ibuf0_pa + ibuf.size;
	srp.obuf0_pa = obuf.base + obuf.offset;
	srp.obuf1_pa = srp.obuf0_pa + obuf.size;

	/* Get bitstream bufferring size */
	srp.wbuf_size = ibuf.size * 4;

	/* Get Data offset */
	srp.data_offset = (obuf.size * 2) + obuf.offset;

	srp_info("[VA]IBUF0[0x%p], [PA]IBUF0[0x%x]\n",
						srp.ibuf0, srp.ibuf0_pa);
	srp_info("[VA]IBUF1[0x%p], [PA]IBUF1[0x%x]\n",
						srp.ibuf1, srp.ibuf1_pa);
	srp_info("[VA]OBUF0[0x%p], [PA]OBUF0[0x%x]\n",
						srp.obuf0, srp.obuf0_pa);
	srp_info("[VA]OBUF1[0x%p], [PA]OBUF1[0x%x]\n",
						srp.obuf1, srp.obuf1_pa);
	srp_info("IBUF SIZE [%ld]Bytes, OBUF SIZE [%ld]Bytes\n",
						ibuf.size, obuf.size);
}

static void srp_alloc_buf(void)
{
	unsigned long ibuf_size = srp.pdata->ibuf.size;
	unsigned long obuf_size = srp.pdata->obuf.size;
	unsigned long commbox_size = srp.pdata->commbox_size;

	srp.wbuf = kmalloc(srp.wbuf_size, GFP_KERNEL | GFP_DMA);
	if (!srp.wbuf) {
		srp_err("Failed to alloc memory for wbuf\n");
		return;
	}

	srp.sp_data.ibuf = kmalloc(ibuf_size * 2, GFP_KERNEL | GFP_DMA);
	if (!srp.sp_data.ibuf) {
		srp_err("Failed to alloc memory for sp_data ibuf\n");
		goto err1;
	}

	srp.sp_data.obuf = kmalloc(obuf_size * 2, GFP_KERNEL | GFP_DMA);
	if (!srp.sp_data.obuf) {
		srp_err("Failed to alloc memory for sp_data obuf\n");
		goto err2;
	}

	srp.sp_data.commbox = kmalloc(commbox_size, GFP_KERNEL | GFP_DMA);
	if (!srp.sp_data.commbox) {
		srp_err("Failed to alloc memory for sp_data commbox\n");
		goto err3;
	}

	return;

err3:
	kfree(srp.sp_data.obuf);
err2:
	kfree(srp.sp_data.ibuf);
err1:
	kfree(srp.wbuf);

	return;
}

static int srp_free_buf(void)
{
	kfree(srp.fw_info.vliw_va);
	kfree(srp.fw_info.cga_va);
	kfree(srp.fw_info.data_va);

	kfree(srp.wbuf);
	kfree(srp.sp_data.ibuf);
	kfree(srp.sp_data.obuf);
	kfree(srp.sp_data.commbox);

	if (srp.fw_info.vliw)
		release_firmware(srp.fw_info.vliw);

	if (srp.fw_info.cga)
		release_firmware(srp.fw_info.cga);

	if (srp.fw_info.data)
		release_firmware(srp.fw_info.data);

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

static irqreturn_t srp_irq(int irqno, void *dev_id)
{
	unsigned int irq_code = readl(srp.commbox + SRP_INTERRUPT_CODE);
	unsigned int irq_code_req;
	unsigned int wakeup_read = 0;
	unsigned int wakeup_decinfo = 0;
	unsigned int hw_reset = 0;
	unsigned int reset_type = srp.pdata->type;

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
				if (reset_type == SRP_SW_RESET) {
					if (!srp.hw_reset_stat) {
						srp_pending_ctrl(STALL);
						hw_reset = 1;
						break;
					}
				}
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
			} else {
				srp_debug("OBUF1 FULL\n");
				srp.obuf_fill_done[1] = 1;
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

		srp.ibuf_empty[0] = 1;
		srp.ibuf_empty[1] = 1;
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
		srp.idle = true;
		if (waitqueue_active(&read_wq))
			wake_up_interruptible(&read_wq);
	}

	if (wakeup_decinfo) {
		if (waitqueue_active(&decinfo_wq))
			wake_up_interruptible(&decinfo_wq);
	}

	if (hw_reset) {
		srp_info("Complete h/w reset.\n");
		srp.hw_reset_stat = true;
		srp.idle = true;
		if (waitqueue_active(&reset_wq))
			wake_up_interruptible(&reset_wq);
	}

	srp_debug("IRQ Exited!\n");

	return IRQ_HANDLED;
}

static void
srp_firmware_request_complete(const struct firmware *vliw, void *context)
{
	const struct firmware *cga;
	const struct firmware *data;
	struct device *dev = context;
	unsigned long icache_size = srp.pdata->icache_size;
	unsigned long dmem_size = srp.pdata->dmem_size;
	unsigned long cmem_size = srp.pdata->cmem_size;
	unsigned int reset_type = srp.pdata->type;
	unsigned int check_card;

	check_card = srp_check_sound_list();

	if (check_card < 1) {
		srp_err("Failed to detect sound card\n");
		check_card = 0;
		return;
	}

	if (!vliw) {
		srp_err("Failed to requset firmware[%s]\n", VLIW_NAME);
		return;
	}

	if (request_firmware(&cga, CGA_NAME, dev)) {
		srp_err("Failed to requset firmware[%s]\n", CGA_NAME);
		return;
	}

	if (request_firmware(&data, DATA_NAME, dev)) {
		srp_err("Failed to requset firmware[%s]\n", DATA_NAME);
		return;
	}

	srp.fw_info.vliw = vliw;
	srp.fw_info.cga = cga;
	srp.fw_info.data = data;

	srp.fw_info.vliw_size = vliw->size;
	srp.fw_info.cga_size = cga->size;
	srp.fw_info.data_size = data->size;

	/* Firmware Memory allocation */
	srp.fw_info.vliw_va = kmalloc(icache_size, GFP_KERNEL | GFP_DMA);
	if (!srp.fw_info.vliw_va) {
		srp_err("Failed to alloc memory for vliw\n");
		goto err1;
	}

	srp.fw_info.cga_va = kmalloc(cmem_size, GFP_KERNEL | GFP_DMA);
	if (!srp.fw_info.cga_va) {
		srp_err("Failed to alloc memory for cga\n");
		goto err2;
	}

	srp.fw_info.data_va = kmalloc(dmem_size, GFP_KERNEL | GFP_DMA);
	if (!srp.fw_info.data_va) {
		srp_err("Failed to alloc memory for data\n");
		goto err3;
	}

	srp.fw_info.vliw_pa = virt_to_phys(srp.fw_info.vliw_va);
	srp.fw_info.cga_pa = virt_to_phys(srp.fw_info.cga_va);
	srp.fw_info.data_pa = virt_to_phys(srp.fw_info.data_va);

	memcpy(srp.fw_info.vliw_va, vliw->data, vliw->size);
	memcpy(srp.fw_info.cga_va, cga->data, cga->size);
	memcpy(srp.fw_info.data_va, data->data, data->size);

	srp_info("Completed loading vliw[%d]\n", vliw->size);
	srp_info("Completed loading cga[%d]\n", cga->size);
	srp_info("Completed loading data[%d]\n", data->size);

	release_firmware(srp.fw_info.vliw);
	release_firmware(srp.fw_info.cga);
	srp.is_loaded = true;

	srp_get_buf_info();
	srp_alloc_buf();

	if (reset_type == SRP_SW_RESET) {
		srp_pm_control(true);
#ifndef CONFIG_PM_RUNTIME
		srp_core_reset();
#endif
		srp_pm_control(false);
	}

	srp_fw_ready_done = srp.is_loaded;

	return;

err3:
	kfree(srp.fw_info.cga_va);
err2:
	kfree(srp.fw_info.vliw_va);
err1:
	release_firmware(srp.fw_info.vliw);
	release_firmware(srp.fw_info.cga);
	release_firmware(srp.fw_info.data);

	return;
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
	srp_info("Suspend\n");

	if (!srp.is_loaded) {
		srp_info("Not loaded srp firmware.\n");
		return 0;
	}

	if (srp.pm_suspended) {
		srp_info("Already suspended!\n");
		return 0;
	}

	srp_pm_control(true);

	srp.idle = true;

	srp_core_suspend(SLEEP);

	srp_pm_control(false);

	return 0;
}

static int srp_resume(struct platform_device *pdev)
{
	srp_info("Resume\n");

	return 0;
}

#else
#define srp_suspend NULL
#define srp_resume  NULL
#endif

static __devinit int srp_probe(struct platform_device *pdev)
{
	struct exynos_srp_pdata *pdata = pdev->dev.platform_data;
	struct resource *int_mem_res;
	struct resource *commbox_res;
	unsigned long int_mem_size = 0;
	unsigned int int_mem_base = 0;
	unsigned int commbox_base = 0;
	int ret = 0;

	if (!pdata) {
		srp_err("Failed to get platform data\n");
		return -ENODEV;
	}

	srp.pdata = kzalloc(sizeof(struct exynos_srp_pdata), GFP_KERNEL);
	if (!srp.pdata) {
		srp_err("Failed to alloc memory for platform data\n");
		return -ENOMEM;
	}

	memcpy(srp.pdata, pdata, sizeof(struct exynos_srp_pdata));

	/* Total size of internal memory */
	int_mem_size = srp.pdata->icache_size + srp.pdata->dmem_size
					      + srp.pdata->cmem_size;

	int_mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!int_mem_res) {
		srp_err("Unable to get internal mem resource\n");
		ret = -ENXIO;
		goto err0;
	}
	
	commbox_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!commbox_res) {
		srp_err("Unable to get commbox resource\n");
		ret = -ENXIO;
		goto err0;
	}

	if (!request_mem_region(int_mem_res->start, resource_size(int_mem_res),
								"samsung-rp")) {
		srp_err("Unable to request internal mem\n");
		ret = -EBUSY;
		goto err0;
	}
	int_mem_base = int_mem_res->start;

	if (!request_mem_region(commbox_res->start, resource_size(commbox_res),
								"samsung-rp")) {
		srp_err("Unable to request commbox\n");
		ret = -EBUSY;
		goto err1;
	}
	commbox_base = commbox_res->start;

	srp.dmem = ioremap(int_mem_base, int_mem_size);
	if (srp.dmem == NULL) {
		srp_err("Failed to ioremap for dmem\n");
		ret = -ENOMEM;
		goto err2;

	}
	srp.icache = srp.dmem + srp.pdata->dmem_size;
	srp.cmem = srp.icache + srp.pdata->icache_size;

	srp.commbox = ioremap(commbox_base, srp.pdata->commbox_size);
	if (srp.commbox == NULL) {
		srp_err("Failed to ioremap for audio subsystem\n");
		ret = -ENOMEM;
		goto err3;
	}

	if (srp.pdata->use_iram) {
		srp.iram = ioremap(srp.pdata->ibuf.base,
					srp.pdata->iram_size);
		if (srp.iram == NULL) {
			srp_err("Failed to ioremap for sram area\n");
			ret = -ENOMEM;
			goto err4;
		}
	}

	ret = request_irq(IRQ_AUDIO_SS, srp_irq, IRQF_DISABLED, "samsung-rp", pdev);
	if (ret < 0) {
		srp_err("SRP: Fail to claim SRP(AUDIO_SS) irq\n");
		goto err5;
	}

	ret = misc_register(&srp_miscdev);
	if (ret) {
		srp_err("SRP: Cannot register miscdev on minor=%d\n",
			SRP_DEV_MINOR);
		goto err6;
	}

	ret = request_firmware_nowait(THIS_MODULE,
				      FW_ACTION_HOTPLUG,
				      VLIW_NAME,
				      &pdev->dev,
				      GFP_KERNEL,
				      &pdev->dev,
				      srp_firmware_request_complete);
	if (ret) {
		dev_err(&pdev->dev, "could not load firmware (err=%d)\n", ret);
		goto err7;
	}

	return 0;

err7:
	misc_deregister(&srp_miscdev);
err6:
	free_irq(IRQ_AUDIO_SS, pdev);
err5:
	if (srp.pdata->use_iram)
		iounmap(srp.iram);
err4:
	iounmap(srp.commbox);
err3:
	iounmap(srp.dmem);
err2:
	release_mem_region(commbox_base, resource_size(commbox_res));
err1:
	release_mem_region(int_mem_base, resource_size(int_mem_res));
err0:
	kfree(srp.pdata);

	return ret;
}

static __devexit int srp_remove(struct platform_device *pdev)
{
	free_irq(IRQ_AUDIO_SS, pdev);
	srp_free_buf();

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
