/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _IOMAP_INTERNAL_H
#define _IOMAP_INTERNAL_H 1

#define IOEND_BATCH_SIZE	4096

u32 iomap_finish_ioend_direct(struct iomap_ioend *ioend);

#endif /* _IOMAP_INTERNAL_H */
