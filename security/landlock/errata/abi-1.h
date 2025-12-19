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
 */
LANDLOCK_ERRATUM(3)
