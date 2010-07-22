/*
 * nosy-dump - Interface to snoop mode driver for TI PCILynx 1394 controllers
 * Copyright (C) 2002-2006 Kristian Høgsberg
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <byteswap.h>
#include <endian.h>
#include <fcntl.h>
#include <linux/firewire-constants.h>
#include <poll.h>
#include <popt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "list.h"
#include "nosy-dump.h"
#include "nosy-user.h"

enum {
	PACKET_FIELD_DETAIL		= 0x01,
	PACKET_FIELD_DATA_LENGTH	= 0x02,
	/* Marks the fields we print in transaction view. */
	PACKET_FIELD_TRANSACTION	= 0x04,
};

static void print_packet(uint32_t *data, size_t length);
static void decode_link_packet(struct link_packet *packet, size_t length,
			       int include_flags, int exclude_flags);
static int run = 1;
sig_t sys_sigint_handler;

static char *option_nosy_device = "/dev/nosy";
static char *option_view = "packet";
static char *option_output;
static char *option_input;
static int option_hex;
static int option_iso;
static int option_cycle_start;
static int option_version;
static int option_verbose;

enum {
	VIEW_TRANSACTION,
	VIEW_PACKET,
	VIEW_STATS,
};

static const struct poptOption options[] = {
	{
		.longName	= "device",
		.shortName	= 'd',
		.argInfo	= POPT_ARG_STRING,
		.arg		= &option_nosy_device,
		.descrip	= "Path to nosy device.",
		.argDescrip	= "DEVICE"
	},
	{
		.longName	= "view",
		.argInfo	= POPT_ARG_STRING,
		.arg		= &option_view,
		.descrip	= "Specify view of bus traffic: packet, transaction or stats.",
		.argDescrip	= "VIEW"
	},
	{
		.longName	= "hex",
		.shortName	= 'x',
		.argInfo	= POPT_ARG_NONE,
		.arg		= &option_hex,
		.descrip	= "Print each packet in hex.",
	},
	{
		.longName	= "iso",
		.argInfo	= POPT_ARG_NONE,
		.arg		= &option_iso,
		.descrip	= "Print iso packets.",
	},
	{
		.longName	= "cycle-start",
		.argInfo	= POPT_ARG_NONE,
		.arg		= &option_cycle_start,
		.descrip	= "Print cycle start packets.",
	},
	{
		.longName	= "verbose",
		.shortName	= 'v',
		.argInfo	= POPT_ARG_NONE,
		.arg		= &option_verbose,
		.descrip	= "Verbose packet view.",
	},
	{
		.longName	= "output",
		.shortName	= 'o',
		.argInfo	= POPT_ARG_STRING,
		.arg		= &option_output,
		.descrip	= "Log to output file.",
		.argDescrip	= "FILENAME"
	},
	{
		.longName	= "input",
		.shortName	= 'i',
		.argInfo	= POPT_ARG_STRING,
		.arg		= &option_input,
		.descrip	= "Decode log from file.",
		.argDescrip	= "FILENAME"
	},
	{
		.longName	= "version",
		.argInfo	= POPT_ARG_NONE,
		.arg		= &option_version,
		.descrip	= "Specify print version info.",
	},
	POPT_AUTOHELP
	POPT_TABLEEND
};

/* Allow all ^C except the first to interrupt the program in the usual way. */
static void
sigint_handler(int signal_num)
{
	if (run == 1) {
		run = 0;
		signal(SIGINT, SIG_DFL);
	}
}

static struct subaction *
subaction_create(uint32_t *data, size_t length)
{
	struct subaction *sa;

	/* we put the ack in the subaction struct for easy access. */
	sa = malloc(sizeof *sa - sizeof sa->packet + length);
	sa->ack = data[length / 4 - 1];
	sa->length = length;
	memcpy(&sa->packet, data, length);

	return sa;
}

static void
subaction_destroy(struct subaction *sa)
{
	free(sa);
}

static struct list pending_transaction_list = {
	&pending_transaction_list, &pending_transaction_list
};

static struct link_transaction *
link_transaction_lookup(int request_node, int response_node, int tlabel)
{
	struct link_transaction *t;

	list_for_each_entry(t, &pending_transaction_list, link) {
		if (t->request_node == request_node &&
		    t->response_node == response_node &&
		    t->tlabel == tlabel)
			return t;
	}

	t = malloc(sizeof *t);
	t->request_node = request_node;
	t->response_node = response_node;
	t->tlabel = tlabel;
	list_init(&t->request_list);
	list_init(&t->response_list);

	list_append(&pending_transaction_list, &t->link);

	return t;
}

static void
link_transaction_destroy(struct link_transaction *t)
{
	struct subaction *sa;

	while (!list_empty(&t->request_list)) {
		sa = list_head(&t->request_list, struct subaction, link);
		list_remove(&sa->link);
		subaction_destroy(sa);
	}
	while (!list_empty(&t->response_list)) {
		sa = list_head(&t->response_list, struct subaction, link);
		list_remove(&sa->link);
		subaction_destroy(sa);
	}
	free(t);
}

struct protocol_decoder {
	const char *name;
	int (*decode)(struct link_transaction *t);
};

static const struct protocol_decoder protocol_decoders[] = {
	{ "FCP", decode_fcp }
};

static void
handle_transaction(struct link_transaction *t)
{
	struct subaction *sa;
	int i;

	if (!t->request) {
		printf("BUG in handle_transaction\n");
		return;
	}

	for (i = 0; i < array_length(protocol_decoders); i++)
		if (protocol_decoders[i].decode(t))
			break;

	/* HACK: decode only fcp right now. */
	return;

	decode_link_packet(&t->request->packet, t->request->length,
			   PACKET_FIELD_TRANSACTION, 0);
	if (t->response)
		decode_link_packet(&t->response->packet, t->request->length,
				   PACKET_FIELD_TRANSACTION, 0);
	else
		printf("[no response]");

	if (option_verbose) {
		list_for_each_entry(sa, &t->request_list, link)
			print_packet((uint32_t *) &sa->packet, sa->length);
		list_for_each_entry(sa, &t->response_list, link)
			print_packet((uint32_t *) &sa->packet, sa->length);
	}
	printf("\r\n");

	link_transaction_destroy(t);
}

static void
clear_pending_transaction_list(void)
{
	struct link_transaction *t;

	while (!list_empty(&pending_transaction_list)) {
		t = list_head(&pending_transaction_list,
			      struct link_transaction, link);
		list_remove(&t->link);
		link_transaction_destroy(t);
		/* print unfinished transactions */
	}
}

static const char * const tcode_names[] = {
	[0x0] = "write_quadlet_request",	[0x6] = "read_quadlet_response",
	[0x1] = "write_block_request",		[0x7] = "read_block_response",
	[0x2] = "write_response",		[0x8] = "cycle_start",
	[0x3] = "reserved",			[0x9] = "lock_request",
	[0x4] = "read_quadlet_request",		[0xa] = "iso_data",
	[0x5] = "read_block_request",		[0xb] = "lock_response",
};

static const char * const ack_names[] = {
	[0x0] = "no ack",			[0x8] = "reserved (0x08)",
	[0x1] = "ack_complete",			[0x9] = "reserved (0x09)",
	[0x2] = "ack_pending",			[0xa] = "reserved (0x0a)",
	[0x3] = "reserved (0x03)",		[0xb] = "reserved (0x0b)",
	[0x4] = "ack_busy_x",			[0xc] = "reserved (0x0c)",
	[0x5] = "ack_busy_a",			[0xd] = "ack_data_error",
	[0x6] = "ack_busy_b",			[0xe] = "ack_type_error",
	[0x7] = "reserved (0x07)",		[0xf] = "reserved (0x0f)",
};

static const char * const rcode_names[] = {
	[0x0] = "complete",			[0x4] = "conflict_error",
	[0x1] = "reserved (0x01)",		[0x5] = "data_error",
	[0x2] = "reserved (0x02)",		[0x6] = "type_error",
	[0x3] = "reserved (0x03)",		[0x7] = "address_error",
};

static const char * const retry_names[] = {
	[0x0] = "retry_1",
	[0x1] = "retry_x",
	[0x2] = "retry_a",
	[0x3] = "retry_b",
};

enum {
	PACKET_RESERVED,
	PACKET_REQUEST,
	PACKET_RESPONSE,
	PACKET_OTHER,
};

struct packet_info {
	const char *name;
	int type;
	int response_tcode;
	const struct packet_field *fields;
	int field_count;
};

struct packet_field {
	const char *name; /* Short name for field. */
	int offset;	/* Location of field, specified in bits; */
			/* negative means from end of packet.    */
	int width;	/* Width of field, 0 means use data_length. */
	int flags;	/* Show options. */
	const char * const *value_names;
};

#define COMMON_REQUEST_FIELDS						\
	{ "dest", 0, 16, PACKET_FIELD_TRANSACTION },			\
	{ "tl", 16, 6 },						\
	{ "rt", 22, 2, PACKET_FIELD_DETAIL, retry_names },		\
	{ "tcode", 24, 4, PACKET_FIELD_TRANSACTION, tcode_names },	\
	{ "pri", 28, 4, PACKET_FIELD_DETAIL },				\
	{ "src", 32, 16, PACKET_FIELD_TRANSACTION },			\
	{ "offs", 48, 48, PACKET_FIELD_TRANSACTION }

#define COMMON_RESPONSE_FIELDS						\
	{ "dest", 0, 16 },						\
	{ "tl", 16, 6 },						\
	{ "rt", 22, 2, PACKET_FIELD_DETAIL, retry_names },		\
	{ "tcode", 24, 4, 0, tcode_names },				\
	{ "pri", 28, 4, PACKET_FIELD_DETAIL },				\
	{ "src", 32, 16 },						\
	{ "rcode", 48, 4, PACKET_FIELD_TRANSACTION, rcode_names }

static const struct packet_field read_quadlet_request_fields[] = {
	COMMON_REQUEST_FIELDS,
	{ "crc", 96, 32, PACKET_FIELD_DETAIL },
	{ "ack", 156, 4, 0, ack_names },
};

static const struct packet_field read_quadlet_response_fields[] = {
	COMMON_RESPONSE_FIELDS,
	{ "data", 96, 32, PACKET_FIELD_TRANSACTION },
	{ "crc", 128, 32, PACKET_FIELD_DETAIL },
	{ "ack", 188, 4, 0, ack_names },
};

static const struct packet_field read_block_request_fields[] = {
	COMMON_REQUEST_FIELDS,
	{ "data_length", 96, 16, PACKET_FIELD_TRANSACTION },
	{ "extended_tcode", 112, 16 },
	{ "crc", 128, 32, PACKET_FIELD_DETAIL },
	{ "ack", 188, 4, 0, ack_names },
};

static const struct packet_field block_response_fields[] = {
	COMMON_RESPONSE_FIELDS,
	{ "data_length", 96, 16, PACKET_FIELD_DATA_LENGTH },
	{ "extended_tcode", 112, 16 },
	{ "crc", 128, 32, PACKET_FIELD_DETAIL },
	{ "data", 160, 0, PACKET_FIELD_TRANSACTION },
	{ "crc", -64, 32, PACKET_FIELD_DETAIL },
	{ "ack", -4, 4, 0, ack_names },
};

static const struct packet_field write_quadlet_request_fields[] = {
	COMMON_REQUEST_FIELDS,
	{ "data", 96, 32, PACKET_FIELD_TRANSACTION },
	{ "ack", -4, 4, 0, ack_names },
};

static const struct packet_field block_request_fields[] = {
	COMMON_REQUEST_FIELDS,
	{ "data_length", 96, 16, PACKET_FIELD_DATA_LENGTH | PACKET_FIELD_TRANSACTION },
	{ "extended_tcode", 112, 16, PACKET_FIELD_TRANSACTION },
	{ "crc", 128, 32, PACKET_FIELD_DETAIL },
	{ "data", 160, 0, PACKET_FIELD_TRANSACTION },
	{ "crc", -64, 32, PACKET_FIELD_DETAIL },
	{ "ack", -4, 4, 0, ack_names },
};

static const struct packet_field write_response_fields[] = {
	COMMON_RESPONSE_FIELDS,
	{ "reserved", 64, 32, PACKET_FIELD_DETAIL },
	{ "ack", -4, 4, 0, ack_names },
};

static const struct packet_field iso_data_fields[] = {
	{ "data_length", 0, 16, PACKET_FIELD_DATA_LENGTH },
	{ "tag", 16, 2 },
	{ "channel", 18, 6 },
	{ "tcode", 24, 4, 0, tcode_names },
	{ "sy", 28, 4 },
	{ "crc", 32, 32, PACKET_FIELD_DETAIL },
	{ "data", 64, 0 },
	{ "crc", -64, 32, PACKET_FIELD_DETAIL },
	{ "ack", -4, 4, 0, ack_names },
};

static const struct packet_info packet_info[] = {
	{
		.name		= "write_quadlet_request",
		.type		= PACKET_REQUEST,
		.response_tcode	= TCODE_WRITE_RESPONSE,
		.fields		= write_quadlet_request_fields,
		.field_count	= array_length(write_quadlet_request_fields)
	},
	{
		.name		= "write_block_request",
		.type		= PACKET_REQUEST,
		.response_tcode	= TCODE_WRITE_RESPONSE,
		.fields		= block_request_fields,
		.field_count	= array_length(block_request_fields)
	},
	{
		.name		= "write_response",
		.type		= PACKET_RESPONSE,
		.fields		= write_response_fields,
		.field_count	= array_length(write_response_fields)
	},
	{
		.name		= "reserved",
		.type		= PACKET_RESERVED,
	},
	{
		.name		= "read_quadlet_request",
		.type		= PACKET_REQUEST,
		.response_tcode	= TCODE_READ_QUADLET_RESPONSE,
		.fields		= read_quadlet_request_fields,
		.field_count	= array_length(read_quadlet_request_fields)
	},
	{
		.name		= "read_block_request",
		.type		= PACKET_REQUEST,
		.response_tcode	= TCODE_READ_BLOCK_RESPONSE,
		.fields		= read_block_request_fields,
		.field_count	= array_length(read_block_request_fields)
	},
	{
		.name		= "read_quadlet_response",
		.type		= PACKET_RESPONSE,
		.fields		= read_quadlet_response_fields,
		.field_count	= array_length(read_quadlet_response_fields)
	},
	{
		.name		= "read_block_response",
		.type		= PACKET_RESPONSE,
		.fields		= block_response_fields,
		.field_count	= array_length(block_response_fields)
	},
	{
		.name		= "cycle_start",
		.type		= PACKET_OTHER,
		.fields		= write_quadlet_request_fields,
		.field_count	= array_length(write_quadlet_request_fields)
	},
	{
		.name		= "lock_request",
		.type		= PACKET_REQUEST,
		.fields		= block_request_fields,
		.field_count	= array_length(block_request_fields)
	},
	{
		.name		= "iso_data",
		.type		= PACKET_OTHER,
		.fields		= iso_data_fields,
		.field_count	= array_length(iso_data_fields)
	},
	{
		.name		= "lock_response",
		.type		= PACKET_RESPONSE,
		.fields		= block_response_fields,
		.field_count	= array_length(block_response_fields)
	},
};

static int
handle_request_packet(uint32_t *data, size_t length)
{
	struct link_packet *p = (struct link_packet *) data;
	struct subaction *sa, *prev;
	struct link_transaction *t;

	t = link_transaction_lookup(p->common.source, p->common.destination,
			p->common.tlabel);
	sa = subaction_create(data, length);
	t->request = sa;

	if (!list_empty(&t->request_list)) {
		prev = list_tail(&t->request_list,
				 struct subaction, link);

		if (!ACK_BUSY(prev->ack)) {
			/*
			 * error, we should only see ack_busy_* before the
			 * ack_pending/ack_complete -- this is an ack_pending
			 * instead (ack_complete would have finished the
			 * transaction).
			 */
		}

		if (prev->packet.common.tcode != sa->packet.common.tcode ||
		    prev->packet.common.tlabel != sa->packet.common.tlabel) {
			/* memcmp() ? */
			/* error, these should match for retries. */
		}
	}

	list_append(&t->request_list, &sa->link);

	switch (sa->ack) {
	case ACK_COMPLETE:
		if (p->common.tcode != TCODE_WRITE_QUADLET_REQUEST &&
		    p->common.tcode != TCODE_WRITE_BLOCK_REQUEST)
			/* error, unified transactions only allowed for write */;
		list_remove(&t->link);
		handle_transaction(t);
		break;

	case ACK_NO_ACK:
	case ACK_DATA_ERROR:
	case ACK_TYPE_ERROR:
		list_remove(&t->link);
		handle_transaction(t);
		break;

	case ACK_PENDING:
		/* request subaction phase over, wait for response. */
		break;

	case ACK_BUSY_X:
	case ACK_BUSY_A:
	case ACK_BUSY_B:
		/* ok, wait for retry. */
		/* check that retry protocol is respected. */
		break;
	}

	return 1;
}

static int
handle_response_packet(uint32_t *data, size_t length)
{
	struct link_packet *p = (struct link_packet *) data;
	struct subaction *sa, *prev;
	struct link_transaction *t;

	t = link_transaction_lookup(p->common.destination, p->common.source,
			p->common.tlabel);
	if (list_empty(&t->request_list)) {
		/* unsolicited response */
	}

	sa = subaction_create(data, length);
	t->response = sa;

	if (!list_empty(&t->response_list)) {
		prev = list_tail(&t->response_list, struct subaction, link);

		if (!ACK_BUSY(prev->ack)) {
			/*
			 * error, we should only see ack_busy_* before the
			 * ack_pending/ack_complete
			 */
		}

		if (prev->packet.common.tcode != sa->packet.common.tcode ||
		    prev->packet.common.tlabel != sa->packet.common.tlabel) {
			/* use memcmp() instead? */
			/* error, these should match for retries. */
		}
	} else {
		prev = list_tail(&t->request_list, struct subaction, link);
		if (prev->ack != ACK_PENDING) {
			/*
			 * error, should not get response unless last request got
			 * ack_pending.
			 */
		}

		if (packet_info[prev->packet.common.tcode].response_tcode !=
		    sa->packet.common.tcode) {
			/* error, tcode mismatch */
		}
	}

	list_append(&t->response_list, &sa->link);

	switch (sa->ack) {
	case ACK_COMPLETE:
	case ACK_NO_ACK:
	case ACK_DATA_ERROR:
	case ACK_TYPE_ERROR:
		list_remove(&t->link);
		handle_transaction(t);
		/* transaction complete, remove t from pending list. */
		break;

	case ACK_PENDING:
		/* error for responses. */
		break;

	case ACK_BUSY_X:
	case ACK_BUSY_A:
	case ACK_BUSY_B:
		/* no problem, wait for next retry */
		break;
	}

	return 1;
}

static int
handle_packet(uint32_t *data, size_t length)
{
	if (length == 0) {
		printf("bus reset\r\n");
		clear_pending_transaction_list();
	} else if (length > sizeof(struct phy_packet)) {
		struct link_packet *p = (struct link_packet *) data;

		switch (packet_info[p->common.tcode].type) {
		case PACKET_REQUEST:
			return handle_request_packet(data, length);

		case PACKET_RESPONSE:
			return handle_response_packet(data, length);

		case PACKET_OTHER:
		case PACKET_RESERVED:
			return 0;
		}
	}

	return 1;
}

static unsigned int
get_bits(struct link_packet *packet, int offset, int width)
{
	uint32_t *data = (uint32_t *) packet;
	uint32_t index, shift, mask;

	index = offset / 32 + 1;
	shift = 32 - (offset & 31) - width;
	mask = width == 32 ? ~0 : (1 << width) - 1;

	return (data[index] >> shift) & mask;
}

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define byte_index(i) ((i) ^ 3)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define byte_index(i) (i)
#else
#error unsupported byte order.
#endif

static void
dump_data(unsigned char *data, int length)
{
	int i, print_length;

	if (length > 128)
		print_length = 128;
	else
		print_length = length;

	for (i = 0; i < print_length; i++)
		printf("%s%02hhx",
		       (i % 4 == 0 && i != 0) ? " " : "",
		       data[byte_index(i)]);

	if (print_length < length)
		printf(" (%d more bytes)", length - print_length);
}

static void
decode_link_packet(struct link_packet *packet, size_t length,
		   int include_flags, int exclude_flags)
{
	const struct packet_info *pi;
	int data_length = 0;
	int i;

	pi = &packet_info[packet->common.tcode];

	for (i = 0; i < pi->field_count; i++) {
		const struct packet_field *f = &pi->fields[i];
		int offset;

		if (f->flags & exclude_flags)
			continue;
		if (include_flags && !(f->flags & include_flags))
			continue;

		if (f->offset < 0)
			offset = length * 8 + f->offset - 32;
		else
			offset = f->offset;

		if (f->value_names != NULL) {
			uint32_t bits;

			bits = get_bits(packet, offset, f->width);
			printf("%s", f->value_names[bits]);
		} else if (f->width == 0) {
			printf("%s=[", f->name);
			dump_data((unsigned char *) packet + (offset / 8 + 4), data_length);
			printf("]");
		} else {
			unsigned long long bits;
			int high_width, low_width;

			if ((offset & ~31) != ((offset + f->width - 1) & ~31)) {
				/* Bit field spans quadlet boundary. */
				high_width = ((offset + 31) & ~31) - offset;
				low_width = f->width - high_width;

				bits = get_bits(packet, offset, high_width);
				bits = (bits << low_width) |
					get_bits(packet, offset + high_width, low_width);
			} else {
				bits = get_bits(packet, offset, f->width);
			}

			printf("%s=0x%0*llx", f->name, (f->width + 3) / 4, bits);

			if (f->flags & PACKET_FIELD_DATA_LENGTH)
				data_length = bits;
		}

		if (i < pi->field_count - 1)
			printf(", ");
	}
}

static void
print_packet(uint32_t *data, size_t length)
{
	int i;

	printf("%6u  ", data[0]);

	if (length == 4) {
		printf("bus reset");
	} else if (length < sizeof(struct phy_packet)) {
		printf("short packet: ");
		for (i = 1; i < length / 4; i++)
			printf("%s%08x", i == 0 ? "[" : " ", data[i]);
		printf("]");

	} else if (length == sizeof(struct phy_packet) && data[1] == ~data[2]) {
		struct phy_packet *pp = (struct phy_packet *) data;

		/* phy packet are 3 quadlets: the 1 quadlet payload,
		 * the bitwise inverse of the payload and the snoop
		 * mode ack */

		switch (pp->common.identifier) {
		case PHY_PACKET_CONFIGURATION:
			if (!pp->phy_config.set_root && !pp->phy_config.set_gap_count) {
				printf("ext phy config: phy_id=%02x", pp->phy_config.root_id);
			} else {
				printf("phy config:");
				if (pp->phy_config.set_root)
					printf(" set_root_id=%02x", pp->phy_config.root_id);
				if (pp->phy_config.set_gap_count)
					printf(" set_gap_count=%d", pp->phy_config.gap_count);
			}
			break;

		case PHY_PACKET_LINK_ON:
			printf("link-on packet, phy_id=%02x", pp->link_on.phy_id);
			break;

		case PHY_PACKET_SELF_ID:
			if (pp->self_id.extended) {
				printf("extended self id: phy_id=%02x, seq=%d",
				       pp->ext_self_id.phy_id, pp->ext_self_id.sequence);
			} else {
				static const char * const speed_names[] = {
					"S100", "S200", "S400", "BETA"
				};
				printf("self id: phy_id=%02x, link %s, gap_count=%d, speed=%s%s%s",
				       pp->self_id.phy_id,
				       (pp->self_id.link_active ? "active" : "not active"),
				       pp->self_id.gap_count,
				       speed_names[pp->self_id.phy_speed],
				       (pp->self_id.contender ? ", irm contender" : ""),
				       (pp->self_id.initiated_reset ? ", initiator" : ""));
			}
			break;
		default:
			printf("unknown phy packet: ");
			for (i = 1; i < length / 4; i++)
				printf("%s%08x", i == 0 ? "[" : " ", data[i]);
			printf("]");
			break;
		}
	} else {
		struct link_packet *packet = (struct link_packet *) data;

		decode_link_packet(packet, length, 0,
				   option_verbose ? 0 : PACKET_FIELD_DETAIL);
	}

	if (option_hex) {
		printf("  [");
		dump_data((unsigned char *) data + 4, length - 4);
		printf("]");
	}

	printf("\r\n");
}

#define HIDE_CURSOR	"\033[?25l"
#define SHOW_CURSOR	"\033[?25h"
#define CLEAR		"\033[H\033[2J"

static void
print_stats(uint32_t *data, size_t length)
{
	static int bus_reset_count, short_packet_count, phy_packet_count;
	static int tcode_count[16];
	static struct timeval last_update;
	struct timeval now;
	int i;

	if (length == 0)
		bus_reset_count++;
	else if (length < sizeof(struct phy_packet))
		short_packet_count++;
	else if (length == sizeof(struct phy_packet) && data[1] == ~data[2])
		phy_packet_count++;
	else {
		struct link_packet *packet = (struct link_packet *) data;
		tcode_count[packet->common.tcode]++;
	}

	gettimeofday(&now, NULL);
	if (now.tv_sec <= last_update.tv_sec &&
	    now.tv_usec < last_update.tv_usec + 500000)
		return;

	last_update = now;
	printf(CLEAR HIDE_CURSOR
	       "  bus resets              : %8d\n"
	       "  short packets           : %8d\n"
	       "  phy packets             : %8d\n",
	       bus_reset_count, short_packet_count, phy_packet_count);

	for (i = 0; i < array_length(packet_info); i++)
		if (packet_info[i].type != PACKET_RESERVED)
			printf("  %-24s: %8d\n", packet_info[i].name, tcode_count[i]);
	printf(SHOW_CURSOR "\n");
}

static struct termios saved_attributes;

static void
reset_input_mode(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &saved_attributes);
}

static void
set_input_mode(void)
{
	struct termios tattr;

	/* Make sure stdin is a terminal. */
	if (!isatty(STDIN_FILENO)) {
		fprintf(stderr, "Not a terminal.\n");
		exit(EXIT_FAILURE);
	}

	/* Save the terminal attributes so we can restore them later. */
	tcgetattr(STDIN_FILENO, &saved_attributes);
	atexit(reset_input_mode);

	/* Set the funny terminal modes. */
	tcgetattr(STDIN_FILENO, &tattr);
	tattr.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
	tattr.c_cc[VMIN] = 1;
	tattr.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &tattr);
}

int main(int argc, const char *argv[])
{
	uint32_t buf[128 * 1024];
	uint32_t filter;
	int length, retval, view;
	int fd = -1;
	FILE *output = NULL, *input = NULL;
	poptContext con;
	char c;
	struct pollfd pollfds[2];

	sys_sigint_handler = signal(SIGINT, sigint_handler);

	con = poptGetContext(NULL, argc, argv, options, 0);
	retval = poptGetNextOpt(con);
	if (retval < -1) {
		poptPrintUsage(con, stdout, 0);
		return -1;
	}

	if (option_version) {
		printf("dump tool for nosy sniffer, version %s\n", VERSION);
		return 0;
	}

	if (__BYTE_ORDER != __LITTLE_ENDIAN)
		fprintf(stderr, "warning: nosy has only been tested on little "
			"endian machines\n");

	if (option_input != NULL) {
		input = fopen(option_input, "r");
		if (input == NULL) {
			fprintf(stderr, "Could not open %s, %m\n", option_input);
			return -1;
		}
	} else {
		fd = open(option_nosy_device, O_RDWR);
		if (fd < 0) {
			fprintf(stderr, "Could not open %s, %m\n", option_nosy_device);
			return -1;
		}
		set_input_mode();
	}

	if (strcmp(option_view, "transaction") == 0)
		view = VIEW_TRANSACTION;
	else if (strcmp(option_view, "stats") == 0)
		view = VIEW_STATS;
	else
		view = VIEW_PACKET;

	if (option_output) {
		output = fopen(option_output, "w");
		if (output == NULL) {
			fprintf(stderr, "Could not open %s, %m\n", option_output);
			return -1;
		}
	}

	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

	filter = ~0;
	if (!option_iso)
		filter &= ~(1 << TCODE_STREAM_DATA);
	if (!option_cycle_start)
		filter &= ~(1 << TCODE_CYCLE_START);
	if (view == VIEW_STATS)
		filter = ~(1 << TCODE_CYCLE_START);

	ioctl(fd, NOSY_IOC_FILTER, filter);

	ioctl(fd, NOSY_IOC_START);

	pollfds[0].fd = fd;
	pollfds[0].events = POLLIN;
	pollfds[1].fd = STDIN_FILENO;
	pollfds[1].events = POLLIN;

	while (run) {
		if (input != NULL) {
			if (fread(&length, sizeof length, 1, input) != 1)
				return 0;
			fread(buf, 1, length, input);
		} else {
			poll(pollfds, 2, -1);
			if (pollfds[1].revents) {
				read(STDIN_FILENO, &c, sizeof c);
				switch (c) {
				case 'q':
					if (output != NULL)
						fclose(output);
					return 0;
				}
			}

			if (pollfds[0].revents)
				length = read(fd, buf, sizeof buf);
			else
				continue;
		}

		if (output != NULL) {
			fwrite(&length, sizeof length, 1, output);
			fwrite(buf, 1, length, output);
		}

		switch (view) {
		case VIEW_TRANSACTION:
			handle_packet(buf, length);
			break;
		case VIEW_PACKET:
			print_packet(buf, length);
			break;
		case VIEW_STATS:
			print_stats(buf, length);
			break;
		}
	}

	if (output != NULL)
		fclose(output);

	close(fd);

	poptFreeContext(con);

	return 0;
}
