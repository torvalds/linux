/* mpi-mod.c -  Modular reduction
 * Copyright (C) 1998, 1999, 2001, 2002, 2003,
 *               2007  Free Software Foundation, Inc.
 *
 * This file is part of Libgcrypt.
 */

#include "mpi-internal.h"

int mpi_mod(MPI rem, MPI dividend, MPI divisor)
{
	return mpi_fdiv_r(rem, dividend, divisor);
}
