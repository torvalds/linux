/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _OBJTOOL_CHECKSUM_TYPES_H
#define _OBJTOOL_CHECKSUM_TYPES_H

struct sym_checksum {
	u64 addr;
	u64 checksum;
};

#ifdef BUILD_KLP

#include <xxhash.h>

struct checksum {
	XXH3_state_t *state;
	XXH64_hash_t checksum;
};

#else /* !BUILD_KLP */

struct checksum {};

#endif /* !BUILD_KLP */

#endif /* _OBJTOOL_CHECKSUM_TYPES_H */
