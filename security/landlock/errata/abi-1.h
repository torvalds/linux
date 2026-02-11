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
