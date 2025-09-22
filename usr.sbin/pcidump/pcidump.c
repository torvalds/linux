/*	$OpenBSD: pcidump.c,v 1.71 2024/04/23 13:34:51 jsg Exp $	*/

/*
 * Copyright (c) 2006, 2007 David Gwynne <loki@animata.net>
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/pciio.h>

#include <stdio.h>	/* need NULL for dev/pci/ headers */

#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/pcidevs_data.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <vis.h>

#define PCIDEV	"/dev/pci"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

__dead void usage(void);
void scanpcidomain(void);
int probe(int, int, int);
void dump(int, int, int);
void hexdump(int, int, int, int);
const char *str2busdevfunc(const char *, int *, int *, int *);
int pci_nfuncs(int, int);
int pci_read(int, int, int, u_int32_t, u_int32_t *);
int pci_readmask(int, int, int, u_int32_t, u_int32_t *);
void dump_bars(int, int, int, int);
void dump_caplist(int, int, int, u_int8_t);
void dump_vpd(int, int, int);
void dump_pci_powerstate(int, int, int, uint8_t);
void dump_pcie_linkspeed(int, int, int, uint8_t);
void dump_pcie_devserial(int, int, int, uint16_t);
void dump_msi(int, int, int, uint8_t);
void dump_msix(int, int, int, uint8_t);
void print_pcie_ls(uint8_t);
int dump_rom(int, int, int);
int dump_vga_bios(void);

static const char *
	pci_class_name(pci_class_t);
static const char *
	pci_subclass_name(pci_class_t, pci_subclass_t);

void	dump_type0(int bus, int dev, int func);
void	dump_type1(int bus, int dev, int func);
void	dump_type2(int bus, int dev, int func);

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-v] [-x | -xx | -xxx] [-d pcidev] [bus:dev:func]\n"
	    "       %s -r file [-d pcidev] bus:dev:func\n",
	    __progname, __progname);
	exit(1);
}

int pcifd;
int romfd;
int verbose = 0;
int hex = 0;
int size = 64;

const char *pci_capnames[] = {
	"Reserved",
	"Power Management",
	"AGP",
	"Vital Product Data (VPD)",
	"Slot Identification",
	"Message Signalled Interrupts (MSI)",
	"CompactPCI Hot Swap",
	"PCI-X",
	"AMD LDT/HT",
	"Vendor Specific",
	"Debug Port",
	"CompactPCI Central Resource Control",
	"PCI Hot-Plug",
	"PCI-PCI",
	"AGP8",
	"Secure",
	"PCI Express",
	"Extended Message Signalled Interrupts (MSI-X)",
	"SATA",
	"PCI Advanced Features",
	"Enhanced Allocation",
	"Flattening Portal Bridge",
};

const char *pci_enhanced_capnames[] = {
	"Unknown",
	"Advanced Error Reporting",
	"Virtual Channel Capability",
	"Device Serial Number",
	"Power Budgeting",
	"Root Complex Link Declaration",
	"Root Complex Internal Link Control",
	"Root Complex Event Collector",
	"Multi-Function VC Capability",
	"Virtual Channel Capability",
	"Root Complex/Root Bridge",
	"Vendor-Specific",
	"Config Access",
	"Access Control Services",
	"Alternate Routing ID",
	"Address Translation Services",
	"Single Root I/O Virtualization",
	"Multi Root I/O Virtualization",
	"Multicast",
	"Page Request Interface",
	"Reserved for AMD",
	"Resizable BAR",
	"Dynamic Power Allocation",
	"TPH Requester",
	"Latency Tolerance Reporting",
	"Secondary PCIe Capability",
	"Protocol Multiplexing",
	"Process Address Space ID",
	"LN Requester",
	"Downstream Port Containment",
	"L1 PM",
	"Precision Time Measurement",
	"PCI Express over M-PHY",
	"FRS Queueing",
	"Readiness Time Reporting",
	"Designated Vendor-Specific",
	"VF Resizable BAR",
	"Data Link Feature ",
	"Physical Layer 16.0 GT/s",
	"Lane Margining at the Receiver",
	"Hierarchy ID",
	"Native PCIe Enclosure Management",
	"Physical Layer 32.0 GT/s",
	"Alternate Protocol",
	"System Firmware Intermediary",
	"Shadow Functions",
	"Data Object Exchange",
	"Device 3",
	"Integrity and Data Encryption",
};

int
main(int argc, char *argv[])
{
	int nfuncs;
	int bus, dev, func;
	char pcidev[PATH_MAX] = PCIDEV;
	char *romfile = NULL;
	const char *errstr;
	int c, error = 0, dumpall = 1, domid = 0;

	while ((c = getopt(argc, argv, "d:r:vx")) != -1) {
		switch (c) {
		case 'd':
			strlcpy(pcidev, optarg, sizeof(pcidev));
			dumpall = 0;
			break;
		case 'r':
			romfile = optarg;
			dumpall = 0;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'x':
			hex++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1 || (romfile && argc != 1))
		usage();

	if (romfile) {
		romfd = open(romfile, O_WRONLY|O_CREAT|O_TRUNC, 0777);
		if (romfd == -1)
			err(1, "%s", romfile);
	}

	if (unveil("/dev", "r") == -1)
		err(1, "unveil /dev");
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	if (hex > 1)
		size = 256;
	if (hex > 2)
		size = 4096;

	if (argc == 1)
		dumpall = 0;

	if (dumpall == 0) {
		pcifd = open(pcidev, O_RDONLY);
		if (pcifd == -1)
			err(1, "%s", pcidev);
	} else {
		for (;;) {
			snprintf(pcidev, 16, "/dev/pci%d", domid++);
			pcifd = open(pcidev, O_RDONLY);
			if (pcifd == -1) {
				if (errno == ENXIO || errno == ENOENT) {
					return 0;
				} else {
					err(1, "%s", pcidev);
				}
			}
			printf("Domain %s:\n", pcidev);
			scanpcidomain();
			close(pcifd);
		}
	}

	if (argc == 1) {
		errstr = str2busdevfunc(argv[0], &bus, &dev, &func);
		if (errstr != NULL)
			errx(1, "\"%s\": %s", argv[0], errstr);

		nfuncs = pci_nfuncs(bus, dev);
		if (nfuncs == -1 || func > nfuncs)
			error = ENXIO;
		else if (romfile)
			error = dump_rom(bus, dev, func);
		else
			error = probe(bus, dev, func);

		if (error != 0)
			errc(1, error, "\"%s\"", argv[0]);
	} else {
		printf("Domain %s:\n", pcidev);
		scanpcidomain();
	}

	return (0);
}

void
scanpcidomain(void)
{
	int nfuncs;
	int bus, dev, func;

	for (bus = 0; bus < 256; bus++) {
		for (dev = 0; dev < 32; dev++) {
			nfuncs = pci_nfuncs(bus, dev);
			for (func = 0; func < nfuncs; func++) {
				probe(bus, dev, func);
			}
		}
	}
}

const char *
str2busdevfunc(const char *string, int *bus, int *dev, int *func)
{
	const char *errstr;
	char b[80], *d, *f;

	strlcpy(b, string, sizeof(b));

	d = strchr(b, ':');
	if (d == NULL)
		return("device not specified");
	*d++ = '\0';

	f = strchr(d, ':');
	if (f == NULL)
		return("function not specified");
	*f++ = '\0';

	*bus = strtonum(b, 0, 255, &errstr);
	if (errstr != NULL)
		return (errstr);
	*dev = strtonum(d, 0, 31, &errstr);
	if (errstr != NULL)
		return (errstr);
	*func = strtonum(f, 0, 7, &errstr);
	if (errstr != NULL)
		return (errstr);

	return (NULL);
}

int
probe(int bus, int dev, int func)
{
	u_int32_t id_reg;
	const struct pci_known_vendor *pkv;
	const struct pci_known_product *pkp;
	const char *vendor = NULL, *product = NULL;

	if (pci_read(bus, dev, func, PCI_ID_REG, &id_reg) != 0)
		return (errno);

	if (PCI_VENDOR(id_reg) == PCI_VENDOR_INVALID ||
	    PCI_VENDOR(id_reg) == 0)
		return (ENXIO);

	for (pkv = pci_known_vendors; pkv->vendorname != NULL; pkv++) {
		if (pkv->vendor == PCI_VENDOR(id_reg)) {
			vendor = pkv->vendorname;
			break;
		}
	}

	if (vendor != NULL) {
		for (pkp = pci_known_products; pkp->productname != NULL; pkp++)
			if (pkp->vendor == PCI_VENDOR(id_reg) &&
				pkp->product == PCI_PRODUCT(id_reg)) {
				product = pkp->productname;
				break;
			}
	}

	printf(" %d:%d:%d: %s %s\n", bus, dev, func,
	    (vendor == NULL) ? "unknown" : vendor,
	    (product == NULL) ? "unknown" : product);

	if (verbose)
		dump(bus, dev, func);
	if (hex > 0)
		hexdump(bus, dev, func, size);

	return (0);
}

int
print_bytes(const uint8_t *buf, size_t len)
{
	char dst[8];
	size_t i;

	for (i = 0; i < len; i++) {
		vis(dst, buf[i], VIS_TAB|VIS_NL, 0);
		printf("%s", dst);
	}
	printf("\n");

	return (0);
}

int
print_vpd(const uint8_t *buf, size_t len)
{
	const struct pci_vpd *vpd;
	char key0[8];
	char key1[8];
	size_t vlen;

	while (len > 0) {
		if (len < sizeof(*vpd))
			return (1);

		vpd = (const struct pci_vpd *)buf;
		vis(key0, vpd->vpd_key0, VIS_TAB|VIS_NL, 0);
		vis(key1, vpd->vpd_key1, VIS_TAB|VIS_NL, 0);
		vlen = vpd->vpd_len;

		printf("\t\t    %s%s: ", key0, key1);

		buf += sizeof(*vpd);
		len -= sizeof(*vpd);

		if (len < vlen)
			return (1);
		print_bytes(buf, vlen);

		buf += vlen;
		len -= vlen;
	}

	return (0);
}

void
dump_vpd(int bus, int dev, int func)
{
	struct pci_vpd_req io;
	uint32_t data[64]; /* XXX this can be up to 32k of data */
	uint8_t *buf = (uint8_t *)data;
	size_t len = sizeof(data);

	bzero(&io, sizeof(io));
	io.pv_sel.pc_bus = bus;
	io.pv_sel.pc_dev = dev;
	io.pv_sel.pc_func = func;
	io.pv_offset = 0;
	io.pv_count = nitems(data);
	io.pv_data = data;

	if (ioctl(pcifd, PCIOCGETVPD, &io) == -1) {
		warn("PCIOCGETVPD");
		return;
	}

	do {
		uint8_t vpd = *buf;
		uint8_t type;
		size_t hlen, vlen;
		int (*print)(const uint8_t *, size_t) = print_bytes;

		if (PCI_VPDRES_ISLARGE(vpd)) {
			struct pci_vpd_largeres *res;
			type = PCI_VPDRES_LARGE_NAME(vpd);

			switch (type) {
			case PCI_VPDRES_TYPE_IDENTIFIER_STRING:
				printf("\t\tProduct Name: ");
				break;
			case PCI_VPDRES_TYPE_VPD:
				print = print_vpd;
				break;
			default:
				printf("%02x: ", type);
				break;
			}

			if (len < sizeof(*res))
				goto trunc;
			res = (struct pci_vpd_largeres *)buf;

			hlen = sizeof(*res);
			vlen = ((size_t)res->vpdres_len_msb << 8) |
			    (size_t)res->vpdres_len_lsb;
		} else { /* small */
			type = PCI_VPDRES_SMALL_NAME(vpd);
			if (type == PCI_VPDRES_TYPE_END_TAG)
				break;

			printf("\t\t");
			switch (type) {
			case PCI_VPDRES_TYPE_COMPATIBLE_DEVICE_ID:
			case PCI_VPDRES_TYPE_VENDOR_DEFINED:
			default:
				printf("%02x", type);
				break;
			}

			hlen = sizeof(vpd);
			vlen = PCI_VPDRES_SMALL_LENGTH(vpd);
		}
		buf += hlen;
		len -= hlen;

		if (len < vlen)
			goto trunc;
		(*print)(buf, vlen);

		buf += vlen;
		len -= vlen;
	} while (len > 0);

	return;
trunc:
	/* i have spent too much time in tcpdump - dlg */
	printf("[|vpd]\n");
}

void
dump_pci_powerstate(int bus, int dev, int func, uint8_t ptr)
{
	u_int32_t pmcsr;

	if (pci_read(bus, dev, func, ptr + PCI_PMCSR, &pmcsr) != 0)
		return;

	printf("\t\tState: D%d", pmcsr & PCI_PMCSR_STATE_MASK);
	if (pmcsr & PCI_PMCSR_PME_EN)
		printf(" PME# enabled");
	if (pmcsr & PCI_PMCSR_PME_STATUS)
		printf(" PME# asserted");
	printf("\n");
}

static unsigned int
pcie_dcap_mps(uint32_t dcap)
{
	uint32_t shift = dcap & 0x7;
	return (128 << shift);
}

static unsigned int
pcie_dcsr_mps(uint32_t dcsr)
{
	uint32_t shift = (dcsr >> 5) & 0x7;
	return (128 << shift);
}

static unsigned int
pcie_dcsr_mrrs(uint32_t dcsr)
{
	uint32_t shift = (dcsr >> 12) & 0x7;
	return (128 << shift);
}

void
print_pcie_ls(uint8_t speed)
{
	if (speed == 6)
		printf("64.0");
	else if (speed == 5)
		printf("32.0");
	else if (speed == 4)
		printf("16.0");
	else if (speed == 3)
		printf("8.0");
	else if (speed == 2)
		printf("5.0");
	else if (speed == 1)
		printf("2.5");
	else
		printf("unknown (%d)", speed);
}

void
dump_pcie_linkspeed(int bus, int dev, int func, uint8_t ptr)
{
	u_int32_t dcap, dcsr;
	u_int32_t lcap, lcsr;
	u_int8_t cwidth, cspeed, swidth, sspeed;

	if (pci_read(bus, dev, func, ptr + PCI_PCIE_DCAP, &dcap) != 0)
		return;

	if (pci_read(bus, dev, func, ptr + PCI_PCIE_DCSR, &dcsr) != 0)
		return;

	printf("\t\tMax Payload Size: %u / %u bytes\n",
	    pcie_dcsr_mps(dcsr), pcie_dcap_mps(dcap));
	printf("\t\tMax Read Request Size: %u bytes\n",
	    pcie_dcsr_mrrs(dcsr));

	if (pci_read(bus, dev, func, ptr + PCI_PCIE_LCAP, &lcap) != 0)
		return;
	cspeed = lcap & 0x0f;
	cwidth = (lcap >> 4) & 0x3f;
	if (cwidth == 0)
		return;

	if (pci_read(bus, dev, func, ptr + PCI_PCIE_LCSR, &lcsr) != 0)
		return;
	sspeed = (lcsr >> 16) & 0x0f;
	swidth = (lcsr >> 20) & 0x3f;

	printf("\t\tLink Speed: ");
	print_pcie_ls(sspeed);
	printf(" / ");
	print_pcie_ls(cspeed);
	printf(" GT/s\n");

	printf("\t\tLink Width: x%d / x%d\n", swidth, cwidth);
}

void
dump_pcie_devserial(int bus, int dev, int func, u_int16_t ptr)
{
	uint32_t lower, upper;
	uint64_t serial;

	if ((pci_read(bus, dev, func, ptr + 8, &upper) != 0) ||
	    (pci_read(bus, dev, func, ptr + 4, &lower) != 0))
		return;

	serial = ((uint64_t)upper << 32) | (uint64_t)lower;

	printf("\t\tSerial Number: %016llx\n", serial);
}

void
dump_msi(int bus, int dev, int func, u_int8_t ptr)
{
	u_int32_t reg;

	if (pci_read(bus, dev, func, ptr, &reg) != 0)
		return;

	printf("\t\tEnabled: %s; %d vectors (%d enabled)\n",
	    reg & PCI_MSI_MC_MSIE ? "yes" : "no",
	    (1 << ((reg & PCI_MSI_MC_MMC_MASK) >> PCI_MSI_MC_MMC_SHIFT)),
	    (1 << ((reg & PCI_MSI_MC_MME_MASK) >> PCI_MSI_MC_MME_SHIFT)));
}

void
dump_msix(int bus, int dev, int func, u_int8_t ptr)
{
	u_int32_t reg;
	u_int32_t table;

	if ((pci_read(bus, dev, func, ptr, &reg) != 0) ||
	    (pci_read(bus, dev, func, ptr + PCI_MSIX_TABLE, &table) != 0))
		return;

	printf("\t\tEnabled: %s; table size %d (BAR %d:%d)\n",
	    reg & PCI_MSIX_MC_MSIXE ? "yes" : "no",
	    PCI_MSIX_MC_TBLSZ(reg) + 1,
	    (table & PCI_MSIX_TABLE_BIR),
	    (table & PCI_MSIX_TABLE_OFF));
}

void
dump_pcie_enhanced_caplist(int bus, int dev, int func)
{
	u_int32_t reg;
	u_int32_t capidx;
	u_int16_t ptr;
	u_int16_t ecap;

	ptr = PCI_PCIE_ECAP;

	do {
		if (pci_read(bus, dev, func, ptr, &reg) != 0)
			return;

		if (PCI_PCIE_ECAP_ID(reg) == 0xffff &&
		    PCI_PCIE_ECAP_NEXT(reg) == PCI_PCIE_ECAP_LAST)
			return;

		ecap = PCI_PCIE_ECAP_ID(reg);
		if (ecap >= nitems(pci_enhanced_capnames))
			capidx = 0;
		else
			capidx = ecap;

		printf("\t0x%04x: Enhanced Capability 0x%02x: ", ptr, ecap);
		printf("%s\n", pci_enhanced_capnames[capidx]);

		switch (ecap) {
		case 0x03:
			dump_pcie_devserial(bus, dev, func, ptr);
			break;
		}

		ptr = PCI_PCIE_ECAP_NEXT(reg);

	} while (ptr != PCI_PCIE_ECAP_LAST);
}

void
dump_caplist(int bus, int dev, int func, u_int8_t ptr)
{
	u_int32_t reg;
	u_int8_t cap;

	if (pci_read(bus, dev, func, PCI_COMMAND_STATUS_REG, &reg) != 0)
		return;
	if (!(reg & PCI_STATUS_CAPLIST_SUPPORT))
		return;

	if (pci_read(bus, dev, func, ptr, &reg) != 0)
		return;
	ptr = PCI_CAPLIST_PTR(reg);
	while (ptr != 0) {
		if (pci_read(bus, dev, func, ptr, &reg) != 0)
			return;
		cap = PCI_CAPLIST_CAP(reg);
		printf("\t0x%04x: Capability 0x%02x: ", ptr, cap);
		if (cap >= nitems(pci_capnames))
			cap = 0;
		printf("%s\n", pci_capnames[cap]);
		switch (cap) {
		case PCI_CAP_PWRMGMT:
			dump_pci_powerstate(bus, dev, func, ptr);
			break;
		case PCI_CAP_VPD:
			dump_vpd(bus, dev, func);
			break;
		case PCI_CAP_PCIEXPRESS:
			dump_pcie_linkspeed(bus, dev, func, ptr);
			dump_pcie_enhanced_caplist(bus, dev, func);
			break;
		case PCI_CAP_MSI:
			dump_msi(bus, dev,func, ptr);
			break;
		case PCI_CAP_MSIX:
			dump_msix(bus, dev, func, ptr);
			break;
		}
		ptr = PCI_CAPLIST_NEXT(reg);
	}
}

void
dump_bars(int bus, int dev, int func, int end)
{
	const char *memtype;
	u_int64_t mem;
	u_int64_t mask;
	u_int32_t reg, reg1;
	int bar;

	for (bar = PCI_MAPREG_START; bar < end; bar += 0x4) {
		if (pci_read(bus, dev, func, bar, &reg) != 0 ||
		    pci_readmask(bus, dev, func, bar, &reg1) != 0)
			warn("unable to read PCI_MAPREG 0x%02x", bar);

		printf("\t0x%04x: BAR ", bar);

		if (reg == 0 && reg1 == 0) {
			printf("empty (%08x)\n", reg);
			continue;
		}

		switch (PCI_MAPREG_TYPE(reg)) {
		case PCI_MAPREG_TYPE_MEM:
			printf("mem ");
			if (PCI_MAPREG_MEM_PREFETCHABLE(reg))
				printf("prefetchable ");

			memtype = "32bit 1m";
			switch (PCI_MAPREG_MEM_TYPE(reg)) {
			case PCI_MAPREG_MEM_TYPE_32BIT:
				memtype = "32bit";
			case PCI_MAPREG_MEM_TYPE_32BIT_1M:
				printf("%s ", memtype);

				printf("addr: 0x%08x/0x%08x\n",
				    PCI_MAPREG_MEM_ADDR(reg),
				    PCI_MAPREG_MEM_SIZE(reg1));

				break;
			case PCI_MAPREG_MEM_TYPE_64BIT:
				mem = reg;
				mask = reg1;
				bar += 0x04;
				if (pci_read(bus, dev, func, bar, &reg) != 0 ||
				    pci_readmask(bus, dev, func, bar, &reg1) != 0)
					warn("unable to read 0x%02x", bar);

				mem |= (u_int64_t)reg << 32;
				mask |= (u_int64_t)reg1 << 32;

				printf("64bit addr: 0x%016llx/0x%08llx\n",
				    PCI_MAPREG_MEM64_ADDR(mem),
				    PCI_MAPREG_MEM64_SIZE(mask));

				break;
			}
			break;

		case PCI_MAPREG_TYPE_IO:
			printf("io addr: 0x%08x/0x%04x\n",
			    PCI_MAPREG_IO_ADDR(reg),
			    PCI_MAPREG_IO_SIZE(reg1));
			break;
		}
	}
}

void
dump_type0(int bus, int dev, int func)
{
	u_int32_t reg;

	dump_bars(bus, dev, func, PCI_MAPREG_END);

	if (pci_read(bus, dev, func, PCI_CARDBUS_CIS_REG, &reg) != 0)
		warn("unable to read PCI_CARDBUS_CIS_REG");
	printf("\t0x%04x: Cardbus CIS: %08x\n", PCI_CARDBUS_CIS_REG, reg);

	if (pci_read(bus, dev, func, PCI_SUBSYS_ID_REG, &reg) != 0)
		warn("unable to read PCI_SUBSYS_ID_REG");
	printf("\t0x%04x: Subsystem Vendor ID: %04x Product ID: %04x\n",
	    PCI_SUBSYS_ID_REG, PCI_VENDOR(reg), PCI_PRODUCT(reg));

	if (pci_read(bus, dev, func, PCI_ROM_REG, &reg) != 0)
		warn("unable to read PCI_ROM_REG");
	printf("\t0x%04x: Expansion ROM Base Address: %08x\n",
	    PCI_ROM_REG, reg);

	if (pci_read(bus, dev, func, 0x38, &reg) != 0)
		warn("unable to read 0x38 (reserved)");
	printf("\t0x%04x: %08x\n", 0x38, reg);

	if (pci_read(bus, dev, func, PCI_INTERRUPT_REG, &reg) != 0)
		warn("unable to read PCI_INTERRUPT_REG");
	printf("\t0x%04x: Interrupt Pin: %02x Line: %02x Min Gnt: %02x"
	    " Max Lat: %02x\n", PCI_INTERRUPT_REG, PCI_INTERRUPT_PIN(reg),
	    PCI_INTERRUPT_LINE(reg), PCI_MIN_GNT(reg), PCI_MAX_LAT(reg));
}

void
dump_type1(int bus, int dev, int func)
{
	u_int32_t reg;

	dump_bars(bus, dev, func, PCI_MAPREG_PPB_END);

	if (pci_read(bus, dev, func, PCI_PRIBUS_1, &reg) != 0)
		warn("unable to read PCI_PRIBUS_1");
	printf("\t0x%04x: Primary Bus: %d, Secondary Bus: %d, "
	    "Subordinate Bus: %d,\n\t\tSecondary Latency Timer: %02x\n",
	    PCI_PRIBUS_1, (reg >> 0) & 0xff, (reg >> 8) & 0xff,
	    (reg >> 16) & 0xff, (reg >> 24) & 0xff);

	if (pci_read(bus, dev, func, PCI_IOBASEL_1, &reg) != 0)
		warn("unable to read PCI_IOBASEL_1");
	printf("\t0x%04x: I/O Base: %02x, I/O Limit: %02x, "
	    "Secondary Status: %04x\n", PCI_IOBASEL_1, (reg >> 0 ) & 0xff,
	    (reg >> 8) & 0xff, (reg >> 16) & 0xffff);

	if (pci_read(bus, dev, func, PCI_MEMBASE_1, &reg) != 0)
		warn("unable to read PCI_MEMBASE_1");
	printf("\t0x%04x: Memory Base: %04x, Memory Limit: %04x\n",
	    PCI_MEMBASE_1, (reg >> 0) & 0xffff, (reg >> 16) & 0xffff);

	if (pci_read(bus, dev, func, PCI_PMBASEL_1, &reg) != 0)
		warn("unable to read PCI_PMBASEL_1");
	printf("\t0x%04x: Prefetch Memory Base: %04x, "
	    "Prefetch Memory Limit: %04x\n", PCI_PMBASEL_1,
	    (reg >> 0) & 0xffff, (reg >> 16) & 0xffff);

#undef PCI_PMBASEH_1
#define PCI_PMBASEH_1	0x28
	if (pci_read(bus, dev, func, PCI_PMBASEH_1, &reg) != 0)
		warn("unable to read PCI_PMBASEH_1");
	printf("\t0x%04x: Prefetch Memory Base Upper 32 Bits: %08x\n",
	    PCI_PMBASEH_1, reg);

#undef PCI_PMLIMITH_1
#define PCI_PMLIMITH_1	0x2c
	if (pci_read(bus, dev, func, PCI_PMLIMITH_1, &reg) != 0)
		warn("unable to read PCI_PMLIMITH_1");
	printf("\t0x%04x: Prefetch Memory Limit Upper 32 Bits: %08x\n",
	    PCI_PMLIMITH_1, reg);

#undef PCI_IOBASEH_1
#define PCI_IOBASEH_1	0x30
	if (pci_read(bus, dev, func, PCI_IOBASEH_1, &reg) != 0)
		warn("unable to read PCI_IOBASEH_1");
	printf("\t0x%04x: I/O Base Upper 16 Bits: %04x, "
	    "I/O Limit Upper 16 Bits: %04x\n", PCI_IOBASEH_1,
	    (reg >> 0) & 0xffff, (reg >> 16) & 0xffff);

#define PCI_PPB_ROM_REG		0x38
	if (pci_read(bus, dev, func, PCI_PPB_ROM_REG, &reg) != 0)
		warn("unable to read PCI_PPB_ROM_REG");
	printf("\t0x%04x: Expansion ROM Base Address: %08x\n",
	    PCI_PPB_ROM_REG, reg);

	if (pci_read(bus, dev, func, PCI_INTERRUPT_REG, &reg) != 0)
		warn("unable to read PCI_INTERRUPT_REG");
	printf("\t0x%04x: Interrupt Pin: %02x, Line: %02x, "
	    "Bridge Control: %04x\n",
	    PCI_INTERRUPT_REG, PCI_INTERRUPT_PIN(reg),
	    PCI_INTERRUPT_LINE(reg), reg >> 16);
}

void
dump_type2(int bus, int dev, int func)
{
	u_int32_t reg;

	if (pci_read(bus, dev, func, PCI_MAPREG_START, &reg) != 0)
		warn("unable to read PCI_MAPREG\n");
	printf("\t0x%04x: Cardbus Control Registers Base Address: %08x\n",
	    PCI_MAPREG_START, reg);

	if (pci_read(bus, dev, func, PCI_PRIBUS_2, &reg) != 0)
		warn("unable to read PCI_PRIBUS_2");
	printf("\t0x%04x: Primary Bus: %d Cardbus Bus: %d "
	    "Subordinate Bus: %d \n\t        Cardbus Latency Timer: %02x\n",
	    PCI_PRIBUS_2, (reg >> 0) & 0xff, (reg >> 8) & 0xff,
	    (reg >> 16) & 0xff, (reg >> 24) & 0xff);

	if (pci_read(bus, dev, func, PCI_MEMBASE0_2, &reg) != 0)
		warn("unable to read PCI_MEMBASE0_2\n");
	printf("\t0x%04x: Memory Base 0: %08x\n", PCI_MEMBASE0_2, reg);

	if (pci_read(bus, dev, func, PCI_MEMLIMIT0_2, &reg) != 0)
		warn("unable to read PCI_MEMLIMIT0_2\n");
	printf("\t0x%04x: Memory Limit 0: %08x\n", PCI_MEMLIMIT0_2, reg);

	if (pci_read(bus, dev, func, PCI_MEMBASE1_2, &reg) != 0)
		warn("unable to read PCI_MEMBASE1_2\n");
	printf("\t0x%04x: Memory Base 1: %08x\n", PCI_MEMBASE1_2, reg);

	if (pci_read(bus, dev, func, PCI_MEMLIMIT1_2, &reg) != 0)
		warn("unable to read PCI_MEMLIMIT1_2\n");
	printf("\t0x%04x: Memory Limit 1: %08x\n", PCI_MEMLIMIT1_2, reg);

	if (pci_read(bus, dev, func, PCI_IOBASE0_2, &reg) != 0)
		warn("unable to read PCI_IOBASE0_2\n");
	printf("\t0x%04x: I/O Base 0: %08x\n", PCI_IOBASE0_2, reg);

	if (pci_read(bus, dev, func, PCI_IOLIMIT0_2, &reg) != 0)
		warn("unable to read PCI_IOLIMIT0_2\n");
	printf("\t0x%04x: I/O Limit 0: %08x\n", PCI_IOLIMIT0_2, reg);

	if (pci_read(bus, dev, func, PCI_IOBASE1_2, &reg) != 0)
		warn("unable to read PCI_IOBASE1_2\n");
	printf("\t0x%04x: I/O Base 1: %08x\n", PCI_IOBASE1_2, reg);

	if (pci_read(bus, dev, func, PCI_IOLIMIT1_2, &reg) != 0)
		warn("unable to read PCI_IOLIMIT1_2\n");
	printf("\t0x%04x: I/O Limit 1: %08x\n", PCI_IOLIMIT1_2, reg);

	if (pci_read(bus, dev, func, PCI_INTERRUPT_REG, &reg) != 0)
		warn("unable to read PCI_INTERRUPT_REG");
	printf("\t0x%04x: Interrupt Pin: %02x Line: %02x "
	    "Bridge Control: %04x\n",
	    PCI_INTERRUPT_REG, PCI_INTERRUPT_PIN(reg),
	    PCI_INTERRUPT_LINE(reg), reg >> 16);

	if (pci_read(bus, dev, func, PCI_SUBVEND_2, &reg) != 0)
		warn("unable to read PCI_SUBVEND_2");
	printf("\t0x%04x: Subsystem Vendor ID: %04x Product ID: %04x\n",
	    PCI_SUBVEND_2, PCI_VENDOR(reg), PCI_PRODUCT(reg));

	if (pci_read(bus, dev, func, PCI_PCCARDIF_2, &reg) != 0)
		warn("unable to read PCI_PCCARDIF_2\n");
	printf("\t0x%04x: 16-bit Legacy Mode Base Address: %08x\n",
	    PCI_PCCARDIF_2, reg);
}

void
dump(int bus, int dev, int func)
{
	u_int32_t reg;
	u_int8_t capptr = PCI_CAPLISTPTR_REG;
	pci_class_t class;
	pci_subclass_t subclass;

	if (pci_read(bus, dev, func, PCI_ID_REG, &reg) != 0)
		warn("unable to read PCI_ID_REG");
	printf("\t0x%04x: Vendor ID: %04x, Product ID: %04x\n", PCI_ID_REG,
	    PCI_VENDOR(reg), PCI_PRODUCT(reg));

	if (pci_read(bus, dev, func, PCI_COMMAND_STATUS_REG, &reg) != 0)
		warn("unable to read PCI_COMMAND_STATUS_REG");
	printf("\t0x%04x: Command: %04x, Status: %04x\n",
	    PCI_COMMAND_STATUS_REG, reg & 0xffff, (reg  >> 16) & 0xffff);

	if (pci_read(bus, dev, func, PCI_CLASS_REG, &reg) != 0)
		warn("unable to read PCI_CLASS_REG");
	class = PCI_CLASS(reg);
	subclass = PCI_SUBCLASS(reg);
	printf("\t0x%04x:\tClass: %02x %s,", PCI_CLASS_REG, class,
	    pci_class_name(class));
	printf(" Subclass: %02x %s,", subclass,
	    pci_subclass_name(class, subclass));
	printf("\n\t\tInterface: %02x, Revision: %02x\n",
	    PCI_INTERFACE(reg), PCI_REVISION(reg));

	if (pci_read(bus, dev, func, PCI_BHLC_REG, &reg) != 0)
		warn("unable to read PCI_BHLC_REG");
	printf("\t0x%04x: BIST: %02x, Header Type: %02x, "
	    "Latency Timer: %02x,\n\t\tCache Line Size: %02x\n", PCI_BHLC_REG,
	    PCI_BIST(reg), PCI_HDRTYPE(reg),
	    PCI_LATTIMER(reg), PCI_CACHELINE(reg));

	switch (PCI_HDRTYPE_TYPE(reg)) {
	case 2:
		dump_type2(bus, dev, func);
		capptr = PCI_CARDBUS_CAPLISTPTR_REG;
		break;
	case 1:
		dump_type1(bus, dev, func);
		break;
	case 0:
		dump_type0(bus, dev, func);
		break;
	default:
		break;
	}
	dump_caplist(bus, dev, func, capptr);
}

void
hexdump(int bus, int dev, int func, int size)
{
	u_int32_t reg;
	int i;

	for (i = 0; i < size; i += 4) {
		if (pci_read(bus, dev, func, i, &reg) != 0) {
			if (errno == EINVAL)
				return;
			warn("unable to read 0x%02x", i);
		}

		if ((i % 16) == 0)
			printf("\t0x%04x:", i);
		printf(" %08x", reg);

		if ((i % 16) == 12)
			printf("\n");
	}
}

int
pci_nfuncs(int bus, int dev)
{
	u_int32_t hdr;

	if (pci_read(bus, dev, 0, PCI_BHLC_REG, &hdr) != 0)
		return (-1);

	return (PCI_HDRTYPE_MULTIFN(hdr) ? 8 : 1);
}

int
pci_read(int bus, int dev, int func, u_int32_t reg, u_int32_t *val)
{
	struct pci_io io;
	int rv;

	bzero(&io, sizeof(io));
	io.pi_sel.pc_bus = bus;
	io.pi_sel.pc_dev = dev;
	io.pi_sel.pc_func = func;
	io.pi_reg = reg;
	io.pi_width = 4;

	rv = ioctl(pcifd, PCIOCREAD, &io);
	if (rv != 0)
		return (rv);

	*val = io.pi_data;

	return (0);
}

int
pci_readmask(int bus, int dev, int func, u_int32_t reg, u_int32_t *val)
{
	struct pci_io io;
	int rv;

	bzero(&io, sizeof(io));
	io.pi_sel.pc_bus = bus;
	io.pi_sel.pc_dev = dev;
	io.pi_sel.pc_func = func;
	io.pi_reg = reg;
	io.pi_width = 4;

	rv = ioctl(pcifd, PCIOCREADMASK, &io);
	if (rv != 0)
		return (rv);

	*val = io.pi_data;

	return (0);
}

int
dump_rom(int bus, int dev, int func)
{
	struct pci_rom rom;
	u_int32_t cr, addr;

	if (pci_read(bus, dev, func, PCI_ROM_REG, &addr) != 0 ||
	    pci_read(bus, dev, func, PCI_CLASS_REG, &cr) != 0)
		return (errno);

	if (addr == 0 && PCI_CLASS(cr) == PCI_CLASS_DISPLAY &&
	    PCI_SUBCLASS(cr) == PCI_SUBCLASS_DISPLAY_VGA)
		return dump_vga_bios();

	bzero(&rom, sizeof(rom));
	rom.pr_sel.pc_bus = bus;
	rom.pr_sel.pc_dev = dev;
	rom.pr_sel.pc_func = func;
	if (ioctl(pcifd, PCIOCGETROMLEN, &rom) == -1)
		return (errno);

	rom.pr_rom = malloc(rom.pr_romlen);
	if (rom.pr_rom == NULL)
		return (ENOMEM);

	if (ioctl(pcifd, PCIOCGETROM, &rom) == -1)
		return (errno);

	if (write(romfd, rom.pr_rom, rom.pr_romlen) == -1)
		return (errno);

	return (0);
}

#define VGA_BIOS_ADDR	0xc0000
#define VGA_BIOS_LEN	0x10000

int
dump_vga_bios(void)
{
#if defined(__amd64__) || defined(__i386__)
	void *bios;
	int fd;

	fd = open(_PATH_MEM, O_RDONLY);
	if (fd == -1)
		err(1, "%s", _PATH_MEM);

	bios = malloc(VGA_BIOS_LEN);
	if (bios == NULL)
		return (ENOMEM);

	if (pread(fd, bios, VGA_BIOS_LEN, VGA_BIOS_ADDR) == -1)
		err(1, "%s", _PATH_MEM);

	if (write(romfd, bios, VGA_BIOS_LEN) == -1) {
		free(bios);
		return (errno);
	}

	free(bios);

	return (0);
#else
	return (ENODEV);
#endif
}

struct pci_subclass {
	pci_subclass_t	 subclass;
	const char	*name;
};

struct pci_class {
	pci_class_t	 class;
	const char	*name;
	const struct pci_subclass
			*subclass;
	size_t		 nsubclass;
};

static const struct pci_subclass pci_subclass_prehistoric[] = {
	{ PCI_SUBCLASS_PREHISTORIC_MISC,	"Miscellaneous"	},
	{ PCI_SUBCLASS_PREHISTORIC_VGA,		"VGA"		},
};

static const struct pci_subclass pci_subclass_mass_storage[] = {
	{ PCI_SUBCLASS_MASS_STORAGE_SCSI,	"SCSI"		},
	{ PCI_SUBCLASS_MASS_STORAGE_IDE,	"IDE"		},
	{ PCI_SUBCLASS_MASS_STORAGE_FLOPPY,	"Floppy"	},
	{ PCI_SUBCLASS_MASS_STORAGE_IPI,	"IPI"		},
	{ PCI_SUBCLASS_MASS_STORAGE_RAID,	"RAID"		},
	{ PCI_SUBCLASS_MASS_STORAGE_ATA,	"ATA"		},
	{ PCI_SUBCLASS_MASS_STORAGE_SATA,	"SATA"		},
	{ PCI_SUBCLASS_MASS_STORAGE_SAS,	"SAS"		},
	{ PCI_SUBCLASS_MASS_STORAGE_UFS,	"UFS"		},
	{ PCI_SUBCLASS_MASS_STORAGE_NVM,	"NVM"		},
	{ PCI_SUBCLASS_MASS_STORAGE_MISC,	"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_network[] = {
	{ PCI_SUBCLASS_NETWORK_ETHERNET,	"Ethernet"	},
	{ PCI_SUBCLASS_NETWORK_TOKENRING,	"Token Ring"	},
	{ PCI_SUBCLASS_NETWORK_FDDI,		"FDDI"		},
	{ PCI_SUBCLASS_NETWORK_ATM,		"ATM"		},
	{ PCI_SUBCLASS_NETWORK_ISDN,		"ISDN"		},
	{ PCI_SUBCLASS_NETWORK_WORLDFIP,	"WorldFip"	},
	{ PCI_SUBCLASS_NETWORK_PCIMGMULTICOMP,	"PCMIG Multi Computing"	},
	{ PCI_SUBCLASS_NETWORK_INFINIBAND,	"InfiniBand"	},
	{ PCI_SUBCLASS_NETWORK_MISC,		"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_display[] = {
	{ PCI_SUBCLASS_DISPLAY_VGA,		"VGA"		},
	{ PCI_SUBCLASS_DISPLAY_XGA,		"XGA"		},
	{ PCI_SUBCLASS_DISPLAY_3D,		"3D"		},
	{ PCI_SUBCLASS_DISPLAY_MISC,		"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_multimedia[] = {
	{ PCI_SUBCLASS_MULTIMEDIA_VIDEO,	"Video"		},
	{ PCI_SUBCLASS_MULTIMEDIA_AUDIO,	"Audio"		},
	{ PCI_SUBCLASS_MULTIMEDIA_TELEPHONY,	"Telephony"	},
	{ PCI_SUBCLASS_MULTIMEDIA_HDAUDIO,	"HD Audio"	},
	{ PCI_SUBCLASS_MULTIMEDIA_MISC,		"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_memory[] = {
	{ PCI_SUBCLASS_MEMORY_RAM,		"RAM"		},
	{ PCI_SUBCLASS_MEMORY_FLASH,		"Flash"		},
	{ PCI_SUBCLASS_MEMORY_MISC,		"Miscellaneous" },
};

static const struct pci_subclass pci_subclass_bridge[] = {
	{ PCI_SUBCLASS_BRIDGE_HOST,		"Host"		},
	{ PCI_SUBCLASS_BRIDGE_ISA,		"ISA"		},
	{ PCI_SUBCLASS_BRIDGE_EISA,		"EISA"		},
	{ PCI_SUBCLASS_BRIDGE_MC,		"MicroChannel"	},
	{ PCI_SUBCLASS_BRIDGE_PCI,		"PCI"		},
	{ PCI_SUBCLASS_BRIDGE_PCMCIA,		"PCMCIA"	},
	{ PCI_SUBCLASS_BRIDGE_NUBUS,		"NuBus"		},
	{ PCI_SUBCLASS_BRIDGE_RACEWAY,		"RACEway"	},
	{ PCI_SUBCLASS_BRIDGE_STPCI,		"Semi-transparent PCI" },
	{ PCI_SUBCLASS_BRIDGE_INFINIBAND,	"InfiniBand"	},
	{ PCI_SUBCLASS_BRIDGE_MISC,		"Miscellaneous"	},
	{ PCI_SUBCLASS_BRIDGE_AS,		"advanced switching" },
};

static const struct pci_subclass pci_subclass_communications[] = {
	{ PCI_SUBCLASS_COMMUNICATIONS_SERIAL,	"Serial"	},
	{ PCI_SUBCLASS_COMMUNICATIONS_PARALLEL,	"Parallel"	},
	{ PCI_SUBCLASS_COMMUNICATIONS_MPSERIAL,	"Multi-port Serial" },
	{ PCI_SUBCLASS_COMMUNICATIONS_MODEM,	"Modem"		},
	{ PCI_SUBCLASS_COMMUNICATIONS_GPIB,	"GPIB"		},
	{ PCI_SUBCLASS_COMMUNICATIONS_SMARTCARD,
						"Smartcard"	},
	{ PCI_SUBCLASS_COMMUNICATIONS_MISC,	"Miscellaneous" },
};

static const struct pci_subclass pci_subclass_system[] = {
	{ PCI_SUBCLASS_SYSTEM_PIC,		"Interrupt"	},
	{ PCI_SUBCLASS_SYSTEM_DMA,		"8237 DMA"	},
	{ PCI_SUBCLASS_SYSTEM_TIMER,		"8254 Timer"	},
	{ PCI_SUBCLASS_SYSTEM_RTC,		"RTC"		},
	{ PCI_SUBCLASS_SYSTEM_SDHC,		"SDHC"		},
	{ PCI_SUBCLASS_SYSTEM_IOMMU,		"IOMMU"		},
	{ PCI_SUBCLASS_SYSTEM_ROOTCOMPEVENT,	"Root Complex Event" },
	{ PCI_SUBCLASS_SYSTEM_MISC,		"Miscellaneous" },
};

static const struct pci_subclass pci_subclass_input[] = {
	{ PCI_SUBCLASS_INPUT_KEYBOARD,		"Keyboard"	},
	{ PCI_SUBCLASS_INPUT_DIGITIZER,		"Digitizer"	},
	{ PCI_SUBCLASS_INPUT_MOUSE,		"Mouse"		},
	{ PCI_SUBCLASS_INPUT_SCANNER,		"Scanner"	},
	{ PCI_SUBCLASS_INPUT_GAMEPORT,		"Game Port"	},
	{ PCI_SUBCLASS_INPUT_MISC,		"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_dock[] = {
	{ PCI_SUBCLASS_DOCK_GENERIC,		"Generic"	},
	{ PCI_SUBCLASS_DOCK_MISC,		"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_processor[] = {
	{ PCI_SUBCLASS_PROCESSOR_386,		"386"		},
	{ PCI_SUBCLASS_PROCESSOR_486,		"486"		},
	{ PCI_SUBCLASS_PROCESSOR_PENTIUM,	"Pentium"	},
	{ PCI_SUBCLASS_PROCESSOR_ALPHA,		"Alpha"		},
	{ PCI_SUBCLASS_PROCESSOR_POWERPC,	"PowerPC"	},
	{ PCI_SUBCLASS_PROCESSOR_MIPS,		"MIPS"		},
	{ PCI_SUBCLASS_PROCESSOR_COPROC,	"Co-Processor"	},
};

static const struct pci_subclass pci_subclass_serialbus[] = {
	{ PCI_SUBCLASS_SERIALBUS_FIREWIRE,	"FireWire"	},
	{ PCI_SUBCLASS_SERIALBUS_ACCESS,	"ACCESS.bus"	},
	{ PCI_SUBCLASS_SERIALBUS_SSA,		"SSA"		},
	{ PCI_SUBCLASS_SERIALBUS_USB,		"USB"		},
	{ PCI_SUBCLASS_SERIALBUS_FIBER,		"Fiber Channel"	},
	{ PCI_SUBCLASS_SERIALBUS_SMBUS,		"SMBus"		},
	{ PCI_SUBCLASS_SERIALBUS_INFINIBAND,	"InfiniBand"	},
	{ PCI_SUBCLASS_SERIALBUS_IPMI,		"IPMI"		},
	{ PCI_SUBCLASS_SERIALBUS_SERCOS,	"SERCOS"	},
	{ PCI_SUBCLASS_SERIALBUS_CANBUS,	"CANbus"	},
};

static const struct pci_subclass pci_subclass_wireless[] = {
	{ PCI_SUBCLASS_WIRELESS_IRDA,		"IrDA"		},
	{ PCI_SUBCLASS_WIRELESS_CONSUMERIR,	"Consumer IR"	},
	{ PCI_SUBCLASS_WIRELESS_RF,		"RF"		},
	{ PCI_SUBCLASS_WIRELESS_BLUETOOTH,	"Bluetooth"	},
	{ PCI_SUBCLASS_WIRELESS_BROADBAND,	"Broadband"	},
	{ PCI_SUBCLASS_WIRELESS_802_11A,	"802.11a"	},
	{ PCI_SUBCLASS_WIRELESS_802_11B,	"802.11b"	},
	{ PCI_SUBCLASS_WIRELESS_MISC,		"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_i2o[] = {
	{ PCI_SUBCLASS_I2O_STANDARD,		"Standard"	},
};

static const struct pci_subclass pci_subclass_satcom[] = {
	{ PCI_SUBCLASS_SATCOM_TV,		"TV"		},
	{ PCI_SUBCLASS_SATCOM_AUDIO,		"Audio"		},
	{ PCI_SUBCLASS_SATCOM_VOICE,		"Voice"		},
	{ PCI_SUBCLASS_SATCOM_DATA,		"Data"		},
};

static const struct pci_subclass pci_subclass_crypto[] = {
	{ PCI_SUBCLASS_CRYPTO_NETCOMP,		"Network/Computing" },
	{ PCI_SUBCLASS_CRYPTO_ENTERTAINMENT,	"Entertainment"	},
	{ PCI_SUBCLASS_CRYPTO_MISC,		"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_dasp[] = {
	{ PCI_SUBCLASS_DASP_DPIO,		"DPIO"		},
	{ PCI_SUBCLASS_DASP_TIMEFREQ,		"Time and Frequency" },
	{ PCI_SUBCLASS_DASP_SYNC,		"Synchronization" },
	{ PCI_SUBCLASS_DASP_MGMT,		"Management"	},
	{ PCI_SUBCLASS_DASP_MISC,		"Miscellaneous"	},
};

static const struct pci_subclass pci_subclass_accelerator[] = {};
static const struct pci_subclass pci_subclass_instrumentation[] = {};

#define CLASS(_c, _n, _s) { \
	.class = _c, \
	.name = _n, \
	.subclass = _s, \
	.nsubclass = nitems(_s), \
}

static const struct pci_class pci_classes[] = {
	CLASS(PCI_CLASS_PREHISTORIC,	"Prehistoric",
	    pci_subclass_prehistoric),
	CLASS(PCI_CLASS_MASS_STORAGE,	"Mass Storage",
	    pci_subclass_mass_storage),
	CLASS(PCI_CLASS_NETWORK,	"Network",
	    pci_subclass_network),
	CLASS(PCI_CLASS_DISPLAY,	"Display",
	    pci_subclass_display),
	CLASS(PCI_CLASS_MULTIMEDIA,	"Multimedia",
	    pci_subclass_multimedia),
	CLASS(PCI_CLASS_MEMORY,		"Memory",
	    pci_subclass_memory),
	CLASS(PCI_CLASS_BRIDGE,		"Bridge",
	    pci_subclass_bridge),
	CLASS(PCI_CLASS_COMMUNICATIONS,	"Communications",
	    pci_subclass_communications),
	CLASS(PCI_CLASS_SYSTEM,		"System",
	    pci_subclass_system),
	CLASS(PCI_CLASS_INPUT,		"Input",
	    pci_subclass_input),
	CLASS(PCI_CLASS_DOCK,		"Dock",
	    pci_subclass_dock),
	CLASS(PCI_CLASS_PROCESSOR,	"Processor",
	    pci_subclass_processor),
	CLASS(PCI_CLASS_SERIALBUS,	"Serial Bus",
	    pci_subclass_serialbus),
	CLASS(PCI_CLASS_WIRELESS,	"Wireless",
	    pci_subclass_wireless),
	CLASS(PCI_CLASS_I2O,		"I2O",
	    pci_subclass_i2o),
	CLASS(PCI_CLASS_SATCOM,		"Satellite Comm",
	    pci_subclass_satcom),
	CLASS(PCI_CLASS_CRYPTO,		"Crypto",
	    pci_subclass_crypto),
	CLASS(PCI_CLASS_DASP,		"DASP",
	    pci_subclass_dasp),
	CLASS(PCI_CLASS_ACCELERATOR,	"Accelerator",
	    pci_subclass_accelerator),
	CLASS(PCI_CLASS_INSTRUMENTATION, "Instrumentation",
	    pci_subclass_instrumentation),
};

static const struct pci_class *
pci_class(pci_class_t class)
{
	const struct pci_class *pc;
	size_t i;

	for (i = 0; i < nitems(pci_classes); i++) {
		pc = &pci_classes[i];
		if (pc->class == class)
			return (pc);
	}

	return (NULL);
}

static const struct pci_subclass *
pci_subclass(const struct pci_class *pc, pci_subclass_t subclass)
{
	const struct pci_subclass *ps;
	size_t i;

	for (i = 0; i < pc->nsubclass; i++) {
		ps = &pc->subclass[i];
		if (ps->subclass == subclass)
			return (ps);
	}

	return (NULL);
}

static const char *
pci_class_name(pci_class_t class)
{
	const struct pci_class *pc;

	pc = pci_class(class);
	if (pc == NULL)
		return ("(unknown)");

	return (pc->name);
}

static const char *
pci_subclass_name(pci_class_t class, pci_subclass_t subclass)
{
	const struct pci_class *pc;
	const struct pci_subclass *ps;

	pc = pci_class(class);
	if (pc == NULL)
		return ("(unknown)");

	ps = pci_subclass(pc, subclass);
	if (ps == NULL || ps->name == NULL)
		return ("(unknown)");

	return (ps->name);
}
