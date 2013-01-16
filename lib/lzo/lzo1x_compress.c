/*
 *  LZO1X Compressor from LZO
 *
 *  Copyright (C) 1996-2012 Markus F.X.J. Oberhumer <markus@oberhumer.com>
 *
 *  The full LZO package can be found at:
 *  http://www.oberhumer.com/opensource/lzo/
 *
 *  Changed for Linux kernel use by:
 *  Nitin Gupta <nitingupta910@gmail.com>
 *  Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/unaligned.h>
#include <linux/lzo.h>
#include "lzodefs.h"

static noinline size_t
lzo1x_1_do_compress(const unsigned char *in, size_t in_len,
		    unsigned char *out, size_t *out_len,
		    size_t ti, void *wrkmem)
{
	const unsigned char *ip;
	unsigned char *op;
	const unsigned char * const in_end = in + in_len;
	const unsigned char * const ip_end = in + in_len - 20;
	const unsigned char *ii;
	lzo_dict_t * const dict = (lzo_dict_t *) wrkmem;

	op = out;
	ip = in;
	ii = ip;
	ip += ti < 4 ? 4 - ti : 0;

	for (;;) {
		const unsigned char *m_pos;
		size_t t, m_len, m_off;
		u32 dv;
literal:
		ip += 1 + ((ip - ii) >> 5);
next:
		if (unlikely(ip >= ip_end))
			break;
		dv = get_unaligned_le32(ip);
		t = ((dv * 0x1824429d) >> (32 - D_BITS)) & D_MASK;
		m_pos = in + dict[t];
		dict[t] = (lzo_dict_t) (ip - in);
		if (unlikely(dv != get_unaligned_le32(m_pos)))
			goto literal;

		ii -= ti;
		ti = 0;
		t = ip - ii;
		if (t != 0) {
			if (t <= 3) {
				op[-2] |= t;
				COPY4(op, ii);
				op += t;
			} else if (t <= 16) {
				*op++ = (t - 3);
				COPY8(op, ii);
				COPY8(op + 8, ii + 8);
				op += t;
			} else {
				if (t <= 18) {
					*op++ = (t - 3);
				} else {
					size_t tt = t - 18;
					*op++ = 0;
					while (unlikely(tt > 255)) {
						tt -= 255;
						*op++ = 0;
					}
					*op++ = tt;
				}
				do {
					COPY8(op, ii);
					COPY8(op + 8, ii + 8);
					op += 16;
					ii += 16;
					t -= 16;
				} while (t >= 16);
				if (t > 0) do {
					*op++ = *ii++;
				} while (--t > 0);
			}
		}

		m_len = 4;
		{
#if defined(LZO_USE_CTZ64)
		u64 v;
		v = get_unaligned((const u64 *) (ip + m_len)) ^
		    get_unaligned((const u64 *) (m_pos + m_len));
		if (unlikely(v == 0)) {
			do {
				m_len += 8;
				v = get_unaligned((const u64 *) (ip + m_len)) ^
				    get_unaligned((const u64 *) (m_pos + m_len));
				if (unlikely(ip + m_len >= ip_end))
					goto m_len_done;
			} while (v == 0);
		}
#  if defined(__LITTLE_ENDIAN)
		m_len += (unsigned) __builtin_ctzll(v) / 8;
#  elif defined(__BIG_ENDIAN)
		m_len += (unsigned) __builtin_clzll(v) / 8;
#  else
#    error "missing endian definition"
#  endif
#elif defined(LZO_USE_CTZ32)
		u32 v;
		v = get_unaligned((const u32 *) (ip + m_len)) ^
		    get_unaligned((const u32 *) (m_pos + m_len));
		if (unlikely(v == 0)) {
			do {
				m_len += 4;
				v = get_unaligned((const u32 *) (ip + m_len)) ^
				    get_unaligned((const u32 *) (m_pos + m_len));
				if (unlikely(ip + m_len >= ip_end))
					goto m_len_done;
			} while (v == 0);
		}
#  if defined(__LITTLE_ENDIAN)
		m_len += (unsigned) __builtin_ctz(v) / 8;
#  elif defined(__BIG_ENDIAN)
		m_len += (unsigned) __builtin_clz(v) / 8;
#  else
#    error "missing endian definition"
#  endif
#else
		if (unlikely(ip[m_len] == m_pos[m_len])) {
			do {
				m_len += 1;
				if (unlikely(ip + m_len >= ip_end))
					goto m_len_done;
			} while (ip[m_len] == m_pos[m_len]);
		}
#endif
		}
m_len_done:

		m_off = ip - m_pos;
		ip += m_len;
		ii = ip;
		if (m_len <= M2_MAX_LEN && m_off <= M2_MAX_OFFSET) {
			m_off -= 1;
			*op++ = (((m_len - 1) << 5) | ((m_off & 7) << 2));
			*op++ = (m_off >> 3);
		} else if (m_off <= M3_MAX_OFFSET) {
			m_off -= 1;
			if (m_len <= M3_MAX_LEN)
				*op++ = (M3_MARKER | (m_len - 2));
			else {
				m_len -= M3_MAX_LEN;
				*op++ = M3_MARKER | 0;
				while (unlikely(m_len > 255)) {
					m_len -= 255;
					*op++ = 0;
				}
				*op++ = (m_len);
			}
			*op++ = (m_off << 2);
			*op++ = (m_off >> 6);
		} else {
			m_off -= 0x4000;
			if (m_len <= M4_MAX_LEN)
				*op++ = (M4_MARKER | ((m_off >> 11) & 8)
						| (m_len - 2));
			else {
				m_len -= M4_MAX_LEN;
				*op++ = (M4_MARKER | ((m_off >> 11) & 8));
				while (unlikely(m_len > 255)) {
					m_len -= 255;
					*op++ = 0;
				}
				*op++ = (m_len);
			}
			*op++ = (m_off << 2);
			*op++ = (m_off >> 6);
		}
		goto next;
	}
	*out_len = op - out;
	return in_end - (ii - ti);
}

int lzo1x_1_compress(const unsigned char *in, size_t in_len,
		     unsigned char *out, size_t *out_len,
		     void *wrkmem)
{
	const unsigned char *ip = in;
	unsigned char *op = out;
	size_t l = in_len;
	size_t t = 0;

	while (l > 20) {
		size_t ll = l <= (M4_MAX_OFFSET + 1) ? l : (M4_MAX_OFFSET + 1);
		uintptr_t ll_end = (uintptr_t) ip + ll;
		if ((ll_end + ((t + ll) >> 5)) <= ll_end)
			break;
		BUILD_BUG_ON(D_SIZE * sizeof(lzo_dict_t) > LZO1X_1_MEM_COMPRESS);
		memset(wrkmem, 0, D_SIZE * sizeof(lzo_dict_t));
		t = lzo1x_1_do_compress(ip, ll, op, out_len, t, wrkmem);
		ip += ll;
		op += *out_len;
		l  -= ll;
	}
	t += l;

	if (t > 0) {
		const unsigned char *ii = in + in_len - t;

		if (op == out && t <= 238) {
			*op++ = (17 + t);
		} else if (t <= 3) {
			op[-2] |= t;
		} else if (t <= 18) {
			*op++ = (t - 3);
		} else {
			size_t tt = t - 18;
			*op++ = 0;
			while (tt > 255) {
				tt -= 255;
				*op++ = 0;
			}
			*op++ = tt;
		}
		if (t >= 16) do {
			COPY8(op, ii);
			COPY8(op + 8, ii + 8);
			op += 16;
			ii += 16;
			t -= 16;
		} while (t >= 16);
		if (t > 0) do {
			*op++ = *ii++;
		} while (--t > 0);
	}

	*op++ = M4_MARKER | 1;
	*op++ = 0;
	*op++ = 0;

	*out_len = op - out;
	return LZO_E_OK;
}
EXPORT_SYMBOL_GPL(lzo1x_1_compress);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZO1X-1 Compressor");
