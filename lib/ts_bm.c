// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lib/ts_bm.c		Boyer-Moore text search implementation
 *
 * Authors:	Pablo Neira Ayuso <pablo@eurodev.net>
 *
 * ==========================================================================
 * 
 *   Implements Boyer-Moore string matching algorithm:
 *
 *   [1] A Fast String Searching Algorithm, R.S. Boyer and Moore.
 *       Communications of the Association for Computing Machinery, 
 *       20(10), 1977, pp. 762-772.
 *       https://www.cs.utexas.edu/users/moore/publications/fstrpos.pdf
 *
 *   [2] Handbook of Exact String Matching Algorithms, Thierry Lecroq, 2004
 *       http://www-igm.univ-mlv.fr/~lecroq/string/string.pdf
 *
 *   Note: Since Boyer-Moore (BM) performs searches for matchings from right 
 *   to left, it's still possible that a matching could be spread over 
 *   multiple blocks, in that case this algorithm won't find any coincidence.
 *   
 *   If you're willing to ensure that such thing won't ever happen, use the
 *   Knuth-Pratt-Morris (KMP) implementation instead. In conclusion, choose 
 *   the proper string search algorithm depending on your setting. 
 *
 *   Say you're using the textsearch infrastructure for filtering, NIDS or 
 *   any similar security focused purpose, then go KMP. Otherwise, if you 
 *   really care about performance, say you're classifying packets to apply
 *   Quality of Service (QoS) policies, and you don't mind about possible
 *   matchings spread over multiple fragments, then go BM.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/textsearch.h>

/* Alphabet size, use ASCII */
#define ASIZE 256

#if 0
#define DEBUGP printk
#else
#define DEBUGP(args, format...)
#endif

struct ts_bm
{
	u8 *		pattern;
	unsigned int	patlen;
	unsigned int 	bad_shift[ASIZE];
	unsigned int	good_shift[];
};

static unsigned int matchpat(const u8 *pattern, unsigned int patlen,
			     const u8 *text, bool icase)
{
	unsigned int i;

	for (i = 0; i < patlen; i++) {
		u8 t = *(text-i);

		if (icase)
			t = toupper(t);

		if (t != *(pattern-i))
			break;
	}

	return i;
}

static unsigned int bm_find(struct ts_config *conf, struct ts_state *state)
{
	struct ts_bm *bm = ts_config_priv(conf);
	unsigned int i, text_len, consumed = state->offset;
	const u8 *text;
	int bs;
	const u8 icase = conf->flags & TS_IGNORECASE;

	for (;;) {
		int shift = bm->patlen - 1;

		text_len = conf->get_next_block(consumed, &text, conf, state);

		if (unlikely(text_len == 0))
			break;

		while (shift < text_len) {
			DEBUGP("Searching in position %d (%c)\n",
			       shift, text[shift]);

			i = matchpat(&bm->pattern[bm->patlen-1], bm->patlen,
				     &text[shift], icase);
			if (i == bm->patlen) {
				/* London calling... */
				DEBUGP("found!\n");
				return consumed + (shift-(bm->patlen-1));
			}

			bs = bm->bad_shift[text[shift-i]];

			/* Now jumping to... */
			shift = max_t(int, shift-i+bs, shift+bm->good_shift[i]);
		}
		consumed += text_len;
	}

	return UINT_MAX;
}

static int subpattern(u8 *pattern, int i, int j, int g)
{
	int x = i+g-1, y = j+g-1, ret = 0;

	while(pattern[x--] == pattern[y--]) {
		if (y < 0) {
			ret = 1;
			break;
		}
		if (--g == 0) {
			ret = pattern[i-1] != pattern[j-1];
			break;
		}
	}

	return ret;
}

static void compute_prefix_tbl(struct ts_bm *bm, int flags)
{
	int i, j, g;

	for (i = 0; i < ASIZE; i++)
		bm->bad_shift[i] = bm->patlen;
	for (i = 0; i < bm->patlen - 1; i++) {
		bm->bad_shift[bm->pattern[i]] = bm->patlen - 1 - i;
		if (flags & TS_IGNORECASE)
			bm->bad_shift[tolower(bm->pattern[i])]
			    = bm->patlen - 1 - i;
	}

	/* Compute the good shift array, used to match reocurrences 
	 * of a subpattern */
	bm->good_shift[0] = 1;
	for (i = 1; i < bm->patlen; i++)
		bm->good_shift[i] = bm->patlen;
        for (i = bm->patlen-1, g = 1; i > 0; g++, i--) {
		for (j = i-1; j >= 1-g ; j--)
			if (subpattern(bm->pattern, i, j, g)) {
				bm->good_shift[g] = bm->patlen-j-g;
				break;
			}
	}
}

static struct ts_config *bm_init(const void *pattern, unsigned int len,
				 gfp_t gfp_mask, int flags)
{
	struct ts_config *conf;
	struct ts_bm *bm;
	int i;
	unsigned int prefix_tbl_len = len * sizeof(unsigned int);
	size_t priv_size = sizeof(*bm) + len + prefix_tbl_len;

	conf = alloc_ts_config(priv_size, gfp_mask);
	if (IS_ERR(conf))
		return conf;

	conf->flags = flags;
	bm = ts_config_priv(conf);
	bm->patlen = len;
	bm->pattern = (u8 *) bm->good_shift + prefix_tbl_len;
	if (flags & TS_IGNORECASE)
		for (i = 0; i < len; i++)
			bm->pattern[i] = toupper(((u8 *)pattern)[i]);
	else
		memcpy(bm->pattern, pattern, len);
	compute_prefix_tbl(bm, flags);

	return conf;
}

static void *bm_get_pattern(struct ts_config *conf)
{
	struct ts_bm *bm = ts_config_priv(conf);
	return bm->pattern;
}

static unsigned int bm_get_pattern_len(struct ts_config *conf)
{
	struct ts_bm *bm = ts_config_priv(conf);
	return bm->patlen;
}

static struct ts_ops bm_ops = {
	.name		  = "bm",
	.find		  = bm_find,
	.init		  = bm_init,
	.get_pattern	  = bm_get_pattern,
	.get_pattern_len  = bm_get_pattern_len,
	.owner		  = THIS_MODULE,
	.list		  = LIST_HEAD_INIT(bm_ops.list)
};

static int __init init_bm(void)
{
	return textsearch_register(&bm_ops);
}

static void __exit exit_bm(void)
{
	textsearch_unregister(&bm_ops);
}

MODULE_LICENSE("GPL");

module_init(init_bm);
module_exit(exit_bm);
