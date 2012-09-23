/*
 * (c) Alexey Starikovskiy, Intel, 2005-2006.
 * (c) Len Brown, Intel, 2007.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifdef DEFINE_ALTERNATE_TYPES
/* hack to enable building old application with new headers -lenb */
#define acpi_fadt_descriptor acpi_table_fadt
#define acpi_rsdp_descriptor acpi_table_rsdp
#define DSDT_SIG ACPI_SIG_DSDT
#define FACS_SIG ACPI_SIG_FACS
#define FADT_SIG ACPI_SIG_FADT
#define xfirmware_ctrl Xfacs
#define firmware_ctrl facs

typedef int				s32;
typedef unsigned char			u8;
typedef unsigned short			u16;
typedef unsigned int			u32;
typedef unsigned long long		u64;
typedef long long			s64;
#endif

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>


#include <acpi/acconfig.h>
#include <acpi/platform/acenv.h>
#include <acpi/actypes.h>
#include <acpi/actbl.h>

static inline u8 checksum(u8 * buffer, u32 length)
{
	u8 sum = 0, *i = buffer;
	buffer += length;
	for (; i < buffer; sum += *(i++));
	return sum;
}

static unsigned long psz, addr, length;
static int print, connect, skip;
static u8 select_sig[4];

static unsigned long read_efi_systab( void )
{
	char buffer[80];
	unsigned long addr;
	FILE *f = fopen("/sys/firmware/efi/systab", "r");
	if (f) {
		while (fgets(buffer, 80, f)) {
			if (sscanf(buffer, "ACPI20=0x%lx", &addr) == 1)
				return addr;
		}
		fclose(f);
	}
	return 0;
}

static u8 *acpi_map_memory(unsigned long where, unsigned length)
{
	unsigned long offset;
	u8 *there;
	int fd = open("/dev/mem", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "acpi_os_map_memory: cannot open /dev/mem\n");
		exit(1);
	}
	offset = where % psz;
	there = mmap(NULL, length + offset, PROT_READ, MAP_PRIVATE,
			 fd, where - offset);
	close(fd);
	if (there == MAP_FAILED) return 0;
	return (there + offset);
}

static void acpi_unmap_memory(u8 * there, unsigned length)
{
	unsigned long offset = (unsigned long)there % psz;
	munmap(there - offset, length + offset);
}

static struct acpi_table_header *acpi_map_table(unsigned long where, char *sig)
{
	unsigned size;
	struct acpi_table_header *tbl = (struct acpi_table_header *)
	    acpi_map_memory(where, sizeof(struct acpi_table_header));
	if (!tbl || (sig && memcmp(sig, tbl->signature, 4))) return 0;
	size = tbl->length;
	acpi_unmap_memory((u8 *) tbl, sizeof(struct acpi_table_header));
	return (struct acpi_table_header *)acpi_map_memory(where, size);
}

static void acpi_unmap_table(struct acpi_table_header *tbl)
{
	acpi_unmap_memory((u8 *)tbl, tbl->length);
}

static struct acpi_rsdp_descriptor *acpi_scan_for_rsdp(u8 *begin, u32 length)
{
	struct acpi_rsdp_descriptor *rsdp;
	u8 *i, *end = begin + length;
	/* Search from given start address for the requested length */
	for (i = begin; i < end; i += ACPI_RSDP_SCAN_STEP) {
		/* The signature and checksum must both be correct */
		if (memcmp((char *)i, "RSD PTR ", 8)) continue;
		rsdp = (struct acpi_rsdp_descriptor *)i;
		/* Signature matches, check the appropriate checksum */
		if (!checksum((u8 *) rsdp, (rsdp->revision < 2) ?
			      ACPI_RSDP_CHECKSUM_LENGTH :
			      ACPI_RSDP_XCHECKSUM_LENGTH))
			/* Checksum valid, we have found a valid RSDP */
			return rsdp;
	}
	/* Searched entire block, no RSDP was found */
	return 0;
}

/*
 * Output data
 */
static void acpi_show_data(int fd, u8 * data, int size)
{
	char buffer[256];
	int len;
	int i, remain = size;
	while (remain > 0) {
		len = snprintf(buffer, 256, "  %04x:", size - remain);
		for (i = 0; i < 16 && i < remain; i++) {
			len +=
			    snprintf(&buffer[len], 256 - len, " %02x", data[i]);
		}
		for (; i < 16; i++) {
			len += snprintf(&buffer[len], 256 - len, "   ");
		}
		len += snprintf(&buffer[len], 256 - len, "  ");
		for (i = 0; i < 16 && i < remain; i++) {
			buffer[len++] = (isprint(data[i])) ? data[i] : '.';
		}
		buffer[len++] = '\n';
		write(fd, buffer, len);
		data += 16;
		remain -= 16;
	}
}

/*
 * Output ACPI table
 */

#define MAX_TABLES 128
int next_table_dump;
u64 dumped_tables[MAX_TABLES];

void
set_table_dumped(u64 address) {
	if (next_table_dump >= MAX_TABLES) {
		printf("increase MAX_TABLES\n");
		exit(1);
	}
	dumped_tables[next_table_dump++] = address;
}

/*
 * list the tables as they are dumped
 * check the list so that they are not dumped twice.
 *
 * this is needed because we follow both the XSDT and RSDT
 * which generally point to all duplicate tables
 * except the FADT
 */
int
check_table_dumped(u64 address) {
	int i;

	for (i = 0; i < MAX_TABLES; ++i) {
		if (address == dumped_tables[i])
			return 1;
		if (dumped_tables[i] == 0)
			return 0;
	}
	return 0;
}
static void acpi_show_table(int fd, struct acpi_table_header *table, unsigned long addr)
{
	char buff[80];
	int len = snprintf(buff, 80, "%.4s @ %p\n", table->signature, (void *)addr);
	write(fd, buff, len);
	acpi_show_data(fd, (u8 *) table, table->length);
	buff[0] = '\n';
	write(fd, buff, 1);
}

static void write_table(int fd, struct acpi_table_header *tbl, unsigned long addr)
{
	static int select_done = 0;

	if (check_table_dumped((u64)addr))
		return;

	if (!select_sig[0]) {
		if (print) {
			acpi_show_table(fd, tbl, addr);
		} else {
			write(fd, tbl, tbl->length);
		}
	} else if (!select_done && !memcmp(select_sig, tbl->signature, 4)) {
		if (skip > 0) {
			--skip;
			return;
		}
		if (print) {
			acpi_show_table(fd, tbl, addr);
		} else {
			write(fd, tbl, tbl->length);
		}
		select_done = 1;
	}
	set_table_dumped((u64) addr);
}

static void acpi_dump_FADT(int fd, struct acpi_table_header *tbl, unsigned long xaddr) {
	struct acpi_fadt_descriptor x;
	unsigned long addr;
	size_t len = sizeof(struct acpi_fadt_descriptor);
	if (len > tbl->length) len = tbl->length;
	memcpy(&x, tbl, len);
	x.header.length = len;
	if (checksum((u8 *)tbl, len)) {
		fprintf(stderr, "Wrong checksum for FADT!\n");
	}
	if (x.header.length >= 148 && x.Xdsdt) {
		addr = (unsigned long)x.Xdsdt;
		if (connect) {
			x.Xdsdt = lseek(fd, 0, SEEK_CUR);
		}
	} else if (x.header.length >= 44 && x.dsdt) {
		addr = (unsigned long)x.dsdt;
		if (connect) {
			x.dsdt = lseek(fd, 0, SEEK_CUR);
		}
	} else {
		fprintf(stderr, "No DSDT in FADT!\n");
		goto no_dsdt;
	}
	tbl = acpi_map_table(addr, DSDT_SIG);
	if (!tbl) goto no_dsdt;
	if (checksum((u8 *)tbl, tbl->length))
		fprintf(stderr, "Wrong checksum for DSDT!\n");
	write_table(fd, tbl, addr);
	acpi_unmap_table(tbl);
no_dsdt:
	if (x.header.length >= 140 && x.xfirmware_ctrl) {
		addr = (unsigned long)x.xfirmware_ctrl;
		if (connect) {
			x.xfirmware_ctrl = lseek(fd, 0, SEEK_CUR);
		}
	} else if (x.header.length >= 40 && x.firmware_ctrl) {
		addr = (unsigned long)x.firmware_ctrl;
		if (connect) {
			x.firmware_ctrl = lseek(fd, 0, SEEK_CUR);
		}
	} else {
		fprintf(stderr, "No FACS in FADT!\n");
		goto no_facs;
	}
	tbl = acpi_map_table(addr, FACS_SIG);
	if (!tbl) goto no_facs;
	/* do not checksum FACS */
	write_table(fd, tbl, addr);
	acpi_unmap_table(tbl);
no_facs:
	write_table(fd, (struct acpi_table_header *)&x, xaddr);
}


static int acpi_dump_RSDT(int fd, struct acpi_rsdp_descriptor *rsdp)
{
	struct acpi_table_header *sdt, *tbl = 0;
	int i, num;
	char *offset;
	unsigned long addr;

	tbl = acpi_map_table(rsdp->rsdt_physical_address, "RSDT");
	if (!tbl) return 0;

	sdt = malloc(tbl->length);
	memcpy(sdt, tbl, tbl->length);
	acpi_unmap_table(tbl);
	if (checksum((u8 *)sdt, sdt->length))
		fprintf(stderr, "Wrong checksum for %s!\n", "RSDT");
	num = (sdt->length - sizeof(struct acpi_table_header))/sizeof(u32);
	offset = (char *)sdt + sizeof(struct acpi_table_header);
	for (i = 0; i < num; ++i, offset += sizeof(u32)) {
		addr = (unsigned long)(*(u32 *)offset);
		if (!addr) continue;
		tbl = acpi_map_table(addr, 0);
		if (!tbl) continue;
		if (!memcmp(tbl->signature, FADT_SIG, 4)) {
			acpi_dump_FADT(fd, tbl, addr);
		} else {
			if (checksum((u8 *)tbl, tbl->length))
				fprintf(stderr, "Wrong checksum for %.4s!\n", tbl->signature);
			write_table(fd, tbl, addr);
		}
		acpi_unmap_table(tbl);
		if (connect) {
			(*(u32*)offset) = lseek(fd, 0, SEEK_CUR);
		}
	}
	addr = (unsigned long)rsdp->rsdt_physical_address;
	if (connect) {
		rsdp->rsdt_physical_address = lseek(fd, 0, SEEK_CUR);
	}
	write_table(fd, sdt, addr);
	free (sdt);
	return 1;
}


static int acpi_dump_XSDT(int fd, struct acpi_rsdp_descriptor *rsdp)
{
	struct acpi_table_header *sdt, *tbl = 0;
	int i, num;
	char *offset;
	unsigned long addr;
	if (rsdp->revision > 1 && rsdp->xsdt_physical_address) {
		tbl = acpi_map_table(rsdp->xsdt_physical_address, "XSDT");
	}
	if (!tbl) return 0;

	sdt = malloc(tbl->length);
	memcpy(sdt, tbl, tbl->length);
	acpi_unmap_table(tbl);
	if (checksum((u8 *)sdt, sdt->length))
		fprintf(stderr, "Wrong checksum for %s!\n", "XSDT");
	num = (sdt->length - sizeof(struct acpi_table_header))/sizeof(u64);
	offset = (char *)sdt + sizeof(struct acpi_table_header);
	for (i = 0; i < num; ++i, offset += sizeof(u64)) {
		addr = (unsigned long)(*(u64 *)offset);
		if (!addr) continue;
		tbl = acpi_map_table(addr, 0);
		if (!tbl) continue;
		if (!memcmp(tbl->signature, FADT_SIG, 4)) {
			acpi_dump_FADT(fd, tbl, addr);
		} else {
			if (checksum((u8 *)tbl, tbl->length))
				fprintf(stderr, "Wrong checksum for %.4s\n", tbl->signature);
			write_table(fd, tbl, addr);
		}
		acpi_unmap_table(tbl);
		if (connect) {
			(*(u64*)offset) = lseek(fd, 0, SEEK_CUR);
		}
	}
	addr = (unsigned long)rsdp->xsdt_physical_address;
	if (connect) {
		rsdp->xsdt_physical_address = lseek(fd, 0, SEEK_CUR);
	}
	write_table(fd, sdt, addr);
	free (sdt);
	return 1;
}

static void usage(const char *progname)
{
	puts("Usage:");
	printf("%s [--addr 0x1234][--table DSDT][--output filename]"
		"[--binary][--length 0x456][--help]\n", progname);
	puts("\t--addr 0x1234 or -a 0x1234 -- look for tables at this physical address");
	puts("\t--table DSDT or -t DSDT -- only dump table with DSDT signature");
	puts("\t--output filename or -o filename -- redirect output from stdin to filename");
	puts("\t--binary or -b -- dump data in binary form rather than in hex-dump format");
	puts("\t--length 0x456 or -l 0x456 -- works only with --addr, dump physical memory"
		"\n\t\tregion without trying to understand it's contents");
	puts("\t--skip 2 or -s 2 -- skip 2 tables of the given name and output only 3rd one");
	puts("\t--help or -h -- this help message");
	exit(0);
}

static struct option long_options[] = {
	{"addr", 1, 0, 0},
	{"table", 1, 0, 0},
	{"output", 1, 0, 0},
	{"binary", 0, 0, 0},
	{"length", 1, 0, 0},
	{"skip", 1, 0, 0},
	{"help", 0, 0, 0},
	{0, 0, 0, 0}
};
int main(int argc, char **argv)
{
	int option_index, c, fd;
	u8 *raw;
	struct acpi_rsdp_descriptor rsdpx, *x = 0;
	char *filename = 0;
	char buff[80];
	memset(select_sig, 0, 4);
	print = 1;
	connect = 0;
	addr = length = 0;
	skip = 0;
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "a:t:o:bl:s:h",
				    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			switch (option_index) {
			case 0:
				addr = strtoul(optarg, (char **)NULL, 16);
				break;
			case 1:
				memcpy(select_sig, optarg, 4);
				break;
			case 2:
				filename = optarg;
				break;
			case 3:
				print = 0;
				break;
			case 4:
				length = strtoul(optarg, (char **)NULL, 16);
				break;
			case 5:
				skip = strtoul(optarg, (char **)NULL, 10);
				break;
			case 6:
				usage(argv[0]);
				exit(0);
			}
			break;
		case 'a':
			addr = strtoul(optarg, (char **)NULL, 16);
			break;
		case 't':
			memcpy(select_sig, optarg, 4);
			break;
		case 'o':
			filename = optarg;
			break;
		case 'b':
			print = 0;
			break;
		case 'l':
			length = strtoul(optarg, (char **)NULL, 16);
			break;
		case 's':
			skip = strtoul(optarg, (char **)NULL, 10);
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		default:
			printf("Unknown option!\n");
			usage(argv[0]);
			exit(0);
		}
	}

	fd = STDOUT_FILENO;
	if (filename) {
		fd = creat(filename, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		if (fd < 0)
			return fd;
	}

	if (!select_sig[0] && !print) {
		connect = 1;
	}

	psz = sysconf(_SC_PAGESIZE);
	if (length && addr) {
		/* We know length and address, it means we just want a memory dump */
		if (!(raw = acpi_map_memory(addr, length)))
			goto not_found;
		write(fd, raw, length);
		acpi_unmap_memory(raw, length);
		return 0;
	}

	length = sizeof(struct acpi_rsdp_descriptor);
	if (!addr) {
		addr = read_efi_systab();
		if (!addr) {
			addr = ACPI_HI_RSDP_WINDOW_BASE;
			length = ACPI_HI_RSDP_WINDOW_SIZE;
		}
	}

	if (!(raw = acpi_map_memory(addr, length)) ||
	    !(x = acpi_scan_for_rsdp(raw, length)))
		goto not_found;

	/* Find RSDP and print all found tables */
	memcpy(&rsdpx, x, sizeof(struct acpi_rsdp_descriptor));
	acpi_unmap_memory(raw, length);
	if (connect) {
		lseek(fd, sizeof(struct acpi_rsdp_descriptor), SEEK_SET);
	}
	if (!acpi_dump_XSDT(fd, &rsdpx))
		goto not_found;
	if (!acpi_dump_RSDT(fd, &rsdpx))
		goto not_found;
	if (connect) {
		lseek(fd, 0, SEEK_SET);
		write(fd, x, (rsdpx.revision < 2) ?
			ACPI_RSDP_CHECKSUM_LENGTH : ACPI_RSDP_XCHECKSUM_LENGTH);
	} else if (!select_sig[0] || !memcmp("RSD PTR ", select_sig, 4)) {
		addr += (long)x - (long)raw;
		length = snprintf(buff, 80, "RSD PTR @ %p\n", (void *)addr);
		write(fd, buff, length);
		acpi_show_data(fd, (u8 *) & rsdpx, (rsdpx.revision < 2) ?
				ACPI_RSDP_CHECKSUM_LENGTH : ACPI_RSDP_XCHECKSUM_LENGTH);
		buff[0] = '\n';
		write(fd, buff, 1);
	}
	return 0;
not_found:
	fprintf(stderr, "ACPI tables were not found. If you know location "
		"of RSD PTR table (from dmesg, etc), "
		"supply it with either --addr or -a option\n");
	return 1;
}
