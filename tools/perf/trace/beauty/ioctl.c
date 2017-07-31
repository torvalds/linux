/*
 * trace/beauty/ioctl.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>

/*
 * FIXME: to support all arches we have to improve this, for
 * now, to build on older systems without things like TIOCGEXCL,
 * get it directly from our copy.
 *
 * Right now only x86 is being supported for beautifying ioctl args
 * in 'perf trace', see tools/perf/trace/beauty/Build and builtin-trace.c
 */
#include <uapi/asm-generic/ioctls.h>

static size_t ioctl__scnprintf_tty_cmd(int nr, char *bf, size_t size)
{
	static const char *ioctl_tty_cmd[] = {
	"TCGETS", "TCSETS", "TCSETSW", "TCSETSF", "TCGETA", "TCSETA", "TCSETAW",
	"TCSETAF", "TCSBRK", "TCXONC", "TCFLSH", "TIOCEXCL", "TIOCNXCL", "TIOCSCTTY",
	"TIOCGPGRP", "TIOCSPGRP", "TIOCOUTQ", "TIOCSTI", "TIOCGWINSZ", "TIOCSWINSZ",
	"TIOCMGET", "TIOCMBIS", "TIOCMBIC", "TIOCMSET", "TIOCGSOFTCAR", "TIOCSSOFTCAR",
	"FIONREAD", "TIOCLINUX", "TIOCCONS", "TIOCGSERIAL", "TIOCSSERIAL", "TIOCPKT",
	"FIONBIO", "TIOCNOTTY", "TIOCSETD", "TIOCGETD", "TCSBRKP",
	[_IOC_NR(TIOCSBRK)] = "TIOCSBRK", "TIOCCBRK", "TIOCGSID", "TCGETS2", "TCSETS2",
	"TCSETSW2", "TCSETSF2", "TIOCGRS48", "TIOCSRS485", "TIOCGPTN", "TIOCSPTLCK",
	"TIOCGDEV", "TCSETX", "TCSETXF", "TCSETXW", "TIOCSIG", "TIOCVHANGUP", "TIOCGPKT",
	"TIOCGPTLCK", [_IOC_NR(TIOCGEXCL)] = "TIOCGEXCL", "TIOCGPTPEER",
	[_IOC_NR(FIONCLEX)] = "FIONCLEX", "FIOCLEX", "FIOASYNC", "TIOCSERCONFIG",
	"TIOCSERGWILD", "TIOCSERSWILD", "TIOCGLCKTRMIOS", "TIOCSLCKTRMIOS",
	"TIOCSERGSTRUCT", "TIOCSERGETLSR", "TIOCSERGETMULTI", "TIOCSERSETMULTI",
	"TIOCMIWAIT", "TIOCGICOUNT", };
	static DEFINE_STRARRAY(ioctl_tty_cmd);

	if (nr < strarray__ioctl_tty_cmd.nr_entries && strarray__ioctl_tty_cmd.entries[nr] != NULL)
		return scnprintf(bf, size, "%s", strarray__ioctl_tty_cmd.entries[nr]);

	return scnprintf(bf, size, "(%#x, %#x)", 'T', nr);
}

static size_t ioctl__scnprintf_drm_cmd(int nr, char *bf, size_t size)
{
#include "trace/beauty/generated/ioctl/drm_ioctl_array.c"
	static DEFINE_STRARRAY(drm_ioctl_cmds);

	if (nr < strarray__drm_ioctl_cmds.nr_entries && strarray__drm_ioctl_cmds.entries[nr] != NULL)
		return scnprintf(bf, size, "DRM_%s", strarray__drm_ioctl_cmds.entries[nr]);

	return scnprintf(bf, size, "(%#x, %#x)", 'd', nr);
}

static size_t ioctl__scnprintf_cmd(unsigned long cmd, char *bf, size_t size)
{
	int dir	 = _IOC_DIR(cmd),
	    type = _IOC_TYPE(cmd),
	    nr	 = _IOC_NR(cmd),
	    sz	 = _IOC_SIZE(cmd);
	int printed = 0;
	static const struct ioctl_type {
		int	type;
		size_t	(*scnprintf)(int nr, char *bf, size_t size);
	} ioctl_types[] = { /* Must be ordered by type */
			      { .type	= 'T', .scnprintf = ioctl__scnprintf_tty_cmd, },
		['d' - 'T'] = { .type	= 'd', .scnprintf = ioctl__scnprintf_drm_cmd, }
	};
	const int nr_types = ARRAY_SIZE(ioctl_types);

	if (type >= ioctl_types[0].type && type <= ioctl_types[nr_types - 1].type) {
		const int index = type - ioctl_types[0].type;

		if (ioctl_types[index].scnprintf != NULL)
			return ioctl_types[index].scnprintf(nr, bf, size);
	}

	printed += scnprintf(bf + printed, size - printed, "%c", '(');

	if (dir == _IOC_NONE) {
		printed += scnprintf(bf + printed, size - printed, "%s", "NONE");
	} else {
		if (dir & _IOC_READ)
			printed += scnprintf(bf + printed, size - printed, "%s", "READ");
		if (dir & _IOC_WRITE)
			printed += scnprintf(bf + printed, size - printed, "%s%s", dir & _IOC_READ ? "|" : "", "WRITE");
	}

	return printed + scnprintf(bf + printed, size - printed, ", %#x, %#x, %#x)", type, nr, sz);
}

size_t syscall_arg__scnprintf_ioctl_cmd(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long cmd = arg->val;

	return ioctl__scnprintf_cmd(cmd, bf, size);
}
