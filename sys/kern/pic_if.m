#-
# Copyright (c) 2015-2016 Svatopluk Kraus
# Copyright (c) 2015-2016 Michal Meloun
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
#include <sys/cpuset.h>
#include <sys/resource.h>
#include <sys/intr.h>

INTERFACE pic;

CODE {
	static int
	dflt_pic_bind_intr(device_t dev, struct intr_irqsrc *isrc)
	{

		return (EOPNOTSUPP);
	}

	static int
	null_pic_activate_intr(device_t dev, struct intr_irqsrc *isrc,
	    struct resource *res, struct intr_map_data *data)
	{

		return (0);
	}

	static int
	null_pic_deactivate_intr(device_t dev, struct intr_irqsrc *isrc,
	    struct resource *res, struct intr_map_data *data)
	{

		return (0);
	}

	static int
	null_pic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
	    struct resource *res, struct intr_map_data *data)
	{

		return (0);
	}

	static int
	null_pic_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
	    struct resource *res, struct intr_map_data *data)
	{

		return (0);
	}

	static void
	null_pic_init_secondary(device_t dev)
	{
	}

	static void
	null_pic_ipi_send(device_t dev, cpuset_t cpus, u_int ipi)
	{
	}

	static int
	dflt_pic_ipi_setup(device_t dev, u_int ipi, struct intr_irqsrc *isrc)
	{

		return (EOPNOTSUPP);
	}
};

METHOD int activate_intr {
	device_t		dev;
	struct intr_irqsrc	*isrc;
	struct resource		*res;
	struct intr_map_data	*data;
} DEFAULT null_pic_activate_intr;

METHOD int bind_intr {
	device_t		dev;
	struct intr_irqsrc	*isrc;
} DEFAULT dflt_pic_bind_intr;

METHOD void disable_intr {
	device_t		dev;
	struct intr_irqsrc	*isrc;
};

METHOD void enable_intr {
	device_t		dev;
	struct intr_irqsrc	*isrc;
};

METHOD int map_intr {
	device_t		dev;
	struct intr_map_data	*data;
	struct intr_irqsrc	**isrcp;
};

METHOD int deactivate_intr {
	device_t		dev;
	struct intr_irqsrc	*isrc;
	struct resource		*res;
	struct intr_map_data	*data;
} DEFAULT null_pic_deactivate_intr;

METHOD int setup_intr {
	device_t		dev;
	struct intr_irqsrc	*isrc;
	struct resource		*res;
	struct intr_map_data	*data;
} DEFAULT null_pic_setup_intr;

METHOD int teardown_intr {
	device_t		dev;
	struct intr_irqsrc	*isrc;
	struct resource		*res;
	struct intr_map_data	*data;
} DEFAULT null_pic_teardown_intr;

METHOD void post_filter {
	device_t		dev;
	struct intr_irqsrc	*isrc;
};

METHOD void post_ithread {
	device_t		dev;
	struct intr_irqsrc	*isrc;
};

METHOD void pre_ithread {
	device_t		dev;
	struct intr_irqsrc	*isrc;
};

METHOD void init_secondary {
	device_t	dev;
} DEFAULT null_pic_init_secondary;

METHOD void ipi_send {
	device_t		dev;
	struct intr_irqsrc	*isrc;
	cpuset_t		cpus;
	u_int			ipi;
} DEFAULT null_pic_ipi_send;

METHOD int ipi_setup {
	device_t		dev;
	u_int			ipi;
	struct intr_irqsrc	**isrcp;
} DEFAULT dflt_pic_ipi_setup;
