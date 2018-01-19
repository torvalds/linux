/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MPX_MM_H
#define _MPX_MM_H

#define PAGE_SIZE 4096
#define MB (1UL<<20)

extern long nr_incore(void *ptr, unsigned long size_bytes);

#endif /* _MPX_MM_H */
