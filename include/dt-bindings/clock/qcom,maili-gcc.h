/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GCC_MAILI_H
#define _DT_BINDINGS_CLK_QCOM_GCC_MAILI_H

#include "qcom,hawi-gcc.h"

/* Maili has below additional clocks on top of Hawi */
#define GCC_QUPV3_WRAP5_CORE_2X_CLK				188
#define GCC_QUPV3_WRAP5_CORE_CLK				189
#define GCC_QUPV3_WRAP5_QSPI_REF_CLK				190
#define GCC_QUPV3_WRAP5_QSPI_REF_CLK_SRC			191
#define GCC_QUPV3_WRAP5_S0_CLK					192
#define GCC_QUPV3_WRAP5_S0_CLK_SRC				193
#define GCC_QUPV3_WRAP_5_M_AHB_CLK				194
#define GCC_QUPV3_WRAP_5_S_AHB_CLK				195

#endif
