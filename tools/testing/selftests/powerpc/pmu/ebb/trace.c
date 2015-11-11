/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "trace.h"


struct trace_buffer *trace_buffer_allocate(u64 size)
{
	struct trace_buffer *tb;

	if (size < sizeof(*tb)) {
		fprintf(stderr, "Error: trace buffer too small\n");
		return NULL;
	}

	tb = mmap(NULL, size, PROT_READ | PROT_WRITE,
		  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (tb == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	tb->size = size;
	tb->tail = tb->data;
	tb->overflow = false;

	return tb;
}

static bool trace_check_bounds(struct trace_buffer *tb, void *p)
{
	return p < ((void *)tb + tb->size);
}

static bool trace_check_alloc(struct trace_buffer *tb, void *p)
{
	/*
	 * If we ever overflowed don't allow any more input. This prevents us
	 * from dropping a large item and then later logging a small one. The
	 * buffer should just stop when overflow happened, not be patchy. If
	 * you're overflowing, make your buffer bigger.
	 */
	if (tb->overflow)
		return false;

	if (!trace_check_bounds(tb, p)) {
		tb->overflow = true;
		return false;
	}

	return true;
}

static void *trace_alloc(struct trace_buffer *tb, int bytes)
{
	void *p, *newtail;

	p = tb->tail;
	newtail = tb->tail + bytes;
	if (!trace_check_alloc(tb, newtail))
		return NULL;

	tb->tail = newtail;

	return p;
}

static struct trace_entry *trace_alloc_entry(struct trace_buffer *tb, int payload_size)
{
	struct trace_entry *e;

	e = trace_alloc(tb, sizeof(*e) + payload_size);
	if (e)
		e->length = payload_size;

	return e;
}

int trace_log_reg(struct trace_buffer *tb, u64 reg, u64 value)
{
	struct trace_entry *e;
	u64 *p;

	e = trace_alloc_entry(tb, sizeof(reg) + sizeof(value));
	if (!e)
		return -ENOSPC;

	e->type = TRACE_TYPE_REG;
	p = (u64 *)e->data;
	*p++ = reg;
	*p++ = value;

	return 0;
}

int trace_log_counter(struct trace_buffer *tb, u64 value)
{
	struct trace_entry *e;
	u64 *p;

	e = trace_alloc_entry(tb, sizeof(value));
	if (!e)
		return -ENOSPC;

	e->type = TRACE_TYPE_COUNTER;
	p = (u64 *)e->data;
	*p++ = value;

	return 0;
}

int trace_log_string(struct trace_buffer *tb, char *str)
{
	struct trace_entry *e;
	char *p;
	int len;

	len = strlen(str);

	/* We NULL terminate to make printing easier */
	e = trace_alloc_entry(tb, len + 1);
	if (!e)
		return -ENOSPC;

	e->type = TRACE_TYPE_STRING;
	p = (char *)e->data;
	memcpy(p, str, len);
	p += len;
	*p = '\0';

	return 0;
}

int trace_log_indent(struct trace_buffer *tb)
{
	struct trace_entry *e;

	e = trace_alloc_entry(tb, 0);
	if (!e)
		return -ENOSPC;

	e->type = TRACE_TYPE_INDENT;

	return 0;
}

int trace_log_outdent(struct trace_buffer *tb)
{
	struct trace_entry *e;

	e = trace_alloc_entry(tb, 0);
	if (!e)
		return -ENOSPC;

	e->type = TRACE_TYPE_OUTDENT;

	return 0;
}

static void trace_print_header(int seq, int prefix)
{
	printf("%*s[%d]: ", prefix, "", seq);
}

static char *trace_decode_reg(int reg)
{
	switch (reg) {
		case 769: return "SPRN_MMCR2"; break;
		case 770: return "SPRN_MMCRA"; break;
		case 779: return "SPRN_MMCR0"; break;
		case 804: return "SPRN_EBBHR"; break;
		case 805: return "SPRN_EBBRR"; break;
		case 806: return "SPRN_BESCR"; break;
		case 800: return "SPRN_BESCRS"; break;
		case 801: return "SPRN_BESCRSU"; break;
		case 802: return "SPRN_BESCRR"; break;
		case 803: return "SPRN_BESCRRU"; break;
		case 771: return "SPRN_PMC1"; break;
		case 772: return "SPRN_PMC2"; break;
		case 773: return "SPRN_PMC3"; break;
		case 774: return "SPRN_PMC4"; break;
		case 775: return "SPRN_PMC5"; break;
		case 776: return "SPRN_PMC6"; break;
		case 780: return "SPRN_SIAR"; break;
		case 781: return "SPRN_SDAR"; break;
		case 768: return "SPRN_SIER"; break;
	}

	return NULL;
}

static void trace_print_reg(struct trace_entry *e)
{
	u64 *p, *reg, *value;
	char *name;

	p = (u64 *)e->data;
	reg = p++;
	value = p;

	name = trace_decode_reg(*reg);
	if (name)
		printf("register %-10s = 0x%016llx\n", name, *value);
	else
		printf("register %lld = 0x%016llx\n", *reg, *value);
}

static void trace_print_counter(struct trace_entry *e)
{
	u64 *value;

	value = (u64 *)e->data;
	printf("counter = %lld\n", *value);
}

static void trace_print_string(struct trace_entry *e)
{
	char *str;

	str = (char *)e->data;
	puts(str);
}

#define BASE_PREFIX	2
#define PREFIX_DELTA	8

static void trace_print_entry(struct trace_entry *e, int seq, int *prefix)
{
	switch (e->type) {
	case TRACE_TYPE_REG:
		trace_print_header(seq, *prefix);
		trace_print_reg(e);
		break;
	case TRACE_TYPE_COUNTER:
		trace_print_header(seq, *prefix);
		trace_print_counter(e);
		break;
	case TRACE_TYPE_STRING:
		trace_print_header(seq, *prefix);
		trace_print_string(e);
		break;
	case TRACE_TYPE_INDENT:
		trace_print_header(seq, *prefix);
		puts("{");
		*prefix += PREFIX_DELTA;
		break;
	case TRACE_TYPE_OUTDENT:
		*prefix -= PREFIX_DELTA;
		if (*prefix < BASE_PREFIX)
			*prefix = BASE_PREFIX;
		trace_print_header(seq, *prefix);
		puts("}");
		break;
	default:
		trace_print_header(seq, *prefix);
		printf("entry @ %p type %d\n", e, e->type);
		break;
	}
}

void trace_buffer_print(struct trace_buffer *tb)
{
	struct trace_entry *e;
	int i, prefix;
	void *p;

	printf("Trace buffer dump:\n");
	printf("  address  %p \n", tb);
	printf("  tail     %p\n", tb->tail);
	printf("  size     %llu\n", tb->size);
	printf("  overflow %s\n", tb->overflow ? "TRUE" : "false");
	printf("  Content:\n");

	p = tb->data;

	i = 0;
	prefix = BASE_PREFIX;

	while (trace_check_bounds(tb, p) && p < tb->tail) {
		e = p;

		trace_print_entry(e, i, &prefix);

		i++;
		p = (void *)e + sizeof(*e) + e->length;
	}
}

void trace_print_location(struct trace_buffer *tb)
{
	printf("Trace buffer 0x%llx bytes @ %p\n", tb->size, tb);
}
