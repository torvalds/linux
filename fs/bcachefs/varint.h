/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_VARINT_H
#define _BCACHEFS_VARINT_H

int bch2_varint_encode(u8 *, u64);
int bch2_varint_decode(const u8 *, const u8 *, u64 *);

int bch2_varint_encode_fast(u8 *, u64);
int bch2_varint_decode_fast(const u8 *, const u8 *, u64 *);

#endif /* _BCACHEFS_VARINT_H */
