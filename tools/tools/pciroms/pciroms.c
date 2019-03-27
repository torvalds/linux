/*
 * Copyright (c) 2007 Bruce M. Simpson.
 * All rights reserved
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/pciio.h>
#include <sys/mman.h>
#include <sys/memrange.h>
#include <sys/stat.h>
#include <machine/endian.h>

#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define	_PATH_DEVPCI	"/dev/pci"
#define	_PATH_DEVMEM	"/dev/mem"

#define	PCI_CFG_CMD		0x04		/* command register */
#define	PCI_CFG_ROM_BAR		0x30		/* rom base register */

#define	PCI_ROM_ADDR_MASK	0xFFFFFC00	/* the 21 MSBs form the BAR */
#define	PCI_ROM_RESERVED_MASK	0x03FE		/* mask for reserved bits */
#define	PCI_ROM_ACTIVATE	0x01		/* mask for activation bit */

#define	PCI_CMD_MEM_SPACE	0x02		/* memory space bit */
#define	PCI_HDRTYPE_MFD		0x80		/* MFD bit in HDRTYPE reg. */

#define	MAX_PCI_DEVS		64		/* # of devices in system */

typedef enum {
	PRINT = 0,
	SAVE = 1
} action_t;

/*
 * This is set to a safe physical base address in PCI range for my Vaio.
 * YOUR MACHINE *WILL* VARY, I SUGGEST YOU LOOK UP YOUR MACHINE'S MEMORY
 * MAP IN DETAIL IF YOU PLAN ON SAVING ROMS.
 *
 * This is the hole between the APIC and the BIOS (FED00000-FEDFFFFF);
 * should be a safe range on the i815 Solano chipset.
 */
#define PCI_DEFAULT_ROM_ADDR	0xFED00000

static char *progname = NULL;
static uintptr_t base_addr = PCI_DEFAULT_ROM_ADDR;

static void	usage(void);
static void	banner(void);
static void	pci_enum_devs(int pci_fd, action_t action);
static uint32_t	pci_testrombar(int pci_fd, struct pci_conf *dev);
static int	pci_enable_bars(int pci_fd, struct pci_conf *dev,
    uint16_t *oldcmd);
static int	pci_disable_bars(int pci_fd, struct pci_conf *dev,
    uint16_t *oldcmd);
static int	pci_save_rom(char *filename, int romsize);

int
main(int argc, char *argv[])
{
	int		 pci_fd;
	int		 err;
	int		 ch;
	action_t	 action;
	char		*base_addr_string;
	char		*ep;

	err = -1;
	pci_fd = -1;
	action = PRINT;
	base_addr_string = NULL;
	ep = NULL;
	progname = basename(argv[0]);

	while ((ch = getopt(argc, argv, "sb:h")) != -1)
		switch (ch) {
		case 's':
			action = SAVE;
			break;
		case 'b':
			base_addr_string = optarg;
			break;
		case 'h':
		default:
		     usage();
	}
	argc -= optind;
	argv += optind;

	if (base_addr_string != NULL) {
		uintmax_t base_addr_max;

		base_addr_max = strtoumax(base_addr_string, &ep, 16);
		if (*ep != '\0') {
			fprintf(stderr, "Invalid base address.\r\n");
			usage();
		}
		/* XXX: TODO: deal with 64-bit PCI. */
		base_addr = (uintptr_t)base_addr_max;
		base_addr &= ~PCI_ROM_RESERVED_MASK;
	}

	if (argc > 0)
		usage();

	if ((pci_fd = open(_PATH_DEVPCI, O_RDWR)) == -1) {
		perror("open");
		goto cleanup;
	}

	banner();
	pci_enum_devs(pci_fd, action);

	err = 0;
cleanup:
	if (pci_fd != -1)
		close(pci_fd);

	exit ((err == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void
usage(void)
{

	fprintf(stderr, "usage: %s [-s] [-b <base-address>]\r\n", progname);
	exit(EXIT_FAILURE);
}

static void
banner(void)
{

	fprintf(stderr,
		"WARNING: You are advised to run this program in single\r\n"
		"user mode, with few or no processes running.\r\n\r\n");
}

/*
 * Enumerate PCI device list to a limit of MAX_PCI_DEVS devices.
 */
static void
pci_enum_devs(int pci_fd, action_t action)
{
	struct pci_conf		 devs[MAX_PCI_DEVS];
	char			 filename[16];
	struct pci_conf_io	 pc;
	struct pci_conf		*p;
	int			 result;
	int			 romsize;
	uint16_t		 oldcmd;

	result = -1;
	romsize = 0;

	bzero(&pc, sizeof(pc));
	pc.match_buf_len = sizeof(devs);
	pc.matches = devs;

	if (ioctl(pci_fd, PCIOCGETCONF, &pc) == -1) {
		perror("ioctl PCIOCGETCONF");
		return;
	}

	if (pc.status == PCI_GETCONF_ERROR) {
		fprintf(stderr,
		    "Error fetching PCI device list from kernel.\r\n");
		return;
	}

	if (pc.status == PCI_GETCONF_MORE_DEVS) {
		fprintf(stderr,
"More than %d devices exist. Only the first %d will be inspected.\r\n",
		    MAX_PCI_DEVS, MAX_PCI_DEVS);
	}

	for (p = devs ; p < &devs[pc.num_matches]; p++) {

		/* No PCI bridges; only PCI devices. */
		if (p->pc_hdr != 0x00)
			continue;

		romsize = pci_testrombar(pci_fd, p);

		switch (action) {
		case PRINT:
			printf(
"Domain %04Xh Bus %02Xh Device %02Xh Function %02Xh: ",
				p->pc_sel.pc_domain, p->pc_sel.pc_bus,
				p->pc_sel.pc_dev, p->pc_sel.pc_func);
			printf((romsize ? "%dKB ROM aperture detected."
					: "No ROM present."), romsize/1024);
			printf("\r\n");
			break;
		case SAVE:
			if (romsize == 0)
				continue;	/* XXX */

			snprintf(filename, sizeof(filename), "%08X.rom",
			    ((p->pc_device << 16) | p->pc_vendor));

			fprintf(stderr, "Saving %dKB ROM image to %s...\r\n",
			    romsize, filename);

			if (pci_enable_bars(pci_fd, p, &oldcmd) == 0)
				result = pci_save_rom(filename, romsize);

			pci_disable_bars(pci_fd, p, &oldcmd);

			if (result == 0)  {
				fprintf(stderr, "Done.\r\n");
			} else  {
				fprintf(stderr,
"An error occurred whilst saving the ROM.\r\n");
			}
			break;
		} /* switch */
	} /* for */
}

/*
 * Return: size of ROM aperture off dev, 0 if no ROM exists.
 */
static uint32_t
pci_testrombar(int pci_fd, struct pci_conf *dev)
{
	struct pci_io	 io;
	uint32_t	 romsize;

	romsize = 0;

	/*
	 * Only attempt to discover ROMs on Header Type 0x00 devices.
	 */
	if (dev->pc_hdr != 0x00)
		return romsize;

	/*
	 * Activate ROM BAR
	 */
	io.pi_sel = dev->pc_sel;
	io.pi_reg = PCI_CFG_ROM_BAR;
	io.pi_width = 4;
	io.pi_data = 0xFFFFFFFF;
	if (ioctl(pci_fd, PCIOCWRITE, &io) == -1)
		return romsize;

	/*
	 * Read back ROM BAR and compare with mask
	 */
	if (ioctl(pci_fd, PCIOCREAD, &io) == -1)
		return 0;

	/*
	 * Calculate ROM aperture if one was set.
	 */
	if (io.pi_data & PCI_ROM_ADDR_MASK)
		romsize = -(io.pi_data & PCI_ROM_ADDR_MASK);

	/*
	 * Disable the ROM BAR when done.
	 */
	io.pi_data = 0;
	if (ioctl(pci_fd, PCIOCWRITE, &io) == -1)
		return 0;

	return romsize;
}

static int
pci_save_rom(char *filename, int romsize)
{
	int	 fd, mem_fd, err;
	void	*map_addr;

	fd = err = mem_fd = -1;
	map_addr = MAP_FAILED;

	if ((mem_fd = open(_PATH_DEVMEM, O_RDONLY)) == -1) {
		perror("open");
		return -1;
	}

	map_addr = mmap(NULL, romsize, PROT_READ, MAP_SHARED|MAP_NOCORE,
	    mem_fd, base_addr);

	/* Dump ROM aperture to a file. */
	if ((fd = open(filename, O_CREAT|O_RDWR|O_TRUNC|O_NOFOLLOW,
	    S_IRUSR|S_IWUSR)) == -1) {
		perror("open");
		goto cleanup;
	}

	if (write(fd, map_addr, romsize) != romsize)
		perror("write");

	err = 0;
cleanup:
	if (fd != -1)
		close(fd);

	if (map_addr != MAP_FAILED)
		munmap((void *)base_addr, romsize);

	if (mem_fd != -1)
		close(mem_fd);

	return err;
}

static int
pci_enable_bars(int pci_fd, struct pci_conf *dev, uint16_t *oldcmd)
{
	struct pci_io io;

	/* Don't grok bridges. */
	if (dev->pc_hdr != 0x00)
		return -1;

	/* Save command register. */
	io.pi_sel = dev->pc_sel;
	io.pi_reg = PCI_CFG_CMD;
	io.pi_width = 2;
	if (ioctl(pci_fd, PCIOCREAD, &io) == -1)
		return -1;
	*oldcmd = (uint16_t)io.pi_data;

	io.pi_data |= PCI_CMD_MEM_SPACE;
	if (ioctl(pci_fd, PCIOCWRITE, &io) == -1)
		return -1;

	/*
	 * Activate ROM BAR and map at the specified base address.
	 */
	io.pi_sel = dev->pc_sel;
	io.pi_reg = PCI_CFG_ROM_BAR;
	io.pi_width = 4;
	io.pi_data = (base_addr | PCI_ROM_ACTIVATE);
	if (ioctl(pci_fd, PCIOCWRITE, &io) == -1)
		return -1;

	return 0;
}

static int
pci_disable_bars(int pci_fd, struct pci_conf *dev, uint16_t *oldcmd)
{
	struct pci_io	 io;

	/*
	 * Clear ROM BAR to deactivate the mapping.
	 */
	io.pi_sel = dev->pc_sel;
	io.pi_reg = PCI_CFG_ROM_BAR;
	io.pi_width = 4;
	io.pi_data = 0;
	if (ioctl(pci_fd, PCIOCWRITE, &io) == -1)
		return 0;

	/*
	 * Restore state of the command register.
	 */
	io.pi_sel = dev->pc_sel;
	io.pi_reg = PCI_CFG_CMD;
	io.pi_width = 2;
	io.pi_data = *oldcmd;
	if (ioctl(pci_fd, PCIOCWRITE, &io) == -1) {
		perror("ioctl");
		return 0;
	}

	return 0;
}
