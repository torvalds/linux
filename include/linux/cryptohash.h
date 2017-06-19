#ifndef __CRYPTOHASH_H
#define __CRYPTOHASH_H

#include <uapi/linux/types.h>

#define SHA_DIGEST_WORDS 5
#define SHA_MESSAGE_BYTES (512 /*bits*/ / 8)
#define SHA_WORKSPACE_WORDS 16

void sha_init(__u32 *buf);
void sha_transform(__u32 *digest, const char *data, __u32 *W);

#endif
