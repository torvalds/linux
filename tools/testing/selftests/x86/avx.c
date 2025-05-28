// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE /* Required for inline xstate helpers */
#include "xstate.h"

int main(void)
{
	test_xstate(XFEATURE_YMM);
	test_xstate(XFEATURE_OPMASK);
	test_xstate(XFEATURE_ZMM_Hi256);
	test_xstate(XFEATURE_Hi16_ZMM);
}
