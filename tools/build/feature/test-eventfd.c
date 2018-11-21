// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>

#include <sys/eventfd.h>

int main(void)
{
	return eventfd(0, EFD_NONBLOCK);
}
