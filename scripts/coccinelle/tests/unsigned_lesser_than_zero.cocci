// SPDX-License-Identifier: GPL-2.0-only
/// Unsigned expressions cannot be lesser than zero. Presence of
/// comparisons 'unsigned (<|<=|>|>=) 0' often indicates a bug,
/// usually wrong type of variable.
///
/// To reduce number of false positives following tests have been added:
/// - parts of range checks are skipped, eg. "if (u < 0 || u > 15) ...",
///   developers prefer to keep such code,
/// - comparisons "<= 0" and "> 0" are performed only on results of
///   signed functions/macros,
/// - hardcoded list of signed functions/macros with always non-negative
///   result is used to avoid false positives difficult to detect by other ways
///
// Confidence: Average
// Copyright: (C) 2015 Andrzej Hajda, Samsung Electronics Co., Ltd.
// URL: http://coccinelle.lip6.fr/
// Options: --all-includes

virtual context
virtual org
virtual report

@r_cmp@
position p;
typedef bool, u8, u16, u32, u64;
{unsigned char, unsigned short, unsigned int, unsigned long, unsigned long long,
	size_t, bool, u8, u16, u32, u64} v;
expression e;
@@

	\( v = e \| &v \)
	...
	(\( v@p < 0 \| v@p <= 0 \| v@p >= 0 \| v@p > 0 \))

@r@
position r_cmp.p;
typedef s8, s16, s32, s64;
{char, short, int, long, long long, ssize_t, s8, s16, s32, s64} vs;
expression c, e, v;
identifier f !~ "^(ata_id_queue_depth|btrfs_copy_from_user|dma_map_sg|dma_map_sg_attrs|fls|fls64|gameport_time|get_write_extents|nla_len|ntoh24|of_flat_dt_match|of_get_child_count|uart_circ_chars_pending|[A-Z0-9_]+)$";
@@

(
	v = f(...)@vs;
	... when != v = e;
*	(\( v@p <=@e 0 \| v@p >@e 0 \))
	... when any
|
(
	(\( v@p < 0 \| v@p <= 0 \)) || ... || (\( v >= c \| v > c \))
|
	(\( v >= c \| v > c \)) || ... || (\( v@p < 0 \| v@p <= 0 \))
|
	(\( v@p >= 0 \| v@p > 0 \)) && ... && (\( v < c \| v <= c \))
|
	((\( v < c \| v <= c \) && ... && \( v@p >= 0 \| v@p > 0 \)))
|
*	(\( v@p <@e 0 \| v@p >=@e 0 \))
)
)

@script:python depends on org@
p << r_cmp.p;
e << r.e;
@@

msg = "WARNING: Unsigned expression compared with zero: %s" % (e)
coccilib.org.print_todo(p[0], msg)

@script:python depends on report@
p << r_cmp.p;
e << r.e;
@@

msg = "WARNING: Unsigned expression compared with zero: %s" % (e)
coccilib.report.print_report(p[0], msg)
