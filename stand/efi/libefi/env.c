/*
 * Copyright (c) 2015 Netflix, Inc.
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

#include <stand.h>
#include <string.h>
#include <efi.h>
#include <efichar.h>
#include <efilib.h>
#include <efigpt.h>	/* Partition GUIDS */
#include <Guid/MemoryTypeInformation.h>
#include <Guid/MtcVendor.h>
#include <Guid/ZeroGuid.h>
#include <Protocol/EdidActive.h>
#include <Protocol/EdidDiscovered.h>
#include <uuid.h>
#include <stdbool.h>
#include <sys/param.h>
#include "bootstrap.h"

/*
 * About ENABLE_UPDATES
 *
 * The UEFI variables are identified only by GUID and name, there is no
 * way to (auto)detect the type for the value, so we need to process the
 * variables case by case, as we do learn about them.
 *
 * While showing the variable name and the value is safe, we must not store
 * random values nor allow removing (random) variables.
 *
 * Since we do have stub code to set/unset the variables, I do want to keep
 * it to make the future development a bit easier, but the updates are disabled
 * by default till:
 *	a) the validation and data translation to values is properly implemented
 *	b) We have established which variables we do allow to be updated.
 * Therefore the set/unset code is included only for developers aid.
 */

static struct efi_uuid_mapping {
	const char *efi_guid_name;
	EFI_GUID efi_guid;
} efi_uuid_mapping[] = {
	{ .efi_guid_name = "global", .efi_guid = EFI_GLOBAL_VARIABLE },
	{ .efi_guid_name = "freebsd", .efi_guid = FREEBSD_BOOT_VAR_GUID },
	/* EFI Systab entry names. */
	{ .efi_guid_name = "MPS Table", .efi_guid = MPS_TABLE_GUID },
	{ .efi_guid_name = "ACPI Table", .efi_guid = ACPI_TABLE_GUID },
	{ .efi_guid_name = "ACPI 2.0 Table", .efi_guid = ACPI_20_TABLE_GUID },
	{ .efi_guid_name = "SMBIOS Table", .efi_guid = SMBIOS_TABLE_GUID },
	{ .efi_guid_name = "SMBIOS3 Table", .efi_guid = SMBIOS3_TABLE_GUID },
	{ .efi_guid_name = "DXE Table", .efi_guid = DXE_SERVICES_TABLE_GUID },
	{ .efi_guid_name = "HOB List Table", .efi_guid = HOB_LIST_TABLE_GUID },
	{ .efi_guid_name = EFI_MEMORY_TYPE_INFORMATION_VARIABLE_NAME,
	    .efi_guid = EFI_MEMORY_TYPE_INFORMATION_GUID },
	{ .efi_guid_name = "Debug Image Info Table",
	    .efi_guid = DEBUG_IMAGE_INFO_TABLE_GUID },
	{ .efi_guid_name = "FDT Table", .efi_guid = FDT_TABLE_GUID },
	/*
	 * Protocol names for debug purposes.
	 * Can be removed along with lsefi command.
	 */
	{ .efi_guid_name = "device path", .efi_guid = DEVICE_PATH_PROTOCOL },
	{ .efi_guid_name = "block io", .efi_guid = BLOCK_IO_PROTOCOL },
	{ .efi_guid_name = "disk io", .efi_guid = DISK_IO_PROTOCOL },
	{ .efi_guid_name = "disk info", .efi_guid =
	    EFI_DISK_INFO_PROTOCOL_GUID },
	{ .efi_guid_name = "simple fs",
	    .efi_guid = SIMPLE_FILE_SYSTEM_PROTOCOL },
	{ .efi_guid_name = "load file", .efi_guid = LOAD_FILE_PROTOCOL },
	{ .efi_guid_name = "device io", .efi_guid = DEVICE_IO_PROTOCOL },
	{ .efi_guid_name = "unicode collation",
	    .efi_guid = UNICODE_COLLATION_PROTOCOL },
	{ .efi_guid_name = "unicode collation2",
	    .efi_guid = EFI_UNICODE_COLLATION2_PROTOCOL_GUID },
	{ .efi_guid_name = "simple network",
	    .efi_guid = EFI_SIMPLE_NETWORK_PROTOCOL },
	{ .efi_guid_name = "simple text output",
	    .efi_guid = SIMPLE_TEXT_OUTPUT_PROTOCOL },
	{ .efi_guid_name = "simple text input",
	    .efi_guid = SIMPLE_TEXT_INPUT_PROTOCOL },
	{ .efi_guid_name = "simple text ex input",
	    .efi_guid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID },
	{ .efi_guid_name = "console control",
	    .efi_guid = EFI_CONSOLE_CONTROL_PROTOCOL_GUID },
	{ .efi_guid_name = "stdin", .efi_guid = EFI_CONSOLE_IN_DEVICE_GUID },
	{ .efi_guid_name = "stdout", .efi_guid = EFI_CONSOLE_OUT_DEVICE_GUID },
	{ .efi_guid_name = "stderr",
	    .efi_guid = EFI_STANDARD_ERROR_DEVICE_GUID },
	{ .efi_guid_name = "GOP",
	    .efi_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID },
	{ .efi_guid_name = "UGA draw", .efi_guid = EFI_UGA_DRAW_PROTOCOL_GUID },
	{ .efi_guid_name = "PXE base code",
	    .efi_guid = EFI_PXE_BASE_CODE_PROTOCOL },
	{ .efi_guid_name = "PXE base code callback",
	    .efi_guid = EFI_PXE_BASE_CODE_CALLBACK_PROTOCOL },
	{ .efi_guid_name = "serial io", .efi_guid = SERIAL_IO_PROTOCOL },
	{ .efi_guid_name = "loaded image", .efi_guid = LOADED_IMAGE_PROTOCOL },
	{ .efi_guid_name = "loaded image device path",
	    .efi_guid = EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID },
	{ .efi_guid_name = "ISA io", .efi_guid = EFI_ISA_IO_PROTOCOL_GUID },
	{ .efi_guid_name = "IDE controller init",
	    .efi_guid = EFI_IDE_CONTROLLER_INIT_PROTOCOL_GUID },
	{ .efi_guid_name = "ISA ACPI", .efi_guid = EFI_ISA_ACPI_PROTOCOL_GUID },
	{ .efi_guid_name = "PCI", .efi_guid = EFI_PCI_IO_PROTOCOL_GUID },
	{ .efi_guid_name = "PCI root", .efi_guid = EFI_PCI_ROOT_IO_GUID },
	{ .efi_guid_name = "PCI enumeration",
	    .efi_guid = EFI_PCI_ENUMERATION_COMPLETE_GUID },
        { .efi_guid_name = "Driver diagnostics",
	    .efi_guid = EFI_DRIVER_DIAGNOSTICS_PROTOCOL_GUID },
        { .efi_guid_name = "Driver diagnostics2",
	    .efi_guid = EFI_DRIVER_DIAGNOSTICS2_PROTOCOL_GUID },
        { .efi_guid_name = "simple pointer",
	    .efi_guid = EFI_SIMPLE_POINTER_PROTOCOL_GUID },
        { .efi_guid_name = "absolute pointer",
	    .efi_guid = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID },
        { .efi_guid_name = "VLAN config",
	    .efi_guid = EFI_VLAN_CONFIG_PROTOCOL_GUID },
        { .efi_guid_name = "ARP service binding",
	    .efi_guid = EFI_ARP_SERVICE_BINDING_PROTOCOL_GUID },
        { .efi_guid_name = "ARP", .efi_guid = EFI_ARP_PROTOCOL_GUID },
        { .efi_guid_name = "IPv4 service binding",
	    .efi_guid = EFI_IP4_SERVICE_BINDING_PROTOCOL },
        { .efi_guid_name = "IPv4", .efi_guid = EFI_IP4_PROTOCOL },
        { .efi_guid_name = "IPv4 config",
	    .efi_guid = EFI_IP4_CONFIG_PROTOCOL_GUID },
        { .efi_guid_name = "IPv6 service binding",
	    .efi_guid = EFI_IP6_SERVICE_BINDING_PROTOCOL },
        { .efi_guid_name = "IPv6", .efi_guid = EFI_IP6_PROTOCOL },
        { .efi_guid_name = "IPv6 config",
	    .efi_guid = EFI_IP6_CONFIG_PROTOCOL_GUID },
        { .efi_guid_name = "UDPv4", .efi_guid = EFI_UDP4_PROTOCOL },
        { .efi_guid_name = "UDPv4 service binding",
	    .efi_guid = EFI_UDP4_SERVICE_BINDING_PROTOCOL },
        { .efi_guid_name = "UDPv6", .efi_guid = EFI_UDP6_PROTOCOL },
        { .efi_guid_name = "UDPv6 service binding",
	    .efi_guid = EFI_UDP6_SERVICE_BINDING_PROTOCOL },
        { .efi_guid_name = "TCPv4", .efi_guid = EFI_TCP4_PROTOCOL },
        { .efi_guid_name = "TCPv4 service binding",
	    .efi_guid = EFI_TCP4_SERVICE_BINDING_PROTOCOL },
        { .efi_guid_name = "TCPv6", .efi_guid = EFI_TCP6_PROTOCOL },
        { .efi_guid_name = "TCPv6 service binding",
	    .efi_guid = EFI_TCP6_SERVICE_BINDING_PROTOCOL },
        { .efi_guid_name = "EFI System partition",
	    .efi_guid = EFI_PART_TYPE_EFI_SYSTEM_PART_GUID },
        { .efi_guid_name = "MBR legacy",
	    .efi_guid = EFI_PART_TYPE_LEGACY_MBR_GUID },
        { .efi_guid_name = "device tree", .efi_guid = EFI_DEVICE_TREE_GUID },
        { .efi_guid_name = "USB io", .efi_guid = EFI_USB_IO_PROTOCOL_GUID },
        { .efi_guid_name = "USB2 HC", .efi_guid = EFI_USB2_HC_PROTOCOL_GUID },
        { .efi_guid_name = "component name",
	    .efi_guid = EFI_COMPONENT_NAME_PROTOCOL_GUID },
        { .efi_guid_name = "component name2",
	    .efi_guid = EFI_COMPONENT_NAME2_PROTOCOL_GUID },
        { .efi_guid_name = "driver binding",
	    .efi_guid = EFI_DRIVER_BINDING_PROTOCOL_GUID },
        { .efi_guid_name = "driver configuration",
	    .efi_guid = EFI_DRIVER_CONFIGURATION_PROTOCOL_GUID },
        { .efi_guid_name = "driver configuration2",
	    .efi_guid = EFI_DRIVER_CONFIGURATION2_PROTOCOL_GUID },
        { .efi_guid_name = "decompress",
	    .efi_guid = EFI_DECOMPRESS_PROTOCOL_GUID },
        { .efi_guid_name = "ebc interpreter",
	    .efi_guid = EFI_EBC_INTERPRETER_PROTOCOL_GUID },
        { .efi_guid_name = "network interface identifier",
	    .efi_guid = EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL },
        { .efi_guid_name = "network interface identifier_31",
	    .efi_guid = EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_31 },
        { .efi_guid_name = "managed network service binding",
	    .efi_guid = EFI_MANAGED_NETWORK_SERVICE_BINDING_PROTOCOL_GUID },
        { .efi_guid_name = "managed network",
	    .efi_guid = EFI_MANAGED_NETWORK_PROTOCOL_GUID },
        { .efi_guid_name = "form browser",
	    .efi_guid = EFI_FORM_BROWSER2_PROTOCOL_GUID },
        { .efi_guid_name = "HII config routing",
	    .efi_guid = EFI_HII_CONFIG_ROUTING_PROTOCOL_GUID },
        { .efi_guid_name = "HII database",
	    .efi_guid = EFI_HII_DATABASE_PROTOCOL_GUID },
        { .efi_guid_name = "HII string",
	    .efi_guid = EFI_HII_STRING_PROTOCOL_GUID },
        { .efi_guid_name = "HII image",
	    .efi_guid = EFI_HII_IMAGE_PROTOCOL_GUID },
        { .efi_guid_name = "HII font", .efi_guid = EFI_HII_FONT_PROTOCOL_GUID },
        { .efi_guid_name = "HII config",
	    .efi_guid = EFI_HII_CONFIGURATION_ACCESS_PROTOCOL_GUID },
        { .efi_guid_name = "MTFTP4 service binding",
	    .efi_guid = EFI_MTFTP4_SERVICE_BINDING_PROTOCOL_GUID },
        { .efi_guid_name = "MTFTP4", .efi_guid = EFI_MTFTP4_PROTOCOL_GUID },
        { .efi_guid_name = "MTFTP6 service binding",
	    .efi_guid = EFI_MTFTP6_SERVICE_BINDING_PROTOCOL_GUID },
        { .efi_guid_name = "MTFTP6", .efi_guid = EFI_MTFTP6_PROTOCOL_GUID },
        { .efi_guid_name = "DHCP4 service binding",
	    .efi_guid = EFI_DHCP4_SERVICE_BINDING_PROTOCOL_GUID },
        { .efi_guid_name = "DHCP4", .efi_guid = EFI_DHCP4_PROTOCOL_GUID },
        { .efi_guid_name = "DHCP6 service binding",
	    .efi_guid = EFI_DHCP6_SERVICE_BINDING_PROTOCOL_GUID },
        { .efi_guid_name = "DHCP6", .efi_guid = EFI_DHCP6_PROTOCOL_GUID },
        { .efi_guid_name = "SCSI io", .efi_guid = EFI_SCSI_IO_PROTOCOL_GUID },
        { .efi_guid_name = "SCSI pass thru",
	    .efi_guid = EFI_SCSI_PASS_THRU_PROTOCOL_GUID },
        { .efi_guid_name = "SCSI pass thru ext",
	    .efi_guid = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID },
        { .efi_guid_name = "Capsule arch",
	    .efi_guid = EFI_CAPSULE_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "monotonic counter arch",
	    .efi_guid = EFI_MONOTONIC_COUNTER_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "realtime clock arch",
	    .efi_guid = EFI_REALTIME_CLOCK_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "variable arch",
	    .efi_guid = EFI_VARIABLE_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "variable write arch",
	    .efi_guid = EFI_VARIABLE_WRITE_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "watchdog timer arch",
	    .efi_guid = EFI_WATCHDOG_TIMER_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "ACPI support",
	    .efi_guid = EFI_ACPI_SUPPORT_PROTOCOL_GUID },
        { .efi_guid_name = "BDS arch", .efi_guid = EFI_BDS_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "metronome arch",
	    .efi_guid = EFI_METRONOME_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "timer arch",
	    .efi_guid = EFI_TIMER_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "DPC", .efi_guid = EFI_DPC_PROTOCOL_GUID },
        { .efi_guid_name = "print2", .efi_guid = EFI_PRINT2_PROTOCOL_GUID },
        { .efi_guid_name = "device path to text",
	    .efi_guid = EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID },
        { .efi_guid_name = "reset arch",
	    .efi_guid = EFI_RESET_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "CPU arch", .efi_guid = EFI_CPU_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "CPU IO2", .efi_guid = EFI_CPU_IO2_PROTOCOL_GUID },
        { .efi_guid_name = "Legacy 8259",
	    .efi_guid = EFI_LEGACY_8259_PROTOCOL_GUID },
        { .efi_guid_name = "Security arch",
	    .efi_guid = EFI_SECURITY_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "Security2 arch",
	    .efi_guid = EFI_SECURITY2_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "Runtime arch",
	    .efi_guid = EFI_RUNTIME_ARCH_PROTOCOL_GUID },
        { .efi_guid_name = "status code runtime",
	    .efi_guid = EFI_STATUS_CODE_RUNTIME_PROTOCOL_GUID },
        { .efi_guid_name = "data hub", .efi_guid = EFI_DATA_HUB_PROTOCOL_GUID },
        { .efi_guid_name = "PCD", .efi_guid = PCD_PROTOCOL_GUID },
        { .efi_guid_name = "EFI PCD", .efi_guid = EFI_PCD_PROTOCOL_GUID },
        { .efi_guid_name = "firmware volume block",
	    .efi_guid = EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL_GUID },
        { .efi_guid_name = "firmware volume2",
	    .efi_guid = EFI_FIRMWARE_VOLUME2_PROTOCOL_GUID },
        { .efi_guid_name = "firmware volume dispatch",
	    .efi_guid = EFI_FIRMWARE_VOLUME_DISPATCH_PROTOCOL_GUID },
        { .efi_guid_name = "lzma compress", .efi_guid = LZMA_COMPRESS_GUID },
        { .efi_guid_name = "MP services",
	    .efi_guid = EFI_MP_SERVICES_PROTOCOL_GUID },
        { .efi_guid_name = MTC_VARIABLE_NAME, .efi_guid = MTC_VENDOR_GUID },
        { .efi_guid_name = "RTC", .efi_guid = { 0x378D7B65, 0x8DA9, 0x4773,
	    { 0xB6, 0xE4, 0xA4, 0x78, 0x26, 0xA8, 0x33, 0xE1} } },
        { .efi_guid_name = "Active EDID",
	    .efi_guid = EFI_EDID_ACTIVE_PROTOCOL_GUID },
        { .efi_guid_name = "Discovered EDID",
	    .efi_guid = EFI_EDID_DISCOVERED_PROTOCOL_GUID }
};

bool
efi_guid_to_str(const EFI_GUID *guid, char **sp)
{
	uint32_t status;

	uuid_to_string((const uuid_t *)guid, sp, &status);
	return (status == uuid_s_ok ? true : false);
}

bool
efi_str_to_guid(const char *s, EFI_GUID *guid)
{
	uint32_t status;

	uuid_from_string(s, (uuid_t *)guid, &status);
	return (status == uuid_s_ok ? true : false);
}

bool
efi_name_to_guid(const char *name, EFI_GUID *guid)
{
	uint32_t i;

	for (i = 0; i < nitems(efi_uuid_mapping); i++) {
		if (strcasecmp(name, efi_uuid_mapping[i].efi_guid_name) == 0) {
			*guid = efi_uuid_mapping[i].efi_guid;
			return (true);
		}
	}
	return (efi_str_to_guid(name, guid));
}

bool
efi_guid_to_name(EFI_GUID *guid, char **name)
{
	uint32_t i;
	int rv;

	for (i = 0; i < nitems(efi_uuid_mapping); i++) {
		rv = uuid_equal((uuid_t *)guid,
		    (uuid_t *)&efi_uuid_mapping[i].efi_guid, NULL);
		if (rv != 0) {
			*name = strdup(efi_uuid_mapping[i].efi_guid_name);
			if (*name == NULL)
				return (false);
			return (true);
		}
	}
	return (efi_guid_to_str(guid, name));
}

void
efi_init_environment(void)
{
	char var[128];

	snprintf(var, sizeof(var), "%d.%02d", ST->Hdr.Revision >> 16,
	    ST->Hdr.Revision & 0xffff);
	env_setenv("efi-version", EV_VOLATILE, var, env_noset, env_nounset);
}

COMMAND_SET(efishow, "efi-show", "print some or all EFI variables", command_efi_show);

static int
efi_print_other_value(uint8_t *data, UINTN datasz)
{
	UINTN i;
	bool is_ascii = true;

	printf(" = ");
	for (i = 0; i < datasz - 1; i++) {
		/*
		 * Quick hack to see if this ascii-ish string is printable
		 * range plus tab, cr and lf.
		 */
		if ((data[i] < 32 || data[i] > 126)
		    && data[i] != 9 && data[i] != 10 && data[i] != 13) {
			is_ascii = false;
			break;
		}
	}
	if (data[datasz - 1] != '\0')
		is_ascii = false;
	if (is_ascii == true) {
		printf("%s", data);
		if (pager_output("\n"))
			return (CMD_WARN);
	} else {
		if (pager_output("\n"))
			return (CMD_WARN);
		/*
		 * Dump hex bytes grouped by 4.
		 */
		for (i = 0; i < datasz; i++) {
			printf("%02x ", data[i]);
			if ((i + 1) % 4 == 0)
				printf(" ");
			if ((i + 1) % 20 == 0) {
				if (pager_output("\n"))
					return (CMD_WARN);
			}
		}
		if (pager_output("\n"))
			return (CMD_WARN);
	}

	return (CMD_OK);
}

/* This appears to be some sort of UEFI shell alias table. */
static int
efi_print_shell_str(const CHAR16 *varnamearg __unused, uint8_t *data,
    UINTN datasz __unused)
{
	printf(" = %S", (CHAR16 *)data);
	if (pager_output("\n"))
		return (CMD_WARN);
	return (CMD_OK);
}

const char *
efi_memory_type(EFI_MEMORY_TYPE type)
{
	const char *types[] = {
	    "Reserved",
	    "LoaderCode",
	    "LoaderData",
	    "BootServicesCode",
	    "BootServicesData",
	    "RuntimeServicesCode",
	    "RuntimeServicesData",
	    "ConventionalMemory",
	    "UnusableMemory",
	    "ACPIReclaimMemory",
	    "ACPIMemoryNVS",
	    "MemoryMappedIO",
	    "MemoryMappedIOPortSpace",
	    "PalCode",
	    "PersistentMemory"
	};

	switch (type) {
	case EfiReservedMemoryType:
	case EfiLoaderCode:
	case EfiLoaderData:
	case EfiBootServicesCode:
	case EfiBootServicesData:
	case EfiRuntimeServicesCode:
	case EfiRuntimeServicesData:
	case EfiConventionalMemory:
	case EfiUnusableMemory:
	case EfiACPIReclaimMemory:
	case EfiACPIMemoryNVS:
	case EfiMemoryMappedIO:
	case EfiMemoryMappedIOPortSpace:
	case EfiPalCode:
	case EfiPersistentMemory:
		return (types[type]);
	default:
		return ("Unknown");
	}
}

/* Print memory type table. */
static int
efi_print_mem_type(const CHAR16 *varnamearg __unused, uint8_t *data,
    UINTN datasz)
{
	int i, n;
	EFI_MEMORY_TYPE_INFORMATION *ti;

	ti = (EFI_MEMORY_TYPE_INFORMATION *)data;
	if (pager_output(" = \n"))
		return (CMD_WARN);

	n = datasz / sizeof (EFI_MEMORY_TYPE_INFORMATION);
	for (i = 0; i < n && ti[i].NumberOfPages != 0; i++) {
		printf("\t%23s pages: %u", efi_memory_type(ti[i].Type),
		    ti[i].NumberOfPages);
		if (pager_output("\n"))
			return (CMD_WARN);
	}

	return (CMD_OK);
}

/*
 * Print FreeBSD variables.
 * We have LoaderPath and LoaderDev as CHAR16 strings.
 */
static int
efi_print_freebsd(const CHAR16 *varnamearg, uint8_t *data,
    UINTN datasz __unused)
{
	int rv = -1;
	char *var = NULL;

	if (ucs2_to_utf8(varnamearg, &var) != 0)
		return (CMD_ERROR);

	if (strcmp("LoaderPath", var) == 0 ||
	    strcmp("LoaderDev", var) == 0) {
		printf(" = ");
		printf("%S", (CHAR16 *)data);

		if (pager_output("\n"))
			rv = CMD_WARN;
		else
			rv = CMD_OK;
	}

	free(var);
	return (rv);
}

/* Print global variables. */
static int
efi_print_global(const CHAR16 *varnamearg, uint8_t *data, UINTN datasz)
{
	int rv = -1;
	char *var = NULL;

	if (ucs2_to_utf8(varnamearg, &var) != 0)
		return (CMD_ERROR);

	if (strcmp("AuditMode", var) == 0) {
		printf(" = ");
		printf("0x%x", *data);	/* 8-bit int */
		goto done;
	}

	if (strcmp("BootOptionSupport", var) == 0) {
		printf(" = ");
		printf("0x%x", *((uint32_t *)data));	/* UINT32 */
		goto done;
	}

	if (strcmp("BootCurrent", var) == 0 ||
	    strcmp("BootNext", var) == 0 ||
	    strcmp("Timeout", var) == 0) {
		printf(" = ");
		printf("%u", *((uint16_t *)data));	/* UINT16 */
		goto done;
	}

	if (strcmp("BootOrder", var) == 0 ||
	    strcmp("DriverOrder", var) == 0) {
		UINTN i;
		UINT16 *u16 = (UINT16 *)data;

		printf(" =");
		for (i = 0; i < datasz / sizeof (UINT16); i++)
			printf(" %u", u16[i]);
		goto done;
	}
	if (strncmp("Boot", var, 4) == 0 ||
	    strncmp("Driver", var, 5) == 0 ||
	    strncmp("SysPrep", var, 7) == 0 ||
	    strncmp("OsRecovery", var, 10) == 0) {
		UINT16 filepathlistlen;
		CHAR16 *text;
		int desclen;
		EFI_DEVICE_PATH *dp;

		data += sizeof(UINT32);
		filepathlistlen = *(uint16_t *)data;
		data += sizeof (UINT16);
		text = (CHAR16 *)data;

		for (desclen = 0; text[desclen] != 0; desclen++)
			;
		if (desclen != 0) {
			/* Add terminating zero and we have CHAR16. */
			desclen = (desclen + 1) * 2;
		}

		printf(" = ");
		printf("%S", text);
		if (filepathlistlen != 0) {
			/* Output pathname from new line. */
			if (pager_output("\n")) {
				rv = CMD_WARN;
				goto done;
			}
			dp = malloc(filepathlistlen);
			if (dp == NULL)
				goto done;

			memcpy(dp, data + desclen, filepathlistlen);
			text = efi_devpath_name(dp);
			if (text != NULL) {
				printf("\t%S", text);
				efi_free_devpath_name(text);
			}
			free(dp);
		}
		goto done;
	}

	if (strcmp("ConIn", var) == 0 ||
	    strcmp("ConInDev", var) == 0 ||
	    strcmp("ConOut", var) == 0 ||
	    strcmp("ConOutDev", var) == 0 ||
	    strcmp("ErrOut", var) == 0 ||
	    strcmp("ErrOutDev", var) == 0) {
		CHAR16 *text;

		printf(" = ");
		text = efi_devpath_name((EFI_DEVICE_PATH *)data);
		if (text != NULL) {
			printf("%S", text);
			efi_free_devpath_name(text);
		}
		goto done;
	}

	if (strcmp("PlatformLang", var) == 0 ||
	    strcmp("PlatformLangCodes", var) == 0 ||
	    strcmp("LangCodes", var) == 0 ||
	    strcmp("Lang", var) == 0) {
		printf(" = ");
		printf("%s", data);	/* ASCII string */
		goto done;
	}

	/*
	 * Feature bitmap from firmware to OS.
	 * Older UEFI provides UINT32, newer UINT64.
	 */
	if (strcmp("OsIndicationsSupported", var) == 0) {
		printf(" = ");
		if (datasz == 4)
			printf("0x%x", *((uint32_t *)data));
		else
			printf("0x%jx", *((uint64_t *)data));
		goto done;
	}

	/* Fallback for anything else. */
	rv = efi_print_other_value(data, datasz);
done:
	if (rv == -1) {
		if (pager_output("\n"))
			rv = CMD_WARN;
		else
			rv = CMD_OK;
	}
	free(var);
	return (rv);
}

static void
efi_print_var_attr(UINT32 attr)
{
	bool comma = false;

	if (attr & EFI_VARIABLE_NON_VOLATILE) {
		printf("NV");
		comma = true;
	}
	if (attr & EFI_VARIABLE_BOOTSERVICE_ACCESS) {
		if (comma == true)
			printf(",");
		printf("BS");
		comma = true;
	}
	if (attr & EFI_VARIABLE_RUNTIME_ACCESS) {
		if (comma == true)
			printf(",");
		printf("RS");
		comma = true;
	}
	if (attr & EFI_VARIABLE_HARDWARE_ERROR_RECORD) {
		if (comma == true)
			printf(",");
		printf("HR");
		comma = true;
	}
	if (attr & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) {
		if (comma == true)
			printf(",");
		printf("AT");
		comma = true;
	}
}

static int
efi_print_var(CHAR16 *varnamearg, EFI_GUID *matchguid, int lflag)
{
	UINTN		datasz;
	EFI_STATUS	status;
	UINT32		attr;
	char		*str;
	uint8_t		*data;
	int		rv = CMD_OK;

	str = NULL;
	datasz = 0;
	status = RS->GetVariable(varnamearg, matchguid, &attr, &datasz, NULL);
	if (status != EFI_BUFFER_TOO_SMALL) {
		printf("Can't get the variable: error %#lx\n",
		    EFI_ERROR_CODE(status));
		return (CMD_ERROR);
	}
	data = malloc(datasz);
	if (data == NULL) {
		printf("Out of memory\n");
		return (CMD_ERROR);
	}

	status = RS->GetVariable(varnamearg, matchguid, &attr, &datasz, data);
	if (status != EFI_SUCCESS) {
		printf("Can't get the variable: error %#lx\n",
		    EFI_ERROR_CODE(status));
		free(data);
		return (CMD_ERROR);
	}

	if (efi_guid_to_name(matchguid, &str) == false) {
		rv = CMD_ERROR;
		goto done;
	}
	printf("%s ", str);
	efi_print_var_attr(attr);
	printf(" %S", varnamearg);

	if (lflag == 0) {
		if (strcmp(str, "global") == 0)
			rv = efi_print_global(varnamearg, data, datasz);
		else if (strcmp(str, "freebsd") == 0)
			rv = efi_print_freebsd(varnamearg, data, datasz);
		else if (strcmp(str,
		    EFI_MEMORY_TYPE_INFORMATION_VARIABLE_NAME) == 0)
			rv = efi_print_mem_type(varnamearg, data, datasz);
		else if (strcmp(str,
		    "47c7b227-c42a-11d2-8e57-00a0c969723b") == 0)
			rv = efi_print_shell_str(varnamearg, data, datasz);
		else if (strcmp(str, MTC_VARIABLE_NAME) == 0) {
			printf(" = ");
			printf("%u", *((uint32_t *)data));	/* UINT32 */
			rv = CMD_OK;
			if (pager_output("\n"))
				rv = CMD_WARN;
		} else
			rv = efi_print_other_value(data, datasz);
	} else if (pager_output("\n"))
		rv =  CMD_WARN;

done:
	free(str);
	free(data);
	return (rv);
}

static int
command_efi_show(int argc, char *argv[])
{
	/*
	 * efi-show [-a]
	 *	print all the env
	 * efi-show -g UUID
	 *	print all the env vars tagged with UUID
	 * efi-show -v var
	 *	search all the env vars and print the ones matching var
	 * efi-show -g UUID -v var
	 * efi-show UUID var
	 *	print all the env vars that match UUID and var
	 */
	/* NB: We assume EFI_GUID is the same as uuid_t */
	int		aflag = 0, gflag = 0, lflag = 0, vflag = 0;
	int		ch, rv;
	unsigned	i;
	EFI_STATUS	status;
	EFI_GUID	varguid = ZERO_GUID;
	EFI_GUID	matchguid = ZERO_GUID;
	CHAR16		*varname;
	CHAR16		*newnm;
	CHAR16		varnamearg[128];
	UINTN		varalloc;
	UINTN		varsz;

	optind = 1;
	optreset = 1;
	opterr = 1;

	while ((ch = getopt(argc, argv, "ag:lv:")) != -1) {
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'g':
			gflag = 1;
			if (efi_name_to_guid(optarg, &matchguid) == false) {
				printf("uuid %s could not be parsed\n", optarg);
				return (CMD_ERROR);
			}
			break;
		case 'l':
			lflag = 1;
			break;
		case 'v':
			vflag = 1;
			if (strlen(optarg) >= nitems(varnamearg)) {
				printf("Variable %s is longer than %zu "
				    "characters\n", optarg, nitems(varnamearg));
				return (CMD_ERROR);
			}
			cpy8to16(optarg, varnamearg, nitems(varnamearg));
			break;
		default:
			return (CMD_ERROR);
		}
	}

	if (argc == 1)		/* default is -a */
		aflag = 1;

	if (aflag && (gflag || vflag)) {
		printf("-a isn't compatible with -g or -v\n");
		return (CMD_ERROR);
	}

	if (aflag && optind < argc) {
		printf("-a doesn't take any args\n");
		return (CMD_ERROR);
	}

	argc -= optind;
	argv += optind;

	pager_open();
	if (vflag && gflag) {
		rv = efi_print_var(varnamearg, &matchguid, lflag);
		if (rv == CMD_WARN)
			rv = CMD_OK;
		pager_close();
		return (rv);
	}

	if (argc == 2) {
		optarg = argv[0];
		if (strlen(optarg) >= nitems(varnamearg)) {
			printf("Variable %s is longer than %zu characters\n",
			    optarg, nitems(varnamearg));
			pager_close();
			return (CMD_ERROR);
		}
		for (i = 0; i < strlen(optarg); i++)
			varnamearg[i] = optarg[i];
		varnamearg[i] = 0;
		optarg = argv[1];
		if (efi_name_to_guid(optarg, &matchguid) == false) {
			printf("uuid %s could not be parsed\n", optarg);
			pager_close();
			return (CMD_ERROR);
		}
		rv = efi_print_var(varnamearg, &matchguid, lflag);
		if (rv == CMD_WARN)
			rv = CMD_OK;
		pager_close();
		return (rv);
	}

	if (argc > 0) {
		printf("Too many args: %d\n", argc);
		pager_close();
		return (CMD_ERROR);
	}

	/*
	 * Initiate the search -- note the standard takes pain
	 * to specify the initial call must be a poiner to a NULL
	 * character.
	 */
	varalloc = 1024;
	varname = malloc(varalloc);
	if (varname == NULL) {
		printf("Can't allocate memory to get variables\n");
		pager_close();
		return (CMD_ERROR);
	}
	varname[0] = 0;
	while (1) {
		varsz = varalloc;
		status = RS->GetNextVariableName(&varsz, varname, &varguid);
		if (status == EFI_BUFFER_TOO_SMALL) {
			varalloc = varsz;
			newnm = realloc(varname, varalloc);
			if (newnm == NULL) {
				printf("Can't allocate memory to get "
				    "variables\n");
				rv = CMD_ERROR;
				break;
			}
			varname = newnm;
			continue; /* Try again with bigger buffer */
		}
		if (status == EFI_NOT_FOUND) {
			rv = CMD_OK;
			break;
		}
		if (status != EFI_SUCCESS) {
			rv = CMD_ERROR;
			break;
		}

		if (aflag) {
			rv = efi_print_var(varname, &varguid, lflag);
			if (rv != CMD_OK) {
				if (rv == CMD_WARN)
					rv = CMD_OK;
				break;
			}
			continue;
		}
		if (vflag) {
			if (wcscmp(varnamearg, varname) == 0) {
				rv = efi_print_var(varname, &varguid, lflag);
				if (rv != CMD_OK) {
					if (rv == CMD_WARN)
						rv = CMD_OK;
					break;
				}
				continue;
			}
		}
		if (gflag) {
			rv = uuid_equal((uuid_t *)&varguid,
			    (uuid_t *)&matchguid, NULL);
			if (rv != 0) {
				rv = efi_print_var(varname, &varguid, lflag);
				if (rv != CMD_OK) {
					if (rv == CMD_WARN)
						rv = CMD_OK;
					break;
				}
				continue;
			}
		}
	}
	free(varname);
	pager_close();

	return (rv);
}

COMMAND_SET(efiset, "efi-set", "set EFI variables", command_efi_set);

static int
command_efi_set(int argc, char *argv[])
{
	char *uuid, *var, *val;
	CHAR16 wvar[128];
	EFI_GUID guid;
#if defined(ENABLE_UPDATES)
	EFI_STATUS err;
#endif

	if (argc != 4) {
		printf("efi-set uuid var new-value\n");
		return (CMD_ERROR);
	}
	uuid = argv[1];
	var = argv[2];
	val = argv[3];
	if (efi_name_to_guid(uuid, &guid) == false) {
		printf("Invalid uuid %s\n", uuid);
		return (CMD_ERROR);
	}
	cpy8to16(var, wvar, nitems(wvar));
#if defined(ENABLE_UPDATES)
	err = RS->SetVariable(wvar, &guid, EFI_VARIABLE_NON_VOLATILE |
	    EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS,
	    strlen(val) + 1, val);
	if (EFI_ERROR(err)) {
		printf("Failed to set variable: error %lu\n",
		    EFI_ERROR_CODE(err));
		return (CMD_ERROR);
	}
#else
	printf("would set %s %s = %s\n", uuid, var, val);
#endif
	return (CMD_OK);
}

COMMAND_SET(efiunset, "efi-unset", "delete / unset EFI variables", command_efi_unset);

static int
command_efi_unset(int argc, char *argv[])
{
	char *uuid, *var;
	CHAR16 wvar[128];
	EFI_GUID guid;
#if defined(ENABLE_UPDATES)
	EFI_STATUS err;
#endif

	if (argc != 3) {
		printf("efi-unset uuid var\n");
		return (CMD_ERROR);
	}
	uuid = argv[1];
	var = argv[2];
	if (efi_name_to_guid(uuid, &guid) == false) {
		printf("Invalid uuid %s\n", uuid);
		return (CMD_ERROR);
	}
	cpy8to16(var, wvar, nitems(wvar));
#if defined(ENABLE_UPDATES)
	err = RS->SetVariable(wvar, &guid, 0, 0, NULL);
	if (EFI_ERROR(err)) {
		printf("Failed to unset variable: error %lu\n",
		    EFI_ERROR_CODE(err));
		return (CMD_ERROR);
	}
#else
	printf("would unset %s %s \n", uuid, var);
#endif
	return (CMD_OK);
}
