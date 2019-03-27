/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 smh@freebsd.org
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mfiutil.h"

MFI_TABLE(top, foreign);

static int
foreign_clear(__unused int ac, __unused char **av)
{
	int ch, error, fd;

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	printf(
	    "Are you sure you wish to clear ALL foreign configurations"
	    " on mfi%u? [y/N] ", mfi_unit);

	ch = getchar();
	if (ch != 'y' && ch != 'Y') {
		printf("\nAborting\n");
		close(fd);
		return (0);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_CLEAR, NULL, 0, NULL,
	    0, NULL) < 0) {
		error = errno;
		warn("Failed to clear foreign configuration");
		close(fd);
		return (error);
	}

	printf("mfi%d: Foreign configuration cleared\n", mfi_unit);
	close(fd);
	return (0);
}
MFI_COMMAND(foreign, clear, foreign_clear);

static int
foreign_scan(__unused int ac, __unused char **av)
{
	struct mfi_foreign_scan_info info;
	int error, fd;

	fd = mfi_open(mfi_unit, O_RDONLY);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_SCAN, &info,
	    sizeof(info), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to scan foreign configuration");
		close(fd);
		return (error);
	}

	printf("mfi%d: Found %d foreign configurations\n", mfi_unit,
	       info.count);
	close(fd);
	return (0);
}
MFI_COMMAND(foreign, scan, foreign_scan);

static int
foreign_show_cfg(int fd, uint32_t opcode, uint8_t cfgidx, int diagnostic)
{
	struct mfi_config_data *config;
	char prefix[64];
	int error;
	uint8_t mbox[4];

	bzero(mbox, sizeof(mbox));
	mbox[0] = cfgidx;
	if (mfi_config_read_opcode(fd, opcode, &config, mbox, sizeof(mbox)) < 0) {
		error = errno;
		warn("Failed to get foreign config %d", error);
		close(fd);
		return (error);
	}

	if (opcode == MFI_DCMD_CFG_FOREIGN_PREVIEW)
		sprintf(prefix, "Foreign configuration preview %d", cfgidx);
	else
		sprintf(prefix, "Foreign configuration %d", cfgidx);
	/*
	 * MegaCli uses DCMD opcodes: 0x03100200 (which fails) followed by
	 * 0x1a721880 which returns what looks to be drive / volume info
	 * but we have no real information on what these are or what they do
	 * so we're currently relying solely on the config returned above
	 */
	if (diagnostic)
		dump_config(fd, config, prefix);
	else {
		char *ld_list;
		int i;

		ld_list = (char *)(config->array);

        	printf("%s: %d arrays, %d volumes, %d spares\n", prefix, 
		       config->array_count, config->log_drv_count,
		       config->spares_count);


		for (i = 0; i < config->array_count; i++)
			 ld_list += config->array_size;

		for (i = 0; i < config->log_drv_count; i++) {
        		const char *level;
        		char size[6], stripe[5];
			struct mfi_ld_config *ld;

			ld = (struct mfi_ld_config *)ld_list;

        		format_stripe(stripe, sizeof(stripe),
            			ld->params.stripe_size);
			/*
			 * foreign configs don't seem to have a secondary raid level
			 * but, we can use span depth here as if a LD spans multiple
			 * arrays of disks (2 raid 1 sets for example), we will have an
			 * indication based on the spam depth. swb
			 */ 
        		level = mfi_raid_level(ld->params.primary_raid_level,
            					(ld->params.span_depth - 1));

        		humanize_number(size, sizeof(size), ld->span[0].num_blocks * 512,
            			"", HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);

			printf(" ID%d ", i);
              		printf("(%6s) %-8s |",
				size, level);
			printf("volume spans %d %s\n",	ld->params.span_depth,
							(ld->params.span_depth > 1) ? "arrays" : "array");
			for (int j = 0; j < ld->params.span_depth; j++) {
				char *ar_list;
				struct mfi_array *ar;
				uint16_t device_id;

				printf("      array %u @ ", ld->span[j].array_ref);
        			humanize_number(size, sizeof(size), ld->span[j].num_blocks * 512,
            				"", HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
				
				printf("(%6s)\n",size);
				ar_list = (char *)config->array + (ld->span[j].array_ref * config->array_size);

				ar = (struct mfi_array *)ar_list;
				for (int k = 0; k < ar->num_drives; k++) {
					device_id = ar->pd[k].ref.v.device_id;
					if (device_id == 0xffff)
						printf("        drive MISSING\n");
					else {
						printf("        drive %u %s\n", device_id,
			    				mfi_pdstate(ar->pd[k].fw_state));
					}
				}

			}
			ld_list += config->log_drv_size;
		}
	}

	free(config);

	return (0);
}

int
display_format(int ac, char **av, int diagnostic, mfi_dcmd_t display_cmd)
{
	struct mfi_foreign_scan_info info;
	uint8_t i;
	int error, fd;

	if (ac > 2) {
		warnx("foreign display: extra arguments");
                return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDONLY);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_SCAN, &info,
	    sizeof(info), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to scan foreign configuration");
		close(fd);
		return (error);
	}

	if (info.count == 0) {
		warnx("foreign display: no foreign configs found");
		close(fd);
		return (EINVAL);
	}

	if (ac == 1) {
		for (i = 0; i < info.count; i++) {
			error = foreign_show_cfg(fd,
				display_cmd, i, diagnostic);
			if(error != 0) {
				close(fd);
				return (error);
			}
			if (i < info.count - 1)
				printf("\n");
		}
	} else if (ac == 2) {
		error = foreign_show_cfg(fd,
			display_cmd, atoi(av[1]), diagnostic);
		if (error != 0) {
			close(fd);
			return (error);
		}
	}
	
	close(fd);
	return (0);
}

static int
foreign_display(int ac, char **av)
{
	return(display_format(ac, av, 1/*diagnostic output*/, MFI_DCMD_CFG_FOREIGN_DISPLAY));
}
MFI_COMMAND(foreign, diag, foreign_display);

static int
foreign_preview(int ac, char **av)
{
	return(display_format(ac, av, 1/*diagnostic output*/, MFI_DCMD_CFG_FOREIGN_PREVIEW));
}
MFI_COMMAND(foreign, preview, foreign_preview);

static int
foreign_import(int ac, char **av)
{
	struct mfi_foreign_scan_info info;
	int ch, error, fd;
	uint8_t cfgidx;
	uint8_t mbox[4];

	if (ac > 2) {
		warnx("foreign preview: extra arguments");
                return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_SCAN, &info,
	    sizeof(info), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to scan foreign configuration");
		close(fd);
		return (error);
	}

	if (info.count == 0) {
		warnx("foreign import: no foreign configs found");
		close(fd);
		return (EINVAL);
	}

	if (ac == 1) {
		cfgidx = 0xff;
		printf("Are you sure you wish to import ALL foreign "
		       "configurations on mfi%u? [y/N] ", mfi_unit);
	} else {
		/*
		 * While this is docmmented for MegaCli this failed with
		 * exit code 0x03 on the test controller which was a Supermicro
		 * SMC2108 with firmware 12.12.0-0095 which is a LSI 2108 based
		 * controller.
		 */
		cfgidx = atoi(av[1]);
		if (cfgidx >= info.count) {
			warnx("Invalid foreign config %d specified max is %d",
			      cfgidx, info.count - 1);
			close(fd);
			return (EINVAL);
		}
		printf("Are you sure you wish to import the foreign "
		       "configuration %d on mfi%u? [y/N] ", cfgidx, mfi_unit);
	}

	ch = getchar();
	if (ch != 'y' && ch != 'Y') {
		printf("\nAborting\n");
		close(fd);
		return (0);
	}

	bzero(mbox, sizeof(mbox));
	mbox[0] = cfgidx;
	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_IMPORT, NULL, 0, mbox,
	    sizeof(mbox), NULL) < 0) {
		error = errno;
		warn("Failed to import foreign configuration");
		close(fd);
		return (error);
	}

	if (ac == 1)
		printf("mfi%d: All foreign configurations imported\n",
		       mfi_unit);
	else
		printf("mfi%d: Foreign configuration %d imported\n", mfi_unit,
		       cfgidx);
	close(fd);
	return (0);
}
MFI_COMMAND(foreign, import, foreign_import);
