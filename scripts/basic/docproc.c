/*
 *	docproc is a simple preprocessor for the template files
 *      used as placeholders for the kernel internal documentation.
 *	docproc is used for documentation-frontend and
 *      dependency-generator.
 *	The two usages have in common that they require
 *	some knowledge of the .tmpl syntax, therefore they
 *	are kept together.
 *
 *	documentation-frontend
 *		Scans the template file and call kernel-doc for
 *		all occurrences of ![EIF]file
 *		Beforehand each referenced file is scanned for
 *		any symbols that are exported via these macros:
 *			EXPORT_SYMBOL(), EXPORT_SYMBOL_GPL(), &
 *			EXPORT_SYMBOL_GPL_FUTURE()
 *		This is used to create proper -function and
 *		-nofunction arguments in calls to kernel-doc.
 *		Usage: docproc doc file.tmpl
 *
 *	dependency-generator:
 *		Scans the template file and list all files
 *		referenced in a format recognized by make.
 *		Usage:	docproc depend file.tmpl
 *		Writes dependency information to stdout
 *		in the following format:
 *		file.tmpl src.c	src2.c
 *		The filenames are obtained from the following constructs:
 *		!Efilename
 *		!Ifilename
 *		!Dfilename
 *		!Ffilename
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>

/* exitstatus is used to keep track of any failing calls to kernel-doc,
 * but execution continues. */
int exitstatus = 0;

typedef void DFL(char *);
DFL *defaultline;

typedef void FILEONLY(char * file);
FILEONLY *internalfunctions;
FILEONLY *externalfunctions;
FILEONLY *symbolsonly;

typedef void FILELINE(char * file, char * line);
FILELINE * singlefunctions;
FILELINE * entity_system;

#define MAXLINESZ     2048
#define MAXFILES      250
#define KERNELDOCPATH "scripts/"
#define KERNELDOC     "kernel-doc"
#define DOCBOOK       "-docbook"
#define FUNCTION      "-function"
#define NOFUNCTION    "-nofunction"

char *srctree;

void usage (void)
{
	fprintf(stderr, "Usage: docproc {doc|depend} file\n");
	fprintf(stderr, "Input is read from file.tmpl. Output is sent to stdout\n");
	fprintf(stderr, "doc: frontend when generating kernel documentation\n");
	fprintf(stderr, "depend: generate list of files referenced within file\n");
	fprintf(stderr, "Environment variable SRCTREE: absolute path to kernel source tree.\n");
}

/*
 * Execute kernel-doc with parameters given in svec
 */
void exec_kernel_doc(char **svec)
{
	pid_t pid;
	int ret;
	char real_filename[PATH_MAX + 1];
	/* Make sure output generated so far are flushed */
	fflush(stdout);
	switch (pid=fork()) {
		case -1:
			perror("fork");
			exit(1);
		case  0:
			memset(real_filename, 0, sizeof(real_filename));
			strncat(real_filename, srctree, PATH_MAX);
			strncat(real_filename, KERNELDOCPATH KERNELDOC,
					PATH_MAX - strlen(real_filename));
			execvp(real_filename, svec);
			fprintf(stderr, "exec ");
			perror(real_filename);
			exit(1);
		default:
			waitpid(pid, &ret ,0);
	}
	if (WIFEXITED(ret))
		exitstatus |= WEXITSTATUS(ret);
	else
		exitstatus = 0xff;
}

/* Types used to create list of all exported symbols in a number of files */
struct symbols
{
	char *name;
};

struct symfile
{
	char *filename;
	struct symbols *symbollist;
	int symbolcnt;
};

struct symfile symfilelist[MAXFILES];
int symfilecnt = 0;

void add_new_symbol(struct symfile *sym, char * symname)
{
	sym->symbollist =
          realloc(sym->symbollist, (sym->symbolcnt + 1) * sizeof(char *));
	sym->symbollist[sym->symbolcnt++].name = strdup(symname);
}

/* Add a filename to the list */
struct symfile * add_new_file(char * filename)
{
	symfilelist[symfilecnt++].filename = strdup(filename);
	return &symfilelist[symfilecnt - 1];
}

/* Check if file already are present in the list */
struct symfile * filename_exist(char * filename)
{
	int i;
	for (i=0; i < symfilecnt; i++)
		if (strcmp(symfilelist[i].filename, filename) == 0)
			return &symfilelist[i];
	return NULL;
}

/*
 * List all files referenced within the template file.
 * Files are separated by tabs.
 */
void adddep(char * file)		   { printf("\t%s", file); }
void adddep2(char * file, char * line)     { line = line; adddep(file); }
void noaction(char * line)		   { line = line; }
void noaction2(char * file, char * line)   { file = file; line = line; }

/* Echo the line without further action */
void printline(char * line)               { printf("%s", line); }

/*
 * Find all symbols in filename that are exported with EXPORT_SYMBOL &
 * EXPORT_SYMBOL_GPL (& EXPORT_SYMBOL_GPL_FUTURE implicitly).
 * All symbols located are stored in symfilelist.
 */
void find_export_symbols(char * filename)
{
	FILE * fp;
	struct symfile *sym;
	char line[MAXLINESZ];
	if (filename_exist(filename) == NULL) {
		char real_filename[PATH_MAX + 1];
		memset(real_filename, 0, sizeof(real_filename));
		strncat(real_filename, srctree, PATH_MAX);
		strncat(real_filename, filename,
				PATH_MAX - strlen(real_filename));
		sym = add_new_file(filename);
		fp = fopen(real_filename, "r");
		if (fp == NULL)
		{
			fprintf(stderr, "docproc: ");
			perror(real_filename);
			exit(1);
		}
		while (fgets(line, MAXLINESZ, fp)) {
			char *p;
			char *e;
			if (((p = strstr(line, "EXPORT_SYMBOL_GPL")) != NULL) ||
                            ((p = strstr(line, "EXPORT_SYMBOL")) != NULL)) {
				/* Skip EXPORT_SYMBOL{_GPL} */
				while (isalnum(*p) || *p == '_')
					p++;
				/* Remove parentheses & additional whitespace */
				while (isspace(*p))
					p++;
				if (*p != '(')
					continue; /* Syntax error? */
				else
					p++;
				while (isspace(*p))
					p++;
				e = p;
				while (isalnum(*e) || *e == '_')
					e++;
				*e = '\0';
				add_new_symbol(sym, p);
			}
		}
		fclose(fp);
	}
}

/*
 * Document all external or internal functions in a file.
 * Call kernel-doc with following parameters:
 * kernel-doc -docbook -nofunction function_name1 filename
 * Function names are obtained from all the src files
 * by find_export_symbols.
 * intfunc uses -nofunction
 * extfunc uses -function
 */
void docfunctions(char * filename, char * type)
{
	int i,j;
	int symcnt = 0;
	int idx = 0;
	char **vec;

	for (i=0; i <= symfilecnt; i++)
		symcnt += symfilelist[i].symbolcnt;
	vec = malloc((2 + 2 * symcnt + 2) * sizeof(char*));
	if (vec == NULL) {
		perror("docproc: ");
		exit(1);
	}
	vec[idx++] = KERNELDOC;
	vec[idx++] = DOCBOOK;
	for (i=0; i < symfilecnt; i++) {
		struct symfile * sym = &symfilelist[i];
		for (j=0; j < sym->symbolcnt; j++) {
			vec[idx++]     = type;
			vec[idx++] = sym->symbollist[j].name;
		}
	}
	vec[idx++]     = filename;
	vec[idx] = NULL;
	printf("<!-- %s -->\n", filename);
	exec_kernel_doc(vec);
	fflush(stdout);
	free(vec);
}
void intfunc(char * filename) {	docfunctions(filename, NOFUNCTION); }
void extfunc(char * filename) { docfunctions(filename, FUNCTION);   }

/*
 * Document specific function(s) in a file.
 * Call kernel-doc with the following parameters:
 * kernel-doc -docbook -function function1 [-function function2]
 */
void singfunc(char * filename, char * line)
{
	char *vec[200]; /* Enough for specific functions */
        int i, idx = 0;
        int startofsym = 1;
	vec[idx++] = KERNELDOC;
	vec[idx++] = DOCBOOK;

        /* Split line up in individual parameters preceded by FUNCTION */
        for (i=0; line[i]; i++) {
                if (isspace(line[i])) {
                        line[i] = '\0';
                        startofsym = 1;
                        continue;
                }
                if (startofsym) {
                        startofsym = 0;
                        vec[idx++] = FUNCTION;
                        vec[idx++] = &line[i];
                }
        }
	vec[idx++] = filename;
	vec[idx] = NULL;
	exec_kernel_doc(vec);
}

/*
 * Parse file, calling action specific functions for:
 * 1) Lines containing !E
 * 2) Lines containing !I
 * 3) Lines containing !D
 * 4) Lines containing !F
 * 5) Default lines - lines not matching the above
 */
void parse_file(FILE *infile)
{
	char line[MAXLINESZ];
	char * s;
	while (fgets(line, MAXLINESZ, infile)) {
		if (line[0] == '!') {
			s = line + 2;
			switch (line[1]) {
				case 'E':
					while (*s && !isspace(*s)) s++;
					*s = '\0';
					externalfunctions(line+2);
					break;
				case 'I':
					while (*s && !isspace(*s)) s++;
					*s = '\0';
					internalfunctions(line+2);
					break;
				case 'D':
					while (*s && !isspace(*s)) s++;
                                        *s = '\0';
                                        symbolsonly(line+2);
                                        break;
				case 'F':
					/* filename */
					while (*s && !isspace(*s)) s++;
					*s++ = '\0';
                                        /* function names */
					while (isspace(*s))
						s++;
					singlefunctions(line +2, s);
					break;
				default:
					defaultline(line);
			}
		}
		else {
			defaultline(line);
		}
	}
	fflush(stdout);
}


int main(int argc, char *argv[])
{
	FILE * infile;

	srctree = getenv("SRCTREE");
	if (!srctree)
		srctree = getcwd(NULL, 0);
	if (argc != 3) {
		usage();
		exit(1);
	}
	/* Open file, exit on error */
	infile = fopen(argv[2], "r");
        if (infile == NULL) {
                fprintf(stderr, "docproc: ");
                perror(argv[2]);
                exit(2);
        }

	if (strcmp("doc", argv[1]) == 0)
	{
		/* Need to do this in two passes.
		 * First pass is used to collect all symbols exported
		 * in the various files;
		 * Second pass generate the documentation.
		 * This is required because some functions are declared
		 * and exported in different files :-((
		 */
		/* Collect symbols */
		defaultline       = noaction;
		internalfunctions = find_export_symbols;
		externalfunctions = find_export_symbols;
		symbolsonly       = find_export_symbols;
		singlefunctions   = noaction2;
		parse_file(infile);

		/* Rewind to start from beginning of file again */
		fseek(infile, 0, SEEK_SET);
		defaultline       = printline;
		internalfunctions = intfunc;
		externalfunctions = extfunc;
		symbolsonly       = printline;
		singlefunctions   = singfunc;

		parse_file(infile);
	}
	else if (strcmp("depend", argv[1]) == 0)
	{
		/* Create first part of dependency chain
		 * file.tmpl */
		printf("%s\t", argv[2]);
		defaultline       = noaction;
		internalfunctions = adddep;
		externalfunctions = adddep;
		symbolsonly       = adddep;
		singlefunctions   = adddep2;
		parse_file(infile);
		printf("\n");
	}
	else
	{
		fprintf(stderr, "Unknown option: %s\n", argv[1]);
		exit(1);
	}
	fclose(infile);
	fflush(stdout);
	return exitstatus;
}
