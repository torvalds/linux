/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MTD_MTDRAM_H__
#define __MTD_MTDRAM_H__

#include <linux/mtd/mtd.h>
int mtdram_init_device(struct mtd_info *mtd, void *mapped_address,
			unsigned long size, const char *name);

#endif /* __MTD_MTDRAM_H__ */
