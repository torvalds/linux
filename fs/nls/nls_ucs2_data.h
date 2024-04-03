/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _NLS_UCS2_DATA_H
#define _NLS_UCS2_DATA_H

struct UniCaseRange {
	wchar_t start;
	wchar_t end;
	signed char *table;
};

extern signed char NlsUniUpperTable[512];
extern const struct UniCaseRange NlsUniUpperRange[];

#endif /* _NLS_UCS2_DATA_H */
