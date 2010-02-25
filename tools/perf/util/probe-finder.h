#ifndef _PROBE_FINDER_H
#define _PROBE_FINDER_H

#include <stdbool.h>
#include "util.h"

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

/* Line number container */
struct line_node {
	struct list_head	list;
	unsigned int		line;
};

/* Line range */
struct line_range {
	char			*file;			/* File name */
	char			*function;		/* Function name */
	unsigned int		start;			/* Start line number */
	unsigned int		end;			/* End line number */
	int			offset;			/* Start line offset */
	char			*path;			/* Real path name */
	struct list_head	line_list;		/* Visible lines */
};

#ifndef NO_DWARF_SUPPORT
extern int find_probe_point(int fd, struct probe_point *pp);
extern int find_line_range(int fd, struct line_range *lr);

#include <dwarf.h>
#include <libdw.h>

struct probe_finder {
	struct probe_point	*pp;		/* Target probe point */

	/* For function searching */
	Dwarf_Addr		addr;		/* Address */
	const char		*fname;		/* File name */
	int			lno;		/* Line number */
	Dwarf_Die		cu_die;		/* Current CU */

	/* For variable searching */
	Dwarf_Op		*fb_ops;	/* Frame base attribute */
	Dwarf_Addr		cu_base;	/* Current CU base address */
	const char		*var;		/* Current variable name */
	char			*buf;		/* Current output buffer */
	int			len;		/* Length of output buffer */
};

struct line_finder {
	struct line_range	*lr;		/* Target line range */

	const char		*fname;		/* File name */
	int			lno_s;		/* Start line number */
	int			lno_e;		/* End line number */
	Dwarf_Die		cu_die;		/* Current CU */
	int			found;
};

#endif /* NO_DWARF_SUPPORT */

#endif /*_PROBE_FINDER_H */
