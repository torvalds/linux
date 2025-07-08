#include <linux/sector_cache_map.h> // あなたのヘッダーファイル
#include <linux/slab.h>     // kmalloc のため (メモリ割り当て)
#include <linux/init.h>     // __init のため (初期化関数)
#include <linux/kernel.h>   // printk のため
#include <linux/spinlock.h> // 同期のため (スピンロック)
#include <linux/rcupdate.h> // 同期のため (RCU)
#include <linux/io_uring.h>
#include <linux/kernel.h>   // printk のため
#include <linux/uaccess.h>  // READ_ONCE のため
#include <linux/errno.h>    // -ENODATA, -EOPNOTSUPP などのエラーコードのため


// グローバルな xarray の実体定義
DEFINE_XARRAY(global_sector_to_cache_map_xa);

// xarray への書き込み（追加、削除）を保護するためのグローバルなスピンロック
static DEFINE_SPINLOCK(global_sector_map_lock);

/*
 * @brief 探索木の初期化関数
 * カーネル起動時に一度だけ呼び出されます。
 */
void __init global_sector_map_init(void) {
    // DEFINE_XARRAY マクロは xarray を自動的に初期化するため、特別な初期化コードは不要な場合が多いです。
    printk(KERN_INFO "io_cache: Global sector to cache map initialized.\n");

    // ★追加の確認コード★
    if (xa_empty(&global_sector_to_cache_map_xa)) {
        printk(KERN_INFO "io_cache: XArray is initially empty (as expected).\n");
    } else {
        printk(KERN_ERR "io_cache: XArray is NOT empty after init! (unexpected error)\n");
        // BUG_ON(1); // 開発中であれば、ここでカーネルパニックさせて問題を検出
    }
}

/*
 * @brief 探索木にマッピングエントリを追加します。
 * @param sector マッピングの開始セクタ番号 (キーとして使用)
 * @param folio ページキャッシュ上の folio へのポインタ
 * @param inode_ptr 対応する inode へのポインタ
 * @param file_offset ファイル内オフセット (バイト単位)
 * @return 0で成功、負の値でエラーコード。
 */
int global_sector_map_add(sector_t sector, struct folio *folio, struct inode *inode_ptr, loff_t file_offset) {
    struct host_cached_map_entry *entry;
    int ret = 0;

    // エントリのメモリを割り当て
    // GFP_ATOMIC は割り込みコンテキスト（I/O完了ハンドラなど）でも安全なメモリ割り当てフラグ
    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry) {
        printk(KERN_ERR "io_cache: Failed to allocate memory for map entry.\n");
        return -ENOMEM;
    }

    // 情報を設定
    entry->folio = folio;
    entry->inode = inode_ptr;
    entry->file_offset = file_offset;
    entry->map_start_sector = sector; // デバッグ情報
    entry->map_nr_sectors = folio_nr_pages(folio) >> 9; // folioサイズをセクタ数に変換 (SECTOR_SHIFTは通常9)

    // folio の参照カウントを増やす:
    // このエントリが folio への参照を保持するため、folio が途中で解放されないよう保護します。
    // folio_get() は Linux 6.x での推奨 API
    folio_get(folio);

    // スピンロックで保護して xarray に追加
    // 書き込み操作は複数のCPUからの同時アクセスを防ぐため、ロックが必要です。
    spin_lock(&global_sector_map_lock);
    // xa_insert は、同じキーに既に値がある場合、それを上書きします。
    ret = xa_insert(&global_sector_to_cache_map_xa, (unsigned long)sector, entry, GFP_ATOMIC);
    spin_unlock(&global_sector_map_lock);

    if (ret < 0) {
        printk(KERN_ERR "io_cache: Failed to insert map entry for sector %llu: %d\n", (unsigned long long)sector, ret);
        folio_put(folio); // 挿入失敗時は増やした参照カウントを戻す
        kfree(entry);     // 割り当てたメモリも解放
    } else {
        printk(KERN_DEBUG "io_cache: Added map for sector %llu, inode %lu, offset %llu, folio %p\n",
               (unsigned long long)sector, inode_ptr ? inode_ptr->i_ino : 0, (unsigned long long)file_offset, folio);
    }
    return ret;
}

/*
 * @brief 探索木からマッピングエントリを検索します。
 * @param sector 検索するセクタ番号 (キー)
 * @return 見つかったエントリへのポインタ、見つからない場合は NULL。
 */
struct host_cached_map_entry *global_sector_map_lookup(sector_t sector) {
    struct host_cached_map_entry *entry;
    // RCU (Read-Copy Update) 保護を使って読み取り
    // 読み取り中はロックが不要で非常に高速ですが、書き込み側は複雑になります。
    rcu_read_lock();
    entry = xa_load(&global_sector_to_cache_map_xa, (unsigned long)sector);
    rcu_read_unlock();

    if (entry) {
        // 見つかったエントリがまだ有効な folio を指しているか、追加の検証が必要な場合もあります。
        // (例: folio が追い出されていないか、refcount が 0 になっていないかなど)
        // ここで folio_try_get() を試みて、参照を確保しつつ folio の有効性を確認する手法もあります。
        // しかし、まずはシンプルに xa_load が返したポインタを信頼する。
        printk(KERN_DEBUG "io_cache: Lookup found for sector %llu, folio %p\n", (unsigned long long)sector, entry->folio);
    } else {
        printk(KERN_DEBUG "io_cache: Lookup MISS for sector %llu\n", (unsigned long long)sector);
    }
    return entry;
}

/*
 * @brief 探索木からマッピングエントリを削除します。
 * ページキャッシュから folio が追い出された際などに呼び出されます。
 * @param sector 削除するセクタ番号 (キー)
 */
void global_sector_map_remove(sector_t sector) {
    struct host_cached_map_entry *entry;
    // スピンロックで保護して xarray から削除
    // 書き込み操作はロックが必要です。
    spin_lock(&global_sector_map_lock);
    entry = xa_erase(&global_sector_to_cache_map_xa, (unsigned long)sector);
    spin_unlock(&global_sector_map_lock);

    if (entry) {
        printk(KERN_INFO "io_cache: Removed map for sector %llu.\n", (unsigned long long)sector);
        folio_put(entry->folio); // global_sector_map_add で増やした参照カウントを減らす
        kfree(entry);             // エントリ自体を解放
    } else {
        printk(KERN_WARNING "io_cache: Attempted to remove non-existent map for sector %llu.\n", (unsigned long long)sector);
    }
}


