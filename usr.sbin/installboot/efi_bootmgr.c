/*	$OpenBSD: efi_bootmgr.c,v 1.6 2025/07/27 20:00:26 krw Exp $	*/
/*
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>

#include <dev/efi/efi.h>
#include <dev/efi/efiio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "installboot.h"

/*
 * Device Path support
 */

typedef struct _EFI_DEVICE_PATH {
	UINT8				Type;
	UINT8				SubType;
	UINT8				Length[2];
} EFI_DEVICE_PATH;

#define END_DEVICE_PATH			0x7f

#define END_INSTANCE_DEVICE_PATH	0x01
#define END_ENTIRE_DEVICE_PATH		0xff

#define MEDIA_DEVICE_PATH		0x04

#define MEDIA_HARDDRIVE_DP		0x01
typedef struct _HARDDRIVE_DEVICE_PATH {
	EFI_DEVICE_PATH			Header;
	UINT32				PartitionNumber;
	UINT64				PartitionStart;
	UINT64				PartitionSize;
	UINT8				Signature[16];
	UINT8				MBRType;
	UINT8				SignatureType;
} __packed HARDDRIVE_DEVICE_PATH;

#define MBR_TYPE_MBR			0x01
#define MBR_TYPE_GPT			0x02

#define SIGNATURE_TYPE_NONE		0x00
#define SIGNATURE_TYPE_MBR		0x01
#define SIGNATURE_TYPE_GUID		0x02

#define MEDIA_FILEPATH_DP		0x04
typedef struct _FILEPATH_DEVICE_PATH {
	EFI_DEVICE_PATH			Header;
	CHAR16				PathName[1];
} __packed FILEPATH_DEVICE_PATH;

/*
 * Variable Attributes
 */

#define EFI_VARIABLE_NON_VOLATILE	0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS	0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS	0x00000004

/*
 * Load Options
 */

typedef struct _EFI_LOAD_OPTION {
	UINT32				Attributes;
	UINT16				FilePathListLength;
} __packed EFI_LOAD_OPTION;

#define LOAD_OPTION_ACTIVE		0x00000001

EFI_GUID guid = EFI_GLOBAL_VARIABLE;

static size_t
create_efi_load_option(int gpart, struct gpt_partition *gp,
    const char *path, EFI_LOAD_OPTION **opt)
{
	EFI_DEVICE_PATH *dp, *dp0;
	HARDDRIVE_DEVICE_PATH *hd;
	FILEPATH_DEVICE_PATH *fp;
	CHAR16 desc[] = u"OpenBSD";
	size_t pathlen, dplen, optlen;
	int i;

	pathlen = 2 * (strlen(path) + 1);
	dplen = sizeof(HARDDRIVE_DEVICE_PATH) +
	    sizeof(EFI_DEVICE_PATH) + pathlen +
	    sizeof(EFI_DEVICE_PATH);
	if ((dp = dp0 = malloc(dplen)) == NULL)
		err(1, NULL);
	memset(dp, 0, dplen);

	dp->Type = MEDIA_DEVICE_PATH;
	dp->SubType = MEDIA_HARDDRIVE_DP;
	dp->Length[0] = sizeof(HARDDRIVE_DEVICE_PATH);
	hd = (HARDDRIVE_DEVICE_PATH *)dp;
	hd->PartitionNumber = gpart + 1;
	hd->PartitionStart = gp->gp_lba_start;
	hd->PartitionSize = gp->gp_lba_end - gp->gp_lba_start + 1;
	memcpy(hd->Signature, &gp->gp_guid, sizeof(hd->Signature));
	hd->MBRType = MBR_TYPE_GPT;
	hd->SignatureType = SIGNATURE_TYPE_GUID;

	dp = (EFI_DEVICE_PATH *)((UINT8 *)dp + dp->Length[0]);
	dp->Type = MEDIA_DEVICE_PATH;
	dp->SubType = MEDIA_FILEPATH_DP;
	dp->Length[0] = sizeof(EFI_DEVICE_PATH) + pathlen;
	fp = (FILEPATH_DEVICE_PATH *)dp;
	for (i = 0; i < strlen(path); i++)
		fp->PathName[i] = path[i];

	dp = (EFI_DEVICE_PATH *)((UINT8 *)dp + dp->Length[0]);
	dp->Type = END_DEVICE_PATH;
	dp->SubType = END_ENTIRE_DEVICE_PATH;
	dp->Length[0] = sizeof(EFI_DEVICE_PATH);

	optlen = sizeof(EFI_LOAD_OPTION) + sizeof(desc) + dplen;
	if ((*opt = malloc(optlen)) == NULL)
		err(1, NULL);
	memset(*opt, 0, optlen);

	(*opt)->Attributes = LOAD_OPTION_ACTIVE;
	(*opt)->FilePathListLength = htole16(dplen);
	memcpy(((UINT8 *)(*opt)) + sizeof(**opt), desc, sizeof(desc));
	memcpy(((UINT8 *)(*opt)) + sizeof(**opt) + sizeof(desc), dp0, dplen);

	return optlen;
}

static void
write_efi_load_option(EFI_LOAD_OPTION *opt, size_t optlen)
{
	struct efi_var_ioc var;
	efi_char name[512];
	char data[512];
	int found = 0, idx = 0;
	int error;
	int fd;
	int i;

	fd = open("/dev/efi", O_RDWR);
	if (fd == -1)
		err(1, "open");

	/*
	 * Check whether a Boot#### variable with the desired load
	 * option already exists.
	 */
	memset(&var, 0, sizeof(var));
	memset(name, 0, sizeof(name));
	while (!found) {
		var.name = name;
		var.namesize = sizeof(name);
		var.datasize = 0;
		error = ioctl(fd, EFIIOC_VAR_NEXT, &var);
		if (error) {
			if (errno == ENOENT)
				break;
			warn("EFIIOC_VAR_NEXT");
			return;
		}
		if (memcmp(&var.vendor, &guid, sizeof(guid)) != 0)
			continue;
		if (var.namesize != 18 ||
		    name[0] != 'B' || name[1] != 'o' ||
		    name[2] != 'o' || name[3] != 't' ||
		    !isxdigit(name[4]) || !isxdigit(name[5]) ||
		    !isxdigit(name[6]) || !isxdigit(name[7]))
			continue;
		idx = 0;
		idx |= ((name[4] - '0') << 12);
		idx |= ((name[5] - '0') << 8);
		idx |= ((name[6] - '0') << 4);
		idx |= ((name[7] - '0') << 0);
		var.data = data;
		var.datasize = sizeof(data);
		error = ioctl(fd, EFIIOC_VAR_GET, &var);
		if (error) {
			warn("EFIIOC_VAR_GET: Boot%04X", idx);
			return;
		}
		if (var.datasize != optlen)
			continue;
		if (memcmp(data, opt, optlen) != 0)
			continue;

		found = 1;
		if (verbose) {
			fprintf(stderr, "%s Boot%04X\n",
			    (nowrite ? "would reuse" : "reusing"), idx);
		}
	}

	/*
	 * If we didn't find a Boot#### variable with the desired load
	 * option, create a new one now.
	 */
	if (!found) {
		memcpy(&var.vendor, &guid, sizeof(guid));
		memset(name, 0, sizeof(name));
		name[0] = 'B';
		name[1] = 'o';
		name[2] = 'o';
		name[3] = 't';

		/* Find a Boot#### variable that isn't in use yet. */
		for (idx = 0; idx < 65536; idx++) {
			name[4] = '0' + ((idx & 0xf000) >> 12);
			name[5] = '0' + ((idx & 0x0f00) >> 8);
			name[6] = '0' + ((idx & 0x00f0) >> 4);
			name[7] = '0' + ((idx & 0x000f) >> 0);
			var.name = name;
			var.namesize = 18;
			var.data = data;
			var.datasize = sizeof(data);
			error = ioctl(fd, EFIIOC_VAR_GET, &var);
			if (error) {
				if (errno == ENOENT)
					break;
				warn("EFIIOC_VAR_GET: Boot%04X", idx);
				return;
			}
		}
		if (idx >= 65536)
			return;

		if (verbose) {
			fprintf(stderr, "%s Boot%04X\n",
			    (nowrite ? "would create" : "creating"), idx);
		}

		if (!nowrite) {
			var.name = name;
			var.namesize = 18;
			var.data = opt;
			var.datasize = optlen;
			var.attrib = EFI_VARIABLE_NON_VOLATILE |
			    EFI_VARIABLE_BOOTSERVICE_ACCESS |
			    EFI_VARIABLE_RUNTIME_ACCESS;
			error = ioctl(fd, EFIIOC_VAR_SET, &var);
			if (error) {
				if (errno != EPERM && errno != ENOSYS)
					warn("EFIIOC_VAR_SET: Boot%04X", idx);
				return;
			}
		}
	}

	/*
	 * Add our load option to the BootOrder variable if necessary.
	 * Prepend it such that it becomes the default.
	 */
	var.name = u"BootOrder";
	var.namesize = 20;
	memcpy(&var.vendor, &guid, sizeof(guid));
	var.data = data;
	var.datasize = sizeof(data);
	error = ioctl(fd, EFIIOC_VAR_GET, &var);
	if (error) {
		if (errno != ENOENT) {
			warn("EFIIOC_VAR_GET: BootOrder");
			return;
		}
		var.datasize = 0;
	}

	found = 0;
	for (i = 0; i < var.datasize; i += 2) {
		if (*(uint16_t *)&data[i] == idx) {
			found = 1;
			break;
		} 
	}
	if (!found) {
		/*
		 * If there are more than 256 load options, simply
		 * give up.
		 */
		if (var.datasize + 2 > sizeof(data))
			return;
		memmove(&data[2], &data[0], var.datasize);
		var.datasize += 2;
		*(uint16_t *)&data[0] = idx;
	}

	if (!nowrite) {
		var.attrib = EFI_VARIABLE_NON_VOLATILE |
		    EFI_VARIABLE_BOOTSERVICE_ACCESS |
		    EFI_VARIABLE_RUNTIME_ACCESS;
		error = ioctl(fd, EFIIOC_VAR_SET, &var);
		if (error) {
			if (errno != EPERM && errno != ENOSYS)
				warn("EFIIOC_VAR_SET: BootOrder");
			return;
		}
	}
}

void
efi_bootmgr_setup(int gpart, struct gpt_partition *gp, const char *path)
{
	EFI_LOAD_OPTION *opt;
	size_t optlen;
	int mib[2] = { CTL_KERN, KERN_SECURELVL };
	int securelevel;
	size_t sz = sizeof(securelevel);

	if (sysctl(mib, 2, &securelevel, &sz, NULL, 0) == -1)
		err(1, "sysctl");
	if (securelevel > 0 && !nowrite) {
		if (verbose)
			fprintf(stderr, "can't configure firmware\n");
		return;
	}

	optlen = create_efi_load_option(gpart, gp, path, &opt);
	write_efi_load_option(opt, optlen);
}
