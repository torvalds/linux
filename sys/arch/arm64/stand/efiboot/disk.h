/* $OpenBSD: disk.h,v 1.3 2020/12/09 18:10:18 krw Exp $ */

#ifndef _DISK_H
#define _DISK_H

#include <sys/queue.h>

typedef struct efi_diskinfo {
	EFI_BLOCK_IO		*blkio;
	UINT32			 mediaid;
} *efi_diskinfo_t;

struct diskinfo {
	struct efi_diskinfo ed;
	struct disklabel disklabel;
	struct sr_boot_volume *sr_vol;

	u_int part;
	u_int flags;
#define DISKINFO_FLAG_GOODLABEL		(1 << 0)

	int (*diskio)(int, struct diskinfo *, u_int, int, void *);
	int (*strategy)(void *, int, daddr_t, size_t, void *, size_t *);

	TAILQ_ENTRY(diskinfo) list;
};
TAILQ_HEAD(disklist_lh, diskinfo);

extern struct diskinfo *bootdev_dip;

extern struct disklist_lh disklist;

#endif /* _DISK_H */
