/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Politecnico di Torino, Italy
 *                    TORSEC group -- https://security.polito.it
 *
 * Author: Roberto Sassu <roberto.sassu@polito.it>
 *
 * File: ima_template_lib.h
 *      Header for the library of supported template fields.
 */
#ifndef __LINUX_IMA_TEMPLATE_LIB_H
#define __LINUX_IMA_TEMPLATE_LIB_H

#include <linux/seq_file.h>
#include "ima.h"

#define ENFORCE_FIELDS 0x00000001
#define ENFORCE_BUFEND 0x00000002

void ima_show_template_digest(struct seq_file *m, enum ima_show_type show,
			      struct ima_field_data *field_data);
void ima_show_template_digest_ng(struct seq_file *m, enum ima_show_type show,
				 struct ima_field_data *field_data);
void ima_show_template_string(struct seq_file *m, enum ima_show_type show,
			      struct ima_field_data *field_data);
void ima_show_template_sig(struct seq_file *m, enum ima_show_type show,
			   struct ima_field_data *field_data);
void ima_show_template_buf(struct seq_file *m, enum ima_show_type show,
			   struct ima_field_data *field_data);
void ima_show_template_uint(struct seq_file *m, enum ima_show_type show,
			    struct ima_field_data *field_data);
int ima_parse_buf(void *bufstartp, void *bufendp, void **bufcurp,
		  int maxfields, struct ima_field_data *fields, int *curfields,
		  unsigned long *len_mask, int enforce_mask, char *bufname);
int ima_eventdigest_init(struct ima_event_data *event_data,
			 struct ima_field_data *field_data);
int ima_eventname_init(struct ima_event_data *event_data,
		       struct ima_field_data *field_data);
int ima_eventdigest_ng_init(struct ima_event_data *event_data,
			    struct ima_field_data *field_data);
int ima_eventdigest_modsig_init(struct ima_event_data *event_data,
				struct ima_field_data *field_data);
int ima_eventname_ng_init(struct ima_event_data *event_data,
			  struct ima_field_data *field_data);
int ima_eventsig_init(struct ima_event_data *event_data,
		      struct ima_field_data *field_data);
int ima_eventbuf_init(struct ima_event_data *event_data,
		      struct ima_field_data *field_data);
int ima_eventmodsig_init(struct ima_event_data *event_data,
			 struct ima_field_data *field_data);
int ima_eventevmsig_init(struct ima_event_data *event_data,
			 struct ima_field_data *field_data);
int ima_eventinodeuid_init(struct ima_event_data *event_data,
			   struct ima_field_data *field_data);
int ima_eventinodegid_init(struct ima_event_data *event_data,
			   struct ima_field_data *field_data);
int ima_eventinodemode_init(struct ima_event_data *event_data,
			    struct ima_field_data *field_data);
int ima_eventinodexattrnames_init(struct ima_event_data *event_data,
				  struct ima_field_data *field_data);
int ima_eventinodexattrlengths_init(struct ima_event_data *event_data,
				    struct ima_field_data *field_data);
int ima_eventinodexattrvalues_init(struct ima_event_data *event_data,
				   struct ima_field_data *field_data);
#endif /* __LINUX_IMA_TEMPLATE_LIB_H */
