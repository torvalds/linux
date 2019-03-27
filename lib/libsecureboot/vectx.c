/*-
 * Copyright (c) 2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _STANDALONE
/* Avoid unwanted userlandish components */
#define _KERNEL
#include <sys/errno.h>
#undef _KERNEL
#endif

#include "libsecureboot-priv.h"

/**
 * @file vectx.c
 * @brief api to verify file while reading
 *
 * This API allows the hash of a file to be computed as it is read.
 * Key to this is seeking by reading.
 *
 * On close an indication of the verification result is returned.
 */

struct vectx {
	br_hash_compat_context vec_ctx;	/* hash ctx */
	const br_hash_class *vec_md;	/* hash method */
	const char	*vec_path;	/* path we are verifying */
	const char	*vec_want;	/* hash value we want */
	off_t		vec_off;	/* current offset */
	size_t		vec_size;	/* size of path */
	size_t		vec_hashsz;	/* size of hash */
	int		vec_fd;		/* file descriptor */
	int		vec_status;	/* verification status */
};

/**
 * @brief
 * verify an open file as we read it
 *
 * If the file has no fingerprint to match, we will still return a
 * verification context containing little more than the file
 * descriptor, and an error code in @c error.
 *
 * @param[in] fd
 *	open descriptor
 *
 * @param[in] path
 *	pathname to open
 *
 * @param[in] off
 *	current offset
 *
 * @param[in] stp
 *	pointer to struct stat
 *
 * @param[out] error
 *	@li 0 all is good
 *	@li ENOMEM out of memory
 *	@li VE_FINGERPRINT_NONE	no entry found
 *	@li VE_FINGERPRINT_UNKNOWN no fingerprint in entry
 *
 * @return ctx or NULL on error.
 *	NULL is only returned for non-files or out-of-memory.
 */
struct vectx *
vectx_open(int fd, const char *path, off_t off, struct stat *stp, int *error)
{
	struct vectx *ctx;
	struct stat st;
	size_t hashsz;
	char *cp;

	if (!stp) {
		if (fstat(fd, &st) == 0)
			stp = &st;
	}

	/* we *should* only get called for files */
	if (stp && !S_ISREG(stp->st_mode)) {
		*error = 0;
		return (NULL);
	}

	ctx = malloc(sizeof(struct vectx));
	if (!ctx)
		goto enomem;
	ctx->vec_fd = fd;
	ctx->vec_path = path;
	ctx->vec_size = stp->st_size;
	ctx->vec_off = 0;
	ctx->vec_want = NULL;
	ctx->vec_status = 0;
	hashsz = 0;

	cp = fingerprint_info_lookup(fd, path);
	if (!cp) {
		ctx->vec_status = VE_FINGERPRINT_NONE;
		ve_error_set("%s: no entry", path);
	} else {
		if (strncmp(cp, "sha256=", 7) == 0) {
			ctx->vec_md = &br_sha256_vtable;
			hashsz = br_sha256_SIZE;
			cp += 7;
#ifdef VE_SHA1_SUPPORT
		} else if (strncmp(cp, "sha1=", 5) == 0) {
			ctx->vec_md = &br_sha1_vtable;
			hashsz = br_sha1_SIZE;
			cp += 5;
#endif
#ifdef VE_SHA384_SUPPORT
		} else if (strncmp(cp, "sha384=", 7) == 0) {
		    ctx->vec_md = &br_sha384_vtable;
		    hashsz = br_sha384_SIZE;
		    cp += 7;
#endif
#ifdef VE_SHA512_SUPPORT
		} else if (strncmp(cp, "sha512=", 7) == 0) {
		    ctx->vec_md = &br_sha512_vtable;
		    hashsz = br_sha512_SIZE;
		    cp += 7;
#endif
		} else {
			ctx->vec_status = VE_FINGERPRINT_UNKNOWN;
			ve_error_set("%s: no supported fingerprint", path);
		}
	}
	*error = ctx->vec_status;
	ctx->vec_hashsz = hashsz;
	ctx->vec_want = cp;
	ctx->vec_md->init(&ctx->vec_ctx.vtable);

	if (hashsz > 0 && off > 0) {
		lseek(fd, 0, SEEK_SET);
		vectx_lseek(ctx, off, SEEK_SET);
	}
	return (ctx);

enomem:					/* unlikely */
	*error = ENOMEM;
	free(ctx);
	return (NULL);
}

/**
 * @brief
 * read bytes from file and update hash
 *
 * It is critical that all file I/O comes through here.
 * We keep track of current offset.
 *
 * @param[in] pctx
 *	pointer to ctx
 *
 * @param[in] buf
 *
 * @param[in] nbytes
 *
 * @return bytes read or error.
 */
ssize_t
vectx_read(struct vectx *ctx, void *buf, size_t nbytes)
{
	unsigned char *bp = buf;
	int n;
	size_t off;

	if (ctx->vec_hashsz == 0)	/* nothing to do */
		return (read(ctx->vec_fd, buf, nbytes));

	off = 0;
	do {
		n = read(ctx->vec_fd, &bp[off], nbytes - off);
		if (n < 0)
			return (n);
		if (n > 0) {
			ctx->vec_md->update(&ctx->vec_ctx.vtable, &bp[off], n);
			off += n;
			ctx->vec_off += n;
		}
	} while (n > 0 && off < nbytes);
	return (off);
}

/**
 * @brief
 * vectx equivalent of lseek
 *
 * We do not actually, seek, but call vectx_read
 * to reach the desired offset.
 *
 * We do not support seeking backwards.
 *
 * @param[in] pctx
 *	pointer to ctx
 *
 * @param[in] off
 *	desired offset
 *
 * @param[in] whence
 *
 * @return offset or error.
 */
off_t
vectx_lseek(struct vectx *ctx, off_t off, int whence)
{
	unsigned char buf[PAGE_SIZE];
	size_t delta;
	ssize_t n;

	if (ctx->vec_hashsz == 0)	/* nothing to do */
		return (lseek(ctx->vec_fd, off, whence));

	/*
	 * Try to convert whence to SEEK_SET
	 * but we cannot support seeking backwards!
	 * Nor beyond end of file.
	 */
	if (whence == SEEK_END && off <= 0) {
		whence = SEEK_SET;
		off += ctx->vec_size;
	} else if (whence == SEEK_CUR && off >= 0) {
		whence = SEEK_SET;
		off += ctx->vec_off;
	}
	if (whence != SEEK_SET || off < ctx->vec_off ||
	    (size_t)off > ctx->vec_size) {
		printf("ERROR: %s: unsupported operation\n",  __func__);
		return (-1);
	}
	n = 0;
	do {
		delta = off - ctx->vec_off;
		if (delta > 0) {
			delta = MIN(PAGE_SIZE, delta);
			n = vectx_read(ctx, buf, delta);
			if (n < 0)
				return (n);
		}
	} while (ctx->vec_off < off && n > 0);
	return (ctx->vec_off);
}

/**
 * @brief
 * check that hashes match and cleanup
 *
 * We have finished reading file, compare the hash with what
 * we wanted.
 *
 * @param[in] pctx
 *	pointer to ctx
 *
 * @return 0 or an error.
 */
int
vectx_close(struct vectx *ctx)
{
	int rc;

	if (ctx->vec_hashsz == 0) {
		rc = ctx->vec_status;
	} else {
		rc = ve_check_hash(&ctx->vec_ctx, ctx->vec_md,
		    ctx->vec_path, ctx->vec_want, ctx->vec_hashsz);
	}
	free(ctx);
	return ((rc < 0) ? rc : 0);
}
