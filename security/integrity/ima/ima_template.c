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
#include "ima.h"
#include "ima_template_lib.h"

static struct ima_template_desc defined_templates[] = {
	{.name = IMA_TEMPLATE_IMA_NAME,.fmt = IMA_TEMPLATE_IMA_FMT},
};

static struct ima_template_field supported_fields[] = {
	{.field_id = "d",.field_init = ima_eventdigest_init,
	 .field_show = ima_show_template_digest},
	{.field_id = "n",.field_init = ima_eventname_init,
	 .field_show = ima_show_template_string},
};

static struct ima_template_field *lookup_template_field(const char *field_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_fields); i++)
		if (strncmp(supported_fields[i].field_id, field_id,
			    IMA_TEMPLATE_FIELD_ID_MAX_LEN) == 0)
			return &supported_fields[i];
	return NULL;
}

static int template_fmt_size(char *template_fmt)
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

static int template_desc_init_fields(char *template_fmt,
				     struct ima_template_field ***fields,
				     int *num_fields)
{
	char *c, *template_fmt_ptr = template_fmt;
	int template_num_fields = template_fmt_size(template_fmt);
	int i, result = 0;

	if (template_num_fields > IMA_TEMPLATE_NUM_FIELDS_MAX)
		return -EINVAL;

	*fields = kzalloc(template_num_fields * sizeof(*fields), GFP_KERNEL);
	if (*fields == NULL) {
		result = -ENOMEM;
		goto out;
	}
	for (i = 0; (c = strsep(&template_fmt_ptr, "|")) != NULL &&
	     i < template_num_fields; i++) {
		struct ima_template_field *f = lookup_template_field(c);

		if (!f) {
			result = -ENOENT;
			goto out;
		}
		(*fields)[i] = f;
	}
	*num_fields = i;
	return 0;
out:
	kfree(*fields);
	*fields = NULL;
	return result;
}

static int init_defined_templates(void)
{
	int i = 0;
	int result = 0;

	/* Init defined templates. */
	for (i = 0; i < ARRAY_SIZE(defined_templates); i++) {
		struct ima_template_desc *template = &defined_templates[i];

		result = template_desc_init_fields(template->fmt,
						   &(template->fields),
						   &(template->num_fields));
		if (result < 0)
			return result;
	}
	return result;
}

int ima_init_template(void)
{
	int result;

	result = init_defined_templates();
	if (result < 0)
		return result;

	return 0;
}
