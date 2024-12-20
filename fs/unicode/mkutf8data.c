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
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* Generator for a compact trie for unicode normalization */

#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* Default names of the in- and output files. */

#define AGE_NAME	"DerivedAge.txt"
#define CCC_NAME	"DerivedCombiningClass.txt"
#define PROP_NAME	"DerivedCoreProperties.txt"
#define DATA_NAME	"UnicodeData.txt"
#define FOLD_NAME	"CaseFolding.txt"
#define NORM_NAME	"NormalizationCorrections.txt"
#define TEST_NAME	"NormalizationTest.txt"
#define UTF8_NAME	"utf8data.c"

const char	*age_name  = AGE_NAME;
const char	*ccc_name  = CCC_NAME;
const char	*prop_name = PROP_NAME;
const char	*data_name = DATA_NAME;
const char	*fold_name = FOLD_NAME;
const char	*norm_name = NORM_NAME;
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
 * Unicode version numbers consist of three parts: major, minor, and a
 * revision.  These numbers are packed into an unsigned int to obtain
 * a single version number.
 *
 * To save space in the generated trie, the unicode version is not
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

static int age_valid(unsigned int major, unsigned int minor,
		     unsigned int revision)
{
	if (major > UNICODE_MAJ_MAX)
		return 0;
	if (minor > UNICODE_MIN_MAX)
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
 * Internal nodes are one byte for the node itself, and up to three
 * bytes for an offset into the tree.  The first byte contains the
 * following information:
 *  NEXTBYTE  - flag        - advance to next byte if set
 *  BITNUM    - 3 bit field - the bit number to tested
 *  OFFLEN    - 2 bit field - number of bytes in the offset
 * if offlen == 0 (non-branching node)
 *  RIGHTPATH - 1 bit field - set if the following node is for the
 *                            right-hand path (tested bit is set)
 *  TRIENODE  - 1 bit field - set if the following node is an internal
 *                            node, otherwise it is a leaf node
 * if offlen != 0 (branching node)
 *  LEFTNODE  - 1 bit field - set if the left-hand node is internal
 *  RIGHTNODE - 1 bit field - set if the right-hand node is internal
 *
 * Due to the way utf8 works, there cannot be branching nodes with
 * NEXTBYTE set, and moreover those nodes always have a righthand
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
 *          defined.  The CCC of a non-defined code point is 0.
 * leaf[1]: Canonical Combining Class. During normalization, we need
 *          to do a stable sort into ascending order of all characters
 *          with a non-zero CCC that occur between two characters with
 *          a CCC of 0, or at the begin or end of a string.
 *          The unicode standard guarantees that all CCC values are
 *          between 0 and 254 inclusive, which leaves 255 available as
 *          a special value.
 *          Code points with CCC 0 are known as stoppers.
 * leaf[2]: Decomposition. If leaf[1] == 255, then leaf[2] is the
 *          start of a NUL-terminated string that is the decomposition
 *          of the character.
 *          The CCC of a decomposable character is the same as the CCC
 *          of the first character of its decomposition.
 *          Some characters decompose as the empty string: these are
 *          characters with the Default_Ignorable_Code_Point property.
 *          These do affect normalization, as they all have CCC 0.
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
 * must not decode sequences that do not satisfy this requirement.
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
 * Even within those ranges not all values are allowed: the surrogates
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
	int childnode;
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

struct node {
	int index;
	int offset;
	int mark;
	int size;
	struct node *parent;
	void *left;
	void *right;
	unsigned char bitnum;
	unsigned char nextbyte;
	unsigned char leftnode;
	unsigned char rightnode;
	unsigned int keybits;
	unsigned int keymask;
};

/*
 * Example lookup function for a tree.
 */
static void *lookup(struct tree *tree, const char *key)
{
	struct node *node;
	void *leaf = NULL;

	node = tree->root;
	while (!leaf && node) {
		if (node->nextbyte)
			key++;
		if (*key & (1 << (node->bitnum & 7))) {
			/* Right leg */
			if (node->rightnode == NODE) {
				node = node->right;
			} else if (node->rightnode == LEAF) {
				leaf = node->right;
			} else {
				node = NULL;
			}
		} else {
			/* Left leg */
			if (node->leftnode == NODE) {
				node = node->left;
			} else if (node->leftnode == LEAF) {
				leaf = node->left;
			} else {
				node = NULL;
			}
		}
	}

	return leaf;
}

/*
 * A simple non-recursive tree walker: keep track of visits to the
 * left and right branches in the leftmask and rightmask.
 */
static void tree_walk(struct tree *tree)
{
	struct node *node;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	int indent = 1;
	int nodes, singletons, leaves;

	nodes = singletons = leaves = 0;

	printf("%s_%x root %p\n", tree->type, tree->maxage, tree->root);
	if (tree->childnode == LEAF) {
		assert(tree->root);
		tree->leaf_print(tree->root, indent);
		leaves = 1;
	} else {
		assert(tree->childnode == NODE);
		node = tree->root;
		leftmask = rightmask = 0;
		while (node) {
			printf("%*snode @ %p bitnum %d nextbyte %d"
			       " left %p right %p mask %x bits %x\n",
				indent, "", node,
				node->bitnum, node->nextbyte,
				node->left, node->right,
				node->keymask, node->keybits);
			nodes += 1;
			if (!(node->left && node->right))
				singletons += 1;

			while (node) {
				bitmask = 1 << node->bitnum;
				if ((leftmask & bitmask) == 0) {
					leftmask |= bitmask;
					if (node->leftnode == LEAF) {
						assert(node->left);
						tree->leaf_print(node->left,
								 indent+1);
						leaves += 1;
					} else if (node->left) {
						assert(node->leftnode == NODE);
						indent += 1;
						node = node->left;
						break;
					}
				}
				if ((rightmask & bitmask) == 0) {
					rightmask |= bitmask;
					if (node->rightnode == LEAF) {
						assert(node->right);
						tree->leaf_print(node->right,
								 indent+1);
						leaves += 1;
					} else if (node->right) {
						assert(node->rightnode == NODE);
						indent += 1;
						node = node->right;
						break;
					}
				}
				leftmask &= ~bitmask;
				rightmask &= ~bitmask;
				node = node->parent;
				indent -= 1;
			}
		}
	}
	printf("nodes %d leaves %d singletons %d\n",
	       nodes, leaves, singletons);
}

/*
 * Allocate an initialize a new internal node.
 */
static struct node *alloc_node(struct node *parent)
{
	struct node *node;
	int bitnum;

	node = malloc(sizeof(*node));
	node->left = node->right = NULL;
	node->parent = parent;
	node->leftnode = NODE;
	node->rightnode = NODE;
	node->keybits = 0;
	node->keymask = 0;
	node->mark = 0;
	node->index = 0;
	node->offset = -1;
	node->size = 4;

	if (node->parent) {
		bitnum = parent->bitnum;
		if ((bitnum & 7) == 0) {
			node->bitnum = bitnum + 7 + 8;
			node->nextbyte = 1;
		} else {
			node->bitnum = bitnum - 1;
			node->nextbyte = 0;
		}
	} else {
		node->bitnum = 7;
		node->nextbyte = 0;
	}

	return node;
}

/*
 * Insert a new leaf into the tree, and collapse any subtrees that are
 * fully populated and end in identical leaves. A nextbyte tagged
 * internal node will not be removed to preserve the tree's integrity.
 * Note that due to the structure of utf8, no nextbyte tagged node
 * will be a candidate for removal.
 */
static int insert(struct tree *tree, char *key, int keylen, void *leaf)
{
	struct node *node;
	struct node *parent;
	void **cursor;
	int keybits;

	assert(keylen >= 1 && keylen <= 4);

	node = NULL;
	cursor = &tree->root;
	keybits = 8 * keylen;

	/* Insert, creating path along the way. */
	while (keybits) {
		if (!*cursor)
			*cursor = alloc_node(node);
		node = *cursor;
		if (node->nextbyte)
			key++;
		if (*key & (1 << (node->bitnum & 7)))
			cursor = &node->right;
		else
			cursor = &node->left;
		keybits--;
	}
	*cursor = leaf;

	/* Merge subtrees if possible. */
	while (node) {
		if (*key & (1 << (node->bitnum & 7)))
			node->rightnode = LEAF;
		else
			node->leftnode = LEAF;
		if (node->nextbyte)
			break;
		if (node->leftnode == NODE || node->rightnode == NODE)
			break;
		assert(node->left);
		assert(node->right);
		/* Compare */
		if (! tree->leaf_equal(node->left, node->right))
			break;
		/* Keep left, drop right leaf. */
		leaf = node->left;
		/* Check in parent */
		parent = node->parent;
		if (!parent) {
			/* root of tree! */
			tree->root = leaf;
			tree->childnode = LEAF;
		} else if (parent->left == node) {
			parent->left = leaf;
			parent->leftnode = LEAF;
			if (parent->right) {
				parent->keymask = 0;
				parent->keybits = 0;
			} else {
				parent->keymask |= (1 << node->bitnum);
			}
		} else if (parent->right == node) {
			parent->right = leaf;
			parent->rightnode = LEAF;
			if (parent->left) {
				parent->keymask = 0;
				parent->keybits = 0;
			} else {
				parent->keymask |= (1 << node->bitnum);
				parent->keybits |= (1 << node->bitnum);
			}
		} else {
			/* internal tree error */
			assert(0);
		}
		free(node);
		node = parent;
	}

	/* Propagate keymasks up along singleton chains. */
	while (node) {
		parent = node->parent;
		if (!parent)
			break;
		/* Nix the mask for parents with two children. */
		if (node->keymask == 0) {
			parent->keymask = 0;
			parent->keybits = 0;
		} else if (parent->left && parent->right) {
			parent->keymask = 0;
			parent->keybits = 0;
		} else {
			assert((parent->keymask & node->keymask) == 0);
			parent->keymask |= node->keymask;
			parent->keymask |= (1 << parent->bitnum);
			parent->keybits |= node->keybits;
			if (parent->right)
				parent->keybits |= (1 << parent->bitnum);
		}
		node = parent;
	}

	return 0;
}

/*
 * Prune internal nodes.
 *
 * Fully populated subtrees that end at the same leaf have already
 * been collapsed.  There are still internal nodes that have for both
 * their left and right branches a sequence of singletons that make
 * identical choices and end in identical leaves.  The keymask and
 * keybits collected in the nodes describe the choices made in these
 * singleton chains.  When they are identical for the left and right
 * branch of a node, and the two leaves comare identical, the node in
 * question can be removed.
 *
 * Note that nodes with the nextbyte tag set will not be removed by
 * this to ensure tree integrity.  Note as well that the structure of
 * utf8 ensures that these nodes would not have been candidates for
 * removal in any case.
 */
static void prune(struct tree *tree)
{
	struct node *node;
	struct node *left;
	struct node *right;
	struct node *parent;
	void *leftleaf;
	void *rightleaf;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	int count;

	if (verbose > 0)
		printf("Pruning %s_%x\n", tree->type, tree->maxage);

	count = 0;
	if (tree->childnode == LEAF)
		return;
	if (!tree->root)
		return;

	leftmask = rightmask = 0;
	node = tree->root;
	while (node) {
		if (node->nextbyte)
			goto advance;
		if (node->leftnode == LEAF)
			goto advance;
		if (node->rightnode == LEAF)
			goto advance;
		if (!node->left)
			goto advance;
		if (!node->right)
			goto advance;
		left = node->left;
		right = node->right;
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
			if (left->leftnode == LEAF)
				leftleaf = left->left;
			else if (left->rightnode == LEAF)
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
			if (right->leftnode == LEAF)
				rightleaf = right->left;
			else if (right->rightnode == LEAF)
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
		 * This node has identical singleton-only subtrees.
		 * Remove it.
		 */
		parent = node->parent;
		left = node->left;
		right = node->right;
		if (parent->left == node)
			parent->left = left;
		else if (parent->right == node)
			parent->right = left;
		else
			assert(0);
		left->parent = parent;
		left->keymask |= (1 << node->bitnum);
		node->left = NULL;
		while (node) {
			bitmask = 1 << node->bitnum;
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			if (node->leftnode == NODE && node->left) {
				left = node->left;
				free(node);
				count++;
				node = left;
			} else if (node->rightnode == NODE && node->right) {
				right = node->right;
				free(node);
				count++;
				node = right;
			} else {
				node = NULL;
			}
		}
		/* Propagate keymasks up along singleton chains. */
		node = parent;
		/* Force re-check */
		bitmask = 1 << node->bitnum;
		leftmask &= ~bitmask;
		rightmask &= ~bitmask;
		for (;;) {
			if (node->left && node->right)
				break;
			if (node->left) {
				left = node->left;
				node->keymask |= left->keymask;
				node->keybits |= left->keybits;
			}
			if (node->right) {
				right = node->right;
				node->keymask |= right->keymask;
				node->keybits |= right->keybits;
			}
			node->keymask |= (1 << node->bitnum);
			node = node->parent;
			/* Force re-check */
			bitmask = 1 << node->bitnum;
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
		}
	advance:
		bitmask = 1 << node->bitnum;
		if ((leftmask & bitmask) == 0 &&
		    node->leftnode == NODE &&
		    node->left) {
			leftmask |= bitmask;
			node = node->left;
		} else if ((rightmask & bitmask) == 0 &&
			   node->rightnode == NODE &&
			   node->right) {
			rightmask |= bitmask;
			node = node->right;
		} else {
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			node = node->parent;
		}
	}
	if (verbose > 0)
		printf("Pruned %d nodes\n", count);
}

/*
 * Mark the nodes in the tree that lead to leaves that must be
 * emitted.
 */
static void mark_nodes(struct tree *tree)
{
	struct node *node;
	struct node *n;
	unsigned int leftmask;
	unsigned int rightmask;
	unsigned int bitmask;
	int marked;

	marked = 0;
	if (verbose > 0)
		printf("Marking %s_%x\n", tree->type, tree->maxage);
	if (tree->childnode == LEAF)
		goto done;

	assert(tree->childnode == NODE);
	node = tree->root;
	leftmask = rightmask = 0;
	while (node) {
		bitmask = 1 << node->bitnum;
		if ((leftmask & bitmask) == 0) {
			leftmask |= bitmask;
			if (node->leftnode == LEAF) {
				assert(node->left);
				if (tree->leaf_mark(node->left)) {
					n = node;
					while (n && !n->mark) {
						marked++;
						n->mark = 1;
						n = n->parent;
					}
				}
			} else if (node->left) {
				assert(node->leftnode == NODE);
				node = node->left;
				continue;
			}
		}
		if ((rightmask & bitmask) == 0) {
			rightmask |= bitmask;
			if (node->rightnode == LEAF) {
				assert(node->right);
				if (tree->leaf_mark(node->right)) {
					n = node;
					while (n && !n->mark) {
						marked++;
						n->mark = 1;
						n = n->parent;
					}
				}
			} else if (node->right) {
				assert(node->rightnode == NODE);
				node = node->right;
				continue;
			}
		}
		leftmask &= ~bitmask;
		rightmask &= ~bitmask;
		node = node->parent;
	}

	/* second pass: left siblings and singletons */

	assert(tree->childnode == NODE);
	node = tree->root;
	leftmask = rightmask = 0;
	while (node) {
		bitmask = 1 << node->bitnum;
		if ((leftmask & bitmask) == 0) {
			leftmask |= bitmask;
			if (node->leftnode == LEAF) {
				assert(node->left);
				if (tree->leaf_mark(node->left)) {
					n = node;
					while (n && !n->mark) {
						marked++;
						n->mark = 1;
						n = n->parent;
					}
				}
			} else if (node->left) {
				assert(node->leftnode == NODE);
				node = node->left;
				if (!node->mark && node->parent->mark) {
					marked++;
					node->mark = 1;
				}
				continue;
			}
		}
		if ((rightmask & bitmask) == 0) {
			rightmask |= bitmask;
			if (node->rightnode == LEAF) {
				assert(node->right);
				if (tree->leaf_mark(node->right)) {
					n = node;
					while (n && !n->mark) {
						marked++;
						n->mark = 1;
						n = n->parent;
					}
				}
			} else if (node->right) {
				assert(node->rightnode == NODE);
				node = node->right;
				if (!node->mark && node->parent->mark &&
				    !node->parent->left) {
					marked++;
					node->mark = 1;
				}
				continue;
			}
		}
		leftmask &= ~bitmask;
		rightmask &= ~bitmask;
		node = node->parent;
	}
done:
	if (verbose > 0)
		printf("Marked %d nodes\n", marked);
}

/*
 * Compute the index of each node and leaf, which is the offset in the
 * emitted trie.  These values must be pre-computed because relative
 * offsets between nodes are used to navigate the tree.
 */
static int index_nodes(struct tree *tree, int index)
{
	struct node *node;
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
	if (tree->childnode == LEAF) {
		index += tree->leaf_size(tree->root);
		goto done;
	}

	assert(tree->childnode == NODE);
	node = tree->root;
	leftmask = rightmask = 0;
	while (node) {
		if (!node->mark)
			goto skip;
		count++;
		if (node->index != index)
			node->index = index;
		index += node->size;
skip:
		while (node) {
			bitmask = 1 << node->bitnum;
			if (node->mark && (leftmask & bitmask) == 0) {
				leftmask |= bitmask;
				if (node->leftnode == LEAF) {
					assert(node->left);
					*tree->leaf_index(tree, node->left) =
									index;
					index += tree->leaf_size(node->left);
					count++;
				} else if (node->left) {
					assert(node->leftnode == NODE);
					indent += 1;
					node = node->left;
					break;
				}
			}
			if (node->mark && (rightmask & bitmask) == 0) {
				rightmask |= bitmask;
				if (node->rightnode == LEAF) {
					assert(node->right);
					*tree->leaf_index(tree, node->right) = index;
					index += tree->leaf_size(node->right);
					count++;
				} else if (node->right) {
					assert(node->rightnode == NODE);
					indent += 1;
					node = node->right;
					break;
				}
			}
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			node = node->parent;
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
 * Mark the nodes in a subtree, helper for size_nodes().
 */
static int mark_subtree(struct node *node)
{
	int changed;

	if (!node || node->mark)
		return 0;
	node->mark = 1;
	node->index = node->parent->index;
	changed = 1;
	if (node->leftnode == NODE)
		changed += mark_subtree(node->left);
	if (node->rightnode == NODE)
		changed += mark_subtree(node->right);
	return changed;
}

/*
 * Compute the size of nodes and leaves. We start by assuming that
 * each node needs to store a three-byte offset. The indexes of the
 * nodes are calculated based on that, and then this function is
 * called to see if the sizes of some nodes can be reduced.  This is
 * repeated until no more changes are seen.
 */
static int size_nodes(struct tree *tree)
{
	struct tree *next;
	struct node *node;
	struct node *right;
	struct node *n;
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
	if (tree->childnode == LEAF)
		goto done;

	assert(tree->childnode == NODE);
	pathbits = 0;
	pathmask = 0;
	node = tree->root;
	leftmask = rightmask = 0;
	while (node) {
		if (!node->mark)
			goto skip;
		offset = 0;
		if (!node->left || !node->right) {
			size = 1;
		} else {
			if (node->rightnode == NODE) {
				/*
				 * If the right node is not marked,
				 * look for a corresponding node in
				 * the next tree.  Such a node need
				 * not exist.
				 */
				right = node->right;
				next = tree->next;
				while (!right->mark) {
					assert(next);
					n = next->root;
					while (n->bitnum != node->bitnum) {
						nbit = 1 << n->bitnum;
						if (!(pathmask & nbit))
							break;
						if (pathbits & nbit) {
							if (n->rightnode == LEAF)
								break;
							n = n->right;
						} else {
							if (n->leftnode == LEAF)
								break;
							n = n->left;
						}
					}
					if (n->bitnum != node->bitnum)
						break;
					n = n->right;
					right = n;
					next = next->next;
				}
				/* Make sure the right node is marked. */
				if (!right->mark)
					changed += mark_subtree(right);
				offset = right->index - node->index;
			} else {
				offset = *tree->leaf_index(tree, node->right);
				offset -= node->index;
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
		if (node->size != size || node->offset != offset) {
			node->size = size;
			node->offset = offset;
			changed++;
		}
skip:
		while (node) {
			bitmask = 1 << node->bitnum;
			pathmask |= bitmask;
			if (node->mark && (leftmask & bitmask) == 0) {
				leftmask |= bitmask;
				if (node->leftnode == LEAF) {
					assert(node->left);
				} else if (node->left) {
					assert(node->leftnode == NODE);
					indent += 1;
					node = node->left;
					break;
				}
			}
			if (node->mark && (rightmask & bitmask) == 0) {
				rightmask |= bitmask;
				pathbits |= bitmask;
				if (node->rightnode == LEAF) {
					assert(node->right);
				} else if (node->right) {
					assert(node->rightnode == NODE);
					indent += 1;
					node = node->right;
					break;
				}
			}
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			pathmask &= ~bitmask;
			pathbits &= ~bitmask;
			node = node->parent;
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
	struct node *node;
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
	int nodes[4];
	unsigned char byte;

	nodes[0] = nodes[1] = nodes[2] = nodes[3] = 0;
	leaves = 0;
	bytes = 0;
	index = tree->index;
	data += index;
	indent = 1;
	if (verbose > 0)
		printf("Emitting %s_%x\n", tree->type, tree->maxage);
	if (tree->childnode == LEAF) {
		assert(tree->root);
		tree->leaf_emit(tree->root, data);
		size = tree->leaf_size(tree->root);
		index += size;
		leaves++;
		goto done;
	}

	assert(tree->childnode == NODE);
	node = tree->root;
	leftmask = rightmask = 0;
	while (node) {
		if (!node->mark)
			goto skip;
		assert(node->offset != -1);
		assert(node->index == index);

		byte = 0;
		if (node->nextbyte)
			byte |= NEXTBYTE;
		byte |= (node->bitnum & BITNUM);
		if (node->left && node->right) {
			if (node->leftnode == NODE)
				byte |= LEFTNODE;
			if (node->rightnode == NODE)
				byte |= RIGHTNODE;
			if (node->offset <= 0xff)
				offlen = 1;
			else if (node->offset <= 0xffff)
				offlen = 2;
			else
				offlen = 3;
			nodes[offlen]++;
			offset = node->offset;
			byte |= offlen << OFFLEN_SHIFT;
			*data++ = byte;
			index++;
			while (offlen--) {
				*data++ = offset & 0xff;
				index++;
				offset >>= 8;
			}
		} else if (node->left) {
			if (node->leftnode == NODE)
				byte |= TRIENODE;
			nodes[0]++;
			*data++ = byte;
			index++;
		} else if (node->right) {
			byte |= RIGHTNODE;
			if (node->rightnode == NODE)
				byte |= TRIENODE;
			nodes[0]++;
			*data++ = byte;
			index++;
		} else {
			assert(0);
		}
skip:
		while (node) {
			bitmask = 1 << node->bitnum;
			if (node->mark && (leftmask & bitmask) == 0) {
				leftmask |= bitmask;
				if (node->leftnode == LEAF) {
					assert(node->left);
					data = tree->leaf_emit(node->left,
							       data);
					size = tree->leaf_size(node->left);
					index += size;
					bytes += size;
					leaves++;
				} else if (node->left) {
					assert(node->leftnode == NODE);
					indent += 1;
					node = node->left;
					break;
				}
			}
			if (node->mark && (rightmask & bitmask) == 0) {
				rightmask |= bitmask;
				if (node->rightnode == LEAF) {
					assert(node->right);
					data = tree->leaf_emit(node->right,
							       data);
					size = tree->leaf_size(node->right);
					index += size;
					bytes += size;
					leaves++;
				} else if (node->right) {
					assert(node->rightnode == NODE);
					indent += 1;
					node = node->right;
					break;
				}
			}
			leftmask &= ~bitmask;
			rightmask &= ~bitmask;
			node = node->parent;
			indent -= 1;
		}
	}
done:
	if (verbose > 0) {
		printf("Emitted %d (%d) leaves",
			leaves, bytes);
		printf(" %d (%d+%d+%d+%d) nodes",
			nodes[0] + nodes[1] + nodes[2] + nodes[3],
			nodes[0], nodes[1], nodes[2], nodes[3]);
		printf(" %d total\n", index - tree->index);
	}
}

/* ------------------------------------------------------------------ */

/*
 * Unicode data.
 *
 * We need to keep track of the Canonical Combining Class, the Age,
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
		trees[i].childnode = NODE;
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
		mark_nodes(&trees[i]);
	do {
		size = 0;
		for (i = 0; i != trees_count; i++)
			size = index_nodes(&trees[i], size);
		changed = 0;
		for (i = 0; i != trees_count; i++)
			changed += size_nodes(&trees[i]);
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
	int		nocf;

	if (verbose > 0)
		printf("Verifying %s_%x\n", tree->type, tree->maxage);
	nocf = strcmp(tree->type, "nfdicf");

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
				} else if (nocf) {
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
	printf("normalization of UTF-8 strings. The trie is derived from\n");
	printf("a set of input files from the Unicode character database\n");
	printf("found at: http://www.unicode.org/Public/UCD/latest/ucd/\n");
	printf("\n");
	printf("The generated tree supports two normalization forms:\n");
	printf("\n");
	printf("\tnfdi:\n");
	printf("\t- Apply unicode normalization form NFD.\n");
	printf("\t- Remove any Default_Ignorable_Code_Point.\n");
	printf("\n");
	printf("\tnfdicf:\n");
	printf("\t- Apply unicode normalization form NFD.\n");
	printf("\t- Remove any Default_Ignorable_Code_Point.\n");
	printf("\t- Apply a full casefold (C + F).\n");
	printf("\n");
	printf("These forms were chosen as being most useful when dealing\n");
	printf("with file names: NFD catches most cases where characters\n");
	printf("should be considered equivalent. The ignorables are mostly\n");
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
	unsigned int minor;
	unsigned int revision;
	int gen;
	int count;
	int ret;

	if (verbose > 0)
		printf("Parsing %s\n", age_name);

	file = fopen(age_name, "r");
	if (!file)
		open_fail(age_name, errno);
	count = 0;

	gen = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "# Age=V%d_%d_%d",
				&major, &minor, &revision);
		if (ret == 3) {
			ages_count++;
			if (verbose > 1)
				printf(" Age V%d_%d_%d\n",
					major, minor, revision);
			if (!age_valid(major, minor, revision))
				line_fail(age_name, line);
			continue;
		}
		ret = sscanf(line, "# Age=V%d_%d", &major, &minor);
		if (ret == 2) {
			ages_count++;
			if (verbose > 1)
				printf(" Age V%d_%d\n", major, minor);
			if (!age_valid(major, minor, 0))
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
				&major, &minor, &revision);
		if (ret == 3) {
			ages[++gen] =
				UNICODE_AGE(major, minor, revision);
			if (verbose > 1)
				printf(" Age V%d_%d_%d = gen %d\n",
					major, minor, revision, gen);
			if (!age_valid(major, minor, revision))
				line_fail(age_name, line);
			continue;
		}
		ret = sscanf(line, "# Age=V%d_%d", &major, &minor);
		if (ret == 2) {
			ages[++gen] = UNICODE_AGE(major, minor, 0);
			if (verbose > 1)
				printf(" Age V%d_%d = %d\n",
					major, minor, gen);
			if (!age_valid(major, minor, 0))
				line_fail(age_name, line);
			continue;
		}
		ret = sscanf(line, "%X..%X ; %d.%d #",
			     &first, &last, &major, &minor);
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
		ret = sscanf(line, "%X ; %d.%d #", &unichar, &major, &minor);
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
		open_fail(ccc_name, errno);

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

static int ignore_compatibility_form(char *type)
{
	int i;
	char *ignored_types[] = {"font", "noBreak", "initial", "medial",
				 "final", "isolated", "circle", "super",
				 "sub", "vertical", "wide", "narrow",
				 "small", "square", "fraction", "compat"};

	for (i = 0 ; i < ARRAY_SIZE(ignored_types); i++)
		if (strcmp(type, ignored_types[i]) == 0)
			return 1;
	return 0;
}

static void nfdi_init(void)
{
	FILE *file;
	unsigned int unichar;
	unsigned int mapping[19]; /* Magic - guaranteed not to be exceeded. */
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
		open_fail(data_name, errno);

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
			if(ignore_compatibility_form(type))
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
	unsigned int mapping[19]; /* Magic - guaranteed not to be exceeded. */
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
		open_fail(fold_name, errno);

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

static void ignore_init(void)
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
		open_fail(prop_name, errno);
	assert(file);
	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X..%X ; %s # ", &first, &last, buf0);
		if (ret == 3) {
			if (strcmp(buf0, "Default_Ignorable_Code_Point"))
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
				printf(" %X..%X Default_Ignorable_Code_Point\n",
					first, last);
			continue;
		}
		ret = sscanf(line, "%X ; %s # ", &unichar, buf0);
		if (ret == 2) {
			if (strcmp(buf0, "Default_Ignorable_Code_Point"))
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
				printf(" %X Default_Ignorable_Code_Point\n",
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
	unsigned int minor;
	unsigned int revision;
	unsigned int age;
	unsigned int *um;
	unsigned int mapping[19]; /* Magic - guaranteed not to be exceeded. */
	char *s;
	int i;
	int count;
	int ret;

	if (verbose > 0)
		printf("Parsing %s\n", norm_name);
	file = fopen(norm_name, "r");
	if (!file)
		open_fail(norm_name, errno);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X;%[^;];%[^;];%d.%d.%d #",
				&unichar, buf0, buf1,
				&major, &minor, &revision);
		if (ret != 6)
			continue;
		if (!utf32valid(unichar) || !age_valid(major, minor, revision))
			line_fail(norm_name, line);
		count++;
	}
	corrections = calloc(count, sizeof(struct unicode_data));
	corrections_count = count;
	rewind(file);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X;%[^;];%[^;];%d.%d.%d #",
				&unichar, buf0, buf1,
				&major, &minor, &revision);
		if (ret != 6)
			continue;
		if (!utf32valid(unichar) || !age_valid(major, minor, revision))
			line_fail(norm_name, line);
		corrections[count] = unicode_data[unichar];
		assert(corrections[count].code == unichar);
		age = UNICODE_AGE(major, minor, revision);
		corrections[count].correction = age;

		i = 0;
		s = buf0;
		while (*s) {
			mapping[i] = strtoul(s, &s, 16);
			if (!utf32valid(mapping[i]))
				line_fail(norm_name, line);
			i++;
		}
		mapping[i++] = 0;

		um = malloc(i * sizeof(unsigned int));
		memcpy(um, mapping, i * sizeof(unsigned int));
		corrections[count].utf32nfdi = um;

		if (verbose > 1)
			printf(" %X -> %s -> %s V%d_%d_%d\n",
				unichar, buf0, buf1, major, minor, revision);
		count++;
	}
	fclose(file);

	if (verbose > 0)
	        printf("Found %d entries\n", count);
	if (count == 0)
		file_fail(norm_name);
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
 * LV (Canonical/Full)
 *   LIndex = SIndex / NCount
 *   VIndex = (Sindex % NCount) / TCount
 *   LPart = LBase + LIndex
 *   VPart = VBase + VIndex
 *
 * LVT (Canonical)
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
		 * decompositions must not be stored in the generated
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
	unsigned int mapping[19]; /* Magic - guaranteed not to be exceeded. */
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
		/* Add this decomposition to nfdicf if there is no entry. */
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
	unsigned int mapping[19]; /* Magic - guaranteed not to be exceeded. */
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
 * LV (Canonical/Full)
 *   LIndex = SIndex / NCount
 *   VIndex = (Sindex % NCount) / TCount
 *   LPart = LBase + LIndex
 *   VPart = VBase + VIndex
 *
 * LVT (Canonical)
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
 * A non-NULL return guarantees that the UTF-8 sequence starting at s
 * is well-formed and corresponds to a known unicode code point.  The
 * shorthand for this will be "is valid UTF-8 unicode".
 */
static utf8leaf_t *utf8nlookup(struct tree *tree, unsigned char *hangul,
			       const char *s, size_t len)
{
	utf8trie_t	*trie;
	int		offlen;
	int		offset;
	int		mask;
	int		node;

	if (!tree)
		return NULL;
	if (len == 0)
		return NULL;
	node = 1;
	trie = utf8data + tree->index;
	while (node) {
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
				/* Right node at offset of trie */
				node = (*trie & RIGHTNODE);
				offset = trie[offlen];
				while (--offlen) {
					offset <<= 8;
					offset |= trie[offlen];
				}
				trie += offset;
			} else if (*trie & RIGHTPATH) {
				/* Right node after this node */
				node = (*trie & TRIENODE);
				trie++;
			} else {
				/* No right node. */
				return NULL;
			}
		} else {
			/* Left leg */
			if (offlen) {
				/* Left node after this node. */
				node = (*trie & LEFTNODE);
				trie += offlen + 1;
			} else if (*trie & RIGHTPATH) {
				/* No left node. */
				return NULL;
			} else {
				/* Left node after this node */
				node = (*trie & TRIENODE);
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
 * Return -1 if s is not valid UTF-8 unicode.
 * Return 0 if only non-assigned code points are used.
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
 * Return -1 if s is not valid UTF-8 unicode.
 * Return 0 if non-assigned code points are used.
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
 * Return -1 if s is not valid UTF-8 unicode.
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
 * Return -1 if s is not valid UTF-8 unicode.
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
 * Length of the normalization of s.
 * Return -1 if s is not valid UTF-8 unicode.
 *
 * A string of Default_Ignorable_Code_Point has length 0.
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
 * Length of the normalization of s, touch at most len bytes.
 * Return -1 if s is not valid UTF-8 unicode.
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
 * Cursor structure used by the normalizer.
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
 *   trie   : utf8trie_t to use for normalization.
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
	/* The first byte of s may not be an utf8 continuation. */
	if (len > 0 && (*s & 0xC0) == 0x80)
		return -1;
	return 0;
}

/*
 * Set up an utf8cursor for use by utf8byte().
 *
 *   s      : NUL-terminated string.
 *   u8c    : pointer to cursor.
 *   trie   : utf8trie_t to use for normalization.
 *
 * Returns -1 on error, 0 on success.
 */
int utf8cursor(struct utf8cursor *u8c, struct tree *tree, const char *s)
{
	return utf8ncursor(u8c, tree, s, (unsigned int)-1);
}

/*
 * Get one byte from the normalized form of the string described by u8c.
 *
 * Returns the byte cast to an unsigned char on succes, and -1 on failure.
 *
 * The cursor keeps track of the location in the string in u8c->s.
 * When a character is decomposed, the current location is stored in
 * u8c->p, and u8c->s is set to the start of the decomposition. Note
 * that bytes from a decomposition do not count against u8c->len.
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
			/* There is no next byte. */
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
		 * If this is not a stopper, then see if it updates
		 * the next canonical class to be emitted.
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
			 * Scan forward for the first canonical class
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
			/* Not a stopper, and not the ccc we're emitting. */
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

static int normalize_line(struct tree *tree)
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

static void normalization_test(void)
{
	FILE *file;
	unsigned int unichar;
	struct unicode_data *data;
	char *s;
	char *t;
	int ret;
	int ignorables;
	int tests = 0;
	int failures = 0;

	if (verbose > 0)
		printf("Parsing %s\n", test_name);
	/* Step one, read data from file. */
	file = fopen(test_name, "r");
	if (!file)
		open_fail(test_name, errno);

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

		ignorables = 0;
		s = buf1;
		t = buf3;
		while (*s) {
			unichar = strtoul(s, &s, 16);
			data = &unicode_data[unichar];
			if (data->utf8nfdi && !*data->utf8nfdi)
				ignorables = 1;
			else
				t += utf8encode(t, unichar);
		}
		*t = '\0';

		tests++;
		if (normalize_line(nfdi_tree) < 0) {
			printf("Line %s -> %s", buf0, buf1);
			if (ignorables)
				printf(" (ignorables removed)");
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
		open_fail(utf8_name, errno);

	fprintf(file, "/* This file is generated code, do not edit. */\n");
	fprintf(file, "\n");
	fprintf(file, "#include <linux/module.h>\n");
	fprintf(file, "#include <linux/kernel.h>\n");
	fprintf(file, "#include \"utf8n.h\"\n");
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
	fprintf(file, "\n");
	fprintf(file, "const struct utf8data_table utf8_data_table = {\n");
	fprintf(file, "\t.utf8agetab = utf8agetab,\n");
	fprintf(file, "\t.utf8agetab_size = ARRAY_SIZE(utf8agetab),\n");
	fprintf(file, "\n");
	fprintf(file, "\t.utf8nfdicfdata = utf8nfdicfdata,\n");
	fprintf(file, "\t.utf8nfdicfdata_size = ARRAY_SIZE(utf8nfdicfdata),\n");
	fprintf(file, "\n");
	fprintf(file, "\t.utf8nfdidata = utf8nfdidata,\n");
	fprintf(file, "\t.utf8nfdidata_size = ARRAY_SIZE(utf8nfdidata),\n");
	fprintf(file, "\n");
	fprintf(file, "\t.utf8data = utf8data,\n");
	fprintf(file, "};\n");
	fprintf(file, "EXPORT_SYMBOL_GPL(utf8_data_table);");
	fprintf(file, "\n");
	fprintf(file, "MODULE_DESCRIPTION(\"UTF8 data table\");\n");
	fprintf(file, "MODULE_LICENSE(\"GPL v2\");\n");
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
			norm_name = optarg;
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
	ignore_init();
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
	normalization_test();
	write_file();

	return 0;
}
