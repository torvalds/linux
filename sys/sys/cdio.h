/*	$OpenBSD: cdio.h,v 1.17 2017/10/24 09:36:13 jsg Exp $	*/
/*	$NetBSD: cdio.h,v 1.11 1996/02/19 18:29:04 scottr Exp $	*/

#ifndef _SYS_CDIO_H_
#define _SYS_CDIO_H_

#include <sys/types.h>
#include <sys/ioccom.h>

/* Shared between kernel & process */

union msf_lba {
	struct {
		u_char unused;
		u_char minute;
		u_char second;
		u_char frame;
	} msf;
	u_int32_t	lba;
	u_char		addr[4];
};

struct cd_toc_entry {
	u_char	nothing1;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	control:4;
	u_int	addr_type:4;
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int	addr_type:4;
	u_int	control:4;
#endif
	u_char	track;
	u_char	nothing2;
	union msf_lba addr;
};

struct cd_sub_channel_header {
	u_char	nothing1;
	u_char	audio_status;
#define CD_AS_AUDIO_INVALID	0x00
#define CD_AS_PLAY_IN_PROGRESS	0x11
#define CD_AS_PLAY_PAUSED	0x12
#define CD_AS_PLAY_COMPLETED	0x13
#define CD_AS_PLAY_ERROR	0x14
#define CD_AS_NO_STATUS		0x15
	u_char	data_len[2];
};

struct cd_sub_channel_q_data {
	u_char	data_format;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	control:4;
	u_int	addr_type:4;
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int	addr_type:4;
	u_int	control:4;
#endif
	u_char	track_number;
	u_char	index_number;
	u_char	absaddr[4];
	u_char	reladdr[4];
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	:7;
	u_int	mc_valid:1;
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int	mc_valid:1;
	u_int	:7;
#endif
	u_char	mc_number[15]; 
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	:7;
	u_int	ti_valid:1;   
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int	ti_valid:1;   
	u_int	:7;
#endif
	u_char	ti_number[15]; 
};

struct cd_sub_channel_position_data {
	u_char	data_format;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	control:4;
	u_int	addr_type:4;
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int	addr_type:4;
	u_int	control:4;
#endif
	u_char	track_number;
	u_char	index_number;
	union msf_lba absaddr;
	union msf_lba reladdr;
};

struct cd_sub_channel_media_catalog {
	u_char	data_format;
	u_char	nothing1;
	u_char	nothing2;
	u_char	nothing3;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	:7;
	u_int	mc_valid:1;
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int	mc_valid:1;
	u_int	:7;
#endif
	u_char	mc_number[15];
};

struct cd_sub_channel_track_info {
	u_char	data_format;
	u_char	nothing1;
	u_char	track_number;
	u_char	nothing2;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	:7;
	u_int	ti_valid:1;
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int	ti_valid:1;
	u_int	:7;
#endif
	u_char	ti_number[15];
};

struct cd_sub_channel_info {
	struct cd_sub_channel_header header;
	union {
		struct cd_sub_channel_q_data q_data;
		struct cd_sub_channel_position_data position;
		struct cd_sub_channel_media_catalog media_catalog;
		struct cd_sub_channel_track_info track_info;
	} what;
};

/*
 * Ioctls for the CD drive
 */
struct ioc_play_track {
	u_char	start_track;
	u_char	start_index;
	u_char	end_track;
	u_char	end_index;
};

#define	CDIOCPLAYTRACKS	_IOW('c', 1, struct ioc_play_track)
struct ioc_play_blocks {
	int	blk;
	int	len;
};
#define	CDIOCPLAYBLOCKS	_IOW('c', 2, struct ioc_play_blocks)

struct ioc_read_subchannel {
	u_char	address_format;
#define CD_LBA_FORMAT		1
#define CD_MSF_FORMAT		2
	u_char	data_format;
#define CD_SUBQ_DATA		0
#define CD_CURRENT_POSITION	1
#define CD_MEDIA_CATALOG	2
#define CD_TRACK_INFO		3
	u_char	track;
	int	data_len;
	struct	cd_sub_channel_info *data;
};
#define CDIOCREADSUBCHANNEL _IOWR('c', 3, struct ioc_read_subchannel)

struct ioc_toc_header {
	u_short	len;
	u_char	starting_track;
	u_char	ending_track;
};

#define CDIOREADTOCHEADER _IOR('c', 4, struct ioc_toc_header)

struct ioc_read_toc_entry {
	u_char	address_format;
	u_char	starting_track;
#define CD_TRACK_LEADOUT	0xaa
	u_short	data_len;
	struct	cd_toc_entry *data;
};
#define CDIOREADTOCENTRIES _IOWR('c', 5, struct ioc_read_toc_entry)
#define CDIOREADTOCENTRYS CDIOREADTOCENTRIES

/* read LBA start of a given session; 0=last, others not yet supported */
#define CDIOREADMSADDR _IOWR('c', 6, int)

struct	ioc_patch {
	u_char	patch[4];	/* one for each channel */
};
#define	CDIOCSETPATCH	_IOW('c', 9, struct ioc_patch)

struct	ioc_vol {
	u_char	vol[4];	/* one for each channel */
};
#define	CDIOCGETVOL	_IOR('c', 10, struct ioc_vol)
#define	CDIOCSETVOL	_IOW('c', 11, struct ioc_vol)
#define	CDIOCSETMONO	_IO('c', 12)
#define	CDIOCSETSTEREO	_IO('c', 13)
#define	CDIOCSETMUTE	_IO('c', 14)
#define	CDIOCSETLEFT	_IO('c', 15)
#define	CDIOCSETRIGHT	_IO('c', 16)
#define	CDIOCSETDEBUG	_IO('c', 17)
#define	CDIOCCLRDEBUG	_IO('c', 18)
#define	CDIOCPAUSE	_IO('c', 19)
#define	CDIOCRESUME	_IO('c', 20)
#define	CDIOCRESET	_IO('c', 21)
#define	CDIOCSTART	_IO('c', 22)
#define	CDIOCSTOP	_IO('c', 23)
#define	CDIOCEJECT	_IO('c', 24)
#define	CDIOCALLOW	_IO('c', 25)
#define	CDIOCPREVENT	_IO('c', 26)
#define	CDIOCCLOSE	_IO('c', 27)

struct ioc_play_msf {
	u_char	start_m;
	u_char	start_s;
	u_char	start_f;
	u_char	end_m;
	u_char	end_s;
	u_char	end_f;
};
#define	CDIOCPLAYMSF	_IOW('c', 25, struct ioc_play_msf)

struct ioc_load_unload {
	u_char options;
#define	CD_LU_ABORT	0x1	/* NOTE: These are the same as the ATAPI */
#define	CD_LU_UNLOAD	0x2	/* op values for the LOAD_UNLOAD command */
#define	CD_LU_LOAD	0x3
	u_char slot;
};
#define		CDIOCLOADUNLOAD	_IOW('c', 26, struct ioc_load_unload)

/* DVD definitions */

/* DVD-ROM Specific ioctls */
#define DVD_READ_STRUCT		_IOWR('d', 0, union dvd_struct)
#define DVD_WRITE_STRUCT	_IOWR('d', 1, union dvd_struct)
#define DVD_AUTH		_IOWR('d', 2, union dvd_authinfo)

#define GPCMD_READ_DVD_STRUCTURE	0xad
#define GPCMD_SEND_DVD_STRUCTURE	0xad
#define GPCMD_REPORT_KEY		0xa4
#define GPCMD_SEND_KEY			0xa3

/* DVD struct types */
#define DVD_STRUCT_PHYSICAL		0x00
#define DVD_STRUCT_COPYRIGHT		0x01
#define DVD_STRUCT_DISCKEY		0x02
#define DVD_STRUCT_BCA			0x03
#define DVD_STRUCT_MANUFACT		0x04

struct dvd_layer {
	u_int8_t book_version;
	u_int8_t book_type;
	u_int8_t min_rate;
	u_int8_t disc_size;
	u_int8_t layer_type;
	u_int8_t track_path;
	u_int8_t nlayers;
	u_int8_t track_density;
	u_int8_t linear_density;
	u_int8_t bca;
	u_int32_t start_sector;
	u_int32_t end_sector;
	u_int32_t end_sector_l0;
};
 
struct dvd_physical {
	u_int8_t type;

	u_int8_t layer_num;
	struct dvd_layer layer[4];
};

struct dvd_copyright {
	u_int8_t type;

	u_int8_t layer_num;
	u_int8_t cpst;
	u_int8_t rmi;
};

struct dvd_disckey {
	u_int8_t type;

	u_int8_t agid;
	u_int8_t value[2048];
};

struct dvd_bca {
	u_int8_t type;

	int len;
	u_int8_t value[188];
};

struct dvd_manufact {
	u_int8_t type;

	u_int8_t layer_num;
	int len;
	u_int8_t value[2048];
};

union dvd_struct {
	u_int8_t type;

	struct dvd_physical	physical;
	struct dvd_copyright	copyright;
	struct dvd_disckey	disckey;
	struct dvd_bca		bca;
        struct dvd_manufact	manufact;
};

/*
 * DVD authentication ioctl
 */

/* Authentication states */
#define DVD_LU_SEND_AGID	0
#define DVD_HOST_SEND_CHALLENGE	1
#define DVD_LU_SEND_KEY1	2
#define DVD_LU_SEND_CHALLENGE	3
#define DVD_HOST_SEND_KEY2	4

/* Termination states */
#define DVD_AUTH_ESTABLISHED	5
#define DVD_AUTH_FAILURE	6

/* Other functions */
#define DVD_LU_SEND_TITLE_KEY	7
#define DVD_LU_SEND_ASF		8
#define DVD_INVALIDATE_AGID	9
#define DVD_LU_SEND_RPC_STATE	10
#define DVD_HOST_SEND_RPC_STATE	11

#if 0
/* State data */
typedef u_int8_t dvd_key[5];		/* 40-bit value, MSB is first elem. */
typedef u_int8_t dvd_challenge[10];	/* 80-bit value, MSB is first elem. */
#endif

#define DVD_KEY_SIZE		5
#define DVD_CHALLENGE_SIZE	10

struct dvd_lu_send_agid {
	u_int8_t type;

	u_int8_t agid;
};

struct dvd_host_send_challenge {
	u_int8_t type;

	u_int8_t agid;
	u_int8_t chal[DVD_CHALLENGE_SIZE];
};

struct dvd_send_key {
	u_int8_t type;

	u_int8_t agid;
	u_int8_t key[DVD_KEY_SIZE];
};

struct dvd_lu_send_challenge {
	u_int8_t type;

	u_int8_t agid;
	u_int8_t chal[DVD_CHALLENGE_SIZE];
};

#define DVD_CPM_NO_COPYRIGHT	0
#define DVD_CPM_COPYRIGHTED	1

#define DVD_CP_SEC_NONE		0
#define DVD_CP_SEC_EXIST	1

#define DVD_CGMS_UNRESTRICTED	0
#define DVD_CGMS_SINGLE		2
#define DVD_CGMS_RESTRICTED	3

struct dvd_lu_send_title_key {
	u_int8_t type;

	u_int8_t agid;
	u_int8_t title_key[DVD_KEY_SIZE];
	int lba;
	u_int8_t cpm;
	u_int8_t cp_sec;
	u_int8_t cgms;
};

struct dvd_lu_send_asf {
	u_int8_t type;

	u_int8_t agid;
	u_int8_t asf;
};

struct dvd_host_send_rpcstate {
	u_int8_t type;
	u_int8_t pdrc;
};

struct dvd_lu_send_rpcstate {
	u_int8_t type;
	u_int8_t vra;
	u_int8_t ucca;
	u_int8_t region_mask;
	u_int8_t rpc_scheme;
      };

union dvd_authinfo {
	u_int8_t type;

	struct dvd_lu_send_agid		lsa;
	struct dvd_host_send_challenge	hsc;
	struct dvd_send_key		lsk;
	struct dvd_lu_send_challenge	lsc;
	struct dvd_send_key		hsk;
	struct dvd_lu_send_title_key	lstk;
	struct dvd_lu_send_asf		lsasf;
	struct dvd_host_send_rpcstate	hrpcs;
	struct dvd_lu_send_rpcstate	lrpcs;
};
#endif /* !_SYS_CDIO_H_ */
