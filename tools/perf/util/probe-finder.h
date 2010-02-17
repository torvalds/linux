#ifndef _PROBE_FINDER_H
#define _PROBE_FINDER_H

#define MAX_PATH_LEN		 256
#define MAX_PROBE_BUFFER	1024
#define MAX_PROBES		 128

static inline int is_c_varname(const char *name)
{
	/* TODO */
	return isalpha(name[0]) || name[0] == '_';
}

struct probe_point {
	char			*event;			/* Event name */
	char			*group;			/* Event group */

	/* Inputs */
	char			*file;			/* File name */
	int			line;			/* Line number */

	char			*function;		/* Function name */
	int			offset;			/* Offset bytes */

	int			nr_args;		/* Number of arguments */
	char			**args;			/* Arguments */

	int			retprobe;		/* Return probe */

	/* Output */
	int			found;			/* Number of found probe points */
	char			*probes[MAX_PROBES];	/* Output buffers (will be allocated)*/
};

#ifndef NO_LIBDWARF
extern int find_probepoint(int fd, struct probe_point *pp);

/* Workaround for undefined _MIPS_SZLONG bug in libdwarf.h: */
#ifndef _MIPS_SZLONG
# define _MIPS_SZLONG		0
#endif

#include <dwarf.h>
#include <libdwarf.h>

struct probe_finder {
	struct probe_point	*pp;			/* Target probe point */

	/* For function searching */
	Dwarf_Addr		addr;			/* Address */
	Dwarf_Unsigned		fno;			/* File number */
	Dwarf_Unsigned		lno;			/* Line number */
	Dwarf_Off		inl_offs;		/* Inline offset */
	Dwarf_Die		cu_die;			/* Current CU */

	/* For variable searching */
	Dwarf_Addr		cu_base;		/* Current CU base address */
	Dwarf_Locdesc		fbloc;			/* Location of Current Frame Base */
	const char		*var;			/* Current variable name */
	char			*buf;			/* Current output buffer */
	int			len;			/* Length of output buffer */
};
#endif /* NO_LIBDWARF */

#endif /*_PROBE_FINDER_H */
