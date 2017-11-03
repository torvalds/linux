/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_COMMENT_H
#define _XT_COMMENT_H

#define XT_MAX_COMMENT_LEN 256

struct xt_comment_info {
	char comment[XT_MAX_COMMENT_LEN];
};

#endif /* XT_COMMENT_H */
