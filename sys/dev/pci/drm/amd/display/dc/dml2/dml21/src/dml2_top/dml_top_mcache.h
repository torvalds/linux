// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML_TOP_MCACHE_H__
#define __DML_TOP_MCACHE_H__

#include "dml2_external_lib_deps.h"
#include "dml_top_display_cfg_types.h"
#include "dml_top_types.h"
#include "dml2_internal_shared_types.h"

bool dml2_top_mcache_calc_mcache_count_and_offsets(struct top_mcache_calc_mcache_count_and_offsets_in_out *params);

void dml2_top_mcache_assign_global_mcache_ids(struct top_mcache_assign_global_mcache_ids_in_out *params);

bool dml2_top_mcache_validate_admissability(struct top_mcache_validate_admissability_in_out *params);

bool dml2_top_mcache_build_mcache_programming(struct dml2_build_mcache_programming_in_out *params);

bool dml2_top_mcache_unit_test(void);

#endif
