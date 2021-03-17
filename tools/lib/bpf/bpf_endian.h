/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_ENDIAN__
#define __BPF_ENDIAN__

/*
 * Isolate byte #n and put it into byte #m, for __u##b type.
 * E.g., moving byte #6 (nnnnnnnn) into byte #1 (mmmmmmmm) for __u64:
 * 1) xxxxxxxx nnnnnnnn xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx mmmmmmmm xxxxxxxx
 * 2) nnnnnnnn xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx mmmmmmmm xxxxxxxx 00000000
 * 3) 00000000 00000000 00000000 00000000 00000000 00000000 00000000 nnnnnnnn
 * 4) 00000000 00000000 00000000 00000000 00000000 00000000 nnnnnnnn 00000000
 */
#define ___bpf_mvb(x, b, n, m) ((__u##b)(x) << (b-(n+1)*8) >> (b-8) << (m*8))

#define ___bpf_swab16(x) ((__u16)(			\
			  ___bpf_mvb(x, 16, 0, 1) |	\
			  ___bpf_mvb(x, 16, 1, 0)))

#define ___bpf_swab32(x) ((__u32)(			\
			  ___bpf_mvb(x, 32, 0, 3) |	\
			  ___bpf_mvb(x, 32, 1, 2) |	\
			  ___bpf_mvb(x, 32, 2, 1) |	\
			  ___bpf_mvb(x, 32, 3, 0)))

#define ___bpf_swab64(x) ((__u64)(			\
			  ___bpf_mvb(x, 64, 0, 7) |	\
			  ___bpf_mvb(x, 64, 1, 6) |	\
			  ___bpf_mvb(x, 64, 2, 5) |	\
			  ___bpf_mvb(x, 64, 3, 4) |	\
			  ___bpf_mvb(x, 64, 4, 3) |	\
			  ___bpf_mvb(x, 64, 5, 2) |	\
			  ___bpf_mvb(x, 64, 6, 1) |	\
			  ___bpf_mvb(x, 64, 7, 0)))

/* LLVM's BPF target selects the endianness of the CPU
 * it compiles on, or the user specifies (bpfel/bpfeb),
 * respectively. The used __BYTE_ORDER__ is defined by
 * the compiler, we cannot rely on __BYTE_ORDER from
 * libc headers, since it doesn't reflect the actual
 * requested byte order.
 *
 * Note, LLVM's BPF target has different __builtin_bswapX()
 * semantics. It does map to BPF_ALU | BPF_END | BPF_TO_BE
 * in bpfel and bpfeb case, which means below, that we map
 * to cpu_to_be16(). We could use it unconditionally in BPF
 * case, but better not rely on it, so that this header here
 * can be used from application and BPF program side, which
 * use different targets.
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define __bpf_ntohs(x)			__builtin_bswap16(x)
# define __bpf_htons(x)			__builtin_bswap16(x)
# define __bpf_constant_ntohs(x)	___bpf_swab16(x)
# define __bpf_constant_htons(x)	___bpf_swab16(x)
# define __bpf_ntohl(x)			__builtin_bswap32(x)
# define __bpf_htonl(x)			__builtin_bswap32(x)
# define __bpf_constant_ntohl(x)	___bpf_swab32(x)
# define __bpf_constant_htonl(x)	___bpf_swab32(x)
# define __bpf_be64_to_cpu(x)		__builtin_bswap64(x)
# define __bpf_cpu_to_be64(x)		__builtin_bswap64(x)
# define __bpf_constant_be64_to_cpu(x)	___bpf_swab64(x)
# define __bpf_constant_cpu_to_be64(x)	___bpf_swab64(x)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define __bpf_ntohs(x)			(x)
# define __bpf_htons(x)			(x)
# define __bpf_constant_ntohs(x)	(x)
# define __bpf_constant_htons(x)	(x)
# define __bpf_ntohl(x)			(x)
# define __bpf_htonl(x)			(x)
# define __bpf_constant_ntohl(x)	(x)
# define __bpf_constant_htonl(x)	(x)
# define __bpf_be64_to_cpu(x)		(x)
# define __bpf_cpu_to_be64(x)		(x)
# define __bpf_constant_be64_to_cpu(x)  (x)
# define __bpf_constant_cpu_to_be64(x)  (x)
#else
# error "Fix your compiler's __BYTE_ORDER__?!"
#endif

#define bpf_htons(x)				\
	(__builtin_constant_p(x) ?		\
	 __bpf_constant_htons(x) : __bpf_htons(x))
#define bpf_ntohs(x)				\
	(__builtin_constant_p(x) ?		\
	 __bpf_constant_ntohs(x) : __bpf_ntohs(x))
#define bpf_htonl(x)				\
	(__builtin_constant_p(x) ?		\
	 __bpf_constant_htonl(x) : __bpf_htonl(x))
#define bpf_ntohl(x)				\
	(__builtin_constant_p(x) ?		\
	 __bpf_constant_ntohl(x) : __bpf_ntohl(x))
#define bpf_cpu_to_be64(x)			\
	(__builtin_constant_p(x) ?		\
	 __bpf_constant_cpu_to_be64(x) : __bpf_cpu_to_be64(x))
#define bpf_be64_to_cpu(x)			\
	(__builtin_constant_p(x) ?		\
	 __bpf_constant_be64_to_cpu(x) : __bpf_be64_to_cpu(x))

#endif /* __BPF_ENDIAN__ */
