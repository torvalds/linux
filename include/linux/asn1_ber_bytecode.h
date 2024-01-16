/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ASN.1 BER/DER/CER parsing state machine internal definitions
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_ASN1_BER_BYTECODE_H
#define _LINUX_ASN1_BER_BYTECODE_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif
#include <linux/asn1.h>

typedef int (*asn1_action_t)(void *context,
			     size_t hdrlen, /* In case of ANY type */
			     unsigned char tag, /* In case of ANY type */
			     const void *value, size_t vlen);

struct asn1_decoder {
	const unsigned char *machine;
	size_t machlen;
	const asn1_action_t *actions;
};

enum asn1_opcode {
	/* The tag-matching ops come first and the odd-numbered slots
	 * are for OR_SKIP ops.
	 */
#define ASN1_OP_MATCH__SKIP		  0x01
#define ASN1_OP_MATCH__ACT		  0x02
#define ASN1_OP_MATCH__JUMP		  0x04
#define ASN1_OP_MATCH__ANY		  0x08
#define ASN1_OP_MATCH__COND		  0x10

	ASN1_OP_MATCH			= 0x00,
	ASN1_OP_MATCH_OR_SKIP		= 0x01,
	ASN1_OP_MATCH_ACT		= 0x02,
	ASN1_OP_MATCH_ACT_OR_SKIP	= 0x03,
	ASN1_OP_MATCH_JUMP		= 0x04,
	ASN1_OP_MATCH_JUMP_OR_SKIP	= 0x05,
	ASN1_OP_MATCH_ANY		= 0x08,
	ASN1_OP_MATCH_ANY_OR_SKIP	= 0x09,
	ASN1_OP_MATCH_ANY_ACT		= 0x0a,
	ASN1_OP_MATCH_ANY_ACT_OR_SKIP	= 0x0b,
	/* Everything before here matches unconditionally */

	ASN1_OP_COND_MATCH_OR_SKIP	= 0x11,
	ASN1_OP_COND_MATCH_ACT_OR_SKIP	= 0x13,
	ASN1_OP_COND_MATCH_JUMP_OR_SKIP	= 0x15,
	ASN1_OP_COND_MATCH_ANY		= 0x18,
	ASN1_OP_COND_MATCH_ANY_OR_SKIP	= 0x19,
	ASN1_OP_COND_MATCH_ANY_ACT	= 0x1a,
	ASN1_OP_COND_MATCH_ANY_ACT_OR_SKIP = 0x1b,

	/* Everything before here will want a tag from the data */
#define ASN1_OP__MATCHES_TAG ASN1_OP_COND_MATCH_ANY_ACT_OR_SKIP

	/* These are here to help fill up space */
	ASN1_OP_COND_FAIL		= 0x1c,
	ASN1_OP_COMPLETE		= 0x1d,
	ASN1_OP_ACT			= 0x1e,
	ASN1_OP_MAYBE_ACT		= 0x1f,

	/* The following eight have bit 0 -> SET, 1 -> OF, 2 -> ACT */
	ASN1_OP_END_SEQ			= 0x20,
	ASN1_OP_END_SET			= 0x21,
	ASN1_OP_END_SEQ_OF		= 0x22,
	ASN1_OP_END_SET_OF		= 0x23,
	ASN1_OP_END_SEQ_ACT		= 0x24,
	ASN1_OP_END_SET_ACT		= 0x25,
	ASN1_OP_END_SEQ_OF_ACT		= 0x26,
	ASN1_OP_END_SET_OF_ACT		= 0x27,
#define ASN1_OP_END__SET		  0x01
#define ASN1_OP_END__OF			  0x02
#define ASN1_OP_END__ACT		  0x04

	ASN1_OP_RETURN			= 0x28,

	ASN1_OP__NR
};

#define _tag(CLASS, CP, TAG) ((ASN1_##CLASS << 6) | (ASN1_##CP << 5) | ASN1_##TAG)
#define _tagn(CLASS, CP, TAG) ((ASN1_##CLASS << 6) | (ASN1_##CP << 5) | TAG)
#define _jump_target(N) (N)
#define _action(N) (N)

#endif /* _LINUX_ASN1_BER_BYTECODE_H */
