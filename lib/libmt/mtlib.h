/*-
 * Copyright (c) 2013, 2014 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Ken Merry           (Spectra Logic Corporation)
 *
 * $FreeBSD$
 */

#ifndef	_MTLIB_H
#define	_MTLIB_H

typedef enum {
	MT_TYPE_NONE,
	MT_TYPE_STRING,
	MT_TYPE_INT,
	MT_TYPE_UINT,
	MT_TYPE_NODE
} mt_variable_type;

struct mt_status_nv {
	char *name;
	char *value;
	STAILQ_ENTRY(mt_status_nv) links;
};

struct mt_status_entry {
	char *entry_name;
	char *value;
	uint64_t value_unsigned;
	int64_t value_signed;
	char *fmt;
	char *desc;
	size_t size;
	mt_variable_type var_type;
	struct mt_status_entry *parent;
	STAILQ_HEAD(, mt_status_nv) nv_list;
	STAILQ_HEAD(, mt_status_entry) child_entries;
	STAILQ_ENTRY(mt_status_entry) links;
};

struct mt_status_data {
	int level;
	struct sbuf *cur_sb[32];
	struct mt_status_entry *cur_entry[32];
	int error;
	char error_str[128];
	STAILQ_HEAD(, mt_status_entry) entries;
};

typedef enum {
	MT_PF_NONE		= 0x00,
	MT_PF_VERBOSE		= 0x01,
	MT_PF_FULL_PATH		= 0x02,
	MT_PF_INCLUDE_ROOT	= 0x04
} mt_print_flags;

struct mt_print_params {
	mt_print_flags flags;
	char root_name[64];
};

__BEGIN_DECLS
void mt_start_element(void *user_data, const char *name, const char **attr);
void mt_end_element(void *user_data, const char *name);
void mt_char_handler(void *user_data, const XML_Char *str, int len);
void mt_status_tree_sbuf(struct sbuf *sb, struct mt_status_entry *entry,
			 int indent, void (*sbuf_func)(struct sbuf *sb,
			 struct mt_status_entry *entry, void *arg), void *arg);
void mt_status_tree_print(struct mt_status_entry *entry, int indent,
			  void (*print_func)(struct mt_status_entry *entry,
			  void *arg), void *arg);
struct mt_status_entry *mt_entry_find(struct mt_status_entry *entry,
				      char *name);
struct mt_status_entry *mt_status_entry_find(struct mt_status_data *status_data,
					     char *name);
void mt_status_entry_free(struct mt_status_entry *entry);
void mt_status_free(struct mt_status_data *status_data);
void mt_entry_sbuf(struct sbuf *sb, struct mt_status_entry *entry, char *fmt);
void mt_param_parent_print(struct mt_status_entry *entry,
			   struct mt_print_params *print_params);
void mt_param_parent_sbuf(struct sbuf *sb, struct mt_status_entry *entry,
			  struct mt_print_params *print_params);
void mt_param_entry_sbuf(struct sbuf *sb, struct mt_status_entry *entry,
			 void *arg);
void mt_param_entry_print(struct mt_status_entry *entry, void *arg);
int mt_protect_print(struct mt_status_data *status_data, int verbose);
int mt_param_list(struct mt_status_data *status_data, char *param_name,
		  int quiet);
const char *mt_density_name(int density_num);
int mt_density_bp(int density_num, int bpi);
int mt_density_num(const char *density_name);
int mt_get_xml_str(int mtfd, unsigned long cmd, char **xml_str);
int mt_get_status(char *xml_str, struct mt_status_data *status_data);
__END_DECLS

#endif /* _MTLIB_H */
