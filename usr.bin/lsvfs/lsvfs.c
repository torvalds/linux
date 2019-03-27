/*
 * lsvfs - list loaded VFSes
 * Garrett A. Wollman, September 1994
 * This file is in the public domain.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMT	"%-32.32s 0x%08x %5d  %s\n"
#define HDRFMT	"%-32.32s %10s %5.5s  %s\n"
#define DASHES	"-------------------------------- "	\
		"---------- -----  ---------------\n"

static struct flaglist {
	int		flag;
	const char	str[32]; /* must be longer than the longest one. */
} fl[] = {
	{ .flag = VFCF_STATIC, .str = "static", },
	{ .flag = VFCF_NETWORK, .str = "network", },
	{ .flag = VFCF_READONLY, .str = "read-only", },
	{ .flag = VFCF_SYNTHETIC, .str = "synthetic", },
	{ .flag = VFCF_LOOPBACK, .str = "loopback", },
	{ .flag = VFCF_UNICODE, .str = "unicode", },
	{ .flag = VFCF_JAIL, .str = "jail", },
	{ .flag = VFCF_DELEGADMIN, .str = "delegated-administration", },
};

static const char *fmt_flags(int);

int
main(int argc, char **argv)
{
  int cnt, rv = 0, i; 
  struct xvfsconf vfc, *xvfsp;
  size_t buflen;
  argc--, argv++;

  printf(HDRFMT, "Filesystem", "Num", "Refs", "Flags");
  fputs(DASHES, stdout);

  if(argc) {
    for(; argc; argc--, argv++) {
      if (getvfsbyname(*argv, &vfc) == 0) {
        printf(FMT, vfc.vfc_name, vfc.vfc_typenum, vfc.vfc_refcount,
	    fmt_flags(vfc.vfc_flags));
      } else {
	warnx("VFS %s unknown or not loaded", *argv);
        rv++;
      }
    }
  } else {
    if (sysctlbyname("vfs.conflist", NULL, &buflen, NULL, 0) < 0)
      err(1, "sysctl(vfs.conflist)");
    xvfsp = malloc(buflen);
    if (xvfsp == NULL)
      errx(1, "malloc failed");
    if (sysctlbyname("vfs.conflist", xvfsp, &buflen, NULL, 0) < 0)
      err(1, "sysctl(vfs.conflist)");
    cnt = buflen / sizeof(struct xvfsconf);

    for (i = 0; i < cnt; i++) {
      printf(FMT, xvfsp[i].vfc_name, xvfsp[i].vfc_typenum,
	    xvfsp[i].vfc_refcount, fmt_flags(xvfsp[i].vfc_flags));
    }
    free(xvfsp);
  }

  return rv;
}

static const char *
fmt_flags(int flags)
{
	static char buf[sizeof(struct flaglist) * sizeof(fl)];
	int i;

	buf[0] = '\0';
	for (i = 0; i < (int)nitems(fl); i++)
		if (flags & fl[i].flag) {
			strlcat(buf, fl[i].str, sizeof(buf));
			strlcat(buf, ", ", sizeof(buf));
		}
	if (buf[0] != '\0')
		buf[strlen(buf) - 2] = '\0';
	return (buf);
}
