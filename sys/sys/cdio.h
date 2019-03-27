/*
 * 16 Feb 93	Julian Elischer	(julian@dialix.oz.au)
 *
 * $FreeBSD$
 */

/*
<1>	Fixed a conflict with ioctl usage.  There were two different
	functions using code #25.  Made file formatting consistent.
	Added two new ioctl codes: door closing and audio pitch playback.
	Added a STEREO union called STEREO.
	5-Mar-95  Frank Durda IV	bsdmail@nemesis.lonestar.org

<2>	Added a new ioctl that allows you to find out what capabilities
	a drive has and what commands it will accept.  This allows a
	user application to only offer controls (buttons, sliders, etc)
	for functions that drive can actually do.   Things it can't do
	can disappear or be greyed-out (like some other system).
	If the driver doesn't respond to this call, well, handle it the
	way you used to do it.
	2-Apr-95  Frank Durda IV	bsdmail@nemesis.lonestar.org
*/

/* Shared between kernel & process */

#ifndef	_SYS_CDIO_H_
#define	_SYS_CDIO_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

union msf_lba {
	struct {
		unsigned char   unused;
		unsigned char   minute;
		unsigned char   second;
		unsigned char   frame;
	} msf;
	int     lba;    /* network byte order */
	u_char	addr[4];
};

struct cd_toc_entry {
#if BYTE_ORDER == LITTLE_ENDIAN
	u_int	:8;
	u_int	control:4;
	u_int	addr_type:4;
#else
	u_int	:8;
	u_int	addr_type:4;
	u_int	control:4;
#endif
	u_char  track;
	u_int	:8;
	union msf_lba  addr;
};

struct cd_sub_channel_header {
	u_int	:8;
	u_char	audio_status;
#define CD_AS_AUDIO_INVALID        0x00
#define CD_AS_PLAY_IN_PROGRESS     0x11
#define CD_AS_PLAY_PAUSED          0x12
#define CD_AS_PLAY_COMPLETED       0x13
#define CD_AS_PLAY_ERROR           0x14
#define CD_AS_NO_STATUS            0x15
	u_char	data_len[2];
};

struct cd_sub_channel_position_data {
	u_char	data_format;
	u_int	control:4;
	u_int	addr_type:4;
	u_char	track_number;
	u_char	index_number;
	union msf_lba  absaddr;
	union msf_lba  reladdr;
};

struct cd_sub_channel_media_catalog {
        u_char  data_format;
        u_int   :8;
        u_int   :8;
        u_int   :8;
        u_int   :7;
        u_int   mc_valid:1;
        u_char  mc_number[15];
};

struct cd_sub_channel_track_info {
        u_char  data_format;
        u_int   :8;
        u_char  track_number;
        u_int   :8;
        u_int   :7;
        u_int   ti_valid:1;
        u_char  ti_number[15];
};

struct cd_sub_channel_info {
	struct cd_sub_channel_header header;
	union {
		struct cd_sub_channel_position_data position;
		struct cd_sub_channel_media_catalog media_catalog;
		struct cd_sub_channel_track_info track_info;
	} what;
};


/***************************************************************\
* Ioctls for the CD drive					*
\***************************************************************/

struct ioc_play_track {
	u_char	start_track;
	u_char	start_index;
	u_char	end_track;
	u_char	end_index;
};
#define	CDIOCPLAYTRACKS	_IOW('c',1,struct ioc_play_track)


struct ioc_play_blocks {
	int	blk;
	int	len;
};
#define	CDIOCPLAYBLOCKS	_IOW('c',2,struct ioc_play_blocks)


struct ioc_read_subchannel {
	u_char address_format;
#define CD_LBA_FORMAT	1
#define CD_MSF_FORMAT	2
	u_char data_format;
#define CD_SUBQ_DATA		0
#define CD_CURRENT_POSITION	1
#define CD_MEDIA_CATALOG	2
#define CD_TRACK_INFO		3
	u_char track;
	int	data_len;
	struct  cd_sub_channel_info *data;
};
#define CDIOCREADSUBCHANNEL _IOWR('c', 3 , struct ioc_read_subchannel )


struct ioc_toc_header {
	u_short len;
	u_char  starting_track;
	u_char  ending_track;
};
#define CDIOREADTOCHEADER _IOR('c',4,struct ioc_toc_header)


struct ioc_read_toc_entry {
	u_char	address_format;
	u_char	starting_track;
	u_short	data_len;
	struct  cd_toc_entry *data;
};
#define CDIOREADTOCENTRYS _IOWR('c',5,struct ioc_read_toc_entry)


struct ioc_read_toc_single_entry {
	u_char	address_format;
	u_char	track;
	struct  cd_toc_entry entry;
};
#define CDIOREADTOCENTRY _IOWR('c',6,struct ioc_read_toc_single_entry)


struct ioc_patch {
	u_char	patch[4];	/* one for each channel */
};
#define	CDIOCSETPATCH	_IOW('c',9,struct ioc_patch)


struct ioc_vol {
	u_char	vol[4];	/* one for each channel */
};
#define	CDIOCGETVOL	_IOR('c',10,struct ioc_vol)

#define	CDIOCSETVOL	_IOW('c',11,struct ioc_vol)

#define	CDIOCSETMONO	_IO('c',12)

#define	CDIOCSETSTERIO	_IO('c',13)
#define	CDIOCSETSTEREO	_IO('c',13)

#define	CDIOCSETMUTE	_IO('c',14)

#define	CDIOCSETLEFT	_IO('c',15)

#define	CDIOCSETRIGHT	_IO('c',16)

#define	CDIOCSETDEBUG	_IO('c',17)

#define	CDIOCCLRDEBUG	_IO('c',18)

#define	CDIOCPAUSE	_IO('c',19)

#define	CDIOCRESUME	_IO('c',20)

#define	CDIOCRESET	_IO('c',21)

#define	CDIOCSTART	_IO('c',22)

#define	CDIOCSTOP	_IO('c',23)

#define	CDIOCEJECT	_IO('c',24)


struct ioc_play_msf {
	u_char	start_m;
	u_char	start_s;
	u_char	start_f;
	u_char	end_m;
	u_char	end_s;
	u_char	end_f;
};
#define	CDIOCPLAYMSF	_IOW('c',25,struct ioc_play_msf)

#define	CDIOCALLOW	_IO('c',26)

#define	CDIOCPREVENT	_IO('c',27)

				/*<1>For drives that support it, this*/
				/*<1>causes the drive to close its door*/
				/*<1>and make the media (if any) ready*/
#define CDIOCCLOSE	_IO('c',28)	/*<1>*/


struct ioc_pitch {		/*<1>For drives that support it, this*/
				/*<1>call instructs the drive to play the*/
	short	speed;		/*<1>audio at a faster or slower-than-normal*/
};				/*<1>rate. -32767 to -1 is slower, 0==normal,*/
				/*<1>and 1 to 32767 is faster.  LSB bits are*/
				/*<1>discarded first by drives with less res.*/
#define	CDIOCPITCH	_IOW('c',29,struct ioc_pitch)	/*<1>*/

struct ioc_capability {			/*<2>*/
	u_long	play_function;		/*<2>*/
#define CDDOPLAYTRK	0x00000001	/*<2>Can Play tracks/index*/
#define	CDDOPLAYMSF	0x00000002	/*<2>Can Play msf to msf*/
#define	CDDOPLAYBLOCKS	0x00000004	/*<2>Can Play range of blocks*/
#define	CDDOPAUSE	0x00000100	/*<2>Output can be paused*/
#define	CDDORESUME	0x00000200	/*<2>Output can be resumed*/
#define	CDDORESET	0x00000400	/*<2>Drive can be completely reset*/
#define	CDDOSTART	0x00000800	/*<2>Audio can be started*/
#define CDDOSTOP	0x00001000	/*<2>Audio can be stopped*/
#define CDDOPITCH	0x00002000	/*<2>Audio pitch */

	u_long	routing_function;	/*<2>*/
#define CDREADVOLUME	0x00000001	/*<2>Volume settings can be read*/
#define CDSETVOLUME	0x00000002	/*<2>Volume settings can be set*/
#define	CDSETMONO	0x00000100	/*<2>Output can be set to mono*/
#define CDSETSTEREO	0x00000200	/*<2>Output can be set to stereo (def)*/
#define	CDSETLEFT	0x00000400	/*<2>Output can be set to left only*/
#define	CDSETRIGHT	0x00000800	/*<2>Output can be set to right only*/
#define	CDSETMUTE	0x00001000	/*<2>Output can be muted*/
#define CDSETPATCH	0x00008000	/*<2>Direct routing control allowed*/

	u_long	special_function;	/*<2>*/
#define	CDDOEJECT	0x00000001	/*<2>The tray can be opened*/
#define	CDDOCLOSE	0x00000002	/*<2>The tray can be closed*/
#define	CDDOLOCK	0x00000004	/*<2>The tray can be locked*/
#define CDREADHEADER	0x00000100	/*<2>Can read Table of Contents*/
#define	CDREADENTRIES	0x00000200	/*<2>Can read TOC Entries*/
#define	CDREADSUBQ	0x00000200	/*<2>Can read Subchannel info*/
#define CDREADRW	0x00000400	/*<2>Can read subcodes R-W*/
#define	CDHASDEBUG	0x00004000	/*<2>The tray has dynamic debugging*/
};					/*<2>*/

#define	CDIOCCAPABILITY	_IOR('c',30,struct ioc_capability)	/*<2>*/

/*
 * Special version of CDIOCREADSUBCHANNEL which assumes that
 * ioc_read_subchannel->data points to the kernel memory. For
 * use in compatibility layers.
 */
#define CDIOCREADSUBCHANNEL_SYSSPACE _IOWR('c', 31, struct ioc_read_subchannel)

#endif /* !_SYS_CDIO_H_ */
