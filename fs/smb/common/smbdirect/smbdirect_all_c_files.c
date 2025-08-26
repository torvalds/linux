// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (c) 2025, Stefan Metzmacher
 */

/*
 * This is a temporary solution in order
 * to include the common smbdirect functions
 * into .c files in order to make a transformation
 * in tiny bisectable steps possible.
 *
 * It will be replaced by a smbdirect.ko with
 * exported public functions at the end.
 */
#ifndef SMBDIRECT_USE_INLINE_C_FILES
#error SMBDIRECT_USE_INLINE_C_FILES define needed
#endif
#include "smbdirect_socket.c"
#include "smbdirect_connection.c"
