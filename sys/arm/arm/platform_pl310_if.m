#-
# Copyright (c) 2017 Andrew Turner
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/pl310.h>
#include <machine/platformvar.h>

INTERFACE platform_pl310;

HEADER {
	struct pl310_softc;
};

CODE {
	static void platform_pl310_default_write_ctrl(platform_t plat,
	    struct pl310_softc *sc, uint32_t val)
	{
		pl310_write4(sc, PL310_CTRL, val);
	}

	static void platform_pl310_default_write_debug(platform_t plat,
	    struct pl310_softc *sc, uint32_t val)
	{
		pl310_write4(sc, PL310_DEBUG_CTRL, val);
	}
};

/**
 * Initialize the pl310, e.g. to configure the prefetch control. The following
 * write functions may have already been called so they must not rely on
 * this function.
 */
METHOD void init {
	platform_t	_plat;
	struct pl310_softc *sc;
};

/**
 * Write to the Control Register.
 */
METHOD void write_ctrl {
	platform_t	_plat;
	struct pl310_softc *sc;
	uint32_t	val;
} DEFAULT platform_pl310_default_write_ctrl;

/**
 * Write to the Debug Control Register.
 */
METHOD void write_debug {
	platform_t	_plat;
	struct pl310_softc *sc;
	uint32_t	val;
} DEFAULT platform_pl310_default_write_debug;
