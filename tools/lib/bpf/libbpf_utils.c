// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 * Copyright (C) 2017 Nicira, Inc.
 */

#undef _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/kernel.h>

#include "libbpf.h"
#include "libbpf_internal.h"

#ifndef ENOTSUPP
#define ENOTSUPP	524
#endif

/* make sure libbpf doesn't use kernel-only integer typedefs */
#pragma GCC poison u8 u16 u32 u64 s8 s16 s32 s64

#define ERRNO_OFFSET(e)		((e) - __LIBBPF_ERRNO__START)
#define ERRCODE_OFFSET(c)	ERRNO_OFFSET(LIBBPF_ERRNO__##c)
#define NR_ERRNO	(__LIBBPF_ERRNO__END - __LIBBPF_ERRNO__START)

static const char *libbpf_strerror_table[NR_ERRNO] = {
	[ERRCODE_OFFSET(LIBELF)]	= "Something wrong in libelf",
	[ERRCODE_OFFSET(FORMAT)]	= "BPF object format invalid",
	[ERRCODE_OFFSET(KVERSION)]	= "'version' section incorrect or lost",
	[ERRCODE_OFFSET(ENDIAN)]	= "Endian mismatch",
	[ERRCODE_OFFSET(INTERNAL)]	= "Internal error in libbpf",
	[ERRCODE_OFFSET(RELOC)]		= "Relocation failed",
	[ERRCODE_OFFSET(VERIFY)]	= "Kernel verifier blocks program loading",
	[ERRCODE_OFFSET(PROG2BIG)]	= "Program too big",
	[ERRCODE_OFFSET(KVER)]		= "Incorrect kernel version",
	[ERRCODE_OFFSET(PROGTYPE)]	= "Kernel doesn't support this program type",
	[ERRCODE_OFFSET(WRNGPID)]	= "Wrong pid in netlink message",
	[ERRCODE_OFFSET(INVSEQ)]	= "Invalid netlink sequence",
	[ERRCODE_OFFSET(NLPARSE)]	= "Incorrect netlink message parsing",
};

int libbpf_strerror(int err, char *buf, size_t size)
{
	int ret;

	if (!buf || !size)
		return libbpf_err(-EINVAL);

	err = err > 0 ? err : -err;

	if (err < __LIBBPF_ERRNO__START) {
		ret = strerror_r(err, buf, size);
		buf[size - 1] = '\0';
		return libbpf_err_errno(ret);
	}

	if (err < __LIBBPF_ERRNO__END) {
		const char *msg;

		msg = libbpf_strerror_table[ERRNO_OFFSET(err)];
		ret = snprintf(buf, size, "%s", msg);
		buf[size - 1] = '\0';
		/* The length of the buf and msg is positive.
		 * A negative number may be returned only when the
		 * size exceeds INT_MAX. Not likely to appear.
		 */
		if (ret >= size)
			return libbpf_err(-ERANGE);
		return 0;
	}

	ret = snprintf(buf, size, "Unknown libbpf error %d", err);
	buf[size - 1] = '\0';
	if (ret >= size)
		return libbpf_err(-ERANGE);
	return libbpf_err(-ENOENT);
}

const char *libbpf_errstr(int err)
{
	static __thread char buf[12];

	if (err > 0)
		err = -err;

	switch (err) {
	case -E2BIG:		return "-E2BIG";
	case -EACCES:		return "-EACCES";
	case -EADDRINUSE:	return "-EADDRINUSE";
	case -EADDRNOTAVAIL:	return "-EADDRNOTAVAIL";
	case -EAGAIN:		return "-EAGAIN";
	case -EALREADY:		return "-EALREADY";
	case -EBADF:		return "-EBADF";
	case -EBADFD:		return "-EBADFD";
	case -EBUSY:		return "-EBUSY";
	case -ECANCELED:	return "-ECANCELED";
	case -ECHILD:		return "-ECHILD";
	case -EDEADLK:		return "-EDEADLK";
	case -EDOM:		return "-EDOM";
	case -EEXIST:		return "-EEXIST";
	case -EFAULT:		return "-EFAULT";
	case -EFBIG:		return "-EFBIG";
	case -EILSEQ:		return "-EILSEQ";
	case -EINPROGRESS:	return "-EINPROGRESS";
	case -EINTR:		return "-EINTR";
	case -EINVAL:		return "-EINVAL";
	case -EIO:		return "-EIO";
	case -EISDIR:		return "-EISDIR";
	case -ELOOP:		return "-ELOOP";
	case -EMFILE:		return "-EMFILE";
	case -EMLINK:		return "-EMLINK";
	case -EMSGSIZE:		return "-EMSGSIZE";
	case -ENAMETOOLONG:	return "-ENAMETOOLONG";
	case -ENFILE:		return "-ENFILE";
	case -ENODATA:		return "-ENODATA";
	case -ENODEV:		return "-ENODEV";
	case -ENOENT:		return "-ENOENT";
	case -ENOEXEC:		return "-ENOEXEC";
	case -ENOLINK:		return "-ENOLINK";
	case -ENOMEM:		return "-ENOMEM";
	case -ENOSPC:		return "-ENOSPC";
	case -ENOTBLK:		return "-ENOTBLK";
	case -ENOTDIR:		return "-ENOTDIR";
	case -ENOTSUPP:		return "-ENOTSUPP";
	case -ENOTTY:		return "-ENOTTY";
	case -ENXIO:		return "-ENXIO";
	case -EOPNOTSUPP:	return "-EOPNOTSUPP";
	case -EOVERFLOW:	return "-EOVERFLOW";
	case -EPERM:		return "-EPERM";
	case -EPIPE:		return "-EPIPE";
	case -EPROTO:		return "-EPROTO";
	case -EPROTONOSUPPORT:	return "-EPROTONOSUPPORT";
	case -ERANGE:		return "-ERANGE";
	case -EROFS:		return "-EROFS";
	case -ESPIPE:		return "-ESPIPE";
	case -ESRCH:		return "-ESRCH";
	case -ETXTBSY:		return "-ETXTBSY";
	case -EUCLEAN:		return "-EUCLEAN";
	case -EXDEV:		return "-EXDEV";
	default:
		snprintf(buf, sizeof(buf), "%d", err);
		return buf;
	}
}

static inline __u32 get_unaligned_be32(const void *p)
{
	__be32 val;

	memcpy(&val, p, sizeof(val));
	return be32_to_cpu(val);
}

static inline void put_unaligned_be32(__u32 val, void *p)
{
	__be32 be_val = cpu_to_be32(val);

	memcpy(p, &be_val, sizeof(be_val));
}

#define SHA256_BLOCK_LENGTH 64
#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define Sigma_0(x) (ror32((x), 2) ^ ror32((x), 13) ^ ror32((x), 22))
#define Sigma_1(x) (ror32((x), 6) ^ ror32((x), 11) ^ ror32((x), 25))
#define sigma_0(x) (ror32((x), 7) ^ ror32((x), 18) ^ ((x) >> 3))
#define sigma_1(x) (ror32((x), 17) ^ ror32((x), 19) ^ ((x) >> 10))

static const __u32 sha256_K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define SHA256_ROUND(i, a, b, c, d, e, f, g, h)                                \
	{                                                                      \
		__u32 tmp = h + Sigma_1(e) + Ch(e, f, g) + sha256_K[i] + w[i]; \
		d += tmp;                                                      \
		h = tmp + Sigma_0(a) + Maj(a, b, c);                           \
	}

static void sha256_blocks(__u32 state[8], const __u8 *data, size_t nblocks)
{
	while (nblocks--) {
		__u32 a = state[0];
		__u32 b = state[1];
		__u32 c = state[2];
		__u32 d = state[3];
		__u32 e = state[4];
		__u32 f = state[5];
		__u32 g = state[6];
		__u32 h = state[7];
		__u32 w[64];
		int i;

		for (i = 0; i < 16; i++)
			w[i] = get_unaligned_be32(&data[4 * i]);
		for (; i < ARRAY_SIZE(w); i++)
			w[i] = sigma_1(w[i - 2]) + w[i - 7] +
			       sigma_0(w[i - 15]) + w[i - 16];
		for (i = 0; i < ARRAY_SIZE(w); i += 8) {
			SHA256_ROUND(i + 0, a, b, c, d, e, f, g, h);
			SHA256_ROUND(i + 1, h, a, b, c, d, e, f, g);
			SHA256_ROUND(i + 2, g, h, a, b, c, d, e, f);
			SHA256_ROUND(i + 3, f, g, h, a, b, c, d, e);
			SHA256_ROUND(i + 4, e, f, g, h, a, b, c, d);
			SHA256_ROUND(i + 5, d, e, f, g, h, a, b, c);
			SHA256_ROUND(i + 6, c, d, e, f, g, h, a, b);
			SHA256_ROUND(i + 7, b, c, d, e, f, g, h, a);
		}
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
		data += SHA256_BLOCK_LENGTH;
	}
}

void libbpf_sha256(const void *data, size_t len, __u8 out[SHA256_DIGEST_LENGTH])
{
	__u32 state[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	const __be64 bitcount = cpu_to_be64((__u64)len * 8);
	__u8 final_data[2 * SHA256_BLOCK_LENGTH] = { 0 };
	size_t final_len = len % SHA256_BLOCK_LENGTH;
	int i;

	sha256_blocks(state, data, len / SHA256_BLOCK_LENGTH);

	memcpy(final_data, data + len - final_len, final_len);
	final_data[final_len] = 0x80;
	final_len = roundup(final_len + 9, SHA256_BLOCK_LENGTH);
	memcpy(&final_data[final_len - 8], &bitcount, 8);

	sha256_blocks(state, final_data, final_len / SHA256_BLOCK_LENGTH);

	for (i = 0; i < ARRAY_SIZE(state); i++)
		put_unaligned_be32(state[i], &out[4 * i]);
}
