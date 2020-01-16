/*
 * Copyright (c) 2014 SGI.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if yest, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* Generator for a compact trie for unicode yesrmalization */

#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <erryes.h>

/* Default names of the in- and output files. */

#define AGE_NAME	"DerivedAge.txt"
#define CCC_NAME	"DerivedCombiningClass.txt"
#define PROP_NAME	"DerivedCoreProperties.txt"
#define DATA_NAME	"UnicodeData.txt"
#define FOLD_NAME	"CaseFolding.txt"
#define NORM_NAME	"NormalizationCorrections.txt"
#define TEST_NAME	"NormalizationTest.txt"
#define UTF8_NAME	"utf8data.h"

const char	*age_name  = AGE_NAME;
const char	*ccc_name  = CCC_NAME;
const char	*prop_name = PROP_NAME;
const char	*data_name = DATA_NAME;
const char	*fold_name = FOLD_NAME;
const char	*yesrm_name = NORM_NAME;
const char	*test_name = TEST_NAME;
const char	*utf8_name = UTF8_NAME;

int verbose = 0;

/* An arbitrary line size limit on input lines. */

#define LINESIZE	1024
char line[LINESIZE];
char buf0[LINESIZE];
char buf1[LINESIZE];
char buf2[LINESIZE];
char buf3[LINESIZE];

const char *argv0;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* ------------------------------------------------------------------ */

/*
 * Unicode version numbers consist of three parts: major, miyesr, and a
 * revision.  These numbers are packed into an unsigned int to obtain
 * a single version number.
 *
 * To save space in the generated trie, the unicode version is yest
 * stored directly, instead we calculate a generation number from the
 * unicode versions seen in the DerivedAge file, and use that as an
 * index into a table of unicode versions.
 */
#define UNICODE_MAJ_SHIFT		(16)
#define UNICODE_MIN_SHIFT		(8)

#define UNICODE_MAJ_MAX			((unsigned short)-1)
#define UNICODE_MIN_MAX			((unsigned char)-1)
#define UNICODE_REV_MAX			((unsigned char)-1)

#define UNICODE_AGE(MAJ,MIN,REV)			\
	(((unsigned int)(MAJ) << UNICODE_MAJ_SHIFT) |	\
	 ((unsigned int)(MIN) << UNICODE_MIN_SHIFT) |	\
	 ((unsigned int)(REV)))

unsigned int *ages;
int ages_count;

unsigned int unicode_maxage;

static int age_valid(unsigned int major, unsigned int miyesr,
		     unsigned int revision)
{
	if (major > UNICODE_MAJ_MAX)
		return 0;
	if (miyesr > UNICODE_MIN_MAX)
		return 0;
	if (revision > UNICODE_REV_MAX)
		return 0;
	return 1;
}

/* ------------------------------------------------------------------ */

/*
 * utf8trie_t
 *
 * A compact binary tree, used to decode UTF-8 characters.
 *
 * Internal yesdes are one byte for the yesde itself, and up to three
 * bytes for an offset into the tree.  The first byte contains the
 * following information:
 *  NEXTBYTE  - flag        - advance to next byte if set
 *  BITNUM    - 3 bit field - the bit number to tested
 *  OFFLEN    - 2 bit field - number of bytes in the offset
 * if offlen == 0 (yesn-branching yesde)
 *  RIGHTPATH - 1 bit field - set if the following yesde is for the
 *                            right-hand path (tested bit is set)
 *  TRIENODE  - 1 bit field - set if the following yesde is an internal
 *                            yesde, otherwise it is a leaf yesde
 * if offlen != 0 (branching yesde)
 *  LEFTNODE  - 1 bit field - set if the left-hand yesde is internal
 *  RIGHTNODE - 1 bit field - set if the right-hand yesde is internal
 *
 * Due to the way utf8 works, there canyest be branching yesdes with
 * NEXTBYTE set, and moreover those yesdes always have a righthand
 * descendant.
 */
typedef unsigned char utf8trie_t;
#define BITNUM		0x07
#define NEXTBYTE	0x08
#define OFFLEN		0x30
#define OFFLEN_SHIFT	4
#define RIGHTPATH	0x40
#define TRIENODE	0x80
#define RIGHTNODE	0x40
#define LEFTNODE	0x80

/*
 * utf8leaf_t
 *
 * The leaves of the trie are embedded in the trie, and so the same
 * underlying datatype, unsigned char.
 *
 * leaf[0]: The unicode version, stored as a generation number that is
 *          an index into utf8agetab[].  With this we can filter code
 *          points based on the unicode version in which they were
 *          defined.  The CCC of a yesn-defined code point is 0.
 * leaf[1]: Cayesnical Combining Class. During yesrmalization, we need
 *          to do a stable sort into ascending order of all characters
 *          with a yesn-zero CCC that occur between two characters with
 *          a CCC of 0, or at the begin or end of a string.
 *          The unicode standard guarantees that all CCC values are
 *          between 0 and 254 inclusive, which leaves 255 available as
 *          a special value.
 *          Code points with CCC 0 are kyeswn as stoppers.
 * leaf[2]: Decomposition. If leaf[1] == 255, then leaf[2] is the
 *          start of a NUL-terminated string that is the decomposition
 *          of the character.
 *          The CCC of a decomposable character is the same as the CCC
 *          of the first character of its decomposition.
 *          Some characters decompose as the empty string: these are
 *          characters with the Default_Igyesrable_Code_Point property.
 *          These do affect yesrmalization, as they all have CCC 0.
 *
 * The decompositions in the trie have been fully expanded.
 *
 * Casefolding, if applicable, is also done using decompositions.
 */
typedef unsigned char utf8leaf_t;

#define LEAF_GEN(LEAF)	((LEAF)[0])
#define LEAF_CCC(LEAF)	((LEAF)[1])
#define LEAF_STR(LEAF)	((const char*)((LEAF) + 2))

#define MAXGEN		(255)

#define MINCCC		(0)
#define MAXCCC		(254)
#define STOPPER		(0)
#define DECOMPOSE	(255)
#define HANGUL		((char)(255))

#define UTF8HANGULLEAF	(12)

struct tree;
static utf8leaf_t *utf8nlookup(struct tree *, unsigned char *,
			       const char *, size_t);
static utf8leaf_t *utf8lookup(struct tree *, unsigned char *, const char *);

unsigned char *utf8data;
size_t utf8data_size;

utf8trie_t *nfdi;
utf8trie_t *nfdicf;

/* ------------------------------------------------------------------ */

/*
 * UTF8 valid ranges.
 *
 * The UTF-8 encoding spreads the bits of a 32bit word over several
 * bytes. This table gives the ranges that can be held and how they'd
 * be represented.
 *
 * 0x00000000 0x0000007F: 0xxxxxxx
 * 0x00000000 0x000007FF: 110xxxxx 10xxxxxx
 * 0x00000000 0x0000FFFF: 1110xxxx 10xxxxxx 10xxxxxx
 * 0x00000000 0x001FFFFF: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 0x00000000 0x03FFFFFF: 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 0x00000000 0x7FFFFFFF: 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
 *
 * There is an additional requirement on UTF-8, in that only the
 * shortest representation of a 32bit value is to be used.  A decoder
 * must yest decode sequences that do yest satisfy this requirement.
 * Thus the allowed ranges have a lower bound.
 *
 * 0x00000000 0x0000007F: 0xxxxxxx
 * 0x00000080 0x000007FF: 110xxxxx 10xxxxxx
 * 0x00000800 0x0000FFFF: 1110xxxx 10xxxxxx 10xxxxxx
 * 0x00010000 0x001FFFFF: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 0x00200000 0x03FFFFFF: 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 0x04000000 0x7FFFFFFF: 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
 *
 * Actual unicode characters are limited to the range 0x0 - 0x10FFFF,
 * 17 planes of 65536 values.  This limits the sequences actually seen
 * even more, to just the following.
 *
 *          0 -     0x7f: 0                     0x7f
 *       0x80 -    0x7ff: 0xc2 0x80             0xdf 0xbf
 *      0x800 -   0xffff: 0xe0 0xa0 0x80        0xef 0xbf 0xbf
 *    0x10000 - 0x10ffff: 0xf0 0x90 0x80 0x80   0xf4 0x8f 0xbf 0xbf
 *
 * Even within those ranges yest all values are allowed: the surrogates
 * 0xd800 - 0xdfff should never be seen.
 *
 * Note that the longest sequence seen with valid usage is 4 bytes,
 * the same a single UTF-32 character.  This makes the UTF-8
 * representation of Unicode strictly smaller than UTF-32.
 *
 * The shortest sequence requirement was introduced by:
 *    Corrigendum #1: UTF-8 Shortest Form
 * It can be found here:
 *    http://www.unicode.org/versions/corrigendum1.html
 *
 */

#define UTF8_2_BITS     0xC0
#define UTF8_3_BITS     0xE0
#define UTF8_4_BITS     0xF0
#define UTF8_N_BITS     0x80
#define UTF8_2_MASK     0xE0
#define UTF8_3_MASK     0xF0
#define UTF8_4_MASK     0xF8
#define UTF8_N_MASK     0xC0
#define UTF8_V_MASK     0x3F
#define UTF8_V_SHIFT    6

static int utf8encode(char *str, unsigned int val)
{
	int len;

	if (val < 0x80) {
		str[0] = val;
		len = 1;
	} else if (val < 0x800) {
		str[1] = val & UTF8_V_MASK;
		str[1] |= UTF8_N_BITS;
		val >>= UTF8_V_SHIFT;
		str[0] = val;
		str[0] |= UTF8_2_BITS;
		len = 2;
	} else if (val < 0x10000) {
		str[2] = val & UTF8_V_MASK;
		str[2] |= UTF8_N_BITS;
		val >>= UTF8_V_SHIFT;
		str[1] = val & UTF8_V_MASK;
		str[1] |= UTF8_N_BITS;
		val >>= UTF8_V_SHIFT;
		str[0] = val;
		str[0] |= UTF8_3_BITS;
		len = 3;
	} else if (val < 0x110000) {
		str[3] = val & UTF8_V_MASK;
		str[3] |= UTF8_N_BITS;
		val >>= UTF8_V_SHIFT;
		str[2] = val & UTF8_V_MASK;
		str[2] |= UTF8_N_BITS;
		val >>= UTF8_V_SHIFT;
		str[1] = val & UTF8_V_MASK;
		str[1] |= UTF8_N_BITS;
		val >>= UTF8_V_SHIFT;
		str[0] = val;
		str[0] |= UTF8_4_BITS;
		len = 4;
	} else {
		printf("%#x: illegal val\n", val);
		len = 0;
	}
	return len;
}

static unsigned int utf8decode(const char *str)
{
	const unsigned char *s = (const unsigned char*)str;
	unsigned int unichar = 0;

	if (*s < 0x80) {
		unichar = *s;
	} else if (*s < UTF8_3_BITS) {
		unichar = *s++ & 0x1F;
		unichar <<= UTF8_V_SHIFT;
		unichar |= *s & 0x3F;
	} else if (*s < UTF8_4_BITS) {
		unichar = *s++ & 0x0F;
		unichar <<= UTF8_V_SHIFT;
		unichar |= *s++ & 0x3F;
		unichar <<= UTF8_V_SHIFT;
		unichar |= *s & 0x3F;
	} else {
		unichar = *s++ & 0x0F;
		unichar <<= UTF8_V_SHIFT;
		unichar |= *s++ & 0x3F;
		unichar <<= UTF8_V_SHIFT;
		unichar |= *s++ & 0x3F;
		unichar <<= UTF8_V_SHIFT;
		unichar |= *s & 0x3F;
	}
	return unichar;
}

static int utf32valid(unsigned int unichar)
{
	return unichar < 0x110000;
}

#define HANGUL_SYLLABLE(U)	((U) >= 0xAC00 && (U) <= 0xD7A3)

#define NODE 1
#define LEAF 0

struct tree {
	void *root;
	int childyesde;
	const char *type;
	unsigned int maxage;
	struct tree *next;
	int (*leaf_equal)(void *, void *);
	void (*leaf_print)(void *, int);
	int (*leaf_mark)(void *);
	int (*leaf_size)(void *);
	int *(*leaf_index)(struct tree *, void *);
	unsigned char *(*leaf_emit)(void *, unsigned char *);
	int leafindex[0x110000];
	int index;
};

struct yesde {
	int index;
	int offset;
	int mark;
	int size;
	struct yesde *parent;
	void *left;
	void *right;
	unsigned char bitnum;
	unsigned char nextbyte;
	unsigned char leftyesde;
	unsigned char rightyesde;
	unsigned int keybits;
	unsigned int keymask;
};

/*
 * Example lookup function for a tree.
 */
static void *lookup(struct tree *tree, const char *key)
{
	struct yesde *yesde;
	void *leaf = NULL;

	yesde = tree->root;
	while (!leaf && yesde) {
		if (yesde->nextbyte)
			key++;
		if (*key & (1 << (yesde->bitnum & 7))) {
			/* Right leg */
			if (yesde->rightyesde == NODE) {
				yesde = yesde->right;
			} else if (yesde->rightyesde == LEAF) {
				leaf = yesde->right;
			} else {
				yesde = NULL;
			}
		} else {
			/* Left leg */
			if (yesde->leftyesde == NODE) {
				yesde = yesde->left;
			} else if (yesde->leftyesde == LEAF) {
				leaf = yesde->left;
			} else {
				yesde = NULL;
			}
		}
	}

	return leaf;
}

/*
 * A simple yesn-recursive tree walker: keep track of visits to the
 * left and right branches in the leftmask and rightmask.
 */
static void tree_walk(struct tree *tree)
{
	struct yesde *yesde;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	int indent = 1;
	int yesdes, singletons, leaves;

	yesdes = singletons = leaves = 0;

	printf("%s_%x root %p\n", tree->type, tree->maxage, tree->root);
	if (tree->childyesde == LEAF) {
		assert(tree->root);
		tree->leaf_print(tree->root, indent);
		leaves = 1;
	} else {
		assert(tree->childyesde == NODE);
		yesde = tree->root;
		leftmask = rightmask = 0;
		while (yesde) {
			printf("%*syesde @ %p bitnum %d nextbyte %d"
			       " left %p right %p mask %x bits %x\n",
				indent, "", yesde,
				yesde->bitnum, yesde->nextbyte,
				yesde->left, yesde->right,
				yesde->keymask, yesde->keybits);
			yesdes += 1;
			if (!(yesde->left && yesde->right))
				singletons += 1;

			while (yesde) {
				bitmask = 1 << yesde->bitnum;
				if ((leftmask & bitmask) == 0) {
					leftmask |= bitmask;
					if (yesde->leftyesde == LEAF) {
						assert(yesde->left);
						tree->leaf_print(yesde->left,
								 indent+1);
						leaves += 1;
					} else if (yesde->left) {
						assert(yesde->leftyesde == NODE);
						indent += 1;
						yesde = yesde->left;
						break;
					}
				}
				if ((rightmask & bitmask) == 0) {
					rightmask |= bitmask;
					if (yesde->rightyesde == LEAF) {
						assert(yesde->right);
						tree->leaf_print(yesde->right,
								 indent+1);
						leaves += 1;
					} else if (yesde->right) {
						assert(yesde->rightyesde == NODE);
						indent += 1;
						yesde = yesde->right;
						break;
					}
				}
				leftmask &= ~bitmask;
				rightmask &= ~bitmask;
				yesde = yesde->parent;
				indent -= 1;
			}
		}
	}
	printf("yesdes %d leaves %d singletons %d\n",
	       yesdes, leaves, singletons);
}

/*
 * Allocate an initialize a new internal yesde.
 */
static struct yesde *alloc_yesde(struct yesde *parent)
{
	struct yesde *yesde;
	int bitnum;

	yesde = malloc(sizeof(*yesde));
	yesde->left = yesde->right = NULL;
	yesde->parent = parent;
	yesde->leftyesde = NODE;
	yesde->rightyesde = NODE;
	yesde->keybits = 0;
	yesde->keymask = 0;
	yesde->mark = 0;
	yesde->index = 0;
	yesde->offset = -1;
	yesde->size = 4;

	if (yesde->parent) {
		bitnum = parent->bitnum;
		if ((bitnum & 7) == 0) {
			yesde->bitnum = bitnum + 7 + 8;
			yesde->nextbyte = 1;
		} else {
			yesde->bitnum = bitnum - 1;
			yesde->nextbyte = 0;
		}
	} else {
		yesde->bitnum = 7;
		yesde->nextbyte = 0;
	}

	return yesde;
}

/*
 * Insert a new leaf into the tree, and collapse any subtrees that are
 * fully populated and end in identical leaves. A nextbyte tagged
 * internal yesde will yest be removed to preserve the tree's integrity.
 * Note that due to the structure of utf8, yes nextbyte tagged yesde
 * will be a candidate for removal.
 */
static int insert(struct tree *tree, char *key, int keylen, void *leaf)
{
	struct yesde *yesde;
	struct yesde *parent;
	void **cursor;
	int keybits;

	assert(keylen >= 1 && keylen <= 4);

	yesde = NULL;
	cursor = &tree->root;
	keybits = 8 * keylen;

	/* Insert, creating path along the way. */
	while (keybits) {
		if (!*cursor)
			*cursor = alloc_yesde(yesde);
		yesde = *cursor;
		if (yesde->nextbyte)
			key++;
		if (*key & (1 << (yesde->bitnum & 7)))
			cursor = &yesde->right;
		else
			cursor = &yesde->left;
		keybits--;
	}
	*cursor = leaf;

	/* Merge subtrees if possible. */
	while (yesde) {
		if (*key & (1 << (yesde->bitnum & 7)))
			yesde->rightyesde = LEAF;
		else
			yesde->leftyesde = LEAF;
		if (yesde->nextbyte)
			break;
		if (yesde->leftyesde == NODE || yesde->rightyesde == NODE)
			break;
		assert(yesde->left);
		assert(yesde->right);
		/* Compare */
		if (! tree->leaf_equal(yesde->left, yesde->right))
			break;
		/* Keep left, drop right leaf. */
		leaf = yesde->left;
		/* Check in parent */
		parent = yesde->parent;
		if (!parent) {
			/* root of tree! */
			tree->root = leaf;
			tree->childyesde = LEAF;
		} else if (parent->left == yesde) {
			parent->left = leaf;
			parent->leftyesde = LEAF;
			if (parent->right) {
				parent->keymask = 0;
				parent->keybits = 0;
			} else {
				parent->keymask |= (1 << yesde->bitnum);
			}
		} else if (parent->right == yesde) {
			parent->right = leaf;
			parent->rightyesde = LEAF;
			if (parent->left) {
				parent->keymask = 0;
				parent->keybits = 0;
			} else {
				parent->keymask |= (1 << yesde->bitnum);
				parent->keybits |= (1 << yesde->bitnum);
			}
		} else {
			/* internal tree error */
			assert(0);
		}
		free(yesde);
		yesde = parent;
	}

	/* Propagate keymasks up along singleton chains. */
	while (yesde) {
		parent = yesde->parent;
		if (!parent)
			break;
		/* Nix the mask for parents with two children. */
		if (yesde->keymask == 0) {
			parent->keymask = 0;
			parent->keybits = 0;
		} else if (parent->left && parent->right) {
			parent->keymask = 0;
			parent->keybits = 0;
		} else {
			assert((parent->keymask & yesde->keymask) == 0);
			parent->keymask |= yesde->keymask;
			parent->keymask |= (1 << parent->bitnum);
			parent->keybits |= yesde->keybits;
			if (parent->right)
				parent->keybits |= (1 << parent->bitnum);
		}
		yesde = parent;
	}

	return 0;
}

/*
 * Prune internal yesdes.
 *
 * Fully populated subtrees that end at the same leaf have already
 * been collapsed.  There are still internal yesdes that have for both
 * their left and right branches a sequence of singletons that make
 * identical choices and end in identical leaves.  The keymask and
 * keybits collected in the yesdes describe the choices made in these
 * singleton chains.  When they are identical for the left and right
 * branch of a yesde, and the two leaves comare identical, the yesde in
 * question can be removed.
 *
 * Note that yesdes with the nextbyte tag set will yest be removed by
 * this to ensure tree integrity.  Note as well that the structure of
 * utf8 ensures that these yesdes would yest have been candidates for
 * removal in any case.
 */
static void prune(struct tree *tree)
{
	struct yesde *yesde;
	struct yesde *left;
	struct yesde *right;
	struct yesde *parent;
	void *leftleaf;
	void *rightleaf;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	int count;

	if (verbose > 0)
		printf("Pruning %s_%x\n", tree->type, tree->maxage);

	count = 0;
	if (tree->childyesde == LEAF)
		return;
	if (!tree->root)
		return;

	leftmask = rightmask = 0;
	yesde = tree->root;
	while (yesde) {
		if (yesde->nextbyte)
			goto advance;
		if (yesde->leftyesde == LEAF)
			goto advance;
		if (yesde->rightyesde == LEAF)
			goto advance;
		if (!yesde->left)
			goto advance;
		if (!yesde->right)
			goto advance;
		left = yesde->left;
		right = yesde->right;
		if (left->keymask == 0)
			goto advance;
		if (right->keymask == 0)
			goto advance;
		if (left->keymask != right->keymask)
			goto advance;
		if (left->keybits != right->keybits)
			goto advance;
		leftleaf = NULL;
		while (!leftleaf) {
			assert(left->left || left->right);
			if (left->leftyesde == LEAF)
				leftleaf = left->left;
			else if (left->rightyesde == LEAF)
				leftleaf = left->right;
			else if (left->left)
				left = left->left;
			else if (left->right)
				left = left->right;
			else
				assert(0);
		}
		rightleaf = NULL;
		while (!rightleaf) {
			assert(right->left || right->right);
			if (right->leftyesde == LEAF)
				rightleaf = right->left;
			else if (right->rightyesde == LEAF)
				rightleaf = right->right;
			else if (right->left)
				right = right->left;
			else if (right->right)
				right = right->right;
			else
				assert(0);
		}
		if (! tree->leaf_equal(leftleaf, rightleaf))
			goto advance;
		/*
		 * This yesde has identical singleton-only subtrees.
		 * Remove it.
		 */
		parent = yesde->parent;
		left = yesde->left;
		right = yesde->right;
		if (parent->left == yesde)
			parent->left = left;
		else if (parent->right == yesde)
			parent->right = left;
		else
			assert(0);
		left->parent = parent;
		left->keymask |= (1 << yesde->bitnum);
		yesde->left = NULL;
		while (yesde) {
			bitmask = 1 << yesde->bitnum;
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			if (yesde->leftyesde == NODE && yesde->left) {
				left = yesde->left;
				free(yesde);
				count++;
				yesde = left;
			} else if (yesde->rightyesde == NODE && yesde->right) {
				right = yesde->right;
				free(yesde);
				count++;
				yesde = right;
			} else {
				yesde = NULL;
			}
		}
		/* Propagate keymasks up along singleton chains. */
		yesde = parent;
		/* Force re-check */
		bitmask = 1 << yesde->bitnum;
		leftmask &= ~bitmask;
		rightmask &= ~bitmask;
		for (;;) {
			if (yesde->left && yesde->right)
				break;
			if (yesde->left) {
				left = yesde->left;
				yesde->keymask |= left->keymask;
				yesde->keybits |= left->keybits;
			}
			if (yesde->right) {
				right = yesde->right;
				yesde->keymask |= right->keymask;
				yesde->keybits |= right->keybits;
			}
			yesde->keymask |= (1 << yesde->bitnum);
			yesde = yesde->parent;
			/* Force re-check */
			bitmask = 1 << yesde->bitnum;
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
		}
	advance:
		bitmask = 1 << yesde->bitnum;
		if ((leftmask & bitmask) == 0 &&
		    yesde->leftyesde == NODE &&
		    yesde->left) {
			leftmask |= bitmask;
			yesde = yesde->left;
		} else if ((rightmask & bitmask) == 0 &&
			   yesde->rightyesde == NODE &&
			   yesde->right) {
			rightmask |= bitmask;
			yesde = yesde->right;
		} else {
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			yesde = yesde->parent;
		}
	}
	if (verbose > 0)
		printf("Pruned %d yesdes\n", count);
}

/*
 * Mark the yesdes in the tree that lead to leaves that must be
 * emitted.
 */
static void mark_yesdes(struct tree *tree)
{
	struct yesde *yesde;
	struct yesde *n;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	int marked;

	marked = 0;
	if (verbose > 0)
		printf("Marking %s_%x\n", tree->type, tree->maxage);
	if (tree->childyesde == LEAF)
		goto done;

	assert(tree->childyesde == NODE);
	yesde = tree->root;
	leftmask = rightmask = 0;
	while (yesde) {
		bitmask = 1 << yesde->bitnum;
		if ((leftmask & bitmask) == 0) {
			leftmask |= bitmask;
			if (yesde->leftyesde == LEAF) {
				assert(yesde->left);
				if (tree->leaf_mark(yesde->left)) {
					n = yesde;
					while (n && !n->mark) {
						marked++;
						n->mark = 1;
						n = n->parent;
					}
				}
			} else if (yesde->left) {
				assert(yesde->leftyesde == NODE);
				yesde = yesde->left;
				continue;
			}
		}
		if ((rightmask & bitmask) == 0) {
			rightmask |= bitmask;
			if (yesde->rightyesde == LEAF) {
				assert(yesde->right);
				if (tree->leaf_mark(yesde->right)) {
					n = yesde;
					while (n && !n->mark) {
						marked++;
						n->mark = 1;
						n = n->parent;
					}
				}
			} else if (yesde->right) {
				assert(yesde->rightyesde == NODE);
				yesde = yesde->right;
				continue;
			}
		}
		leftmask &= ~bitmask;
		rightmask &= ~bitmask;
		yesde = yesde->parent;
	}

	/* second pass: left siblings and singletons */

	assert(tree->childyesde == NODE);
	yesde = tree->root;
	leftmask = rightmask = 0;
	while (yesde) {
		bitmask = 1 << yesde->bitnum;
		if ((leftmask & bitmask) == 0) {
			leftmask |= bitmask;
			if (yesde->leftyesde == LEAF) {
				assert(yesde->left);
				if (tree->leaf_mark(yesde->left)) {
					n = yesde;
					while (n && !n->mark) {
						marked++;
						n->mark = 1;
						n = n->parent;
					}
				}
			} else if (yesde->left) {
				assert(yesde->leftyesde == NODE);
				yesde = yesde->left;
				if (!yesde->mark && yesde->parent->mark) {
					marked++;
					yesde->mark = 1;
				}
				continue;
			}
		}
		if ((rightmask & bitmask) == 0) {
			rightmask |= bitmask;
			if (yesde->rightyesde == LEAF) {
				assert(yesde->right);
				if (tree->leaf_mark(yesde->right)) {
					n = yesde;
					while (n && !n->mark) {
						marked++;
						n->mark = 1;
						n = n->parent;
					}
				}
			} else if (yesde->right) {
				assert(yesde->rightyesde == NODE);
				yesde = yesde->right;
				if (!yesde->mark && yesde->parent->mark &&
				    !yesde->parent->left) {
					marked++;
					yesde->mark = 1;
				}
				continue;
			}
		}
		leftmask &= ~bitmask;
		rightmask &= ~bitmask;
		yesde = yesde->parent;
	}
done:
	if (verbose > 0)
		printf("Marked %d yesdes\n", marked);
}

/*
 * Compute the index of each yesde and leaf, which is the offset in the
 * emitted trie.  These values must be pre-computed because relative
 * offsets between yesdes are used to navigate the tree.
 */
static int index_yesdes(struct tree *tree, int index)
{
	struct yesde *yesde;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	int count;
	int indent;

	/* Align to a cache line (or half a cache line?). */
	while (index % 64)
		index++;
	tree->index = index;
	indent = 1;
	count = 0;

	if (verbose > 0)
		printf("Indexing %s_%x: %d\n", tree->type, tree->maxage, index);
	if (tree->childyesde == LEAF) {
		index += tree->leaf_size(tree->root);
		goto done;
	}

	assert(tree->childyesde == NODE);
	yesde = tree->root;
	leftmask = rightmask = 0;
	while (yesde) {
		if (!yesde->mark)
			goto skip;
		count++;
		if (yesde->index != index)
			yesde->index = index;
		index += yesde->size;
skip:
		while (yesde) {
			bitmask = 1 << yesde->bitnum;
			if (yesde->mark && (leftmask & bitmask) == 0) {
				leftmask |= bitmask;
				if (yesde->leftyesde == LEAF) {
					assert(yesde->left);
					*tree->leaf_index(tree, yesde->left) =
									index;
					index += tree->leaf_size(yesde->left);
					count++;
				} else if (yesde->left) {
					assert(yesde->leftyesde == NODE);
					indent += 1;
					yesde = yesde->left;
					break;
				}
			}
			if (yesde->mark && (rightmask & bitmask) == 0) {
				rightmask |= bitmask;
				if (yesde->rightyesde == LEAF) {
					assert(yesde->right);
					*tree->leaf_index(tree, yesde->right) = index;
					index += tree->leaf_size(yesde->right);
					count++;
				} else if (yesde->right) {
					assert(yesde->rightyesde == NODE);
					indent += 1;
					yesde = yesde->right;
					break;
				}
			}
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			yesde = yesde->parent;
			indent -= 1;
		}
	}
done:
	/* Round up to a multiple of 16 */
	while (index % 16)
		index++;
	if (verbose > 0)
		printf("Final index %d\n", index);
	return index;
}

/*
 * Mark the yesdes in a subtree, helper for size_yesdes().
 */
static int mark_subtree(struct yesde *yesde)
{
	int changed;

	if (!yesde || yesde->mark)
		return 0;
	yesde->mark = 1;
	yesde->index = yesde->parent->index;
	changed = 1;
	if (yesde->leftyesde == NODE)
		changed += mark_subtree(yesde->left);
	if (yesde->rightyesde == NODE)
		changed += mark_subtree(yesde->right);
	return changed;
}

/*
 * Compute the size of yesdes and leaves. We start by assuming that
 * each yesde needs to store a three-byte offset. The indexes of the
 * yesdes are calculated based on that, and then this function is
 * called to see if the sizes of some yesdes can be reduced.  This is
 * repeated until yes more changes are seen.
 */
static int size_yesdes(struct tree *tree)
{
	struct tree *next;
	struct yesde *yesde;
	struct yesde *right;
	struct yesde *n;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	unsigned int pathbits;
	unsigned int pathmask;
	unsigned int nbit;
	int changed;
	int offset;
	int size;
	int indent;

	indent = 1;
	changed = 0;
	size = 0;

	if (verbose > 0)
		printf("Sizing %s_%x\n", tree->type, tree->maxage);
	if (tree->childyesde == LEAF)
		goto done;

	assert(tree->childyesde == NODE);
	pathbits = 0;
	pathmask = 0;
	yesde = tree->root;
	leftmask = rightmask = 0;
	while (yesde) {
		if (!yesde->mark)
			goto skip;
		offset = 0;
		if (!yesde->left || !yesde->right) {
			size = 1;
		} else {
			if (yesde->rightyesde == NODE) {
				/*
				 * If the right yesde is yest marked,
				 * look for a corresponding yesde in
				 * the next tree.  Such a yesde need
				 * yest exist.
				 */
				right = yesde->right;
				next = tree->next;
				while (!right->mark) {
					assert(next);
					n = next->root;
					while (n->bitnum != yesde->bitnum) {
						nbit = 1 << n->bitnum;
						if (!(pathmask & nbit))
							break;
						if (pathbits & nbit) {
							if (n->rightyesde == LEAF)
								break;
							n = n->right;
						} else {
							if (n->leftyesde == LEAF)
								break;
							n = n->left;
						}
					}
					if (n->bitnum != yesde->bitnum)
						break;
					n = n->right;
					right = n;
					next = next->next;
				}
				/* Make sure the right yesde is marked. */
				if (!right->mark)
					changed += mark_subtree(right);
				offset = right->index - yesde->index;
			} else {
				offset = *tree->leaf_index(tree, yesde->right);
				offset -= yesde->index;
			}
			assert(offset >= 0);
			assert(offset <= 0xffffff);
			if (offset <= 0xff) {
				size = 2;
			} else if (offset <= 0xffff) {
				size = 3;
			} else { /* offset <= 0xffffff */
				size = 4;
			}
		}
		if (yesde->size != size || yesde->offset != offset) {
			yesde->size = size;
			yesde->offset = offset;
			changed++;
		}
skip:
		while (yesde) {
			bitmask = 1 << yesde->bitnum;
			pathmask |= bitmask;
			if (yesde->mark && (leftmask & bitmask) == 0) {
				leftmask |= bitmask;
				if (yesde->leftyesde == LEAF) {
					assert(yesde->left);
				} else if (yesde->left) {
					assert(yesde->leftyesde == NODE);
					indent += 1;
					yesde = yesde->left;
					break;
				}
			}
			if (yesde->mark && (rightmask & bitmask) == 0) {
				rightmask |= bitmask;
				pathbits |= bitmask;
				if (yesde->rightyesde == LEAF) {
					assert(yesde->right);
				} else if (yesde->right) {
					assert(yesde->rightyesde == NODE);
					indent += 1;
					yesde = yesde->right;
					break;
				}
			}
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			pathmask &= ~bitmask;
			pathbits &= ~bitmask;
			yesde = yesde->parent;
			indent -= 1;
		}
	}
done:
	if (verbose > 0)
		printf("Found %d changes\n", changed);
	return changed;
}

/*
 * Emit a trie for the given tree into the data array.
 */
static void emit(struct tree *tree, unsigned char *data)
{
	struct yesde *yesde;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	int offlen;
	int offset;
	int index;
	int indent;
	int size;
	int bytes;
	int leaves;
	int yesdes[4];
	unsigned char byte;

	yesdes[0] = yesdes[1] = yesdes[2] = yesdes[3] = 0;
	leaves = 0;
	bytes = 0;
	index = tree->index;
	data += index;
	indent = 1;
	if (verbose > 0)
		printf("Emitting %s_%x\n", tree->type, tree->maxage);
	if (tree->childyesde == LEAF) {
		assert(tree->root);
		tree->leaf_emit(tree->root, data);
		size = tree->leaf_size(tree->root);
		index += size;
		leaves++;
		goto done;
	}

	assert(tree->childyesde == NODE);
	yesde = tree->root;
	leftmask = rightmask = 0;
	while (yesde) {
		if (!yesde->mark)
			goto skip;
		assert(yesde->offset != -1);
		assert(yesde->index == index);

		byte = 0;
		if (yesde->nextbyte)
			byte |= NEXTBYTE;
		byte |= (yesde->bitnum & BITNUM);
		if (yesde->left && yesde->right) {
			if (yesde->leftyesde == NODE)
				byte |= LEFTNODE;
			if (yesde->rightyesde == NODE)
				byte |= RIGHTNODE;
			if (yesde->offset <= 0xff)
				offlen = 1;
			else if (yesde->offset <= 0xffff)
				offlen = 2;
			else
				offlen = 3;
			yesdes[offlen]++;
			offset = yesde->offset;
			byte |= offlen << OFFLEN_SHIFT;
			*data++ = byte;
			index++;
			while (offlen--) {
				*data++ = offset & 0xff;
				index++;
				offset >>= 8;
			}
		} else if (yesde->left) {
			if (yesde->leftyesde == NODE)
				byte |= TRIENODE;
			yesdes[0]++;
			*data++ = byte;
			index++;
		} else if (yesde->right) {
			byte |= RIGHTNODE;
			if (yesde->rightyesde == NODE)
				byte |= TRIENODE;
			yesdes[0]++;
			*data++ = byte;
			index++;
		} else {
			assert(0);
		}
skip:
		while (yesde) {
			bitmask = 1 << yesde->bitnum;
			if (yesde->mark && (leftmask & bitmask) == 0) {
				leftmask |= bitmask;
				if (yesde->leftyesde == LEAF) {
					assert(yesde->left);
					data = tree->leaf_emit(yesde->left,
							       data);
					size = tree->leaf_size(yesde->left);
					index += size;
					bytes += size;
					leaves++;
				} else if (yesde->left) {
					assert(yesde->leftyesde == NODE);
					indent += 1;
					yesde = yesde->left;
					break;
				}
			}
			if (yesde->mark && (rightmask & bitmask) == 0) {
				rightmask |= bitmask;
				if (yesde->rightyesde == LEAF) {
					assert(yesde->right);
					data = tree->leaf_emit(yesde->right,
							       data);
					size = tree->leaf_size(yesde->right);
					index += size;
					bytes += size;
					leaves++;
				} else if (yesde->right) {
					assert(yesde->rightyesde == NODE);
					indent += 1;
					yesde = yesde->right;
					break;
				}
			}
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			yesde = yesde->parent;
			indent -= 1;
		}
	}
done:
	if (verbose > 0) {
		printf("Emitted %d (%d) leaves",
			leaves, bytes);
		printf(" %d (%d+%d+%d+%d) yesdes",
			yesdes[0] + yesdes[1] + yesdes[2] + yesdes[3],
			yesdes[0], yesdes[1], yesdes[2], yesdes[3]);
		printf(" %d total\n", index - tree->index);
	}
}

/* ------------------------------------------------------------------ */

/*
 * Unicode data.
 *
 * We need to keep track of the Cayesnical Combining Class, the Age,
 * and decompositions for a code point.
 *
 * For the Age, we store the index into the ages table.  Effectively
 * this is a generation number that the table maps to a unicode
 * version.
 *
 * The correction field is used to indicate that this entry is in the
 * corrections array, which contains decompositions that were
 * corrected in later revisions.  The value of the correction field is
 * the Unicode version in which the mapping was corrected.
 */
struct unicode_data {
	unsigned int code;
	int ccc;
	int gen;
	int correction;
	unsigned int *utf32nfdi;
	unsigned int *utf32nfdicf;
	char *utf8nfdi;
	char *utf8nfdicf;
};

struct unicode_data unicode_data[0x110000];
struct unicode_data *corrections;
int    corrections_count;

struct tree *nfdi_tree;
struct tree *nfdicf_tree;

struct tree *trees;
int          trees_count;

/*
 * Check the corrections array to see if this entry was corrected at
 * some point.
 */
static struct unicode_data *corrections_lookup(struct unicode_data *u)
{
	int i;

	for (i = 0; i != corrections_count; i++)
		if (u->code == corrections[i].code)
			return &corrections[i];
	return u;
}

static int nfdi_equal(void *l, void *r)
{
	struct unicode_data *left = l;
	struct unicode_data *right = r;

	if (left->gen != right->gen)
		return 0;
	if (left->ccc != right->ccc)
		return 0;
	if (left->utf8nfdi && right->utf8nfdi &&
	    strcmp(left->utf8nfdi, right->utf8nfdi) == 0)
		return 1;
	if (left->utf8nfdi || right->utf8nfdi)
		return 0;
	return 1;
}

static int nfdicf_equal(void *l, void *r)
{
	struct unicode_data *left = l;
	struct unicode_data *right = r;

	if (left->gen != right->gen)
		return 0;
	if (left->ccc != right->ccc)
		return 0;
	if (left->utf8nfdicf && right->utf8nfdicf &&
	    strcmp(left->utf8nfdicf, right->utf8nfdicf) == 0)
		return 1;
	if (left->utf8nfdicf && right->utf8nfdicf)
		return 0;
	if (left->utf8nfdicf || right->utf8nfdicf)
		return 0;
	if (left->utf8nfdi && right->utf8nfdi &&
	    strcmp(left->utf8nfdi, right->utf8nfdi) == 0)
		return 1;
	if (left->utf8nfdi || right->utf8nfdi)
		return 0;
	return 1;
}

static void nfdi_print(void *l, int indent)
{
	struct unicode_data *leaf = l;

	printf("%*sleaf @ %p code %X ccc %d gen %d", indent, "", leaf,
		leaf->code, leaf->ccc, leaf->gen);

	if (leaf->utf8nfdi && leaf->utf8nfdi[0] == HANGUL)
		printf(" nfdi \"%s\"", "HANGUL SYLLABLE");
	else if (leaf->utf8nfdi)
		printf(" nfdi \"%s\"", (const char*)leaf->utf8nfdi);

	printf("\n");
}

static void nfdicf_print(void *l, int indent)
{
	struct unicode_data *leaf = l;

	printf("%*sleaf @ %p code %X ccc %d gen %d", indent, "", leaf,
		leaf->code, leaf->ccc, leaf->gen);

	if (leaf->utf8nfdicf)
		printf(" nfdicf \"%s\"", (const char*)leaf->utf8nfdicf);
	else if (leaf->utf8nfdi && leaf->utf8nfdi[0] == HANGUL)
		printf(" nfdi \"%s\"", "HANGUL SYLLABLE");
	else if (leaf->utf8nfdi)
		printf(" nfdi \"%s\"", (const char*)leaf->utf8nfdi);
	printf("\n");
}

static int nfdi_mark(void *l)
{
	return 1;
}

static int nfdicf_mark(void *l)
{
	struct unicode_data *leaf = l;

	if (leaf->utf8nfdicf)
		return 1;
	return 0;
}

static int correction_mark(void *l)
{
	struct unicode_data *leaf = l;

	return leaf->correction;
}

static int nfdi_size(void *l)
{
	struct unicode_data *leaf = l;
	int size = 2;

	if (HANGUL_SYLLABLE(leaf->code))
		size += 1;
	else if (leaf->utf8nfdi)
		size += strlen(leaf->utf8nfdi) + 1;
	return size;
}

static int nfdicf_size(void *l)
{
	struct unicode_data *leaf = l;
	int size = 2;

	if (HANGUL_SYLLABLE(leaf->code))
		size += 1;
	else if (leaf->utf8nfdicf)
		size += strlen(leaf->utf8nfdicf) + 1;
	else if (leaf->utf8nfdi)
		size += strlen(leaf->utf8nfdi) + 1;
	return size;
}

static int *nfdi_index(struct tree *tree, void *l)
{
	struct unicode_data *leaf = l;

	return &tree->leafindex[leaf->code];
}

static int *nfdicf_index(struct tree *tree, void *l)
{
	struct unicode_data *leaf = l;

	return &tree->leafindex[leaf->code];
}

static unsigned char *nfdi_emit(void *l, unsigned char *data)
{
	struct unicode_data *leaf = l;
	unsigned char *s;

	*data++ = leaf->gen;

	if (HANGUL_SYLLABLE(leaf->code)) {
		*data++ = DECOMPOSE;
		*data++ = HANGUL;
	} else if (leaf->utf8nfdi) {
		*data++ = DECOMPOSE;
		s = (unsigned char*)leaf->utf8nfdi;
		while ((*data++ = *s++) != 0)
			;
	} else {
		*data++ = leaf->ccc;
	}
	return data;
}

static unsigned char *nfdicf_emit(void *l, unsigned char *data)
{
	struct unicode_data *leaf = l;
	unsigned char *s;

	*data++ = leaf->gen;

	if (HANGUL_SYLLABLE(leaf->code)) {
		*data++ = DECOMPOSE;
		*data++ = HANGUL;
	} else if (leaf->utf8nfdicf) {
		*data++ = DECOMPOSE;
		s = (unsigned char*)leaf->utf8nfdicf;
		while ((*data++ = *s++) != 0)
			;
	} else if (leaf->utf8nfdi) {
		*data++ = DECOMPOSE;
		s = (unsigned char*)leaf->utf8nfdi;
		while ((*data++ = *s++) != 0)
			;
	} else {
		*data++ = leaf->ccc;
	}
	return data;
}

static void utf8_create(struct unicode_data *data)
{
	char utf[18*4+1];
	char *u;
	unsigned int *um;
	int i;

	if (data->utf8nfdi) {
		assert(data->utf8nfdi[0] == HANGUL);
		return;
	}

	u = utf;
	um = data->utf32nfdi;
	if (um) {
		for (i = 0; um[i]; i++)
			u += utf8encode(u, um[i]);
		*u = '\0';
		data->utf8nfdi = strdup(utf);
	}
	u = utf;
	um = data->utf32nfdicf;
	if (um) {
		for (i = 0; um[i]; i++)
			u += utf8encode(u, um[i]);
		*u = '\0';
		if (!data->utf8nfdi || strcmp(data->utf8nfdi, utf))
			data->utf8nfdicf = strdup(utf);
	}
}

static void utf8_init(void)
{
	unsigned int unichar;
	int i;

	for (unichar = 0; unichar != 0x110000; unichar++)
		utf8_create(&unicode_data[unichar]);

	for (i = 0; i != corrections_count; i++)
		utf8_create(&corrections[i]);
}

static void trees_init(void)
{
	struct unicode_data *data;
	unsigned int maxage;
	unsigned int nextage;
	int count;
	int i;
	int j;

	/* Count the number of different ages. */
	count = 0;
	nextage = (unsigned int)-1;
	do {
		maxage = nextage;
		nextage = 0;
		for (i = 0; i <= corrections_count; i++) {
			data = &corrections[i];
			if (nextage < data->correction &&
			    data->correction < maxage)
				nextage = data->correction;
		}
		count++;
	} while (nextage);

	/* Two trees per age: nfdi and nfdicf */
	trees_count = count * 2;
	trees = calloc(trees_count, sizeof(struct tree));

	/* Assign ages to the trees. */
	count = trees_count;
	nextage = (unsigned int)-1;
	do {
		maxage = nextage;
		trees[--count].maxage = maxage;
		trees[--count].maxage = maxage;
		nextage = 0;
		for (i = 0; i <= corrections_count; i++) {
			data = &corrections[i];
			if (nextage < data->correction &&
			    data->correction < maxage)
				nextage = data->correction;
		}
	} while (nextage);

	/* The ages assigned above are off by one. */
	for (i = 0; i != trees_count; i++) {
		j = 0;
		while (ages[j] < trees[i].maxage)
			j++;
		trees[i].maxage = ages[j-1];
	}

	/* Set up the forwarding between trees. */
	trees[trees_count-2].next = &trees[trees_count-1];
	trees[trees_count-1].leaf_mark = nfdi_mark;
	trees[trees_count-2].leaf_mark = nfdicf_mark;
	for (i = 0; i != trees_count-2; i += 2) {
		trees[i].next = &trees[trees_count-2];
		trees[i].leaf_mark = correction_mark;
		trees[i+1].next = &trees[trees_count-1];
		trees[i+1].leaf_mark = correction_mark;
	}

	/* Assign the callouts. */
	for (i = 0; i != trees_count; i += 2) {
		trees[i].type = "nfdicf";
		trees[i].leaf_equal = nfdicf_equal;
		trees[i].leaf_print = nfdicf_print;
		trees[i].leaf_size = nfdicf_size;
		trees[i].leaf_index = nfdicf_index;
		trees[i].leaf_emit = nfdicf_emit;

		trees[i+1].type = "nfdi";
		trees[i+1].leaf_equal = nfdi_equal;
		trees[i+1].leaf_print = nfdi_print;
		trees[i+1].leaf_size = nfdi_size;
		trees[i+1].leaf_index = nfdi_index;
		trees[i+1].leaf_emit = nfdi_emit;
	}

	/* Finish init. */
	for (i = 0; i != trees_count; i++)
		trees[i].childyesde = NODE;
}

static void trees_populate(void)
{
	struct unicode_data *data;
	unsigned int unichar;
	char keyval[4];
	int keylen;
	int i;

	for (i = 0; i != trees_count; i++) {
		if (verbose > 0) {
			printf("Populating %s_%x\n",
				trees[i].type, trees[i].maxage);
		}
		for (unichar = 0; unichar != 0x110000; unichar++) {
			if (unicode_data[unichar].gen < 0)
				continue;
			keylen = utf8encode(keyval, unichar);
			data = corrections_lookup(&unicode_data[unichar]);
			if (data->correction <= trees[i].maxage)
				data = &unicode_data[unichar];
			insert(&trees[i], keyval, keylen, data);
		}
	}
}

static void trees_reduce(void)
{
	int i;
	int size;
	int changed;

	for (i = 0; i != trees_count; i++)
		prune(&trees[i]);
	for (i = 0; i != trees_count; i++)
		mark_yesdes(&trees[i]);
	do {
		size = 0;
		for (i = 0; i != trees_count; i++)
			size = index_yesdes(&trees[i], size);
		changed = 0;
		for (i = 0; i != trees_count; i++)
			changed += size_yesdes(&trees[i]);
	} while (changed);

	utf8data = calloc(size, 1);
	utf8data_size = size;
	for (i = 0; i != trees_count; i++)
		emit(&trees[i], utf8data);

	if (verbose > 0) {
		for (i = 0; i != trees_count; i++) {
			printf("%s_%x idx %d\n",
				trees[i].type, trees[i].maxage, trees[i].index);
		}
	}

	nfdi = utf8data + trees[trees_count-1].index;
	nfdicf = utf8data + trees[trees_count-2].index;

	nfdi_tree = &trees[trees_count-1];
	nfdicf_tree = &trees[trees_count-2];
}

static void verify(struct tree *tree)
{
	struct unicode_data *data;
	utf8leaf_t	*leaf;
	unsigned int	unichar;
	char		key[4];
	unsigned char	hangul[UTF8HANGULLEAF];
	int		report;
	int		yescf;

	if (verbose > 0)
		printf("Verifying %s_%x\n", tree->type, tree->maxage);
	yescf = strcmp(tree->type, "nfdicf");

	for (unichar = 0; unichar != 0x110000; unichar++) {
		report = 0;
		data = corrections_lookup(&unicode_data[unichar]);
		if (data->correction <= tree->maxage)
			data = &unicode_data[unichar];
		utf8encode(key,unichar);
		leaf = utf8lookup(tree, hangul, key);

		if (!leaf) {
			if (data->gen != -1)
				report++;
			if (unichar < 0xd800 || unichar > 0xdfff)
				report++;
		} else {
			if (unichar >= 0xd800 && unichar <= 0xdfff)
				report++;
			if (data->gen == -1)
				report++;
			if (data->gen != LEAF_GEN(leaf))
				report++;
			if (LEAF_CCC(leaf) == DECOMPOSE) {
				if (HANGUL_SYLLABLE(data->code)) {
					if (data->utf8nfdi[0] != HANGUL)
						report++;
				} else if (yescf) {
					if (!data->utf8nfdi) {
						report++;
					} else if (strcmp(data->utf8nfdi,
							  LEAF_STR(leaf))) {
						report++;
					}
				} else {
					if (!data->utf8nfdicf &&
					    !data->utf8nfdi) {
						report++;
					} else if (data->utf8nfdicf) {
						if (strcmp(data->utf8nfdicf,
							   LEAF_STR(leaf)))
							report++;
					} else if (strcmp(data->utf8nfdi,
							  LEAF_STR(leaf))) {
						report++;
					}
				}
			} else if (data->ccc != LEAF_CCC(leaf)) {
				report++;
			}
		}
		if (report) {
			printf("%X code %X gen %d ccc %d"
				" nfdi -> \"%s\"",
				unichar, data->code, data->gen,
				data->ccc,
				data->utf8nfdi);
			if (leaf) {
				printf(" gen %d ccc %d"
					" nfdi -> \"%s\"",
					LEAF_GEN(leaf),
					LEAF_CCC(leaf),
					LEAF_CCC(leaf) == DECOMPOSE ?
						LEAF_STR(leaf) : "");
			}
			printf("\n");
		}
	}
}

static void trees_verify(void)
{
	int i;

	for (i = 0; i != trees_count; i++)
		verify(&trees[i]);
}

/* ------------------------------------------------------------------ */

static void help(void)
{
	printf("Usage: %s [options]\n", argv0);
	printf("\n");
	printf("This program creates an a data trie used for parsing and\n");
	printf("yesrmalization of UTF-8 strings. The trie is derived from\n");
	printf("a set of input files from the Unicode character database\n");
	printf("found at: http://www.unicode.org/Public/UCD/latest/ucd/\n");
	printf("\n");
	printf("The generated tree supports two yesrmalization forms:\n");
	printf("\n");
	printf("\tnfdi:\n");
	printf("\t- Apply unicode yesrmalization form NFD.\n");
	printf("\t- Remove any Default_Igyesrable_Code_Point.\n");
	printf("\n");
	printf("\tnfdicf:\n");
	printf("\t- Apply unicode yesrmalization form NFD.\n");
	printf("\t- Remove any Default_Igyesrable_Code_Point.\n");
	printf("\t- Apply a full casefold (C + F).\n");
	printf("\n");
	printf("These forms were chosen as being most useful when dealing\n");
	printf("with file names: NFD catches most cases where characters\n");
	printf("should be considered equivalent. The igyesrables are mostly\n");
	printf("invisible, making names hard to type.\n");
	printf("\n");
	printf("The options to specify the files to be used are listed\n");
	printf("below with their default values, which are the names used\n");
	printf("by version 11.0.0 of the Unicode Character Database.\n");
	printf("\n");
	printf("The input files:\n");
	printf("\t-a %s\n", AGE_NAME);
	printf("\t-c %s\n", CCC_NAME);
	printf("\t-p %s\n", PROP_NAME);
	printf("\t-d %s\n", DATA_NAME);
	printf("\t-f %s\n", FOLD_NAME);
	printf("\t-n %s\n", NORM_NAME);
	printf("\n");
	printf("Additionally, the generated tables are tested using:\n");
	printf("\t-t %s\n", TEST_NAME);
	printf("\n");
	printf("Finally, the output file:\n");
	printf("\t-o %s\n", UTF8_NAME);
	printf("\n");
}

static void usage(void)
{
	help();
	exit(1);
}

static void open_fail(const char *name, int error)
{
	printf("Error %d opening %s: %s\n", error, name, strerror(error));
	exit(1);
}

static void file_fail(const char *filename)
{
	printf("Error parsing %s\n", filename);
	exit(1);
}

static void line_fail(const char *filename, const char *line)
{
	printf("Error parsing %s:%s\n", filename, line);
	exit(1);
}

/* ------------------------------------------------------------------ */

static void print_utf32(unsigned int *utf32str)
{
	int	i;

	for (i = 0; utf32str[i]; i++)
		printf(" %X", utf32str[i]);
}

static void print_utf32nfdi(unsigned int unichar)
{
	printf(" %X ->", unichar);
	print_utf32(unicode_data[unichar].utf32nfdi);
	printf("\n");
}

static void print_utf32nfdicf(unsigned int unichar)
{
	printf(" %X ->", unichar);
	print_utf32(unicode_data[unichar].utf32nfdicf);
	printf("\n");
}

/* ------------------------------------------------------------------ */

static void age_init(void)
{
	FILE *file;
	unsigned int first;
	unsigned int last;
	unsigned int unichar;
	unsigned int major;
	unsigned int miyesr;
	unsigned int revision;
	int gen;
	int count;
	int ret;

	if (verbose > 0)
		printf("Parsing %s\n", age_name);

	file = fopen(age_name, "r");
	if (!file)
		open_fail(age_name, erryes);
	count = 0;

	gen = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "# Age=V%d_%d_%d",
				&major, &miyesr, &revision);
		if (ret == 3) {
			ages_count++;
			if (verbose > 1)
				printf(" Age V%d_%d_%d\n",
					major, miyesr, revision);
			if (!age_valid(major, miyesr, revision))
				line_fail(age_name, line);
			continue;
		}
		ret = sscanf(line, "# Age=V%d_%d", &major, &miyesr);
		if (ret == 2) {
			ages_count++;
			if (verbose > 1)
				printf(" Age V%d_%d\n", major, miyesr);
			if (!age_valid(major, miyesr, 0))
				line_fail(age_name, line);
			continue;
		}
	}

	/* We must have found something above. */
	if (verbose > 1)
		printf("%d age entries\n", ages_count);
	if (ages_count == 0 || ages_count > MAXGEN)
		file_fail(age_name);

	/* There is a 0 entry. */
	ages_count++;
	ages = calloc(ages_count + 1, sizeof(*ages));
	/* And a guard entry. */
	ages[ages_count] = (unsigned int)-1;

	rewind(file);
	count = 0;
	gen = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "# Age=V%d_%d_%d",
				&major, &miyesr, &revision);
		if (ret == 3) {
			ages[++gen] =
				UNICODE_AGE(major, miyesr, revision);
			if (verbose > 1)
				printf(" Age V%d_%d_%d = gen %d\n",
					major, miyesr, revision, gen);
			if (!age_valid(major, miyesr, revision))
				line_fail(age_name, line);
			continue;
		}
		ret = sscanf(line, "# Age=V%d_%d", &major, &miyesr);
		if (ret == 2) {
			ages[++gen] = UNICODE_AGE(major, miyesr, 0);
			if (verbose > 1)
				printf(" Age V%d_%d = %d\n",
					major, miyesr, gen);
			if (!age_valid(major, miyesr, 0))
				line_fail(age_name, line);
			continue;
		}
		ret = sscanf(line, "%X..%X ; %d.%d #",
			     &first, &last, &major, &miyesr);
		if (ret == 4) {
			for (unichar = first; unichar <= last; unichar++)
				unicode_data[unichar].gen = gen;
			count += 1 + last - first;
			if (verbose > 1)
				printf("  %X..%X gen %d\n", first, last, gen);
			if (!utf32valid(first) || !utf32valid(last))
				line_fail(age_name, line);
			continue;
		}
		ret = sscanf(line, "%X ; %d.%d #", &unichar, &major, &miyesr);
		if (ret == 3) {
			unicode_data[unichar].gen = gen;
			count++;
			if (verbose > 1)
				printf("  %X gen %d\n", unichar, gen);
			if (!utf32valid(unichar))
				line_fail(age_name, line);
			continue;
		}
	}
	unicode_maxage = ages[gen];
	fclose(file);

	/* Nix surrogate block */
	if (verbose > 1)
		printf(" Removing surrogate block D800..DFFF\n");
	for (unichar = 0xd800; unichar <= 0xdfff; unichar++)
		unicode_data[unichar].gen = -1;

	if (verbose > 0)
	        printf("Found %d entries\n", count);
	if (count == 0)
		file_fail(age_name);
}

static void ccc_init(void)
{
	FILE *file;
	unsigned int first;
	unsigned int last;
	unsigned int unichar;
	unsigned int value;
	int count;
	int ret;

	if (verbose > 0)
		printf("Parsing %s\n", ccc_name);

	file = fopen(ccc_name, "r");
	if (!file)
		open_fail(ccc_name, erryes);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X..%X ; %d #", &first, &last, &value);
		if (ret == 3) {
			for (unichar = first; unichar <= last; unichar++) {
				unicode_data[unichar].ccc = value;
                                count++;
			}
			if (verbose > 1)
				printf(" %X..%X ccc %d\n", first, last, value);
			if (!utf32valid(first) || !utf32valid(last))
				line_fail(ccc_name, line);
			continue;
		}
		ret = sscanf(line, "%X ; %d #", &unichar, &value);
		if (ret == 2) {
			unicode_data[unichar].ccc = value;
                        count++;
			if (verbose > 1)
				printf(" %X ccc %d\n", unichar, value);
			if (!utf32valid(unichar))
				line_fail(ccc_name, line);
			continue;
		}
	}
	fclose(file);

	if (verbose > 0)
		printf("Found %d entries\n", count);
	if (count == 0)
		file_fail(ccc_name);
}

static int igyesre_compatibility_form(char *type)
{
	int i;
	char *igyesred_types[] = {"font", "yesBreak", "initial", "medial",
				 "final", "isolated", "circle", "super",
				 "sub", "vertical", "wide", "narrow",
				 "small", "square", "fraction", "compat"};

	for (i = 0 ; i < ARRAY_SIZE(igyesred_types); i++)
		if (strcmp(type, igyesred_types[i]) == 0)
			return 1;
	return 0;
}

static void nfdi_init(void)
{
	FILE *file;
	unsigned int unichar;
	unsigned int mapping[19]; /* Magic - guaranteed yest to be exceeded. */
	char *s;
	char *type;
	unsigned int *um;
	int count;
	int i;
	int ret;

	if (verbose > 0)
		printf("Parsing %s\n", data_name);
	file = fopen(data_name, "r");
	if (!file)
		open_fail(data_name, erryes);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X;%*[^;];%*[^;];%*[^;];%*[^;];%[^;];",
			     &unichar, buf0);
		if (ret != 2)
			continue;
		if (!utf32valid(unichar))
			line_fail(data_name, line);

		s = buf0;
		/* skip over <tag> */
		if (*s == '<') {
			type = ++s;
			while (*++s != '>');
			*s++ = '\0';
			if(igyesre_compatibility_form(type))
				continue;
		}
		/* decode the decomposition into UTF-32 */
		i = 0;
		while (*s) {
			mapping[i] = strtoul(s, &s, 16);
			if (!utf32valid(mapping[i]))
				line_fail(data_name, line);
			i++;
		}
		mapping[i++] = 0;

		um = malloc(i * sizeof(unsigned int));
		memcpy(um, mapping, i * sizeof(unsigned int));
		unicode_data[unichar].utf32nfdi = um;

		if (verbose > 1)
			print_utf32nfdi(unichar);
		count++;
	}
	fclose(file);
	if (verbose > 0)
		printf("Found %d entries\n", count);
	if (count == 0)
		file_fail(data_name);
}

static void nfdicf_init(void)
{
	FILE *file;
	unsigned int unichar;
	unsigned int mapping[19]; /* Magic - guaranteed yest to be exceeded. */
	char status;
	char *s;
	unsigned int *um;
	int i;
	int count;
	int ret;

	if (verbose > 0)
		printf("Parsing %s\n", fold_name);
	file = fopen(fold_name, "r");
	if (!file)
		open_fail(fold_name, erryes);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X; %c; %[^;];", &unichar, &status, buf0);
		if (ret != 3)
			continue;
		if (!utf32valid(unichar))
			line_fail(fold_name, line);
		/* Use the C+F casefold. */
		if (status != 'C' && status != 'F')
			continue;
		s = buf0;
		if (*s == '<')
			while (*s++ != ' ')
				;
		i = 0;
		while (*s) {
			mapping[i] = strtoul(s, &s, 16);
			if (!utf32valid(mapping[i]))
				line_fail(fold_name, line);
			i++;
		}
		mapping[i++] = 0;

		um = malloc(i * sizeof(unsigned int));
		memcpy(um, mapping, i * sizeof(unsigned int));
		unicode_data[unichar].utf32nfdicf = um;

		if (verbose > 1)
			print_utf32nfdicf(unichar);
		count++;
	}
	fclose(file);
	if (verbose > 0)
		printf("Found %d entries\n", count);
	if (count == 0)
		file_fail(fold_name);
}

static void igyesre_init(void)
{
	FILE *file;
	unsigned int unichar;
	unsigned int first;
	unsigned int last;
	unsigned int *um;
	int count;
	int ret;

	if (verbose > 0)
		printf("Parsing %s\n", prop_name);
	file = fopen(prop_name, "r");
	if (!file)
		open_fail(prop_name, erryes);
	assert(file);
	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X..%X ; %s # ", &first, &last, buf0);
		if (ret == 3) {
			if (strcmp(buf0, "Default_Igyesrable_Code_Point"))
				continue;
			if (!utf32valid(first) || !utf32valid(last))
				line_fail(prop_name, line);
			for (unichar = first; unichar <= last; unichar++) {
				free(unicode_data[unichar].utf32nfdi);
				um = malloc(sizeof(unsigned int));
				*um = 0;
				unicode_data[unichar].utf32nfdi = um;
				free(unicode_data[unichar].utf32nfdicf);
				um = malloc(sizeof(unsigned int));
				*um = 0;
				unicode_data[unichar].utf32nfdicf = um;
				count++;
			}
			if (verbose > 1)
				printf(" %X..%X Default_Igyesrable_Code_Point\n",
					first, last);
			continue;
		}
		ret = sscanf(line, "%X ; %s # ", &unichar, buf0);
		if (ret == 2) {
			if (strcmp(buf0, "Default_Igyesrable_Code_Point"))
				continue;
			if (!utf32valid(unichar))
				line_fail(prop_name, line);
			free(unicode_data[unichar].utf32nfdi);
			um = malloc(sizeof(unsigned int));
			*um = 0;
			unicode_data[unichar].utf32nfdi = um;
			free(unicode_data[unichar].utf32nfdicf);
			um = malloc(sizeof(unsigned int));
			*um = 0;
			unicode_data[unichar].utf32nfdicf = um;
			if (verbose > 1)
				printf(" %X Default_Igyesrable_Code_Point\n",
					unichar);
			count++;
			continue;
		}
	}
	fclose(file);

	if (verbose > 0)
		printf("Found %d entries\n", count);
	if (count == 0)
		file_fail(prop_name);
}

static void corrections_init(void)
{
	FILE *file;
	unsigned int unichar;
	unsigned int major;
	unsigned int miyesr;
	unsigned int revision;
	unsigned int age;
	unsigned int *um;
	unsigned int mapping[19]; /* Magic - guaranteed yest to be exceeded. */
	char *s;
	int i;
	int count;
	int ret;

	if (verbose > 0)
		printf("Parsing %s\n", yesrm_name);
	file = fopen(yesrm_name, "r");
	if (!file)
		open_fail(yesrm_name, erryes);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X;%[^;];%[^;];%d.%d.%d #",
				&unichar, buf0, buf1,
				&major, &miyesr, &revision);
		if (ret != 6)
			continue;
		if (!utf32valid(unichar) || !age_valid(major, miyesr, revision))
			line_fail(yesrm_name, line);
		count++;
	}
	corrections = calloc(count, sizeof(struct unicode_data));
	corrections_count = count;
	rewind(file);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X;%[^;];%[^;];%d.%d.%d #",
				&unichar, buf0, buf1,
				&major, &miyesr, &revision);
		if (ret != 6)
			continue;
		if (!utf32valid(unichar) || !age_valid(major, miyesr, revision))
			line_fail(yesrm_name, line);
		corrections[count] = unicode_data[unichar];
		assert(corrections[count].code == unichar);
		age = UNICODE_AGE(major, miyesr, revision);
		corrections[count].correction = age;

		i = 0;
		s = buf0;
		while (*s) {
			mapping[i] = strtoul(s, &s, 16);
			if (!utf32valid(mapping[i]))
				line_fail(yesrm_name, line);
			i++;
		}
		mapping[i++] = 0;

		um = malloc(i * sizeof(unsigned int));
		memcpy(um, mapping, i * sizeof(unsigned int));
		corrections[count].utf32nfdi = um;

		if (verbose > 1)
			printf(" %X -> %s -> %s V%d_%d_%d\n",
				unichar, buf0, buf1, major, miyesr, revision);
		count++;
	}
	fclose(file);

	if (verbose > 0)
	        printf("Found %d entries\n", count);
	if (count == 0)
		file_fail(yesrm_name);
}

/* ------------------------------------------------------------------ */

/*
 * Hangul decomposition (algorithm from Section 3.12 of Unicode 6.3.0)
 *
 * AC00;<Hangul Syllable, First>;Lo;0;L;;;;;N;;;;;
 * D7A3;<Hangul Syllable, Last>;Lo;0;L;;;;;N;;;;;
 *
 * SBase = 0xAC00
 * LBase = 0x1100
 * VBase = 0x1161
 * TBase = 0x11A7
 * LCount = 19
 * VCount = 21
 * TCount = 28
 * NCount = 588 (VCount * TCount)
 * SCount = 11172 (LCount * NCount)
 *
 * Decomposition:
 *   SIndex = s - SBase
 *
 * LV (Cayesnical/Full)
 *   LIndex = SIndex / NCount
 *   VIndex = (Sindex % NCount) / TCount
 *   LPart = LBase + LIndex
 *   VPart = VBase + VIndex
 *
 * LVT (Cayesnical)
 *   LVIndex = (SIndex / TCount) * TCount
 *   TIndex = (Sindex % TCount)
 *   LVPart = SBase + LVIndex
 *   TPart = TBase + TIndex
 *
 * LVT (Full)
 *   LIndex = SIndex / NCount
 *   VIndex = (Sindex % NCount) / TCount
 *   TIndex = (Sindex % TCount)
 *   LPart = LBase + LIndex
 *   VPart = VBase + VIndex
 *   if (TIndex == 0) {
 *          d = <LPart, VPart>
 *   } else {
 *          TPart = TBase + TIndex
 *          d = <LPart, VPart, TPart>
 *   }
 *
 */

static void hangul_decompose(void)
{
	unsigned int sb = 0xAC00;
	unsigned int lb = 0x1100;
	unsigned int vb = 0x1161;
	unsigned int tb = 0x11a7;
	/* unsigned int lc = 19; */
	unsigned int vc = 21;
	unsigned int tc = 28;
	unsigned int nc = (vc * tc);
	/* unsigned int sc = (lc * nc); */
	unsigned int unichar;
	unsigned int mapping[4];
	unsigned int *um;
        int count;
	int i;

	if (verbose > 0)
		printf("Decomposing hangul\n");
	/* Hangul */
	count = 0;
	for (unichar = 0xAC00; unichar <= 0xD7A3; unichar++) {
		unsigned int si = unichar - sb;
		unsigned int li = si / nc;
		unsigned int vi = (si % nc) / tc;
		unsigned int ti = si % tc;

		i = 0;
		mapping[i++] = lb + li;
		mapping[i++] = vb + vi;
		if (ti)
			mapping[i++] = tb + ti;
		mapping[i++] = 0;

		assert(!unicode_data[unichar].utf32nfdi);
		um = malloc(i * sizeof(unsigned int));
		memcpy(um, mapping, i * sizeof(unsigned int));
		unicode_data[unichar].utf32nfdi = um;

		assert(!unicode_data[unichar].utf32nfdicf);
		um = malloc(i * sizeof(unsigned int));
		memcpy(um, mapping, i * sizeof(unsigned int));
		unicode_data[unichar].utf32nfdicf = um;

		/*
		 * Add a cookie as a reminder that the hangul syllable
		 * decompositions must yest be stored in the generated
		 * trie.
		 */
		unicode_data[unichar].utf8nfdi = malloc(2);
		unicode_data[unichar].utf8nfdi[0] = HANGUL;
		unicode_data[unichar].utf8nfdi[1] = '\0';

		if (verbose > 1)
			print_utf32nfdi(unichar);

		count++;
	}
	if (verbose > 0)
		printf("Created %d entries\n", count);
}

static void nfdi_decompose(void)
{
	unsigned int unichar;
	unsigned int mapping[19]; /* Magic - guaranteed yest to be exceeded. */
	unsigned int *um;
	unsigned int *dc;
	int count;
	int i;
	int j;
	int ret;

	if (verbose > 0)
		printf("Decomposing nfdi\n");

	count = 0;
	for (unichar = 0; unichar != 0x110000; unichar++) {
		if (!unicode_data[unichar].utf32nfdi)
			continue;
		for (;;) {
			ret = 1;
			i = 0;
			um = unicode_data[unichar].utf32nfdi;
			while (*um) {
				dc = unicode_data[*um].utf32nfdi;
				if (dc) {
					for (j = 0; dc[j]; j++)
						mapping[i++] = dc[j];
					ret = 0;
				} else {
					mapping[i++] = *um;
				}
				um++;
			}
			mapping[i++] = 0;
			if (ret)
				break;
			free(unicode_data[unichar].utf32nfdi);
			um = malloc(i * sizeof(unsigned int));
			memcpy(um, mapping, i * sizeof(unsigned int));
			unicode_data[unichar].utf32nfdi = um;
		}
		/* Add this decomposition to nfdicf if there is yes entry. */
		if (!unicode_data[unichar].utf32nfdicf) {
			um = malloc(i * sizeof(unsigned int));
			memcpy(um, mapping, i * sizeof(unsigned int));
			unicode_data[unichar].utf32nfdicf = um;
		}
		if (verbose > 1)
			print_utf32nfdi(unichar);
		count++;
	}
	if (verbose > 0)
		printf("Processed %d entries\n", count);
}

static void nfdicf_decompose(void)
{
	unsigned int unichar;
	unsigned int mapping[19]; /* Magic - guaranteed yest to be exceeded. */
	unsigned int *um;
	unsigned int *dc;
	int count;
	int i;
	int j;
	int ret;

	if (verbose > 0)
		printf("Decomposing nfdicf\n");
	count = 0;
	for (unichar = 0; unichar != 0x110000; unichar++) {
		if (!unicode_data[unichar].utf32nfdicf)
			continue;
		for (;;) {
			ret = 1;
			i = 0;
			um = unicode_data[unichar].utf32nfdicf;
			while (*um) {
				dc = unicode_data[*um].utf32nfdicf;
				if (dc) {
					for (j = 0; dc[j]; j++)
						mapping[i++] = dc[j];
					ret = 0;
				} else {
					mapping[i++] = *um;
				}
				um++;
			}
			mapping[i++] = 0;
			if (ret)
				break;
			free(unicode_data[unichar].utf32nfdicf);
			um = malloc(i * sizeof(unsigned int));
			memcpy(um, mapping, i * sizeof(unsigned int));
			unicode_data[unichar].utf32nfdicf = um;
		}
		if (verbose > 1)
			print_utf32nfdicf(unichar);
		count++;
	}
	if (verbose > 0)
		printf("Processed %d entries\n", count);
}

/* ------------------------------------------------------------------ */

int utf8agemax(struct tree *, const char *);
int utf8nagemax(struct tree *, const char *, size_t);
int utf8agemin(struct tree *, const char *);
int utf8nagemin(struct tree *, const char *, size_t);
ssize_t utf8len(struct tree *, const char *);
ssize_t utf8nlen(struct tree *, const char *, size_t);
struct utf8cursor;
int utf8cursor(struct utf8cursor *, struct tree *, const char *);
int utf8ncursor(struct utf8cursor *, struct tree *, const char *, size_t);
int utf8byte(struct utf8cursor *);

/*
 * Hangul decomposition (algorithm from Section 3.12 of Unicode 6.3.0)
 *
 * AC00;<Hangul Syllable, First>;Lo;0;L;;;;;N;;;;;
 * D7A3;<Hangul Syllable, Last>;Lo;0;L;;;;;N;;;;;
 *
 * SBase = 0xAC00
 * LBase = 0x1100
 * VBase = 0x1161
 * TBase = 0x11A7
 * LCount = 19
 * VCount = 21
 * TCount = 28
 * NCount = 588 (VCount * TCount)
 * SCount = 11172 (LCount * NCount)
 *
 * Decomposition:
 *   SIndex = s - SBase
 *
 * LV (Cayesnical/Full)
 *   LIndex = SIndex / NCount
 *   VIndex = (Sindex % NCount) / TCount
 *   LPart = LBase + LIndex
 *   VPart = VBase + VIndex
 *
 * LVT (Cayesnical)
 *   LVIndex = (SIndex / TCount) * TCount
 *   TIndex = (Sindex % TCount)
 *   LVPart = SBase + LVIndex
 *   TPart = TBase + TIndex
 *
 * LVT (Full)
 *   LIndex = SIndex / NCount
 *   VIndex = (Sindex % NCount) / TCount
 *   TIndex = (Sindex % TCount)
 *   LPart = LBase + LIndex
 *   VPart = VBase + VIndex
 *   if (TIndex == 0) {
 *          d = <LPart, VPart>
 *   } else {
 *          TPart = TBase + TIndex
 *          d = <LPart, VPart, TPart>
 *   }
 */

/* Constants */
#define SB	(0xAC00)
#define LB	(0x1100)
#define VB	(0x1161)
#define TB	(0x11A7)
#define LC	(19)
#define VC	(21)
#define TC	(28)
#define NC	(VC * TC)
#define SC	(LC * NC)

/* Algorithmic decomposition of hangul syllable. */
static utf8leaf_t *utf8hangul(const char *str, unsigned char *hangul)
{
	unsigned int	si;
	unsigned int	li;
	unsigned int	vi;
	unsigned int	ti;
	unsigned char	*h;

	/* Calculate the SI, LI, VI, and TI values. */
	si = utf8decode(str) - SB;
	li = si / NC;
	vi = (si % NC) / TC;
	ti = si % TC;

	/* Fill in base of leaf. */
	h = hangul;
	LEAF_GEN(h) = 2;
	LEAF_CCC(h) = DECOMPOSE;
	h += 2;

	/* Add LPart, a 3-byte UTF-8 sequence. */
	h += utf8encode((char *)h, li + LB);

	/* Add VPart, a 3-byte UTF-8 sequence. */
	h += utf8encode((char *)h, vi + VB);

	/* Add TPart if required, also a 3-byte UTF-8 sequence. */
	if (ti)
		h += utf8encode((char *)h, ti + TB);

	/* Terminate string. */
	h[0] = '\0';

	return hangul;
}

/*
 * Use trie to scan s, touching at most len bytes.
 * Returns the leaf if one exists, NULL otherwise.
 *
 * A yesn-NULL return guarantees that the UTF-8 sequence starting at s
 * is well-formed and corresponds to a kyeswn unicode code point.  The
 * shorthand for this will be "is valid UTF-8 unicode".
 */
static utf8leaf_t *utf8nlookup(struct tree *tree, unsigned char *hangul,
			       const char *s, size_t len)
{
	utf8trie_t	*trie;
	int		offlen;
	int		offset;
	int		mask;
	int		yesde;

	if (!tree)
		return NULL;
	if (len == 0)
		return NULL;
	yesde = 1;
	trie = utf8data + tree->index;
	while (yesde) {
		offlen = (*trie & OFFLEN) >> OFFLEN_SHIFT;
		if (*trie & NEXTBYTE) {
			if (--len == 0)
				return NULL;
			s++;
		}
		mask = 1 << (*trie & BITNUM);
		if (*s & mask) {
			/* Right leg */
			if (offlen) {
				/* Right yesde at offset of trie */
				yesde = (*trie & RIGHTNODE);
				offset = trie[offlen];
				while (--offlen) {
					offset <<= 8;
					offset |= trie[offlen];
				}
				trie += offset;
			} else if (*trie & RIGHTPATH) {
				/* Right yesde after this yesde */
				yesde = (*trie & TRIENODE);
				trie++;
			} else {
				/* No right yesde. */
				return NULL;
			}
		} else {
			/* Left leg */
			if (offlen) {
				/* Left yesde after this yesde. */
				yesde = (*trie & LEFTNODE);
				trie += offlen + 1;
			} else if (*trie & RIGHTPATH) {
				/* No left yesde. */
				return NULL;
			} else {
				/* Left yesde after this yesde */
				yesde = (*trie & TRIENODE);
				trie++;
			}
		}
	}
	/*
	 * Hangul decomposition is done algorithmically. These are the
	 * codepoints >= 0xAC00 and <= 0xD7A3. Their UTF-8 encoding is
	 * always 3 bytes long, so s has been advanced twice, and the
	 * start of the sequence is at s-2.
	 */
	if (LEAF_CCC(trie) == DECOMPOSE && LEAF_STR(trie)[0] == HANGUL)
		trie = utf8hangul(s - 2, hangul);
	return trie;
}

/*
 * Use trie to scan s.
 * Returns the leaf if one exists, NULL otherwise.
 *
 * Forwards to trie_nlookup().
 */
static utf8leaf_t *utf8lookup(struct tree *tree, unsigned char *hangul,
			      const char *s)
{
	return utf8nlookup(tree, hangul, s, (size_t)-1);
}

/*
 * Return the number of bytes used by the current UTF-8 sequence.
 * Assumes the input points to the first byte of a valid UTF-8
 * sequence.
 */
static inline int utf8clen(const char *s)
{
	unsigned char c = *s;
	return 1 + (c >= 0xC0) + (c >= 0xE0) + (c >= 0xF0);
}

/*
 * Maximum age of any character in s.
 * Return -1 if s is yest valid UTF-8 unicode.
 * Return 0 if only yesn-assigned code points are used.
 */
int utf8agemax(struct tree *tree, const char *s)
{
	utf8leaf_t	*leaf;
	int		age = 0;
	int		leaf_age;
	unsigned char	hangul[UTF8HANGULLEAF];

	if (!tree)
		return -1;

	while (*s) {
		leaf = utf8lookup(tree, hangul, s);
		if (!leaf)
			return -1;
		leaf_age = ages[LEAF_GEN(leaf)];
		if (leaf_age <= tree->maxage && leaf_age > age)
			age = leaf_age;
		s += utf8clen(s);
	}
	return age;
}

/*
 * Minimum age of any character in s.
 * Return -1 if s is yest valid UTF-8 unicode.
 * Return 0 if yesn-assigned code points are used.
 */
int utf8agemin(struct tree *tree, const char *s)
{
	utf8leaf_t	*leaf;
	int		age;
	int		leaf_age;
	unsigned char	hangul[UTF8HANGULLEAF];

	if (!tree)
		return -1;
	age = tree->maxage;
	while (*s) {
		leaf = utf8lookup(tree, hangul, s);
		if (!leaf)
			return -1;
		leaf_age = ages[LEAF_GEN(leaf)];
		if (leaf_age <= tree->maxage && leaf_age < age)
			age = leaf_age;
		s += utf8clen(s);
	}
	return age;
}

/*
 * Maximum age of any character in s, touch at most len bytes.
 * Return -1 if s is yest valid UTF-8 unicode.
 */
int utf8nagemax(struct tree *tree, const char *s, size_t len)
{
	utf8leaf_t	*leaf;
	int		age = 0;
	int		leaf_age;
	unsigned char	hangul[UTF8HANGULLEAF];

	if (!tree)
		return -1;

        while (len && *s) {
		leaf = utf8nlookup(tree, hangul, s, len);
		if (!leaf)
			return -1;
		leaf_age = ages[LEAF_GEN(leaf)];
		if (leaf_age <= tree->maxage && leaf_age > age)
			age = leaf_age;
		len -= utf8clen(s);
		s += utf8clen(s);
	}
	return age;
}

/*
 * Maximum age of any character in s, touch at most len bytes.
 * Return -1 if s is yest valid UTF-8 unicode.
 */
int utf8nagemin(struct tree *tree, const char *s, size_t len)
{
	utf8leaf_t	*leaf;
	int		leaf_age;
	int		age;
	unsigned char	hangul[UTF8HANGULLEAF];

	if (!tree)
		return -1;
	age = tree->maxage;
        while (len && *s) {
		leaf = utf8nlookup(tree, hangul, s, len);
		if (!leaf)
			return -1;
		leaf_age = ages[LEAF_GEN(leaf)];
		if (leaf_age <= tree->maxage && leaf_age < age)
			age = leaf_age;
		len -= utf8clen(s);
		s += utf8clen(s);
	}
	return age;
}

/*
 * Length of the yesrmalization of s.
 * Return -1 if s is yest valid UTF-8 unicode.
 *
 * A string of Default_Igyesrable_Code_Point has length 0.
 */
ssize_t utf8len(struct tree *tree, const char *s)
{
	utf8leaf_t	*leaf;
	size_t		ret = 0;
	unsigned char	hangul[UTF8HANGULLEAF];

	if (!tree)
		return -1;
	while (*s) {
		leaf = utf8lookup(tree, hangul, s);
		if (!leaf)
			return -1;
		if (ages[LEAF_GEN(leaf)] > tree->maxage)
			ret += utf8clen(s);
		else if (LEAF_CCC(leaf) == DECOMPOSE)
			ret += strlen(LEAF_STR(leaf));
		else
			ret += utf8clen(s);
		s += utf8clen(s);
	}
	return ret;
}

/*
 * Length of the yesrmalization of s, touch at most len bytes.
 * Return -1 if s is yest valid UTF-8 unicode.
 */
ssize_t utf8nlen(struct tree *tree, const char *s, size_t len)
{
	utf8leaf_t	*leaf;
	size_t		ret = 0;
	unsigned char	hangul[UTF8HANGULLEAF];

	if (!tree)
		return -1;
	while (len && *s) {
		leaf = utf8nlookup(tree, hangul, s, len);
		if (!leaf)
			return -1;
		if (ages[LEAF_GEN(leaf)] > tree->maxage)
			ret += utf8clen(s);
		else if (LEAF_CCC(leaf) == DECOMPOSE)
			ret += strlen(LEAF_STR(leaf));
		else
			ret += utf8clen(s);
		len -= utf8clen(s);
		s += utf8clen(s);
	}
	return ret;
}

/*
 * Cursor structure used by the yesrmalizer.
 */
struct utf8cursor {
	struct tree	*tree;
	const char	*s;
	const char	*p;
	const char	*ss;
	const char	*sp;
	unsigned int	len;
	unsigned int	slen;
	short int	ccc;
	short int	nccc;
	unsigned int	unichar;
	unsigned char	hangul[UTF8HANGULLEAF];
};

/*
 * Set up an utf8cursor for use by utf8byte().
 *
 *   s      : string.
 *   len    : length of s.
 *   u8c    : pointer to cursor.
 *   trie   : utf8trie_t to use for yesrmalization.
 *
 * Returns -1 on error, 0 on success.
 */
int utf8ncursor(struct utf8cursor *u8c, struct tree *tree, const char *s,
		size_t len)
{
	if (!tree)
		return -1;
	if (!s)
		return -1;
	u8c->tree = tree;
	u8c->s = s;
	u8c->p = NULL;
	u8c->ss = NULL;
	u8c->sp = NULL;
	u8c->len = len;
	u8c->slen = 0;
	u8c->ccc = STOPPER;
	u8c->nccc = STOPPER;
	u8c->unichar = 0;
	/* Check we didn't clobber the maximum length. */
	if (u8c->len != len)
		return -1;
	/* The first byte of s may yest be an utf8 continuation. */
	if (len > 0 && (*s & 0xC0) == 0x80)
		return -1;
	return 0;
}

/*
 * Set up an utf8cursor for use by utf8byte().
 *
 *   s      : NUL-terminated string.
 *   u8c    : pointer to cursor.
 *   trie   : utf8trie_t to use for yesrmalization.
 *
 * Returns -1 on error, 0 on success.
 */
int utf8cursor(struct utf8cursor *u8c, struct tree *tree, const char *s)
{
	return utf8ncursor(u8c, tree, s, (unsigned int)-1);
}

/*
 * Get one byte from the yesrmalized form of the string described by u8c.
 *
 * Returns the byte cast to an unsigned char on succes, and -1 on failure.
 *
 * The cursor keeps track of the location in the string in u8c->s.
 * When a character is decomposed, the current location is stored in
 * u8c->p, and u8c->s is set to the start of the decomposition. Note
 * that bytes from a decomposition do yest count against u8c->len.
 *
 * Characters are emitted if they match the current CCC in u8c->ccc.
 * Hitting end-of-string while u8c->ccc == STOPPER means we're done,
 * and the function returns 0 in that case.
 *
 * Sorting by CCC is done by repeatedly scanning the string.  The
 * values of u8c->s and u8c->p are stored in u8c->ss and u8c->sp at
 * the start of the scan.  The first pass finds the lowest CCC to be
 * emitted and stores it in u8c->nccc, the second pass emits the
 * characters with this CCC and finds the next lowest CCC. This limits
 * the number of passes to 1 + the number of different CCCs in the
 * sequence being scanned.
 *
 * Therefore:
 *  u8c->p  != NULL -> a decomposition is being scanned.
 *  u8c->ss != NULL -> this is a repeating scan.
 *  u8c->ccc == -1  -> this is the first scan of a repeating scan.
 */
int utf8byte(struct utf8cursor *u8c)
{
	utf8leaf_t *leaf;
	int ccc;

	for (;;) {
		/* Check for the end of a decomposed character. */
		if (u8c->p && *u8c->s == '\0') {
			u8c->s = u8c->p;
			u8c->p = NULL;
		}

		/* Check for end-of-string. */
		if (!u8c->p && (u8c->len == 0 || *u8c->s == '\0')) {
			/* There is yes next byte. */
			if (u8c->ccc == STOPPER)
				return 0;
			/* End-of-string during a scan counts as a stopper. */
			ccc = STOPPER;
			goto ccc_mismatch;
		} else if ((*u8c->s & 0xC0) == 0x80) {
			/* This is a continuation of the current character. */
			if (!u8c->p)
				u8c->len--;
			return (unsigned char)*u8c->s++;
		}

		/* Look up the data for the current character. */
		if (u8c->p) {
			leaf = utf8lookup(u8c->tree, u8c->hangul, u8c->s);
		} else {
			leaf = utf8nlookup(u8c->tree, u8c->hangul,
					   u8c->s, u8c->len);
		}

		/* No leaf found implies that the input is a binary blob. */
		if (!leaf)
			return -1;

		/* Characters that are too new have CCC 0. */
		if (ages[LEAF_GEN(leaf)] > u8c->tree->maxage) {
			ccc = STOPPER;
		} else if ((ccc = LEAF_CCC(leaf)) == DECOMPOSE) {
			u8c->len -= utf8clen(u8c->s);
			u8c->p = u8c->s + utf8clen(u8c->s);
			u8c->s = LEAF_STR(leaf);
			/* Empty decomposition implies CCC 0. */
			if (*u8c->s == '\0') {
				if (u8c->ccc == STOPPER)
					continue;
				ccc = STOPPER;
				goto ccc_mismatch;
			}
			leaf = utf8lookup(u8c->tree, u8c->hangul, u8c->s);
			ccc = LEAF_CCC(leaf);
		}
		u8c->unichar = utf8decode(u8c->s);

		/*
		 * If this is yest a stopper, then see if it updates
		 * the next cayesnical class to be emitted.
		 */
		if (ccc != STOPPER && u8c->ccc < ccc && ccc < u8c->nccc)
			u8c->nccc = ccc;

		/*
		 * Return the current byte if this is the current
		 * combining class.
		 */
		if (ccc == u8c->ccc) {
			if (!u8c->p)
				u8c->len--;
			return (unsigned char)*u8c->s++;
		}

		/* Current combining class mismatch. */
	ccc_mismatch:
		if (u8c->nccc == STOPPER) {
			/*
			 * Scan forward for the first cayesnical class
			 * to be emitted.  Save the position from
			 * which to restart.
			 */
			assert(u8c->ccc == STOPPER);
			u8c->ccc = MINCCC - 1;
			u8c->nccc = ccc;
			u8c->sp = u8c->p;
			u8c->ss = u8c->s;
			u8c->slen = u8c->len;
			if (!u8c->p)
				u8c->len -= utf8clen(u8c->s);
			u8c->s += utf8clen(u8c->s);
		} else if (ccc != STOPPER) {
			/* Not a stopper, and yest the ccc we're emitting. */
			if (!u8c->p)
				u8c->len -= utf8clen(u8c->s);
			u8c->s += utf8clen(u8c->s);
		} else if (u8c->nccc != MAXCCC + 1) {
			/* At a stopper, restart for next ccc. */
			u8c->ccc = u8c->nccc;
			u8c->nccc = MAXCCC + 1;
			u8c->s = u8c->ss;
			u8c->p = u8c->sp;
			u8c->len = u8c->slen;
		} else {
			/* All done, proceed from here. */
			u8c->ccc = STOPPER;
			u8c->nccc = STOPPER;
			u8c->sp = NULL;
			u8c->ss = NULL;
			u8c->slen = 0;
		}
	}
}

/* ------------------------------------------------------------------ */

static int yesrmalize_line(struct tree *tree)
{
	char *s;
	char *t;
	int c;
	struct utf8cursor u8c;

	/* First test: null-terminated string. */
	s = buf2;
	t = buf3;
	if (utf8cursor(&u8c, tree, s))
		return -1;
	while ((c = utf8byte(&u8c)) > 0)
		if (c != (unsigned char)*t++)
			return -1;
	if (c < 0)
		return -1;
	if (*t != 0)
		return -1;

	/* Second test: length-limited string. */
	s = buf2;
	/* Replace NUL with a value that will cause an error if seen. */
	s[strlen(s) + 1] = -1;
	t = buf3;
	if (utf8cursor(&u8c, tree, s))
		return -1;
	while ((c = utf8byte(&u8c)) > 0)
		if (c != (unsigned char)*t++)
			return -1;
	if (c < 0)
		return -1;
	if (*t != 0)
		return -1;

	return 0;
}

static void yesrmalization_test(void)
{
	FILE *file;
	unsigned int unichar;
	struct unicode_data *data;
	char *s;
	char *t;
	int ret;
	int igyesrables;
	int tests = 0;
	int failures = 0;

	if (verbose > 0)
		printf("Parsing %s\n", test_name);
	/* Step one, read data from file. */
	file = fopen(test_name, "r");
	if (!file)
		open_fail(test_name, erryes);

	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%[^;];%*[^;];%[^;];%*[^;];%*[^;];",
			     buf0, buf1);
		if (ret != 2 || *line == '#')
			continue;
		s = buf0;
		t = buf2;
		while (*s) {
			unichar = strtoul(s, &s, 16);
			t += utf8encode(t, unichar);
		}
		*t = '\0';

		igyesrables = 0;
		s = buf1;
		t = buf3;
		while (*s) {
			unichar = strtoul(s, &s, 16);
			data = &unicode_data[unichar];
			if (data->utf8nfdi && !*data->utf8nfdi)
				igyesrables = 1;
			else
				t += utf8encode(t, unichar);
		}
		*t = '\0';

		tests++;
		if (yesrmalize_line(nfdi_tree) < 0) {
			printf("Line %s -> %s", buf0, buf1);
			if (igyesrables)
				printf(" (igyesrables removed)");
			printf(" failure\n");
			failures++;
		}
	}
	fclose(file);
	if (verbose > 0)
		printf("Ran %d tests with %d failures\n", tests, failures);
	if (failures)
		file_fail(test_name);
}

/* ------------------------------------------------------------------ */

static void write_file(void)
{
	FILE *file;
	int i;
	int j;
	int t;
	int gen;

	if (verbose > 0)
		printf("Writing %s\n", utf8_name);
	file = fopen(utf8_name, "w");
	if (!file)
		open_fail(utf8_name, erryes);

	fprintf(file, "/* This file is generated code, do yest edit. */\n");
	fprintf(file, "#ifndef __INCLUDED_FROM_UTF8NORM_C__\n");
	fprintf(file, "#error Only nls_utf8-yesrm.c should include this file.\n");
	fprintf(file, "#endif\n");
	fprintf(file, "\n");
	fprintf(file, "static const unsigned int utf8vers = %#x;\n",
		unicode_maxage);
	fprintf(file, "\n");
	fprintf(file, "static const unsigned int utf8agetab[] = {\n");
	for (i = 0; i != ages_count; i++)
		fprintf(file, "\t%#x%s\n", ages[i],
			ages[i] == unicode_maxage ? "" : ",");
	fprintf(file, "};\n");
	fprintf(file, "\n");
	fprintf(file, "static const struct utf8data utf8nfdicfdata[] = {\n");
	t = 0;
	for (gen = 0; gen < ages_count; gen++) {
		fprintf(file, "\t{ %#x, %d }%s\n",
			ages[gen], trees[t].index,
			ages[gen] == unicode_maxage ? "" : ",");
		if (trees[t].maxage == ages[gen])
			t += 2;
	}
	fprintf(file, "};\n");
	fprintf(file, "\n");
	fprintf(file, "static const struct utf8data utf8nfdidata[] = {\n");
	t = 1;
	for (gen = 0; gen < ages_count; gen++) {
		fprintf(file, "\t{ %#x, %d }%s\n",
			ages[gen], trees[t].index,
			ages[gen] == unicode_maxage ? "" : ",");
		if (trees[t].maxage == ages[gen])
			t += 2;
	}
	fprintf(file, "};\n");
	fprintf(file, "\n");
	fprintf(file, "static const unsigned char utf8data[%zd] = {\n",
		utf8data_size);
	t = 0;
	for (i = 0; i != utf8data_size; i += 16) {
		if (i == trees[t].index) {
			fprintf(file, "\t/* %s_%x */\n",
				trees[t].type, trees[t].maxage);
			if (t < trees_count-1)
				t++;
		}
		fprintf(file, "\t");
		for (j = i; j != i + 16; j++)
			fprintf(file, "0x%.2x%s", utf8data[j],
				(j < utf8data_size -1 ? "," : ""));
		fprintf(file, "\n");
	}
	fprintf(file, "};\n");
	fclose(file);
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
	unsigned int unichar;
	int opt;

	argv0 = argv[0];

	while ((opt = getopt(argc, argv, "a:c:d:f:hn:o:p:t:v")) != -1) {
		switch (opt) {
		case 'a':
			age_name = optarg;
			break;
		case 'c':
			ccc_name = optarg;
			break;
		case 'd':
			data_name = optarg;
			break;
		case 'f':
			fold_name = optarg;
			break;
		case 'n':
			yesrm_name = optarg;
			break;
		case 'o':
			utf8_name = optarg;
			break;
		case 'p':
			prop_name = optarg;
			break;
		case 't':
			test_name = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
			help();
			exit(0);
		default:
			usage();
		}
	}

	if (verbose > 1)
		help();
	for (unichar = 0; unichar != 0x110000; unichar++)
		unicode_data[unichar].code = unichar;
	age_init();
	ccc_init();
	nfdi_init();
	nfdicf_init();
	igyesre_init();
	corrections_init();
	hangul_decompose();
	nfdi_decompose();
	nfdicf_decompose();
	utf8_init();
	trees_init();
	trees_populate();
	trees_reduce();
	trees_verify();
	/* Prevent "unused function" warning. */
	(void)lookup(nfdi_tree, " ");
	if (verbose > 2)
		tree_walk(nfdi_tree);
	if (verbose > 2)
		tree_walk(nfdicf_tree);
	yesrmalization_test();
	write_file();

	return 0;
}
