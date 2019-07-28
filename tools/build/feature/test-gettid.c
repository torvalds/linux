// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
#define _GNU_SOURCE
#include <unistd.h>

int main(void)
{
	return gettid();
}

#undef _GNU_SOURCE
