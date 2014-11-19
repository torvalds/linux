/*
 * Implementation of get_cpuid().
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Alexander Yarygin <yarygin@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "../../util/header.h"

int get_cpuid(char *buffer, size_t sz)
{
	const char *cpuid = "IBM/S390";

	if (strlen(cpuid) + 1 > sz)
		return -1;

	strcpy(buffer, cpuid);
	return 0;
}
