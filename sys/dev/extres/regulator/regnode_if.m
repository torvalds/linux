#-
# Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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

INTERFACE regnode;
HEADER {
	struct regnode;
}

CODE {
	static int
	regnode_default_stop(struct regnode *regnode, int *udelay)
	{

		return(REGNODE_ENABLE(regnode, false, udelay));
	}
}

#
# Initialize regulator
# Returns 0 on success or a standard errno value.
#
METHOD int init {
	struct regnode	*regnode;
};

#
# Enable/disable regulator
# Returns 0 on success or a standard errno value.
#  - enable - input.
#  - delay - output, delay needed to stabilize voltage (in us)
#
METHOD int enable {
	struct regnode	*regnode;
	bool		enable;
	int		*udelay;
};

#
# Get regulator status
# Returns 0 on success or a standard errno value.
#
METHOD int status {
	struct regnode	*regnode;
	int		*status;	/* REGULATOR_STATUS_* */
};

#
# Set regulator voltage
# Returns 0 on success or a standard errno value.
#  - min_uvolt, max_uvolt - input, requested voltage range (in uV)
#  - delay - output, delay needed to stabilize voltage (in us)
METHOD int set_voltage {
	struct regnode	*regnode;
	int		min_uvolt;
	int		max_uvolt;
	int		*udelay;
};

#
# Get regulator voltage
# Returns 0 on success or a standard errno value.
#
METHOD int get_voltage {
	struct regnode	*regnode;
	int		*uvolt;
};

#
# Stop (shutdown) regulator
# Returns 0 on success or a standard errno value.
#
METHOD int stop {
	struct regnode	*regnode;
	int		*udelay;
} DEFAULT regnode_default_stop;
