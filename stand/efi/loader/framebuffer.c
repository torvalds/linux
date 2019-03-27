/*-
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Benno Rice under sponsorship from
 * the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <bootstrap.h>
#include <sys/endian.h>
#include <sys/param.h>
#include <stand.h>

#include <efi.h>
#include <efilib.h>
#include <efiuga.h>
#include <efipciio.h>
#include <machine/metadata.h>

#include "framebuffer.h"

static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID pciio_guid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID uga_guid = EFI_UGA_DRAW_PROTOCOL_GUID;

static struct named_resolution {
	const char *name;
	const char *alias;
	unsigned int width;
	unsigned int height;
} resolutions[] = {
	{
		.name = "480p",
		.width = 640,
		.height = 480,
	},
	{
		.name = "720p",
		.width = 1280,
		.height = 720,
	},
	{
		.name = "1080p",
		.width = 1920,
		.height = 1080,
	},
	{
		.name = "2160p",
		.alias = "4k",
		.width = 3840,
		.height = 2160,
	},
	{
		.name = "5k",
		.width = 5120,
		.height = 2880,
	}
};

static u_int
efifb_color_depth(struct efi_fb *efifb)
{
	uint32_t mask;
	u_int depth;

	mask = efifb->fb_mask_red | efifb->fb_mask_green |
	    efifb->fb_mask_blue | efifb->fb_mask_reserved;
	if (mask == 0)
		return (0);
	for (depth = 1; mask != 1; depth++)
		mask >>= 1;
	return (depth);
}

static int
efifb_mask_from_pixfmt(struct efi_fb *efifb, EFI_GRAPHICS_PIXEL_FORMAT pixfmt,
    EFI_PIXEL_BITMASK *pixinfo)
{
	int result;

	result = 0;
	switch (pixfmt) {
	case PixelRedGreenBlueReserved8BitPerColor:
		efifb->fb_mask_red = 0x000000ff;
		efifb->fb_mask_green = 0x0000ff00;
		efifb->fb_mask_blue = 0x00ff0000;
		efifb->fb_mask_reserved = 0xff000000;
		break;
	case PixelBlueGreenRedReserved8BitPerColor:
		efifb->fb_mask_red = 0x00ff0000;
		efifb->fb_mask_green = 0x0000ff00;
		efifb->fb_mask_blue = 0x000000ff;
		efifb->fb_mask_reserved = 0xff000000;
		break;
	case PixelBitMask:
		efifb->fb_mask_red = pixinfo->RedMask;
		efifb->fb_mask_green = pixinfo->GreenMask;
		efifb->fb_mask_blue = pixinfo->BlueMask;
		efifb->fb_mask_reserved = pixinfo->ReservedMask;
		break;
	default:
		result = 1;
		break;
	}
	return (result);
}

static int
efifb_from_gop(struct efi_fb *efifb, EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info)
{
	int result;

	efifb->fb_addr = mode->FrameBufferBase;
	efifb->fb_size = mode->FrameBufferSize;
	efifb->fb_height = info->VerticalResolution;
	efifb->fb_width = info->HorizontalResolution;
	efifb->fb_stride = info->PixelsPerScanLine;
	result = efifb_mask_from_pixfmt(efifb, info->PixelFormat,
	    &info->PixelInformation);
	return (result);
}

static ssize_t
efifb_uga_find_pixel(EFI_UGA_DRAW_PROTOCOL *uga, u_int line,
    EFI_PCI_IO_PROTOCOL *pciio, uint64_t addr, uint64_t size)
{
	EFI_UGA_PIXEL pix0, pix1;
	uint8_t *data1, *data2;
	size_t count, maxcount = 1024;
	ssize_t ofs;
	EFI_STATUS status;
	u_int idx;

	status = uga->Blt(uga, &pix0, EfiUgaVideoToBltBuffer,
	    0, line, 0, 0, 1, 1, 0);
	if (EFI_ERROR(status)) {
		printf("UGA BLT operation failed (video->buffer)");
		return (-1);
	}
	pix1.Red = ~pix0.Red;
	pix1.Green = ~pix0.Green;
	pix1.Blue = ~pix0.Blue;
	pix1.Reserved = 0;

	data1 = calloc(maxcount, 2);
	if (data1 == NULL) {
		printf("Unable to allocate memory");
		return (-1);
	}
	data2 = data1 + maxcount;

	ofs = 0;
	while (size > 0) {
		count = min(size, maxcount);

		status = pciio->Mem.Read(pciio, EfiPciIoWidthUint32,
		    EFI_PCI_IO_PASS_THROUGH_BAR, addr + ofs, count >> 2,
		    data1);
		if (EFI_ERROR(status)) {
			printf("Error reading frame buffer (before)");
			goto fail;
		}
		status = uga->Blt(uga, &pix1, EfiUgaBltBufferToVideo,
		    0, 0, 0, line, 1, 1, 0);
		if (EFI_ERROR(status)) {
			printf("UGA BLT operation failed (modify)");
			goto fail;
		}
		status = pciio->Mem.Read(pciio, EfiPciIoWidthUint32,
		    EFI_PCI_IO_PASS_THROUGH_BAR, addr + ofs, count >> 2,
		    data2);
		if (EFI_ERROR(status)) {
			printf("Error reading frame buffer (after)");
			goto fail;
		}
		status = uga->Blt(uga, &pix0, EfiUgaBltBufferToVideo,
		    0, 0, 0, line, 1, 1, 0);
		if (EFI_ERROR(status)) {
			printf("UGA BLT operation failed (restore)");
			goto fail;
		}
		for (idx = 0; idx < count; idx++) {
			if (data1[idx] != data2[idx]) {
				free(data1);
				return (ofs + (idx & ~3));
			}
		}
		ofs += count;
		size -= count;
	}
	printf("No change detected in frame buffer");

 fail:
	printf(" -- error %lu\n", EFI_ERROR_CODE(status));
	free(data1);
	return (-1);
}

static EFI_PCI_IO_PROTOCOL *
efifb_uga_get_pciio(void)
{
	EFI_PCI_IO_PROTOCOL *pciio;
	EFI_HANDLE *buf, *hp;
	EFI_STATUS status;
	UINTN bufsz;

	/* Get all handles that support the UGA protocol. */
	bufsz = 0;
	status = BS->LocateHandle(ByProtocol, &uga_guid, NULL, &bufsz, NULL);
	if (status != EFI_BUFFER_TOO_SMALL)
		return (NULL);
	buf = malloc(bufsz);
	status = BS->LocateHandle(ByProtocol, &uga_guid, NULL, &bufsz, buf);
	if (status != EFI_SUCCESS) {
		free(buf);
		return (NULL);
	}
	bufsz /= sizeof(EFI_HANDLE);

	/* Get the PCI I/O interface of the first handle that supports it. */
	pciio = NULL;
	for (hp = buf; hp < buf + bufsz; hp++) {
		status = BS->HandleProtocol(*hp, &pciio_guid, (void **)&pciio);
		if (status == EFI_SUCCESS) {
			free(buf);
			return (pciio);
		}
	}
	free(buf);
	return (NULL);
}

static EFI_STATUS
efifb_uga_locate_framebuffer(EFI_PCI_IO_PROTOCOL *pciio, uint64_t *addrp,
    uint64_t *sizep)
{
	uint8_t *resattr;
	uint64_t addr, size;
	EFI_STATUS status;
	u_int bar;

	if (pciio == NULL)
		return (EFI_DEVICE_ERROR);

	/* Attempt to get the frame buffer address (imprecise). */
	*addrp = 0;
	*sizep = 0;
	for (bar = 0; bar < 6; bar++) {
		status = pciio->GetBarAttributes(pciio, bar, NULL,
		    (void **)&resattr);
		if (status != EFI_SUCCESS)
			continue;
		/* XXX magic offsets and constants. */
		if (resattr[0] == 0x87 && resattr[3] == 0) {
			/* 32-bit address space descriptor (MEMIO) */
			addr = le32dec(resattr + 10);
			size = le32dec(resattr + 22);
		} else if (resattr[0] == 0x8a && resattr[3] == 0) {
			/* 64-bit address space descriptor (MEMIO) */
			addr = le64dec(resattr + 14);
			size = le64dec(resattr + 38);
		} else {
			addr = 0;
			size = 0;
		}
		BS->FreePool(resattr);
		if (addr == 0 || size == 0)
			continue;

		/* We assume the largest BAR is the frame buffer. */
		if (size > *sizep) {
			*addrp = addr;
			*sizep = size;
		}
	}
	return ((*addrp == 0 || *sizep == 0) ? EFI_DEVICE_ERROR : 0);
}

static int
efifb_from_uga(struct efi_fb *efifb, EFI_UGA_DRAW_PROTOCOL *uga)
{
	EFI_PCI_IO_PROTOCOL *pciio;
	char *ev, *p;
	EFI_STATUS status;
	ssize_t offset;
	uint64_t fbaddr;
	uint32_t horiz, vert, stride;
	uint32_t np, depth, refresh;

	status = uga->GetMode(uga, &horiz, &vert, &depth, &refresh);
	if (EFI_ERROR(status))
		return (1);
	efifb->fb_height = vert;
	efifb->fb_width = horiz;
	/* Paranoia... */
	if (efifb->fb_height == 0 || efifb->fb_width == 0)
		return (1);

	/* The color masks are fixed AFAICT. */
	efifb_mask_from_pixfmt(efifb, PixelBlueGreenRedReserved8BitPerColor,
	    NULL);

	/* pciio can be NULL on return! */
	pciio = efifb_uga_get_pciio();

	/* Try to find the frame buffer. */
	status = efifb_uga_locate_framebuffer(pciio, &efifb->fb_addr,
	    &efifb->fb_size);
	if (EFI_ERROR(status)) {
		efifb->fb_addr = 0;
		efifb->fb_size = 0;
	}

	/*
	 * There's no reliable way to detect the frame buffer or the
	 * offset within the frame buffer of the visible region, nor
	 * the stride. Our only option is to look at the system and
	 * fill in the blanks based on that. Luckily, UGA was mostly
	 * only used on Apple hardware.
	 */
	offset = -1;
	ev = getenv("smbios.system.maker");
	if (ev != NULL && !strcmp(ev, "Apple Inc.")) {
		ev = getenv("smbios.system.product");
		if (ev != NULL && !strcmp(ev, "iMac7,1")) {
			/* These are the expected values we should have. */
			horiz = 1680;
			vert = 1050;
			fbaddr = 0xc0000000;
			/* These are the missing bits. */
			offset = 0x10000;
			stride = 1728;
		} else if (ev != NULL && !strcmp(ev, "MacBook3,1")) {
			/* These are the expected values we should have. */
			horiz = 1280;
			vert = 800;
			fbaddr = 0xc0000000;
			/* These are the missing bits. */
			offset = 0x0;
			stride = 2048;
		}
	}

	/*
	 * If this is hardware we know, make sure that it looks familiar
	 * before we accept our hardcoded values.
	 */
	if (offset >= 0 && efifb->fb_width == horiz &&
	    efifb->fb_height == vert && efifb->fb_addr == fbaddr) {
		efifb->fb_addr += offset;
		efifb->fb_size -= offset;
		efifb->fb_stride = stride;
		return (0);
	} else if (offset >= 0) {
		printf("Hardware make/model known, but graphics not "
		    "as expected.\n");
		printf("Console may not work!\n");
	}

	/*
	 * The stride is equal or larger to the width. Often it's the
	 * next larger power of two. We'll start with that...
	 */
	efifb->fb_stride = efifb->fb_width;
	do {
		np = efifb->fb_stride & (efifb->fb_stride - 1);
		if (np) {
			efifb->fb_stride |= (np - 1);
			efifb->fb_stride++;
		}
	} while (np);

	ev = getenv("hw.efifb.address");
	if (ev == NULL) {
		if (efifb->fb_addr == 0) {
			printf("Please set hw.efifb.address and "
			    "hw.efifb.stride.\n");
			return (1);
		}

		/*
		 * The visible part of the frame buffer may not start at
		 * offset 0, so try to detect it. Note that we may not
		 * always be able to read from the frame buffer, which
		 * means that we may not be able to detect anything. In
		 * that case, we would take a long time scanning for a
		 * pixel change in the frame buffer, which would have it
		 * appear that we're hanging, so we limit the scan to
		 * 1/256th of the frame buffer. This number is mostly
		 * based on PR 202730 and the fact that on a MacBoook,
		 * where we can't read from the frame buffer the offset
		 * of the visible region is 0. In short: we want to scan
		 * enough to handle all adapters that have an offset
		 * larger than 0 and we want to scan as little as we can
		 * to not appear to hang when we can't read from the
		 * frame buffer.
		 */
		offset = efifb_uga_find_pixel(uga, 0, pciio, efifb->fb_addr,
		    efifb->fb_size >> 8);
		if (offset == -1) {
			printf("Unable to reliably detect frame buffer.\n");
		} else if (offset > 0) {
			efifb->fb_addr += offset;
			efifb->fb_size -= offset;
		}
	} else {
		offset = 0;
		efifb->fb_size = efifb->fb_height * efifb->fb_stride * 4;
		efifb->fb_addr = strtoul(ev, &p, 0);
		if (*p != '\0')
			return (1);
	}

	ev = getenv("hw.efifb.stride");
	if (ev == NULL) {
		if (pciio != NULL && offset != -1) {
			/* Determine the stride. */
			offset = efifb_uga_find_pixel(uga, 1, pciio,
			    efifb->fb_addr, horiz * 8);
			if (offset != -1)
				efifb->fb_stride = offset >> 2;
		} else {
			printf("Unable to reliably detect the stride.\n");
		}
	} else {
		efifb->fb_stride = strtoul(ev, &p, 0);
		if (*p != '\0')
			return (1);
	}

	/*
	 * We finalized on the stride, so recalculate the size of the
	 * frame buffer.
	 */
	efifb->fb_size = efifb->fb_height * efifb->fb_stride * 4;
	return (0);
}

int
efi_find_framebuffer(struct efi_fb *efifb)
{
	EFI_GRAPHICS_OUTPUT *gop;
	EFI_UGA_DRAW_PROTOCOL *uga;
	EFI_STATUS status;

	status = BS->LocateProtocol(&gop_guid, NULL, (VOID **)&gop);
	if (status == EFI_SUCCESS)
		return (efifb_from_gop(efifb, gop->Mode, gop->Mode->Info));

	status = BS->LocateProtocol(&uga_guid, NULL, (VOID **)&uga);
	if (status == EFI_SUCCESS)
		return (efifb_from_uga(efifb, uga));

	return (1);
}

static void
print_efifb(int mode, struct efi_fb *efifb, int verbose)
{
	u_int depth;

	if (mode >= 0)
		printf("mode %d: ", mode);
	depth = efifb_color_depth(efifb);
	printf("%ux%ux%u, stride=%u", efifb->fb_width, efifb->fb_height,
	    depth, efifb->fb_stride);
	if (verbose) {
		printf("\n    frame buffer: address=%jx, size=%jx",
		    (uintmax_t)efifb->fb_addr, (uintmax_t)efifb->fb_size);
		printf("\n    color mask: R=%08x, G=%08x, B=%08x\n",
		    efifb->fb_mask_red, efifb->fb_mask_green,
		    efifb->fb_mask_blue);
	}
}

static bool
efi_resolution_compare(struct named_resolution *res, const char *cmp)
{

	if (strcasecmp(res->name, cmp) == 0)
		return (true);
	if (res->alias != NULL && strcasecmp(res->alias, cmp) == 0)
		return (true);
	return (false);
}


static void
efi_get_max_resolution(int *width, int *height)
{
	struct named_resolution *res;
	char *maxres;
	char *height_start, *width_start;
	int idx;

	*width = *height = 0;
	maxres = getenv("efi_max_resolution");
	/* No max_resolution set? Bail out; choose highest resolution */
	if (maxres == NULL)
		return;
	/* See if it matches one of our known resolutions */
	for (idx = 0; idx < nitems(resolutions); ++idx) {
		res = &resolutions[idx];
		if (efi_resolution_compare(res, maxres)) {
			*width = res->width;
			*height = res->height;
			return;
		}
	}
	/* Not a known resolution, try to parse it; make a copy we can modify */
	maxres = strdup(maxres);
	if (maxres == NULL)
		return;
	height_start = strchr(maxres, 'x');
	if (height_start == NULL) {
		free(maxres);
		return;
	}
	width_start = maxres;
	*height_start++ = 0;
	/* Errors from this will effectively mean "no max" */
	*width = (int)strtol(width_start, NULL, 0);
	*height = (int)strtol(height_start, NULL, 0);
	free(maxres);
}

static int
gop_autoresize(EFI_GRAPHICS_OUTPUT *gop)
{
	struct efi_fb efifb;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
	EFI_STATUS status;
	UINTN infosz;
	UINT32 best_mode, currdim, maxdim, mode;
	int height, max_height, max_width, width;

	best_mode = maxdim = 0;
	efi_get_max_resolution(&max_width, &max_height);
	for (mode = 0; mode < gop->Mode->MaxMode; mode++) {
		status = gop->QueryMode(gop, mode, &infosz, &info);
		if (EFI_ERROR(status))
			continue;
		efifb_from_gop(&efifb, gop->Mode, info);
		width = info->HorizontalResolution;
		height = info->VerticalResolution;
		currdim = width * height;
		if (currdim > maxdim) {
			if ((max_width != 0 && width > max_width) ||
			    (max_height != 0 && height > max_height))
				continue;
			maxdim = currdim;
			best_mode = mode;
		}
	}

	if (maxdim != 0) {
		status = gop->SetMode(gop, best_mode);
		if (EFI_ERROR(status)) {
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "gop_autoresize: Unable to set mode to %u (error=%lu)",
			    mode, EFI_ERROR_CODE(status));
			return (CMD_ERROR);
		}
	}
	return (CMD_OK);
}

static int
text_autoresize()
{
	SIMPLE_TEXT_OUTPUT_INTERFACE *conout;
	EFI_STATUS status;
	UINTN i, max_dim, best_mode, cols, rows;

	conout = ST->ConOut;
	max_dim = best_mode = 0;
	for (i = 0; i < conout->Mode->MaxMode; i++) {
		status = conout->QueryMode(conout, i, &cols, &rows);
		if (EFI_ERROR(status))
			continue;
		if (cols * rows > max_dim) {
			max_dim = cols * rows;
			best_mode = i;
		}
	}
	if (max_dim > 0)
		conout->SetMode(conout, best_mode);
	return (CMD_OK);
}

static int
uga_autoresize(EFI_UGA_DRAW_PROTOCOL *uga)
{

	return (text_autoresize());
}

COMMAND_SET(efi_autoresize, "efi-autoresizecons", "EFI Auto-resize Console", command_autoresize);

static int
command_autoresize(int argc, char *argv[])
{
	EFI_GRAPHICS_OUTPUT *gop;
	EFI_UGA_DRAW_PROTOCOL *uga;
	char *textmode;
	EFI_STATUS status;
	u_int mode;

	textmode = getenv("hw.vga.textmode");
	/* If it's set and non-zero, we'll select a console mode instead */
	if (textmode != NULL && strcmp(textmode, "0") != 0)
		return (text_autoresize());

	gop = NULL;
	uga = NULL;
	status = BS->LocateProtocol(&gop_guid, NULL, (VOID **)&gop);
	if (EFI_ERROR(status) == 0)
		return (gop_autoresize(gop));

	status = BS->LocateProtocol(&uga_guid, NULL, (VOID **)&uga);
	if (EFI_ERROR(status) == 0)
		return (uga_autoresize(uga));

	snprintf(command_errbuf, sizeof(command_errbuf),
	    "%s: Neither Graphics Output Protocol nor Universal Graphics Adapter present",
	    argv[0]);

	/*
	 * Default to text_autoresize if we have neither GOP or UGA.  This won't
	 * give us the most ideal resolution, but it will at least leave us
	 * functional rather than failing the boot for an objectively bad
	 * reason.
	 */
	return (text_autoresize());
}

COMMAND_SET(gop, "gop", "graphics output protocol", command_gop);

static int
command_gop(int argc, char *argv[])
{
	struct efi_fb efifb;
	EFI_GRAPHICS_OUTPUT *gop;
	EFI_STATUS status;
	u_int mode;

	status = BS->LocateProtocol(&gop_guid, NULL, (VOID **)&gop);
	if (EFI_ERROR(status)) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "%s: Graphics Output Protocol not present (error=%lu)",
		    argv[0], EFI_ERROR_CODE(status));
		return (CMD_ERROR);
	}

	if (argc < 2)
		goto usage;

	if (!strcmp(argv[1], "set")) {
		char *cp;

		if (argc != 3)
			goto usage;
		mode = strtol(argv[2], &cp, 0);
		if (cp[0] != '\0') {
			sprintf(command_errbuf, "mode is an integer");
			return (CMD_ERROR);
		}
		status = gop->SetMode(gop, mode);
		if (EFI_ERROR(status)) {
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "%s: Unable to set mode to %u (error=%lu)",
			    argv[0], mode, EFI_ERROR_CODE(status));
			return (CMD_ERROR);
		}
	} else if (!strcmp(argv[1], "get")) {
		if (argc != 2)
			goto usage;
		efifb_from_gop(&efifb, gop->Mode, gop->Mode->Info);
		print_efifb(gop->Mode->Mode, &efifb, 1);
		printf("\n");
	} else if (!strcmp(argv[1], "list")) {
		EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
		UINTN infosz;

		if (argc != 2)
			goto usage;
		pager_open();
		for (mode = 0; mode < gop->Mode->MaxMode; mode++) {
			status = gop->QueryMode(gop, mode, &infosz, &info);
			if (EFI_ERROR(status))
				continue;
			efifb_from_gop(&efifb, gop->Mode, info);
			print_efifb(mode, &efifb, 0);
			if (pager_output("\n"))
				break;
		}
		pager_close();
	}
	return (CMD_OK);

 usage:
	snprintf(command_errbuf, sizeof(command_errbuf),
	    "usage: %s [list | get | set <mode>]", argv[0]);
	return (CMD_ERROR);
}

COMMAND_SET(uga, "uga", "universal graphics adapter", command_uga);

static int
command_uga(int argc, char *argv[])
{
	struct efi_fb efifb;
	EFI_UGA_DRAW_PROTOCOL *uga;
	EFI_STATUS status;

	status = BS->LocateProtocol(&uga_guid, NULL, (VOID **)&uga);
	if (EFI_ERROR(status)) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "%s: UGA Protocol not present (error=%lu)",
		    argv[0], EFI_ERROR_CODE(status));
		return (CMD_ERROR);
	}

	if (argc != 1)
		goto usage;

	if (efifb_from_uga(&efifb, uga) != CMD_OK) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "%s: Unable to get UGA information", argv[0]);
		return (CMD_ERROR);
	}

	print_efifb(-1, &efifb, 1);
	printf("\n");
	return (CMD_OK);

 usage:
	snprintf(command_errbuf, sizeof(command_errbuf), "usage: %s", argv[0]);
	return (CMD_ERROR);
}
