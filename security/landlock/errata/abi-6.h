/* SPDX-License-Identifier: GPL-2.0-only */

/**
 * DOC: erratum_2
 *
 * Erratum 2: Scoped signal handling
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This fix addresses an issue where signal scoping was overly restrictive,
 * preventing sandboxed threads from signaling other threads within the same
 * process if they belonged to different domains.  Because threads are not
 * security boundaries, user space might assume that any thread within the same
 * process can send signals between themselves (see :manpage:`nptl(7)` and
 * :manpage:`libpsx(3)`).  Consistent with :manpage:`ptrace(2)` behavior, direct
 * interaction between threads of the same process should always be allowed.
 * This change ensures that any thread is allowed to send signals to any other
 * thread within the same process, regardless of their domain.
 */
LANDLOCK_ERRATUM(2)
