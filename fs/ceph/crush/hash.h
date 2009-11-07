#ifndef _CRUSH_HASH_H
#define _CRUSH_HASH_H

extern __u32 crush_hash32(__u32 a);
extern __u32 crush_hash32_2(__u32 a, __u32 b);
extern __u32 crush_hash32_3(__u32 a, __u32 b, __u32 c);
extern __u32 crush_hash32_4(__u32 a, __u32 b, __u32 c,
			    __u32 d);
extern __u32 crush_hash32_5(__u32 a, __u32 b, __u32 c,
			    __u32 d, __u32 e);

#endif
