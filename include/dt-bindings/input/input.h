/*
 * This header provides constants for most input bindings.
 *
 * Most input bindings include key code, matrix key code format.
 * In most cases, key code and matrix key code format uses
 * the standard values/macro defined in this header.
 */

#ifndef _DT_BINDINGS_INPUT_INPUT_H
#define _DT_BINDINGS_INPUT_INPUT_H

#include "linux-event-codes.h"

#define MATRIX_KEY(row, col, code)	\
	((((row) & 0xFF) << 24) | (((col) & 0xFF) << 16) | ((code) & 0xFFFF))

#endif /* _DT_BINDINGS_INPUT_INPUT_H */
