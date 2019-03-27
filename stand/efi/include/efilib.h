/*-
 * Copyright (c) 2000 Doug Rabson
 * Copyright (c) 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LOADER_EFILIB_H
#define	_LOADER_EFILIB_H

#include <stand.h>
#include <stdbool.h>
#include <sys/queue.h>

extern EFI_HANDLE		IH;
extern EFI_SYSTEM_TABLE		*ST;
extern EFI_BOOT_SERVICES	*BS;
extern EFI_RUNTIME_SERVICES	*RS;

extern struct devsw efipart_fddev;
extern struct devsw efipart_cddev;
extern struct devsw efipart_hddev;
extern struct devsw efinet_dev;
extern struct netif_driver efinetif;

/* EFI block device data, included here to help efi_zfs_probe() */
typedef STAILQ_HEAD(pdinfo_list, pdinfo) pdinfo_list_t;

typedef struct pdinfo
{
	STAILQ_ENTRY(pdinfo)	pd_link;	/* link in device list */
	pdinfo_list_t		pd_part;	/* list of partitions */
	EFI_HANDLE		pd_handle;
	EFI_HANDLE		pd_alias;
	EFI_DEVICE_PATH		*pd_devpath;
	EFI_BLOCK_IO		*pd_blkio;
	uint32_t		pd_unit;	/* unit number */
	uint32_t		pd_open;	/* reference counter */
	void			*pd_bcache;	/* buffer cache data */
	struct pdinfo		*pd_parent;	/* Linked items (eg partitions) */
	struct devsw		*pd_devsw;	/* Back pointer to devsw */
} pdinfo_t;

pdinfo_list_t *efiblk_get_pdinfo_list(struct devsw *dev);
pdinfo_t *efiblk_get_pdinfo(struct devdesc *dev);
pdinfo_t *efiblk_get_pdinfo_by_handle(EFI_HANDLE h);
pdinfo_t *efiblk_get_pdinfo_by_device_path(EFI_DEVICE_PATH *path);

void *efi_get_table(EFI_GUID *tbl);

int efi_getdev(void **vdev, const char *devspec, const char **path);
char *efi_fmtdev(void *vdev);
int efi_setcurrdev(struct env_var *ev, int flags, const void *value);


int efi_register_handles(struct devsw *, EFI_HANDLE *, EFI_HANDLE *, int);
EFI_HANDLE efi_find_handle(struct devsw *, int);
int efi_handle_lookup(EFI_HANDLE, struct devsw **, int *,  uint64_t *);
int efi_handle_update_dev(EFI_HANDLE, struct devsw *, int, uint64_t);

EFI_DEVICE_PATH *efi_lookup_image_devpath(EFI_HANDLE);
EFI_DEVICE_PATH *efi_lookup_devpath(EFI_HANDLE);
EFI_HANDLE efi_devpath_handle(EFI_DEVICE_PATH *);
EFI_DEVICE_PATH *efi_devpath_last_node(EFI_DEVICE_PATH *);
EFI_DEVICE_PATH *efi_devpath_trim(EFI_DEVICE_PATH *);
bool efi_devpath_match(EFI_DEVICE_PATH *, EFI_DEVICE_PATH *);
bool efi_devpath_match_node(EFI_DEVICE_PATH *, EFI_DEVICE_PATH *);
bool efi_devpath_is_prefix(EFI_DEVICE_PATH *, EFI_DEVICE_PATH *);
CHAR16 *efi_devpath_name(EFI_DEVICE_PATH *);
void efi_free_devpath_name(CHAR16 *);
EFI_DEVICE_PATH *efi_devpath_to_media_path(EFI_DEVICE_PATH *);
UINTN efi_devpath_length(EFI_DEVICE_PATH *);

int efi_status_to_errno(EFI_STATUS);
EFI_STATUS errno_to_efi_status(int errno);

void efi_time_init(void);
void efi_time_fini(void);

EFI_STATUS efi_main(EFI_HANDLE Ximage, EFI_SYSTEM_TABLE* Xsystab);

EFI_STATUS main(int argc, CHAR16 *argv[]);
void efi_exit(EFI_STATUS status) __dead2;
void delay(int usecs);

/* EFI environment initialization. */
void efi_init_environment(void);

/* EFI Memory type strings. */
const char *efi_memory_type(EFI_MEMORY_TYPE);

/* CHAR16 utility functions. */
int wcscmp(CHAR16 *, CHAR16 *);
void cpy8to16(const char *, CHAR16 *, size_t);
void cpy16to8(const CHAR16 *, char *, size_t);

/*
 * Routines for interacting with EFI's env vars in a more unix-like
 * way than the standard APIs. In addition, convenience routines for
 * the loader setting / getting FreeBSD specific variables.
 */

EFI_STATUS efi_freebsd_getenv(const char *v, void *data, __size_t *len);
EFI_STATUS efi_getenv(EFI_GUID *g, const char *v, void *data, __size_t *len);
EFI_STATUS efi_global_getenv(const char *v, void *data, __size_t *len);
EFI_STATUS efi_setenv_freebsd_wcs(const char *varname, CHAR16 *valstr);

/* guids and names */
bool efi_guid_to_str(const EFI_GUID *, char **);
bool efi_str_to_guid(const char *, EFI_GUID *);
bool efi_name_to_guid(const char *, EFI_GUID *);
bool efi_guid_to_name(EFI_GUID *, char **);

/* efipart.c */
int	efipart_inithandles(void);

#endif	/* _LOADER_EFILIB_H */
