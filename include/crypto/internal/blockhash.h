/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Handle partial blocks for block hash.
 *
 * Copyright (c) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 * Copyright (c) 2025 Herbert Xu <herbert@gondor.apana.org.au>
 */

#ifndef _CRYPTO_INTERNAL_BLOCKHASH_H
#define _CRYPTO_INTERNAL_BLOCKHASH_H

#include <linux/string.h>
#include <linux/types.h>

#define BLOCK_HASH_UPDATE_BASE(block_fn, state, src, nbytes, bs, dv,	\
			       buf, buflen)				\
	({								\
		typeof(block_fn) *_block_fn = &(block_fn);		\
		typeof(state + 0) _state = (state);			\
		unsigned int _buflen = (buflen);			\
		size_t _nbytes = (nbytes);				\
		unsigned int _bs = (bs);				\
		const u8 *_src = (src);					\
		u8 *_buf = (buf);					\
		while ((_buflen + _nbytes) >= _bs) {			\
			const u8 *data = _src;				\
			size_t len = _nbytes;				\
			size_t blocks;					\
			int remain;					\
			if (_buflen) {					\
				remain = _bs - _buflen;			\
				memcpy(_buf + _buflen, _src, remain);	\
				data = _buf;				\
				len = _bs;				\
			}						\
			remain = len % bs;				\
			blocks = (len - remain) / (dv);			\
			(*_block_fn)(_state, data, blocks);		\
			_src += len - remain - _buflen;			\
			_nbytes -= len - remain - _buflen;		\
			_buflen = 0;					\
		}							\
		memcpy(_buf + _buflen, _src, _nbytes);			\
		_buflen += _nbytes;					\
	})

#define BLOCK_HASH_UPDATE(block, state, src, nbytes, bs, buf, buflen) \
	BLOCK_HASH_UPDATE_BASE(block, state, src, nbytes, bs, 1, buf, buflen)
#define BLOCK_HASH_UPDATE_BLOCKS(block, state, src, nbytes, bs, buf, buflen) \
	BLOCK_HASH_UPDATE_BASE(block, state, src, nbytes, bs, bs, buf, buflen)

#endif	/* _CRYPTO_INTERNAL_BLOCKHASH_H */
