#include <netinet/in.h>
#ifdef __sun__
#include <inttypes.h>
#else
#include <stdint.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include "modpost.h"

/*
 * Stolen form Cryptographic API.
 *
 * MD4 Message Digest Algorithm (RFC1320).
 *
 * Implementation derived from Andrew Tridgell and Steve French's
 * CIFS MD4 implementation, and the cryptoapi implementation
 * originally based on the public domain implementation written
 * by Colin Plumb in 1993.
 *
 * Copyright (c) Andrew Tridgell 1997-1998.
 * Modified by Steve French (sfrench@us.ibm.com) 2002
 * Copyright (c) Cryptoapi developers.
 * Copyright (c) 2002 David S. Miller (davem@redhat.com)
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#define MD4_DIGEST_SIZE		16
#define MD4_HMAC_BLOCK_SIZE	64
#define MD4_BLOCK_WORDS		16
#define MD4_HASH_WORDS		4

struct md4_ctx {
	uint32_t hash[MD4_HASH_WORDS];
	uint32_t block[MD4_BLOCK_WORDS];
	uint64_t byte_count;
};

static inline uint32_t lshift(uint32_t x, unsigned int s)
{
	x &= 0xFFFFFFFF;
	return ((x << s) & 0xFFFFFFFF) | (x >> (32 - s));
}

static inline uint32_t F(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) | ((~x) & z);
}

static inline uint32_t G(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) | (x & z) | (y & z);
}

static inline uint32_t H(uint32_t x, uint32_t y, uint32_t z)
{
	return x ^ y ^ z;
}

#define ROUND1(a,b,c,d,k,s) (a = lshift(a + F(b,c,d) + k, s))
#define ROUND2(a,b,c,d,k,s) (a = lshift(a + G(b,c,d) + k + (uint32_t)0x5A827999,s))
#define ROUND3(a,b,c,d,k,s) (a = lshift(a + H(b,c,d) + k + (uint32_t)0x6ED9EBA1,s))

/* XXX: this stuff can be optimized */
static inline void le32_to_cpu_array(uint32_t *buf, unsigned int words)
{
	while (words--) {
		*buf = ntohl(*buf);
		buf++;
	}
}

static inline void cpu_to_le32_array(uint32_t *buf, unsigned int words)
{
	while (words--) {
		*buf = htonl(*buf);
		buf++;
	}
}

static void md4_transform(uint32_t *hash, uint32_t const *in)
{
	uint32_t a, b, c, d;

	a = hash[0];
	b = hash[1];
	c = hash[2];
	d = hash[3];

	ROUND1(a, b, c, d, in[0], 3);
	ROUND1(d, a, b, c, in[1], 7);
	ROUND1(c, d, a, b, in[2], 11);
	ROUND1(b, c, d, a, in[3], 19);
	ROUND1(a, b, c, d, in[4], 3);
	ROUND1(d, a, b, c, in[5], 7);
	ROUND1(c, d, a, b, in[6], 11);
	ROUND1(b, c, d, a, in[7], 19);
	ROUND1(a, b, c, d, in[8], 3);
	ROUND1(d, a, b, c, in[9], 7);
	ROUND1(c, d, a, b, in[10], 11);
	ROUND1(b, c, d, a, in[11], 19);
	ROUND1(a, b, c, d, in[12], 3);
	ROUND1(d, a, b, c, in[13], 7);
	ROUND1(c, d, a, b, in[14], 11);
	ROUND1(b, c, d, a, in[15], 19);

	ROUND2(a, b, c, d,in[ 0], 3);
	ROUND2(d, a, b, c, in[4], 5);
	ROUND2(c, d, a, b, in[8], 9);
	ROUND2(b, c, d, a, in[12], 13);
	ROUND2(a, b, c, d, in[1], 3);
	ROUND2(d, a, b, c, in[5], 5);
	ROUND2(c, d, a, b, in[9], 9);
	ROUND2(b, c, d, a, in[13], 13);
	ROUND2(a, b, c, d, in[2], 3);
	ROUND2(d, a, b, c, in[6], 5);
	ROUND2(c, d, a, b, in[10], 9);
	ROUND2(b, c, d, a, in[14], 13);
	ROUND2(a, b, c, d, in[3], 3);
	ROUND2(d, a, b, c, in[7], 5);
	ROUND2(c, d, a, b, in[11], 9);
	ROUND2(b, c, d, a, in[15], 13);

	ROUND3(a, b, c, d,in[ 0], 3);
	ROUND3(d, a, b, c, in[8], 9);
	ROUND3(c, d, a, b, in[4], 11);
	ROUND3(b, c, d, a, in[12], 15);
	ROUND3(a, b, c, d, in[2], 3);
	ROUND3(d, a, b, c, in[10], 9);
	ROUND3(c, d, a, b, in[6], 11);
	ROUND3(b, c, d, a, in[14], 15);
	ROUND3(a, b, c, d, in[1], 3);
	ROUND3(d, a, b, c, in[9], 9);
	ROUND3(c, d, a, b, in[5], 11);
	ROUND3(b, c, d, a, in[13], 15);
	ROUND3(a, b, c, d, in[3], 3);
	ROUND3(d, a, b, c, in[11], 9);
	ROUND3(c, d, a, b, in[7], 11);
	ROUND3(b, c, d, a, in[15], 15);

	hash[0] += a;
	hash[1] += b;
	hash[2] += c;
	hash[3] += d;
}

static inline void md4_transform_helper(struct md4_ctx *ctx)
{
	le32_to_cpu_array(ctx->block, sizeof(ctx->block) / sizeof(uint32_t));
	md4_transform(ctx->hash, ctx->block);
}

static void md4_init(struct md4_ctx *mctx)
{
	mctx->hash[0] = 0x67452301;
	mctx->hash[1] = 0xefcdab89;
	mctx->hash[2] = 0x98badcfe;
	mctx->hash[3] = 0x10325476;
	mctx->byte_count = 0;
}

static void md4_update(struct md4_ctx *mctx,
		       const unsigned char *data, unsigned int len)
{
	const uint32_t avail = sizeof(mctx->block) - (mctx->byte_count & 0x3f);

	mctx->byte_count += len;

	if (avail > len) {
		memcpy((char *)mctx->block + (sizeof(mctx->block) - avail),
		       data, len);
		return;
	}

	memcpy((char *)mctx->block + (sizeof(mctx->block) - avail),
	       data, avail);

	md4_transform_helper(mctx);
	data += avail;
	len -= avail;

	while (len >= sizeof(mctx->block)) {
		memcpy(mctx->block, data, sizeof(mctx->block));
		md4_transform_helper(mctx);
		data += sizeof(mctx->block);
		len -= sizeof(mctx->block);
	}

	memcpy(mctx->block, data, len);
}

static void md4_final_ascii(struct md4_ctx *mctx, char *out, unsigned int len)
{
	const unsigned int offset = mctx->byte_count & 0x3f;
	char *p = (char *)mctx->block + offset;
	int padding = 56 - (offset + 1);

	*p++ = 0x80;
	if (padding < 0) {
		memset(p, 0x00, padding + sizeof (uint64_t));
		md4_transform_helper(mctx);
		p = (char *)mctx->block;
		padding = 56;
	}

	memset(p, 0, padding);
	mctx->block[14] = mctx->byte_count << 3;
	mctx->block[15] = mctx->byte_count >> 29;
	le32_to_cpu_array(mctx->block, (sizeof(mctx->block) -
			  sizeof(uint64_t)) / sizeof(uint32_t));
	md4_transform(mctx->hash, mctx->block);
	cpu_to_le32_array(mctx->hash, sizeof(mctx->hash) / sizeof(uint32_t));

	snprintf(out, len, "%08X%08X%08X%08X",
		 mctx->hash[0], mctx->hash[1], mctx->hash[2], mctx->hash[3]);
}

static inline void add_char(unsigned char c, struct md4_ctx *md)
{
	md4_update(md, &c, 1);
}

static int parse_string(const char *file, unsigned long len,
			struct md4_ctx *md)
{
	unsigned long i;

	add_char(file[0], md);
	for (i = 1; i < len; i++) {
		add_char(file[i], md);
		if (file[i] == '"' && file[i-1] != '\\')
			break;
	}
	return i;
}

static int parse_comment(const char *file, unsigned long len)
{
	unsigned long i;

	for (i = 2; i < len; i++) {
		if (file[i-1] == '*' && file[i] == '/')
			break;
	}
	return i;
}

/* FIXME: Handle .s files differently (eg. # starts comments) --RR */
static int parse_file(const char *fname, struct md4_ctx *md)
{
	char *file;
	unsigned long i, len;

	file = read_text_file(fname);
	len = strlen(file);

	for (i = 0; i < len; i++) {
		/* Collapse and ignore \ and CR. */
		if (file[i] == '\\' && (i+1 < len) && file[i+1] == '\n') {
			i++;
			continue;
		}

		/* Ignore whitespace */
		if (isspace(file[i]))
			continue;

		/* Handle strings as whole units */
		if (file[i] == '"') {
			i += parse_string(file+i, len - i, md);
			continue;
		}

		/* Comments: ignore */
		if (file[i] == '/' && file[i+1] == '*') {
			i += parse_comment(file+i, len - i);
			continue;
		}

		add_char(file[i], md);
	}
	free(file);
	return 1;
}
/* Check whether the file is a static library or not */
static int is_static_library(const char *objfile)
{
	int len = strlen(objfile);
	if (objfile[len - 2] == '.' && objfile[len - 1] == 'a')
		return 1;
	else
		return 0;
}

/* We have dir/file.o.  Open dir/.file.o.cmd, look for source_ and deps_ line
 * to figure out source files. */
static int parse_source_files(const char *objfile, struct md4_ctx *md)
{
	char *cmd, *file, *line, *dir, *pos;
	const char *base;
	int dirlen, ret = 0, check_files = 0;

	cmd = NOFAIL(malloc(strlen(objfile) + sizeof("..cmd")));

	base = strrchr(objfile, '/');
	if (base) {
		base++;
		dirlen = base - objfile;
		sprintf(cmd, "%.*s.%s.cmd", dirlen, objfile, base);
	} else {
		dirlen = 0;
		sprintf(cmd, ".%s.cmd", objfile);
	}
	dir = NOFAIL(malloc(dirlen + 1));
	strncpy(dir, objfile, dirlen);
	dir[dirlen] = '\0';

	file = read_text_file(cmd);

	pos = file;

	/* Sum all files in the same dir or subdirs. */
	while ((line = get_line(&pos))) {
		char* p = line;

		if (strncmp(line, "source_", sizeof("source_")-1) == 0) {
			p = strrchr(line, ' ');
			if (!p) {
				warn("malformed line: %s\n", line);
				goto out_file;
			}
			p++;
			if (!parse_file(p, md)) {
				warn("could not open %s: %s\n",
				     p, strerror(errno));
				goto out_file;
			}
			continue;
		}
		if (strncmp(line, "deps_", sizeof("deps_")-1) == 0) {
			check_files = 1;
			continue;
		}
		if (!check_files)
			continue;

		/* Continue until line does not end with '\' */
		if ( *(p + strlen(p)-1) != '\\')
			break;
		/* Terminate line at first space, to get rid of final ' \' */
		while (*p) {
			if (isspace(*p)) {
				*p = '\0';
				break;
			}
			p++;
		}

		/* Check if this file is in same dir as objfile */
		if ((strstr(line, dir)+strlen(dir)-1) == strrchr(line, '/')) {
			if (!parse_file(line, md)) {
				warn("could not open %s: %s\n",
				     line, strerror(errno));
				goto out_file;
			}

		}

	}

	/* Everyone parsed OK */
	ret = 1;
out_file:
	free(file);
	free(dir);
	free(cmd);
	return ret;
}

/* Calc and record src checksum. */
void get_src_version(const char *modname, char sum[], unsigned sumlen)
{
	char *buf, *pos, *firstline;
	struct md4_ctx md;
	char *fname;
	char filelist[PATH_MAX + 1];
	int postfix_len = 1;

	if (strends(modname, ".lto.o"))
		postfix_len = 5;

	/* objects for a module are listed in the first line of *.mod file. */
	snprintf(filelist, sizeof(filelist), "%.*smod",
		 (int)strlen(modname) - postfix_len, modname);

	buf = read_text_file(filelist);

	pos = buf;
	firstline = get_line(&pos);
	if (!firstline) {
		warn("bad ending versions file for %s\n", modname);
		goto free;
	}

	md4_init(&md);
	while ((fname = strsep(&firstline, " "))) {
		if (!*fname)
			continue;
		if (!(is_static_library(fname)) &&
				!parse_source_files(fname, &md))
			goto free;
	}

	md4_final_ascii(&md, sum, sumlen);
free:
	free(buf);
}
