/* SPDX-License-Identifier: GPL-2.0-only */

/**
 * DOC: erratum_3
 *
 * Erratum 3: Disconnected directory handling
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This fix addresses an issue with disconnected directories that occur when a
 * directory is moved outside the scope of a bind mount.  The change ensures
 * that evaluated access rights include both those from the disconnected file
 * hierarchy down to its filesystem root and those from the related mount point
 * hierarchy.  This prevents access right widening through rename or link
 * actions.
 *
 * Impact:
 *
 * Without this fix, it was possible to widen access rights through rename or
 * link actions involving disconnected directories, potentially bypassing
 * ``LANDLOCK_ACCESS_FS_REFER`` restrictions.  This could allow privilege
 * escalation in complex mount scenarios where directories become disconnected
 * from their original mount points.
 */
LANDLOCK_ERRATUM(3)

/**
 * DOC: erratum_4
 *
 * Erratum 4: Creation of whiteout objects
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This fix changes the access rights required for the creation of whiteout
 * objects through :manpage:`mknod(2)`, :manpage:`renameat2(2)`, or
 * :manpage:`link(2)`.  Creating whiteout objects is now guarded by
 * ``LANDLOCK_ACCESS_FS_MAKE_REG`` instead of ``LANDLOCK_ACCESS_FS_MAKE_CHAR``.
 *
 * Whiteout objects are used in OverlayFS to mark the absence of a file in an
 * upper file system.  Despite being created with ``S_IFCHR``, whiteout objects
 * do not count as character devices.
 *
 * Impact:
 *
 * Sandboxed programs that create OverlayFS whiteouts (such as fuse-overlayfs)
 * now require ``LANDLOCK_ACCESS_FS_MAKE_REG`` instead of
 * ``LANDLOCK_ACCESS_FS_MAKE_CHAR``.
 */
LANDLOCK_ERRATUM(4)
