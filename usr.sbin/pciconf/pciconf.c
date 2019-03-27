/*
 * Copyright 1996 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/pciio.h>
#include <sys/queue.h>

#include <vm/vm.h>

#include <dev/pci/pcireg.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"
#include "pciconf.h"

struct pci_device_info
{
    TAILQ_ENTRY(pci_device_info)	link;
    int					id;
    char				*desc;
};

struct pci_vendor_info
{
    TAILQ_ENTRY(pci_vendor_info)	link;
    TAILQ_HEAD(,pci_device_info)	devs;
    int					id;
    char				*desc;
};

static TAILQ_HEAD(,pci_vendor_info)	pci_vendors;

static struct pcisel getsel(const char *str);
static void list_bridge(int fd, struct pci_conf *p);
static void list_bars(int fd, struct pci_conf *p);
static void list_devs(const char *name, int verbose, int bars, int bridge,
    int caps, int errors, int vpd);
static void list_verbose(struct pci_conf *p);
static void list_vpd(int fd, struct pci_conf *p);
static const char *guess_class(struct pci_conf *p);
static const char *guess_subclass(struct pci_conf *p);
static int load_vendors(void);
static void readit(const char *, const char *, int);
static void writeit(const char *, const char *, const char *, int);
static void chkattached(const char *);
static void dump_bar(const char *name, const char *reg, const char *bar_start,
    const char *bar_count, int width, int verbose);

static int exitstatus = 0;

static void
usage(void)
{

	fprintf(stderr, "%s",
		"usage: pciconf -l [-BbcevV] [device]\n"
		"       pciconf -a device\n"
		"       pciconf -r [-b | -h] device addr[:addr2]\n"
		"       pciconf -w [-b | -h] device addr value\n"
		"       pciconf -D [-b | -h | -x] device bar [start [count]]"
		"\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int c, width;
	int listmode, readmode, writemode, attachedmode, dumpbarmode;
	int bars, bridge, caps, errors, verbose, vpd;

	listmode = readmode = writemode = attachedmode = dumpbarmode = 0;
	bars = bridge = caps = errors = verbose = vpd= 0;
	width = 4;

	while ((c = getopt(argc, argv, "aBbcDehlrwVv")) != -1) {
		switch(c) {
		case 'a':
			attachedmode = 1;
			break;

		case 'B':
			bridge = 1;
			break;

		case 'b':
			bars = 1;
			width = 1;
			break;

		case 'c':
			caps = 1;
			break;

		case 'D':
			dumpbarmode = 1;
			break;

		case 'e':
			errors = 1;
			break;

		case 'h':
			width = 2;
			break;

		case 'l':
			listmode = 1;
			break;

		case 'r':
			readmode = 1;
			break;

		case 'w':
			writemode = 1;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			vpd = 1;
			break;

		case 'x':
			width = 8;
			break;

		default:
			usage();
		}
	}

	if ((listmode && optind >= argc + 1)
	    || (writemode && optind + 3 != argc)
	    || (readmode && optind + 2 != argc)
	    || (attachedmode && optind + 1 != argc)
	    || (dumpbarmode && (optind + 2 > argc || optind + 4 < argc))
	    || (width == 8 && !dumpbarmode))
		usage();

	if (listmode) {
		list_devs(optind + 1 == argc ? argv[optind] : NULL, verbose,
		    bars, bridge, caps, errors, vpd);
	} else if (attachedmode) {
		chkattached(argv[optind]);
	} else if (readmode) {
		readit(argv[optind], argv[optind + 1], width);
	} else if (writemode) {
		writeit(argv[optind], argv[optind + 1], argv[optind + 2],
		    width);
	} else if (dumpbarmode) {
		dump_bar(argv[optind], argv[optind + 1],
		    optind + 2 < argc ? argv[optind + 2] : NULL, 
		    optind + 3 < argc ? argv[optind + 3] : NULL, 
		    width, verbose);
	} else {
		usage();
	}

	return (exitstatus);
}

static void
list_devs(const char *name, int verbose, int bars, int bridge, int caps,
    int errors, int vpd)
{
	int fd;
	struct pci_conf_io pc;
	struct pci_conf conf[255], *p;
	struct pci_match_conf patterns[1];
	int none_count = 0;

	if (verbose)
		load_vendors();

	fd = open(_PATH_DEVPCI, (bridge || caps || errors) ? O_RDWR : O_RDONLY,
	    0);
	if (fd < 0)
		err(1, "%s", _PATH_DEVPCI);

	bzero(&pc, sizeof(struct pci_conf_io));
	pc.match_buf_len = sizeof(conf);
	pc.matches = conf;
	if (name != NULL) {
		bzero(&patterns, sizeof(patterns));
		patterns[0].pc_sel = getsel(name);
		patterns[0].flags = PCI_GETCONF_MATCH_DOMAIN |
		    PCI_GETCONF_MATCH_BUS | PCI_GETCONF_MATCH_DEV |
		    PCI_GETCONF_MATCH_FUNC;
		pc.num_patterns = 1;
		pc.pat_buf_len = sizeof(patterns);
		pc.patterns = patterns;
	}

	do {
		if (ioctl(fd, PCIOCGETCONF, &pc) == -1)
			err(1, "ioctl(PCIOCGETCONF)");

		/*
		 * 255 entries should be more than enough for most people,
		 * but if someone has more devices, and then changes things
		 * around between ioctls, we'll do the cheesy thing and
		 * just bail.  The alternative would be to go back to the
		 * beginning of the list, and print things twice, which may
		 * not be desirable.
		 */
		if (pc.status == PCI_GETCONF_LIST_CHANGED) {
			warnx("PCI device list changed, please try again");
			exitstatus = 1;
			close(fd);
			return;
		} else if (pc.status ==  PCI_GETCONF_ERROR) {
			warnx("error returned from PCIOCGETCONF ioctl");
			exitstatus = 1;
			close(fd);
			return;
		}
		for (p = conf; p < &conf[pc.num_matches]; p++) {
			printf("%s%d@pci%d:%d:%d:%d:\tclass=0x%06x card=0x%08x "
			    "chip=0x%08x rev=0x%02x hdr=0x%02x\n",
			    *p->pd_name ? p->pd_name :
			    "none",
			    *p->pd_name ? (int)p->pd_unit :
			    none_count++, p->pc_sel.pc_domain,
			    p->pc_sel.pc_bus, p->pc_sel.pc_dev,
			    p->pc_sel.pc_func, (p->pc_class << 16) |
			    (p->pc_subclass << 8) | p->pc_progif,
			    (p->pc_subdevice << 16) | p->pc_subvendor,
			    (p->pc_device << 16) | p->pc_vendor,
			    p->pc_revid, p->pc_hdr);
			if (verbose)
				list_verbose(p);
			if (bars)
				list_bars(fd, p);
			if (bridge)
				list_bridge(fd, p);
			if (caps)
				list_caps(fd, p);
			if (errors)
				list_errors(fd, p);
			if (vpd)
				list_vpd(fd, p);
		}
	} while (pc.status == PCI_GETCONF_MORE_DEVS);

	close(fd);
}

static void
print_bus_range(int fd, struct pci_conf *p, int secreg, int subreg)
{
	uint8_t secbus, subbus;

	secbus = read_config(fd, &p->pc_sel, secreg, 1);
	subbus = read_config(fd, &p->pc_sel, subreg, 1);
	printf("    bus range  = %u-%u\n", secbus, subbus);
}

static void
print_window(int reg, const char *type, int range, uint64_t base,
    uint64_t limit)
{

	printf("    window[%02x] = type %s, range %2d, addr %#jx-%#jx, %s\n",
	    reg, type, range, (uintmax_t)base, (uintmax_t)limit,
	    base < limit ? "enabled" : "disabled");
}

static void
print_special_decode(bool isa, bool vga, bool subtractive)
{
	bool comma;

	if (isa || vga || subtractive) {
		comma = false;
		printf("    decode     = ");
		if (isa) {
			printf("ISA");
			comma = true;
		}
		if (vga) {
			printf("%sVGA", comma ? ", " : "");
			comma = true;
		}
		if (subtractive)
			printf("%ssubtractive", comma ? ", " : "");
		printf("\n");
	}
}

static void
print_bridge_windows(int fd, struct pci_conf *p)
{
	uint64_t base, limit;
	uint32_t val;
	uint16_t bctl;
	bool subtractive;
	int range;

	/*
	 * XXX: This assumes that a window with a base and limit of 0
	 * is not implemented.  In theory a window might be programmed
	 * at the smallest size with a base of 0, but those do not seem
	 * common in practice.
	 */
	val = read_config(fd, &p->pc_sel, PCIR_IOBASEL_1, 1);
	if (val != 0 || read_config(fd, &p->pc_sel, PCIR_IOLIMITL_1, 1) != 0) {
		if ((val & PCIM_BRIO_MASK) == PCIM_BRIO_32) {
			base = PCI_PPBIOBASE(
			    read_config(fd, &p->pc_sel, PCIR_IOBASEH_1, 2),
			    val);
			limit = PCI_PPBIOLIMIT(
			    read_config(fd, &p->pc_sel, PCIR_IOLIMITH_1, 2),
			    read_config(fd, &p->pc_sel, PCIR_IOLIMITL_1, 1));
			range = 32;
		} else {
			base = PCI_PPBIOBASE(0, val);
			limit = PCI_PPBIOLIMIT(0,
			    read_config(fd, &p->pc_sel, PCIR_IOLIMITL_1, 1));
			range = 16;
		}
		print_window(PCIR_IOBASEL_1, "I/O Port", range, base, limit);
	}

	base = PCI_PPBMEMBASE(0,
	    read_config(fd, &p->pc_sel, PCIR_MEMBASE_1, 2));
	limit = PCI_PPBMEMLIMIT(0,
	    read_config(fd, &p->pc_sel, PCIR_MEMLIMIT_1, 2));
	print_window(PCIR_MEMBASE_1, "Memory", 32, base, limit);

	val = read_config(fd, &p->pc_sel, PCIR_PMBASEL_1, 2);
	if (val != 0 || read_config(fd, &p->pc_sel, PCIR_PMLIMITL_1, 2) != 0) {
		if ((val & PCIM_BRPM_MASK) == PCIM_BRPM_64) {
			base = PCI_PPBMEMBASE(
			    read_config(fd, &p->pc_sel, PCIR_PMBASEH_1, 4),
			    val);
			limit = PCI_PPBMEMLIMIT(
			    read_config(fd, &p->pc_sel, PCIR_PMLIMITH_1, 4),
			    read_config(fd, &p->pc_sel, PCIR_PMLIMITL_1, 2));
			range = 64;
		} else {
			base = PCI_PPBMEMBASE(0, val);
			limit = PCI_PPBMEMLIMIT(0,
			    read_config(fd, &p->pc_sel, PCIR_PMLIMITL_1, 2));
			range = 32;
		}
		print_window(PCIR_PMBASEL_1, "Prefetchable Memory", range, base,
		    limit);
	}

	/*
	 * XXX: This list of bridges that are subtractive but do not set
	 * progif to indicate it is copied from pci_pci.c.
	 */
	subtractive = p->pc_progif == PCIP_BRIDGE_PCI_SUBTRACTIVE;
	switch (p->pc_device << 16 | p->pc_vendor) {
	case 0xa002177d:		/* Cavium ThunderX */
	case 0x124b8086:		/* Intel 82380FB Mobile */
	case 0x060513d7:		/* Toshiba ???? */
		subtractive = true;
	}
	if (p->pc_vendor == 0x8086 && (p->pc_device & 0xff00) == 0x2400)
		subtractive = true;
		
	bctl = read_config(fd, &p->pc_sel, PCIR_BRIDGECTL_1, 2);
	print_special_decode(bctl & PCIB_BCR_ISA_ENABLE,
	    bctl & PCIB_BCR_VGA_ENABLE, subtractive);
}

static void
print_cardbus_mem_window(int fd, struct pci_conf *p, int basereg, int limitreg,
    bool prefetch)
{

	print_window(basereg, prefetch ? "Prefetchable Memory" : "Memory", 32,
	    PCI_CBBMEMBASE(read_config(fd, &p->pc_sel, basereg, 4)),
	    PCI_CBBMEMLIMIT(read_config(fd, &p->pc_sel, limitreg, 4)));
}

static void
print_cardbus_io_window(int fd, struct pci_conf *p, int basereg, int limitreg)
{
	uint32_t base, limit;
	uint32_t val;
	int range;

	val = read_config(fd, &p->pc_sel, basereg, 2);
	if ((val & PCIM_CBBIO_MASK) == PCIM_CBBIO_32) {
		base = PCI_CBBIOBASE(read_config(fd, &p->pc_sel, basereg, 4));
		limit = PCI_CBBIOBASE(read_config(fd, &p->pc_sel, limitreg, 4));
		range = 32;
	} else {
		base = PCI_CBBIOBASE(val);
		limit = PCI_CBBIOBASE(read_config(fd, &p->pc_sel, limitreg, 2));
		range = 16;
	}
	print_window(basereg, "I/O Port", range, base, limit);
}

static void
print_cardbus_windows(int fd, struct pci_conf *p)
{
	uint16_t bctl;

	bctl = read_config(fd, &p->pc_sel, PCIR_BRIDGECTL_2, 2);
	print_cardbus_mem_window(fd, p, PCIR_MEMBASE0_2, PCIR_MEMLIMIT0_2,
	    bctl & CBB_BCR_PREFETCH_0_ENABLE);
	print_cardbus_mem_window(fd, p, PCIR_MEMBASE1_2, PCIR_MEMLIMIT1_2,
	    bctl & CBB_BCR_PREFETCH_1_ENABLE);
	print_cardbus_io_window(fd, p, PCIR_IOBASE0_2, PCIR_IOLIMIT0_2);
	print_cardbus_io_window(fd, p, PCIR_IOBASE1_2, PCIR_IOLIMIT1_2);
	print_special_decode(bctl & CBB_BCR_ISA_ENABLE,
	    bctl & CBB_BCR_VGA_ENABLE, false);
}

static void
list_bridge(int fd, struct pci_conf *p)
{

	switch (p->pc_hdr & PCIM_HDRTYPE) {
	case PCIM_HDRTYPE_BRIDGE:
		print_bus_range(fd, p, PCIR_SECBUS_1, PCIR_SUBBUS_1);
		print_bridge_windows(fd, p);
		break;
	case PCIM_HDRTYPE_CARDBUS:
		print_bus_range(fd, p, PCIR_SECBUS_2, PCIR_SUBBUS_2);
		print_cardbus_windows(fd, p);
		break;
	}
}

static void
list_bars(int fd, struct pci_conf *p)
{
	int i, max;

	switch (p->pc_hdr & PCIM_HDRTYPE) {
	case PCIM_HDRTYPE_NORMAL:
		max = PCIR_MAX_BAR_0;
		break;
	case PCIM_HDRTYPE_BRIDGE:
		max = PCIR_MAX_BAR_1;
		break;
	case PCIM_HDRTYPE_CARDBUS:
		max = PCIR_MAX_BAR_2;
		break;
	default:
		return;
	}

	for (i = 0; i <= max; i++)
		print_bar(fd, p, "bar   ", PCIR_BAR(i));
}

void
print_bar(int fd, struct pci_conf *p, const char *label, uint16_t bar_offset)
{
	uint64_t base;
	const char *type;
	struct pci_bar_io bar;
	int range;

	bar.pbi_sel = p->pc_sel;
	bar.pbi_reg = bar_offset;
	if (ioctl(fd, PCIOCGETBAR, &bar) < 0)
		return;
	if (PCI_BAR_IO(bar.pbi_base)) {
		type = "I/O Port";
		range = 32;
		base = bar.pbi_base & PCIM_BAR_IO_BASE;
	} else {
		if (bar.pbi_base & PCIM_BAR_MEM_PREFETCH)
			type = "Prefetchable Memory";
		else
			type = "Memory";
		switch (bar.pbi_base & PCIM_BAR_MEM_TYPE) {
		case PCIM_BAR_MEM_32:
			range = 32;
			break;
		case PCIM_BAR_MEM_1MB:
			range = 20;
			break;
		case PCIM_BAR_MEM_64:
			range = 64;
			break;
		default:
			range = -1;
		}
		base = bar.pbi_base & ~((uint64_t)0xf);
	}
	printf("    %s[%02x] = type %s, range %2d, base %#jx, ",
	    label, bar_offset, type, range, (uintmax_t)base);
	printf("size %ju, %s\n", (uintmax_t)bar.pbi_length,
	    bar.pbi_enabled ? "enabled" : "disabled");
}

static void
list_verbose(struct pci_conf *p)
{
	struct pci_vendor_info	*vi;
	struct pci_device_info	*di;
	const char *dp;

	TAILQ_FOREACH(vi, &pci_vendors, link) {
		if (vi->id == p->pc_vendor) {
			printf("    vendor     = '%s'\n", vi->desc);
			break;
		}
	}
	if (vi == NULL) {
		di = NULL;
	} else {
		TAILQ_FOREACH(di, &vi->devs, link) {
			if (di->id == p->pc_device) {
				printf("    device     = '%s'\n", di->desc);
				break;
			}
		}
	}
	if ((dp = guess_class(p)) != NULL)
		printf("    class      = %s\n", dp);
	if ((dp = guess_subclass(p)) != NULL)
		printf("    subclass   = %s\n", dp);
}

static void
list_vpd(int fd, struct pci_conf *p)
{
	struct pci_list_vpd_io list;
	struct pci_vpd_element *vpd, *end;

	list.plvi_sel = p->pc_sel;
	list.plvi_len = 0;
	list.plvi_data = NULL;
	if (ioctl(fd, PCIOCLISTVPD, &list) < 0 || list.plvi_len == 0)
		return;

	list.plvi_data = malloc(list.plvi_len);
	if (ioctl(fd, PCIOCLISTVPD, &list) < 0) {
		free(list.plvi_data);
		return;
	}

	vpd = list.plvi_data;
	end = (struct pci_vpd_element *)((char *)vpd + list.plvi_len);
	for (; vpd < end; vpd = PVE_NEXT(vpd)) {
		if (vpd->pve_flags == PVE_FLAG_IDENT) {
			printf("    VPD ident  = '%.*s'\n",
			    (int)vpd->pve_datalen, vpd->pve_data);
			continue;
		}

		/* Ignore the checksum keyword. */
		if (!(vpd->pve_flags & PVE_FLAG_RW) &&
		    memcmp(vpd->pve_keyword, "RV", 2) == 0)
			continue;

		/* Ignore remaining read-write space. */
		if (vpd->pve_flags & PVE_FLAG_RW &&
		    memcmp(vpd->pve_keyword, "RW", 2) == 0)
			continue;

		/* Handle extended capability keyword. */
		if (!(vpd->pve_flags & PVE_FLAG_RW) &&
		    memcmp(vpd->pve_keyword, "CP", 2) == 0) {
			printf("    VPD ro CP  = ID %02x in map 0x%x[0x%x]\n",
			    (unsigned int)vpd->pve_data[0],
			    PCIR_BAR((unsigned int)vpd->pve_data[1]),
			    (unsigned int)vpd->pve_data[3] << 8 |
			    (unsigned int)vpd->pve_data[2]);
			continue;
		}

		/* Remaining keywords should all have ASCII values. */
		printf("    VPD %s %c%c  = '%.*s'\n",
		    vpd->pve_flags & PVE_FLAG_RW ? "rw" : "ro",
		    vpd->pve_keyword[0], vpd->pve_keyword[1],
		    (int)vpd->pve_datalen, vpd->pve_data);
	}
	free(list.plvi_data);
}

/*
 * This is a direct cut-and-paste from the table in sys/dev/pci/pci.c.
 */
static struct
{
	int	class;
	int	subclass;
	const char *desc;
} pci_nomatch_tab[] = {
	{PCIC_OLD,		-1,			"old"},
	{PCIC_OLD,		PCIS_OLD_NONVGA,	"non-VGA display device"},
	{PCIC_OLD,		PCIS_OLD_VGA,		"VGA-compatible display device"},
	{PCIC_STORAGE,		-1,			"mass storage"},
	{PCIC_STORAGE,		PCIS_STORAGE_SCSI,	"SCSI"},
	{PCIC_STORAGE,		PCIS_STORAGE_IDE,	"ATA"},
	{PCIC_STORAGE,		PCIS_STORAGE_FLOPPY,	"floppy disk"},
	{PCIC_STORAGE,		PCIS_STORAGE_IPI,	"IPI"},
	{PCIC_STORAGE,		PCIS_STORAGE_RAID,	"RAID"},
	{PCIC_STORAGE,		PCIS_STORAGE_ATA_ADMA,	"ATA (ADMA)"},
	{PCIC_STORAGE,		PCIS_STORAGE_SATA,	"SATA"},
	{PCIC_STORAGE,		PCIS_STORAGE_SAS,	"SAS"},
	{PCIC_STORAGE,		PCIS_STORAGE_NVM,	"NVM"},
	{PCIC_NETWORK,		-1,			"network"},
	{PCIC_NETWORK,		PCIS_NETWORK_ETHERNET,	"ethernet"},
	{PCIC_NETWORK,		PCIS_NETWORK_TOKENRING,	"token ring"},
	{PCIC_NETWORK,		PCIS_NETWORK_FDDI,	"fddi"},
	{PCIC_NETWORK,		PCIS_NETWORK_ATM,	"ATM"},
	{PCIC_NETWORK,		PCIS_NETWORK_ISDN,	"ISDN"},
	{PCIC_DISPLAY,		-1,			"display"},
	{PCIC_DISPLAY,		PCIS_DISPLAY_VGA,	"VGA"},
	{PCIC_DISPLAY,		PCIS_DISPLAY_XGA,	"XGA"},
	{PCIC_DISPLAY,		PCIS_DISPLAY_3D,	"3D"},
	{PCIC_MULTIMEDIA,	-1,			"multimedia"},
	{PCIC_MULTIMEDIA,	PCIS_MULTIMEDIA_VIDEO,	"video"},
	{PCIC_MULTIMEDIA,	PCIS_MULTIMEDIA_AUDIO,	"audio"},
	{PCIC_MULTIMEDIA,	PCIS_MULTIMEDIA_TELE,	"telephony"},
	{PCIC_MULTIMEDIA,	PCIS_MULTIMEDIA_HDA,	"HDA"},
	{PCIC_MEMORY,		-1,			"memory"},
	{PCIC_MEMORY,		PCIS_MEMORY_RAM,	"RAM"},
	{PCIC_MEMORY,		PCIS_MEMORY_FLASH,	"flash"},
	{PCIC_BRIDGE,		-1,			"bridge"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_HOST,	"HOST-PCI"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_ISA,	"PCI-ISA"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_EISA,	"PCI-EISA"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_MCA,	"PCI-MCA"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_PCI,	"PCI-PCI"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_PCMCIA,	"PCI-PCMCIA"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_NUBUS,	"PCI-NuBus"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_CARDBUS,	"PCI-CardBus"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_RACEWAY,	"PCI-RACEway"},
	{PCIC_SIMPLECOMM,	-1,			"simple comms"},
	{PCIC_SIMPLECOMM,	PCIS_SIMPLECOMM_UART,	"UART"},	/* could detect 16550 */
	{PCIC_SIMPLECOMM,	PCIS_SIMPLECOMM_PAR,	"parallel port"},
	{PCIC_SIMPLECOMM,	PCIS_SIMPLECOMM_MULSER,	"multiport serial"},
	{PCIC_SIMPLECOMM,	PCIS_SIMPLECOMM_MODEM,	"generic modem"},
	{PCIC_BASEPERIPH,	-1,			"base peripheral"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_PIC,	"interrupt controller"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_DMA,	"DMA controller"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_TIMER,	"timer"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_RTC,	"realtime clock"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_PCIHOT,	"PCI hot-plug controller"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_SDHC,	"SD host controller"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_IOMMU,	"IOMMU"},
	{PCIC_INPUTDEV,		-1,			"input device"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_KEYBOARD,	"keyboard"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_DIGITIZER,"digitizer"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_MOUSE,	"mouse"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_SCANNER,	"scanner"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_GAMEPORT,	"gameport"},
	{PCIC_DOCKING,		-1,			"docking station"},
	{PCIC_PROCESSOR,	-1,			"processor"},
	{PCIC_SERIALBUS,	-1,			"serial bus"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_FW,	"FireWire"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_ACCESS,	"AccessBus"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_SSA,	"SSA"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_USB,	"USB"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_FC,	"Fibre Channel"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_SMBUS,	"SMBus"},
	{PCIC_WIRELESS,		-1,			"wireless controller"},
	{PCIC_WIRELESS,		PCIS_WIRELESS_IRDA,	"iRDA"},
	{PCIC_WIRELESS,		PCIS_WIRELESS_IR,	"IR"},
	{PCIC_WIRELESS,		PCIS_WIRELESS_RF,	"RF"},
	{PCIC_INTELLIIO,	-1,			"intelligent I/O controller"},
	{PCIC_INTELLIIO,	PCIS_INTELLIIO_I2O,	"I2O"},
	{PCIC_SATCOM,		-1,			"satellite communication"},
	{PCIC_SATCOM,		PCIS_SATCOM_TV,		"sat TV"},
	{PCIC_SATCOM,		PCIS_SATCOM_AUDIO,	"sat audio"},
	{PCIC_SATCOM,		PCIS_SATCOM_VOICE,	"sat voice"},
	{PCIC_SATCOM,		PCIS_SATCOM_DATA,	"sat data"},
	{PCIC_CRYPTO,		-1,			"encrypt/decrypt"},
	{PCIC_CRYPTO,		PCIS_CRYPTO_NETCOMP,	"network/computer crypto"},
	{PCIC_CRYPTO,		PCIS_CRYPTO_NETCOMP,	"entertainment crypto"},
	{PCIC_DASP,		-1,			"dasp"},
	{PCIC_DASP,		PCIS_DASP_DPIO,		"DPIO module"},
	{PCIC_DASP,		PCIS_DASP_PERFCNTRS,	"performance counters"},
	{PCIC_DASP,		PCIS_DASP_COMM_SYNC,	"communication synchronizer"},
	{PCIC_DASP,		PCIS_DASP_MGMT_CARD,	"signal processing management"},
	{PCIC_ACCEL,		-1,			"processing accelerators"},
	{PCIC_ACCEL,		PCIS_ACCEL_PROCESSING,	"processing accelerators"},
	{PCIC_INSTRUMENT,	-1,			"non-essential instrumentation"},
	{0, 0,		NULL}
};

static const char *
guess_class(struct pci_conf *p)
{
	int	i;

	for (i = 0; pci_nomatch_tab[i].desc != NULL; i++) {
		if (pci_nomatch_tab[i].class == p->pc_class)
			return(pci_nomatch_tab[i].desc);
	}
	return(NULL);
}

static const char *
guess_subclass(struct pci_conf *p)
{
	int	i;

	for (i = 0; pci_nomatch_tab[i].desc != NULL; i++) {
		if ((pci_nomatch_tab[i].class == p->pc_class) &&
		    (pci_nomatch_tab[i].subclass == p->pc_subclass))
			return(pci_nomatch_tab[i].desc);
	}
	return(NULL);
}

static int
load_vendors(void)
{
	const char *dbf;
	FILE *db;
	struct pci_vendor_info *cv;
	struct pci_device_info *cd;
	char buf[1024], str[1024];
	char *ch;
	int id, error;

	/*
	 * Locate the database and initialise.
	 */
	TAILQ_INIT(&pci_vendors);
	if ((dbf = getenv("PCICONF_VENDOR_DATABASE")) == NULL)
		dbf = _PATH_LPCIVDB;
	if ((db = fopen(dbf, "r")) == NULL) {
		dbf = _PATH_PCIVDB;
		if ((db = fopen(dbf, "r")) == NULL)
			return(1);
	}
	cv = NULL;
	cd = NULL;
	error = 0;

	/*
	 * Scan input lines from the database
	 */
	for (;;) {
		if (fgets(buf, sizeof(buf), db) == NULL)
			break;

		if ((ch = strchr(buf, '#')) != NULL)
			*ch = '\0';
		ch = strchr(buf, '\0') - 1;
		while (ch > buf && isspace(*ch))
			*ch-- = '\0';
		if (ch <= buf)
			continue;

		/* Can't handle subvendor / subdevice entries yet */
		if (buf[0] == '\t' && buf[1] == '\t')
			continue;

		/* Check for vendor entry */
		if (buf[0] != '\t' && sscanf(buf, "%04x %[^\n]", &id, str) == 2) {
			if ((id == 0) || (strlen(str) < 1))
				continue;
			if ((cv = malloc(sizeof(struct pci_vendor_info))) == NULL) {
				warn("allocating vendor entry");
				error = 1;
				break;
			}
			if ((cv->desc = strdup(str)) == NULL) {
				free(cv);
				warn("allocating vendor description");
				error = 1;
				break;
			}
			cv->id = id;
			TAILQ_INIT(&cv->devs);
			TAILQ_INSERT_TAIL(&pci_vendors, cv, link);
			continue;
		}

		/* Check for device entry */
		if (buf[0] == '\t' && sscanf(buf + 1, "%04x %[^\n]", &id, str) == 2) {
			if ((id == 0) || (strlen(str) < 1))
				continue;
			if (cv == NULL) {
				warnx("device entry with no vendor!");
				continue;
			}
			if ((cd = malloc(sizeof(struct pci_device_info))) == NULL) {
				warn("allocating device entry");
				error = 1;
				break;
			}
			if ((cd->desc = strdup(str)) == NULL) {
				free(cd);
				warn("allocating device description");
				error = 1;
				break;
			}
			cd->id = id;
			TAILQ_INSERT_TAIL(&cv->devs, cd, link);
			continue;
		}

		/* It's a comment or junk, ignore it */
	}
	if (ferror(db))
		error = 1;
	fclose(db);

	return(error);
}

uint32_t
read_config(int fd, struct pcisel *sel, long reg, int width)
{
	struct pci_io pi;

	pi.pi_sel = *sel;
	pi.pi_reg = reg;
	pi.pi_width = width;

	if (ioctl(fd, PCIOCREAD, &pi) < 0)
		err(1, "ioctl(PCIOCREAD)");

	return (pi.pi_data);
}

static struct pcisel
getdevice(const char *name)
{
	struct pci_conf_io pc;
	struct pci_conf conf[1];
	struct pci_match_conf patterns[1];
	char *cp;
	int fd;	

	fd = open(_PATH_DEVPCI, O_RDONLY, 0);
	if (fd < 0)
		err(1, "%s", _PATH_DEVPCI);

	bzero(&pc, sizeof(struct pci_conf_io));
	pc.match_buf_len = sizeof(conf);
	pc.matches = conf;

	bzero(&patterns, sizeof(patterns));

	/*
	 * The pattern structure requires the unit to be split out from
	 * the driver name.  Walk backwards from the end of the name to
	 * find the start of the unit.
	 */
	if (name[0] == '\0')
		errx(1, "Empty device name");
	cp = strchr(name, '\0');
	assert(cp != NULL && cp != name);
	cp--;
	while (cp != name && isdigit(cp[-1]))
		cp--;
	if (cp == name || !isdigit(*cp))
		errx(1, "Invalid device name");
	if ((size_t)(cp - name) + 1 > sizeof(patterns[0].pd_name))
		errx(1, "Device name is too long");
	memcpy(patterns[0].pd_name, name, cp - name);
	patterns[0].pd_unit = strtol(cp, &cp, 10);
	if (*cp != '\0')
		errx(1, "Invalid device name");
	patterns[0].flags = PCI_GETCONF_MATCH_NAME | PCI_GETCONF_MATCH_UNIT;
	pc.num_patterns = 1;
	pc.pat_buf_len = sizeof(patterns);
	pc.patterns = patterns;

	if (ioctl(fd, PCIOCGETCONF, &pc) == -1)
		err(1, "ioctl(PCIOCGETCONF)");
	if (pc.status != PCI_GETCONF_LAST_DEVICE &&
	    pc.status != PCI_GETCONF_MORE_DEVS)
		errx(1, "error returned from PCIOCGETCONF ioctl");
	close(fd);
	if (pc.num_matches == 0)
		errx(1, "Device not found");
	return (conf[0].pc_sel);
}

static struct pcisel
parsesel(const char *str)
{
	const char *ep;
	char *eppos;
	struct pcisel sel;
	unsigned long selarr[4];
	int i;

	ep = strchr(str, '@');
	if (ep != NULL)
		ep++;
	else
		ep = str;

	if (strncmp(ep, "pci", 3) == 0) {
		ep += 3;
		i = 0;
		while (isdigit(*ep) && i < 4) {
			selarr[i++] = strtoul(ep, &eppos, 10);
			ep = eppos;
			if (*ep == ':')
				ep++;
		}
		if (i > 0 && *ep == '\0') {
			sel.pc_func = (i > 2) ? selarr[--i] : 0;
			sel.pc_dev = (i > 0) ? selarr[--i] : 0;
			sel.pc_bus = (i > 0) ? selarr[--i] : 0;
			sel.pc_domain = (i > 0) ? selarr[--i] : 0;
			return (sel);
		}
	}
	errx(1, "cannot parse selector %s", str);
}

static struct pcisel
getsel(const char *str)
{

	/*
	 * No device names contain colons and selectors always contain
	 * at least one colon.
	 */
	if (strchr(str, ':') == NULL)
		return (getdevice(str));
	else
		return (parsesel(str));
}

static void
readone(int fd, struct pcisel *sel, long reg, int width)
{

	printf("%0*x", width*2, read_config(fd, sel, reg, width));
}

static void
readit(const char *name, const char *reg, int width)
{
	long rstart;
	long rend;
	long r;
	char *end;
	int i;
	int fd;
	struct pcisel sel;

	fd = open(_PATH_DEVPCI, O_RDWR, 0);
	if (fd < 0)
		err(1, "%s", _PATH_DEVPCI);

	rend = rstart = strtol(reg, &end, 0);
	if (end && *end == ':') {
		end++;
		rend = strtol(end, (char **) 0, 0);
	}
	sel = getsel(name);
	for (i = 1, r = rstart; r <= rend; i++, r += width) {
		readone(fd, &sel, r, width);
		if (i && !(i % 8))
			putchar(' ');
		putchar(i % (16/width) ? ' ' : '\n');
	}
	if (i % (16/width) != 1)
		putchar('\n');
	close(fd);
}

static void
writeit(const char *name, const char *reg, const char *data, int width)
{
	int fd;
	struct pci_io pi;

	pi.pi_sel = getsel(name);
	pi.pi_reg = strtoul(reg, (char **)0, 0); /* XXX error check */
	pi.pi_width = width;
	pi.pi_data = strtoul(data, (char **)0, 0); /* XXX error check */

	fd = open(_PATH_DEVPCI, O_RDWR, 0);
	if (fd < 0)
		err(1, "%s", _PATH_DEVPCI);

	if (ioctl(fd, PCIOCWRITE, &pi) < 0)
		err(1, "ioctl(PCIOCWRITE)");
	close(fd);
}

static void
chkattached(const char *name)
{
	int fd;
	struct pci_io pi;

	pi.pi_sel = getsel(name);

	fd = open(_PATH_DEVPCI, O_RDWR, 0);
	if (fd < 0)
		err(1, "%s", _PATH_DEVPCI);

	if (ioctl(fd, PCIOCATTACHED, &pi) < 0)
		err(1, "ioctl(PCIOCATTACHED)");

	exitstatus = pi.pi_data ? 0 : 2; /* exit(2), if NOT attached */
	printf("%s: %s%s\n", name, pi.pi_data == 0 ? "not " : "", "attached");
	close(fd);
}

static void
dump_bar(const char *name, const char *reg, const char *bar_start,
    const char *bar_count, int width, int verbose)
{
	struct pci_bar_mmap pbm;
	uint32_t *dd;
	uint16_t *dh;
	uint8_t *db;
	uint64_t *dx, a, start, count;
	char *el;
	size_t res;
	int fd;

	start = 0;
	if (bar_start != NULL) {
		start = strtoul(bar_start, &el, 0);
		if (*el != '\0')
			errx(1, "Invalid bar start specification %s",
			    bar_start);
	}
	count = 0;
	if (bar_count != NULL) {
		count = strtoul(bar_count, &el, 0);
		if (*el != '\0')
			errx(1, "Invalid count specification %s",
			    bar_count);
	}

	pbm.pbm_sel = getsel(name);
	pbm.pbm_reg = strtoul(reg, &el, 0);
	if (*reg == '\0' || *el != '\0')
		errx(1, "Invalid bar specification %s", reg);
	pbm.pbm_flags = 0;
	pbm.pbm_memattr = VM_MEMATTR_UNCACHEABLE; /* XXX */

	fd = open(_PATH_DEVPCI, O_RDWR, 0);
	if (fd < 0)
		err(1, "%s", _PATH_DEVPCI);

	if (ioctl(fd, PCIOCBARMMAP, &pbm) < 0)
		err(1, "ioctl(PCIOCBARMMAP)");

	if (count == 0)
		count = pbm.pbm_bar_length / width;
	if (start + count < start || (start + count) * width < (uint64_t)width)
		errx(1, "(start + count) x width overflow");
	if ((start + count) * width > pbm.pbm_bar_length) {
		if (start * width > pbm.pbm_bar_length)
			count = 0;
		else
			count = (pbm.pbm_bar_length - start * width) / width;
	}
	if (verbose) {
		fprintf(stderr,
		    "Dumping pci%d:%d:%d:%d BAR %x mapped base %p "
		    "off %#x length %#jx from %#jx count %#jx in %d-bytes\n",
		    pbm.pbm_sel.pc_domain, pbm.pbm_sel.pc_bus,
		    pbm.pbm_sel.pc_dev, pbm.pbm_sel.pc_func,
		    pbm.pbm_reg, pbm.pbm_map_base, pbm.pbm_bar_off,
		    pbm.pbm_bar_length, start, count, width);
	}
	switch (width) {
	case 1:
		db = (uint8_t *)(uintptr_t)((uintptr_t)pbm.pbm_map_base +
		    pbm.pbm_bar_off + start * width);
		for (a = 0; a < count; a += width, db++) {
			res = fwrite(db, width, 1, stdout);
			if (res != 1) {
				errx(1, "error writing to stdout");
				break;
			}
		}
		break;
	case 2:
		dh = (uint16_t *)(uintptr_t)((uintptr_t)pbm.pbm_map_base +
		    pbm.pbm_bar_off + start * width);
		for (a = 0; a < count; a += width, dh++) {
			res = fwrite(dh, width, 1, stdout);
			if (res != 1) {
				errx(1, "error writing to stdout");
				break;
			}
		}
		break;
	case 4:
		dd = (uint32_t *)(uintptr_t)((uintptr_t)pbm.pbm_map_base +
		    pbm.pbm_bar_off + start * width);
		for (a = 0; a < count; a += width, dd++) {
			res = fwrite(dd, width, 1, stdout);
			if (res != 1) {
				errx(1, "error writing to stdout");
				break;
			}
		}
		break;
	case 8:
		dx = (uint64_t *)(uintptr_t)((uintptr_t)pbm.pbm_map_base +
		    pbm.pbm_bar_off + start * width);
		for (a = 0; a < count; a += width, dx++) {
			res = fwrite(dx, width, 1, stdout);
			if (res != 1) {
				errx(1, "error writing to stdout");
				break;
			}
		}
		break;
	default:
		errx(1, "invalid access width");
	}

	munmap((void *)pbm.pbm_map_base, pbm.pbm_map_length);
	close(fd);
}
