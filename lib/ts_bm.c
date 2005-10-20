/*
 * lib/ts_bm.c		Boyer-Moore text search implementation
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
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
 *       http://www.cs.utexas.edu/users/moore/publications/fstrpos.pdf
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
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
	unsigned int	good_shift[0];
};

static unsigned int bm_find(struct ts_config *conf, struct ts_state *state)
{
	struct ts_bm *bm = ts_config_priv(conf);
	unsigned int i, text_len, consumed = state->offset;
	const u8 *text;
	int shift = bm->patlen, bs;

	for (;;) {
		text_len = conf->get_next_block(consumed, &text, conf, state);

		if (unlikely(text_len == 0))
			break;

		while (shift < text_len) {
			DEBUGP("Searching in position %d (%c)\n", 
				shift, text[shift]);
			for (i = 0; i < bm->patlen; i++) 
			     if (text[shift-i] != bm->pattern[bm->patlen-1-i])
				     goto next;

			/* London calling... */
			DEBUGP("found!\n");
			return consumed += (shift-(bm->patlen-1));

next:			bs = bm->bad_shift[text[shift-i]];

			/* Now jumping to... */
			shift = max_t(int, shift-i+bs, shift+bm->good_shift[i]);
		}
		consumed += text_len;
	}

	return UINT_MAX;
}

static void compute_prefix_tbl(struct ts_bm *bm, const u8 *pattern,
			       unsigned int len)
{
	int i, j, ended, l[ASIZE];

	for (i = 0; i < ASIZE; i++)
		bm->bad_shift[i] = len;
	for (i = 0; i < len - 1; i++)
		bm->bad_shift[pattern[i]] = len - 1 - i;

	/* Compute the good shift array, used to match reocurrences 
	 * of a subpattern */
	for (i = 1; i < bm->patlen; i++) {
		for (j = 0; j < bm->patlen && bm->pattern[bm->patlen - 1 - j]
				== bm->pattern[bm->patlen - 1 - i - j]; j++);
		l[i] = j;
	}  

	bm->good_shift[0] = 1;
	for (i = 1; i < bm->patlen; i++)
		bm->good_shift[i] = bm->patlen;
	for (i = bm->patlen - 1; i > 0; i--)
		bm->good_shift[l[i]] = i;
	ended = 0;
	for (i = 0; i < bm->patlen; i++) {
		if (l[i] == bm->patlen - 1 - i)
			ended = i;
		if (ended)
			bm->good_shift[i] = ended;
	}
}

static struct ts_config *bm_init(const void *pattern, unsigned int len,
				 gfp_t gfp_mask)
{
	struct ts_config *conf;
	struct ts_bm *bm;
	unsigned int prefix_tbl_len = len * sizeof(unsigned int);
	size_t priv_size = sizeof(*bm) + len + prefix_tbl_len;

	conf = alloc_ts_config(priv_size, gfp_mask);
	if (IS_ERR(conf))
		return conf;

	bm = ts_config_priv(conf);
	bm->patlen = len;
	bm->pattern = (u8 *) bm->good_shift + prefix_tbl_len;
	compute_prefix_tbl(bm, pattern, len);
	memcpy(bm->pattern, pattern, len);

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
