// SPDX-License-Identifier: 0BSD

/*
 * XZ decoder module information
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 */

#include <linux/module.h>
#include <linux/xz.h>

EXPORT_SYMBOL(xz_dec_init);
EXPORT_SYMBOL(xz_dec_reset);
EXPORT_SYMBOL(xz_dec_run);
EXPORT_SYMBOL(xz_dec_end);

#ifdef CONFIG_XZ_DEC_MICROLZMA
EXPORT_SYMBOL(xz_dec_microlzma_alloc);
EXPORT_SYMBOL(xz_dec_microlzma_reset);
EXPORT_SYMBOL(xz_dec_microlzma_run);
EXPORT_SYMBOL(xz_dec_microlzma_end);
#endif

MODULE_DESCRIPTION("XZ decompressor");
MODULE_VERSION("1.2");
MODULE_AUTHOR("Lasse Collin <lasse.collin@tukaani.org> and Igor Pavlov");
MODULE_LICENSE("Dual BSD/GPL");
