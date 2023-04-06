// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Stubs for DSA functionality called by the core network stack.
 * These are necessary because CONFIG_NET_DSA can be a module, and built-in
 * code cannot directly call symbols exported by modules.
 */
#include <net/dsa_stubs.h>

const struct dsa_stubs *dsa_stubs;
EXPORT_SYMBOL_GPL(dsa_stubs);
