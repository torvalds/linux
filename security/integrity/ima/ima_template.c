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
 * File: ima_template.c
 *      Helpers to manage template descriptors.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/rculist.h>
#include "ima.h"
#include "ima_template_lib.h"

static struct ima_template_desc builtin_templates[] = {
	{.name = IMA_TEMPLATE_IMA_NAME, .fmt = IMA_TEMPLATE_IMA_FMT},
	{.name = "ima-ng", .fmt = "d-ng|n-ng"},
	{.name = "ima-sig", .fmt = "d-ng|n-ng|sig"},
	{.name = "", .fmt = ""},	/* placeholder for a custom format */
};

static LIST_HEAD(defined_templates);
static DEFINE_SPINLOCK(template_list);

static struct ima_template_field supported_fields[] = {
	{.field_id = "d", .field_init = ima_eventdigest_init,
	 .field_show = ima_show_template_digest},
	{.field_id = "n", .field_init = ima_eventname_init,
	 .field_show = ima_show_template_string},
	{.field_id = "d-ng", .field_init = ima_eventdigest_ng_init,
	 .field_show = ima_show_template_digest_ng},
	{.field_id = "n-ng", .field_init = ima_eventname_ng_init,
	 .field_show = ima_show_template_string},
	{.field_id = "sig", .field_init = ima_eventsig_init,
	 .field_show = ima_show_template_sig},
};
#define MAX_TEMPLATE_NAME_LEN 15

static struct ima_template_desc *ima_template;
static struct ima_template_desc *lookup_template_desc(const char *name);
static int template_desc_init_fields(const char *template_fmt,
				     struct ima_template_field ***fields,
				     int *num_fields);

static int __init ima_template_setup(char *str)
{
	struct ima_template_desc *template_desc;
	int template_len = strlen(str);

	if (ima_template)
		return 1;

	ima_init_template_list();

	/*
	 * Verify that a template with the supplied name exists.
	 * If not, use CONFIG_IMA_DEFAULT_TEMPLATE.
	 */
	template_desc = lookup_template_desc(str);
	if (!template_desc) {
		pr_err("template %s not found, using %s\n",
		       str, CONFIG_IMA_DEFAULT_TEMPLATE);
		return 1;
	}

	/*
	 * Verify whether the current hash algorithm is supported
	 * by the 'ima' template.
	 */
	if (template_len == 3 && strcmp(str, IMA_TEMPLATE_IMA_NAME) == 0 &&
	    ima_hash_algo != HASH_ALGO_SHA1 && ima_hash_algo != HASH_ALGO_MD5) {
		pr_err("template does not support hash alg\n");
		return 1;
	}

	ima_template = template_desc;
	return 1;
}
__setup("ima_template=", ima_template_setup);

static int __init ima_template_fmt_setup(char *str)
{
	int num_templates = ARRAY_SIZE(builtin_templates);

	if (ima_template)
		return 1;

	if (template_desc_init_fields(str, NULL, NULL) < 0) {
		pr_err("format string '%s' not valid, using template %s\n",
		       str, CONFIG_IMA_DEFAULT_TEMPLATE);
		return 1;
	}

	builtin_templates[num_templates - 1].fmt = str;
	ima_template = builtin_templates + num_templates - 1;

	return 1;
}
__setup("ima_template_fmt=", ima_template_fmt_setup);

static struct ima_template_desc *lookup_template_desc(const char *name)
{
	struct ima_template_desc *template_desc;
	int found = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(template_desc, &defined_templates, list) {
		if ((strcmp(template_desc->name, name) == 0) ||
		    (strcmp(template_desc->fmt, name) == 0)) {
			found = 1;
			break;
		}
	}
	rcu_read_unlock();
	return found ? template_desc : NULL;
}

static struct ima_template_field *lookup_template_field(const char *field_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_fields); i++)
		if (strncmp(supported_fields[i].field_id, field_id,
			    IMA_TEMPLATE_FIELD_ID_MAX_LEN) == 0)
			return &supported_fields[i];
	return NULL;
}

static int template_fmt_size(const char *template_fmt)
{
	char c;
	int template_fmt_len = strlen(template_fmt);
	int i = 0, j = 0;

	while (i < template_fmt_len) {
		c = template_fmt[i];
		if (c == '|')
			j++;
		i++;
	}

	return j + 1;
}

static int template_desc_init_fields(const char *template_fmt,
				     struct ima_template_field ***fields,
				     int *num_fields)
{
	const char *template_fmt_ptr;
	struct ima_template_field *found_fields[IMA_TEMPLATE_NUM_FIELDS_MAX];
	int template_num_fields;
	int i, len;

	if (num_fields && *num_fields > 0) /* already initialized? */
		return 0;

	template_num_fields = template_fmt_size(template_fmt);

	if (template_num_fields > IMA_TEMPLATE_NUM_FIELDS_MAX) {
		pr_err("format string '%s' contains too many fields\n",
		       template_fmt);
		return -EINVAL;
	}

	for (i = 0, template_fmt_ptr = template_fmt; i < template_num_fields;
	     i++, template_fmt_ptr += len + 1) {
		char tmp_field_id[IMA_TEMPLATE_FIELD_ID_MAX_LEN + 1];

		len = strchrnul(template_fmt_ptr, '|') - template_fmt_ptr;
		if (len == 0 || len > IMA_TEMPLATE_FIELD_ID_MAX_LEN) {
			pr_err("Invalid field with length %d\n", len);
			return -EINVAL;
		}

		memcpy(tmp_field_id, template_fmt_ptr, len);
		tmp_field_id[len] = '\0';
		found_fields[i] = lookup_template_field(tmp_field_id);
		if (!found_fields[i]) {
			pr_err("field '%s' not found\n", tmp_field_id);
			return -ENOENT;
		}
	}

	if (fields && num_fields) {
		*fields = kmalloc_array(i, sizeof(*fields), GFP_KERNEL);
		if (*fields == NULL)
			return -ENOMEM;

		memcpy(*fields, found_fields, i * sizeof(*fields));
		*num_fields = i;
	}

	return 0;
}

void ima_init_template_list(void)
{
	int i;

	if (!list_empty(&defined_templates))
		return;

	spin_lock(&template_list);
	for (i = 0; i < ARRAY_SIZE(builtin_templates); i++) {
		list_add_tail_rcu(&builtin_templates[i].list,
				  &defined_templates);
	}
	spin_unlock(&template_list);
}

struct ima_template_desc *ima_template_desc_current(void)
{
	if (!ima_template) {
		ima_init_template_list();
		ima_template =
		    lookup_template_desc(CONFIG_IMA_DEFAULT_TEMPLATE);
	}
	return ima_template;
}

int __init ima_init_template(void)
{
	struct ima_template_desc *template = ima_template_desc_current();
	int result;

	result = template_desc_init_fields(template->fmt,
					   &(template->fields),
					   &(template->num_fields));
	if (result < 0)
		pr_err("template %s init failed, result: %d\n",
		       (strlen(template->name) ?
		       template->name : template->fmt), result);

	return result;
}

static struct ima_template_desc *restore_template_fmt(char *template_name)
{
	struct ima_template_desc *template_desc = NULL;
	int ret;

	ret = template_desc_init_fields(template_name, NULL, NULL);
	if (ret < 0) {
		pr_err("attempting to initialize the template \"%s\" failed\n",
			template_name);
		goto out;
	}

	template_desc = kzalloc(sizeof(*template_desc), GFP_KERNEL);
	if (!template_desc)
		goto out;

	template_desc->name = "";
	template_desc->fmt = kstrdup(template_name, GFP_KERNEL);
	if (!template_desc->fmt)
		goto out;

	spin_lock(&template_list);
	list_add_tail_rcu(&template_desc->list, &defined_templates);
	spin_unlock(&template_list);
out:
	return template_desc;
}

static int ima_restore_template_data(struct ima_template_desc *template_desc,
				     void *template_data,
				     int template_data_size,
				     struct ima_template_entry **entry)
{
	struct binary_field_data {
		u32 len;
		u8 data[0];
	} __packed;

	struct binary_field_data *field_data;
	int offset = 0;
	int ret = 0;
	int i;

	*entry = kzalloc(sizeof(**entry) +
		    template_desc->num_fields * sizeof(struct ima_field_data),
		    GFP_NOFS);
	if (!*entry)
		return -ENOMEM;

	(*entry)->template_desc = template_desc;
	for (i = 0; i < template_desc->num_fields; i++) {
		field_data = template_data + offset;

		/* Each field of the template data is prefixed with a length. */
		if (offset > (template_data_size - sizeof(*field_data))) {
			pr_err("Restoring the template field failed\n");
			ret = -EINVAL;
			break;
		}
		offset += sizeof(*field_data);

		if (ima_canonical_fmt)
			field_data->len = le32_to_cpu(field_data->len);

		if (offset > (template_data_size - field_data->len)) {
			pr_err("Restoring the template field data failed\n");
			ret = -EINVAL;
			break;
		}
		offset += field_data->len;

		(*entry)->template_data[i].len = field_data->len;
		(*entry)->template_data_len += sizeof(field_data->len);

		(*entry)->template_data[i].data =
			kzalloc(field_data->len + 1, GFP_KERNEL);
		if (!(*entry)->template_data[i].data) {
			ret = -ENOMEM;
			break;
		}
		memcpy((*entry)->template_data[i].data, field_data->data,
			field_data->len);
		(*entry)->template_data_len += field_data->len;
	}

	if (ret < 0) {
		ima_free_template_entry(*entry);
		*entry = NULL;
	}

	return ret;
}

/* Restore the serialized binary measurement list without extending PCRs. */
int ima_restore_measurement_list(loff_t size, void *buf)
{
	struct binary_hdr_v1 {
		u32 pcr;
		u8 digest[TPM_DIGEST_SIZE];
		u32 template_name_len;
		char template_name[0];
	} __packed;
	char template_name[MAX_TEMPLATE_NAME_LEN];

	struct binary_data_v1 {
		u32 template_data_size;
		char template_data[0];
	} __packed;

	struct ima_kexec_hdr *khdr = buf;
	struct binary_hdr_v1 *hdr_v1;
	struct binary_data_v1 *data_v1;

	void *bufp = buf + sizeof(*khdr);
	void *bufendp;
	struct ima_template_entry *entry;
	struct ima_template_desc *template_desc;
	unsigned long count = 0;
	int ret = 0;

	if (!buf || size < sizeof(*khdr))
		return 0;

	if (ima_canonical_fmt) {
		khdr->version = le16_to_cpu(khdr->version);
		khdr->count = le64_to_cpu(khdr->count);
		khdr->buffer_size = le64_to_cpu(khdr->buffer_size);
	}

	if (khdr->version != 1) {
		pr_err("attempting to restore a incompatible measurement list");
		return -EINVAL;
	}

	if (khdr->count > ULONG_MAX - 1) {
		pr_err("attempting to restore too many measurements");
		return -EINVAL;
	}

	/*
	 * ima kexec buffer prefix: version, buffer size, count
	 * v1 format: pcr, digest, template-name-len, template-name,
	 *	      template-data-size, template-data
	 */
	bufendp = buf + khdr->buffer_size;
	while ((bufp < bufendp) && (count++ < khdr->count)) {
		hdr_v1 = bufp;
		if (bufp > (bufendp - sizeof(*hdr_v1))) {
			pr_err("attempting to restore partial measurement\n");
			ret = -EINVAL;
			break;
		}
		bufp += sizeof(*hdr_v1);

		if (ima_canonical_fmt)
			hdr_v1->template_name_len =
			    le32_to_cpu(hdr_v1->template_name_len);

		if ((hdr_v1->template_name_len >= MAX_TEMPLATE_NAME_LEN) ||
		    (bufp > (bufendp - hdr_v1->template_name_len))) {
			pr_err("attempting to restore a template name \
				that is too long\n");
			ret = -EINVAL;
			break;
		}
		data_v1 = bufp += (u_int8_t)hdr_v1->template_name_len;

		/* template name is not null terminated */
		memcpy(template_name, hdr_v1->template_name,
		       hdr_v1->template_name_len);
		template_name[hdr_v1->template_name_len] = 0;

		if (strcmp(template_name, "ima") == 0) {
			pr_err("attempting to restore an unsupported \
				template \"%s\" failed\n", template_name);
			ret = -EINVAL;
			break;
		}

		template_desc = lookup_template_desc(template_name);
		if (!template_desc) {
			template_desc = restore_template_fmt(template_name);
			if (!template_desc)
				break;
		}

		/*
		 * Only the running system's template format is initialized
		 * on boot.  As needed, initialize the other template formats.
		 */
		ret = template_desc_init_fields(template_desc->fmt,
						&(template_desc->fields),
						&(template_desc->num_fields));
		if (ret < 0) {
			pr_err("attempting to restore the template fmt \"%s\" \
				failed\n", template_desc->fmt);
			ret = -EINVAL;
			break;
		}

		if (bufp > (bufendp - sizeof(data_v1->template_data_size))) {
			pr_err("restoring the template data size failed\n");
			ret = -EINVAL;
			break;
		}
		bufp += (u_int8_t) sizeof(data_v1->template_data_size);

		if (ima_canonical_fmt)
			data_v1->template_data_size =
			    le32_to_cpu(data_v1->template_data_size);

		if (bufp > (bufendp - data_v1->template_data_size)) {
			pr_err("restoring the template data failed\n");
			ret = -EINVAL;
			break;
		}
		bufp += data_v1->template_data_size;

		ret = ima_restore_template_data(template_desc,
						data_v1->template_data,
						data_v1->template_data_size,
						&entry);
		if (ret < 0)
			break;

		memcpy(entry->digest, hdr_v1->digest, TPM_DIGEST_SIZE);
		entry->pcr =
		    !ima_canonical_fmt ? hdr_v1->pcr : le32_to_cpu(hdr_v1->pcr);
		ret = ima_restore_measurement_entry(entry);
		if (ret < 0)
			break;

	}
	return ret;
}
