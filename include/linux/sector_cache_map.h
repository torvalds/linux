#ifndef _LINUX_SECTOR_CACHE_MAP_H
#define _LINUX_SECTOR_CACHE_MAP_H

#include <linux/types.h>    // sector_t, loff_t のために必要
#include <linux/fs.h>       // struct inode のために必要
#include <linux/mm.h>       // struct folio のために必要
#include <linux/xarray.h>   // xarray のために必要
#include <linux/types.h>

struct io_kiocb;
struct io_uring_sqe;

/*
 * @brief グローバルなセクタからページキャッシュ上の情報へのマッピングエントリ
 */
struct host_cached_map_entry {
    struct folio *folio;       // ページキャッシュ上の対応する folio へのポインタ
    struct inode *inode;       // 対応するファイルの inode へのポインタ
    loff_t file_offset;        // ファイル内でのこのセクタの開始オフセット (バイト単位)

    // デバッグや詳細なマッピング情報のためのフィールド (任意)
    sector_t map_start_sector;  // このエントリがカバーするセクタ範囲の開始
    unsigned int map_nr_sectors; // このエントリがカバーするセクタ数
};

// グローバルな xarray の宣言
// カーネル全体からアクセス可能
extern struct xarray global_sector_to_cache_map_xa;

// 探索木操作のグローバルな API プロトタイプ
// これらの関数は mm/sector_cache_map.c で実装されます。
extern void global_sector_map_init(void);
extern int global_sector_map_add(sector_t sector, struct folio *folio, struct inode *inode_ptr, loff_t file_offset);
extern struct host_cached_map_entry *global_sector_map_lookup(sector_t sector);
extern void global_sector_map_remove(sector_t sector);

extern int io_pagecache_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
extern int io_pagecache_issue(struct io_kiocb *req, unsigned int issue_flags);

#endif /* _LINUX_SECTOR_CACHE_MAP_H */