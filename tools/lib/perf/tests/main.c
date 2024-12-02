// SPDX-License-Identifier: GPL-2.0
#include <internal/tests.h>
#include "tests.h"

int tests_failed;
int tests_verbose;

int main(int argc, char **argv)
{
	__T("test cpumap", !test_cpumap(argc, argv));
	__T("test threadmap", !test_threadmap(argc, argv));
	__T("test evlist", !test_evlist(argc, argv));
	__T("test evsel", !test_evsel(argc, argv));
	return 0;
}
