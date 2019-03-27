#-
# Copyright (c) 2004-2006 Marcel Moolenaar
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

#include <sys/param.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <dev/scc/scc_bfe.h>

# The SCC hardware interface. The core SCC code is hardware independent.
# The details of the hardware are abstracted by the SCC hardware interface.

INTERFACE scc;

# Default implementations of some methods.
CODE {
	static int
	default_enabled(struct scc_softc *this, struct scc_chan *ch)
	{
		return (1);
	}
}

# attach() - attach hardware.
# This method is called when the device is being attached. All resources
# have been allocated. The intend of this method is to setup the hardware
# for normal operation.
# The reset parameter informs the hardware driver whether a full device
# reset is allowed or not. This is important when one of the channels can
# be used as system console and a hardware reset would disrupt output.
METHOD int attach {
	struct scc_softc *this;
	int reset;
};

# enabled()
METHOD int enabled {
	struct scc_softc *this;
	struct scc_chan *chan;
} DEFAULT default_enabled;

# iclear()
METHOD int iclear {
	struct scc_softc *this;
	struct scc_chan *chan;
};

# ipend() - query SCC for pending interrupts.
# When an interrupt is signalled, the handler will call this method to find
# out which of the interrupt sources needs attention. The handler will use
# this information to dispatch service routines that deal with each of the
# interrupt sources. An advantage of this approach is that it allows multi-
# port drivers (like puc(4)) to query multiple devices concurrently and
# service them on an interrupt priority basis. If the hardware cannot provide
# the information reliably, it is free to service the interrupt and return 0,
# meaning that no attention is required.
METHOD int ipend {
	struct scc_softc *this;
}

# probe() - detect hardware.
# This method is called as part of the bus probe to make sure the
# hardware exists. This function should also set the device description
# to something that represents the hardware.
METHOD int probe {
	struct scc_softc *this;
};
