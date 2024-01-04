// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fw_tables.c - Parsing support for ACPI and ACPI-like tables provided by
 *                platform or device firmware
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2023 Intel Corp.
 */
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

enum acpi_subtable_type {
	ACPI_SUBTABLE_COMMON,
	ACPI_SUBTABLE_HMAT,
	ACPI_SUBTABLE_PRMT,
	ACPI_SUBTABLE_CEDT,
};

struct acpi_subtable_entry {
	union acpi_subtable_headers *hdr;
	enum acpi_subtable_type type;
};

static unsigned long __init_or_acpilib
acpi_get_entry_type(struct acpi_subtable_entry *entry)
{
	switch (entry->type) {
	case ACPI_SUBTABLE_COMMON:
		return entry->hdr->common.type;
	case ACPI_SUBTABLE_HMAT:
		return entry->hdr->hmat.type;
	case ACPI_SUBTABLE_PRMT:
		return 0;
	case ACPI_SUBTABLE_CEDT:
		return entry->hdr->cedt.type;
	}
	return 0;
}

static unsigned long __init_or_acpilib
acpi_get_entry_length(struct acpi_subtable_entry *entry)
{
	switch (entry->type) {
	case ACPI_SUBTABLE_COMMON:
		return entry->hdr->common.length;
	case ACPI_SUBTABLE_HMAT:
		return entry->hdr->hmat.length;
	case ACPI_SUBTABLE_PRMT:
		return entry->hdr->prmt.length;
	case ACPI_SUBTABLE_CEDT:
		return entry->hdr->cedt.length;
	}
	return 0;
}

static unsigned long __init_or_acpilib
acpi_get_subtable_header_length(struct acpi_subtable_entry *entry)
{
	switch (entry->type) {
	case ACPI_SUBTABLE_COMMON:
		return sizeof(entry->hdr->common);
	case ACPI_SUBTABLE_HMAT:
		return sizeof(entry->hdr->hmat);
	case ACPI_SUBTABLE_PRMT:
		return sizeof(entry->hdr->prmt);
	case ACPI_SUBTABLE_CEDT:
		return sizeof(entry->hdr->cedt);
	}
	return 0;
}

static enum acpi_subtable_type __init_or_acpilib
acpi_get_subtable_type(char *id)
{
	if (strncmp(id, ACPI_SIG_HMAT, 4) == 0)
		return ACPI_SUBTABLE_HMAT;
	if (strncmp(id, ACPI_SIG_PRMT, 4) == 0)
		return ACPI_SUBTABLE_PRMT;
	if (strncmp(id, ACPI_SIG_CEDT, 4) == 0)
		return ACPI_SUBTABLE_CEDT;
	return ACPI_SUBTABLE_COMMON;
}

static __init_or_acpilib int call_handler(struct acpi_subtable_proc *proc,
					  union acpi_subtable_headers *hdr,
					  unsigned long end)
{
	if (proc->handler)
		return proc->handler(hdr, end);
	if (proc->handler_arg)
		return proc->handler_arg(hdr, proc->arg, end);
	return -EINVAL;
}

/**
 * acpi_parse_entries_array - for each proc_num find a suitable subtable
 *
 * @id: table id (for debugging purposes)
 * @table_size: size of the root table
 * @table_header: where does the table start?
 * @proc: array of acpi_subtable_proc struct containing entry id
 *        and associated handler with it
 * @proc_num: how big proc is?
 * @max_entries: how many entries can we process?
 *
 * For each proc_num find a subtable with proc->id and run proc->handler
 * on it. Assumption is that there's only single handler for particular
 * entry id.
 *
 * The table_size is not the size of the complete ACPI table (the length
 * field in the header struct), but only the size of the root table; i.e.,
 * the offset from the very first byte of the complete ACPI table, to the
 * first byte of the very first subtable.
 *
 * On success returns sum of all matching entries for all proc handlers.
 * Otherwise, -ENODEV or -EINVAL is returned.
 */
int __init_or_acpilib
acpi_parse_entries_array(char *id, unsigned long table_size,
			 struct acpi_table_header *table_header,
			 struct acpi_subtable_proc *proc,
			 int proc_num, unsigned int max_entries)
{
	unsigned long table_end, subtable_len, entry_len;
	struct acpi_subtable_entry entry;
	int count = 0;
	int i;

	table_end = (unsigned long)table_header + table_header->length;

	/* Parse all entries looking for a match. */

	entry.type = acpi_get_subtable_type(id);
	entry.hdr = (union acpi_subtable_headers *)
	    ((unsigned long)table_header + table_size);
	subtable_len = acpi_get_subtable_header_length(&entry);

	while (((unsigned long)entry.hdr) + subtable_len < table_end) {
		for (i = 0; i < proc_num; i++) {
			if (acpi_get_entry_type(&entry) != proc[i].id)
				continue;

			if (!max_entries || count < max_entries)
				if (call_handler(&proc[i], entry.hdr, table_end))
					return -EINVAL;

			proc[i].count++;
			count++;
			break;
		}

		/*
		 * If entry->length is 0, break from this loop to avoid
		 * infinite loop.
		 */
		entry_len = acpi_get_entry_length(&entry);
		if (entry_len == 0) {
			pr_err("[%4.4s:0x%02x] Invalid zero length\n", id, proc->id);
			return -EINVAL;
		}

		entry.hdr = (union acpi_subtable_headers *)
		    ((unsigned long)entry.hdr + entry_len);
	}

	if (max_entries && count > max_entries) {
		pr_warn("[%4.4s:0x%02x] ignored %i entries of %i found\n",
			id, proc->id, count - max_entries, count);
	}

	return count;
}
