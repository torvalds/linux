#-
# Copyright (c) 1998 Doug Rabson
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
# from: src/sys/kern/bus_if.m,v 1.21 2002/04/21 11:16:10 markm Exp
# $FreeBSD$
#

#include <sys/bus.h>
#include <sys/cpuset.h>
#include <machine/frame.h>

INTERFACE pic;

CODE {
	static pic_translate_code_t pic_translate_code_default;

	static void pic_translate_code_default(device_t dev, u_int irq,
	    int code, enum intr_trigger *trig, enum intr_polarity *pol)
	{
		*trig = INTR_TRIGGER_CONFORM;
		*pol = INTR_POLARITY_CONFORM;
	}
};

METHOD void bind {
	device_t	dev;
	u_int		irq;
	cpuset_t	cpumask;
	void		**priv;
};

METHOD void translate_code {
	device_t	dev;
	u_int		irq;
	int		code;
	enum intr_trigger *trig;
	enum intr_polarity *pol;
} DEFAULT pic_translate_code_default;

METHOD void config {
	device_t	dev;
	u_int		irq;
	enum intr_trigger trig;
	enum intr_polarity pol;
};

METHOD void dispatch {
	device_t	dev;
	struct trapframe *tf;
};

METHOD void enable {
	device_t	dev;
	u_int		irq;
	u_int		vector;
	void		**priv;
};

METHOD void eoi {
	device_t	dev;
	u_int		irq;
	void		*priv;
};

METHOD void ipi {
	device_t	dev;
	u_int		cpu;
};

METHOD void mask {
	device_t	dev;
	u_int		irq;
	void		*priv;
};

METHOD void unmask {
	device_t	dev;
	u_int		irq;
	void		*priv;
};

