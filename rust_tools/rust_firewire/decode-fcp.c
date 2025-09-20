// SPDX-License-Identifier: GPL-2.0
#include <linux/firewire-constants.h>
#include <stdio.h>
#include <stdlib.h>

#include "list.h"
#include "nosy-dump.h"

#define CSR_FCP_COMMAND			0xfffff0000b00ull
#define CSR_FCP_RESPONSE		0xfffff0000d00ull

static const char * const ctype_names[] = {
	[0x0] = "control",		[0x8] = "not implemented",
	[0x1] = "status",		[0x9] = "accepted",
	[0x2] = "specific inquiry",	[0xa] = "rejected",
	[0x3] = "notify",		[0xb] = "in transition",
	[0x4] = "general inquiry",	[0xc] = "stable",
	[0x5] = "(reserved 0x05)",	[0xd] = "changed",
	[0x6] = "(reserved 0x06)",	[0xe] = "(reserved 0x0e)",
	[0x7] = "(reserved 0x07)",	[0xf] = "interim",
};

static const char * const subunit_type_names[] = {
	[0x00] = "monitor",		[0x10] = "(reserved 0x10)",
	[0x01] = "audio",		[0x11] = "(reserved 0x11)",
	[0x02] = "printer",		[0x12] = "(reserved 0x12)",
	[0x03] = "disc",		[0x13] = "(reserved 0x13)",
	[0x04] = "tape recorder/player",[0x14] = "(reserved 0x14)",
	[0x05] = "tuner",		[0x15] = "(reserved 0x15)",
	[0x06] = "ca",			[0x16] = "(reserved 0x16)",
	[0x07] = "camera",		[0x17] = "(reserved 0x17)",
	[0x08] = "(reserved 0x08)",	[0x18] = "(reserved 0x18)",
	[0x09] = "panel",		[0x19] = "(reserved 0x19)",
	[0x0a] = "bulletin board",	[0x1a] = "(reserved 0x1a)",
	[0x0b] = "camera storage",	[0x1b] = "(reserved 0x1b)",
	[0x0c] = "(reserved 0x0c)",	[0x1c] = "vendor unique",
	[0x0d] = "(reserved 0x0d)",	[0x1d] = "all subunit types",
	[0x0e] = "(reserved 0x0e)",	[0x1e] = "subunit_type extended to next byte",
	[0x0f] = "(reserved 0x0f)",	[0x1f] = "unit",
};

struct avc_enum {
	int value;
	const char *name;
};

struct avc_field {
	const char *name;	/* Short name for field. */
	int offset;		/* Location of field, specified in bits; */
				/* negative means from end of packet.    */
	int width;		/* Width of field, 0 means use data_length. */
	struct avc_enum *names;
};

struct avc_opcode_info {
	const char *name;
	struct avc_field fields[8];
};

struct avc_enum power_field_names[] = {
	{ 0x70, "on" },
	{ 0x60, "off" },
	{ }
};

static const struct avc_opcode_info opcode_info[256] = {

	/* TA Document 1999026 */
	/* AV/C Digital Interface Command Set General Specification 4.0 */
	[0xb2] = { "power", {
			{ "state", 0, 8, power_field_names }
		}
	},
	[0x30] = { "unit info", {
			{ "foo", 0, 8 },
			{ "unit_type", 8, 5 },
			{ "unit", 13, 3 },
			{ "company id", 16, 24 },
		}
	},
	[0x31] = { "subunit info" },
	[0x01] = { "reserve" },
	[0xb0] = { "version" },
	[0x00] = { "vendor dependent" },
	[0x02] = { "plug info" },
	[0x12] = { "channel usage" },
	[0x24] = { "connect" },
	[0x20] = { "connect av" },
	[0x22] = { "connections" },
	[0x11] = { "digital input" },
	[0x10] = { "digital output" },
	[0x25] = { "disconnect" },
	[0x21] = { "disconnect av" },
	[0x19] = { "input plug signal format" },
	[0x18] = { "output plug signal format" },
	[0x1f] = { "general bus setup" },

	/* TA Document 1999025 */
	/* AV/C Descriptor Mechanism Specification Version 1.0 */
	[0x0c] = { "create descriptor" },
	[0x08] = { "open descriptor" },
	[0x09] = { "read descriptor" },
	[0x0a] = { "write descriptor" },
	[0x05] = { "open info block" },
	[0x06] = { "read info block" },
	[0x07] = { "write info block" },
	[0x0b] = { "search descriptor" },
	[0x0d] = { "object number select" },

	/* TA Document 1999015 */
	/* AV/C Command Set for Rate Control of Isochronous Data Flow 1.0 */
	[0xb3] = { "rate", {
			{ "subfunction", 0, 8 },
			{ "result", 8, 8 },
			{ "plug_type", 16, 8 },
			{ "plug_id", 16, 8 },
		}
	},

	/* TA Document 1999008 */
	/* AV/C Audio Subunit Specification 1.0 */
	[0xb8] = { "function block" },

	/* TA Document 2001001 */
	/* AV/C Panel Subunit Specification 1.1 */
	[0x7d] = { "gui update" },
	[0x7e] = { "push gui data" },
	[0x7f] = { "user action" },
	[0x7c] = { "pass through" },

	/* */
	[0x26] = { "asynchronous connection" },
};

struct avc_frame {
	uint32_t operand0:8;
	uint32_t opcode:8;
	uint32_t subunit_id:3;
	uint32_t subunit_type:5;
	uint32_t ctype:4;
	uint32_t cts:4;
};

static void
decode_avc(struct link_transaction *t)
{
	struct avc_frame *frame =
	    (struct avc_frame *) t->request->packet.write_block.data;
	const struct avc_opcode_info *info;
	const char *name;
	char buffer[32];
	int i;

	info = &opcode_info[frame->opcode];
	if (info->name == NULL) {
		snprintf(buffer, sizeof(buffer),
			 "(unknown opcode 0x%02x)", frame->opcode);
		name = buffer;
	} else {
		name = info->name;
	}

	printf("av/c %s, subunit_type=%s, subunit_id=%u, opcode=%s",
	    ctype_names[frame->ctype], subunit_type_names[frame->subunit_type],
	    frame->subunit_id, name);

	for (i = 0; info->fields[i].name != NULL; i++)
		printf(", %s", info->fields[i].name);

	printf("\n");
}

int
decode_fcp(struct link_transaction *t)
{
	struct avc_frame *frame =
	    (struct avc_frame *) t->request->packet.write_block.data;
	unsigned long long offset =
	    ((unsigned long long) t->request->packet.common.offset_high << 32) |
	    t->request->packet.common.offset_low;

	if (t->request->packet.common.tcode != TCODE_WRITE_BLOCK_REQUEST)
		return 0;

	if (offset == CSR_FCP_COMMAND || offset == CSR_FCP_RESPONSE) {
		switch (frame->cts) {
		case 0x00:
			decode_avc(t);
			break;
		case 0x01:
			printf("cal fcp frame (cts=0x01)\n");
			break;
		case 0x02:
			printf("ehs fcp frame (cts=0x02)\n");
			break;
		case 0x03:
			printf("havi fcp frame (cts=0x03)\n");
			break;
		case 0x0e:
			printf("vendor specific fcp frame (cts=0x0e)\n");
			break;
		case 0x0f:
			printf("extended cts\n");
			break;
		default:
			printf("reserved fcp frame (ctx=0x%02x)\n", frame->cts);
			break;
		}
		return 1;
	}

	return 0;
}

