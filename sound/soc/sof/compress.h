/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2019-2020 Intel Corporation. All rights reserved.
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef __SOF_COMPRESS_H
#define __SOF_COMPRESS_H

#include <sound/compress_driver.h>

extern struct snd_soc_cdai_ops sof_probe_compr_ops;
extern const struct snd_compress_ops sof_probe_compressed_ops;

#endif
