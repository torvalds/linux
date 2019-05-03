// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
//
#ifndef HAVE_GET_CURRENT_DIR_NAME
#include "util.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdlib.h>

/* Android's 'bionic' library, for one, doesn't have this */

char *get_current_dir_name(void)
{
	char pwd[PATH_MAX];

	return getcwd(pwd, sizeof(pwd)) == NULL ? NULL : strdup(pwd);
}
#endif // HAVE_GET_CURRENT_DIR_NAME
