/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 * Author: Daeho Jeong <daehojeong@google.com>
 */
#ifndef __F2FS_IOSTAT_H__
#define __F2FS_IOSTAT_H__

#ifdef CONFIG_F2FS_IOSTAT

#define DEFAULT_IOSTAT_PERIOD_MS	3000
#define MIN_IOSTAT_PERIOD_MS		100
/* maximum period of iostat tracing is 1 day */
#define MAX_IOSTAT_PERIOD_MS		8640000

extern int __maybe_unused iostat_info_seq_show(struct seq_file *seq,
			void *offset);
extern void f2fs_reset_iostat(struct f2fs_sb_info *sbi);
extern void f2fs_update_iostat(struct f2fs_sb_info *sbi,
			enum iostat_type type, unsigned long long io_bytes);
extern int f2fs_init_iostat(struct f2fs_sb_info *sbi);
#else
static inline void f2fs_update_iostat(struct f2fs_sb_info *sbi,
		enum iostat_type type, unsigned long long io_bytes) {}
static inline int f2fs_init_iostat(struct f2fs_sb_info *sbi) { return 0; }
#endif
#endif /* __F2FS_IOSTAT_H__ */
