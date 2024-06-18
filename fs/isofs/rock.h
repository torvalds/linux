/* SPDX-License-Identifier: GPL-2.0 */
/*
 * These structs are used by the system-use-sharing protocol, in which the
 * Rock Ridge extensions are embedded.  It is quite possible that other
 * extensions are present on the disk, and this is fine as long as they
 * all use SUSP
 */

struct SU_SP_s {
	__u8 magic[2];
	__u8 skip;
} __attribute__ ((packed));

struct SU_CE_s {
	__u8 extent[8];
	__u8 offset[8];
	__u8 size[8];
};

struct SU_ER_s {
	__u8 len_id;
	__u8 len_des;
	__u8 len_src;
	__u8 ext_ver;
	__u8 data[];
} __attribute__ ((packed));

struct RR_RR_s {
	__u8 flags[1];
} __attribute__ ((packed));

struct RR_PX_s {
	__u8 mode[8];
	__u8 n_links[8];
	__u8 uid[8];
	__u8 gid[8];
};

struct RR_PN_s {
	__u8 dev_high[8];
	__u8 dev_low[8];
};

struct SL_component {
	__u8 flags;
	__u8 len;
	__u8 text[];
} __attribute__ ((packed));

struct RR_SL_s {
	__u8 flags;
	struct SL_component link;
} __attribute__ ((packed));

struct RR_NM_s {
	__u8 flags;
	char name[];
} __attribute__ ((packed));

struct RR_CL_s {
	__u8 location[8];
};

struct RR_PL_s {
	__u8 location[8];
};

struct stamp {
	__u8 time[7];		/* actually 6 unsigned, 1 signed */
} __attribute__ ((packed));

struct RR_TF_s {
	__u8 flags;
	struct stamp times[];	/* Variable number of these beasts */
} __attribute__ ((packed));

/* Linux-specific extension for transparent decompression */
struct RR_ZF_s {
	__u8 algorithm[2];
	__u8 parms[2];
	__u8 real_size[8];
};

/*
 * These are the bits and their meanings for flags in the TF structure.
 */
#define TF_CREATE 1
#define TF_MODIFY 2
#define TF_ACCESS 4
#define TF_ATTRIBUTES 8
#define TF_BACKUP 16
#define TF_EXPIRATION 32
#define TF_EFFECTIVE 64
#define TF_LONG_FORM 128

struct rock_ridge {
	__u8 signature[2];
	__u8 len;
	__u8 version;
	union {
		struct SU_SP_s SP;
		struct SU_CE_s CE;
		struct SU_ER_s ER;
		struct RR_RR_s RR;
		struct RR_PX_s PX;
		struct RR_PN_s PN;
		struct RR_SL_s SL;
		struct RR_NM_s NM;
		struct RR_CL_s CL;
		struct RR_PL_s PL;
		struct RR_TF_s TF;
		struct RR_ZF_s ZF;
	} u;
};

#define RR_PX 1			/* POSIX attributes */
#define RR_PN 2			/* POSIX devices */
#define RR_SL 4			/* Symbolic link */
#define RR_NM 8			/* Alternate Name */
#define RR_CL 16		/* Child link */
#define RR_PL 32		/* Parent link */
#define RR_RE 64		/* Relocation directory */
#define RR_TF 128		/* Timestamps */
