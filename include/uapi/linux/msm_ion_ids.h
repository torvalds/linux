/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */
#ifndef _MSM_ION_IDS_H
#define _MSM_ION_IDS_H

#define ION_BIT(nr) (1U << (nr))

/**
 * These are the only ids that should be used for Ion heap ids.
 * The ids listed are the order in which allocation will be attempted
 * if specified. Don't swap the order of heap ids unless you know what
 * you are doing!
 * Id's are spaced by purpose to allow new Id's to be inserted in-between (for
 * possible fallbacks)
 */

/* ION_BIT(0) is reserved for the generic system heap. */
#define ION_QSECOM_TA_HEAP_ID		ION_BIT(1)
#define ION_CAMERA_HEAP_ID		ION_BIT(2)
#define ION_DISPLAY_HEAP_ID		ION_BIT(3)
#define ION_ADSP_HEAP_ID		ION_BIT(4)
#define ION_AUDIO_ML_HEAP_ID		ION_BIT(5)
#define ION_USER_CONTIG_HEAP_ID		ION_BIT(6)
#define ION_QSECOM_HEAP_ID		ION_BIT(7)
#define ION_AUDIO_HEAP_ID		ION_BIT(8)
#define ION_CP_MM_HEAP_ID		ION_BIT(9)
#define ION_SECURE_HEAP_ID		ION_BIT(10)
#define ION_SECURE_DISPLAY_HEAP_ID	ION_BIT(11)
#define ION_SPSS_HEAP_ID		ION_BIT(14)
#define ION_SECURE_CARVEOUT_HEAP_ID	ION_BIT(15)
#define ION_TUI_CARVEOUT_HEAP_ID	ION_BIT(16)
#define ION_SYSTEM_HEAP_ID		ION_BIT(25)
#define ION_HEAP_ID_RESERVED		ION_BIT(31)

#endif /* _MSM_ION_IDS_H */
