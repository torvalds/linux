#ifndef _IPT_SCTP_H_
#define _IPT_SCTP_H_

#define IPT_SCTP_SRC_PORTS	        0x01
#define IPT_SCTP_DEST_PORTS	        0x02
#define IPT_SCTP_CHUNK_TYPES		0x04

#define IPT_SCTP_VALID_FLAGS		0x07

#define ELEMCOUNT(x) (sizeof(x)/sizeof(x[0]))


struct ipt_sctp_flag_info {
	u_int8_t chunktype;
	u_int8_t flag;
	u_int8_t flag_mask;
};

#define IPT_NUM_SCTP_FLAGS	4

struct ipt_sctp_info {
	u_int16_t dpts[2];  /* Min, Max */
	u_int16_t spts[2];  /* Min, Max */

	u_int32_t chunkmap[256 / sizeof (u_int32_t)];  /* Bit mask of chunks to be matched according to RFC 2960 */

#define SCTP_CHUNK_MATCH_ANY   0x01  /* Match if any of the chunk types are present */
#define SCTP_CHUNK_MATCH_ALL   0x02  /* Match if all of the chunk types are present */
#define SCTP_CHUNK_MATCH_ONLY  0x04  /* Match if these are the only chunk types present */

	u_int32_t chunk_match_type;
	struct ipt_sctp_flag_info flag_info[IPT_NUM_SCTP_FLAGS];
	int flag_count;

	u_int32_t flags;
	u_int32_t invflags;
};

#define bytes(type) (sizeof(type) * 8)

#define SCTP_CHUNKMAP_SET(chunkmap, type) 		\
	do { 						\
		chunkmap[type / bytes(u_int32_t)] |= 	\
			1 << (type % bytes(u_int32_t));	\
	} while (0)

#define SCTP_CHUNKMAP_CLEAR(chunkmap, type)		 	\
	do {							\
		chunkmap[type / bytes(u_int32_t)] &= 		\
			~(1 << (type % bytes(u_int32_t)));	\
	} while (0)

#define SCTP_CHUNKMAP_IS_SET(chunkmap, type) 			\
({								\
	(chunkmap[type / bytes (u_int32_t)] & 			\
		(1 << (type % bytes (u_int32_t)))) ? 1: 0;	\
})

#define SCTP_CHUNKMAP_RESET(chunkmap) 				\
	do {							\
		int i; 						\
		for (i = 0; i < ELEMCOUNT(chunkmap); i++)	\
			chunkmap[i] = 0;			\
	} while (0)

#define SCTP_CHUNKMAP_SET_ALL(chunkmap) 			\
	do {							\
		int i; 						\
		for (i = 0; i < ELEMCOUNT(chunkmap); i++) 	\
			chunkmap[i] = ~0;			\
	} while (0)

#define SCTP_CHUNKMAP_COPY(destmap, srcmap) 			\
	do {							\
		int i; 						\
		for (i = 0; i < ELEMCOUNT(chunkmap); i++) 	\
			destmap[i] = srcmap[i];			\
	} while (0)

#define SCTP_CHUNKMAP_IS_CLEAR(chunkmap) 		\
({							\
	int i; 						\
	int flag = 1;					\
	for (i = 0; i < ELEMCOUNT(chunkmap); i++) {	\
		if (chunkmap[i]) {			\
			flag = 0;			\
			break;				\
		}					\
	}						\
        flag;						\
})

#define SCTP_CHUNKMAP_IS_ALL_SET(chunkmap) 		\
({							\
	int i; 						\
	int flag = 1;					\
	for (i = 0; i < ELEMCOUNT(chunkmap); i++) {	\
		if (chunkmap[i] != ~0) {		\
			flag = 0;			\
				break;			\
		}					\
	}						\
        flag;						\
})

#endif /* _IPT_SCTP_H_ */

