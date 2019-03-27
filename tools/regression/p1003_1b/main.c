/* $FreeBSD$ */

#include <sys/param.h>

#include <stdio.h>
#include <string.h>

int fifo(int argc, char *argv[]);
int memlock(int argc, char *argv[]);
int p26(int argc, char *argv[]);
int sched(int argc, char *argv[]);
int yield(int argc, char *argv[]);

static struct {
	const char *t;
	int (*f)(int, char *[]);
	int works;
} tab[] = {
	{ "fifo", fifo, 1 },
	{ "memlock", memlock, 0 },
	{ "p26", p26, 1 },
	{ "sched", sched, 1 },
	{ "yield", yield, 1 },
};

static int usage(int argc, char *argv[])
{
	int i;
	if (argc > 1)
		fprintf(stderr, "%s is unknown\n", argv[1]);

	fprintf(stderr, "usage: %s [-a] or one of [", argv[0]);
	for (i = 0; i < (sizeof(tab) / sizeof(tab[0])); i++)
		fprintf(stderr, "%s%s", (i)? " | " : "", tab[i].t);
	fprintf(stderr, "]\n");

	return -1;
}

int main(int argc, char *argv[])
{
	int i;

	if (argc == 2 && strcmp(argv[1], "-a") == 0) {
#if 1
		fprintf(stderr,
			"-a should but doesn't really work"
			" (my notes say \"because things detach\");\n"
			"meanwhile do these individual tests and look"
			" for a non-zero exit code:\n");
		for (i = 0; i < nitems(tab); i++)
			if (tab[i].works)
				fprintf(stderr, "p1003_1b %s\n", tab[i].t);
		return -1;
#else
		{
			int r;
			for (i = 0; i < nitems(tab); i++) {
				if (tab[i].works) {
					if ( (r =
					(*tab[i].f)(argc - 1, argv + 1)) ) {
						fprintf(stderr,
						"%s failed\n", tab[i].t);
						return r;
					}
				}
			}
			return 0;
		}
#endif
	}
	
	if (argc > 1) {
		for (i = 0; i < nitems(tab); i++)
			if (strcmp(tab[i].t, argv[1]) == 0)
				return (*tab[i].f)(argc - 1, argv + 1);
	}

	return usage(argc, argv);
}
