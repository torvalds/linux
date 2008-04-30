#ifndef _XT_SCTP_H_
#define _XT_SCTP_H_

#define XT_SCTP_SRC_PORTS	        0x01
#define XT_SCTP_DEST_PORTS	        0x02
#define XT_SCTP_CHUNK_TYPES		0x04

#define XT_SCTP_VALID_FLAGS		0x07

struct xt_sctp_flag_info {
	u_int8_t chunktype;
	u_int8_t flag;
	u_int8_t flag_mask;
};

#define XT_NUM_SCTP_FLAGS	4

struct xt_sctp_info {
	u_int16_t dpts[2];  /* Min, Max */
	u_int16_t spts[2];  /* Min, Max */

	u_int32_t chunkmap[256 / sizeof (u_int32_t)];  /* Bit mask of chunks to be matched according to RFC 2960 */

#define SCTP_CHUNK_MATCH_ANY   0x01  /* Match if any of the chunk types are present */
#define SCTP_CHUNK_MATCH_ALL   0x02  /* Match if all of the chunk types are present */
#define SCTP_CHUNK_MATCH_ONLY  0x04  /* Match if these are the only chunk types present */

	u_int32_t chunk_match_type;
	struct xt_sctp_flag_info flag_info[XT_NUM_SCTP_FLAGS];
	int flag_count;

	u_int32_t flags;
	u_int32_t invflags;
};

#define bytes(type) (sizeof(type) * 8)

#define SCTP_CHUNKMAP_SET(chunkmap, type) 		\
	do { 						\
		(chunkmap)[type / bytes(u_int32_t)] |= 	\
			1 << (type % bytes(u_int32_t));	\
	} while (0)

#define SCTP_CHUNKMAP_CLEAR(chunkmap, type)		 	\
	do {							\
		(chunkmap)[type / bytes(u_int32_t)] &= 		\
			~(1 << (type % bytes(u_int32_t)));	\
	} while (0)

#define SCTP_CHUNKMAP_IS_SET(chunkmap, type) 			\
({								\
	((chunkmap)[type / bytes (u_int32_t)] & 		\
		(1 << (type % bytes (u_int32_t)))) ? 1: 0;	\
})

#define SCTP_CHUNKMAP_RESET(chunkmap) \
	memset((chunkmap), 0, sizeof(chunkmap))

#define SCTP_CHUNKMAP_SET_ALL(chunkmap) \
	memset((chunkmap), ~0U, sizeof(chunkmap))

#define SCTP_CHUNKMAP_COPY(destmap, srcmap) \
	memcpy((destmap), (srcmap), sizeof(srcmap))

#define SCTP_CHUNKMAP_IS_CLEAR(chunkmap) \
	__sctp_chunkmap_is_clear((chunkmap), ARRAY_SIZE(chunkmap))
static inline bool
__sctp_chunkmap_is_clear(const u_int32_t *chunkmap, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; ++i)
		if (chunkmap[i])
			return false;
	return true;
}

#define SCTP_CHUNKMAP_IS_ALL_SET(chunkmap) \
	__sctp_chunkmap_is_all_set((chunkmap), ARRAY_SIZE(chunkmap))
static inline bool
__sctp_chunkmap_is_all_set(const u_int32_t *chunkmap, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; ++i)
		if (chunkmap[i] != ~0U)
			return false;
	return true;
}

#endif /* _XT_SCTP_H_ */

