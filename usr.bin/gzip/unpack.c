/*	$FreeBSD$	*/
/*	$NetBSD: unpack.c,v 1.3 2017/08/04 07:27:08 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Xin LI <delphij@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* This file is #included by gzip.c */

/*
 * pack(1) file format:
 *
 * The first 7 bytes is the header:
 *	00, 01 - Signature (US, RS), we already validated it earlier.
 *	02..05 - Uncompressed size
 *	    06 - Level for the huffman tree (<=24)
 *
 * pack(1) will then store symbols (leaf) nodes count in each huffman
 * tree levels, each level would consume 1 byte (See [1]).
 *
 * After the symbol count table, there is the symbol table, storing
 * symbols represented by corresponding leaf node.  EOB is not being
 * explicitly transmitted (not necessary anyway) in the symbol table.
 *
 * Compressed data goes after the symbol table.
 *
 * NOTES
 *
 * [1] If we count EOB into the symbols, that would mean that we will
 * have at most 256 symbols in the huffman tree.  pack(1) rejects empty
 * file and files that just repeats one character, which means that we
 * will have at least 2 symbols.  Therefore, pack(1) would reduce the
 * last level symbol count by 2 which makes it a number in
 * range [0..254], so all levels' symbol count would fit into 1 byte.
 */

#define	PACK_HEADER_LENGTH	7
#define	HTREE_MAXLEVEL		24

/*
 * unpack descriptor
 *
 * Represent the huffman tree in a similar way that pack(1) would
 * store in a packed file.  We store all symbols in a linear table,
 * and store pointers to each level's first symbol.  In addition to
 * that, maintain two counts for each level: inner nodes count and
 * leaf nodes count.
 */
typedef struct {
	int	symbol_size;		/* Size of the symbol table */
	int	treelevels;		/* Levels for the huffman tree */

	int    *symbolsin;		/* Table of leaf symbols count in each
					 * level */
	int    *inodesin;		/* Table of internal nodes count in
					 * each level */

	char   *symbol;			/* The symbol table */
	char   *symbol_eob;		/* Pointer to the EOB symbol */
	char  **tree;			/* Decoding huffman tree (pointers to
					 * first symbol of each tree level */

	off_t	uncompressed_size;	/* Uncompressed size */
	FILE   *fpIn;			/* Input stream */
	FILE   *fpOut;			/* Output stream */
} unpack_descriptor_t;

/*
 * Release resource allocated to an unpack descriptor.
 *
 * Caller is responsible to make sure that all of these pointers are
 * initialized (in our case, they all point to valid memory block).
 * We don't zero out pointers here because nobody else would ever
 * reference the memory block without scrubbing them.
 */
static void
unpack_descriptor_fini(unpack_descriptor_t *unpackd)
{

	free(unpackd->symbolsin);
	free(unpackd->inodesin);
	free(unpackd->symbol);
	free(unpackd->tree);

	fclose(unpackd->fpIn);
	fclose(unpackd->fpOut);
}

/*
 * Recursively fill the internal node count table
 */
static void
unpackd_fill_inodesin(const unpack_descriptor_t *unpackd, int level)
{

	/*
	 * The internal nodes would be 1/2 of total internal nodes and
	 * leaf nodes in the next level.  For the last level there
	 * would be no internal node by definition.
	 */
	if (level < unpackd->treelevels) {
		unpackd_fill_inodesin(unpackd, level + 1);
		unpackd->inodesin[level] = (unpackd->inodesin[level + 1] +
		    unpackd->symbolsin[level + 1]) / 2;
	} else
		unpackd->inodesin[level] = 0;
}

/*
 * Update counter for accepted bytes
 */
static void
accepted_bytes(off_t *bytes_in, off_t newbytes)
{

	if (bytes_in != NULL)
		(*bytes_in) += newbytes;
}

/*
 * Read file header and construct the tree.  Also, prepare the buffered I/O
 * for decode routine.
 *
 * Return value is uncompressed size.
 */
static void
unpack_parse_header(int in, int out, char *pre, size_t prelen, off_t *bytes_in,
    unpack_descriptor_t *unpackd)
{
	unsigned char hdr[PACK_HEADER_LENGTH];	/* buffer for header */
	ssize_t bytesread;		/* Bytes read from the file */
	int i, j, thisbyte;

	if (prelen > sizeof hdr)
		maybe_err("prelen too long");

	/* Prepend the header buffer if we already read some data */
	if (prelen != 0)
		memcpy(hdr, pre, prelen);

	/* Read in and fill the rest bytes of header */
	bytesread = read(in, hdr + prelen, PACK_HEADER_LENGTH - prelen);
	if (bytesread < 0)
		maybe_err("Error reading pack header");
	infile_newdata(bytesread);

	accepted_bytes(bytes_in, PACK_HEADER_LENGTH);

	/* Obtain uncompressed length (bytes 2,3,4,5) */
	unpackd->uncompressed_size = 0;
	for (i = 2; i <= 5; i++) {
		unpackd->uncompressed_size <<= 8;
		unpackd->uncompressed_size |= hdr[i];
	}

	/* Get the levels of the tree */
	unpackd->treelevels = hdr[6];
	if (unpackd->treelevels > HTREE_MAXLEVEL || unpackd->treelevels < 1)
		maybe_errx("Huffman tree has insane levels");

	/* Let libc take care for buffering from now on */
	if ((unpackd->fpIn = fdopen(in, "r")) == NULL)
		maybe_err("Can not fdopen() input stream");
	if ((unpackd->fpOut = fdopen(out, "w")) == NULL)
		maybe_err("Can not fdopen() output stream");

	/* Allocate for the tables of bounds and the tree itself */
	unpackd->inodesin =
	    calloc(unpackd->treelevels, sizeof(*(unpackd->inodesin)));
	unpackd->symbolsin =
	    calloc(unpackd->treelevels, sizeof(*(unpackd->symbolsin)));
	unpackd->tree =
	    calloc(unpackd->treelevels, (sizeof(*(unpackd->tree))));
	if (unpackd->inodesin == NULL || unpackd->symbolsin == NULL ||
	    unpackd->tree == NULL)
		maybe_err("calloc");

	/* We count from 0 so adjust to match array upper bound */
	unpackd->treelevels--;

	/* Read the levels symbol count table and calculate total */
	unpackd->symbol_size = 1;	/* EOB */
	for (i = 0; i <= unpackd->treelevels; i++) {
		if ((thisbyte = fgetc(unpackd->fpIn)) == EOF)
			maybe_err("File appears to be truncated");
		unpackd->symbolsin[i] = (unsigned char)thisbyte;
		unpackd->symbol_size += unpackd->symbolsin[i];
	}
	accepted_bytes(bytes_in, unpackd->treelevels);
	if (unpackd->symbol_size > 256)
		maybe_errx("Bad symbol table");
	infile_newdata(unpackd->treelevels);

	/* Allocate for the symbol table, point symbol_eob at the beginning */
	unpackd->symbol_eob = unpackd->symbol = calloc(1, unpackd->symbol_size);
	if (unpackd->symbol == NULL)
		maybe_err("calloc");

	/*
	 * Read in the symbol table, which contain [2, 256] symbols.
	 * In order to fit the count in one byte, pack(1) would offset
	 * it by reducing 2 from the actual number from the last level.
	 *
	 * We adjust the last level's symbol count by 1 here, because
	 * the EOB symbol is not being transmitted explicitly.  Another
	 * adjustment would be done later afterward.
	 */
	unpackd->symbolsin[unpackd->treelevels]++;
	for (i = 0; i <= unpackd->treelevels; i++) {
		unpackd->tree[i] = unpackd->symbol_eob;
		for (j = 0; j < unpackd->symbolsin[i]; j++) {
			if ((thisbyte = fgetc(unpackd->fpIn)) == EOF)
				maybe_errx("Symbol table truncated");
			*unpackd->symbol_eob++ = (char)thisbyte;
		}
		infile_newdata(unpackd->symbolsin[i]);
		accepted_bytes(bytes_in, unpackd->symbolsin[i]);
	}

	/* Now, take account for the EOB symbol as well */
	unpackd->symbolsin[unpackd->treelevels]++;

	/*
	 * The symbolsin table has been constructed now.
	 * Calculate the internal nodes count table based on it.
	 */
	unpackd_fill_inodesin(unpackd, 0);
}

/*
 * Decode huffman stream, based on the huffman tree.
 */
static void
unpack_decode(const unpack_descriptor_t *unpackd, off_t *bytes_in)
{
	int thislevel, thiscode, thisbyte, inlevelindex;
	int i;
	off_t bytes_out = 0;
	const char *thissymbol;	/* The symbol pointer decoded from stream */

	/*
	 * Decode huffman.  Fetch every bytes from the file, get it
	 * into 'thiscode' bit-by-bit, then output the symbol we got
	 * when one has been found.
	 *
	 * Assumption: sizeof(int) > ((max tree levels + 1) / 8).
	 * bad things could happen if not.
	 */
	thislevel = 0;
	thiscode = thisbyte = 0;

	while ((thisbyte = fgetc(unpackd->fpIn)) != EOF) {
		accepted_bytes(bytes_in, 1);
		infile_newdata(1);
		check_siginfo();

		/*
		 * Split one bit from thisbyte, from highest to lowest,
		 * feed the bit into thiscode, until we got a symbol from
		 * the tree.
		 */
		for (i = 7; i >= 0; i--) {
			thiscode = (thiscode << 1) | ((thisbyte >> i) & 1);

			/* Did we got a symbol? (referencing leaf node) */
			if (thiscode >= unpackd->inodesin[thislevel]) {
				inlevelindex =
				    thiscode - unpackd->inodesin[thislevel];
				if (inlevelindex > unpackd->symbolsin[thislevel])
					maybe_errx("File corrupt");

				thissymbol =
				    &(unpackd->tree[thislevel][inlevelindex]);
				if ((thissymbol == unpackd->symbol_eob) &&
				    (bytes_out == unpackd->uncompressed_size))
					goto finished;

				fputc((*thissymbol), unpackd->fpOut);
				bytes_out++;

				/* Prepare for next input */
				thislevel = 0; thiscode = 0;
			} else {
				thislevel++;
				if (thislevel > unpackd->treelevels)
					maybe_errx("File corrupt");
			}
		}
	}

finished:
	if (bytes_out != unpackd->uncompressed_size)
		maybe_errx("Premature EOF");
}

/* Handler for pack(1)'ed file */
static off_t
unpack(int in, int out, char *pre, size_t prelen, off_t *bytes_in)
{
	unpack_descriptor_t unpackd;

	in = dup(in);
	if (in == -1)
		maybe_err("dup");
	out = dup(out);
	if (out == -1)
		maybe_err("dup");

	unpack_parse_header(in, out, pre, prelen, bytes_in, &unpackd);
	unpack_decode(&unpackd, bytes_in);
	unpack_descriptor_fini(&unpackd);

	/* If we reached here, the unpack was successful */
	return (unpackd.uncompressed_size);
}
