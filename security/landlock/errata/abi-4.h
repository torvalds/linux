/* SPDX-License-Identifier: GPL-2.0-only */

/**
 * DOC: erratum_1
 *
 * Erratum 1: TCP socket identification
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This fix addresses an issue where IPv4 and IPv6 stream sockets (e.g., SMC,
 * MPTCP, or SCTP) were incorrectly restricted by TCP access rights during
 * :manpage:`bind(2)` and :manpage:`connect(2)` operations. This change ensures
 * that only TCP sockets are subject to TCP access rights, allowing other
 * protocols to operate without unnecessary restrictions.
 *
 * Impact:
 *
 * In kernels without this fix, using ``LANDLOCK_ACCESS_NET_BIND_TCP`` or
 * ``LANDLOCK_ACCESS_NET_CONNECT_TCP`` would incorrectly restrict non-TCP
 * stream protocols (SMC, MPTCP, SCTP), potentially breaking applications
 * that rely on these protocols while using Landlock network restrictions.
 */
LANDLOCK_ERRATUM(1)

/**
 * DOC: erratum_5
 *
 * Erratum 5: Denial of ``LANDLOCK_ACCESS_FS_REFER`` in layered domains
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This fix addresses an issue where ``LANDLOCK_ACCESS_FS_REFER`` was implicitly
 * handled in domain layers that handle no filesystem access rights.
 *
 * With this fix, a reparenting operation is allowed in a layered Landlock
 * domain if and only if each layer, enforced on its own, would allow it.
 *
 * Impact:
 *
 * In kernels without this fix, if a domain combined a layer that handles
 * filesystem access rights with a layer that doesn't, the resulting layered
 * domain would deny all ``LANDLOCK_ACCESS_FS_REFER`` operations, even when the
 * filesystem layer granted these operations with a rule.
 */
LANDLOCK_ERRATUM(5)
