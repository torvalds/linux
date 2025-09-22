/*	$OpenBSD: efiboot.c,v 1.11 2024/06/23 13:11:51 kettenis Exp $	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
 * Copyright (c) 2016 Mark Kettenis
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

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <dev/cons.h>
#include <sys/disklabel.h>

#include <efi.h>
#include <efiapi.h>
#include <efiprot.h>
#include <eficonsctl.h>

#include <dev/biovar.h>
#include <dev/softraidvar.h>

#include <lib/libkern/libkern.h>
#include <lib/libsa/softraid.h>
#include <stand/boot/cmd.h>

#include "libsa.h"
#include "disk.h"
#include "softraid_riscv64.h"

#include "efidev.h"
#include "efiboot.h"
#include "efidt.h"
#include "fdt.h"

EFI_SYSTEM_TABLE	*ST;
EFI_BOOT_SERVICES	*BS;
EFI_RUNTIME_SERVICES	*RS;
EFI_HANDLE		 IH, efi_bootdp;
void			*fdt_sys = NULL;
void			*fdt_override = NULL;
size_t			 fdt_override_size;

EFI_PHYSICAL_ADDRESS	 heap;
UINTN			 heapsiz = 1 * 1024 * 1024;
EFI_MEMORY_DESCRIPTOR	*mmap;
UINTN			 mmap_key;
UINTN			 mmap_ndesc;
UINTN			 mmap_descsiz;
UINT32			 mmap_version;

static EFI_GUID		 imgp_guid = LOADED_IMAGE_PROTOCOL;
static EFI_GUID		 blkio_guid = BLOCK_IO_PROTOCOL;
static EFI_GUID		 devp_guid = DEVICE_PATH_PROTOCOL;
static EFI_GUID		 gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID		 fdt_guid = FDT_TABLE_GUID;
static EFI_GUID		 dt_fixup_guid = EFI_DT_FIXUP_PROTOCOL_GUID;

#define efi_guidcmp(_a, _b)	memcmp((_a), (_b), sizeof(EFI_GUID))

int efi_device_path_depth(EFI_DEVICE_PATH *dp, int);
int efi_device_path_ncmp(EFI_DEVICE_PATH *, EFI_DEVICE_PATH *, int);
static void efi_heap_init(void);
static void efi_memprobe_internal(void);
static void efi_timer_init(void);
static void efi_timer_cleanup(void);
static EFI_STATUS efi_memprobe_find(UINTN, UINTN, EFI_PHYSICAL_ADDRESS *);
void *efi_fdt(void);
int fdt_load_override(char *);

EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
	extern char		*progname;
	EFI_LOADED_IMAGE	*imgp;
	EFI_DEVICE_PATH		*dp = NULL;
	EFI_STATUS		 status;
	int			 i;

	ST = systab;
	BS = ST->BootServices;
	RS = ST->RuntimeServices;
	IH = image;

	/* disable reset by watchdog after 5 minutes */
	BS->SetWatchdogTimer(0, 0, 0, NULL);

	status = BS->HandleProtocol(image, &imgp_guid, (void **)&imgp);
	if (status == EFI_SUCCESS)
		status = BS->HandleProtocol(imgp->DeviceHandle, &devp_guid,
		    (void **)&dp);
	if (status == EFI_SUCCESS)
		efi_bootdp = dp;

	for (i = 0; i < ST->NumberOfTableEntries; i++) {
		if (efi_guidcmp(&fdt_guid,
		    &ST->ConfigurationTable[i].VendorGuid) == 0)
			fdt_sys = ST->ConfigurationTable[i].VendorTable;
	}
	fdt_init(fdt_sys);

	progname = "BOOTRISCV64";

	boot(0);

	return (EFI_SUCCESS);
}

static SIMPLE_TEXT_OUTPUT_INTERFACE *conout;
static SIMPLE_INPUT_INTERFACE *conin;

/*
 * The device majors for these don't match the ones used by the
 * kernel.  That's fine.  They're just used as an index into the cdevs
 * array and never passed on to the kernel.
 */
static dev_t serial = makedev(0, 0);
static dev_t framebuffer = makedev(1, 0);

static char framebuffer_path[128];

void
efi_cons_probe(struct consdev *cn)
{
	cn->cn_pri = CN_MIDPRI;
	cn->cn_dev = serial;
}

void
efi_cons_init(struct consdev *cp)
{
	conin = ST->ConIn;
	conout = ST->ConOut;
}

int
efi_cons_getc(dev_t dev)
{
	EFI_INPUT_KEY	 key;
	EFI_STATUS	 status;
#if 0
	UINTN		 dummy;
#endif
	static int	 lastchar = 0;

	if (lastchar) {
		int r = lastchar;
		if ((dev & 0x80) == 0)
			lastchar = 0;
		return (r);
	}

	status = conin->ReadKeyStroke(conin, &key);
	while (status == EFI_NOT_READY || key.UnicodeChar == 0) {
		if (dev & 0x80)
			return (0);
		/*
		 * XXX The implementation of WaitForEvent() in U-boot
		 * is broken and neverreturns.
		 */
#if 0
		BS->WaitForEvent(1, &conin->WaitForKey, &dummy);
#endif
		status = conin->ReadKeyStroke(conin, &key);
	}

	if (dev & 0x80)
		lastchar = key.UnicodeChar;

	return (key.UnicodeChar);
}

void
efi_cons_putc(dev_t dev, int c)
{
	CHAR16	buf[2];

	if (c == '\n')
		efi_cons_putc(dev, '\r');

	buf[0] = c;
	buf[1] = 0;

	conout->OutputString(conout, buf);
}

void
efi_fb_probe(struct consdev *cn)
{
	cn->cn_pri = CN_LOWPRI;
	cn->cn_dev = framebuffer;
}

void
efi_fb_init(struct consdev *cn)
{
	conin = ST->ConIn;
	conout = ST->ConOut;
}

int
efi_fb_getc(dev_t dev)
{
	return efi_cons_getc(dev);
}

void
efi_fb_putc(dev_t dev, int c)
{
	efi_cons_putc(dev, c);
}

static void
efi_heap_init(void)
{
	EFI_STATUS	 status;

	status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(heapsiz), &heap);
	if (status != EFI_SUCCESS)
		panic("BS->AllocatePages()");
}

struct disklist_lh disklist;
struct diskinfo *bootdev_dip;

void
efi_diskprobe(void)
{
	int			 i, bootdev = 0, depth = -1;
	UINTN			 sz;
	EFI_STATUS		 status;
	EFI_HANDLE		*handles = NULL;
	EFI_BLOCK_IO		*blkio;
	EFI_BLOCK_IO_MEDIA	*media;
	struct diskinfo		*di;
	EFI_DEVICE_PATH		*dp;

	TAILQ_INIT(&disklist);

	sz = 0;
	status = BS->LocateHandle(ByProtocol, &blkio_guid, 0, &sz, 0);
	if (status == EFI_BUFFER_TOO_SMALL) {
		handles = alloc(sz);
		status = BS->LocateHandle(ByProtocol, &blkio_guid, 0, &sz,
		    handles);
	}
	if (handles == NULL || EFI_ERROR(status))
		return;

	if (efi_bootdp != NULL)
		depth = efi_device_path_depth(efi_bootdp, MEDIA_DEVICE_PATH);

	/*
	 * U-Boot incorrectly represents devices with a single
	 * MEDIA_DEVICE_PATH component.  In that case include that
	 * component into the matching, otherwise we'll blindly select
	 * the first device.
	 */
	if (depth == 0)
		depth = 1;

	for (i = 0; i < sz / sizeof(EFI_HANDLE); i++) {
		status = BS->HandleProtocol(handles[i], &blkio_guid,
		    (void **)&blkio);
		if (EFI_ERROR(status))
			panic("BS->HandleProtocol() returns %d", status);

		media = blkio->Media;
		if (media->LogicalPartition || !media->MediaPresent)
			continue;
		di = alloc(sizeof(struct diskinfo));
		efid_init(di, blkio);

		if (efi_bootdp == NULL || depth == -1 || bootdev != 0)
			goto next;
		status = BS->HandleProtocol(handles[i], &devp_guid,
		    (void **)&dp);
		if (EFI_ERROR(status))
			goto next;
		if (efi_device_path_ncmp(efi_bootdp, dp, depth) == 0) {
			TAILQ_INSERT_HEAD(&disklist, di, list);
			bootdev_dip = di;
			bootdev = 1;
			continue;
		}
next:
		TAILQ_INSERT_TAIL(&disklist, di, list);
	}

	free(handles, sz);

	/* Print available disks and probe for softraid. */
	i = 0;
	printf("disks:");
	TAILQ_FOREACH(di, &disklist, list) {
		printf(" sd%d%s", i, di == bootdev_dip ? "*" : "");
		i++;
	}
	srprobe();
	printf("\n");
}

/*
 * Determine the number of nodes up to, but not including, the first
 * node of the specified type.
 */
int
efi_device_path_depth(EFI_DEVICE_PATH *dp, int dptype)
{
	int	i;

	for (i = 0; !IsDevicePathEnd(dp); dp = NextDevicePathNode(dp), i++) {
		if (DevicePathType(dp) == dptype)
			return (i);
	}

	return (i);
}

int
efi_device_path_ncmp(EFI_DEVICE_PATH *dpa, EFI_DEVICE_PATH *dpb, int deptn)
{
	int	 i, cmp;

	for (i = 0; i < deptn; i++) {
		if (IsDevicePathEnd(dpa) || IsDevicePathEnd(dpb))
			return ((IsDevicePathEnd(dpa) && IsDevicePathEnd(dpb))
			    ? 0 : (IsDevicePathEnd(dpa))? -1 : 1);
		cmp = DevicePathNodeLength(dpa) - DevicePathNodeLength(dpb);
		if (cmp)
			return (cmp);
		cmp = memcmp(dpa, dpb, DevicePathNodeLength(dpa));
		if (cmp)
			return (cmp);
		dpa = NextDevicePathNode(dpa);
		dpb = NextDevicePathNode(dpb);
	}

	return (0);
}

void
efi_framebuffer(void)
{
	EFI_GRAPHICS_OUTPUT *gop;
	EFI_STATUS status;
	void *node, *child;
	uint32_t acells, scells;
	uint64_t base, size;
	uint32_t reg[4];
	uint32_t width, height, stride;
	char *format;
	char *prop;

	/*
	 * Don't create a "simple-framebuffer" node if we already have
	 * one.  Besides "/chosen", we also check under "/" since that
	 * is where the Raspberry Pi firmware puts it.
	 */
	node = fdt_find_node("/chosen");
	for (child = fdt_child_node(node); child;
	     child = fdt_next_node(child)) {
		if (!fdt_node_is_compatible(child, "simple-framebuffer"))
			continue;
		if (!fdt_node_property(child, "status", &prop) ||
		    strcmp(prop, "okay") == 0) {
			strlcpy(framebuffer_path, "/chosen/",
			    sizeof(framebuffer_path));
			strlcat(framebuffer_path, fdt_node_name(child),
			    sizeof(framebuffer_path));
			return;
		}
	}
	node = fdt_find_node("/");
	for (child = fdt_child_node(node); child;
	     child = fdt_next_node(child)) {
		if (!fdt_node_is_compatible(child, "simple-framebuffer"))
			continue;
		if (!fdt_node_property(child, "status", &prop) ||
		    strcmp(prop, "okay") == 0) {
			strlcpy(framebuffer_path, "/",
			    sizeof(framebuffer_path));
			strlcat(framebuffer_path, fdt_node_name(child),
			    sizeof(framebuffer_path));
			return;
		}
	}

	status = BS->LocateProtocol(&gop_guid, NULL, (void **)&gop);
	if (status != EFI_SUCCESS)
		return;

	/* Paranoia! */
	if (gop == NULL || gop->Mode == NULL || gop->Mode->Info == NULL)
		return;

	/* We only support 32-bit pixel modes for now. */
	switch (gop->Mode->Info->PixelFormat) {
	case PixelRedGreenBlueReserved8BitPerColor:
		format = "x8b8g8r8";
		break;
	case PixelBlueGreenRedReserved8BitPerColor:
		format = "x8r8g8b8";
		break;
	default:
		return;
	}

	base = gop->Mode->FrameBufferBase;
	size = gop->Mode->FrameBufferSize;
	width = htobe32(gop->Mode->Info->HorizontalResolution);
	height = htobe32(gop->Mode->Info->VerticalResolution);
	stride = htobe32(gop->Mode->Info->PixelsPerScanLine * 4);

	node = fdt_find_node("/");
	if (fdt_node_property_int(node, "#address-cells", &acells) != 1)
		acells = 1;
	if (fdt_node_property_int(node, "#size-cells", &scells) != 1)
		scells = 1;
	if (acells > 2 || scells > 2)
		return;
	if (acells >= 1)
		reg[0] = htobe32(base);
	if (acells == 2) {
		reg[1] = reg[0];
		reg[0] = htobe32(base >> 32);
	}
	if (scells >= 1)
		reg[acells] = htobe32(size);
	if (scells == 2) {
		reg[acells + 1] = reg[acells];
		reg[acells] = htobe32(size >> 32);
	}

	node = fdt_find_node("/chosen");
	fdt_node_add_node(node, "framebuffer", &child);
	fdt_node_add_property(child, "status", "okay", strlen("okay") + 1);
	fdt_node_add_property(child, "format", format, strlen(format) + 1);
	fdt_node_add_property(child, "stride", &stride, 4);
	fdt_node_add_property(child, "height", &height, 4);
	fdt_node_add_property(child, "width", &width, 4);
	fdt_node_add_property(child, "reg", reg, (acells + scells) * 4);
	fdt_node_add_property(child, "compatible",
	    "simple-framebuffer", strlen("simple-framebuffer") + 1);

	strlcpy(framebuffer_path, "/chosen/framebuffer",
	    sizeof(framebuffer_path));
}

void
efi_console(void)
{
	void *node;

	if (cn_tab->cn_dev != framebuffer)
		return;

	if (strlen(framebuffer_path) == 0)
		return;

	/* Point stdout-path at the framebuffer node. */
	node = fdt_find_node("/chosen");
	fdt_node_add_property(node, "stdout-path",
	    framebuffer_path, strlen(framebuffer_path) + 1);
}

uint64_t dma_constraint[2] = { 0, -1 };

void
efi_dma_constraint(void)
{
	void *node;

	/* StarFive JH71x0 has peripherals that only support 32-bit DMA. */
	node = fdt_find_node("/");
	if (fdt_node_is_compatible(node, "starfive,jh7100") ||
	    fdt_node_is_compatible(node, "starfive,jh7110"))
		dma_constraint[1] = htobe64(0xffffffff);

	/* Pass DMA constraint. */
	node = fdt_find_node("/chosen");
	fdt_node_add_property(node, "openbsd,dma-constraint",
	    dma_constraint, sizeof(dma_constraint));
}

char *bootmac = NULL;

void *
efi_makebootargs(char *bootargs, int howto)
{
	struct sr_boot_volume *bv;
	u_char bootduid[8];
	u_char zero[8] = { 0 };
	uint64_t uefi_system_table = htobe64((uintptr_t)ST);
	uint32_t boothowto = htobe32(howto);
	int32_t hartid;
	EFI_PHYSICAL_ADDRESS addr;
	void *node, *fdt;
	size_t len;

	fdt = efi_fdt();
	if (fdt == NULL)
		return NULL;

	if (!fdt_get_size(fdt))
		return NULL;

	len = roundup(fdt_get_size(fdt) + PAGE_SIZE, PAGE_SIZE);
	if (BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(len), &addr) == EFI_SUCCESS) {
		memcpy((void *)addr, fdt, fdt_get_size(fdt));
		((struct fdt_head *)addr)->fh_size = htobe32(len);
		fdt = (void *)addr;
	}

	if (!fdt_init(fdt))
		return NULL;

	/* Create common nodes which might not exist when using mach dtb */
	node = fdt_find_node("/aliases");
	if (node == NULL)
		fdt_node_add_node(fdt_find_node("/"), "aliases", &node);
	node = fdt_find_node("/chosen");
	if (node == NULL)
		fdt_node_add_node(fdt_find_node("/"), "chosen", &node);

	node = fdt_find_node("/chosen");
	hartid = efi_get_boot_hart_id();
	if (hartid >= 0) {
		hartid = htobe32(hartid);
		fdt_node_add_property(node, "boot-hartid", &hartid, 4);
	}

	len = strlen(bootargs) + 1;
	fdt_node_add_property(node, "bootargs", bootargs, len);
	fdt_node_add_property(node, "openbsd,boothowto",
	    &boothowto, sizeof(boothowto));

	/* Pass DUID of the boot disk. */
	if (bootdev_dip) {
		memcpy(&bootduid, bootdev_dip->disklabel.d_uid,
		    sizeof(bootduid));
		if (memcmp(bootduid, zero, sizeof(bootduid)) != 0) {
			fdt_node_add_property(node, "openbsd,bootduid",
			    bootduid, sizeof(bootduid));
		}

		if (bootdev_dip->sr_vol != NULL) {
			bv = bootdev_dip->sr_vol;
			fdt_node_add_property(node, "openbsd,sr-bootuuid",
			    &bv->sbv_uuid, sizeof(bv->sbv_uuid));
			if (bv->sbv_maskkey != NULL)
				fdt_node_add_property(node,
				    "openbsd,sr-bootkey", bv->sbv_maskkey,
				    SR_CRYPTO_MAXKEYBYTES);
		}
	}

	sr_clear_keys();

	/* Pass netboot interface address. */
	if (bootmac)
		fdt_node_add_property(node, "openbsd,bootmac", bootmac, 6);

	/* Pass EFI system table. */
	fdt_node_add_property(node, "openbsd,uefi-system-table",
	    &uefi_system_table, sizeof(uefi_system_table));

	/* Placeholders for EFI memory map. */
	fdt_node_add_property(node, "openbsd,uefi-mmap-start", zero, 8);
	fdt_node_add_property(node, "openbsd,uefi-mmap-size", zero, 4);
	fdt_node_add_property(node, "openbsd,uefi-mmap-desc-size", zero, 4);
	fdt_node_add_property(node, "openbsd,uefi-mmap-desc-ver", zero, 4);

	efi_framebuffer();
	efi_console();
	efi_dma_constraint();

	fdt_finalize();

	return fdt;
}

void
efi_updatefdt(void)
{
	uint64_t uefi_mmap_start = htobe64((uintptr_t)mmap);
	uint32_t uefi_mmap_size = htobe32(mmap_ndesc * mmap_descsiz);
	uint32_t uefi_mmap_desc_size = htobe32(mmap_descsiz);
	uint32_t uefi_mmap_desc_ver = htobe32(mmap_version);
	void *node;

	node = fdt_find_node("/chosen");
	if (!node)
		return;

	/* Pass EFI memory map. */
	fdt_node_set_property(node, "openbsd,uefi-mmap-start",
	    &uefi_mmap_start, sizeof(uefi_mmap_start));
	fdt_node_set_property(node, "openbsd,uefi-mmap-size",
	    &uefi_mmap_size, sizeof(uefi_mmap_size));
	fdt_node_set_property(node, "openbsd,uefi-mmap-desc-size",
	    &uefi_mmap_desc_size, sizeof(uefi_mmap_desc_size));
	fdt_node_set_property(node, "openbsd,uefi-mmap-desc-ver",
	    &uefi_mmap_desc_ver, sizeof(uefi_mmap_desc_ver));

	fdt_finalize();
}

u_long efi_loadaddr;

void
machdep(void)
{
	EFI_PHYSICAL_ADDRESS addr;

	cninit();
	efi_heap_init();

	/*
	 * The kernel expects to be loaded into a block of memory aligned
	 * on a 2MB boundary.  We allocate a block of 64MB of memory, which
	 * gives us plenty of room for growth.
	 */
	if (efi_memprobe_find(EFI_SIZE_TO_PAGES(64 * 1024 * 1024),
	    0x200000, &addr) != EFI_SUCCESS)
		printf("Can't allocate memory\n");
	efi_loadaddr = addr;

	efi_timer_init();
	efi_diskprobe();
	efi_pxeprobe();
}

void
efi_cleanup(void)
{
	int		 retry;
	EFI_STATUS	 status;

	efi_timer_cleanup();

	/* retry once in case of failure */
	for (retry = 1; retry >= 0; retry--) {
		efi_memprobe_internal();	/* sync the current map */
		efi_updatefdt();
		status = BS->ExitBootServices(IH, mmap_key);
		if (status == EFI_SUCCESS)
			break;
		if (retry == 0)
			panic("ExitBootServices failed (%d)", status);
	}
}

void
_rtt(void)
{
#ifdef EFI_DEBUG
	printf("Hit any key to reboot\n");
	efi_cons_getc(0);
#endif
	RS->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
	for (;;)
		continue;
}

/*
 * U-Boot only implements the GetTime() Runtime Service if it has been
 * configured with CONFIG_DM_RTC.  Most board configurations don't
 * include that option, so we can't use it to implement our boot
 * prompt timeout.  Instead we use timer events to simulate a clock
 * that ticks ever second.
 */

EFI_EVENT timer;
int ticks;

static VOID
efi_timer(EFI_EVENT event, VOID *context)
{
	ticks++;
}

static void
efi_timer_init(void)
{
	EFI_STATUS status;

	status = BS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
	    efi_timer, NULL, &timer);
	if (status == EFI_SUCCESS)
		status = BS->SetTimer(timer, TimerPeriodic, 10000000);
	if (EFI_ERROR(status))
		printf("Can't create timer\n");
}

static void
efi_timer_cleanup(void)
{
	BS->CloseEvent(timer);
}

time_t
getsecs(void)
{
	return ticks;
}

/*
 * Various device-related bits.
 */

void
devboot(dev_t dev, char *p)
{
	struct sr_boot_volume *bv;
	struct sr_boot_chunk *bc;
	struct diskinfo *dip;
	int sd_boot_vol = 0;
	int sr_boot_vol = -1;
	int part_type = FS_UNUSED;

	if (bootdev_dip == NULL) {
		strlcpy(p, "tftp0a", 7);
		return;
	}

	TAILQ_FOREACH(dip, &disklist, list) {
		if (bootdev_dip == dip)
			break;
		sd_boot_vol++;
	}

	/*
	 * Determine the partition type for the 'a' partition of the
	 * boot device.
	 */
	if ((bootdev_dip->flags & DISKINFO_FLAG_GOODLABEL) != 0)
		part_type = bootdev_dip->disklabel.d_partitions[0].p_fstype;

	/*
	 * See if we booted from a disk that is a member of a bootable
	 * softraid volume.
	 */
	SLIST_FOREACH(bv, &sr_volumes, sbv_link) {
		SLIST_FOREACH(bc, &bv->sbv_chunks, sbc_link)
			if (bc->sbc_diskinfo == bootdev_dip)
				sr_boot_vol = bv->sbv_unit;
		if (sr_boot_vol != -1)
			break;
	}

	if (sr_boot_vol != -1 && part_type != FS_BSDFFS) {
		strlcpy(p, "sr0a", 5);
		p[2] = '0' + sr_boot_vol;
		return;
	}

	strlcpy(p, "sd0a", 5);
	p[2] = '0' + sd_boot_vol;
}

const char cdevs[][4] = { "com", "fb" };
const int ncdevs = nitems(cdevs);

int
cnspeed(dev_t dev, int sp)
{
	return 115200;
}

char ttyname_buf[8];

char *
ttyname(int fd)
{
	snprintf(ttyname_buf, sizeof ttyname_buf, "%s%d",
	    cdevs[major(cn_tab->cn_dev)], minor(cn_tab->cn_dev));

	return ttyname_buf;
}

dev_t
ttydev(char *name)
{
	int i, unit = -1;
	char *no = name + strlen(name) - 1;

	while (no >= name && *no >= '0' && *no <= '9')
		unit = (unit < 0 ? 0 : (unit * 10)) + *no-- - '0';
	if (no < name || unit < 0)
		return NODEV;
	for (i = 0; i < ncdevs; i++)
		if (strncmp(name, cdevs[i], no - name + 1) == 0)
			return makedev(i, unit);
	return NODEV;
}

#define MAXDEVNAME	16

/*
 * Parse a device spec.
 *
 * [A-Za-z]*[0-9]*[A-Za-z]:file
 *    dev   uint    part
 */
int
devparse(const char *fname, int *dev, int *unit, int *part, const char **file)
{
	const char *s;

	*unit = 0;	/* default to wd0a */
	*part = 0;
	*dev  = 0;

	s = strchr(fname, ':');
	if (s != NULL) {
		int devlen;
		int i, u, p = 0;
		struct devsw *dp;
		char devname[MAXDEVNAME];

		devlen = s - fname;
		if (devlen > MAXDEVNAME)
			return (EINVAL);

		/* extract device name */
		for (i = 0; isalpha(fname[i]) && (i < devlen); i++)
			devname[i] = fname[i];
		devname[i] = 0;

		if (!isdigit(fname[i]))
			return (EUNIT);

		/* device number */
		for (u = 0; isdigit(fname[i]) && (i < devlen); i++)
			u = u * 10 + (fname[i] - '0');

		if (!isalpha(fname[i]))
			return (EPART);

		/* partition number */
		if (i < devlen)
			p = fname[i++] - 'a';

		if (i != devlen)
			return (ENXIO);

		/* check device name */
		for (dp = devsw, i = 0; i < ndevs; dp++, i++) {
			if (dp->dv_name && !strcmp(devname, dp->dv_name))
				break;
		}

		if (i >= ndevs)
			return (ENXIO);

		*unit = u;
		*part = p;
		*dev  = i;
		fname = ++s;
	}

	*file = fname;

	return (0);
}

int
devopen(struct open_file *f, const char *fname, char **file)
{
	struct devsw *dp;
	int dev, unit, part, error;

	error = devparse(fname, &dev, &unit, &part, (const char **)file);
	if (error)
		return (error);

	dp = &devsw[dev];
	f->f_dev = dp;

	if (strcmp("tftp", dp->dv_name) != 0) {
		/*
		 * Clear bootmac, to signal that we loaded this file from a
		 * non-network device.
		 */
		bootmac = NULL;
	}

	return (*dp->dv_open)(f, unit, part);
}

static void
efi_memprobe_internal(void)
{
	EFI_STATUS		 status;
	UINTN			 mapkey, mmsiz, siz;
	UINT32			 mmver;
	EFI_MEMORY_DESCRIPTOR	*mm;
	int			 n;

	free(mmap, mmap_ndesc * mmap_descsiz);

	siz = 0;
	status = BS->GetMemoryMap(&siz, NULL, &mapkey, &mmsiz, &mmver);
	if (status != EFI_BUFFER_TOO_SMALL)
		panic("cannot get the size of memory map");
	mm = alloc(siz);
	status = BS->GetMemoryMap(&siz, mm, &mapkey, &mmsiz, &mmver);
	if (status != EFI_SUCCESS)
		panic("cannot get the memory map");
	n = siz / mmsiz;
	mmap = mm;
	mmap_key = mapkey;
	mmap_ndesc = n;
	mmap_descsiz = mmsiz;
	mmap_version = mmver;
}

/*
 * 64-bit ARMs can have a much wider memory mapping, as in somewhere
 * after the 32-bit region.  To cope with our alignment requirement,
 * use the memory table to find a place where we can fit.
 */
static EFI_STATUS
efi_memprobe_find(UINTN pages, UINTN align, EFI_PHYSICAL_ADDRESS *addr)
{
	EFI_MEMORY_DESCRIPTOR	*mm;
	int			 i, j;

	if (align < EFI_PAGE_SIZE)
		return EFI_INVALID_PARAMETER;

	efi_memprobe_internal();	/* sync the current map */

	for (i = 0, mm = mmap; i < mmap_ndesc;
	    i++, mm = NextMemoryDescriptor(mm, mmap_descsiz)) {
		if (mm->Type != EfiConventionalMemory)
			continue;

		if (mm->NumberOfPages < pages)
			continue;

		for (j = 0; j < mm->NumberOfPages; j++) {
			EFI_PHYSICAL_ADDRESS paddr;

			if (mm->NumberOfPages - j < pages)
				break;

			paddr = mm->PhysicalStart + (j * EFI_PAGE_SIZE);
			if (paddr & (align - 1))
				continue;

			if (BS->AllocatePages(AllocateAddress, EfiLoaderData,
			    pages, &paddr) == EFI_SUCCESS) {
				*addr = paddr;
				return EFI_SUCCESS;
			}
		}
	}
	return EFI_OUT_OF_RESOURCES;
}

void *
efi_fdt(void)
{
	/* 'mach dtb' has precedence */
	if (fdt_override != NULL)
		return fdt_override;

	return fdt_sys;
}

int
fdt_load_override(char *file)
{
	EFI_DT_FIXUP_PROTOCOL *dt_fixup;
	EFI_PHYSICAL_ADDRESS addr;
	char path[MAXPATHLEN];
	EFI_STATUS status;
	struct stat sb;
	size_t dt_size;
	UINTN sz;
	int fd;

	if (file == NULL && fdt_override) {
		BS->FreePages((uint64_t)fdt_override,
		    EFI_SIZE_TO_PAGES(fdt_override_size));
		fdt_override = NULL;
		fdt_init(fdt_sys);
		return 0;
	}

	snprintf(path, sizeof(path), "%s:%s", cmd.bootdev, file);

	fd = open(path, O_RDONLY);
	if (fd < 0 || fstat(fd, &sb) == -1) {
		printf("cannot open %s\n", path);
		return 0;
	}
	dt_size = sb.st_size;
retry:
	if (efi_memprobe_find(EFI_SIZE_TO_PAGES(dt_size),
	    PAGE_SIZE, &addr) != EFI_SUCCESS) {
		printf("cannot allocate memory for %s\n", path);
		return 0;
	}
	if (read(fd, (void *)addr, sb.st_size) != sb.st_size) {
		printf("cannot read from %s\n", path);
		return 0;
	}

	status = BS->LocateProtocol(&dt_fixup_guid, NULL, (void **)&dt_fixup);
	if (status == EFI_SUCCESS) {
		sz = dt_size;
		status = dt_fixup->Fixup(dt_fixup, (void *)addr, &sz,
		    EFI_DT_APPLY_FIXUPS | EFI_DT_RESERVE_MEMORY);
		if (status == EFI_BUFFER_TOO_SMALL) {
			BS->FreePages(addr, EFI_SIZE_TO_PAGES(dt_size));
			lseek(fd, 0, SEEK_SET);
			dt_size = sz;
			goto retry;
		}
		if (status != EFI_SUCCESS)
			panic("DT fixup failed: 0x%lx", status);
	}

	if (!fdt_init((void *)addr)) {
		printf("invalid device tree\n");
		BS->FreePages(addr, EFI_SIZE_TO_PAGES(dt_size));
		return 0;
	}

	if (fdt_override) {
		BS->FreePages((uint64_t)fdt_override,
		    EFI_SIZE_TO_PAGES(fdt_override_size));
		fdt_override = NULL;
	}

	fdt_override = (void *)addr;
	fdt_override_size = dt_size;
	return 0;
}

/*
 * Commands
 */

int Xdtb_efi(void);
int Xexit_efi(void);
int Xpoweroff_efi(void);

const struct cmd_table cmd_machine[] = {
	{ "dtb",	CMDT_CMD, Xdtb_efi },
	{ "exit",	CMDT_CMD, Xexit_efi },
	{ "poweroff",	CMDT_CMD, Xpoweroff_efi },
	{ NULL, 0 }
};

int
Xdtb_efi(void)
{
	if (cmd.argc == 1) {
		fdt_load_override(NULL);
		return (0);
	}

	if (cmd.argc != 2) {
		printf("dtb file\n");
		return (0);
	}

	return fdt_load_override(cmd.argv[1]);
}

int
Xexit_efi(void)
{
	BS->Exit(IH, 0, 0, NULL);
	for (;;)
		continue;
	return (0);
}

int
Xpoweroff_efi(void)
{
	RS->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
	return (0);
}
