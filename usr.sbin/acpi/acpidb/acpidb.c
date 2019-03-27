/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2002 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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
 *	$FreeBSD$
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/stdint.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acapps.h>
#include <contrib/dev/acpica/include/acdebug.h>
#include <contrib/dev/acpica/include/amlresrc.h>

/*
 * Dummy DSDT Table Header
 */

static ACPI_TABLE_HEADER dummy_dsdt_table = {
	"DSDT", 123, 1, 123, "OEMID", "OEMTBLID", 1, "CRID", 1
};

/*
 * Region space I/O routines on virtual machine
 */

static int	aml_debug_prompt = 1;

struct ACPIRegionContent {
	TAILQ_ENTRY(ACPIRegionContent) links;
	int			regtype;
	ACPI_PHYSICAL_ADDRESS	addr;
	UINT8			value;
};

TAILQ_HEAD(ACPIRegionContentList, ACPIRegionContent);
static struct	ACPIRegionContentList RegionContentList;

static int		 aml_simulation_initialized = 0;

ACPI_PHYSICAL_ADDRESS	 AeLocalGetRootPointer(void);
void			 AeDoObjectOverrides(void);
void			 AeTableOverride(ACPI_TABLE_HEADER *, ACPI_TABLE_HEADER **);

static void		 aml_simulation_init(void);
static int		 aml_simulate_regcontent_add(int regtype,
			     ACPI_PHYSICAL_ADDRESS addr,
			     UINT8 value);
static int		 aml_simulate_regcontent_read(int regtype,
			     ACPI_PHYSICAL_ADDRESS addr,
			     UINT8 *valuep); 
static int		 aml_simulate_regcontent_write(int regtype,
			     ACPI_PHYSICAL_ADDRESS addr,
			     UINT8 *valuep);
static UINT64		 aml_simulate_prompt(char *msg, UINT64 def_val);
static void		 aml_simulation_regload(const char *dumpfile);
static void		 aml_simulation_regdump(const char *dumpfile);

/* Stubs to simplify linkage to the ACPICA core subsystem. */
ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer(void)
{

	return (0);
}

void
AeDoObjectOverrides(void)
{
}

void
AeTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_TABLE_HEADER **NewTable)
{
}

void
MpSaveGpioInfo(ACPI_PARSE_OBJECT *Op, AML_RESOURCE *Resource,
    UINT32 PinCount, UINT16 *PinList, char *DeviceName)
{
}

void
MpSaveSerialInfo(ACPI_PARSE_OBJECT *Op, AML_RESOURCE *Resource,
    char *DeviceName)
{
}

static void
aml_simulation_init(void)
{

	aml_simulation_initialized = 1;
	TAILQ_INIT(&RegionContentList);
	aml_simulation_regload("region.ini");
}

static int
aml_simulate_regcontent_add(int regtype, ACPI_PHYSICAL_ADDRESS addr, UINT8 value)
{
	struct	ACPIRegionContent *rc;

	rc = malloc(sizeof(struct ACPIRegionContent));
	if (rc == NULL) {
		return (-1);	/* malloc fail */
	}
	rc->regtype = regtype;
	rc->addr = addr;
	rc->value = value;

	TAILQ_INSERT_TAIL(&RegionContentList, rc, links);
	return (0);
}

static int
aml_simulate_regcontent_read(int regtype, ACPI_PHYSICAL_ADDRESS addr, UINT8 *valuep)
{
	struct	ACPIRegionContent *rc;

	if (!aml_simulation_initialized) {
		aml_simulation_init();
	}
	TAILQ_FOREACH(rc, &RegionContentList, links) {
		if (rc->regtype == regtype && rc->addr == addr) {
			*valuep = rc->value;
			return (1);	/* found */
		}
	}

	*valuep = 0;
	return (aml_simulate_regcontent_add(regtype, addr, *valuep));
}

static int
aml_simulate_regcontent_write(int regtype, ACPI_PHYSICAL_ADDRESS addr, UINT8 *valuep)
{
	struct	ACPIRegionContent *rc;

	if (!aml_simulation_initialized) {
		aml_simulation_init();
	}
	TAILQ_FOREACH(rc, &RegionContentList, links) {
		if (rc->regtype == regtype && rc->addr == addr) {
			rc->value = *valuep;
			return (1);	/* exists */
		}
	}

	return (aml_simulate_regcontent_add(regtype, addr, *valuep));
}

static UINT64
aml_simulate_prompt(char *msg, UINT64 def_val)
{
	char		buf[16], *ep;
	UINT64		val;

	val = def_val;
	printf("DEBUG");
	if (msg != NULL) {
		printf("%s", msg);
	}
	printf("(default: 0x%jx ", (uintmax_t)val);
	printf(" / %ju) >>", (uintmax_t)val);
	fflush(stdout);

	bzero(buf, sizeof buf);
	while (1) {
		if (read(0, buf, sizeof buf) == 0) {
			continue;
		}
		if (buf[0] == '\n') {
			break;	/* use default value */
		}
		if (buf[0] == '0' && buf[1] == 'x') {
			val = strtoq(buf, &ep, 16);
		} else {
			val = strtoq(buf, &ep, 10);
		}
		break;
	}
	return (val);
}

static void
aml_simulation_regload(const char *dumpfile)
{
	char	buf[256], *np, *ep;
	struct	ACPIRegionContent rc;
	FILE	*fp;

	if (!aml_simulation_initialized) {
		return;
	}

	if ((fp = fopen(dumpfile, "r")) == NULL) {
		return;
	}

	while (fgets(buf, sizeof buf, fp) != NULL) {
		np = buf;
		/* reading region type */
		rc.regtype = strtoq(np, &ep, 10);
		if (np == ep) {
			continue;
		}
		np = ep;

		/* reading address */
		rc.addr = strtoq(np, &ep, 16);
		if (np == ep) {
			continue;
		}
		np = ep;

		/* reading value */
		rc.value = strtoq(np, &ep, 16);
		if (np == ep) {
			continue;
		}
		aml_simulate_regcontent_write(rc.regtype, rc.addr, &rc.value);
	}

	fclose(fp);
}

static void
aml_simulation_regdump(const char *dumpfile)
{
	struct	ACPIRegionContent *rc;
	FILE	*fp;

	if (!aml_simulation_initialized) {
		return;
	}
	if ((fp = fopen(dumpfile, "w")) == NULL) {
		warn("%s", dumpfile);
		return;
	}
	while (!TAILQ_EMPTY(&RegionContentList)) {
		rc = TAILQ_FIRST(&RegionContentList);
		fprintf(fp, "%d	0x%jx	0x%x\n",
		    rc->regtype, (uintmax_t)rc->addr, rc->value);
		TAILQ_REMOVE(&RegionContentList, rc, links);
		free(rc);
	}

	fclose(fp);
	TAILQ_INIT(&RegionContentList);
}

/*
 * Space handlers on virtual machine
 */

static ACPI_STATUS
aml_vm_space_handler(
	UINT32			SpaceID,
	UINT32			Function,
	ACPI_PHYSICAL_ADDRESS	Address,
	UINT32			BitWidth,
	UINT64			*Value,
	int			Prompt)
{
	int			state;
	UINT8			val;
	UINT64			value, i;
	char			msg[256];
	static const char	*space_names[] = {
		"SYSTEM_MEMORY", "SYSTEM_IO", "PCI_CONFIG",
		"EC", "SMBUS", "CMOS", "PCI_BAR_TARGET"};

	switch (Function) {
	case ACPI_READ:
		value = 0;
		for (i = 0; (i * 8) < BitWidth; i++) {
			state = aml_simulate_regcontent_read(SpaceID,
							     Address + i, &val);
			if (state == -1) {
				return (AE_NO_MEMORY);
			}
			value |= val << (i * 8);
		}
		*Value = value;
		if (Prompt) {
			sprintf(msg, "[read (%s, %2d, 0x%jx)]",
				space_names[SpaceID], BitWidth,
				(uintmax_t)Address);
			*Value = aml_simulate_prompt(msg, value);
			if (*Value != value) {
				return(aml_vm_space_handler(SpaceID,
						ACPI_WRITE,
						Address, BitWidth, Value, 0));
			}
		}
		break;

	case ACPI_WRITE:
		value = *Value;
		if (Prompt) {
			sprintf(msg, "[write(%s, %2d, 0x%jx)]",
				space_names[SpaceID], BitWidth,
				(uintmax_t)Address);
			value = aml_simulate_prompt(msg, *Value);
		}
		*Value = value;
		for (i = 0; (i * 8) < BitWidth; i++) {
			val = value & 0xff;
			state = aml_simulate_regcontent_write(SpaceID,
							      Address + i, &val);
			if (state == -1) {
				return (AE_NO_MEMORY);
			}
			value = value >> 8;
		}
	}

	return (AE_OK);
}

#define DECLARE_VM_SPACE_HANDLER(name, id);			\
static ACPI_STATUS						\
aml_vm_space_handler_##name (					\
	UINT32			Function,			\
	ACPI_PHYSICAL_ADDRESS	Address,			\
	UINT32			BitWidth,			\
	UINT64			*Value)				\
{								\
	return (aml_vm_space_handler(id, Function, Address,	\
		BitWidth, Value, aml_debug_prompt));		\
}

DECLARE_VM_SPACE_HANDLER(system_memory,	ACPI_ADR_SPACE_SYSTEM_MEMORY);
DECLARE_VM_SPACE_HANDLER(system_io,	ACPI_ADR_SPACE_SYSTEM_IO);
DECLARE_VM_SPACE_HANDLER(pci_config,	ACPI_ADR_SPACE_PCI_CONFIG);
DECLARE_VM_SPACE_HANDLER(ec,		ACPI_ADR_SPACE_EC);
DECLARE_VM_SPACE_HANDLER(smbus,		ACPI_ADR_SPACE_SMBUS);
DECLARE_VM_SPACE_HANDLER(cmos,		ACPI_ADR_SPACE_CMOS);
DECLARE_VM_SPACE_HANDLER(pci_bar_target,ACPI_ADR_SPACE_PCI_BAR_TARGET);

/*
 * Load DSDT data file and invoke debugger
 */

static int
load_dsdt(const char *dsdtfile)
{
	char			filetmp[PATH_MAX];
	ACPI_NEW_TABLE_DESC	*list;
	u_int8_t		*code;
	struct stat		sb;
	int			dounlink, error, fd;

	fd = open(dsdtfile, O_RDONLY, 0);
	if (fd == -1) {
		perror("open");
		return (-1);
	}
	if (fstat(fd, &sb) == -1) {
		perror("fstat");
		close(fd);
		return (-1);
	}
	code = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, (off_t)0);
	close(fd);
	if (code == NULL) {
		perror("mmap");
		return (-1);
	}
	if ((error = AcpiInitializeSubsystem()) != AE_OK) {
		munmap(code, (size_t)sb.st_size);
		return (-1);
	}

	/*
	 * make sure DSDT data contains table header or not.
	 */
	if (strncmp((char *)code, "DSDT", 4) == 0) {
		dounlink = 0;
		strlcpy(filetmp, dsdtfile, sizeof(filetmp));
	} else {
		dounlink = 1;
		mode_t	mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		dummy_dsdt_table.Length = sizeof(ACPI_TABLE_HEADER) + sb.st_size;
		if ((size_t)snprintf(filetmp, sizeof(filetmp), "%s.tmp",
		    dsdtfile) > sizeof(filetmp) - 1) {
			fprintf(stderr, "file name too long\n");
			munmap(code, (size_t)sb.st_size);
			return (-1);
		}
		fd = open(filetmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
		if (fd == -1) {
			perror("open");
			munmap(code, (size_t)sb.st_size);
			return (-1);
		}
		write(fd, &dummy_dsdt_table, sizeof(ACPI_TABLE_HEADER));

		write(fd, code, sb.st_size);
		close(fd);
	}
	munmap(code, (size_t)sb.st_size);

	/*
	 * Install the virtual machine version of address space handlers.
	 */
	if ((error = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
			ACPI_ADR_SPACE_SYSTEM_MEMORY,
			(ACPI_ADR_SPACE_HANDLER)aml_vm_space_handler_system_memory,
			NULL, NULL)) != AE_OK) {
		fprintf(stderr, "could not initialise SystemMemory handler: %d\n", error);
		return (-1);
	}
	if ((error = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
			ACPI_ADR_SPACE_SYSTEM_IO,
			(ACPI_ADR_SPACE_HANDLER)aml_vm_space_handler_system_io,
			NULL, NULL)) != AE_OK) {
		fprintf(stderr, "could not initialise SystemIO handler: %d\n", error);
		return (-1);
	}
	if ((error = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
			ACPI_ADR_SPACE_PCI_CONFIG,
			(ACPI_ADR_SPACE_HANDLER)aml_vm_space_handler_pci_config,
			NULL, NULL)) != AE_OK) {
		fprintf(stderr, "could not initialise PciConfig handler: %d\n", error);
		return (-1);
	}
	if ((error = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
			ACPI_ADR_SPACE_EC,
			(ACPI_ADR_SPACE_HANDLER)aml_vm_space_handler_ec,
			NULL, NULL)) != AE_OK) {
		fprintf(stderr, "could not initialise EC handler: %d\n", error);
		return (-1);
	}
	if ((error = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
			ACPI_ADR_SPACE_SMBUS,
			(ACPI_ADR_SPACE_HANDLER)aml_vm_space_handler_smbus,
			NULL, NULL)) != AE_OK) {
		fprintf(stderr, "could not initialise SMBUS handler: %d\n", error);
		return (-1);
	}
	if ((error = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
			ACPI_ADR_SPACE_CMOS,
			(ACPI_ADR_SPACE_HANDLER)aml_vm_space_handler_cmos,
			NULL, NULL)) != AE_OK) {
		fprintf(stderr, "could not initialise CMOS handler: %d\n", error);
		return (-1);
	}
	if ((error = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
			ACPI_ADR_SPACE_PCI_BAR_TARGET,
			(ACPI_ADR_SPACE_HANDLER)aml_vm_space_handler_pci_bar_target,
			NULL, NULL)) != AE_OK) {
		fprintf(stderr, "could not initialise PCI BAR TARGET handler: %d\n", error);
		return (-1);
	}

	list = NULL;
	AcGetAllTablesFromFile(filetmp, TRUE, &list);

	AcpiInitializeDebugger();
	AcpiGbl_DebuggerConfiguration = 0;
	AcpiDbUserCommands();

	if (dounlink) {
		unlink(filetmp);
	}

	return (0);
}

static void
usage(const char *progname)
{

	printf("usage: %s dsdt_file\n", progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char	*progname;

	progname = argv[0];

	if (argc == 1) {
		usage(progname);
	}

	AcpiDbgLevel = ACPI_DEBUG_DEFAULT;

	/*
	 * Match kernel options for the interpreter.  Global variable names
	 * can be found in acglobal.h.
	 */
	AcpiGbl_EnableInterpreterSlack = TRUE;

	aml_simulation_regload("region.ini");
	if (load_dsdt(argv[1]) == 0) {
		aml_simulation_regdump("region.dmp");
	}

	return (0);
}
