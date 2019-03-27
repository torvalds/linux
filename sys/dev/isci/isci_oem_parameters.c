/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/isci/isci.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/vmparam.h>
#include <machine/pc/bios.h>
#include <dev/isci/scil/scu_bios_definitions.h>

struct pcir_header
{
	uint32_t	signature;
	uint16_t	vendor_id;
	uint16_t	device_id;
	uint16_t	reserved;
	uint16_t	struct_length;
	uint8_t		struct_revision;
	uint8_t		cc_interface;
	uint8_t		cc_subclass;
	uint8_t		cc_baseclass;
	uint16_t	image_length;
	uint16_t	code_revision;
	uint8_t		code_type;
	uint8_t		indicator;
	uint16_t	reserved1;
};

struct rom_header
{
	uint8_t		signature_byte[2];
	uint8_t		rom_length;
	uint8_t		jmp_code;
	uint16_t	entry_address;
	uint8_t		reserved[0x12];
	uint16_t	pcir_pointer;
	uint16_t	pnp_pointer;
};

struct oem_parameters_table
{
	uint8_t		signature[4];  /* "$OEM" */
	struct revision
	{
		uint16_t	major:8;  /* bits [7:0] */
		uint16_t	minor:8;  /* bits [8:15] */
	} revision;

	uint16_t	length;
	uint8_t		checksum;
	uint8_t		reserved1;
	uint16_t	reserved2;
	uint8_t		data[1];
};

void
isci_get_oem_parameters(struct isci_softc *isci)
{
	uint32_t OROM_PHYSICAL_ADDRESS_START = 0xC0000;
	uint32_t OROM_SEARCH_LENGTH = 0x30000;
	uint16_t OROM_SIGNATURE = 0xAA55;
	uint32_t OROM_SIZE = 512;
	uint8_t *orom_start =
	    (uint8_t *)BIOS_PADDRTOVADDR(OROM_PHYSICAL_ADDRESS_START);
	uint32_t offset = 0;

	while (offset < OROM_SEARCH_LENGTH) {

		/* Look for the OROM signature at the beginning of every
		 *  512-byte block in the OROM region
		 */
		if (*(uint16_t*)(orom_start + offset) == OROM_SIGNATURE) {
			uint32_t *rom;
			struct rom_header *rom_header;
			struct pcir_header *pcir_header;
			uint16_t vendor_id = isci->pci_common_header.vendor_id;
			uint16_t device_id = isci->pci_common_header.device_id;

			rom = (uint32_t *)(orom_start + offset);
			rom_header = (struct rom_header *)rom;
			pcir_header = (struct pcir_header *)
			    ((uint8_t*)rom + rom_header->pcir_pointer);

			/* OROM signature was found.  Now check if the PCI
			 *  device and vendor IDs match.
			 */
			if (pcir_header->vendor_id == vendor_id &&
			    pcir_header->device_id == device_id)
			{
				/* OROM for this PCI device was found.  Search
				 *  this 512-byte block for the $OEM string,
				 *  which will mark the beginning of the OEM
				 *  parameter block.
				 */
				uint8_t oem_sig[4] = {'$', 'O', 'E', 'M'};
				int dword_index;

				for (dword_index = 0;
				    dword_index < OROM_SIZE/sizeof(uint32_t);
				    dword_index++)
					if (rom[dword_index] == *(uint32_t *)oem_sig) {
						/* $OEM signature string was found.  Now copy the OEM parameter block
						 *  into the struct ISCI_CONTROLLER objects.  After the controllers are
						 *  constructed, we will pass this OEM parameter data to the SCI core
						 *  controller.
						 */
						struct oem_parameters_table *oem =
							(struct oem_parameters_table *)&rom[dword_index];
						SCI_BIOS_OEM_PARAM_BLOCK_T *oem_data =
							(SCI_BIOS_OEM_PARAM_BLOCK_T *)oem->data;
						int index;

						isci->oem_parameters_found = TRUE;
						isci_log_message(1, "ISCI", "oem_data->header.num_elements = %d\n",
						    oem_data->header.num_elements);

						for (index = 0; index < oem_data->header.num_elements; index++)
						{
							memcpy(&isci->controllers[index].oem_parameters.sds1,
							       &oem_data->controller_element[index],
							       sizeof(SCIC_SDS_OEM_PARAMETERS_T));

							isci_log_message(1, "ISCI", "OEM Parameter Data for controller %d\n",
							    index);

							for (int i = 0; i < sizeof(SCIC_SDS_OEM_PARAMETERS_T); i++) {
								uint8_t val = ((uint8_t *)&oem_data->controller_element[index])[i];
								isci_log_message(1, "ISCI", "%02x ", val);
							}
							isci_log_message(1, "ISCI", "\n");
							isci->controllers[index].oem_parameters_version = oem_data->header.version;
						}
					}

				/* No need to continue searching for another
				 *  OROM that matches this PCI device, so return
				 *  immediately.
				 */
				return;
			}
		}

		offset += OROM_SIZE;
	}
}
