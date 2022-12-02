// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include "avs.h"

static unsigned int __kfifo_fromio(struct kfifo *fifo, const void __iomem *src, unsigned int len)
{
	struct __kfifo *__fifo = &fifo->kfifo;
	unsigned int l, off;

	len = min(len, kfifo_avail(fifo));
	off = __fifo->in & __fifo->mask;
	l = min(len, kfifo_size(fifo) - off);

	memcpy_fromio(__fifo->data + off, src, l);
	memcpy_fromio(__fifo->data, src + l, len - l);
	/* Make sure data copied from SRAM is visible to all CPUs. */
	smp_mb();
	__fifo->in += len;

	return len;
}

bool avs_logging_fw(struct avs_dev *adev)
{
	return kfifo_initialized(&adev->trace_fifo);
}

void avs_dump_fw_log(struct avs_dev *adev, const void __iomem *src, unsigned int len)
{
	__kfifo_fromio(&adev->trace_fifo, src, len);
}

void avs_dump_fw_log_wakeup(struct avs_dev *adev, const void __iomem *src, unsigned int len)
{
	avs_dump_fw_log(adev, src, len);
	wake_up(&adev->trace_waitq);
}
