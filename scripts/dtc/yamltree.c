// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright Linaro, Ltd. 2018
 * (C) Copyright Arm Holdings.  2017
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */

#include <stdlib.h>
#include <yaml.h>
#include "dtc.h"
#include "srcpos.h"

char *yaml_error_name[] = {
	[YAML_NO_ERROR] = "no error",
	[YAML_MEMORY_ERROR] = "memory error",
	[YAML_READER_ERROR] = "reader error",
	[YAML_SCANNER_ERROR] = "scanner error",
	[YAML_PARSER_ERROR] = "parser error",
	[YAML_COMPOSER_ERROR] = "composer error",
	[YAML_WRITER_ERROR] = "writer error",
	[YAML_EMITTER_ERROR] = "emitter error",
};

#define yaml_emitter_emit_or_die(emitter, event) (			\
{									\
	if (!yaml_emitter_emit(emitter, event))				\
		die("yaml '%s': %s in %s, line %i\n",			\
		    yaml_error_name[(emitter)->error], 			\
		    (emitter)->problem, __func__, __LINE__);		\
})

static void yaml_propval_int(yaml_emitter_t *emitter, struct marker *markers, char *data, int len, int width)
{
	yaml_event_t event;
	void *tag;
	int off, start_offset = markers->offset;

	switch(width) {
		case 1: tag = "!u8"; break;
		case 2: tag = "!u16"; break;
		case 4: tag = "!u32"; break;
		case 8: tag = "!u64"; break;
		default:
			die("Invalid width %i", width);
	}
	assert(len % width == 0);

	yaml_sequence_start_event_initialize(&event, NULL,
		(yaml_char_t *)tag, width == 4, YAML_FLOW_SEQUENCE_STYLE);
	yaml_emitter_emit_or_die(emitter, &event);

	for (off = 0; off < len; off += width) {
		char buf[32];
		struct marker *m;
		bool is_phandle = false;

		switch(width) {
		case 1:
			sprintf(buf, "0x%"PRIx8, *(uint8_t*)(data + off));
			break;
		case 2:
			sprintf(buf, "0x%"PRIx16, fdt16_to_cpu(*(fdt16_t*)(data + off)));
			break;
		case 4:
			sprintf(buf, "0x%"PRIx32, fdt32_to_cpu(*(fdt32_t*)(data + off)));
			m = markers;
			is_phandle = false;
			for_each_marker_of_type(m, REF_PHANDLE) {
				if (m->offset == (start_offset + off)) {
					is_phandle = true;
					break;
				}
			}
			break;
		case 8:
			sprintf(buf, "0x%"PRIx64, fdt64_to_cpu(*(fdt64_t*)(data + off)));
			break;
		}

		if (is_phandle)
			yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t*)"!phandle", (yaml_char_t *)buf,
				strlen(buf), 0, 0, YAML_PLAIN_SCALAR_STYLE);
		else
			yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t*)YAML_INT_TAG, (yaml_char_t *)buf,
				strlen(buf), 1, 1, YAML_PLAIN_SCALAR_STYLE);
		yaml_emitter_emit_or_die(emitter, &event);
	}

	yaml_sequence_end_event_initialize(&event);
	yaml_emitter_emit_or_die(emitter, &event);
}

static void yaml_propval_string(yaml_emitter_t *emitter, char *str, int len)
{
	yaml_event_t event;
	int i;

	assert(str[len-1] == '\0');

	/* Make sure the entire string is in the lower 7-bit ascii range */
	for (i = 0; i < len; i++)
		assert(isascii(str[i]));

	yaml_scalar_event_initialize(&event, NULL,
		(yaml_char_t *)YAML_STR_TAG, (yaml_char_t*)str,
		len-1, 0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
	yaml_emitter_emit_or_die(emitter, &event);
}

static void yaml_propval(yaml_emitter_t *emitter, struct property *prop)
{
	yaml_event_t event;
	int len = prop->val.len;
	struct marker *m = prop->val.markers;

	/* Emit the property name */
	yaml_scalar_event_initialize(&event, NULL,
		(yaml_char_t *)YAML_STR_TAG, (yaml_char_t*)prop->name,
		strlen(prop->name), 1, 1, YAML_PLAIN_SCALAR_STYLE);
	yaml_emitter_emit_or_die(emitter, &event);

	/* Boolean properties are easiest to deal with. Length is zero, so just emit 'true' */
	if (len == 0) {
		yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_BOOL_TAG,
			(yaml_char_t*)"true",
			strlen("true"), 1, 0, YAML_PLAIN_SCALAR_STYLE);
		yaml_emitter_emit_or_die(emitter, &event);
		return;
	}

	if (!m)
		die("No markers present in property '%s' value\n", prop->name);

	yaml_sequence_start_event_initialize(&event, NULL,
		(yaml_char_t *)YAML_SEQ_TAG, 1, YAML_FLOW_SEQUENCE_STYLE);
	yaml_emitter_emit_or_die(emitter, &event);

	for_each_marker(m) {
		int chunk_len;
		char *data = &prop->val.val[m->offset];

		if (m->type < TYPE_UINT8)
			continue;

		chunk_len = type_marker_length(m) ? : len;
		assert(chunk_len > 0);
		len -= chunk_len;

		switch(m->type) {
		case TYPE_UINT16:
			yaml_propval_int(emitter, m, data, chunk_len, 2);
			break;
		case TYPE_UINT32:
			yaml_propval_int(emitter, m, data, chunk_len, 4);
			break;
		case TYPE_UINT64:
			yaml_propval_int(emitter, m, data, chunk_len, 8);
			break;
		case TYPE_STRING:
			yaml_propval_string(emitter, data, chunk_len);
			break;
		default:
			yaml_propval_int(emitter, m, data, chunk_len, 1);
			break;
		}
	}

	yaml_sequence_end_event_initialize(&event);
	yaml_emitter_emit_or_die(emitter, &event);
}


static void yaml_tree(struct node *tree, yaml_emitter_t *emitter)
{
	struct property *prop;
	struct node *child;
	yaml_event_t event;

	if (tree->deleted)
		return;

	yaml_mapping_start_event_initialize(&event, NULL,
		(yaml_char_t *)YAML_MAP_TAG, 1, YAML_ANY_MAPPING_STYLE);
	yaml_emitter_emit_or_die(emitter, &event);

	for_each_property(tree, prop)
		yaml_propval(emitter, prop);

	/* Loop over all the children, emitting them into the map */
	for_each_child(tree, child) {
		yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG, (yaml_char_t*)child->name,
			strlen(child->name), 1, 0, YAML_PLAIN_SCALAR_STYLE);
		yaml_emitter_emit_or_die(emitter, &event);
		yaml_tree(child, emitter);
	}

	yaml_mapping_end_event_initialize(&event);
	yaml_emitter_emit_or_die(emitter, &event);
}

void dt_to_yaml(FILE *f, struct dt_info *dti)
{
	yaml_emitter_t emitter;
	yaml_event_t event;

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_output_file(&emitter, f);
	yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
	yaml_emitter_emit_or_die(&emitter, &event);

	yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 0);
	yaml_emitter_emit_or_die(&emitter, &event);

	yaml_sequence_start_event_initialize(&event, NULL, (yaml_char_t *)YAML_SEQ_TAG, 1, YAML_ANY_SEQUENCE_STYLE);
	yaml_emitter_emit_or_die(&emitter, &event);

	yaml_tree(dti->dt, &emitter);

	yaml_sequence_end_event_initialize(&event);
	yaml_emitter_emit_or_die(&emitter, &event);

	yaml_document_end_event_initialize(&event, 0);
	yaml_emitter_emit_or_die(&emitter, &event);

	yaml_stream_end_event_initialize(&event);
	yaml_emitter_emit_or_die(&emitter, &event);

	yaml_emitter_delete(&emitter);
}
