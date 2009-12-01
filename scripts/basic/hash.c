/*
 * Copyright (C) 2008 Red Hat, Inc., Jason Baron <jbaron@redhat.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DYNAMIC_DEBUG_HASH_BITS 6

static const char *program;

static void usage(void)
{
	printf("Usage: %s <djb2|r5> <modname>\n", program);
	exit(1);
}

/* djb2 hashing algorithm by Dan Bernstein. From:
 * http://www.cse.yorku.ca/~oz/hash.html
 */

static unsigned int djb2_hash(char *str)
{
	unsigned long hash = 5381;
	int c;

	c = *str;
	while (c) {
		hash = ((hash << 5) + hash) + c;
		c = *++str;
	}
	return (unsigned int)(hash & ((1 << DYNAMIC_DEBUG_HASH_BITS) - 1));
}

static unsigned int r5_hash(char *str)
{
	unsigned long hash = 0;
	int c;

	c = *str;
	while (c) {
		hash = (hash + (c << 4) + (c >> 4)) * 11;
		c = *++str;
	}
	return (unsigned int)(hash & ((1 << DYNAMIC_DEBUG_HASH_BITS) - 1));
}

int main(int argc, char *argv[])
{
	program = argv[0];

	if (argc != 3)
		usage();
	if (!strcmp(argv[1], "djb2"))
		printf("%d\n", djb2_hash(argv[2]));
	else if (!strcmp(argv[1], "r5"))
		printf("%d\n", r5_hash(argv[2]));
	else
		usage();
	exit(0);
}

