/*
 * linux/lib/zlib_deflate/deflate_syms.c
 *
 * Exported symbols for the deflate functionality.
 *
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/zlib.h>

EXPORT_SYMBOL(zlib_deflate_workspacesize);
EXPORT_SYMBOL(zlib_deflate);
EXPORT_SYMBOL(zlib_deflateInit_);
EXPORT_SYMBOL(zlib_deflateInit2_);
EXPORT_SYMBOL(zlib_deflateEnd);
EXPORT_SYMBOL(zlib_deflateReset);
EXPORT_SYMBOL(zlib_deflateCopy);
EXPORT_SYMBOL(zlib_deflateParams);
MODULE_LICENSE("GPL");
