/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */

/*
 * This header file defines the interfaces available from the CTF debugger
 * library, libctf, and an equivalent kernel module.  This API can be used by
 * a debugger to operate on data in the Compact ANSI-C Type Format (CTF).
 * This is NOT a public interface, although it may eventually become one in
 * the fullness of time after we gain more experience with the interfaces.
 *
 * In the meantime, be aware that any program linked with this API in this
 * release of Solaris is almost guaranteed to break in the next release.
 *
 * In short, do not user this header file or the CTF routines for any purpose.
 */

#ifndef	_CTF_API_H
#define	_CTF_API_H

#include <sys/types.h>
#include <sys/param.h>
#include <sys/elf.h>
#include <sys/ctf.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Clients can open one or more CTF containers and obtain a pointer to an
 * opaque ctf_file_t.  Types are identified by an opaque ctf_id_t token.
 * These opaque definitions allow libctf to evolve without breaking clients.
 */
typedef struct ctf_file ctf_file_t;
typedef long ctf_id_t;

/*
 * If the debugger needs to provide the CTF library with a set of raw buffers
 * for use as the CTF data, symbol table, and string table, it can do so by
 * filling in ctf_sect_t structures and passing them to ctf_bufopen():
 */
typedef struct ctf_sect {
	const char *cts_name;	/* section name (if any) */
	ulong_t cts_type;	/* section type (ELF SHT_... value) */
	ulong_t cts_flags;	/* section flags (ELF SHF_... value) */
#ifdef illumos
	const void *cts_data;	/* pointer to section data */
#else
	void *cts_data;		/* pointer to section data */
#endif
	size_t cts_size;	/* size of data in bytes */
	size_t cts_entsize;	/* size of each section entry (symtab only) */
	off64_t cts_offset;	/* file offset of this section (if any) */
} ctf_sect_t;

/*
 * Encoding information for integers, floating-point values, and certain other
 * intrinsics can be obtained by calling ctf_type_encoding(), below.  The flags
 * field will contain values appropriate for the type defined in <sys/ctf.h>.
 */
typedef struct ctf_encoding {
	uint_t cte_format;	/* data format (CTF_INT_* or CTF_FP_* flags) */
	uint_t cte_offset;	/* offset of value in bits */
	uint_t cte_bits;	/* size of storage in bits */
} ctf_encoding_t;

typedef struct ctf_membinfo {
	ctf_id_t ctm_type;	/* type of struct or union member */
	ulong_t ctm_offset;	/* offset of member in bits */
} ctf_membinfo_t;

typedef struct ctf_arinfo {
	ctf_id_t ctr_contents;	/* type of array contents */
	ctf_id_t ctr_index;	/* type of array index */
	uint_t ctr_nelems;	/* number of elements */
} ctf_arinfo_t;

typedef struct ctf_funcinfo {
	ctf_id_t ctc_return;	/* function return type */
	uint_t ctc_argc;	/* number of typed arguments to function */
	uint_t ctc_flags;	/* function attributes (see below) */
} ctf_funcinfo_t;

typedef struct ctf_lblinfo {
	ctf_id_t ctb_typeidx;	/* last type associated with the label */
} ctf_lblinfo_t;

#define	CTF_FUNC_VARARG	0x1	/* function arguments end with varargs */

/*
 * Functions that return integer status or a ctf_id_t use the following value
 * to indicate failure.  ctf_errno() can be used to obtain an error code.
 */
#define	CTF_ERR	(-1L)

/*
 * The CTF data model is inferred to be the caller's data model or the data
 * model of the given object, unless ctf_setmodel() is explicitly called.
 */
#define	CTF_MODEL_ILP32	1	/* object data model is ILP32 */
#define	CTF_MODEL_LP64	2	/* object data model is LP64 */
#ifdef _LP64
#define	CTF_MODEL_NATIVE	CTF_MODEL_LP64
#else
#define	CTF_MODEL_NATIVE	CTF_MODEL_ILP32
#endif

/*
 * Dynamic CTF containers can be created using ctf_create().  The ctf_add_*
 * routines can be used to add new definitions to the dynamic container.
 * New types are labeled as root or non-root to determine whether they are
 * visible at the top-level program scope when subsequently doing a lookup.
 */
#define	CTF_ADD_NONROOT	0	/* type only visible in nested scope */
#define	CTF_ADD_ROOT	1	/* type visible at top-level scope */

/*
 * These typedefs are used to define the signature for callback functions
 * that can be used with the iteration and visit functions below:
 */
typedef int ctf_visit_f(const char *, ctf_id_t, ulong_t, int, void *);
typedef int ctf_member_f(const char *, ctf_id_t, ulong_t, void *);
typedef int ctf_enum_f(const char *, int, void *);
typedef int ctf_type_f(ctf_id_t, void *);
typedef int ctf_label_f(const char *, const ctf_lblinfo_t *, void *);

extern ctf_file_t *ctf_bufopen(const ctf_sect_t *, const ctf_sect_t *,
    const ctf_sect_t *, int *);
extern ctf_file_t *ctf_fdopen(int, int *);
extern ctf_file_t *ctf_open(const char *, int *);
extern ctf_file_t *ctf_create(int *);
extern ctf_file_t *ctf_dup(ctf_file_t *);
extern void ctf_close(ctf_file_t *);

extern ctf_file_t *ctf_parent_file(ctf_file_t *);
extern const char *ctf_parent_name(ctf_file_t *);

extern int ctf_import(ctf_file_t *, ctf_file_t *);
extern int ctf_setmodel(ctf_file_t *, int);
extern int ctf_getmodel(ctf_file_t *);

extern void ctf_setspecific(ctf_file_t *, void *);
extern void *ctf_getspecific(ctf_file_t *);

extern int ctf_errno(ctf_file_t *);
extern const char *ctf_errmsg(int);
extern int ctf_version(int);

extern int ctf_func_info(ctf_file_t *, ulong_t, ctf_funcinfo_t *);
extern int ctf_func_args(ctf_file_t *, ulong_t, uint_t, ctf_id_t *);

extern ctf_id_t ctf_lookup_by_name(ctf_file_t *, const char *);
extern ctf_id_t ctf_lookup_by_symbol(ctf_file_t *, ulong_t);

extern ctf_id_t ctf_type_resolve(ctf_file_t *, ctf_id_t);
extern ssize_t ctf_type_lname(ctf_file_t *, ctf_id_t, char *, size_t);
extern char *ctf_type_name(ctf_file_t *, ctf_id_t, char *, size_t);
extern char *ctf_type_qname(ctf_file_t *, ctf_id_t, char *, size_t,
    const char *);
extern ssize_t ctf_type_size(ctf_file_t *, ctf_id_t);
extern ssize_t ctf_type_align(ctf_file_t *, ctf_id_t);
extern int ctf_type_kind(ctf_file_t *, ctf_id_t);
extern ctf_id_t ctf_type_reference(ctf_file_t *, ctf_id_t);
extern ctf_id_t ctf_type_pointer(ctf_file_t *, ctf_id_t);
extern int ctf_type_encoding(ctf_file_t *, ctf_id_t, ctf_encoding_t *);
extern int ctf_type_visit(ctf_file_t *, ctf_id_t, ctf_visit_f *, void *);
extern int ctf_type_cmp(ctf_file_t *, ctf_id_t, ctf_file_t *, ctf_id_t);
extern int ctf_type_compat(ctf_file_t *, ctf_id_t, ctf_file_t *, ctf_id_t);

extern int ctf_member_info(ctf_file_t *, ctf_id_t, const char *,
    ctf_membinfo_t *);
extern int ctf_array_info(ctf_file_t *, ctf_id_t, ctf_arinfo_t *);

extern const char *ctf_enum_name(ctf_file_t *, ctf_id_t, int);
extern int ctf_enum_value(ctf_file_t *, ctf_id_t, const char *, int *);

extern const char *ctf_label_topmost(ctf_file_t *);
extern int ctf_label_info(ctf_file_t *, const char *, ctf_lblinfo_t *);

extern int ctf_member_iter(ctf_file_t *, ctf_id_t, ctf_member_f *, void *);
extern int ctf_enum_iter(ctf_file_t *, ctf_id_t, ctf_enum_f *, void *);
extern int ctf_type_iter(ctf_file_t *, ctf_type_f *, void *);
extern int ctf_label_iter(ctf_file_t *, ctf_label_f *, void *);

extern ctf_id_t ctf_add_array(ctf_file_t *, uint_t, const ctf_arinfo_t *);
extern ctf_id_t ctf_add_const(ctf_file_t *, uint_t, ctf_id_t);
extern ctf_id_t ctf_add_enum(ctf_file_t *, uint_t, const char *);
extern ctf_id_t ctf_add_float(ctf_file_t *, uint_t,
    const char *, const ctf_encoding_t *);
extern ctf_id_t ctf_add_forward(ctf_file_t *, uint_t, const char *, uint_t);
extern ctf_id_t ctf_add_function(ctf_file_t *, uint_t,
    const ctf_funcinfo_t *, const ctf_id_t *);
extern ctf_id_t ctf_add_integer(ctf_file_t *, uint_t,
    const char *, const ctf_encoding_t *);
extern ctf_id_t ctf_add_pointer(ctf_file_t *, uint_t, ctf_id_t);
extern ctf_id_t ctf_add_type(ctf_file_t *, ctf_file_t *, ctf_id_t);
extern ctf_id_t ctf_add_typedef(ctf_file_t *, uint_t, const char *, ctf_id_t);
extern ctf_id_t ctf_add_restrict(ctf_file_t *, uint_t, ctf_id_t);
extern ctf_id_t ctf_add_struct(ctf_file_t *, uint_t, const char *);
extern ctf_id_t ctf_add_union(ctf_file_t *, uint_t, const char *);
extern ctf_id_t ctf_add_volatile(ctf_file_t *, uint_t, ctf_id_t);

extern int ctf_add_enumerator(ctf_file_t *, ctf_id_t, const char *, int);
extern int ctf_add_member(ctf_file_t *, ctf_id_t, const char *, ctf_id_t);

extern int ctf_set_array(ctf_file_t *, ctf_id_t, const ctf_arinfo_t *);

extern int ctf_delete_type(ctf_file_t *, ctf_id_t);

extern int ctf_update(ctf_file_t *);
extern int ctf_discard(ctf_file_t *);
extern int ctf_write(ctf_file_t *, int);

#ifdef _KERNEL

struct module;
extern ctf_file_t *ctf_modopen(struct module *, int *);

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _CTF_API_H */
