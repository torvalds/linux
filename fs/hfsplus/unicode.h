/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Unicode related declarations
 */

#ifndef _LINUX_HFSPLUS_UNICODE_H
#define _LINUX_HFSPLUS_UNICODE_H

/*
 * struct hfsplus_ccc_range - one run of consecutive BMP code points that
 *                            share the same Unicode canonical combining class
 * @first: the first UTF-16 code point in the run
 * @last: the last code point in the run (inclusive)
 * @combining_class: canonical combining class (ccc) shared by @first..@last
 */
struct hfsplus_ccc_range {
	u16 first;
	u16 last;
	u8 combining_class;
};

/*
 * struct hfsplus_legacy_decomp - corrected canonical decomposition
 * @uc: the code point (a UTF-16 code unit) to look up
 * @len: number of valid code units in @repl
 * @repl: corrected decomposition sequence (base letter + combining marks)
 */
struct hfsplus_legacy_decomp {
	u16 uc;
	u8 len;
#define HFSPLUS_LEGACY_DECOMP_MAX_LEN	(3)
	u16 repl[HFSPLUS_LEGACY_DECOMP_MAX_LEN];
};

/*
 * struct hfsplus_legacy_seq_fixup - corrected replacement for a short
 *                                   sequence of already-decomposed code units
 * @match_len: number of valid code units in @match
 * @match: decomposed sequence to look for in the buffer
 * @repl_len: number of valid code units in @repl
 * @repl: corrected sequence to substitute in place of @match
 */
struct hfsplus_legacy_seq_fixup {
	u8 match_len;
#define HFSPLUS_LEGACY_SEQ_MAX_MATCH	(3)
	u16 match[HFSPLUS_LEGACY_SEQ_MAX_MATCH];
	u8 repl_len;
#define HFSPLUS_LEGACY_SEQ_MAX_REPL	(2)
	u16 repl[HFSPLUS_LEGACY_SEQ_MAX_REPL];
};

extern u16 hfsplus_case_fold_table[];
extern u16 hfsplus_decompose_table[];
extern u16 hfsplus_compose_table[];
extern struct hfsplus_ccc_range hfsplus_ccc_table[];
extern struct hfsplus_legacy_decomp hfsplus_legacy_decomp_table[];
extern struct hfsplus_legacy_seq_fixup hfsplus_legacy_seq_fixups[];

u8 hfsplus_combining_class(u16 c);
const u16 *hfsplus_legacy_decompose(u16 uc, int *size);
void hfsplus_fixup_legacy_sequences(u16 *buf, int *len);

#endif /* _LINUX_HFSPLUS_UNICODE_H */
