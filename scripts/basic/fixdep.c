/*
 * "Optimize" a list of dependencies as spit out by gcc -MD
 * for the kernel build
 * ===========================================================================
 *
 * Author       Kai Germaschewski
 * Copyright    2002 by Kai Germaschewski  <kai.germaschewski@gmx.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 *
 * Introduction:
 *
 * gcc produces a very nice and correct list of dependencies which
 * tells make when to remake a file.
 *
 * To use this list as-is however has the drawback that virtually
 * every file in the kernel includes autoconf.h.
 *
 * If the user re-runs make *config, autoconf.h will be
 * regenerated.  make notices that and will rebuild every file which
 * includes autoconf.h, i.e. basically all files. This is extremely
 * annoying if the user just changed CONFIG_HIS_DRIVER from n to m.
 *
 * So we play the same trick that "mkdep" played before. We replace
 * the dependency on autoconf.h by a dependency on every config
 * option which is mentioned in any of the listed prerequisites.
 *
 * kconfig populates a tree in include/config/ with an empty file
 * for each config symbol and when the configuration is updated
 * the files representing changed config options are touched
 * which then let make pick up the changes and the files that use
 * the config symbols are rebuilt.
 *
 * So if the user changes his CONFIG_HIS_DRIVER option, only the objects
 * which depend on "include/config/his/driver.h" will be rebuilt,
 * so most likely only his driver ;-)
 *
 * The idea above dates, by the way, back to Michael E Chastain, AFAIK.
 *
 * So to get dependencies right, there are two issues:
 * o if any of the files the compiler read changed, we need to rebuild
 * o if the command line given to the compile the file changed, we
 *   better rebuild as well.
 *
 * The former is handled by using the -MD output, the later by saving
 * the command line used to compile the old object and comparing it
 * to the one we would now use.
 *
 * Again, also this idea is pretty old and has been discussed on
 * kbuild-devel a long time ago. I don't have a sensibly working
 * internet connection right now, so I rather don't mention names
 * without double checking.
 *
 * This code here has been based partially based on mkdep.c, which
 * says the following about its history:
 *
 *   Copyright abandoned, Michael Chastain, <mailto:mec@shout.net>.
 *   This is a C version of syncdep.pl by Werner Almesberger.
 *
 *
 * It is invoked as
 *
 *   fixdep <depfile> <target> <cmdline>
 *
 * and will read the dependency file <depfile>
 *
 * The transformed dependency snipped is written to stdout.
 *
 * It first generates a line
 *
 *   cmd_<target> = <cmdline>
 *
 * and then basically copies the .<target>.d file to stdout, in the
 * process filtering out the dependency on autoconf.h and adding
 * dependencies on include/config/my/option.h for every
 * CONFIG_MY_OPTION encountered in any of the prerequisites.
 *
 * It will also filter out all the dependencies on *.ver. We need
 * to make sure that the generated version checksum are globally up
 * to date before even starting the recursive build, so it's too late
 * at this point anyway.
 *
 * We don't even try to really parse the header files, but
 * merely grep, i.e. if CONFIG_FOO is mentioned in a comment, it will
 * be picked up as well. It's not a problem with respect to
 * correctness, since that can only give too many dependencies, thus
 * we cannot miss a rebuild. Since people tend to not mention totally
 * unrelated CONFIG_ options all over the place, it's not an
 * efficiency problem either.
 *
 * (Note: it'd be easy to port over the complete mkdep state machine,
 *  but I don't think the added complexity is worth it)
 */
/*
 * Note 2: if somebody writes HELLO_CONFIG_BOOM in a file, it will depend onto
 * CONFIG_BOOM. This could seem a bug (not too hard to fix), but please do not
 * fix it! Some UserModeLinux files (look at arch/um/) call CONFIG_BOOM as
 * UML_CONFIG_BOOM, to avoid conflicts with /usr/include/linux/autoconf.h,
 * through arch/um/include/uml-config.h; this fixdep "bug" makes sure that
 * those files will have correct dependencies.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>

int insert_extra_deps;
char *target;
char *depfile;
char *cmdline;

static void usage(void)
{
	fprintf(stderr, "Usage: fixdep [-e] <depfile> <target> <cmdline>\n");
	fprintf(stderr, " -e  insert extra dependencies given on stdin\n");
	exit(1);
}

/*
 * Print out the commandline prefixed with cmd_<target filename> :=
 */
static void print_cmdline(void)
{
	printf("cmd_%s := %s\n\n", target, cmdline);
}

/*
 * Print out a dependency path from a symbol name
 */
static void print_config(const char *m, int slen)
{
	int c, i;

	printf("    $(wildcard include/config/");
	for (i = 0; i < slen; i++) {
		c = m[i];
		if (c == '_')
			c = '/';
		else
			c = tolower(c);
		putchar(c);
	}
	printf(".h) \\\n");
}

static void do_extra_deps(void)
{
	if (insert_extra_deps) {
		char buf[80];
		while(fgets(buf, sizeof(buf), stdin)) {
			int len = strlen(buf);
			if (len < 2 || buf[len-1] != '\n') {
				fprintf(stderr, "fixdep: bad data on stdin\n");
				exit(1);
			}
			print_config(buf, len-1);
		}
	}
}

struct item {
	struct item	*next;
	unsigned int	len;
	unsigned int	hash;
	char		name[0];
};

#define HASHSZ 256
static struct item *hashtab[HASHSZ];

static unsigned int strhash(const char *str, unsigned int sz)
{
	/* fnv32 hash */
	unsigned int i, hash = 2166136261U;

	for (i = 0; i < sz; i++)
		hash = (hash ^ str[i]) * 0x01000193;
	return hash;
}

/*
 * Lookup a value in the configuration string.
 */
static int is_defined_config(const char *name, int len, unsigned int hash)
{
	struct item *aux;

	for (aux = hashtab[hash % HASHSZ]; aux; aux = aux->next) {
		if (aux->hash == hash && aux->len == len &&
		    memcmp(aux->name, name, len) == 0)
			return 1;
	}
	return 0;
}

/*
 * Add a new value to the configuration string.
 */
static void define_config(const char *name, int len, unsigned int hash)
{
	struct item *aux = malloc(sizeof(*aux) + len);

	if (!aux) {
		perror("fixdep:malloc");
		exit(1);
	}
	memcpy(aux->name, name, len);
	aux->len = len;
	aux->hash = hash;
	aux->next = hashtab[hash % HASHSZ];
	hashtab[hash % HASHSZ] = aux;
}

/*
 * Record the use of a CONFIG_* word.
 */
static void use_config(const char *m, int slen)
{
	unsigned int hash = strhash(m, slen);

	if (is_defined_config(m, slen, hash))
	    return;

	define_config(m, slen, hash);
	print_config(m, slen);
}

static void parse_config_file(const char *p)
{
	const char *q, *r;

	while ((p = strstr(p, "CONFIG_"))) {
		p += 7;
		q = p;
		while (*q && (isalnum(*q) || *q == '_'))
			q++;
		if (memcmp(q - 7, "_MODULE", 7) == 0)
			r = q - 7;
		else
			r = q;
		if (r > p)
			use_config(p, r - p);
		p = q;
	}
}

/* test if s ends in sub */
static int strrcmp(const char *s, const char *sub)
{
	int slen = strlen(s);
	int sublen = strlen(sub);

	if (sublen > slen)
		return 1;

	return memcmp(s + slen - sublen, sub, sublen);
}

static void do_config_file(const char *filename)
{
	struct stat st;
	int fd;
	char *map;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fixdep: error opening config file: ");
		perror(filename);
		exit(2);
	}
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "fixdep: error fstat'ing config file: ");
		perror(filename);
		exit(2);
	}
	if (st.st_size == 0) {
		close(fd);
		return;
	}
	map = malloc(st.st_size + 1);
	if (!map) {
		perror("fixdep: malloc");
		exit(2);
	}
	if (read(fd, map, st.st_size) != st.st_size) {
		perror("fixdep: read");
		exit(2);
	}
	map[st.st_size] = '\0';
	close(fd);

	parse_config_file(map);

	free(map);
}

/*
 * Important: The below generated source_foo.o and deps_foo.o variable
 * assignments are parsed not only by make, but also by the rather simple
 * parser in scripts/mod/sumversion.c.
 */
static void parse_dep_file(void *map, size_t len)
{
	char *m = map;
	char *end = m + len;
	char *p;
	char s[PATH_MAX];
	int is_target;
	int saw_any_target = 0;
	int is_first_dep = 0;

	while (m < end) {
		/* Skip any "white space" */
		while (m < end && (*m == ' ' || *m == '\\' || *m == '\n'))
			m++;
		/* Find next "white space" */
		p = m;
		while (p < end && *p != ' ' && *p != '\\' && *p != '\n')
			p++;
		/* Is the token we found a target name? */
		is_target = (*(p-1) == ':');
		/* Don't write any target names into the dependency file */
		if (is_target) {
			/* The /next/ file is the first dependency */
			is_first_dep = 1;
		} else {
			/* Save this token/filename */
			memcpy(s, m, p-m);
			s[p - m] = 0;

			/* Ignore certain dependencies */
			if (strrcmp(s, "include/generated/autoconf.h") &&
			    strrcmp(s, "include/generated/autoksyms.h") &&
			    strrcmp(s, "arch/um/include/uml-config.h") &&
			    strrcmp(s, "include/linux/kconfig.h") &&
			    strrcmp(s, ".ver")) {
				/*
				 * Do not list the source file as dependency,
				 * so that kbuild is not confused if a .c file
				 * is rewritten into .S or vice versa. Storing
				 * it in source_* is needed for modpost to
				 * compute srcversions.
				 */
				if (is_first_dep) {
					/*
					 * If processing the concatenation of
					 * multiple dependency files, only
					 * process the first target name, which
					 * will be the original source name,
					 * and ignore any other target names,
					 * which will be intermediate temporary
					 * files.
					 */
					if (!saw_any_target) {
						saw_any_target = 1;
						printf("source_%s := %s\n\n",
							target, s);
						printf("deps_%s := \\\n",
							target);
					}
					is_first_dep = 0;
				} else
					printf("  %s \\\n", s);
				do_config_file(s);
			}
		}
		/*
		 * Start searching for next token immediately after the first
		 * "whitespace" character that follows this token.
		 */
		m = p + 1;
	}

	if (!saw_any_target) {
		fprintf(stderr, "fixdep: parse error; no targets found\n");
		exit(1);
	}

	do_extra_deps();

	printf("\n%s: $(deps_%s)\n\n", target, target);
	printf("$(deps_%s):\n", target);
}

static void print_deps(void)
{
	struct stat st;
	int fd;
	void *map;

	fd = open(depfile, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fixdep: error opening depfile: ");
		perror(depfile);
		exit(2);
	}
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "fixdep: error fstat'ing depfile: ");
		perror(depfile);
		exit(2);
	}
	if (st.st_size == 0) {
		fprintf(stderr,"fixdep: %s is empty\n",depfile);
		close(fd);
		return;
	}
	map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if ((long) map == -1) {
		perror("fixdep: mmap");
		close(fd);
		return;
	}

	parse_dep_file(map, st.st_size);

	munmap(map, st.st_size);

	close(fd);
}

int main(int argc, char *argv[])
{
	if (argc == 5 && !strcmp(argv[1], "-e")) {
		insert_extra_deps = 1;
		argv++;
	} else if (argc != 4)
		usage();

	depfile = argv[1];
	target = argv[2];
	cmdline = argv[3];

	print_cmdline();
	print_deps();

	return 0;
}
