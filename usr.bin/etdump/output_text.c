/*-
 * Copyright (c) 2018 iXsystems, Inc.
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

#include <stdbool.h>
#include <stdio.h>

#include "cd9660.h"
#include "cd9660_eltorito.h"

#include "etdump.h"

static void
output_image(FILE *outfile, const char *filename, boot_volume_descriptor *bvd __unused)
{

	fprintf(outfile, "Image in %s\n", filename);
}

static void
output_section(FILE *outfile, const char *filename __unused,
    boot_catalog_section_header *bcsh)
{

	fprintf(outfile, "\nSection header: %s",
	    system_id_string(bcsh->platform_id[0]));

	if (bcsh->header_indicator[0] == ET_SECTION_HEADER_LAST)
		fprintf(outfile, ", final\n");
	else
		fprintf(outfile, "\n");
}

static void
output_entry(FILE *outfile, const char *filename __unused,
    boot_catalog_section_entry *bcse, u_char platform_id __unused,
    bool initial)
{
	const char *indent;

	switch (bcse->boot_indicator[0]) {
	case ET_BOOTABLE:
		break;
	case ET_NOT_BOOTABLE:
	default:
		return;
	}

	if (initial) {
		fprintf(outfile, "Default entry\n");
		indent = "\t";
	} else {
		fprintf(outfile, "\tSection entry\n");
		indent = "\t\t";
	}

	fprintf(outfile, "%sSystem %s\n", indent,
	    system_id_string(bcse->system_type[0]));
	fprintf(outfile, "%sStart LBA %d (0x%x), sector count %d (0x%x)\n",
	    indent, isonum_731(bcse->load_rba), isonum_731(bcse->load_rba),
	    isonum_721(bcse->sector_count), isonum_721(bcse->sector_count));
	fprintf(outfile, "%sMedia type: %s\n", indent,
	    media_type_string(bcse->media_type[0]));
}

static struct outputter _output_text = {
	.output_image = output_image,
	.output_section = output_section,
	.output_entry = output_entry,
};

struct outputter *output_text = &_output_text;
