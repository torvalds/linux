/*	$OpenBSD: efiboot.c,v 1.44 2025/09/16 05:07:33 yasuoka Exp $	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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
#include <dev/cons.h>
#include <dev/isa/isareg.h>
#include <dev/ic/comreg.h>
#include <sys/disklabel.h>
#include <cmd.h>
#include <stand/boot/bootarg.h>
#include <machine/pio.h>

#include "libsa.h"
#include "disk.h"

#include <efi.h>
#include <efiapi.h>
#include <efiprot.h>
#include <eficonsctl.h>

#include "efidev.h"
#include "efiboot.h"
#include "run_i386.h"

#define	KERN_LOADSPACE_SIZE	(64 * 1024 * 1024)

#define EFI_OS_INDICATIONS_BOOT_TO_FW_UI	1ULL

EFI_SYSTEM_TABLE	*ST;
EFI_BOOT_SERVICES	*BS;
EFI_RUNTIME_SERVICES	*RS;
EFI_HANDLE		 IH;
EFI_DEVICE_PATH		*efi_bootdp = NULL;
EFI_PHYSICAL_ADDRESS	 heap;
EFI_LOADED_IMAGE	*loadedImage;
UINTN			 heapsiz = 1 * 1024 * 1024;
UINTN			 mmap_key;
static EFI_GUID		 imgp_guid = LOADED_IMAGE_PROTOCOL;
static EFI_GUID		 blkio_guid = BLOCK_IO_PROTOCOL;
static EFI_GUID		 devp_guid = DEVICE_PATH_PROTOCOL;
u_long			 efi_loadaddr;

int	 efi_device_path_depth(EFI_DEVICE_PATH *dp, int);
int	 efi_device_path_ncmp(EFI_DEVICE_PATH *, EFI_DEVICE_PATH *, int);
static void	 efi_heap_init(void);
static int	 efi_memprobe_internal(void);
static int	 efi_update_bios_memmap(void);
static void	 efi_video_init(void);
static void	 efi_video_reset(void);
static EFI_STATUS
		 efi_gop_setmode(int mode);
EFI_STATUS	 efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *);

void (*run_i386)(u_long, u_long, int, int, int, int, int, int, int, int)
    __attribute__((noreturn));

extern int bios_bootdev;

EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
	extern char		*progname;
	EFI_LOADED_IMAGE	*imgp;
	EFI_DEVICE_PATH		*dp0 = NULL, *dp;
	EFI_STATUS		 status;
	EFI_PHYSICAL_ADDRESS	 stack;

	ST = systab;
	BS = ST->BootServices;
	RS = ST->RuntimeServices;
	IH = image;

	/* disable reset by watchdog after 5 minutes */
	BS->SetWatchdogTimer(0, 0, 0, NULL);

	efi_video_init();
	efi_heap_init();

	status = BS->HandleProtocol(image, &imgp_guid, (void **)&imgp);
	if (status == EFI_SUCCESS)
		status = BS->HandleProtocol(imgp->DeviceHandle, &devp_guid,
		    (void **)&dp0);
	if (status == EFI_SUCCESS) {
		for (dp = dp0; !IsDevicePathEnd(dp);
		    dp = NextDevicePathNode(dp)) {
			if (DevicePathType(dp) == MEDIA_DEVICE_PATH &&
			    (DevicePathSubType(dp) == MEDIA_HARDDRIVE_DP ||
 			    DevicePathSubType(dp) == MEDIA_CDROM_DP)) {
				bios_bootdev =
				    (DevicePathSubType(dp) == MEDIA_CDROM_DP)
				    ? 0x1e0 : 0x80;
				efi_bootdp = dp0;
				break;
			} else if (DevicePathType(dp) == MESSAGING_DEVICE_PATH&&
			    DevicePathSubType(dp) == MSG_MAC_ADDR_DP) {
				bios_bootdev = 0x0;
				efi_bootdp = dp0;
				break;
			}
		}
	}

#ifdef __amd64__
	/* allocate run_i386_start() on heap */
	if ((run_i386 = alloc(run_i386_size)) == NULL)
		panic("alloc() failed");
	memcpy(run_i386, run_i386_start, run_i386_size);
#endif

	/* can't use sa_cleanup since printf is used after sa_cleanup() */
	/* sa_cleanup = efi_cleanup; */

#ifdef __amd64__
	progname = "BOOTX64";
#else
	progname = "BOOTIA32";
#endif

	/*
	 * Move the stack before calling boot().  UEFI on some machines
	 * locate the stack on our kernel load address.
	 */
	stack = heap + heapsiz;
#if defined(__amd64__)
	asm("movq	%0, %%rsp;"
	    "mov	%1, %%edi;"
	    "call	boot;"
	    :: "r"(stack - 32), "r"(bios_bootdev));
#else
	asm("movl	%0, %%esp;"
	    "movl	%1, (%%esp);"
	    "call	boot;"
	    :: "r"(stack - 32), "r"(bios_bootdev));
#endif
	/* must not reach here */
	return (EFI_SUCCESS);
}

void
efi_cleanup(void)
{
	int		 retry;
	EFI_STATUS	 status;

	/* retry once in case of failure */
	for (retry = 1; retry >= 0; retry--) {
		efi_memprobe_internal();	/* sync the current map */
		status = BS->ExitBootServices(IH, mmap_key);
		if (status == EFI_SUCCESS)
			break;
		if (retry == 0)
			panic("ExitBootServices failed (%d)", status);
	}
}

/***********************************************************************
 * Disk
 ***********************************************************************/
struct disklist_lh efi_disklist;

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

	TAILQ_INIT(&efi_disklist);

	sz = 0;
	status = BS->LocateHandle(ByProtocol, &blkio_guid, 0, &sz, 0);
	if (status == EFI_BUFFER_TOO_SMALL) {
		handles = alloc(sz);
		status = BS->LocateHandle(ByProtocol, &blkio_guid,
		    0, &sz, handles);
	}
	if (handles == NULL || EFI_ERROR(status))
		panic("BS->LocateHandle() returns %d", status);

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
			TAILQ_INSERT_HEAD(&efi_disklist, di, list);
			bootdev = 1;
			continue;
		}
next:
		TAILQ_INSERT_TAIL(&efi_disklist, di, list);
	}

	free(handles, sz);
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

/***********************************************************************
 * Memory
 ***********************************************************************/
bios_memmap_t		 bios_memmap[128];
bios_efiinfo_t		 bios_efiinfo;
int			 bios_memmap_modified = 0;

static void
efi_heap_init(void)
{
	EFI_STATUS	 status;

	heap = HEAP_LIMIT;
	status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(heapsiz), &heap);
	if (status != EFI_SUCCESS)
		panic("BS->AllocatePages()");
}

void
efi_memprobe(void)
{
	u_int		 n = 0;
	bios_memmap_t	*bm;
	EFI_STATUS	 status;
	EFI_PHYSICAL_ADDRESS
			 addr = 0x10000000ULL;	/* Below 256MB */
	int		 error;

	status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(KERN_LOADSPACE_SIZE), &addr);
	if (status != EFI_SUCCESS)
		panic("BS->AllocatePages()");
	efi_loadaddr = addr;

	printf(" mem[");
	error = efi_memprobe_internal();
	for (bm = bios_memmap; bm->type != BIOS_MAP_END; bm++) {
		if (bm->type == BIOS_MAP_FREE && bm->size > 12 * 1024) {
			if (n++ != 0)
				printf(" ");
			if (bm->size > 1024 * 1024)
				printf("%uM", bm->size / 1024 / 1024);
			else
				printf("%uK", bm->size / 1024);
		}
	}
	if (error == E2BIG)
		printf(" overflow");
	printf("]");
}

static int
efi_memprobe_internal(void)
{
	EFI_STATUS		 status;
	UINTN			 mapkey, mmsiz, siz;
	UINT32			 mmver;
	EFI_MEMORY_DESCRIPTOR	*mm;

	if (bios_efiinfo.mmap_start != 0)
		free((void *)bios_efiinfo.mmap_start, bios_efiinfo.mmap_size);

	siz = 0;
	status = BS->GetMemoryMap(&siz, NULL, &mapkey, &mmsiz, &mmver);
	if (status != EFI_BUFFER_TOO_SMALL)
		panic("cannot get the size of memory map");
	mm = alloc(siz);
	status = BS->GetMemoryMap(&siz, mm, &mapkey, &mmsiz, &mmver);
	if (status != EFI_SUCCESS)
		panic("cannot get the memory map");

	mmap_key = mapkey;

	bios_efiinfo.mmap_desc_ver = mmver;
	bios_efiinfo.mmap_desc_size = mmsiz;
	bios_efiinfo.mmap_size = siz;
	bios_efiinfo.mmap_start = (uintptr_t)mm;

	if (bios_memmap_modified)
		return (0);

	return (efi_update_bios_memmap());
}

static int
efi_update_bios_memmap(void)
{
	int			 i, n, error = 0;
	bios_memmap_t		*bm, bm0;
	EFI_MEMORY_DESCRIPTOR	*mm;

	cnvmem = extmem = 0;
	bios_memmap[0].type = BIOS_MAP_END;
	n = bios_efiinfo.mmap_size / bios_efiinfo.mmap_desc_size;
	for (i = 0, mm = (EFI_MEMORY_DESCRIPTOR *)bios_efiinfo.mmap_start;
	    i < n;
	    i++, mm = NextMemoryDescriptor(mm, bios_efiinfo.mmap_desc_size)) {
		bm0.type = BIOS_MAP_END;
		bm0.addr = mm->PhysicalStart;
		bm0.size = mm->NumberOfPages * EFI_PAGE_SIZE;
		if (mm->Type == EfiReservedMemoryType ||
		    mm->Type == EfiUnusableMemory ||
		    mm->Type == EfiRuntimeServicesCode ||
		    mm->Type == EfiRuntimeServicesData)
			bm0.type = BIOS_MAP_RES;
		else if (mm->Type == EfiLoaderCode ||
		    mm->Type == EfiLoaderData ||
		    mm->Type == EfiBootServicesCode ||
		    mm->Type == EfiBootServicesData ||
		    mm->Type == EfiConventionalMemory)
			bm0.type = BIOS_MAP_FREE;
		else if (mm->Type == EfiACPIReclaimMemory)
			bm0.type = BIOS_MAP_ACPI;
		else if (mm->Type == EfiACPIMemoryNVS)
			bm0.type = BIOS_MAP_NVS;
		else
			/*
			 * XXX Is there anything to do for EfiMemoryMappedIO
			 * XXX EfiMemoryMappedIOPortSpace EfiPalCode?
			 */
			bm0.type = BIOS_MAP_RES;

		for (bm = bios_memmap; bm->type != BIOS_MAP_END; bm++) {
			if (bm->type != bm0.type)
				continue;
			if (bm->addr <= bm0.addr &&
			    bm0.addr <= bm->addr + bm->size) {
				bm->size = bm0.addr + bm0.size - bm->addr;
				break;
			} else if (bm0.addr <= bm->addr &&
			    bm->addr <= bm0.addr + bm0.size) {
				bm->size = bm->addr + bm->size - bm0.addr;
				bm->addr = bm0.addr;
				break;
			}
		}
		if (bm->type == BIOS_MAP_END) {
			if (bm == &bios_memmap[nitems(bios_memmap) - 1]) {
				error = E2BIG;
				break;
			}
			*bm = bm0;
			(++bm)->type = BIOS_MAP_END;
		}
	}
	for (bm = bios_memmap; bm->type != BIOS_MAP_END; bm++) {
		if (bm->addr < IOM_BEGIN)	/* Below memory hole */
			cnvmem =
			    max(cnvmem, (bm->addr + bm->size) / 1024);
		if (bm->addr >= IOM_END /* Above the memory hole */ &&
		    bm->addr / 1024 == extmem + 1024)
			extmem += bm->size / 1024;
	}

	return (error);
}

/***********************************************************************
 * Console
 ***********************************************************************/
static SIMPLE_TEXT_OUTPUT_INTERFACE	*conout = NULL;
static SIMPLE_INPUT_INTERFACE		*conin;
static EFI_GRAPHICS_OUTPUT		*gop = NULL;
static EFI_GUID				 con_guid
					    = EFI_CONSOLE_CONTROL_PROTOCOL_GUID;
static EFI_GUID				 gop_guid
					    = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID				 serio_guid
					    = SERIAL_IO_PROTOCOL;
struct efi_video {
	int	cols;
	int	rows;
} efi_video[32];

static void
efi_video_init(void)
{
	EFI_CONSOLE_CONTROL_PROTOCOL	*conctrl = NULL;
	int				 i, mode80x25, mode100x31;
	UINTN				 cols, rows;
	EFI_STATUS			 status;
	EFI_HANDLE			*handles;
	UINTN				 nhandles;
	EFI_GRAPHICS_OUTPUT		*first_gop = NULL;
	EFI_DEVICE_PATH			*devp_test = NULL;

	status = BS->LocateHandleBuffer(ByProtocol, &gop_guid, NULL, &nhandles,
		&handles);
	if (!EFI_ERROR(status)) {
		for (i = 0; i < nhandles; i++) {
			status = BS->HandleProtocol(handles[i], &gop_guid,
			    (void **)&gop);
			if (first_gop == NULL)
				first_gop = gop;
			status = BS->HandleProtocol(handles[i], &devp_guid,
			    (void **)&devp_test);
			if (status == EFI_SUCCESS)
				break;
		}
		if (status != EFI_SUCCESS)
			gop = first_gop;
		BS->FreePool(handles);
	}

	conout = ST->ConOut;
	status = BS->LocateProtocol(&con_guid, NULL, (void **)&conctrl);
	if (status == EFI_SUCCESS)
		conctrl->SetMode(conctrl, EfiConsoleControlScreenText);
	mode80x25 = -1;
	mode100x31 = -1;
	for (i = 0; i < conout->Mode->MaxMode; i++) {
		status = conout->QueryMode(conout, i, &cols, &rows);
		if (EFI_ERROR(status))
			continue;
		if (mode80x25 < 0 && cols == 80 && rows == 25)
			mode80x25 = i;
		if (mode100x31 < 0 && cols == 100 && rows == 31)
			mode100x31 = i;
		if (i < nitems(efi_video)) {
			efi_video[i].cols = cols;
			efi_video[i].rows = rows;
		}
	}
	if (mode100x31 >= 0)
		conout->SetMode(conout, mode100x31);
	else if (mode80x25 >= 0)
		conout->SetMode(conout, mode80x25);
	conin = ST->ConIn;
	efi_video_reset();
}

static void
efi_video_reset(void)
{
	conout->EnableCursor(conout, TRUE);
	conout->SetAttribute(conout, EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
	conout->ClearScreen(conout);
}

void
efi_cons_probe(struct consdev *cn)
{
	cn->cn_pri = CN_MIDPRI;
	cn->cn_dev = makedev(12, 0);
	printf(" pc%d", minor(cn->cn_dev));
}

void
efi_cons_init(struct consdev *cp)
{
}

int
efi_cons_getc(dev_t dev)
{
	EFI_INPUT_KEY	 key;
	EFI_STATUS	 status;
	UINTN		 dummy;
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
		BS->WaitForEvent(1, &conin->WaitForKey, &dummy);
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

int
efi_cons_getshifts(dev_t dev)
{
	/* XXX */
	return (0);
}

int com_addr = -1;
int com_speed = -1;

static SERIAL_IO_INTERFACE	*serios[4];
const int comports[4] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };

/* call with sp == 0 to query the current speed */
int
pio_comspeed(dev_t dev, int sp)
{
	int port = (com_addr == -1) ? comports[minor(dev)] : com_addr;
	int i, newsp;
	int err;

	if (sp <= 0)
		return com_speed;
	/* valid baud rate? */
	if (115200 < sp || sp < 75)
		return -1;

	/*
	 * Accepted speeds:
	 *   75 150 300 600 1200 2400 4800 9600 19200 38400 76800 and
	 *   14400 28800 57600 115200
	 */
	for (i = sp; i != 75 && i != 14400; i >>= 1)
		if (i & 1)
			return -1;

/* ripped screaming from dev/ic/com.c */
#define divrnd(n, q)    (((n)*2/(q)+1)/2)       /* divide and round off */
	newsp = divrnd((COM_FREQ / 16), sp);
	if (newsp <= 0)
		return -1;
	err = divrnd((COM_FREQ / 16) * 1000, sp * newsp) - 1000;
	if (err < 0)
		err = -err;
	if (err > COM_TOLERANCE)
		return -1;
#undef  divrnd

	if (com_speed != -1 && cn_tab && cn_tab->cn_dev == dev &&
	    com_speed != sp) {
		printf("com%d: changing speed to %d baud in 5 seconds, "
		    "change your terminal to match!\n\a",
		    minor(dev), sp);
		sleep(5);
	}

	outb(port + com_cfcr, LCR_DLAB);
	outb(port + com_dlbl, newsp);
	outb(port + com_dlbh, newsp>>8);
	outb(port + com_cfcr, LCR_8BITS);
	if (com_speed != -1)
		printf("\ncom%d: %d baud\n", minor(dev), sp);

	newsp = com_speed;
	com_speed = sp;
	return newsp;
}

int
pio_com_getc(dev_t dev)
{
	int port = (com_addr == -1) ? comports[minor(dev & 0x7f)] : com_addr;

	if (dev & 0x80)
		return (inb(port + com_lsr) & LSR_RXRDY);

	while ((inb(port + com_lsr) & LSR_RXRDY) == 0)
		;

	return (inb(port + com_data) & 0xff);
}

void
pio_com_putc(dev_t dev, int c)
{
	int port = (com_addr == -1) ? comports[minor(dev)] : com_addr;

	while ((inb(port + com_lsr) & LSR_TXRDY) == 0)
		;

	outb(port + com_data, c);
}

void
efi_com_probe(struct consdev *cn)
{
	EFI_HANDLE		*handles = NULL;
	SERIAL_IO_INTERFACE	*serio;
	EFI_STATUS		 status;
	EFI_DEVICE_PATH		*dp, *dp0;
	EFI_DEV_PATH_PTR	 dpp;
	UINTN			 sz;
	int			 i, uid = -1;

	cn->cn_pri = CN_LOWPRI;
	cn->cn_dev = makedev(8, 0);

	sz = 0;
	status = BS->LocateHandle(ByProtocol, &serio_guid, 0, &sz, 0);
	if (status == EFI_BUFFER_TOO_SMALL) {
		handles = alloc(sz);
		status = BS->LocateHandle(ByProtocol, &serio_guid,
		    0, &sz, handles);
	}
	if (handles == NULL || EFI_ERROR(status)) {
		free(handles, sz);
		return;
	}

	for (i = 0; i < sz / sizeof(EFI_HANDLE); i++) {
		/*
		 * Identify port number of the handle.  This assumes ACPI
		 * UID 0-3 map to legacy COM[1-4] and they use the legacy
		 * port address.
		 */
		status = BS->HandleProtocol(handles[i], &devp_guid,
		    (void **)&dp0);
		if (EFI_ERROR(status))
			continue;
		uid = -1;
		for (dp = dp0; !IsDevicePathEnd(dp);
		    dp = NextDevicePathNode(dp)) {
			dpp = (EFI_DEV_PATH_PTR)dp;
			if (DevicePathType(dp) == ACPI_DEVICE_PATH &&
			    DevicePathSubType(dp) == ACPI_DP)
				if (dpp.Acpi->HID == EFI_PNP_ID(0x0501)) {
					uid = dpp.Acpi->UID;
					break;
				}
		}
		if (uid < 0 || nitems(serios) <= uid)
			continue;

		/* Prepare SERIAL_IO_INTERFACE */
		status = BS->HandleProtocol(handles[i], &serio_guid,
		    (void **)&serio);
		if (EFI_ERROR(status))
			continue;
		serios[uid] = serio;
	}
	free(handles, sz);

	for (i = 0; i < nitems(serios); i++) {
		if (serios[i] != NULL)
			printf(" com%d", i);
	}
}

int
efi_valid_com(dev_t dev)
{
	return (minor(dev) < nitems(serios) && serios[minor(dev)] != NULL);
}

int
comspeed(dev_t dev, int sp)
{
	EFI_STATUS		 status;
	SERIAL_IO_INTERFACE	*serio = serios[minor(dev)];
	int			 newsp;

	if (sp <= 0)
		return com_speed;

	if (!efi_valid_com(dev))
		return pio_comspeed(dev, sp);

	if (serio->Mode->BaudRate != sp) {
		status = serio->SetAttributes(serio, sp,
		    serio->Mode->ReceiveFifoDepth,
		    serio->Mode->Timeout, serio->Mode->Parity,
		    serio->Mode->DataBits, serio->Mode->StopBits);
		if (EFI_ERROR(status)) {
			printf("com%d: SetAttribute() failed with status=%d\n",
			    minor(dev), status);
			return (-1);
		}
		if (com_speed != -1)
			printf("\ncom%d: %d baud\n", minor(dev), sp);
	}

	/* same as comspeed() in libsa/bioscons.c */
	newsp = com_speed;
	com_speed = sp;

	return (newsp);
}

void
efi_com_init(struct consdev *cn)
{
	if (!efi_valid_com(cn->cn_dev))
		/* This actually happens if the machine has another serial.  */
		return;

	if (com_speed == -1)
		comspeed(cn->cn_dev, 9600); /* default speed is 9600 baud */
}

int
efi_com_getc(dev_t dev)
{
	EFI_STATUS		 status;
	SERIAL_IO_INTERFACE	*serio;
	UINTN			 sz;
	u_char			 buf;
	static u_char		 lastchar = 0;

	if (!efi_valid_com(dev & 0x7f))
		return pio_com_getc(dev);
	serio = serios[minor(dev & 0x7f)];

	if (lastchar != 0) {
		int r = lastchar;
		if ((dev & 0x80) == 0)
			lastchar = 0;
		return (r);
	}

	for (;;) {
		sz = 1;
		status = serio->Read(serio, &sz, &buf);
		if (status == EFI_SUCCESS && sz > 0)
			break;
		if (status != EFI_TIMEOUT && EFI_ERROR(status))
			panic("Error reading from serial status=%d", status);
		if (dev & 0x80)
			return (0);
	}

	if (dev & 0x80)
		lastchar = buf;

	return (buf);
}

void
efi_com_putc(dev_t dev, int c)
{
	SERIAL_IO_INTERFACE	*serio;
	UINTN			 sz = 1;
	u_char			 buf;

	if (!efi_valid_com(dev)) {
		pio_com_putc(dev, c);
		return;
	}
	serio = serios[minor(dev)];
	buf = c;
	serio->Write(serio, &sz, &buf);
}

/***********************************************************************
 * Miscellaneous
 ***********************************************************************/
/*
 * ACPI GUID is confusing in UEFI spec.
 * {EFI_,}_ACPI_20_TABLE_GUID or EFI_ACPI_TABLE_GUID means
 * ACPI 2.0 or above.
 */
static EFI_GUID			 acpi_guid = ACPI_20_TABLE_GUID;
static EFI_GUID			 smbios_guid = SMBIOS_TABLE_GUID;
static EFI_GUID			 esrt_guid = EFI_SYSTEM_RESOURCE_TABLE_GUID;
static int			 gopmode = -1;

#define	efi_guidcmp(_a, _b)	memcmp((_a), (_b), sizeof(EFI_GUID))

static EFI_STATUS
efi_gop_setmode(int mode)
{
	EFI_STATUS	status;

	status = gop->SetMode(gop, mode);
	if (EFI_ERROR(status) || gop->Mode->Mode != mode)
		printf("GOP SetMode() failed (%d)\n", status);

	return (status);
}

void
efi_makebootargs(void)
{
	int			 i;
	EFI_STATUS		 status;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION
				*gopi;
	bios_efiinfo_t		*ei = &bios_efiinfo;
	int			 curmode;
	UINTN			 sz, gopsiz, bestsiz = 0;

	/*
	 * ACPI, BIOS configuration table
	 */
	for (i = 0; i < ST->NumberOfTableEntries; i++) {
		if (efi_guidcmp(&acpi_guid,
		    &ST->ConfigurationTable[i].VendorGuid) == 0)
			ei->config_acpi = (uintptr_t)
			    ST->ConfigurationTable[i].VendorTable;
		else if (efi_guidcmp(&smbios_guid,
		    &ST->ConfigurationTable[i].VendorGuid) == 0)
			ei->config_smbios = (uintptr_t)
			    ST->ConfigurationTable[i].VendorTable;
		else if (efi_guidcmp(&esrt_guid,
		    &ST->ConfigurationTable[i].VendorGuid) == 0)
			ei->config_esrt = (uintptr_t)
			    ST->ConfigurationTable[i].VendorTable;
	}

	/*
	 * Need to copy ESRT because call to ExitBootServices() frees memory of
	 * type EfiBootServicesData in which ESRT resides.
	 */
	if (ei->config_esrt != 0) {
		EFI_SYSTEM_RESOURCE_TABLE *esrt =
		    (EFI_SYSTEM_RESOURCE_TABLE *)ei->config_esrt;
		size_t esrt_size = sizeof(*esrt) +
		    esrt->FwResourceCount * sizeof(EFI_SYSTEM_RESOURCE_ENTRY);
		void *esrt_copy;

		/*
		 * Using EfiRuntimeServicesData as it maps to BIOS_MAP_RES,
		 * while EfiLoaderData becomes BIOS_MAP_FREE.
		 */
		status = BS->AllocatePool(EfiRuntimeServicesData,
		    esrt_size, &esrt_copy);
		if (status == EFI_SUCCESS) {
			memcpy(esrt_copy, esrt, esrt_size);
			ei->config_esrt = (uintptr_t)esrt_copy;
			ei->flags |= BEI_ESRT;
		}
	}

	/*
	 * Frame buffer
	 */
	if (gop != NULL) {
		if (gopmode < 0) {
			for (i = 0; i < gop->Mode->MaxMode; i++) {
				status = gop->QueryMode(gop, i, &sz, &gopi);
				if (EFI_ERROR(status))
					continue;
				gopsiz = gopi->HorizontalResolution *
				    gopi->VerticalResolution;
				if (gopsiz > bestsiz) {
					gopmode = i;
					bestsiz = gopsiz;
				}
			}
		}
		if (gopmode >= 0 && gopmode != gop->Mode->Mode) {
			curmode = gop->Mode->Mode;
			if (efi_gop_setmode(gopmode) != EFI_SUCCESS)
				(void)efi_gop_setmode(curmode);
		}

		gopi = gop->Mode->Info;
		switch (gopi->PixelFormat) {
		case PixelBlueGreenRedReserved8BitPerColor:
			ei->fb_red_mask      = 0x00ff0000;
			ei->fb_green_mask    = 0x0000ff00;
			ei->fb_blue_mask     = 0x000000ff;
			ei->fb_reserved_mask = 0xff000000;
			break;
		case PixelRedGreenBlueReserved8BitPerColor:
			ei->fb_red_mask      = 0x000000ff;
			ei->fb_green_mask    = 0x0000ff00;
			ei->fb_blue_mask     = 0x00ff0000;
			ei->fb_reserved_mask = 0xff000000;
			break;
		case PixelBitMask:
			ei->fb_red_mask = gopi->PixelInformation.RedMask;
			ei->fb_green_mask = gopi->PixelInformation.GreenMask;
			ei->fb_blue_mask = gopi->PixelInformation.BlueMask;
			ei->fb_reserved_mask =
			    gopi->PixelInformation.ReservedMask;
			break;
		default:
			break;
		}
		ei->fb_addr = gop->Mode->FrameBufferBase;
		ei->fb_size = gop->Mode->FrameBufferSize;
		ei->fb_height = gopi->VerticalResolution;
		ei->fb_width = gopi->HorizontalResolution;
		ei->fb_pixpsl = gopi->PixelsPerScanLine;
	}

	/*
	 * EFI system table
	 */
	ei->system_table = (uintptr_t)ST;

#ifdef __amd64__
	ei->flags |= BEI_64BIT;
#endif

	addbootarg(BOOTARG_EFIINFO, sizeof(bios_efiinfo), &bios_efiinfo);
}

/* Vendor device path used to indicate the mmio UART on AMD SoCs. */
#define AMDSOC_DEVPATH \
	{ 0xe76fd4e9, 0x0a30, 0x4ca9, \
	    { 0x95, 0x40, 0xd7, 0x99, 0x53, 0x4c, 0xc4, 0xff } }

void
efi_setconsdev(void)
{
	bios_consdev_t cd;
	EFI_STATUS status;
	UINT8 data[128];
	UINTN size = sizeof(data);
	EFI_DEVICE_PATH *dp = (void *)data;
	VENDOR_DEVICE_PATH *vdp;
	UART_DEVICE_PATH *udp;
	EFI_GUID global = EFI_GLOBAL_VARIABLE;
	EFI_GUID amdsoc = AMDSOC_DEVPATH;

	memset(&cd, 0, sizeof(cd));
	cd.consdev = cn_tab->cn_dev;
	cd.conspeed = com_speed;
	cd.consaddr = com_addr;

	/*
	 * If the ConOut variable indicates we're using a serial
	 * console, use it to determine the baud rate.
	 */
	status = RS->GetVariable(L"ConOut", &global, NULL, &size, &data);
	if (status == EFI_SUCCESS) {
		for (dp = (void *)data; !IsDevicePathEnd(dp);
		     dp = NextDevicePathNode(dp)) {
			/*
			 * AMD Ryzen Embedded V1000 SoCs integrate a
			 * Synopsys DesignWare UART that is not
			 * compatible with the traditional 8250 UART
			 * found on the IBM PC.  Pass the magic
			 * parameters to the kernel to make this UART
			 * work.
			 */
			if (DevicePathType(dp) == HARDWARE_DEVICE_PATH &&
			    DevicePathSubType(dp) == HW_VENDOR_DP) {
				vdp = (VENDOR_DEVICE_PATH *)dp;
				if (efi_guidcmp(&vdp->Guid, &amdsoc) == 0) {
					cd.consdev = makedev(8, 4);
					cd.consaddr = *(uint64_t *)(vdp + 1);
					cd.consfreq = 48000000;
					cd.flags = BCD_MMIO;
					cd.reg_width = 4;
					cd.reg_shift = 2;
				}
			}

			if (DevicePathType(dp) == MESSAGING_DEVICE_PATH &&
			    DevicePathSubType(dp) == MSG_UART_DP) {
				udp = (UART_DEVICE_PATH *)dp;
				if (cd.conspeed == -1)
					cd.conspeed = udp->BaudRate;
			}
		}
	}

	addbootarg(BOOTARG_CONSDEV, sizeof(cd), &cd);
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

time_t
getsecs(void)
{
	EFI_TIME	t;
	time_t		r = 0;
	int		y = 0;
	const int	daytab[][14] = {
	    { 0, -1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333, 364 },
	    { 0, -1, 30, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	};
#define isleap(_y) (((_y) % 4) == 0 && (((_y) % 100) != 0 || ((_y) % 400) == 0))

	ST->RuntimeServices->GetTime(&t, NULL);

	/* Calc days from UNIX epoch */
	r = (t.Year - 1970) * 365;
	for (y = 1970; y < t.Year; y++) {
		if (isleap(y))
			r++;
	}
	r += daytab[isleap(t.Year)? 1 : 0][t.Month] + t.Day;

	/* Calc secs */
	r *= 60 * 60 * 24;
	r += ((t.Hour * 60) + t.Minute) * 60 + t.Second;
	if (-24 * 60 < t.TimeZone && t.TimeZone < 24 * 60)
		r += t.TimeZone * 60;

	return (r);
}

u_int
sleep(u_int i)
{
	time_t t;
	u_int intr = 0;

	/*
	 * Loop for the requested number of seconds, polling,
	 * so that it may handle interrupts.
	 */
	for (t = getsecs() + i; intr == 0 && getsecs() < t; intr = cnischar())
		;

	return intr;
}

#ifdef IDLE_POWEROFF
CHAR16		*idle_name = L"IdlePoweroff";
EFI_STATUS	 idle_status;
/* randomly generated f948e8a9-0570-4338-ad10-29f4cf12849d */
EFI_GUID	 openbsd_guid = { 0xf948e8a9, 0x0570, 0x4338,
    { 0xad, 0x10, 0x29, 0xf4, 0xcf, 0x12, 0x84, 0x9d } };
/* Non-Volatile, Boot Service Access, Runtime Service Access */
UINT32		 idle_attrs = 0x1 | 0x2 | 0x4;
UINT16		 idle_secs;
UINTN		 idle_sz = sizeof(idle_secs);

int
get_idle_timeout(void)
{
	idle_status = RS->GetVariable(idle_name, &openbsd_guid, NULL,
	    &idle_sz, &idle_secs);
	if (idle_status != EFI_SUCCESS) {
		if (idle_status != EFI_NOT_FOUND) {
			printf("%s: %d\n", __func__, idle_status);
			return 1;
		}
		return -1;
	}
	return 0;
}

int
set_idle_timeout(int secs)
{
	idle_secs = secs;
	idle_sz = idle_secs > 0 ? sizeof(idle_secs) : 0;
	idle_status = RS->SetVariable(idle_name, &openbsd_guid, idle_attrs,
	    idle_sz, &idle_secs);
	if (idle_status != EFI_SUCCESS) {
		printf("%s: %d\n", __func__, idle_status);
		return -1;
	}
	return 0;
}

/* see lib/libsa/softraid.c sr_crypto_passphrase_decrypt() */
void
idle_poweroff(void)
{
	if (get_idle_timeout() == 0 && sleep(idle_secs) == 0) {
		printf("\nno input after %us, powering off...\n", idle_secs);
		Xpoweroff_efi();
	}
}
#endif /* IDLE_POWEROFF */

/***********************************************************************
 * Commands
 ***********************************************************************/
int
Xexit_efi(void)
{
	BS->Exit(IH, 0, 0, NULL);
	for (;;)
		continue;
	return (0);
}

int
Xvideo_efi(void)
{
	int	 i, mode = -1;

	if (cmd.argc >= 2) {
		mode = strtol(cmd.argv[1], NULL, 10);
		if (0 <= mode && mode < nitems(efi_video) &&
		    efi_video[mode].cols > 0) {
			conout->SetMode(conout, mode);
			efi_video_reset();
		}
	} else {
		for (i = 0; i < nitems(efi_video) &&
		    i < conout->Mode->MaxMode; i++) {
			if (efi_video[i].cols > 0)
				printf("Mode %d: %d x %d\n", i,
				    efi_video[i].cols,
				    efi_video[i].rows);
		}
		printf("\n");
	}
	printf("Current Mode = %d\n", conout->Mode->Mode);

	return (0);
}

int
Xpoweroff_efi(void)
{
	RS->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
	return (0);
}

int
Xgop_efi(void)
{
	EFI_STATUS	 status;
	int		 i, mode = -1;
	UINTN		 sz;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION
			*gopi;

	if (gop == NULL) {
		printf("No GOP found\n");
		return (0);
	}
	if (cmd.argc >= 2) {
		mode = strtol(cmd.argv[1], NULL, 10);
		if (0 <= mode && mode < gop->Mode->MaxMode) {
			status = gop->QueryMode(gop, mode, &sz, &gopi);
			if (!EFI_ERROR(status)) {
				if (efi_gop_setmode(mode) == EFI_SUCCESS)
					gopmode = mode;
			}
		}
	} else {
		for (i = 0; i < gop->Mode->MaxMode; i++) {
			status = gop->QueryMode(gop, i, &sz, &gopi);
			if (EFI_ERROR(status))
				continue;
			printf("Mode %d: %d x %d (stride = %d)\n", i,
			    gopi->HorizontalResolution,
			    gopi->VerticalResolution,
			    gopi->PixelsPerScanLine);
		}
		printf("\n");
	}
	printf("Current Mode = %d\n", gop->Mode->Mode);

	return (0);
}

#ifdef IDLE_POWEROFF
int
Xidle_efi(void)
{
	if (cmd.argc >= 2) {
		int secs;

		secs = strtol(cmd.argv[1], NULL, 10);
		if (0 <= secs && secs < UINT16_MAX)
			set_idle_timeout(secs);
	} else {
		if (get_idle_timeout() == 0)
			printf("Timeout = %us\n", idle_secs);
	}
	return 0;
}
#endif /* IDLE_POWEROFF */

int
Xfwsetup_efi(void)
{
	UINT64 osind;
	UINTN osind_size = sizeof(osind);
	UINT32 osind_attrs = 0x1 | 0x2 | 0x4;
	EFI_GUID global = EFI_GLOBAL_VARIABLE;
	EFI_STATUS status;

	status = RS->GetVariable(L"OsIndicationsSupported", &global, NULL,
	    &osind_size, &osind);
	if (status == EFI_NOT_FOUND) {
		printf("not supported on this machine.\n");
		return -1;
	} else if (status != EFI_SUCCESS) {
		printf("%s: %d\n", __func__, status);
		return -1;
	}

	if ((osind & EFI_OS_INDICATIONS_BOOT_TO_FW_UI) == 0) {
		printf("not supported on this machine.\n");
		return -1;
	}

	osind = EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
	status = RS->SetVariable(L"OsIndications", &global, osind_attrs,
	    sizeof(osind), &osind);
	if (status != EFI_SUCCESS) {
		printf("%s: %d\n", __func__, status);
		return -1;
	}

	RS->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
	for (;;)
		continue;
	return 0;
}
