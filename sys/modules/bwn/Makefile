# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/bwn

KMOD=	if_bwn
SRCS=	if_bwn.c if_bwnreg.h if_bwnvar.h
SRCS+=	if_bwn_pci.c
SRCS+=	if_bwn_util.c

# PHY
SRCS+=	if_bwn_phy_common.c
SRCS+=	if_bwn_phy_g.c if_bwn_phy_lp.c
SRCS+=	if_bwn_phy_n.c

# BHND
SRCS+=	bhnd_bus_if.h \
	bhnd_chipc_if.h \
	bhnd_pmu_if.h \
	bhnd_pwrctl_if.h
SRCS+=	bhnd_nvram_map.h

# BHNDB
SRCS+=	bhndb_bus_if.h \
	bhndb_if.h

# Other
SRCS+=	device_if.h bus_if.h gpio_if.h pci_if.h opt_bwn.h opt_wlan.h

# The following need the BWN_GPL_PHY kenrel option to opt-in
# to the GPL'd 802.11n PHY support for this driver.
.PATH: ${SRCTOP}/sys/gnu/dev/bwn/phy_n
SRCS.BWN_GPL_PHY+=  if_bwn_radio_2055.c
SRCS.BWN_GPL_PHY+=  if_bwn_radio_2056.c
SRCS.BWN_GPL_PHY+=  if_bwn_radio_2057.c
SRCS.BWN_GPL_PHY+=  if_bwn_phy_n_sprom.c
SRCS.BWN_GPL_PHY+=  if_bwn_phy_n_tables.c
SRCS.BWN_GPL_PHY+=  if_bwn_phy_n_ppr.c
SRCS.BWN_GPL_PHY+=  if_bwn_phy_n_core.c

.include <bsd.kmod.mk>
