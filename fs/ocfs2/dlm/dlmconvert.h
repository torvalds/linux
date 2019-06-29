/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmconvert.h
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 */

#ifndef DLMCONVERT_H
#define DLMCONVERT_H

enum dlm_status dlmconvert_master(struct dlm_ctxt *dlm,
				  struct dlm_lock_resource *res,
				  struct dlm_lock *lock, int flags, int type);
enum dlm_status dlmconvert_remote(struct dlm_ctxt *dlm,
				  struct dlm_lock_resource *res,
				  struct dlm_lock *lock, int flags, int type);

#endif
