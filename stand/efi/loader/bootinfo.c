/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2004, 2006 Marcel Moolenaar
 * Copyright (c) 2014 The FreeBSD Foundation
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <string.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/reboot.h>
#include <sys/boot.h>
#include <machine/cpufunc.h>
#include <machine/elf.h>
#include <machine/metadata.h>
#include <machine/psl.h>

#include <efi.h>
#include <efilib.h>

#include "bootstrap.h"
#include "loader_efi.h"

#if defined(__amd64__)
#include <machine/specialreg.h>
#endif

#include "framebuffer.h"

#if defined(LOADER_FDT_SUPPORT)
#include <fdt_platform.h>
#endif

#ifdef LOADER_GELI_SUPPORT
#include "geliboot.h"
#endif

int bi_load(char *args, vm_offset_t *modulep, vm_offset_t *kernendp);

extern EFI_SYSTEM_TABLE	*ST;

static int
bi_getboothowto(char *kargs)
{
	const char *sw, *tmp;
	char *opts;
	char *console;
	int howto, speed, port;
	char buf[50];

	howto = boot_parse_cmdline(kargs);
	howto |= boot_env_to_howto();

	console = getenv("console");
	if (console != NULL) {
		if (strcmp(console, "comconsole") == 0)
			howto |= RB_SERIAL;
		if (strcmp(console, "nullconsole") == 0)
			howto |= RB_MUTE;
#if defined(__i386__) || defined(__amd64__)
		if (strcmp(console, "efi") == 0 &&
		    getenv("efi_8250_uid") != NULL &&
		    getenv("hw.uart.console") == NULL) {
			/*
			 * If we found a 8250 com port and com speed, we need to
			 * tell the kernel where the serial port is, and how
			 * fast. Ideally, we'd get the port from ACPI, but that
			 * isn't running in the loader. Do the next best thing
			 * by allowing it to be set by a loader.conf variable,
			 * either a EFI specific one, or the compatible
			 * comconsole_port if not. PCI support is needed, but
			 * for that we'd ideally refactor the
			 * libi386/comconsole.c code to have identical behavior.
			 * We only try to set the port for cases where we saw
			 * the Serial(x) node when parsing, otherwise
			 * specialized hardware that has Uart nodes will have a
			 * bogus address set.
			 * But if someone specifically setup hw.uart.console,
			 * don't override that.
			 */
			speed = -1;
			port = -1;
			tmp = getenv("efi_com_speed");
			if (tmp != NULL)
				speed = strtol(tmp, NULL, 0);
			tmp = getenv("efi_com_port");
			if (tmp == NULL)
				tmp = getenv("comconsole_port");
			if (tmp != NULL)
				port = strtol(tmp, NULL, 0);
			if (speed != -1 && port != -1) {
				snprintf(buf, sizeof(buf), "io:%d,br:%d", port,
				    speed);
				env_setenv("hw.uart.console", EV_VOLATILE, buf,
				    NULL, NULL);
			}
		}
#endif
	}

	return (howto);
}

/*
 * Copy the environment into the load area starting at (addr).
 * Each variable is formatted as <name>=<value>, with a single nul
 * separating each variable, and a double nul terminating the environment.
 */
static vm_offset_t
bi_copyenv(vm_offset_t start)
{
	struct env_var *ep;
	vm_offset_t addr, last;
	size_t len;

	addr = last = start;

	/* Traverse the environment. */
	for (ep = environ; ep != NULL; ep = ep->ev_next) {
		len = strlen(ep->ev_name);
		if ((size_t)archsw.arch_copyin(ep->ev_name, addr, len) != len)
			break;
		addr += len;
		if (archsw.arch_copyin("=", addr, 1) != 1)
			break;
		addr++;
		if (ep->ev_value != NULL) {
			len = strlen(ep->ev_value);
			if ((size_t)archsw.arch_copyin(ep->ev_value, addr, len) != len)
				break;
			addr += len;
		}
		if (archsw.arch_copyin("", addr, 1) != 1)
			break;
		last = ++addr;
	}

	if (archsw.arch_copyin("", last++, 1) != 1)
		last = start;
	return(last);
}

/*
 * Copy module-related data into the load area, where it can be
 * used as a directory for loaded modules.
 *
 * Module data is presented in a self-describing format.  Each datum
 * is preceded by a 32-bit identifier and a 32-bit size field.
 *
 * Currently, the following data are saved:
 *
 * MOD_NAME	(variable)		module name (string)
 * MOD_TYPE	(variable)		module type (string)
 * MOD_ARGS	(variable)		module parameters (string)
 * MOD_ADDR	sizeof(vm_offset_t)	module load address
 * MOD_SIZE	sizeof(size_t)		module size
 * MOD_METADATA	(variable)		type-specific metadata
 */
#define	COPY32(v, a, c) {					\
	uint32_t x = (v);					\
	if (c)							\
		archsw.arch_copyin(&x, a, sizeof(x));		\
	a += sizeof(x);						\
}

#define	MOD_STR(t, a, s, c) {					\
	COPY32(t, a, c);					\
	COPY32(strlen(s) + 1, a, c);				\
	if (c)							\
		archsw.arch_copyin(s, a, strlen(s) + 1);	\
	a += roundup(strlen(s) + 1, sizeof(u_long));		\
}

#define	MOD_NAME(a, s, c)	MOD_STR(MODINFO_NAME, a, s, c)
#define	MOD_TYPE(a, s, c)	MOD_STR(MODINFO_TYPE, a, s, c)
#define	MOD_ARGS(a, s, c)	MOD_STR(MODINFO_ARGS, a, s, c)

#define	MOD_VAR(t, a, s, c) {					\
	COPY32(t, a, c);					\
	COPY32(sizeof(s), a, c);				\
	if (c)							\
		archsw.arch_copyin(&s, a, sizeof(s));		\
	a += roundup(sizeof(s), sizeof(u_long));		\
}

#define	MOD_ADDR(a, s, c)	MOD_VAR(MODINFO_ADDR, a, s, c)
#define	MOD_SIZE(a, s, c)	MOD_VAR(MODINFO_SIZE, a, s, c)

#define	MOD_METADATA(a, mm, c) {				\
	COPY32(MODINFO_METADATA | mm->md_type, a, c);		\
	COPY32(mm->md_size, a, c);				\
	if (c)							\
		archsw.arch_copyin(mm->md_data, a, mm->md_size);	\
	a += roundup(mm->md_size, sizeof(u_long));		\
}

#define	MOD_END(a, c) {						\
	COPY32(MODINFO_END, a, c);				\
	COPY32(0, a, c);					\
}

static vm_offset_t
bi_copymodules(vm_offset_t addr)
{
	struct preloaded_file *fp;
	struct file_metadata *md;
	int c;
	uint64_t v;

	c = addr != 0;
	/* Start with the first module on the list, should be the kernel. */
	for (fp = file_findfile(NULL, NULL); fp != NULL; fp = fp->f_next) {
		MOD_NAME(addr, fp->f_name, c); /* This must come first. */
		MOD_TYPE(addr, fp->f_type, c);
		if (fp->f_args)
			MOD_ARGS(addr, fp->f_args, c);
		v = fp->f_addr;
#if defined(__arm__)
		v -= __elfN(relocation_offset);
#endif
		MOD_ADDR(addr, v, c);
		v = fp->f_size;
		MOD_SIZE(addr, v, c);
		for (md = fp->f_metadata; md != NULL; md = md->md_next)
			if (!(md->md_type & MODINFOMD_NOCOPY))
				MOD_METADATA(addr, md, c);
	}
	MOD_END(addr, c);
	return(addr);
}

static EFI_STATUS
efi_do_vmap(EFI_MEMORY_DESCRIPTOR *mm, UINTN sz, UINTN mmsz, UINT32 mmver)
{
	EFI_MEMORY_DESCRIPTOR *desc, *viter, *vmap;
	EFI_STATUS ret;
	int curr, ndesc, nset;

	nset = 0;
	desc = mm;
	ndesc = sz / mmsz;
	vmap = malloc(sz);
	if (vmap == NULL)
		/* This isn't really an EFI error case, but pretend it is */
		return (EFI_OUT_OF_RESOURCES);
	viter = vmap;
	for (curr = 0; curr < ndesc;
	    curr++, desc = NextMemoryDescriptor(desc, mmsz)) {
		if ((desc->Attribute & EFI_MEMORY_RUNTIME) != 0) {
			++nset;
			desc->VirtualStart = desc->PhysicalStart;
			*viter = *desc;
			viter = NextMemoryDescriptor(viter, mmsz);
		}
	}
	ret = RS->SetVirtualAddressMap(nset * mmsz, mmsz, mmver, vmap);
	free(vmap);
	return (ret);
}

static int
bi_load_efi_data(struct preloaded_file *kfp)
{
	EFI_MEMORY_DESCRIPTOR *mm;
	EFI_PHYSICAL_ADDRESS addr = 0;
	EFI_STATUS status;
	const char *efi_novmap;
	size_t efisz;
	UINTN efi_mapkey;
	UINTN dsz, pages, retry, sz;
	UINT32 mmver;
	struct efi_map_header *efihdr;
	bool do_vmap;

#if defined(__amd64__) || defined(__aarch64__)
	struct efi_fb efifb;

	if (efi_find_framebuffer(&efifb) == 0) {
		printf("EFI framebuffer information:\n");
		printf("addr, size     0x%jx, 0x%jx\n", efifb.fb_addr,
		    efifb.fb_size);
		printf("dimensions     %d x %d\n", efifb.fb_width,
		    efifb.fb_height);
		printf("stride         %d\n", efifb.fb_stride);
		printf("masks          0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
		    efifb.fb_mask_red, efifb.fb_mask_green, efifb.fb_mask_blue,
		    efifb.fb_mask_reserved);

		file_addmetadata(kfp, MODINFOMD_EFI_FB, sizeof(efifb), &efifb);
	}
#endif

	do_vmap = true;
	efi_novmap = getenv("efi_disable_vmap");
	if (efi_novmap != NULL)
		do_vmap = strcasecmp(efi_novmap, "YES") != 0;

	efisz = (sizeof(struct efi_map_header) + 0xf) & ~0xf;

	/*
	 * Assign size of EFI_MEMORY_DESCRIPTOR to keep compatible with
	 * u-boot which doesn't fill this value when buffer for memory
	 * descriptors is too small (eg. 0 to obtain memory map size)
	 */
	dsz = sizeof(EFI_MEMORY_DESCRIPTOR);

	/*
	 * Allocate enough pages to hold the bootinfo block and the
	 * memory map EFI will return to us. The memory map has an
	 * unknown size, so we have to determine that first. Note that
	 * the AllocatePages call can itself modify the memory map, so
	 * we have to take that into account as well. The changes to
	 * the memory map are caused by splitting a range of free
	 * memory into two, so that one is marked as being loader
	 * data.
	 */

	sz = 0;

	/*
	 * Matthew Garrett has observed at least one system changing the
	 * memory map when calling ExitBootServices, causing it to return an
	 * error, probably because callbacks are allocating memory.
	 * So we need to retry calling it at least once.
	 */
	for (retry = 2; retry > 0; retry--) {
		for (;;) {
			status = BS->GetMemoryMap(&sz, mm, &efi_mapkey, &dsz, &mmver);
			if (!EFI_ERROR(status))
				break;

			if (status != EFI_BUFFER_TOO_SMALL) {
				printf("%s: GetMemoryMap error %lu\n", __func__,
	                           EFI_ERROR_CODE(status));
				return (EINVAL);
			}

			if (addr != 0)
				BS->FreePages(addr, pages);

			/* Add 10 descriptors to the size to allow for
			 * fragmentation caused by calling AllocatePages */
			sz += (10 * dsz);
			pages = EFI_SIZE_TO_PAGES(sz + efisz);
			status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
					pages, &addr);
			if (EFI_ERROR(status)) {
				printf("%s: AllocatePages error %lu\n", __func__,
				    EFI_ERROR_CODE(status));
				return (ENOMEM);
			}

			/*
			 * Read the memory map and stash it after bootinfo. Align the
			 * memory map on a 16-byte boundary (the bootinfo block is page
			 * aligned).
			 */
			efihdr = (struct efi_map_header *)(uintptr_t)addr;
			mm = (void *)((uint8_t *)efihdr + efisz);
			sz = (EFI_PAGE_SIZE * pages) - efisz;
		}

		status = BS->ExitBootServices(IH, efi_mapkey);
		if (!EFI_ERROR(status))
			break;
	}

	if (retry == 0) {
		BS->FreePages(addr, pages);
		printf("ExitBootServices error %lu\n", EFI_ERROR_CODE(status));
		return (EINVAL);
	}

	/*
	 * This may be disabled by setting efi_disable_vmap in
	 * loader.conf(5). By default we will setup the virtual
	 * map entries.
	 */

	if (do_vmap)
		efi_do_vmap(mm, sz, dsz, mmver);
	efihdr->memory_size = sz;
	efihdr->descriptor_size = dsz;
	efihdr->descriptor_version = mmver;
	file_addmetadata(kfp, MODINFOMD_EFI_MAP, efisz + sz,
	    efihdr);

	return (0);
}

/*
 * Load the information expected by an amd64 kernel.
 *
 * - The 'boothowto' argument is constructed.
 * - The 'bootdev' argument is constructed.
 * - The 'bootinfo' struct is constructed, and copied into the kernel space.
 * - The kernel environment is copied into kernel space.
 * - Module metadata are formatted and placed in kernel space.
 */
int
bi_load(char *args, vm_offset_t *modulep, vm_offset_t *kernendp)
{
	struct preloaded_file *xp, *kfp;
	struct devdesc *rootdev;
	struct file_metadata *md;
	vm_offset_t addr;
	uint64_t kernend;
	uint64_t envp;
	vm_offset_t size;
	char *rootdevname;
	int howto;
#if defined(LOADER_FDT_SUPPORT)
	vm_offset_t dtbp;
	int dtb_size;
#endif
#if defined(__arm__)
	vm_offset_t vaddr;
	size_t i;
	/*
	 * These metadata addreses must be converted for kernel after
	 * relocation.
	 */
	uint32_t		mdt[] = {
	    MODINFOMD_SSYM, MODINFOMD_ESYM, MODINFOMD_KERNEND,
	    MODINFOMD_ENVP,
#if defined(LOADER_FDT_SUPPORT)
	    MODINFOMD_DTBP
#endif
	};
#endif

	howto = bi_getboothowto(args);

	/*
	 * Allow the environment variable 'rootdev' to override the supplied
	 * device. This should perhaps go to MI code and/or have $rootdev
	 * tested/set by MI code before launching the kernel.
	 */
	rootdevname = getenv("rootdev");
	archsw.arch_getdev((void**)(&rootdev), rootdevname, NULL);
	if (rootdev == NULL) {
		printf("Can't determine root device.\n");
		return(EINVAL);
	}

	/* Try reading the /etc/fstab file to select the root device */
	getrootmount(efi_fmtdev((void *)rootdev));

	addr = 0;
	for (xp = file_findfile(NULL, NULL); xp != NULL; xp = xp->f_next) {
		if (addr < (xp->f_addr + xp->f_size))
			addr = xp->f_addr + xp->f_size;
	}

	/* Pad to a page boundary. */
	addr = roundup(addr, PAGE_SIZE);

	/* Copy our environment. */
	envp = addr;
	addr = bi_copyenv(addr);

	/* Pad to a page boundary. */
	addr = roundup(addr, PAGE_SIZE);

#if defined(LOADER_FDT_SUPPORT)
	/* Handle device tree blob */
	dtbp = addr;
	dtb_size = fdt_copy(addr);
		
	/* Pad to a page boundary */
	if (dtb_size)
		addr += roundup(dtb_size, PAGE_SIZE);
#endif

	kfp = file_findfile(NULL, "elf kernel");
	if (kfp == NULL)
		kfp = file_findfile(NULL, "elf64 kernel");
	if (kfp == NULL)
		panic("can't find kernel file");
	kernend = 0;	/* fill it in later */
	file_addmetadata(kfp, MODINFOMD_HOWTO, sizeof howto, &howto);
	file_addmetadata(kfp, MODINFOMD_ENVP, sizeof envp, &envp);
#if defined(LOADER_FDT_SUPPORT)
	if (dtb_size)
		file_addmetadata(kfp, MODINFOMD_DTBP, sizeof dtbp, &dtbp);
	else
		printf("WARNING! Trying to fire up the kernel, but no "
		    "device tree blob found!\n");
#endif
	file_addmetadata(kfp, MODINFOMD_KERNEND, sizeof kernend, &kernend);
	file_addmetadata(kfp, MODINFOMD_FW_HANDLE, sizeof ST, &ST);
#ifdef LOADER_GELI_SUPPORT
	geli_export_key_metadata(kfp);
#endif
	bi_load_efi_data(kfp);

	/* Figure out the size and location of the metadata. */
	*modulep = addr;
	size = bi_copymodules(0);
	kernend = roundup(addr + size, PAGE_SIZE);
	*kernendp = kernend;

	/* patch MODINFOMD_KERNEND */
	md = file_findmetadata(kfp, MODINFOMD_KERNEND);
	bcopy(&kernend, md->md_data, sizeof kernend);

#if defined(__arm__)
	*modulep -= __elfN(relocation_offset);

	/* Do relocation fixup on metadata of each module. */
	for (xp = file_findfile(NULL, NULL); xp != NULL; xp = xp->f_next) {
		for (i = 0; i < nitems(mdt); i++) {
			md = file_findmetadata(xp, mdt[i]);
			if (md) {
				bcopy(md->md_data, &vaddr, sizeof vaddr);
				vaddr -= __elfN(relocation_offset);
				bcopy(&vaddr, md->md_data, sizeof vaddr);
			}
		}
	}
#endif

	/* Copy module list and metadata. */
	(void)bi_copymodules(addr);

	return (0);
}
