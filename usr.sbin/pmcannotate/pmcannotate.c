/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Nokia Corporation
 * All rights reserved.
 *
 * This software was developed by Attilio Rao for the IPSO project under
 * contract to Nokia Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>

#include <ctype.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

/* NB: Make sure FNBUFF is as large as LNBUFF, otherwise it could overflow */
#define	FNBUFF	512
#define	LNBUFF	512

#define	TMPNAME	"pmcannotate.XXXXXX"

#define	FATAL(ptr, x ...) do {						\
	fqueue_deleteall();						\
	general_deleteall();						\
	if ((ptr) != NULL)						\
		perror(ptr);						\
	fprintf(stderr, ##x);						\
	remove(tbfl);							\
	remove(tofl);							\
	exit(EXIT_FAILURE);						\
} while (0)

#define	PERCSAMP(x)	((x) * 100 / totalsamples)

struct entry {
        TAILQ_ENTRY(entry)	en_iter;
        char		*en_name;
	uintptr_t	en_pc;
	uintptr_t	en_ostart;
	uintptr_t	en_oend;
	u_int		en_nsamples;
};

struct aggent {
	TAILQ_ENTRY(aggent)	ag_fiter;
	long		ag_offset;
	uintptr_t	ag_ostart;
	uintptr_t	ag_oend;
	char		*ag_name;
	u_int		ag_nsamples;
};

static struct aggent	*agg_create(const char *name, u_int nsamples,
			    uintptr_t start, uintptr_t end);
static void		 agg_destroy(struct aggent *agg) __unused;
static void		 asmparse(FILE *fp);
static int		 cparse(FILE *fp);
static void		 entry_acqref(struct entry *entry);
static struct entry	*entry_create(const char *name, uintptr_t pc,
			    uintptr_t start, uintptr_t end);
static void		 entry_destroy(struct entry *entry) __unused;
static void		 fqueue_compact(float th);
static void		 fqueue_deleteall(void);
static struct aggent	*fqueue_findent_by_name(const char *name);
static int		 fqueue_getall(const char *bin, char *temp, int asmf);
static int		 fqueue_insertent(struct entry *entry);
static int		 fqueue_insertgen(void);
static void		 general_deleteall(void);
static struct entry	*general_findent(uintptr_t pc);
static void		 general_insertent(struct entry *entry);
static void		 general_printasm(FILE *fp, struct aggent *agg);
static int		 general_printc(FILE *fp, struct aggent *agg);
static int		 printblock(FILE *fp, struct aggent *agg);
static void		 usage(const char *progname) __dead2;

static TAILQ_HEAD(, entry) mainlst = TAILQ_HEAD_INITIALIZER(mainlst);
static TAILQ_HEAD(, aggent) fqueue = TAILQ_HEAD_INITIALIZER(fqueue);

/*
 * Use a float value in order to automatically promote operations
 * to return a float value rather than use casts.
 */
static float totalsamples;

/*
 * Identifies a string cointaining objdump's assembly printout.
 */
static inline int
isasminline(const char *str)
{
	void *ptr;
	int nbytes;

	if (sscanf(str, " %p%n", &ptr, &nbytes) != 1)
		return (0);
	if (str[nbytes] != ':' || isspace(str[nbytes + 1]) == 0)
		return (0);
	return (1);
}

/*
 * Identifies a string containing objdump's assembly printout
 * for a new function.
 */
static inline int
newfunction(const char *str)
{
	char fname[FNBUFF];
	void *ptr;
	int nbytes;

	if (isspace(str[0]))
		return (0);
	if (sscanf(str, "%p <%[^>:]>:%n", &ptr, fname, &nbytes) != 2)
		return (0);
	return (nbytes);
}

/*
 * Create a new first-level aggregation object for a specified
 * function.
 */
static struct aggent *
agg_create(const char *name, u_int nsamples, uintptr_t start, uintptr_t end)
{
	struct aggent *agg;

	agg = calloc(1, sizeof(struct aggent));
	if (agg == NULL)
		return (NULL);
	agg->ag_name = strdup(name);
	if (agg->ag_name == NULL) {
		free(agg);
		return (NULL);
	}
	agg->ag_nsamples = nsamples;
	agg->ag_ostart = start;
	agg->ag_oend = end;
	return (agg);
}

/*
 * Destroy a first-level aggregation object for a specified
 * function.
 */
static void
agg_destroy(struct aggent *agg)
{

	free(agg->ag_name);
	free(agg);
}

/*
 * Analyze the "objdump -d" output, locate functions and start
 * printing out the assembly functions content.
 * We do not use newfunction() because we actually need the
 * function name in available form, but the heurstic used is
 * the same.
 */
static void
asmparse(FILE *fp)
{
	char buffer[LNBUFF], fname[FNBUFF];
	struct aggent *agg;
	void *ptr;

	while (fgets(buffer, LNBUFF, fp) != NULL) {
		if (isspace(buffer[0]))
			continue;
		if (sscanf(buffer, "%p <%[^>:]>:", &ptr, fname) != 2)
			continue;
		agg = fqueue_findent_by_name(fname);
		if (agg == NULL)
			continue;
		agg->ag_offset = ftell(fp);
	}

	TAILQ_FOREACH(agg, &fqueue, ag_fiter) {
		if (fseek(fp, agg->ag_offset, SEEK_SET) == -1)
			return;
		printf("Profile trace for function: %s() [%.2f%%]\n",
		    agg->ag_name, PERCSAMP(agg->ag_nsamples));
		general_printasm(fp, agg);
		printf("\n");
	}
}

/*
 * Analyze the "objdump -S" output, locate functions and start
 * printing out the C functions content.
 * We do not use newfunction() because we actually need the
 * function name in available form, but the heurstic used is
 * the same.
 * In order to maintain the printout sorted, on the first pass it
 * simply stores the file offsets in order to fastly moved later
 * (when the file is hot-cached also) when the real printout will
 * happen.
 */
static int
cparse(FILE *fp)
{
	char buffer[LNBUFF], fname[FNBUFF];
	struct aggent *agg;
	void *ptr;

	while (fgets(buffer, LNBUFF, fp) != NULL) {
		if (isspace(buffer[0]))
			continue;
		if (sscanf(buffer, "%p <%[^>:]>:", &ptr, fname) != 2)
			continue;
		agg = fqueue_findent_by_name(fname);
		if (agg == NULL)
			continue;
		agg->ag_offset = ftell(fp);
	}

	TAILQ_FOREACH(agg, &fqueue, ag_fiter) {
		if (fseek(fp, agg->ag_offset, SEEK_SET) == -1)
			return (-1);
		printf("Profile trace for function: %s() [%.2f%%]\n",
		    agg->ag_name, PERCSAMP(agg->ag_nsamples));
		if (general_printc(fp, agg) == -1)
			return (-1);
		printf("\n");
	}
	return (0);
}

/*
 * Bump the number of samples for any raw entry.
 */
static void
entry_acqref(struct entry *entry)
{

	entry->en_nsamples++;
}

/*
 * Create a new raw entry object for a specified function.
 */
static struct entry *
entry_create(const char *name, uintptr_t pc, uintptr_t start, uintptr_t end)
{
	struct entry *obj;

	obj = calloc(1, sizeof(struct entry));
	if (obj == NULL)
		return (NULL);
	obj->en_name = strdup(name);
	if (obj->en_name == NULL) {
		free(obj);
		return (NULL);
	}
	obj->en_pc = pc;
	obj->en_ostart = start;
	obj->en_oend = end;
	obj->en_nsamples = 1;
	return (obj);
}

/*
 * Destroy a raw entry object for a specified function.
 */
static void
entry_destroy(struct entry *entry)
{

	free(entry->en_name);
	free(entry);
}

/*
 * Specify a lower bound in percentage and drop from the
 * first-level aggregation queue all the objects with a
 * smaller impact.
 */
static void
fqueue_compact(float th)
{
	u_int thi;
	struct aggent *agg, *tmpagg;

	if (totalsamples == 0)
		return;

	/* Revert the percentage calculation. */
	thi = th * totalsamples / 100;
	TAILQ_FOREACH_SAFE(agg, &fqueue, ag_fiter, tmpagg)
		if (agg->ag_nsamples < thi)
			TAILQ_REMOVE(&fqueue, agg, ag_fiter);
}

/*
 * Flush the first-level aggregates queue.
 */
static void
fqueue_deleteall(void)
{
	struct aggent *agg;

	while (TAILQ_EMPTY(&fqueue) == 0) {
		agg = TAILQ_FIRST(&fqueue);
		TAILQ_REMOVE(&fqueue, agg, ag_fiter);
	}
}

/*
 * Insert a raw entry into the aggregations queue.
 * If the respective first-level aggregation object
 * does not exist create it and maintain it sorted
 * in respect of the number of samples.
 */
static int
fqueue_insertent(struct entry *entry)
{
	struct aggent *obj, *tmp;
	int found;

	found = 0;
	TAILQ_FOREACH(obj, &fqueue, ag_fiter)
		if (!strcmp(obj->ag_name, entry->en_name)) {
			found = 1;
			obj->ag_nsamples += entry->en_nsamples;
			break;
		}

	/*
	 * If the first-level aggregation object already exists,
	 * just aggregate the samples and, if needed, resort
	 * it.
	 */
	if (found) {
		TAILQ_REMOVE(&fqueue, obj, ag_fiter);
		found = 0;
		TAILQ_FOREACH(tmp, &fqueue, ag_fiter)
			if (obj->ag_nsamples > tmp->ag_nsamples) {
				found = 1;
				break;
			}
		if (found)
			TAILQ_INSERT_BEFORE(tmp, obj, ag_fiter);
		else
			TAILQ_INSERT_TAIL(&fqueue, obj, ag_fiter);
		return (0);
	}

	/*
	 * If the first-level aggregation object does not
	 * exist, create it and put in the sorted queue.
	 * If this is the first object, we need to set the
	 * head of the queue.
	 */
	obj = agg_create(entry->en_name, entry->en_nsamples, entry->en_ostart,
	    entry->en_oend);
	if (obj == NULL)
		return (-1);
	if (TAILQ_EMPTY(&fqueue) != 0) {
		TAILQ_INSERT_HEAD(&fqueue, obj, ag_fiter);
		return (0);
	}
	TAILQ_FOREACH(tmp, &fqueue, ag_fiter)
		if (obj->ag_nsamples > tmp->ag_nsamples) {
			found = 1;
			break;
		}
	if (found)
		TAILQ_INSERT_BEFORE(tmp, obj, ag_fiter);
	else
		TAILQ_INSERT_TAIL(&fqueue, obj, ag_fiter);
	return (0);
}

/*
 * Lookup a first-level aggregation object by name.
 */
static struct aggent *
fqueue_findent_by_name(const char *name)
{
	struct aggent *obj;

	TAILQ_FOREACH(obj, &fqueue, ag_fiter)
		if (!strcmp(obj->ag_name, name))
			return (obj);
	return (NULL);
}

/*
 * Return the number of object in the first-level aggregations queue.
 */
static int
fqueue_getall(const char *bin, char *temp, int asmf)
{
	char tmpf[MAXPATHLEN * 2 + 50];
	struct aggent *agg;
	uintptr_t start, end;

	if (mkstemp(temp) == -1)
		return (-1);
	TAILQ_FOREACH(agg, &fqueue, ag_fiter) {
		bzero(tmpf, sizeof(tmpf));
		start = agg->ag_ostart;
		end = agg->ag_oend;

		/*
		 * Fix-up the end address in order to show it in the objdump's
		 * trace.
		 */
		end++;
		if (asmf)
			snprintf(tmpf, sizeof(tmpf),
			    "objdump --start-address=%p "
			    "--stop-address=%p -d %s >> %s", (void *)start,
			    (void *)end, bin, temp);
		else
			snprintf(tmpf, sizeof(tmpf),
			    "objdump --start-address=%p "
			    "--stop-address=%p -S %s >> %s", (void *)start,
			    (void *)end, bin, temp);
		if (system(tmpf) != 0)
			return (-1);
	}
	return (0);
}

/*
 * Insert all the raw entries present in the general queue
 * into the first-level aggregations queue.
 */
static int
fqueue_insertgen(void)
{
	struct entry *obj;

	TAILQ_FOREACH(obj, &mainlst, en_iter)
		if (fqueue_insertent(obj) == -1)
			return (-1);
	return (0);
}

/*
 * Flush the raw entries general queue.
 */
static void
general_deleteall(void)
{
	struct entry *obj;

	while (TAILQ_EMPTY(&mainlst) == 0) {
		obj = TAILQ_FIRST(&mainlst);
		TAILQ_REMOVE(&mainlst, obj, en_iter);
	}
}

/*
 * Lookup a raw entry by the PC.
 */
static struct entry *
general_findent(uintptr_t pc)
{
	struct entry *obj;

	TAILQ_FOREACH(obj, &mainlst, en_iter)
		if (obj->en_pc == pc)
			return (obj);
	return (NULL);
}

/*
 * Insert a new raw entry in the general queue.
 */
static void
general_insertent(struct entry *entry)
{

	TAILQ_INSERT_TAIL(&mainlst, entry, en_iter);
}

/*
 * Printout the body of an "objdump -d" assembly function.
 * It does simply stops when a new function is encountered,
 * bringing back the file position in order to not mess up
 * subsequent analysis.
 * C lines and others not recognized are simply skipped.
 */
static void
general_printasm(FILE *fp, struct aggent *agg)
{
	char buffer[LNBUFF];
	struct entry *obj;
	int nbytes;
	void *ptr;

	while (fgets(buffer, LNBUFF, fp) != NULL) {
		if ((nbytes = newfunction(buffer)) != 0) {
			fseek(fp, nbytes * -1, SEEK_CUR);
			break;
		}
		if (!isasminline(buffer))
			continue;
		if (sscanf(buffer, " %p:", &ptr) != 1)
			continue;
		obj = general_findent((uintptr_t)ptr);
		if (obj == NULL)
			printf("\t| %s", buffer);
		else
			printf("%.2f%%\t| %s",
			    (float)obj->en_nsamples * 100 / agg->ag_nsamples,
			    buffer);
	}
}

/*
 * Printout the body of an "objdump -S" function.
 * It does simply stops when a new function is encountered,
 * bringing back the file position in order to not mess up
 * subsequent analysis.
 * It expect from the starting to the end to find, always, valid blocks
 * (see below for an explanation of the "block" concept).
 */
static int
general_printc(FILE *fp, struct aggent *agg)
{
	char buffer[LNBUFF];

	while (fgets(buffer, LNBUFF, fp) != NULL) {
		fseek(fp, strlen(buffer) * -1, SEEK_CUR);
		if (newfunction(buffer) != 0)
			break;
		if (printblock(fp, agg) == -1)
			return (-1);
	}
	return (0);
}

/*
 * Printout a single block inside an "objdump -S" function.
 * The block is composed of a first part in C and subsequent translation
 * in assembly.
 * This code also operates a second-level aggregation packing together
 * samples relative to PCs into a (lower bottom) block with their
 * C (higher half) counterpart.
 */
static int
printblock(FILE *fp, struct aggent *agg)
{
	char buffer[LNBUFF];
	long lstart;
	struct entry *obj;
	u_int tnsamples;
	int done, nbytes, sentinel;
	void *ptr;

	/*
	 * We expect the first thing of the block is C code, so simply give
	 * up if asm line is found.
	 */
	lstart = ftell(fp);
	sentinel = 0;
	for (;;) {
		if (fgets(buffer, LNBUFF, fp) == NULL)
			return (0);
		if (isasminline(buffer) != 0)
			break;
		sentinel = 1;
		nbytes = newfunction(buffer);
		if (nbytes != 0) {
			if (fseek(fp, nbytes * -1, SEEK_CUR) == -1)
				return (-1);
			return (0);
		}
	}

	/*
	 * If the sentinel is not set, it means it did not match any
	 * "high half" for this code so simply give up.
	 * Operates the second-level aggregation.
	 */
	tnsamples = 0;
	do {
		if (sentinel == 0)
			return (-1);
		if (sscanf(buffer, " %p:", &ptr) != 1)
			return (-1);
		obj = general_findent((uintptr_t)ptr);
		if (obj != NULL)
			tnsamples += obj->en_nsamples;
	} while (fgets(buffer, LNBUFF, fp) != NULL && isasminline(buffer) != 0);

	/* Rewind to the start of the block in order to start the printout. */
	if (fseek(fp, lstart, SEEK_SET) == -1)
		return (-1);

	/* Again the high half of the block rappresenting the C part. */
	done = 0;
	while (fgets(buffer, LNBUFF, fp) != NULL && isasminline(buffer) == 0) {
		if (tnsamples == 0 || done != 0)
			printf("\t| %s", buffer);
		else {
			done = 1;
			printf("%.2f%%\t| %s",
			    (float)tnsamples * 100 / agg->ag_nsamples, buffer);
		}
	}

	/*
	 * Again the low half of the block rappresenting the asm
	 * translation part.
	 */
	for (;;) {
		if (fgets(buffer, LNBUFF, fp) == NULL)
			return (0);
		if (isasminline(buffer) == 0)
			break;
		nbytes = newfunction(buffer);
		if (nbytes != 0) {
			if (fseek(fp, nbytes * -1, SEEK_CUR) == -1)
				return (-1);
			return (0);
		}
	}
	if (fseek(fp, strlen(buffer) * -1, SEEK_CUR) == -1)
		return (-1);
	return (0);
}

/*
 * Helper printout functions.
 */
static void
usage(const char *progname)
{

	fprintf(stderr,
	    "usage: %s [-a] [-h] [-k kfile] [-l lb] pmcraw.out binary\n",
	    progname);
	exit(EXIT_SUCCESS);
}

int
main(int argc, char *argv[])
{
	char buffer[LNBUFF], fname[FNBUFF];
	char *tbfl, *tofl, *tmpdir;
	char tmpf[MAXPATHLEN * 2 + 50];
	float limit;
	char *bin, *exec, *kfile, *ofile;
	struct entry *obj;
	FILE *gfp, *bfp;
	void *ptr, *hstart, *hend;
	uintptr_t tmppc, ostart, oend;
	int cget, asmsrc;

	exec = argv[0];
	ofile = NULL;
	bin = NULL;
	kfile = NULL;
	asmsrc = 0;
	limit = 0.5;
	while ((cget = getopt(argc, argv, "ahl:k:")) != -1)
		switch(cget) {
		case 'a':
			asmsrc = 1;
			break;
		case 'k':
			kfile = optarg;
			break;
		case 'l':
			limit = (float)atof(optarg);
			break;
		case 'h':
		case '?':
		default:
			usage(exec);
		}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		usage(exec);
	ofile = argv[0];
	bin = argv[1];

	if (access(bin, R_OK | F_OK) == -1)
		FATAL(exec, "%s: Impossible to locate the binary file\n",
		    exec);
	if (access(ofile, R_OK | F_OK) == -1)
		FATAL(exec, "%s: Impossible to locate the pmcstat file\n",
		    exec);
	if (kfile != NULL && access(kfile, R_OK | F_OK) == -1)
		FATAL(exec, "%s: Impossible to locate the kernel file\n",
		    exec);

	bzero(tmpf, sizeof(tmpf));
	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL) {
		asprintf(&tbfl, "%s/%s", _PATH_TMP, TMPNAME);
		asprintf(&tofl, "%s/%s", _PATH_TMP, TMPNAME);
	} else {
		asprintf(&tbfl, "%s/%s", tmpdir, TMPNAME);
		asprintf(&tofl, "%s/%s", tmpdir, TMPNAME);
	}
	if (tofl == NULL || tbfl == NULL)
		FATAL(exec, "%s: Cannot create tempfile templates\n",
		    exec);
	if (mkstemp(tofl) == -1)
		FATAL(exec, "%s: Impossible to create the tmp file\n",
		    exec);
	if (kfile != NULL)
		snprintf(tmpf, sizeof(tmpf), "pmcstat -k %s -R %s -m %s",
		    kfile, ofile, tofl);
	else
		snprintf(tmpf, sizeof(tmpf), "pmcstat -R %s -m %s", ofile,
		    tofl);
	if (system(tmpf) != 0)
		FATAL(exec, "%s: Impossible to create the tmp file\n",
		    exec);

	gfp = fopen(tofl, "r");
	if (gfp == NULL)
		FATAL(exec, "%s: Impossible to open the map file\n",
		    exec);

	/*
	 * Make the collection of raw entries from a pmcstat mapped file.
	 * The heuristic here wants strings in the form:
	 * "addr funcname startfaddr endfaddr".
	 */
	while (fgets(buffer, LNBUFF, gfp) != NULL) {
		if (isspace(buffer[0]))
			continue;
		if (sscanf(buffer, "%p %s %p %p\n", &ptr, fname,
		    &hstart, &hend) != 4)
			FATAL(NULL,
			    "%s: Invalid scan of function in the map file\n",
			    exec);
		ostart = (uintptr_t)hstart;
		oend = (uintptr_t)hend;
		tmppc = (uintptr_t)ptr;
		totalsamples++;
		obj = general_findent(tmppc);
		if (obj != NULL) {
			entry_acqref(obj);
			continue;
		}
		obj = entry_create(fname, tmppc, ostart, oend);
		if (obj == NULL)
			FATAL(exec,
			    "%s: Impossible to create a new object\n", exec);
		general_insertent(obj);
	}
	if (fclose(gfp) == EOF)
		FATAL(exec, "%s: Impossible to close the filedesc\n",
		    exec);
	if (remove(tofl) == -1)
                FATAL(exec, "%s: Impossible to remove the tmpfile\n",
                    exec);

	/*
	 * Remove the loose end objects and feed the first-level aggregation
	 * queue.
	 */
	if (fqueue_insertgen() == -1)
		FATAL(exec, "%s: Impossible to generate an analysis\n",
		    exec);
	fqueue_compact(limit);
	if (fqueue_getall(bin, tbfl, asmsrc) == -1)
		FATAL(exec, "%s: Impossible to create the tmp file\n",
		    exec);

	bfp = fopen(tbfl, "r");
	if (bfp == NULL)
		FATAL(exec, "%s: Impossible to open the binary file\n",
		    exec);

	if (asmsrc != 0)
		asmparse(bfp);
	else if (cparse(bfp) == -1)
		FATAL(NULL, "%s: Invalid format for the C file\n", exec);
	if (fclose(bfp) == EOF)
                FATAL(exec, "%s: Impossible to close the filedesc\n",
                    exec);
	if (remove(tbfl) == -1)
                FATAL(exec, "%s: Impossible to remove the tmpfile\n",
                    exec);
	return (0);
}
