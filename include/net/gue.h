/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_GUE_H
#define __NET_GUE_H

/* Definitions for the GUE header, standard and private flags, lengths
 * of optional fields are below.
 *
 * Diagram of GUE header:
 *
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |Ver|C|  Hlen   | Proto/ctype   |        Standard flags       |P|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * ~                      Fields (optional)                        ~
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            Private flags (optional, P bit is set)             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * ~                   Private fields (optional)                   ~
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * C bit indicates contol message when set, data message when unset.
 * For a control message, proto/ctype is interpreted as a type of
 * control message. For data messages, proto/ctype is the IP protocol
 * of the next header.
 *
 * P bit indicates private flags field is present. The private flags
 * may refer to options placed after this field.
 */

struct guehdr {
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u8	hlen:5,
				control:1,
				version:2;
#elif defined (__BIG_ENDIAN_BITFIELD)
			__u8	version:2,
				control:1,
				hlen:5;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
			__u8	proto_ctype;
			__be16	flags;
		};
		__be32	word;
	};
};

/* Standard flags in GUE header */

#define GUE_FLAG_PRIV	htons(1<<0)	/* Private flags are in options */
#define GUE_LEN_PRIV	4

#define GUE_FLAGS_ALL	(GUE_FLAG_PRIV)

/* Private flags in the private option extension */

#define GUE_PFLAG_REMCSUM	htonl(1U << 31)
#define GUE_PLEN_REMCSUM	4

#define GUE_PFLAGS_ALL	(GUE_PFLAG_REMCSUM)

/* Functions to compute options length corresponding to flags.
 * If we ever have a lot of flags this can be potentially be
 * converted to a more optimized algorithm (table lookup
 * for instance).
 */
static inline size_t guehdr_flags_len(__be16 flags)
{
	return ((flags & GUE_FLAG_PRIV) ? GUE_LEN_PRIV : 0);
}

static inline size_t guehdr_priv_flags_len(__be32 flags)
{
	return 0;
}

/* Validate standard and private flags. Returns non-zero (meaning invalid)
 * if there is an unknown standard or private flags, or the options length for
 * the flags exceeds the options length specific in hlen of the GUE header.
 */
static inline int validate_gue_flags(struct guehdr *guehdr, size_t optlen)
{
	__be16 flags = guehdr->flags;
	size_t len;

	if (flags & ~GUE_FLAGS_ALL)
		return 1;

	len = guehdr_flags_len(flags);
	if (len > optlen)
		return 1;

	if (flags & GUE_FLAG_PRIV) {
		/* Private flags are last four bytes accounted in
		 * guehdr_flags_len
		 */
		__be32 pflags = *(__be32 *)((void *)&guehdr[1] +
					    len - GUE_LEN_PRIV);

		if (pflags & ~GUE_PFLAGS_ALL)
			return 1;

		len += guehdr_priv_flags_len(pflags);
		if (len > optlen)
			return 1;
	}

	return 0;
}

#endif
