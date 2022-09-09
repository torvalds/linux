/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_SCTP_H_
#define _XT_SCTP_H_

#include <linux/types.h>

#define XT_SCTP_SRC_PORTS	        0x01
#define XT_SCTP_DEST_PORTS	        0x02
#define XT_SCTP_CHUNK_TYPES		0x04

#define XT_SCTP_VALID_FLAGS		0x07

struct xt_sctp_flag_info {
	__u8 chunktype;
	__u8 flag;
	__u8 flag_mask;
};

#define XT_NUM_SCTP_FLAGS	4

struct xt_sctp_info {
	__u16 dpts[2];  /* Min, Max */
	__u16 spts[2];  /* Min, Max */

	__u32 chunkmap[256 / sizeof (__u32)];  /* Bit mask of chunks to be matched according to RFC 2960 */

#define SCTP_CHUNK_MATCH_ANY   0x01  /* Match if any of the chunk types are present */
#define SCTP_CHUNK_MATCH_ALL   0x02  /* Match if all of the chunk types are present */
#define SCTP_CHUNK_MATCH_ONLY  0x04  /* Match if these are the only chunk types present */

	__u32 chunk_match_type;
	struct xt_sctp_flag_info flag_info[XT_NUM_SCTP_FLAGS];
	int flag_count;

	__u32 flags;
	__u32 invflags;
};

#define bytes(type) (sizeof(type) * 8)

#define SCTP_CHUNKMAP_SET(chunkmap, type) 		\
	do { 						\
		(chunkmap)[type / bytes(__u32)] |= 	\
			1u << (type % bytes(__u32));	\
	} while (0)

#define SCTP_CHUNKMAP_CLEAR(chunkmap, type)		 	\
	do {							\
		(chunkmap)[type / bytes(__u32)] &= 		\
			~(1u << (type % bytes(__u32)));	\
	} while (0)

#define SCTP_CHUNKMAP_IS_SET(chunkmap, type) 			\
({								\
	((chunkmap)[type / bytes (__u32)] & 		\
		(1u << (type % bytes (__u32)))) ? 1: 0;	\
})

#define SCTP_CHUNKMAP_RESET(chunkmap) \
	memset((chunkmap), 0, sizeof(chunkmap))

#define SCTP_CHUNKMAP_SET_ALL(chunkmap) \
	memset((chunkmap), ~0U, sizeof(chunkmap))

#define SCTP_CHUNKMAP_COPY(destmap, srcmap) \
	memcpy((destmap), (srcmap), sizeof(srcmap))

#define SCTP_CHUNKMAP_IS_CLEAR(chunkmap) \
	__sctp_chunkmap_is_clear((chunkmap), ARRAY_SIZE(chunkmap))
static inline _Bool
__sctp_chunkmap_is_clear(const __u32 *chunkmap, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; ++i)
		if (chunkmap[i])
			return 0;
	return 1;
}

#define SCTP_CHUNKMAP_IS_ALL_SET(chunkmap) \
	__sctp_chunkmap_is_all_set((chunkmap), ARRAY_SIZE(chunkmap))
static inline _Bool
__sctp_chunkmap_is_all_set(const __u32 *chunkmap, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; ++i)
		if (chunkmap[i] != ~0U)
			return 0;
	return 1;
}

#endif /* _XT_SCTP_H_ */

