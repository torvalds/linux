#ifndef CEPH_CRUSH_HASH_H
#define CEPH_CRUSH_HASH_H

#define CRUSH_HASH_RJENKINS1   0

#define CRUSH_HASH_DEFAULT CRUSH_HASH_RJENKINS1

extern const char *crush_hash_name(int type);

extern __u32 crush_hash32(int type, __u32 a);
extern __u32 crush_hash32_2(int type, __u32 a, __u32 b);
extern __u32 crush_hash32_3(int type, __u32 a, __u32 b, __u32 c);
extern __u32 crush_hash32_4(int type, __u32 a, __u32 b, __u32 c, __u32 d);
extern __u32 crush_hash32_5(int type, __u32 a, __u32 b, __u32 c, __u32 d,
			    __u32 e);

#endif
