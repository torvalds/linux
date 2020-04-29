// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Politecnico di Torino, Italy
 *                    TORSEC group -- http://security.polito.it
 *
 * Author: Roberto Sassu <roberto.sassu@polito.it>
 *
 * File: ima_template_lib.c
 *      Library of supported template fields.
 */

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
	DATA_FMT_STRING,
	DATA_FMT_HEX
};

static int ima_write_template_field_data(const void *data, const u32 datalen,
					 enum data_formats datafmt,
					 struct ima_field_data *field_data)
{
	u8 *buf, *buf_ptr;
	u32 buflen = datalen;

	if (datafmt == DATA_FMT_STRING)
		buflen = datalen + 1;

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
	if (datafmt == DATA_FMT_STRING) {
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
	u8 *buf_ptr = field_data->data;
	u32 buflen = field_data->len;

	switch (datafmt) {
	case DATA_FMT_DIGEST_WITH_ALGO:
		buf_ptr = strnchr(field_data->data, buflen, ':');
		if (buf_ptr != field_data->data)
			seq_printf(m, "%s", field_data->data);

		/* skip ':' and '\0' */
		buf_ptr += 2;
		buflen -= buf_ptr - field_data->data;
		/* fall through */
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
	u32 len = (show == IMA_SHOW_BINARY_OLD_STRING_FMT) ?
	    strlen(field_data->data) : field_data->len;

	if (show != IMA_SHOW_BINARY_NO_FIELD_LEN) {
		u32 field_len = !ima_canonical_fmt ? len : cpu_to_le32(len);

		ima_putc(m, &field_len, sizeof(field_len));
	}

	if (!len)
		return;

	ima_putc(m, field_data->data, len);
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
	case IMA_SHOW_BINARY_OLD_STRING_FMT:
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

void ima_show_template_buf(struct seq_file *m, enum ima_show_type show,
			   struct ima_field_data *field_data)
{
	ima_show_template_field_data(m, show, DATA_FMT_HEX, field_data);
}

/**
 * ima_parse_buf() - Parses lengths and data from an input buffer
 * @bufstartp:       Buffer start address.
 * @bufendp:         Buffer end address.
 * @bufcurp:         Pointer to remaining (non-parsed) data.
 * @maxfields:       Length of fields array.
 * @fields:          Array containing lengths and pointers of parsed data.
 * @curfields:       Number of array items containing parsed data.
 * @len_mask:        Bitmap (if bit is set, data length should not be parsed).
 * @enforce_mask:    Check if curfields == maxfields and/or bufcurp == bufendp.
 * @bufname:         String identifier of the input buffer.
 *
 * Return: 0 on success, -EINVAL on error.
 */
int ima_parse_buf(void *bufstartp, void *bufendp, void **bufcurp,
		  int maxfields, struct ima_field_data *fields, int *curfields,
		  unsigned long *len_mask, int enforce_mask, char *bufname)
{
	void *bufp = bufstartp;
	int i;

	for (i = 0; i < maxfields; i++) {
		if (len_mask == NULL || !test_bit(i, len_mask)) {
			if (bufp > (bufendp - sizeof(u32)))
				break;

			fields[i].len = *(u32 *)bufp;
			if (ima_canonical_fmt)
				fields[i].len = le32_to_cpu(fields[i].len);

			bufp += sizeof(u32);
		}

		if (bufp > (bufendp - fields[i].len))
			break;

		fields[i].data = bufp;
		bufp += fields[i].len;
	}

	if ((enforce_mask & ENFORCE_FIELDS) && i != maxfields) {
		pr_err("%s: nr of fields mismatch: expected: %d, current: %d\n",
		       bufname, maxfields, i);
		return -EINVAL;
	}

	if ((enforce_mask & ENFORCE_BUFEND) && bufp != bufendp) {
		pr_err("%s: buf end mismatch: expected: %p, current: %p\n",
		       bufname, bufendp, bufp);
		return -EINVAL;
	}

	if (curfields)
		*curfields = i;

	if (bufcurp)
		*bufcurp = bufp;

	return 0;
}

static int ima_eventdigest_init_common(const u8 *digest, u32 digestsize,
				       u8 hash_algo,
				       struct ima_field_data *field_data)
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

	if (hash_algo < HASH_ALGO__LAST) {
		fmt = DATA_FMT_DIGEST_WITH_ALGO;
		offset += snprintf(buffer, CRYPTO_MAX_ALG_NAME + 1, "%s",
				   hash_algo_name[hash_algo]);
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
int ima_eventdigest_init(struct ima_event_data *event_data,
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

	if (event_data->violation)	/* recording a violation. */
		goto out;

	if (ima_template_hash_algo_allowed(event_data->iint->ima_hash->algo)) {
		cur_digest = event_data->iint->ima_hash->digest;
		cur_digestsize = event_data->iint->ima_hash->length;
		goto out;
	}

	if (!event_data->file)	/* missing info to re-calculate the digest */
		return -EINVAL;

	inode = file_inode(event_data->file);
	hash.hdr.algo = ima_template_hash_algo_allowed(ima_hash_algo) ?
	    ima_hash_algo : HASH_ALGO_SHA1;
	result = ima_calc_file_hash(event_data->file, &hash.hdr);
	if (result) {
		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode,
				    event_data->filename, "collect_data",
				    "failed", result, 0);
		return result;
	}
	cur_digest = hash.hdr.digest;
	cur_digestsize = hash.hdr.length;
out:
	return ima_eventdigest_init_common(cur_digest, cur_digestsize,
					   HASH_ALGO__LAST, field_data);
}

/*
 * This function writes the digest of an event (without size limit).
 */
int ima_eventdigest_ng_init(struct ima_event_data *event_data,
			    struct ima_field_data *field_data)
{
	u8 *cur_digest = NULL, hash_algo = HASH_ALGO_SHA1;
	u32 cur_digestsize = 0;

	if (event_data->violation)	/* recording a violation. */
		goto out;

	cur_digest = event_data->iint->ima_hash->digest;
	cur_digestsize = event_data->iint->ima_hash->length;

	hash_algo = event_data->iint->ima_hash->algo;
out:
	return ima_eventdigest_init_common(cur_digest, cur_digestsize,
					   hash_algo, field_data);
}

/*
 * This function writes the digest of the file which is expected to match the
 * digest contained in the file's appended signature.
 */
int ima_eventdigest_modsig_init(struct ima_event_data *event_data,
				struct ima_field_data *field_data)
{
	enum hash_algo hash_algo;
	const u8 *cur_digest;
	u32 cur_digestsize;

	if (!event_data->modsig)
		return 0;

	if (event_data->violation) {
		/* Recording a violation. */
		hash_algo = HASH_ALGO_SHA1;
		cur_digest = NULL;
		cur_digestsize = 0;
	} else {
		int rc;

		rc = ima_get_modsig_digest(event_data->modsig, &hash_algo,
					   &cur_digest, &cur_digestsize);
		if (rc)
			return rc;
		else if (hash_algo == HASH_ALGO__LAST || cur_digestsize == 0)
			/* There was some error collecting the digest. */
			return -EINVAL;
	}

	return ima_eventdigest_init_common(cur_digest, cur_digestsize,
					   hash_algo, field_data);
}

static int ima_eventname_init_common(struct ima_event_data *event_data,
				     struct ima_field_data *field_data,
				     bool size_limit)
{
	const char *cur_filename = NULL;
	u32 cur_filename_len = 0;

	BUG_ON(event_data->filename == NULL && event_data->file == NULL);

	if (event_data->filename) {
		cur_filename = event_data->filename;
		cur_filename_len = strlen(event_data->filename);

		if (!size_limit || cur_filename_len <= IMA_EVENT_NAME_LEN_MAX)
			goto out;
	}

	if (event_data->file) {
		cur_filename = event_data->file->f_path.dentry->d_name.name;
		cur_filename_len = strlen(cur_filename);
	} else
		/*
		 * Truncate filename if the latter is too long and
		 * the file descriptor is not available.
		 */
		cur_filename_len = IMA_EVENT_NAME_LEN_MAX;
out:
	return ima_write_template_field_data(cur_filename, cur_filename_len,
					     DATA_FMT_STRING, field_data);
}

/*
 * This function writes the name of an event (with size limit).
 */
int ima_eventname_init(struct ima_event_data *event_data,
		       struct ima_field_data *field_data)
{
	return ima_eventname_init_common(event_data, field_data, true);
}

/*
 * This function writes the name of an event (without size limit).
 */
int ima_eventname_ng_init(struct ima_event_data *event_data,
			  struct ima_field_data *field_data)
{
	return ima_eventname_init_common(event_data, field_data, false);
}

/*
 *  ima_eventsig_init - include the file signature as part of the template data
 */
int ima_eventsig_init(struct ima_event_data *event_data,
		      struct ima_field_data *field_data)
{
	struct evm_ima_xattr_data *xattr_value = event_data->xattr_value;

	if ((!xattr_value) || (xattr_value->type != EVM_IMA_XATTR_DIGSIG))
		return 0;

	return ima_write_template_field_data(xattr_value, event_data->xattr_len,
					     DATA_FMT_HEX, field_data);
}

/*
 *  ima_eventbuf_init - include the buffer(kexec-cmldine) as part of the
 *  template data.
 */
int ima_eventbuf_init(struct ima_event_data *event_data,
		      struct ima_field_data *field_data)
{
	if ((!event_data->buf) || (event_data->buf_len == 0))
		return 0;

	return ima_write_template_field_data(event_data->buf,
					     event_data->buf_len, DATA_FMT_HEX,
					     field_data);
}

/*
 *  ima_eventmodsig_init - include the appended file signature as part of the
 *  template data
 */
int ima_eventmodsig_init(struct ima_event_data *event_data,
			 struct ima_field_data *field_data)
{
	const void *data;
	u32 data_len;
	int rc;

	if (!event_data->modsig)
		return 0;

	/*
	 * modsig is a runtime structure containing pointers. Get its raw data
	 * instead.
	 */
	rc = ima_get_raw_modsig(event_data->modsig, &data, &data_len);
	if (rc)
		return rc;

	return ima_write_template_field_data(data, data_len, DATA_FMT_HEX,
					     field_data);
}
