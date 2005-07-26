/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2004 Ferenc Havasi <havasi@inf.u-szeged.hu>,
 *                    University of Szeged, Hungary
 *
 * For licensing information, see the file 'LICENCE' in the 
 * jffs2 directory.
 *
 * $Id: compr.h,v 1.8 2005/07/26 13:24:40 havasi Exp $
 *
 */

#ifndef __JFFS2_COMPR_H__
#define __JFFS2_COMPR_H__

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/jffs2.h>
#include <linux/jffs2_fs_i.h>
#include <linux/jffs2_fs_sb.h>
#include "nodelist.h"

#define JFFS2_RUBINMIPS_PRIORITY 10
#define JFFS2_DYNRUBIN_PRIORITY  20
#define JFFS2_LZARI_PRIORITY     30
#define JFFS2_LZO_PRIORITY       40
#define JFFS2_RTIME_PRIORITY     50
#define JFFS2_ZLIB_PRIORITY      60

#define JFFS2_RUBINMIPS_DISABLED /* RUBINs will be used only */
#define JFFS2_DYNRUBIN_DISABLED  /*        for decompression */

#define JFFS2_COMPR_MODE_NONE       0
#define JFFS2_COMPR_MODE_PRIORITY   1
#define JFFS2_COMPR_MODE_SIZE       2

struct jffs2_compressor {
        struct list_head list;
        int priority;              /* used by prirority comr. mode */
        char *name;
        char compr;                /* JFFS2_COMPR_XXX */
        int (*compress)(unsigned char *data_in, unsigned char *cpage_out,
                        uint32_t *srclen, uint32_t *destlen, void *model);
        int (*decompress)(unsigned char *cdata_in, unsigned char *data_out,
                        uint32_t cdatalen, uint32_t datalen, void *model);
        int usecount;
        int disabled;              /* if seted the compressor won't compress */
        unsigned char *compr_buf;  /* used by size compr. mode */
        uint32_t compr_buf_size;   /* used by size compr. mode */
        uint32_t stat_compr_orig_size;
        uint32_t stat_compr_new_size;
        uint32_t stat_compr_blocks;
        uint32_t stat_decompr_blocks;
};

int jffs2_register_compressor(struct jffs2_compressor *comp);
int jffs2_unregister_compressor(struct jffs2_compressor *comp);

int jffs2_compressors_init(void);
int jffs2_compressors_exit(void);

uint16_t jffs2_compress(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
                             unsigned char *data_in, unsigned char **cpage_out,
                             uint32_t *datalen, uint32_t *cdatalen);

int jffs2_decompress(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
                     uint16_t comprtype, unsigned char *cdata_in,
                     unsigned char *data_out, uint32_t cdatalen, uint32_t datalen);

void jffs2_free_comprbuf(unsigned char *comprbuf, unsigned char *orig);

#ifdef CONFIG_JFFS2_PROC
int jffs2_enable_compressor_name(const char *name);
int jffs2_disable_compressor_name(const char *name);
int jffs2_set_compression_mode_name(const char *mode_name);
char *jffs2_get_compression_mode_name(void);
int jffs2_set_compressor_priority(const char *mode_name, int priority);
char *jffs2_list_compressors(void);
char *jffs2_stats(void);
#endif

/* Compressor modules */
/* These functions will be called by jffs2_compressors_init/exit */

#ifdef CONFIG_JFFS2_RUBIN
int jffs2_rubinmips_init(void);
void jffs2_rubinmips_exit(void);
int jffs2_dynrubin_init(void);
void jffs2_dynrubin_exit(void);
#endif
#ifdef CONFIG_JFFS2_RTIME
int jffs2_rtime_init(void);
void jffs2_rtime_exit(void);
#endif
#ifdef CONFIG_JFFS2_ZLIB
int jffs2_zlib_init(void);
void jffs2_zlib_exit(void);
#endif

#endif /* __JFFS2_COMPR_H__ */
