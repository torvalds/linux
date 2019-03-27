# $FreeBSD$

PACKAGE=lib${LIB}
LIB=		ugidfw
SHLIB_MAJOR=	5
SRCS=		ugidfw.c
INCS=		ugidfw.h

WARNS?=		2

MAN+=		bsde_get_rule.3 bsde_get_rule_count.3 bsde_parse_rule.3 \
		bsde_rule_to_string.3 libugidfw.3

MLINKS= 	bsde_get_rule.3 bsde_add_rule.3
MLINKS+= 	bsde_get_rule.3 bsde_delete_rule.3
MLINKS+=	bsde_get_rule.3 bsde_set_rule.3
MLINKS+=	bsde_get_rule_count.3 bsde_get_rule_slots.3
MLINKS+=	bsde_parse_rule.3 bsde_parse_rule_string.3

.include <bsd.lib.mk>
