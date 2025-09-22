/*
 * xfrd-disk.h - XFR (transfer) Daemon TCP system header file. Save/Load state to disk.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef XFRD_DISK_H
#define XFRD_DISK_H

struct xfrd_state;
struct nsd;

/* magic string to identify xfrd state file */
#define XFRD_FILE_MAGIC "NSDXFRD2"

/* read from state file as many zones as possible (until error/eof).*/
void xfrd_read_state(struct xfrd_state* xfrd);
/* write xfrd zone state if possible */
void xfrd_write_state(struct xfrd_state* xfrd);

/* create temp directory */
void xfrd_make_tempdir(struct nsd* nsd);
/* rmdir temp directory */
void xfrd_del_tempdir(struct nsd* nsd);
/* open temp file, makes directory if needed */
FILE* xfrd_open_xfrfile(struct nsd* nsd, uint64_t number, char* mode);
/* unlink temp file */
void xfrd_unlink_xfrfile(struct nsd* nsd, uint64_t number);
/* get temp file size */
uint64_t xfrd_get_xfrfile_size(struct nsd* nsd, uint64_t number );

#endif /* XFRD_DISK_H */
