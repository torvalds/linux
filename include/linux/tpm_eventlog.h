/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_TPM_EVENTLOG_H__
#define __LINUX_TPM_EVENTLOG_H__

#include <linux/tpm.h>

#define TCG_EVENT_NAME_LEN_MAX	255
#define MAX_TEXT_EVENT		1000	/* Max event string length */
#define ACPI_TCPA_SIG		"TCPA"	/* 0x41504354 /'TCPA' */

#define EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2 0x1
#define EFI_TCG2_EVENT_LOG_FORMAT_TCG_2   0x2

#ifdef CONFIG_PPC64
#define do_endian_conversion(x) be32_to_cpu(x)
#else
#define do_endian_conversion(x) x
#endif

enum bios_platform_class {
	BIOS_CLIENT = 0x00,
	BIOS_SERVER = 0x01,
};

struct tcpa_event {
	u32 pcr_index;
	u32 event_type;
	u8 pcr_value[20];	/* SHA1 */
	u32 event_size;
	u8 event_data[];
};

enum tcpa_event_types {
	PREBOOT = 0,
	POST_CODE,
	UNUSED,
	NO_ACTION,
	SEPARATOR,
	ACTION,
	EVENT_TAG,
	SCRTM_CONTENTS,
	SCRTM_VERSION,
	CPU_MICROCODE,
	PLATFORM_CONFIG_FLAGS,
	TABLE_OF_DEVICES,
	COMPACT_HASH,
	IPL,
	IPL_PARTITION_DATA,
	NONHOST_CODE,
	NONHOST_CONFIG,
	NONHOST_INFO,
};

struct tcpa_pc_event {
	u32 event_id;
	u32 event_size;
	u8 event_data[];
};

enum tcpa_pc_event_ids {
	SMBIOS = 1,
	BIS_CERT,
	POST_BIOS_ROM,
	ESCD,
	CMOS,
	NVRAM,
	OPTION_ROM_EXEC,
	OPTION_ROM_CONFIG,
	OPTION_ROM_MICROCODE = 10,
	S_CRTM_VERSION,
	S_CRTM_CONTENTS,
	POST_CONTENTS,
	HOST_TABLE_OF_DEVICES,
};

/* http://www.trustedcomputinggroup.org/tcg-efi-protocol-specification/ */

struct tcg_efi_specid_event_algs {
	u16 alg_id;
	u16 digest_size;
} __packed;

#define TCG_SPECID_SIG "Spec ID Event03"

struct tcg_efi_specid_event_head {
	u8 signature[16];
	u32 platform_class;
	u8 spec_version_minor;
	u8 spec_version_major;
	u8 spec_errata;
	u8 uintnsize;
	u32 num_algs;
	struct tcg_efi_specid_event_algs digest_sizes[];
} __packed;

struct tcg_pcr_event {
	u32 pcr_idx;
	u32 event_type;
	u8 digest[20];
	u32 event_size;
	u8 event[];
} __packed;

struct tcg_event_field {
	u32 event_size;
	u8 event[];
} __packed;

struct tcg_pcr_event2_head {
	u32 pcr_idx;
	u32 event_type;
	u32 count;
	struct tpm_digest digests[];
} __packed;

struct tcg_algorithm_size {
	u16 algorithm_id;
	u16 algorithm_size;
};

struct tcg_algorithm_info {
	u8 signature[16];
	u32 platform_class;
	u8 spec_version_minor;
	u8 spec_version_major;
	u8 spec_errata;
	u8 uintn_size;
	u32 number_of_algorithms;
	struct tcg_algorithm_size digest_sizes[];
};

#ifndef TPM_MEMREMAP
#define TPM_MEMREMAP(start, size) NULL
#endif

#ifndef TPM_MEMUNMAP
#define TPM_MEMUNMAP(start, size) do{} while(0)
#endif

/**
 * __calc_tpm2_event_size - calculate the size of a TPM2 event log entry
 * @event:        Pointer to the event whose size should be calculated
 * @event_header: Pointer to the initial event containing the digest lengths
 * @do_mapping:   Whether or not the event needs to be mapped
 *
 * The TPM2 event log format can contain multiple digests corresponding to
 * separate PCR banks, and also contains a variable length of the data that
 * was measured. This requires knowledge of how long each digest type is,
 * and this information is contained within the first event in the log.
 *
 * We calculate the length by examining the number of events, and then looking
 * at each event in turn to determine how much space is used for events in
 * total. Once we've done this we know the offset of the data length field,
 * and can calculate the total size of the event.
 *
 * Return: size of the event on success, 0 on failure
 */

static __always_inline int __calc_tpm2_event_size(struct tcg_pcr_event2_head *event,
					 struct tcg_pcr_event *event_header,
					 bool do_mapping)
{
	struct tcg_efi_specid_event_head *efispecid;
	struct tcg_event_field *event_field;
	void *mapping = NULL;
	int mapping_size;
	void *marker;
	void *marker_start;
	u32 halg_size;
	size_t size;
	u16 halg;
	int i;
	int j;
	u32 count, event_type;
	const u8 zero_digest[sizeof(event_header->digest)] = {0};

	marker = event;
	marker_start = marker;
	marker = marker + sizeof(event->pcr_idx) + sizeof(event->event_type)
		+ sizeof(event->count);

	/* Map the event header */
	if (do_mapping) {
		mapping_size = marker - marker_start;
		mapping = TPM_MEMREMAP((unsigned long)marker_start,
				       mapping_size);
		if (!mapping) {
			size = 0;
			goto out;
		}
	} else {
		mapping = marker_start;
	}

	event = (struct tcg_pcr_event2_head *)mapping;
	/*
	 * The loop below will unmap these fields if the log is larger than
	 * one page, so save them here for reference:
	 */
	count = event->count;
	event_type = event->event_type;

	/* Verify that it's the log header */
	if (event_header->pcr_idx != 0 ||
	    event_header->event_type != NO_ACTION ||
	    memcmp(event_header->digest, zero_digest, sizeof(zero_digest))) {
		size = 0;
		goto out;
	}

	efispecid = (struct tcg_efi_specid_event_head *)event_header->event;

	/*
	 * Perform validation of the event in order to identify malformed
	 * events. This function may be asked to parse arbitrary byte sequences
	 * immediately following a valid event log. The caller expects this
	 * function to recognize that the byte sequence is not a valid event
	 * and to return an event size of 0.
	 */
	if (memcmp(efispecid->signature, TCG_SPECID_SIG,
		   sizeof(TCG_SPECID_SIG)) ||
	    !efispecid->num_algs || count != efispecid->num_algs) {
		size = 0;
		goto out;
	}

	for (i = 0; i < count; i++) {
		halg_size = sizeof(event->digests[i].alg_id);

		/* Map the digest's algorithm identifier */
		if (do_mapping) {
			TPM_MEMUNMAP(mapping, mapping_size);
			mapping_size = halg_size;
			mapping = TPM_MEMREMAP((unsigned long)marker,
					     mapping_size);
			if (!mapping) {
				size = 0;
				goto out;
			}
		} else {
			mapping = marker;
		}

		memcpy(&halg, mapping, halg_size);
		marker = marker + halg_size;

		for (j = 0; j < efispecid->num_algs; j++) {
			if (halg == efispecid->digest_sizes[j].alg_id) {
				marker +=
					efispecid->digest_sizes[j].digest_size;
				break;
			}
		}
		/* Algorithm without known length. Such event is unparseable. */
		if (j == efispecid->num_algs) {
			size = 0;
			goto out;
		}
	}

	/*
	 * Map the event size - we don't read from the event itself, so
	 * we don't need to map it
	 */
	if (do_mapping) {
		TPM_MEMUNMAP(mapping, mapping_size);
		mapping_size += sizeof(event_field->event_size);
		mapping = TPM_MEMREMAP((unsigned long)marker,
				       mapping_size);
		if (!mapping) {
			size = 0;
			goto out;
		}
	} else {
		mapping = marker;
	}

	event_field = (struct tcg_event_field *)mapping;

	marker = marker + sizeof(event_field->event_size)
		+ event_field->event_size;
	size = marker - marker_start;

	if (event_type == 0 && event_field->event_size == 0)
		size = 0;

out:
	if (do_mapping)
		TPM_MEMUNMAP(mapping, mapping_size);
	return size;
}

#endif
