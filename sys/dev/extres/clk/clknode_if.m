#-
# Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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

INTERFACE clknode;

HEADER {
	struct clknode;
}

#
# Initialize clock node, get shanpshot of cached values
#
METHOD int init {
	struct clknode	*clk;
	device_t	dev;
};

#
# Recalculate frequency
#     req - in/out recalulated frequency
#
METHOD int recalc_freq {
	struct clknode	*clk;
	uint64_t	*freq;
};

#
# Set frequency
#     fin - parent (input)frequency.
#     fout - requested output freqency. If clock cannot change frequency,
#	    then must return new requested frequency for his parent
METHOD int set_freq {
	struct clknode	*clk;
	uint64_t	fin;
	uint64_t	*fout;
	int		flags;
	int		*done;
};

#
# Enable/disable clock
#
METHOD int set_gate {
	struct clknode	*clk;
	bool		enable;
};

#
# Set multiplexer
#
METHOD int set_mux {
	struct clknode	*clk;
	int		idx;
};
