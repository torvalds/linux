/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_ERRORS_TYPES_H
#define _BCACHEFS_SB_ERRORS_TYPES_H

#include "darray.h"

struct bch_sb_error_entry_cpu {
	u64			id:16,
				nr:48;
	u64			last_error_time;
};

typedef DARRAY(struct bch_sb_error_entry_cpu) bch_sb_errors_cpu;

#endif /* _BCACHEFS_SB_ERRORS_TYPES_H */

