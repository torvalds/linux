/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * Declarations for the common functions used by ocs_mgmt.
 */


#if !defined(__OCS_MGMT_H__)
#define __OCS_MGMT_H__

#include "ocs_utils.h"

#define OCS_MGMT_MAX_NAME 128
#define OCS_MGMT_MAX_VALUE 128

#define MGMT_MODE_RD	4
#define MGMT_MODE_WR	2
#define MGMT_MODE_EX	1
#define MGMT_MODE_RW	(MGMT_MODE_RD | MGMT_MODE_WR)

#define FW_WRITE_BUFSIZE 64*1024

typedef struct ocs_mgmt_fw_write_result {
	ocs_sem_t semaphore;
	int32_t status;
	uint32_t actual_xfer;
	uint32_t change_status;
} ocs_mgmt_fw_write_result_t;


/*
 * This structure is used in constructing a table of internal handler functions.
 */
typedef void (*ocs_mgmt_get_func)(ocs_t *, char *, ocs_textbuf_t*);
typedef int (*ocs_mgmt_set_func)(ocs_t *, char *, char *);
typedef int (*ocs_mgmt_action_func)(ocs_t *, char *, void *, uint32_t, void *, uint32_t);
typedef struct ocs_mgmt_table_entry_s {
	char *name;
	ocs_mgmt_get_func get_handler;
	ocs_mgmt_set_func set_handler;
	ocs_mgmt_action_func action_handler;
} ocs_mgmt_table_entry_t;

/*
 * This structure is used when defining the top level management handlers for
 * different types of objects (sport, node, domain, etc)
 */
typedef void (*ocs_mgmt_get_list_handler)(ocs_textbuf_t *textbuf, void* object);
typedef void (*ocs_mgmt_get_all_handler)(ocs_textbuf_t *textbuf, void* object);
typedef int (*ocs_mgmt_get_handler)(ocs_textbuf_t *, char *parent, char *name, void *object);
typedef int (*ocs_mgmt_set_handler)(char *parent, char *name, char *value, void *object);
typedef int (*ocs_mgmt_exec_handler)(char *parent, char *action, void *arg_in, uint32_t arg_in_length,
				      void *arg_out, uint32_t arg_out_length, void *object);

typedef struct ocs_mgmt_functions_s {
	ocs_mgmt_get_list_handler	get_list_handler;
	ocs_mgmt_get_handler		get_handler;
	ocs_mgmt_get_all_handler	get_all_handler;
	ocs_mgmt_set_handler		set_handler;
	ocs_mgmt_exec_handler		exec_handler;
} ocs_mgmt_functions_t;


/* Helper functions */
extern void ocs_mgmt_start_section(ocs_textbuf_t *textbuf, const char *name, int index);
extern void ocs_mgmt_start_unnumbered_section(ocs_textbuf_t *textbuf, const char *name);
extern void ocs_mgmt_end_section(ocs_textbuf_t *textbuf, const char *name, int index);
extern void ocs_mgmt_end_unnumbered_section(ocs_textbuf_t *textbuf, const char *name);
extern void ocs_mgmt_emit_property_name(ocs_textbuf_t *textbuf, int access, const char *name);
extern void ocs_mgmt_emit_string(ocs_textbuf_t *textbuf, int access, const char *name, const char *value);
__attribute__((format(printf,4,5)))
extern void ocs_mgmt_emit_int(ocs_textbuf_t *textbuf, int access, const char *name, const char *fmt, ...);
extern void ocs_mgmt_emit_boolean(ocs_textbuf_t *textbuf, int access, const char *name, const int value);
extern int parse_wwn(char *wwn_in, uint64_t *wwn_out);

/* Top level management functions - called by the ioctl */
extern void ocs_mgmt_get_list(ocs_t *ocs, ocs_textbuf_t *textbuf);
extern void ocs_mgmt_get_all(ocs_t *ocs, ocs_textbuf_t *textbuf);
extern int ocs_mgmt_get(ocs_t *ocs, char *name, ocs_textbuf_t *textbuf);
extern int ocs_mgmt_set(ocs_t *ocs, char *name, char *value);
extern int ocs_mgmt_exec(ocs_t *ocs, char *action, void *arg_in, uint32_t arg_in_length,
		void *arg_out, uint32_t arg_out_length);

extern int set_req_wwnn(ocs_t*, char*, char*);
extern int set_req_wwpn(ocs_t*, char*, char*);
extern int set_configured_speed(ocs_t*, char*, char*);
extern int set_configured_topology(ocs_t*, char*, char*);

#endif
