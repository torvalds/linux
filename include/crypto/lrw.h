#ifndef _CRYPTO_LRW_H
#define _CRYPTO_LRW_H

#include <crypto/b128ops.h>

struct scatterlist;
struct gf128mul_64k;
struct blkcipher_desc;

#define LRW_BLOCK_SIZE 16

struct lrw_table_ctx {
	/* optimizes multiplying a random (non incrementing, as at the
	 * start of a new sector) value with key2, we could also have
	 * used 4k optimization tables or no optimization at all. In the
	 * latter case we would have to store key2 here */
	struct gf128mul_64k *table;
	/* stores:
	 *  key2*{ 0,0,...0,0,0,0,1 }, key2*{ 0,0,...0,0,0,1,1 },
	 *  key2*{ 0,0,...0,0,1,1,1 }, key2*{ 0,0,...0,1,1,1,1 }
	 *  key2*{ 0,0,...1,1,1,1,1 }, etc
	 * needed for optimized multiplication of incrementing values
	 * with key2 */
	be128 mulinc[128];
};

int lrw_init_table(struct lrw_table_ctx *ctx, const u8 *tweak);
void lrw_free_table(struct lrw_table_ctx *ctx);

struct lrw_crypt_req {
	be128 *tbuf;
	unsigned int tbuflen;

	struct lrw_table_ctx *table_ctx;
	void *crypt_ctx;
	void (*crypt_fn)(void *ctx, u8 *blks, unsigned int nbytes);
};

int lrw_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
	      struct scatterlist *src, unsigned int nbytes,
	      struct lrw_crypt_req *req);

#endif  /* _CRYPTO_LRW_H */
