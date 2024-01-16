/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants for most Multiplexer bindings.
 *
 * Most Multiplexer bindings specify an idle state. In most cases, the
 * multiplexer can be left as is when idle, and in some cases it can
 * disconnect the input/output and leave the multiplexer in a high
 * impedance state.
 */

#ifndef _DT_BINDINGS_MUX_MUX_H
#define _DT_BINDINGS_MUX_MUX_H

#define MUX_IDLE_AS_IS      (-1)
#define MUX_IDLE_DISCONNECT (-2)

#endif
