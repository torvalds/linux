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
#include "ima_template_lib.h"

enum data_formats { DATA_FMT_DIGEST = 0, DATA_FMT_EVENT_NAME, DATA_FMT_STRING };
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
	switch (datafmt) {
	case DATA_FMT_DIGEST:
		ima_print_digest(m, field_data->data, field_data->len);
		break;
	case DATA_FMT_STRING:
		seq_printf(m, "%s", field_data->data);
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

void ima_show_template_string(struct seq_file *m, enum ima_show_type show,
			      struct ima_field_data *field_data)
{
	ima_show_template_field_data(m, show, DATA_FMT_STRING, field_data);
}

/*
 * This function writes the digest of an event.
 */
int ima_eventdigest_init(struct integrity_iint_cache *iint, struct file *file,
			 const unsigned char *filename,
			 struct ima_field_data *field_data)
{
	struct {
		struct ima_digest_data hdr;
		char digest[IMA_MAX_DIGEST_SIZE];
	} hash;
	u8 *cur_digest = hash.hdr.digest;
	u32 cur_digestsize = IMA_DIGEST_SIZE;
	struct inode *inode;
	int result;

	memset(&hash, 0, sizeof(hash));

	if (!iint)		/* recording a violation. */
		goto out;

	if (iint->ima_hash->algo == ima_hash_algo) {
		cur_digest = iint->ima_hash->digest;
		cur_digestsize = iint->ima_hash->length;
		goto out;
	}

	if (!file)		/* missing info to re-calculate the digest */
		return -EINVAL;

	inode = file_inode(file);
	hash.hdr.algo = ima_hash_algo;
	result = ima_calc_file_hash(file, &hash.hdr);
	if (result) {
		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode,
				    filename, "collect_data",
				    "failed", result, 0);
		return result;
	}
out:
	return ima_write_template_field_data(cur_digest, cur_digestsize,
					     DATA_FMT_DIGEST, field_data);
}

/*
 * This function writes the name of an event.
 */
int ima_eventname_init(struct integrity_iint_cache *iint, struct file *file,
		       const unsigned char *filename,
		       struct ima_field_data *field_data)
{
	const char *cur_filename = NULL;
	u32 cur_filename_len = 0;

	BUG_ON(filename == NULL && file == NULL);

	if (filename) {
		cur_filename = filename;
		cur_filename_len = strlen(filename);

		if (cur_filename_len <= IMA_EVENT_NAME_LEN_MAX)
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
					     DATA_FMT_EVENT_NAME, field_data);
}
