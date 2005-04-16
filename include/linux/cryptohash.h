#ifndef __CRYPTOHASH_H
#define __CRYPTOHASH_H

#define SHA_DIGEST_WORDS 5
#define SHA_WORKSPACE_WORDS 80

void sha_init(__u32 *buf);
void sha_transform(__u32 *digest, const char *data, __u32 *W);

__u32 half_md4_transform(__u32 buf[4], __u32 const in[8]);

#endif
