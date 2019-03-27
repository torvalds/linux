/*-
 * This file is in the public domain
 *
 * $FreeBSD$
 */

/*
 * 6 digits of version.  The most significant are branch indicators at the
 * time when the last incompatible change was made (which is why it is
 * presently 6 on 7-current).  The least significant digits are incremented
 * as described below.  The format is similar to the __FreeBSD_version, but
 * not tied to it.
 *
 * DO NOT CASUALLY BUMP THIS NUMBER!  The rules are not the same as shared
 * libs or param.h/osreldate.
 *
 * It is the version number of the protocol between config(8) and the
 * sys/conf/ Makefiles (the kernel build system).
 *
 * It is now also used to trap certain problems that the syntax parser cannot
 * detect.
 *
 * Unfortunately, there is no version number for user supplied config files.
 *
 * Once, config(8) used to silently report errors and continue anyway.  This
 * was a huge problem for 'make buildkernel' which was run with the installed
 * /usr/sbin/config, not a cross built one.  We started bumping the version
 * number as a way to trap cases where the previous installworld was not
 * compatible with the new buildkernel.  The buildtools phase and much more
 * comprehensive error code returns solved this original problem.
 *
 * Most end-users will use buildkernel and the build tools from buildworld.
 * The people that are inconvenienced by gratuitous bumps are developers
 * who run config by hand.  However, developers shouldn't gratuitously be
 * inconvenienced.
 *
 * One should bump the CONFIGVERS in the following ways:
 *
 * (1) If you change config such that it won't read old config files,
 *     then bump the major number.  You shouldn't be doing this unless
 *     you are overhauling config.  Do not casually bump this number
 *     and by implication do not make changes that would force a bump
 *     of this number casually.  You should limit major bumps to once
 *     per branch.
 * (2) For each new feature added, bump the minor version of this file.
 *     When a new feature is actually used by the build system, update the
 *     %VERSREQ field in the Makefile.$ARCH of all the affected makefiles
 *     (typically all of them).
 *
 * $FreeBSD$
 */
#define	CONFIGVERS	600016
#define	MAJOR_VERS(x)	((x) / 100000)

/* Last config(8) version to require envmode/hintmode */
#define	CONFIGVERS_ENVMODE_REQ	600015
