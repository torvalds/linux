/*-
 * Copyright (c) 2015 Eric McCorkle
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

#ifndef _BOOT_MODULE_H_
#define _BOOT_MODULE_H_

#include <stdbool.h>

#include <efi.h>
#include <efilib.h>
#include <eficonsctl.h>

#ifdef EFI_DEBUG
#define DPRINTF(fmt, args...) printf(fmt, ##args)
#define DSTALL(d) BS->Stall(d)
#else
#define DPRINTF(fmt, ...) {}
#define DSTALL(d) {}
#endif

/* EFI device info */
typedef struct dev_info
{
	EFI_BLOCK_IO *dev;
	EFI_DEVICE_PATH *devpath;
	EFI_HANDLE *devhandle;
	void *devdata;
	uint64_t partoff;
	int preferred;
	struct dev_info *next;
} dev_info_t;

/*
 * A boot loader module.
 *
 * This is a standard interface for filesystem modules in the EFI system.
 */
typedef struct boot_module_t
{
	const char *name;

	/* init is the optional initialiser for the module. */
	void (*init)(void);

	/*
	 * probe checks to see if the module can handle dev.
	 *
	 * Return codes:
	 * EFI_SUCCESS = The module can handle the device.
	 * EFI_NOT_FOUND = The module can not handle the device.
	 * Other = The module encountered an error.
	 */
	EFI_STATUS (*probe)(dev_info_t* dev);

	/*
	 * load should select the best out of a set of devices that probe
	 * indicated were loadable and load the specified file.
	 *
	 * Return codes:
	 * EFI_SUCCESS = The module can handle the device.
	 * EFI_NOT_FOUND = The module can not handle the device.
	 * Other = The module encountered an error.
	 */
	EFI_STATUS (*load)(const char *filepath, dev_info_t *devinfo,
	    void **buf, size_t *bufsize);

	/* status outputs information about the probed devices. */
	void (*status)(void);

	/* valid devices as found by probe. */
	dev_info_t *(*devices)(void);
} boot_module_t;

/* Standard boot modules. */
#ifdef EFI_UFS_BOOT
extern const boot_module_t ufs_module;
#endif
#ifdef EFI_ZFS_BOOT
extern const boot_module_t zfs_module;
#endif

/* Functions available to modules. */
extern void add_device(dev_info_t **devinfop, dev_info_t *devinfo);
extern int vsnprintf(char *str, size_t sz, const char *fmt, va_list ap);
#endif
