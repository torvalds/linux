#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/sysctl.h>
#include <sys/errno.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "gprof.h"

/* Things which get -E excluded by default. */
static char	*excludes[] = { ".mcount", "_mcleanup", NULL };

int
kernel_getnfile(const char *unused __unused, char ***defaultEs)
{
	char *namelist;
	size_t len;
	char *name;

	if (sysctlbyname("kern.function_list", NULL, &len, NULL, 0) == -1)
		err(1, "sysctlbyname: function_list size");
	for (;;) {
		namelist = malloc(len);
		if (namelist == NULL)
			err(1, "malloc");
		if (sysctlbyname("kern.function_list", namelist, &len, NULL,
		   0) == 0)
			break;
		if (errno == ENOMEM)
			free(namelist);
		else
			err(1, "sysctlbyname: function_list");
	}
	nname = 0;
	for (name = namelist; *name != '\0'; name += strlen(name) + 1)
		nname++;
	/* Allocate memory for them, plus a terminating entry. */
	if ((nl = (nltype *)calloc(nname + 1, sizeof(nltype))) == NULL)
		errx(1, "Insufficient memory for symbol table");
	npe = nl;
	for (name = namelist; *name != '\0'; name += strlen(name) + 1) {
		struct kld_sym_lookup ksl;

		ksl.version = sizeof(ksl);
		ksl.symname = name;
		if (kldsym(0, KLDSYM_LOOKUP, &ksl))
			err(1, "kldsym(%s)", name);
		/* aflag not supported */
		if (uflag && strchr(name, '.') != NULL)
			continue;
		npe->value = ksl.symvalue;
		npe->name = name;
		npe++;
	}
	npe->value = -1;

	*defaultEs = excludes;
	return (0);
}
