/*
 * Context.xs.  XS interfaces for perf script.
 *
 * Copyright (C) 2009 Tom Zanussi <tzanussi@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "../../../perf.h"
#include "../../../util/trace-event.h"

MODULE = Perf::Trace::Context		PACKAGE = Perf::Trace::Context
PROTOTYPES: ENABLE

int
common_pc(context)
	struct scripting_context * context

int
common_flags(context)
	struct scripting_context * context

int
common_lock_depth(context)
	struct scripting_context * context

