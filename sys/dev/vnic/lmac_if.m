#-
# Copyright (c) 2015 The FreeBSD Foundation
# All rights reserved.
#
# This software was developed by Semihalf under
# the sponsorship of the FreeBSD Foundation.
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
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

# LMAC (BGX controller) interface description
#

INTERFACE lmac;

CODE {
	static int null_lmac_media_status(device_t dev, int lmacid, int *link,
	    int *duplex, int *speed)
	{
		return (ENXIO);
	}

	static int null_lmac_media_change(device_t dev, int lmacid, int link,
	    int duplex, int speed)
	{
		return (ENXIO);
	}

	static int null_lmac_phy_connect(device_t dev, int lmacid, int phy)
	{
		return (ENXIO);
	}

	static int null_lmac_phy_disconnect(device_t dev, int lmacid, int phy)
	{
		return (ENXIO);
	}
};

# Get link status
#
# 0 : Success
#
METHOD int media_status {
	device_t		dev;
	int			lmacid;
	int *			link;
	int *			duplex;
	int *			speed;
} DEFAULT null_lmac_media_status;

# Change link status
#
# 0 : Success
#
METHOD int media_change {
	device_t		dev;
	int			lmacid;
	int			link;
	int			duplex;
	int			speed;
} DEFAULT null_lmac_media_change;

# Connect PHY
#
# 0 : Success
#
METHOD int phy_connect {
	device_t		dev;
	int			lmacid;
	int			phy;
} DEFAULT null_lmac_phy_connect;

# Disconnect PHY
#
# 0 : Success
#
METHOD int phy_disconnect {
	device_t		dev;
	int			lmacid;
	int			phy;
} DEFAULT null_lmac_phy_disconnect;
