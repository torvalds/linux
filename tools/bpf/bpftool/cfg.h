/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2018 Netronome Systems, Inc. */

#ifndef __BPF_TOOL_CFG_H
#define __BPF_TOOL_CFG_H

#include "xlated_dumper.h"

void dump_xlated_cfg(struct dump_data *dd, void *buf, unsigned int len,
		     bool opcodes, bool linum);

#endif /* __BPF_TOOL_CFG_H */
