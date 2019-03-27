/*-
 * Copyright (c) 1996 by
 * Sean Eric Fagan <sef@kithrup.com>
 * David Nugent <davidn@blaze.net.au>
 * All rights reserved.
 *
 * Portions copyright (c) 1995,1997 by
 * Berkeley Software Design, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. This work was done expressly for inclusion into FreeBSD.  Other use
 *    is permitted provided this notation is included.
 * 4. Absolutely no warranty of function or purpose is made by the authors.
 * 5. Modifications may be freely made to this file providing the above
 *    conditions are met.
 *
 * Low-level routines relating to the user capabilities database
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <login_cap.h>
#include <paths.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>


/*
 * auth_checknologin()
 * Checks for the existence of a nologin file in the login_cap
 * capability <lc>.  If there isn't one specified, then it checks
 * to see if this class should just ignore nologin files.  Lastly,
 * it tries to print out the default nologin file, and, if such
 * exists, it exits.
 */

void
auth_checknologin(login_cap_t *lc)
{
  const char *file;

  /* Do we ignore a nologin file? */
  if (login_getcapbool(lc, "ignorenologin", 0))
    return;

  /* Note that <file> will be "" if there is no nologin capability */
  if ((file = login_getcapstr(lc, "nologin", "", NULL)) == NULL)
    exit(1);

  /*
   * *file is true IFF there was a "nologin" capability
   * Note that auth_cat() returns 1 only if the specified
   * file exists, and is readable.  E.g., /.nologin exists.
   */
  if ((*file && auth_cat(file)) || auth_cat(_PATH_NOLOGIN))
    exit(1);
}


/*
 * auth_cat()
 * Checks for the readability of <file>; if it can be opened for
 * reading, it prints it out to stdout, and then exits.  Otherwise,
 * it returns 0 (meaning no nologin file).
 */

int
auth_cat(const char *file)
{
  int fd, count;
  char buf[BUFSIZ];

  if ((fd = open(file, O_RDONLY | O_CLOEXEC)) < 0)
    return 0;
  while ((count = read(fd, buf, sizeof(buf))) > 0)
    (void)write(fileno(stdout), buf, count);
  close(fd);
  sleep(5);	/* wait an arbitrary time to drain */
  return 1;
}
