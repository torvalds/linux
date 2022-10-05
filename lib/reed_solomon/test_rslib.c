// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for Generic Reed Solomon encoder / decoder library
 *
 * Written by Ferdinand Blomqvist
 * Based on previous work by Phil Karn, KA9Q
 */
#include <linux/rslib.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/slab.h>

enum verbosity {
	V_SILENT,
	V_PROGRESS,
	V_CSUMMARY
};

enum method {
	CORR_BUFFER,
	CALLER_SYNDROME,
	IN_PLACE
};

#define __param(type, name, init, msg)		\
	static type name = init;		\
	module_param(name, type, 0444);		\
	MODULE_PARM_DESC(name, msg)

__param(int, v, V_PROGRESS, "Verbosity level");
__param(int, ewsc, 1, "Erasures without symbol corruption");
__param(int, bc, 1, "Test for correct behaviour beyond error correction capacity");

struct etab {
	int	symsize;
	int	genpoly;
	int	fcs;
	int	prim;
	int	nroots;
	int	ntrials;
};

/* List of codes to test */
static struct etab Tab[] = {
	{2,	0x7,	1,	1,	1,	100000	},
	{3,	0xb,	1,	1,	2,	100000	},
	{3,	0xb,	1,	1,	3,	100000	},
	{3,	0xb,	2,	1,	4,	100000	},
	{4,	0x13,	1,	1,	4,	10000	},
	{5,	0x25,	1,	1,	6,	1000	},
	{6,	0x43,	3,	1,	8,	1000	},
	{7,	0x89,	1,	1,	14,	500	},
	{8,	0x11d,	1,	1,	30,	100	},
	{8,	0x187,	112,	11,	32,	100	},
	{9,	0x211,	1,	1,	33,	80	},
	{0, 0, 0, 0, 0, 0},
};


struct estat {
	int	dwrong;
	int	irv;
	int	wepos;
	int	nwords;
};

struct bcstat {
	int	rfail;
	int	rsuccess;
	int	noncw;
	int	nwords;
};

struct wspace {
	uint16_t	*c;		/* sent codeword */
	uint16_t	*r;		/* received word */
	uint16_t	*s;		/* syndrome */
	uint16_t	*corr;		/* correction buffer */
	int		*errlocs;
	int		*derrlocs;
};

struct pad {
	int	mult;
	int	shift;
};

static struct pad pad_coef[] = {
	{ 0, 0 },
	{ 1, 2 },
	{ 1, 1 },
	{ 3, 2 },
	{ 1, 0 },
};

static void free_ws(struct wspace *ws)
{
	if (!ws)
		return;

	kfree(ws->errlocs);
	kfree(ws->c);
	kfree(ws);
}

static struct wspace *alloc_ws(struct rs_codec *rs)
{
	int nroots = rs->nroots;
	struct wspace *ws;
	int nn = rs->nn;

	ws = kzalloc(sizeof(*ws), GFP_KERNEL);
	if (!ws)
		return NULL;

	ws->c = kmalloc_array(2 * (nn + nroots),
				sizeof(uint16_t), GFP_KERNEL);
	if (!ws->c)
		goto err;

	ws->r = ws->c + nn;
	ws->s = ws->r + nn;
	ws->corr = ws->s + nroots;

	ws->errlocs = kmalloc_array(nn + nroots, sizeof(int), GFP_KERNEL);
	if (!ws->errlocs)
		goto err;

	ws->derrlocs = ws->errlocs + nn;
	return ws;

err:
	free_ws(ws);
	return NULL;
}


/*
 * Generates a random codeword and stores it in c. Generates random errors and
 * erasures, and stores the random word with errors in r. Erasure positions are
 * stored in derrlocs, while errlocs has one of three values in every position:
 *
 * 0 if there is no error in this position;
 * 1 if there is a symbol error in this position;
 * 2 if there is an erasure without symbol corruption.
 *
 * Returns the number of corrupted symbols.
 */
static int get_rcw_we(struct rs_control *rs, struct wspace *ws,
			int len, int errs, int eras)
{
	int nroots = rs->codec->nroots;
	int *derrlocs = ws->derrlocs;
	int *errlocs = ws->errlocs;
	int dlen = len - nroots;
	int nn = rs->codec->nn;
	uint16_t *c = ws->c;
	uint16_t *r = ws->r;
	int errval;
	int errloc;
	int i;

	/* Load c with random data and encode */
	for (i = 0; i < dlen; i++)
		c[i] = prandom_u32() & nn;

	memset(c + dlen, 0, nroots * sizeof(*c));
	encode_rs16(rs, c, dlen, c + dlen, 0);

	/* Make copyand add errors and erasures */
	memcpy(r, c, len * sizeof(*r));
	memset(errlocs, 0, len * sizeof(*errlocs));
	memset(derrlocs, 0, nroots * sizeof(*derrlocs));

	/* Generating random errors */
	for (i = 0; i < errs; i++) {
		do {
			/* Error value must be nonzero */
			errval = prandom_u32() & nn;
		} while (errval == 0);

		do {
			/* Must not choose the same location twice */
			errloc = prandom_u32_max(len);
		} while (errlocs[errloc] != 0);

		errlocs[errloc] = 1;
		r[errloc] ^= errval;
	}

	/* Generating random erasures */
	for (i = 0; i < eras; i++) {
		do {
			/* Must not choose the same location twice */
			errloc = prandom_u32_max(len);
		} while (errlocs[errloc] != 0);

		derrlocs[i] = errloc;

		if (ewsc && prandom_u32_max(2)) {
			/* Erasure with the symbol intact */
			errlocs[errloc] = 2;
		} else {
			/* Erasure with corrupted symbol */
			do {
				/* Error value must be nonzero */
				errval = prandom_u32() & nn;
			} while (errval == 0);

			errlocs[errloc] = 1;
			r[errloc] ^= errval;
			errs++;
		}
	}

	return errs;
}

static void fix_err(uint16_t *data, int nerrs, uint16_t *corr, int *errlocs)
{
	int i;

	for (i = 0; i < nerrs; i++)
		data[errlocs[i]] ^= corr[i];
}

static void compute_syndrome(struct rs_control *rsc, uint16_t *data,
				int len, uint16_t *syn)
{
	struct rs_codec *rs = rsc->codec;
	uint16_t *alpha_to = rs->alpha_to;
	uint16_t *index_of = rs->index_of;
	int nroots = rs->nroots;
	int prim = rs->prim;
	int fcr = rs->fcr;
	int i, j;

	/* Calculating syndrome */
	for (i = 0; i < nroots; i++) {
		syn[i] = data[0];
		for (j = 1; j < len; j++) {
			if (syn[i] == 0) {
				syn[i] = data[j];
			} else {
				syn[i] = data[j] ^
					alpha_to[rs_modnn(rs, index_of[syn[i]]
						+ (fcr + i) * prim)];
			}
		}
	}

	/* Convert to index form */
	for (i = 0; i < nroots; i++)
		syn[i] = rs->index_of[syn[i]];
}

/* Test up to error correction capacity */
static void test_uc(struct rs_control *rs, int len, int errs,
		int eras, int trials, struct estat *stat,
		struct wspace *ws, int method)
{
	int dlen = len - rs->codec->nroots;
	int *derrlocs = ws->derrlocs;
	int *errlocs = ws->errlocs;
	uint16_t *corr = ws->corr;
	uint16_t *c = ws->c;
	uint16_t *r = ws->r;
	uint16_t *s = ws->s;
	int derrs, nerrs;
	int i, j;

	for (j = 0; j < trials; j++) {
		nerrs = get_rcw_we(rs, ws, len, errs, eras);

		switch (method) {
		case CORR_BUFFER:
			derrs = decode_rs16(rs, r, r + dlen, dlen,
					NULL, eras, derrlocs, 0, corr);
			fix_err(r, derrs, corr, derrlocs);
			break;
		case CALLER_SYNDROME:
			compute_syndrome(rs, r, len, s);
			derrs = decode_rs16(rs, NULL, NULL, dlen,
					s, eras, derrlocs, 0, corr);
			fix_err(r, derrs, corr, derrlocs);
			break;
		case IN_PLACE:
			derrs = decode_rs16(rs, r, r + dlen, dlen,
					NULL, eras, derrlocs, 0, NULL);
			break;
		default:
			continue;
		}

		if (derrs != nerrs)
			stat->irv++;

		if (method != IN_PLACE) {
			for (i = 0; i < derrs; i++) {
				if (errlocs[derrlocs[i]] != 1)
					stat->wepos++;
			}
		}

		if (memcmp(r, c, len * sizeof(*r)))
			stat->dwrong++;
	}
	stat->nwords += trials;
}

static int ex_rs_helper(struct rs_control *rs, struct wspace *ws,
			int len, int trials, int method)
{
	static const char * const desc[] = {
		"Testing correction buffer interface...",
		"Testing with caller provided syndrome...",
		"Testing in-place interface..."
	};

	struct estat stat = {0, 0, 0, 0};
	int nroots = rs->codec->nroots;
	int errs, eras, retval;

	if (v >= V_PROGRESS)
		pr_info("  %s\n", desc[method]);

	for (errs = 0; errs <= nroots / 2; errs++)
		for (eras = 0; eras <= nroots - 2 * errs; eras++)
			test_uc(rs, len, errs, eras, trials, &stat, ws, method);

	if (v >= V_CSUMMARY) {
		pr_info("    Decodes wrong:        %d / %d\n",
				stat.dwrong, stat.nwords);
		pr_info("    Wrong return value:   %d / %d\n",
				stat.irv, stat.nwords);
		if (method != IN_PLACE)
			pr_info("    Wrong error position: %d\n", stat.wepos);
	}

	retval = stat.dwrong + stat.wepos + stat.irv;
	if (retval && v >= V_PROGRESS)
		pr_warn("    FAIL: %d decoding failures!\n", retval);

	return retval;
}

static int exercise_rs(struct rs_control *rs, struct wspace *ws,
		       int len, int trials)
{

	int retval = 0;
	int i;

	if (v >= V_PROGRESS)
		pr_info("Testing up to error correction capacity...\n");

	for (i = 0; i <= IN_PLACE; i++)
		retval |= ex_rs_helper(rs, ws, len, trials, i);

	return retval;
}

/* Tests for correct behaviour beyond error correction capacity */
static void test_bc(struct rs_control *rs, int len, int errs,
		int eras, int trials, struct bcstat *stat,
		struct wspace *ws)
{
	int nroots = rs->codec->nroots;
	int dlen = len - nroots;
	int *derrlocs = ws->derrlocs;
	uint16_t *corr = ws->corr;
	uint16_t *r = ws->r;
	int derrs, j;

	for (j = 0; j < trials; j++) {
		get_rcw_we(rs, ws, len, errs, eras);
		derrs = decode_rs16(rs, r, r + dlen, dlen,
				NULL, eras, derrlocs, 0, corr);
		fix_err(r, derrs, corr, derrlocs);

		if (derrs >= 0) {
			stat->rsuccess++;

			/*
			 * We check that the returned word is actually a
			 * codeword. The obvious way to do this would be to
			 * compute the syndrome, but we don't want to replicate
			 * that code here. However, all the codes are in
			 * systematic form, and therefore we can encode the
			 * returned word, and see whether the parity changes or
			 * not.
			 */
			memset(corr, 0, nroots * sizeof(*corr));
			encode_rs16(rs, r, dlen, corr, 0);

			if (memcmp(r + dlen, corr, nroots * sizeof(*corr)))
				stat->noncw++;
		} else {
			stat->rfail++;
		}
	}
	stat->nwords += trials;
}

static int exercise_rs_bc(struct rs_control *rs, struct wspace *ws,
			  int len, int trials)
{
	struct bcstat stat = {0, 0, 0, 0};
	int nroots = rs->codec->nroots;
	int errs, eras, cutoff;

	if (v >= V_PROGRESS)
		pr_info("Testing beyond error correction capacity...\n");

	for (errs = 1; errs <= nroots; errs++) {
		eras = nroots - 2 * errs + 1;
		if (eras < 0)
			eras = 0;

		cutoff = nroots <= len - errs ? nroots : len - errs;
		for (; eras <= cutoff; eras++)
			test_bc(rs, len, errs, eras, trials, &stat, ws);
	}

	if (v >= V_CSUMMARY) {
		pr_info("  decoder gives up:        %d / %d\n",
				stat.rfail, stat.nwords);
		pr_info("  decoder returns success: %d / %d\n",
				stat.rsuccess, stat.nwords);
		pr_info("    not a codeword:        %d / %d\n",
				stat.noncw, stat.rsuccess);
	}

	if (stat.noncw && v >= V_PROGRESS)
		pr_warn("    FAIL: %d silent failures!\n", stat.noncw);

	return stat.noncw;
}

static int run_exercise(struct etab *e)
{
	int nn = (1 << e->symsize) - 1;
	int kk = nn - e->nroots;
	struct rs_control *rsc;
	int retval = -ENOMEM;
	int max_pad = kk - 1;
	int prev_pad = -1;
	struct wspace *ws;
	int i;

	rsc = init_rs(e->symsize, e->genpoly, e->fcs, e->prim, e->nroots);
	if (!rsc)
		return retval;

	ws = alloc_ws(rsc->codec);
	if (!ws)
		goto err;

	retval = 0;
	for (i = 0; i < ARRAY_SIZE(pad_coef); i++) {
		int pad = (pad_coef[i].mult * max_pad) >> pad_coef[i].shift;
		int len = nn - pad;

		if (pad == prev_pad)
			continue;

		prev_pad = pad;
		if (v >= V_PROGRESS) {
			pr_info("Testing (%d,%d)_%d code...\n",
					len, kk - pad, nn + 1);
		}

		retval |= exercise_rs(rsc, ws, len, e->ntrials);
		if (bc)
			retval |= exercise_rs_bc(rsc, ws, len, e->ntrials);
	}

	free_ws(ws);

err:
	free_rs(rsc);
	return retval;
}

static int __init test_rslib_init(void)
{
	int i, fail = 0;

	for (i = 0; Tab[i].symsize != 0 ; i++) {
		int retval;

		retval = run_exercise(Tab + i);
		if (retval < 0)
			return -ENOMEM;

		fail |= retval;
	}

	if (fail)
		pr_warn("rslib: test failed\n");
	else
		pr_info("rslib: test ok\n");

	return -EAGAIN; /* Fail will directly unload the module */
}

static void __exit test_rslib_exit(void)
{
}

module_init(test_rslib_init)
module_exit(test_rslib_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ferdinand Blomqvist");
MODULE_DESCRIPTION("Reed-Solomon library test");
