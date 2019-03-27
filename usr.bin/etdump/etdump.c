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

#include <err.h>
#include <getopt.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>

#include "cd9660.h"
#include "cd9660_eltorito.h"

#include "etdump.h"

const char *
system_id_string(u_char system_id)
{

	switch (system_id) {
	case ET_SYS_X86:
		return ("i386");
	case ET_SYS_PPC:
		return ("powerpc");
	case ET_SYS_MAC:
		return ("mac");
	case ET_SYS_EFI:
		return ("efi");
	default:
		return ("invalid");
	}
}

const char *
media_type_string(u_char media_type)
{

	switch (media_type) {
	case ET_MEDIA_NOEM:
		return ("no emulation");
	case ET_MEDIA_12FDD:
		return ("1.2MB FDD");
	case ET_MEDIA_144FDD:
		return ("1.44MB FDD");
	case ET_MEDIA_288FDD:
		return ("2.88MB FDD");
	case ET_MEDIA_HDD:
		return ("HDD");
	default:
		return ("invalid");
	}
}

static int
read_sector(FILE *iso, daddr_t sector, char *buffer)
{

	if (fseek(iso, sector * ISO_DEFAULT_BLOCK_SIZE, SEEK_SET) != 0) {
		return (errno);
	}
	if (fread(buffer, ISO_DEFAULT_BLOCK_SIZE, 1, iso) != 1) {
		return (errno);
	}
	return (0);
}

static bool
boot_catalog_valid(char *entry)
{
	boot_catalog_validation_entry *ve;
	int16_t		checksum, sum;
	unsigned char	*csptr;
	size_t		i;

	ve = (boot_catalog_validation_entry *)entry;

	checksum = isonum_721(ve->checksum);
	cd9660_721(0, ve->checksum);
	csptr = (unsigned char *)ve;

	for (i = sum = 0; i < sizeof(*ve); i += 2) {
		sum += (int16_t)csptr[i];
		sum += 256 * (int16_t)csptr[i + 1];
	}
	if (sum + checksum != 0) {
		return (false);
	}
	
	cd9660_721(checksum, ve->checksum);
	return (true);
}

static int
dump_section(char *buffer, size_t offset, FILE *outfile, const char *filename,
    struct outputter *outputter)
{
	boot_catalog_section_header *sh;
	u_char platform_id;
	int i;
	size_t entry_offset;
	boot_catalog_section_entry *entry;

	sh = (boot_catalog_section_header *)&buffer[offset];
	if (outputter->output_section != NULL) {
		outputter->output_section(outfile, filename, sh);
	}

	platform_id = sh->platform_id[0];

	if (outputter->output_entry != NULL) {
		for (i = 1; i <= (int)sh->num_section_entries[0]; i++) {
			entry_offset = offset + i * ET_BOOT_ENTRY_SIZE;
			entry =
			    (boot_catalog_section_entry *)&buffer[entry_offset];
			outputter->output_entry(outfile, filename, entry,
			    platform_id, false);
		}
	}

	return (1 + (int)sh->num_section_entries[0]);
}

static void
dump_eltorito(FILE *iso, const char *filename, FILE *outfile,
    struct outputter *outputter)
{
	char buffer[ISO_DEFAULT_BLOCK_SIZE], *entry;
	boot_volume_descriptor *bvd;
	daddr_t boot_catalog;
	size_t offset;
	int entry_count;

	if (read_sector(iso, 17, buffer) != 0)
		err(1, "failed to read from image");
	
	bvd = (boot_volume_descriptor *)buffer;
	if (memcmp(bvd->identifier, ISO_VOLUME_DESCRIPTOR_STANDARD_ID, 5) != 0)
		warnx("%s: not a valid ISO", filename);
	if (bvd->boot_record_indicator[0] != ISO_VOLUME_DESCRIPTOR_BOOT)
		warnx("%s: not an El Torito bootable ISO", filename);
	if (memcmp(bvd->boot_system_identifier, ET_ID, 23) != 0)
		warnx("%s: not an El Torito bootable ISO", filename);

	boot_catalog = isonum_731(bvd->boot_catalog_pointer);

	if (read_sector(iso, boot_catalog, buffer) != 0)
		err(1, "failed to read from image");
	
	entry = buffer;
	offset = 0;

	if (!boot_catalog_valid(entry))
		warnx("%s: boot catalog checksum is invalid", filename);
	
	if (outputter->output_image != NULL)
		outputter->output_image(outfile, filename, bvd);

	offset += ET_BOOT_ENTRY_SIZE;
	entry = &buffer[offset];
	if (outputter->output_entry != NULL)
		outputter->output_entry(outfile, filename,
		    (boot_catalog_section_entry *)entry, 0, true);

	offset += ET_BOOT_ENTRY_SIZE;

	while (offset < ISO_DEFAULT_BLOCK_SIZE) {
		entry = &buffer[offset];

		if ((uint8_t)entry[0] != ET_SECTION_HEADER_MORE &&
		    (uint8_t)entry[0] != ET_SECTION_HEADER_LAST)
			break;

		entry_count = dump_section(buffer, offset, outfile, filename,
		    outputter);

		offset += entry_count * ET_BOOT_ENTRY_SIZE;
	}
}

static void
usage(const char *progname)
{
	char *path;

	path = strdup(progname);

	fprintf(stderr, "usage: %s [-f format] [-o filename] filename [...]\n",
	    basename(path));
	fprintf(stderr, "\tsupported output formats: shell, text\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int ch, i;
	FILE *outfile, *iso;
	struct outputter *outputter;

	outfile = stdout;
	outputter = output_text;

	static struct option longopts[] = {
		{ "format",	required_argument,	NULL,	'f' },
		{ "output",	required_argument,	NULL,	'o' },
		{ NULL,		0,			NULL,	0 },
	};

	while ((ch = getopt_long(argc, argv, "f:o:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'f':
			if (strcmp(optarg, "shell") == 0)
				outputter = output_shell;
			else if (strcmp(optarg, "text") == 0)
				outputter = output_text;
			else
				usage(argv[0]);
			break;
		case 'o':
			if (strcmp(optarg, "-") == 0) {
				outfile = stdout;
			} else if ((outfile = fopen(optarg, "w")) == NULL) {
				err(1, "unable to open %s for output", optarg);
			}
			break;
		default:
			usage(argv[0]);
		}
	}

	argc -= optind;
	argv += optind;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-") == 0) {
			iso = stdin;
		} else {
			iso = fopen(argv[i], "r");
			if (iso == NULL)
				err(1, "could not open %s", argv[1]);
		}
		dump_eltorito(iso, argv[i], outfile, outputter);
	}
}
