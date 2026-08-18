/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 Namjae Jeon <linkinjeon@kernel.org>
 */

#ifndef _LINUX_EXFAT_IOMAP_H
#define _LINUX_EXFAT_IOMAP_H

extern const struct iomap_dio_ops exfat_write_dio_ops;
extern const struct iomap_ops exfat_iomap_ops;
extern const struct iomap_ops exfat_write_iomap_ops;
extern const struct iomap_writeback_ops exfat_writeback_ops;
extern const struct iomap_read_ops exfat_iomap_bio_read_ops;

int exfat_iomap_swap_activate(struct swap_info_struct *sis,
			       struct file *file, sector_t *span);

#endif /* _LINUX_EXFAT_IOMAP_H */
