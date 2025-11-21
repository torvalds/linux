/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025, Oracle and/or its affiliates.
 */

#ifndef _DISAS_H
#define _DISAS_H

struct disas_context;
struct disas_context *disas_context_create(struct objtool_file *file);
void disas_context_destroy(struct disas_context *dctx);
void disas_warned_funcs(struct disas_context *dctx);

#endif /* _DISAS_H */
