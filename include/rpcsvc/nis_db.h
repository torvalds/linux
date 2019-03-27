/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 *
 * $FreeBSD$
 */

/*
 * Copyright (c) 1991, by Sun Microsystems Inc.
 */

/*
 * This header file defines the interface to the NIS database. All
 * implementations of the database must export at least these routines.
 * They must also follow the conventions set herein. See the implementors
 * guide for specific semantics that are required.
 */

#ifndef	_RPCSVC_NIS_DB_H
#define	_RPCSVC_NIS_DB_H


/* From: #pragma ident	"@(#)nis_db.h	1.8	94/05/03 SMI" */

/*
 * Note: although the version of <rpcsvc/nis_db.h> shipped with Solaris
 * 2.5/2.5.x is actually older than this one (according to the ident
 * string), it contains changes and a few added functions. Those changes
 * have been hand merged into this file to bring it up to date.
 */

#include <rpc/rpc.h>
#include <rpcsvc/nis.h>

#ifdef	__cplusplus
extern "C" {
#endif

enum db_status {
	DB_SUCCESS = 0,
	DB_NOTFOUND = 1,
	DB_NOTUNIQUE = 2,
	DB_BADTABLE = 3,
	DB_BADQUERY = 4,
	DB_BADOBJECT = 5,
	DB_MEMORY_LIMIT = 6,
	DB_STORAGE_LIMIT = 7,
	DB_INTERNAL_ERROR = 8
};
typedef enum db_status db_status;

enum db_action {
	DB_LOOKUP = 0,
	DB_REMOVE = 1,
	DB_ADD = 2,
	DB_FIRST = 3,
	DB_NEXT = 4,
	DB_ALL = 5,
	DB_RESET_NEXT = 6
};
typedef enum db_action db_action;

typedef entry_obj *entry_object_p;

typedef struct {
	u_int db_next_desc_len;
	char *db_next_desc_val;
} db_next_desc;

struct db_result {
	db_status status;
	db_next_desc nextinfo;
	struct {
		u_int objects_len;
		entry_object_p *objects_val;
	} objects;
	long ticks;
};
typedef struct db_result db_result;

/*
 * Prototypes for the database functions.
 */

extern bool_t db_initialize(char *);
#ifdef ORIGINAL_DECLS
extern bool_t db_create_table(char *, table_obj *);
extern bool_t db_destroy_table(char *);
#else
extern db_status db_create_table(char *, table_obj *);
extern db_status db_destroy_table(char *);
#endif
extern db_result *db_first_entry(char *, int, nis_attr *);
extern db_result *db_next_entry(char *, db_next_desc *);
extern db_result *db_reset_next_entry(char *, db_next_desc *);
extern db_result *db_list_entries(char *, int, nis_attr *);
extern db_result *db_add_entry(char *, int,  nis_attr *, entry_obj *);
extern db_result *db_remove_entry(char *, int, nis_attr *);
extern db_status db_checkpoint(char *);
extern db_status db_standby(char *);
#ifndef ORIGINAL_DECLS
extern db_status db_table_exists(char *);
extern db_status db_unload_table(char *);
extern void db_free_result(db_result *);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _RPCSVC_NIS_DB_H */
