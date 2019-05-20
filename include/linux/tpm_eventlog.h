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
	u8 event_data[0];
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
	u8 event_data[0];
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
	u8 event[0];
} __packed;

struct tcg_event_field {
	u32 event_size;
	u8 event[0];
} __packed;

struct tcg_pcr_event2_head {
	u32 pcr_idx;
	u32 event_type;
	u32 count;
	struct tpm_digest digests[];
} __packed;

/**
 * __calc_tpm2_event_size - calculate the size of a TPM2 event log entry
 * @event:        Pointer to the event whose size should be calculated
 * @event_header: Pointer to the initial event containing the digest lengths
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
 * Return: size of the event on success, <0 on failure
 */

static inline int __calc_tpm2_event_size(struct tcg_pcr_event2_head *event,
					 struct tcg_pcr_event *event_header)
{
	struct tcg_efi_specid_event_head *efispecid;
	struct tcg_event_field *event_field;
	void *marker;
	void *marker_start;
	u32 halg_size;
	size_t size;
	u16 halg;
	int i;
	int j;

	marker = event;
	marker_start = marker;
	marker = marker + sizeof(event->pcr_idx) + sizeof(event->event_type)
		+ sizeof(event->count);

	efispecid = (struct tcg_efi_specid_event_head *)event_header->event;

	/* Check if event is malformed. */
	if (event->count > efispecid->num_algs)
		return 0;

	for (i = 0; i < event->count; i++) {
		halg_size = sizeof(event->digests[i].alg_id);
		memcpy(&halg, marker, halg_size);
		marker = marker + halg_size;
		for (j = 0; j < efispecid->num_algs; j++) {
			if (halg == efispecid->digest_sizes[j].alg_id) {
				marker +=
					efispecid->digest_sizes[j].digest_size;
				break;
			}
		}
		/* Algorithm without known length. Such event is unparseable. */
		if (j == efispecid->num_algs)
			return 0;
	}

	event_field = (struct tcg_event_field *)marker;
	marker = marker + sizeof(event_field->event_size)
		+ event_field->event_size;
	size = marker - marker_start;

	if ((event->event_type == 0) && (event_field->event_size == 0))
		return 0;

	return size;
}
#endif
