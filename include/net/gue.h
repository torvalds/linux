#ifndef __NET_GUE_H
#define __NET_GUE_H

struct guehdr {
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u8	hlen:4,
			version:4;
#elif defined (__BIG_ENDIAN_BITFIELD)
			__u8	version:4,
				hlen:4;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
			__u8    next_hdr;
			__u16   flags;
		};
		__u32 word;
	};
};

#endif
