#ifndef DRBD_TAG_MAGIC_H
#define DRBD_TAG_MAGIC_H

#define TT_END     0
#define TT_REMOVED 0xE000

/* declare packet_type enums */
enum packet_types {
#define NL_PACKET(name, number, fields) P_ ## name = number,
#define NL_RESPONSE(name, number) P_ ## name = number,
#define NL_INTEGER(pn, pr, member)
#define NL_INT64(pn, pr, member)
#define NL_BIT(pn, pr, member)
#define NL_STRING(pn, pr, member, len)
#include "drbd_nl.h"
	P_nl_after_last_packet,
};

/* These struct are used to deduce the size of the tag lists: */
#define NL_PACKET(name, number, fields)	\
	struct name ## _tag_len_struct { fields };
#define NL_INTEGER(pn, pr, member)		\
	int member; int tag_and_len ## member;
#define NL_INT64(pn, pr, member)		\
	__u64 member; int tag_and_len ## member;
#define NL_BIT(pn, pr, member)		\
	unsigned char member:1; int tag_and_len ## member;
#define NL_STRING(pn, pr, member, len)	\
	unsigned char member[len]; int member ## _len; \
	int tag_and_len ## member;
#include <linux/drbd_nl.h>

/* declare tag-list-sizes */
static const int tag_list_sizes[] = {
#define NL_PACKET(name, number, fields) 2 fields ,
#define NL_INTEGER(pn, pr, member)      + 4 + 4
#define NL_INT64(pn, pr, member)        + 4 + 8
#define NL_BIT(pn, pr, member)          + 4 + 1
#define NL_STRING(pn, pr, member, len)  + 4 + (len)
#include "drbd_nl.h"
};

/* The two highest bits are used for the tag type */
#define TT_MASK      0xC000
#define TT_INTEGER   0x0000
#define TT_INT64     0x4000
#define TT_BIT       0x8000
#define TT_STRING    0xC000
/* The next bit indicates if processing of the tag is mandatory */
#define T_MANDATORY  0x2000
#define T_MAY_IGNORE 0x0000
#define TN_MASK      0x1fff
/* The remaining 13 bits are used to enumerate the tags */

#define tag_type(T)   ((T) & TT_MASK)
#define tag_number(T) ((T) & TN_MASK)

/* declare tag enums */
#define NL_PACKET(name, number, fields) fields
enum drbd_tags {
#define NL_INTEGER(pn, pr, member)     T_ ## member = pn | TT_INTEGER | pr ,
#define NL_INT64(pn, pr, member)       T_ ## member = pn | TT_INT64   | pr ,
#define NL_BIT(pn, pr, member)         T_ ## member = pn | TT_BIT     | pr ,
#define NL_STRING(pn, pr, member, len) T_ ## member = pn | TT_STRING  | pr ,
#include "drbd_nl.h"
};

struct tag {
	const char *name;
	int type_n_flags;
	int max_len;
};

/* declare tag names */
#define NL_PACKET(name, number, fields) fields
static const struct tag tag_descriptions[] = {
#define NL_INTEGER(pn, pr, member)     [ pn ] = { #member, TT_INTEGER | pr, sizeof(int)   },
#define NL_INT64(pn, pr, member)       [ pn ] = { #member, TT_INT64   | pr, sizeof(__u64) },
#define NL_BIT(pn, pr, member)         [ pn ] = { #member, TT_BIT     | pr, sizeof(int)   },
#define NL_STRING(pn, pr, member, len) [ pn ] = { #member, TT_STRING  | pr, (len)         },
#include "drbd_nl.h"
};

#endif
