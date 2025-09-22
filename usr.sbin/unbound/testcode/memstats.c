/*
 * testcode/memstats.c - debug tool to show memory allocation statistics.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This program reads a log file and prints the memory allocation summed
 * up.
 */
#include "config.h"
#include "util/log.h"
#include "util/rbtree.h"
#include "util/locks.h"
#include "util/fptr_wlist.h"
#include <sys/stat.h>

/**
 * The allocation statistics block
 */
struct codeline {
	/** rbtree node */
	rbnode_type node;
	/** the name of the file:linenumber */
	char* codeline;
	/** the name of the function */
	char* func;
	/** number of bytes allocated */
	uint64_t alloc;
	/** number of bytes freed */
	uint64_t free;
	/** number allocations and frees */
	uint64_t calls;
};

/** print usage and exit */
static void
usage(void)
{
	printf("usage:	memstats <logfile>\n");
	printf("statistics are printed on stdout.\n");
	exit(1);
}

/** match logfile line to see if it needs accounting processing */
static int
match(char* line)
{
	/* f.e.:
	 * [1187340064] unbound[24604:0] info: ul/rb.c:81 r_create malloc(12)
	 * 0123456789 123456789 123456789 123456789
	 * But now also:
	 * Sep 16 15:18:20 unbound[1:0] info: ul/nh.c:143 memdup malloc(11)
	 */
	if(strlen(line) < 32) /* up to 'info: ' */
		return 0;
	if(!strstr(line, " info: "))
		return 0;
	if(strstr(line, "info: stat "))
		return 0; /* skip the hex dumps */
	if(strstr(line+30, "malloc("))
		return 1;
	else if(strstr(line+30, "calloc("))
		return 1;
	/* skip reallocs */
	return 0;
}

/** find or alloc codeline in tree */
static struct codeline*
get_codeline(rbtree_type* tree, char* key, char* func)
{
	struct codeline* cl = (struct codeline*)rbtree_search(tree, key);
	if(!cl) {
		cl = calloc(1, sizeof(*cl));
		if(!cl) return 0;
		cl->codeline = strdup(key);
		if(!cl->codeline) {
			free(cl);
			return 0;
		}
		cl->func = strdup(func);
		if(!cl->func) {
			free(cl->codeline);
			free(cl);
			return 0;
		}
		cl->alloc = 0;
		cl->node.key = cl->codeline;
		(void)rbtree_insert(tree, &cl->node);
	}
	return cl;
}

/** read up the malloc stats */
static void
read_malloc_stat(char* line, rbtree_type* tree)
{
	char codeline[10240];
	char name[10240];
	int skip = 0;
	long num = 0;
	struct codeline* cl = 0;
	line = strstr(line, "info: ")+6;
	if(sscanf(line, "%s %s %n", codeline, name, &skip) != 2) {
		printf("%s\n", line);
		fatal_exit("unhandled malloc");
	}
	if(sscanf(line+skip+7, "%ld", &num) != 1) {
		printf("%s\n%s\n", line, line+skip+7);
		fatal_exit("unhandled malloc");
	}
	cl = get_codeline(tree, codeline, name);
	if(!cl)
		fatal_exit("alloc failure");
	cl->alloc += num;
	cl->calls ++;
}

/** read up the calloc stats */
static void
read_calloc_stat(char* line, rbtree_type* tree)
{
	char codeline[10240];
	char name[10240];
	int skip = 0;
	long num = 0, sz = 0;
	struct codeline* cl = 0;
	line = strstr(line, "info: ")+6;
	if(sscanf(line, "%s %s %n", codeline, name, &skip) != 2) {
		printf("%s\n", line);
		fatal_exit("unhandled calloc");
	}
	if(sscanf(line+skip+7, "%ld, %ld", &num, &sz) != 2) {
		printf("%s\n%s\n", line, line+skip+7);
		fatal_exit("unhandled calloc");
	}

	cl = get_codeline(tree, codeline, name);
	if(!cl)
		fatal_exit("alloc failure");
	cl->alloc += num*sz;
	cl->calls ++;
}

/** get size of file */
static off_t
get_file_size(const char* fname)
{
	struct stat s;
	if(stat(fname, &s) < 0) {
		fatal_exit("could not stat %s: %s", fname, strerror(errno));
	}
	return s.st_size;
}

/** read the logfile */
static void
readfile(rbtree_type* tree, const char* fname)
{
	off_t total = get_file_size(fname);
	off_t done = (off_t)0;
	int report = 0;
	FILE* in = fopen(fname, "r");
	char buf[102400];
	if(!in)
		fatal_exit("could not open %s: %s", fname, strerror(errno));
	printf("Reading %s of size " ARG_LL "d\n", fname, (long long)total);
	while(fgets(buf, 102400, in)) {
		buf[102400-1] = 0;
		done += (off_t)strlen(buf);
		/* progress count */
		if((int)(((double)done / (double)total)*100.) > report) {
			report = (int)(((double)done / (double)total)*100.);
			fprintf(stderr, " %d%%", report);
		}

		if(!match(buf))
			continue;
		else if(strstr(buf+30, "malloc("))
			read_malloc_stat(buf, tree);
		else if(strstr(buf+30, "calloc("))
			read_calloc_stat(buf, tree);
		else {
			printf("%s\n", buf);
			fatal_exit("unhandled input");
		}
	}
	fprintf(stderr, " done\n");
	fclose(in);
}

/** print memory stats */
static void
printstats(rbtree_type* tree)
{
	struct codeline* cl;
	uint64_t total = 0, tcalls = 0;
	RBTREE_FOR(cl, struct codeline*, tree) {
		printf("%12lld / %8lld in %s %s\n", (long long)cl->alloc, 
			(long long)cl->calls, cl->codeline, cl->func);
		total += cl->alloc;
		tcalls += cl->calls;
	}
	printf("------------\n");
	printf("%12lld / %8lld total in %ld code lines\n", (long long)total, 
		(long long)tcalls, (long)tree->count);
	printf("\n");
}

/** main program */
int main(int argc, const char* argv[])
{
	rbtree_type* tree = 0;
	log_init(NULL, 0, 0);
	if(argc != 2) {
		usage();
	}
	tree = rbtree_create(codeline_cmp);
	if(!tree)
		fatal_exit("alloc failure");
	readfile(tree, argv[1]);
	printstats(tree);
	return 0;
}
