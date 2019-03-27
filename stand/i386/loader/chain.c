/*-
 * Copyright 2015 Toomas Soome <tsoome@me.com>
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

/*
 * Chain loader to load BIOS boot block either from MBR or PBR.
 *
 * Note the boot block location 0000:7c000 conflicts with loader, so we need to
 * read in to temporary space and relocate on exec, when btx is stopped.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/diskmbr.h>

#include "bootstrap.h"
#include "libi386/libi386.h"
#include "btxv86.h"

/*
 * The MBR/VBR is located in first sector of disk/partition.
 * Read 512B to temporary location and set up relocation. Then
 * exec relocator.
 */
#define	SECTOR_SIZE	(512)

COMMAND_SET(chain, "chain", "chain load boot block from device", command_chain);

static int
command_chain(int argc, char *argv[])
{
	int fd, len, size = SECTOR_SIZE;
	struct stat st;
	vm_offset_t mem = 0x100000;
	struct i386_devdesc *rootdev;

	if (argc == 1) {
		command_errmsg = "no device or file name specified";
		return (CMD_ERROR);
	}
	if (argc != 2) {
		command_errmsg = "invalid trailing arguments";
		return (CMD_ERROR);
	}

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		command_errmsg = "open failed";
		return (CMD_ERROR);
	}

	len = strlen(argv[1]);
	if (argv[1][len-1] != ':') {
		if (fstat(fd, &st) == -1) {
			command_errmsg = "stat failed";
			close(fd);
			return (CMD_ERROR);
		}
		size = st.st_size;
	} else if (strncmp(argv[1], "disk", 4) != 0) {
		command_errmsg = "can only use disk device";
		close(fd);
		return (CMD_ERROR);
	}

	i386_getdev((void **)(&rootdev), argv[1], NULL);
	if (rootdev == NULL) {
		command_errmsg = "can't determine root device";
		close(fd);
		return (CMD_ERROR);
	}

	if (archsw.arch_readin(fd, mem, size) != size) {
		command_errmsg = "failed to read disk";
		close(fd);
		return (CMD_ERROR);
	}
	close(fd);

	if (argv[1][len-1] == ':' &&
	    *((uint16_t *)PTOV(mem + DOSMAGICOFFSET)) != DOSMAGIC) {
		command_errmsg = "wrong magic";
		return (CMD_ERROR);
	}

	relocater_data[0].src = mem;
	relocater_data[0].dest = 0x7C00;
	relocater_data[0].size = size;

	relocator_edx = bd_unit2bios(rootdev);
	relocator_esi = relocater_size;
	relocator_ds = 0;
	relocator_es = 0;
	relocator_fs = 0;
	relocator_gs = 0;
	relocator_ss = 0;
	relocator_cs = 0;
	relocator_sp = 0x7C00;
	relocator_ip = 0x7C00;
	relocator_a20_enabled = 0;

	i386_copyin(relocater, 0x600, relocater_size);

	dev_cleanup();

	__exec((void *)0x600);

	panic("exec returned");
	return (CMD_ERROR);		/* not reached */
}
