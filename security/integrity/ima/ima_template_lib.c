/*
 * Copyright (C) 2013 Politecnico di Torino, Italy
 *                    TORSEC group -- http://security.polito.it
 *
 * Author: Roberto Sassu <roberto.sassu@polito.it>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_template_lib.c
 *      Library of supported template fields.
 */
#include <crypto/hash_info.h>

#include "ima_template_lib.h"

static bool ima_template_hash_algo_allowed(u8 algo)
{
	if (algo == HASH_ALGO_SHA1 || algo == HASH_ALGO_MD5)
		return true;

	return false;
}

enum data_formats {
	DATA_FMT_DIGEST = 0,
	DATA_FMT_DIGEST_WITH_ALGO,
	DATA_FMT_EVENT_NAME,
	DATA_FMT_STRING,
	DATA_FMT_HEX
};

static int ima_write_template_field_data(const void *data, const u32 datalen,
					 enum data_formats datafmt,
					 struct ima_field_data *field_data)
{
	u8 *buf, *buf_ptr;
	u32 buflen;

	switch (datafmt) {
	case DATA_FMT_EVENT_NAME:
		buflen = IMA_EVENT_NAME_LEN_MAX + 1;
		break;
	case DATA_FMT_STRING:
		buflen = datalen + 1;
		break;
	default:
		buflen = datalen;
	}

	buf = kzalloc(buflen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, data, datalen);

	/*
	 * Replace all space characters with underscore for event names and
	 * strings. This avoid that, during the parsing of a measurements list,
	 * filenames with spaces or that end with the suffix ' (deleted)' are
	 * split into multiple template fields (the space is the delimitator
	 * character for measurements lists in ASCII format).
	 */
	if (datafmt == DATA_FMT_EVENT_NAME || datafmt == DATA_FMT_STRING) {
		for (buf_ptr = buf; buf_ptr - buf < datalen; buf_ptr++)
			if (*buf_ptr == ' ')
				*buf_ptr = '_';
	}

	field_data->data = buf;
	field_data->len = buflen;
	return 0;
}

static void ima_show_template_data_ascii(struct seq_file *m,
					 enum ima_show_type show,
					 enum data_formats datafmt,
					 struct ima_field_data *field_data)
{
	u8 *buf_ptr = field_data->data, buflen = field_data->len;

	switch (datafmt) {
	case DATA_FMT_DIGEST_WITH_ALGO:
		buf_ptr = strnchr(field_data->data, buflen, ':');
		if (buf_ptr != field_data->data)
			seq_printf(m, "%s", field_data->data);

		/* skip ':' and '\0' */
		buf_ptr += 2;
		buflen -= buf_ptr - field_data->data;
	case DATA_FMT_DIGEST:
	case DATA_FMT_HEX:
		if (!buflen)
			break;
		ima_print_digest(m, buf_ptr, buflen);
		break;
	case DATA_FMT_STRING:
		seq_printf(m, "%s", buf_ptr);
		break;
	default:
		break;
	}
}

static void ima_show_template_data_binary(struct seq_file *m,
					  enum ima_show_type show,
					  enum data_formats datafmt,
					  struct ima_field_data *field_data)
{
	if (show != IMA_SHOW_BINARY_NO_FIELD_LEN)
		ima_putc(m, &field_data->len, sizeof(u32));

	if (!field_data->len)
		return;

	ima_putc(m, field_data->data, field_data->len);
}

static void ima_show_template_field_data(struct seq_file *m,
					 enum ima_show_type show,
					 enum data_formats datafmt,
					 struct ima_field_data *field_data)
{
	switch (show) {
	case IMA_SHOW_ASCII:
		ima_show_template_data_ascii(m, show, datafmt, field_data);
		break;
	case IMA_SHOW_BINARY:
	case IMA_SHOW_BINARY_NO_FIELD_LEN:
		ima_show_template_data_binary(m, show, datafmt, field_data);
		break;
	default:
		break;
	}
}

void ima_show_template_digest(struct seq_file *m, enum ima_show_type show,
			      struct ima_field_data *field_data)
{
	ima_show_template_field_data(m, show, DATA_FMT_DIGEST, field_data);
}

void ima_show_template_digest_ng(struct seq_file *m, enum ima_show_type show,
				 struct ima_field_data *field_data)
{
	ima_show_template_field_data(m, show, DATA_FMT_DIGEST_WITH_ALGO,
				     field_data);
}

void ima_show_template_string(struct seq_file *m, enum ima_show_type show,
			      struct ima_field_data *field_data)
{
	ima_show_template_field_data(m, show, DATA_FMT_STRING, field_data);
}

void ima_show_template_sig(struct seq_file *m, enum ima_show_type show,
			   struct ima_field_data *field_data)
{
	ima_show_template_field_data(m, show, DATA_FMT_HEX, field_data);
}

static int ima_eventdigest_init_common(u8 *digest, u32 digestsize, u8 hash_algo,
				       struct ima_field_data *field_data,
				       bool size_limit)
{
	/*
	 * digest formats:
	 *  - DATA_FMT_DIGEST: digest
	 *  - DATA_FMT_DIGEST_WITH_ALGO: [<hash algo>] + ':' + '\0' + digest,
	 *    where <hash algo> is provided if the hash algoritm is not
	 *    SHA1 or MD5
	 */
	u8 buffer[CRYPTO_MAX_ALG_NAME + 2 + IMA_MAX_DIGEST_SIZE] = { 0 };
	enum data_formats fmt = DATA_FMT_DIGEST;
	u32 offset = 0;

	if (!size_limit) {
		fmt = DATA_FMT_DIGEST_WITH_ALGO;
		if (hash_algo < HASH_ALGO__LAST)
			offset += snprintf(buffer, CRYPTO_MAX_ALG_NAME + 1,
					   "%s", hash_algo_name[hash_algo]);
		buffer[offset] = ':';
		offset += 2;
	}

	if (digest)
		memcpy(buffer + offset, digest, digestsize);
	else
		/*
		 * If digest is NULL, the event being recorded is a violation.
		 * Make room for the digest by increasing the offset of
		 * IMA_DIGEST_SIZE.
		 */
		offset += IMA_DIGEST_SIZE;

	return ima_write_template_field_data(buffer, offset + digestsize,
					     fmt, field_data);
}

/*
 * This function writes the digest of an event (with size limit).
 */
int ima_eventdigest_init(struct integrity_iint_cache *iint, struct file *file,
			 const unsigned char *filename,
			 struct evm_ima_xattr_data *xattr_value, int xattr_len,
			 struct ima_field_data *field_data)
{
	struct {
		struct ima_digest_data hdr;
		char digest[IMA_MAX_DIGEST_SIZE];
	} hash;
	u8 *cur_digest = NULL;
	u32 cur_digestsize = 0;
	struct inode *inode;
	int result;

	memset(&hash, 0, sizeof(hash));

	if (!iint)		/* recording a violation. */
		goto out;

	if (ima_template_hash_algo_allowed(iint->ima_hash->algo)) {
		cur_digest = iint->ima_hash->digest;
		cur_digestsize = iint->ima_hash->length;
		goto out;
	}

	if (!file)		/* missing info to re-calculate the digest */
		return -EINVAL;

	inode = file_inode(file);
	hash.hdr.algo = ima_template_hash_algo_allowed(ima_hash_algo) ?
	    ima_hash_algo : HASH_ALGO_SHA1;
	result = ima_calc_file_hash(file, &hash.hdr);
	if (result) {
		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode,
				    filename, "collect_data",
				    "failed", result, 0);
		return result;
	}
	cur_digest = hash.hdr.digest;
	cur_digestsize = hash.hdr.length;
out:
	return ima_eventdigest_init_common(cur_digest, cur_digestsize, -1,
					   field_data, true);
}

/*
 * This function writes the digest of an event (without size limit).
 */
int ima_eventdigest_ng_init(struct integrity_iint_cache *iint,
			    struct file *file, const unsigned char *filename,
			    struct evm_ima_xattr_data *xattr_value,
			    int xattr_len, struct ima_field_data *field_data)
{
	u8 *cur_digest = NULL, hash_algo = HASH_ALGO_SHA1;
	u32 cur_digestsize = 0;

	/* If iint is NULL, we are recording a violation. */
	if (!iint)
		goto out;

	cur_digest = iint->ima_hash->digest;
	cur_digestsize = iint->ima_hash->length;

	hash_algo = iint->ima_hash->algo;
out:
	return ima_eventdigest_init_common(cur_digest, cur_digestsize,
					   hash_algo, field_data, false);
}

static int ima_eventname_init_common(struct integrity_iint_cache *iint,
				     struct file *file,
				     const unsigned char *filename,
				     struct ima_field_data *field_data,
				     bool size_limit)
{
	const char *cur_filename = NULL;
	u32 cur_filename_len = 0;
	enum data_formats fmt = size_limit ?
	    DATA_FMT_EVENT_NAME : DATA_FMT_STRING;

	BUG_ON(filename == NULL && file == NULL);

	if (filename) {
		cur_filename = filename;
		cur_filename_len = strlen(filename);

		if (!size_limit || cur_filename_len <= IMA_EVENT_NAME_LEN_MAX)
			goto out;
	}

	if (file) {
		cur_filename = file->f_dentry->d_name.name;
		cur_filename_len = strlen(cur_filename);
	} else
		/*
		 * Truncate filename if the latter is too long and
		 * the file descriptor is not available.
		 */
		cur_filename_len = IMA_EVENT_NAME_LEN_MAX;
out:
	return ima_write_template_field_data(cur_filename, cur_filename_len,
					     fmt, field_data);
}

/*
 * This function writes the name of an event (with size limit).
 */
int ima_eventname_init(struct integrity_iint_cache *iint, struct file *file,
		       const unsigned char *filename,
		       struct evm_ima_xattr_data *xattr_value, int xattr_len,
		       struct ima_field_data *field_data)
{
	return ima_eventname_init_common(iint, file, filename,
					 field_data, true);
}

/*
 * This function writes the name of an event (without size limit).
 */
int ima_eventname_ng_init(struct integrity_iint_cache *iint, struct file *file,
			  const unsigned char *filename,
			  struct evm_ima_xattr_data *xattr_value, int xattr_len,
			  struct ima_field_data *field_data)
{
	return ima_eventname_init_common(iint, file, filename,
					 field_data, false);
}

/*
 *  ima_eventsig_init - include the file signature as part of the template data
 */
int ima_eventsig_init(struct integrity_iint_cache *iint, struct file *file,
		      const unsigned char *filename,
		      struct evm_ima_xattr_data *xattr_value, int xattr_len,
		      struct ima_field_data *field_data)
{
	enum data_formats fmt = DATA_FMT_HEX;
	int rc = 0;

	if ((!xattr_value) || (xattr_value->type != EVM_IMA_XATTR_DIGSIG))
		goto out;

	rc = ima_write_template_field_data(xattr_value, xattr_len, fmt,
					   field_data);
out:
	return rc;
}
