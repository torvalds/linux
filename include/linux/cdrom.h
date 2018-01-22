/* SPDX-License-Identifier: GPL-2.0 */
/*
 * -- <linux/cdrom.h>
 * General header file for linux CD-ROM drivers 
 * Copyright (C) 1992         David Giller, rafetmad@oxy.edu
 *               1994, 1995   Eberhard Mönkeberg, emoenke@gwdg.de
 *               1996         David van Leeuwen, david@tm.tno.nl
 *               1997, 1998   Erik Andersen, andersee@debian.org
 *               1998-2002    Jens Axboe, axboe@suse.de
 */
#ifndef	_LINUX_CDROM_H
#define	_LINUX_CDROM_H

#include <linux/fs.h>		/* not really needed, later.. */
#include <linux/list.h>
#include <uapi/linux/cdrom.h>

struct packet_command
{
	unsigned char 		cmd[CDROM_PACKET_SIZE];
	unsigned char 		*buffer;
	unsigned int 		buflen;
	int			stat;
	struct request_sense	*sense;
	unsigned char		data_direction;
	int			quiet;
	int			timeout;
	void			*reserved[1];
};

/*
 * _OLD will use PIO transfer on atapi devices, _BPC_* will use DMA
 */
#define CDDA_OLD		0	/* old style */
#define CDDA_BPC_SINGLE		1	/* single frame block pc */
#define CDDA_BPC_FULL		2	/* multi frame block pc */

/* Uniform cdrom data structures for cdrom.c */
struct cdrom_device_info {
	const struct cdrom_device_ops *ops; /* link to device_ops */
	struct list_head list;		/* linked list of all device_info */
	struct gendisk *disk;		/* matching block layer disk */
	void *handle;		        /* driver-dependent data */
/* specifications */
	int mask;                       /* mask of capability: disables them */
	int speed;			/* maximum speed for reading data */
	int capacity;			/* number of discs in jukebox */
/* device-related storage */
	unsigned int options	: 30;	/* options flags */
	unsigned mc_flags	: 2;	/* media change buffer flags */
	unsigned int vfs_events;	/* cached events for vfs path */
	unsigned int ioctl_events;	/* cached events for ioctl path */
    	int use_count;                  /* number of times device opened */
    	char name[20];                  /* name of the device type */
/* per-device flags */
        __u8 sanyo_slot		: 2;	/* Sanyo 3 CD changer support */
        __u8 keeplocked		: 1;	/* CDROM_LOCKDOOR status */
        __u8 reserved		: 5;	/* not used yet */
	int cdda_method;		/* see flags */
	__u8 last_sense;
	__u8 media_written;		/* dirty flag, DVD+RW bookkeeping */
	unsigned short mmc3_profile;	/* current MMC3 profile */
	int for_data;
	int (*exit)(struct cdrom_device_info *);
	int mrw_mode_page;
};

struct cdrom_device_ops {
/* routines */
	int (*open) (struct cdrom_device_info *, int);
	void (*release) (struct cdrom_device_info *);
	int (*drive_status) (struct cdrom_device_info *, int);
	unsigned int (*check_events) (struct cdrom_device_info *cdi,
				      unsigned int clearing, int slot);
	int (*media_changed) (struct cdrom_device_info *, int);
	int (*tray_move) (struct cdrom_device_info *, int);
	int (*lock_door) (struct cdrom_device_info *, int);
	int (*select_speed) (struct cdrom_device_info *, int);
	int (*select_disc) (struct cdrom_device_info *, int);
	int (*get_last_session) (struct cdrom_device_info *,
				 struct cdrom_multisession *);
	int (*get_mcn) (struct cdrom_device_info *,
			struct cdrom_mcn *);
	/* hard reset device */
	int (*reset) (struct cdrom_device_info *);
	/* play stuff */
	int (*audio_ioctl) (struct cdrom_device_info *,unsigned int, void *);

/* driver specifications */
	const int capability;   /* capability flags */
	/* handle uniform packets for scsi type devices (scsi,atapi) */
	int (*generic_packet) (struct cdrom_device_info *,
			       struct packet_command *);
};

/* the general block_device operations structure: */
extern int cdrom_open(struct cdrom_device_info *cdi, struct block_device *bdev,
			fmode_t mode);
extern void cdrom_release(struct cdrom_device_info *cdi, fmode_t mode);
extern int cdrom_ioctl(struct cdrom_device_info *cdi, struct block_device *bdev,
		       fmode_t mode, unsigned int cmd, unsigned long arg);
extern unsigned int cdrom_check_events(struct cdrom_device_info *cdi,
				       unsigned int clearing);
extern int cdrom_media_changed(struct cdrom_device_info *);

extern int register_cdrom(struct cdrom_device_info *cdi);
extern void unregister_cdrom(struct cdrom_device_info *cdi);

typedef struct {
    int data;
    int audio;
    int cdi;
    int xa;
    long error;
} tracktype;

extern int cdrom_get_last_written(struct cdrom_device_info *cdi, long *last_written);
extern int cdrom_number_of_slots(struct cdrom_device_info *cdi);
extern int cdrom_mode_select(struct cdrom_device_info *cdi,
			     struct packet_command *cgc);
extern int cdrom_mode_sense(struct cdrom_device_info *cdi,
			    struct packet_command *cgc,
			    int page_code, int page_control);
extern void init_cdrom_command(struct packet_command *cgc,
			       void *buffer, int len, int type);
extern int cdrom_dummy_generic_packet(struct cdrom_device_info *cdi,
				      struct packet_command *cgc);

/* The SCSI spec says there could be 256 slots. */
#define CDROM_MAX_SLOTS	256

struct cdrom_mechstat_header {
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 fault         : 1;
	__u8 changer_state : 2;
	__u8 curslot       : 5;
	__u8 mech_state    : 3;
	__u8 door_open     : 1;
	__u8 reserved1     : 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 curslot       : 5;
	__u8 changer_state : 2;
	__u8 fault         : 1;
	__u8 reserved1     : 4;
	__u8 door_open     : 1;
	__u8 mech_state    : 3;
#endif
	__u8     curlba[3];
	__u8     nslots;
	__u16 slot_tablelen;
};

struct cdrom_slot {
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 disc_present : 1;
	__u8 reserved1    : 6;
	__u8 change       : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 change       : 1;
	__u8 reserved1    : 6;
	__u8 disc_present : 1;
#endif
	__u8 reserved2[3];
};

struct cdrom_changer_info {
	struct cdrom_mechstat_header hdr;
	struct cdrom_slot slots[CDROM_MAX_SLOTS];
};

typedef enum {
	mechtype_caddy = 0,
	mechtype_tray  = 1,
	mechtype_popup = 2,
	mechtype_individual_changer = 4,
	mechtype_cartridge_changer  = 5
} mechtype_t;

typedef struct {
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 ps			: 1;
	__u8 reserved1		: 1;
	__u8 page_code		: 6;
        __u8 page_length;
	__u8 reserved2		: 1;
	__u8 bufe		: 1;
	__u8 ls_v		: 1;
	__u8 test_write		: 1;
        __u8 write_type		: 4;
	__u8 multi_session	: 2; /* or border, DVD */
	__u8 fp			: 1;
	__u8 copy		: 1;
	__u8 track_mode		: 4;
	__u8 reserved3		: 4;
	__u8 data_block_type	: 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 page_code		: 6;
	__u8 reserved1		: 1;
	__u8 ps			: 1;
        __u8 page_length;
        __u8 write_type		: 4;
	__u8 test_write		: 1;
	__u8 ls_v		: 1;
	__u8 bufe		: 1;
	__u8 reserved2		: 1;
	__u8 track_mode		: 4;
	__u8 copy		: 1;
	__u8 fp			: 1;
	__u8 multi_session	: 2; /* or border, DVD */
	__u8 data_block_type	: 4;
	__u8 reserved3		: 4;
#endif
	__u8 link_size;
	__u8 reserved4;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved5		: 2;
	__u8 app_code		: 6;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 app_code		: 6;
	__u8 reserved5		: 2;
#endif
	__u8 session_format;
	__u8 reserved6;
	__be32 packet_size;
	__u16 audio_pause;
	__u8 mcn[16];
	__u8 isrc[16];
	__u8 subhdr0;
	__u8 subhdr1;
	__u8 subhdr2;
	__u8 subhdr3;
} __attribute__((packed)) write_param_page;

struct modesel_head
{
	__u8	reserved1;
	__u8	medium;
	__u8	reserved2;
	__u8	block_desc_length;
	__u8	density;
	__u8	number_of_blocks_hi;
	__u8	number_of_blocks_med;
	__u8	number_of_blocks_lo;
	__u8	reserved3;
	__u8	block_length_hi;
	__u8	block_length_med;
	__u8	block_length_lo;
};

typedef struct {
	__u16 report_key_length;
	__u8 reserved1;
	__u8 reserved2;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 type_code			: 2;
	__u8 vra			: 3;
	__u8 ucca			: 3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 ucca			: 3;
	__u8 vra			: 3;
	__u8 type_code			: 2;
#endif
	__u8 region_mask;
	__u8 rpc_scheme;
	__u8 reserved3;
} rpc_state_t;

struct event_header {
	__be16 data_len;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 nea		: 1;
	__u8 reserved1		: 4;
	__u8 notification_class	: 3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 notification_class	: 3;
	__u8 reserved1		: 4;
	__u8 nea		: 1;
#endif
	__u8 supp_event_class;
};

struct media_event_desc {
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1		: 4;
	__u8 media_event_code	: 4;
	__u8 reserved2		: 6;
	__u8 media_present	: 1;
	__u8 door_open		: 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 media_event_code	: 4;
	__u8 reserved1		: 4;
	__u8 door_open		: 1;
	__u8 media_present	: 1;
	__u8 reserved2		: 6;
#endif
	__u8 start_slot;
	__u8 end_slot;
};

extern int cdrom_get_media_event(struct cdrom_device_info *cdi, struct media_event_desc *med);

static inline void lba_to_msf(int lba, u8 *m, u8 *s, u8 *f)
{
	lba += CD_MSF_OFFSET;
	lba &= 0xffffff;  /* negative lbas use only 24 bits */
	*m = lba / (CD_SECS * CD_FRAMES);
	lba %= (CD_SECS * CD_FRAMES);
	*s = lba / CD_FRAMES;
	*f = lba % CD_FRAMES;
}

static inline int msf_to_lba(u8 m, u8 s, u8 f)
{
	return (((m * CD_SECS) + s) * CD_FRAMES + f) - CD_MSF_OFFSET;
}
#endif  /* _LINUX_CDROM_H */
