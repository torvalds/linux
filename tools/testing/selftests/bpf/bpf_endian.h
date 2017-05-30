#ifndef __BPF_ENDIAN__
#define __BPF_ENDIAN__

#include <asm/byteorder.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define __bpf_ntohs(x)		__builtin_bswap16(x)
# define __bpf_htons(x)		__builtin_bswap16(x)
#elif __BYTE_ORDER == __BIG_ENDIAN
# define __bpf_ntohs(x)		(x)
# define __bpf_htons(x)		(x)
#else
# error "Fix your __BYTE_ORDER?!"
#endif

#define bpf_htons(x)				\
	(__builtin_constant_p(x) ?		\
	 __constant_htons(x) : __bpf_htons(x))
#define bpf_ntohs(x)				\
	(__builtin_constant_p(x) ?		\
	 __constant_ntohs(x) : __bpf_ntohs(x))

#endif
