/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_network.h"
#include "lio_ctrl.h"
#include "cn23xx_pf_device.h"
#include "lio_image.h"
#include "lio_ioctl.h"
#include "lio_main.h"
#include "lio_rxtx.h"

static int	lio_set_rx_csum(struct ifnet *ifp, uint32_t data);
static int	lio_set_tso4(struct ifnet *ifp);
static int	lio_set_tso6(struct ifnet *ifp);
static int	lio_set_lro(struct ifnet *ifp);
static int	lio_change_mtu(struct ifnet *ifp, int new_mtu);
static int	lio_set_mcast_list(struct ifnet *ifp);
static inline enum	lio_ifflags lio_get_new_flags(struct ifnet *ifp);

static inline bool
lio_is_valid_ether_addr(const uint8_t *addr)
{

	return (!(0x01 & addr[0]) && !((addr[0] + addr[1] + addr[2] + addr[3] +
					addr[4] + addr[5]) == 0x00));
}

static int
lio_change_dev_flags(struct ifnet *ifp)
{
	struct lio_ctrl_pkt	nctrl;
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	int ret = 0;

	bzero(&nctrl, sizeof(struct lio_ctrl_pkt));

	/* Create a ctrl pkt command to be sent to core app. */
	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = LIO_CMD_CHANGE_DEVFLAGS;
	nctrl.ncmd.s.param1 = lio_get_new_flags(ifp);
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;

	ret = lio_send_ctrl_pkt(oct, &nctrl);
	if (ret)
		lio_dev_err(oct, "Failed to change flags ret %d\n", ret);

	return (ret);
}

/*
 * lio_ioctl : User calls this routine for configuring
 * the interface.
 *
 * return 0 on success, positive on failure
 */
int
lio_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct lio	*lio = if_getsoftc(ifp);
	struct ifreq	*ifrequest = (struct ifreq *)data;
	int	error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		lio_dev_dbg(lio->oct_dev, "ioctl: SIOCSIFADDR\n");
		if_setflagbits(ifp, IFF_UP, 0);
		error = ether_ioctl(ifp, cmd, data);
		break;
	case SIOCSIFMTU:
		lio_dev_dbg(lio->oct_dev, "ioctl: SIOCSIFMTU\n");
		error = lio_change_mtu(ifp, ifrequest->ifr_mtu);
		break;
	case SIOCSIFFLAGS:
		lio_dev_dbg(lio->oct_dev, "ioctl: SIOCSIFFLAGS\n");
		if (if_getflags(ifp) & IFF_UP) {
			if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
				if ((if_getflags(ifp) ^ lio->if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					error = lio_change_dev_flags(ifp);
			} else {
				if (!(atomic_load_acq_int(&lio->ifstate) &
				      LIO_IFSTATE_DETACH))
					lio_open(lio);
			}
		} else {
			if (if_getdrvflags(ifp) & IFF_DRV_RUNNING)
				lio_stop(ifp);
		}
		lio->if_flags = if_getflags(ifp);
		break;
	case SIOCADDMULTI:
		lio_dev_dbg(lio->oct_dev, "ioctl: SIOCADDMULTI\n");
		if (if_getdrvflags(ifp) & IFF_DRV_RUNNING)
			error = lio_set_mcast_list(ifp);
		break;
	case SIOCDELMULTI:
		lio_dev_dbg(lio->oct_dev, "ioctl: SIOCSIFMULTI\n");
		break;
	case SIOCSIFMEDIA:
		lio_dev_dbg(lio->oct_dev, "ioctl: SIOCSIFMEDIA\n");
	case SIOCGIFMEDIA:
		lio_dev_dbg(lio->oct_dev, "ioctl: SIOCGIFMEDIA\n");
	case SIOCGIFXMEDIA:
		lio_dev_dbg(lio->oct_dev, "ioctl: SIOCGIFXMEDIA\n");
		error = ifmedia_ioctl(ifp, ifrequest, &lio->ifmedia, cmd);
		break;
	case SIOCSIFCAP:
		{
			int	features = ifrequest->ifr_reqcap ^
					if_getcapenable(ifp);

			lio_dev_dbg(lio->oct_dev, "ioctl: SIOCSIFCAP (Set Capabilities)\n");

			if (!features)
				break;

			if (features & IFCAP_TXCSUM) {
				if_togglecapenable(ifp, IFCAP_TXCSUM);
				if (if_getcapenable(ifp) & IFCAP_TXCSUM)
					if_sethwassistbits(ifp, (CSUM_TCP |
								 CSUM_UDP |
								 CSUM_IP), 0);
				else
					if_sethwassistbits(ifp, 0,
							(CSUM_TCP | CSUM_UDP |
							 CSUM_IP));
			}
			if (features & IFCAP_TXCSUM_IPV6) {
				if_togglecapenable(ifp, IFCAP_TXCSUM_IPV6);
				if (if_getcapenable(ifp) & IFCAP_TXCSUM_IPV6)
					if_sethwassistbits(ifp, (CSUM_UDP_IPV6 |
							   CSUM_TCP_IPV6), 0);
				else
					if_sethwassistbits(ifp, 0,
							   (CSUM_UDP_IPV6 |
							    CSUM_TCP_IPV6));
			}
			if (features & (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6))
				error |= lio_set_rx_csum(ifp, (features &
							       (IFCAP_RXCSUM |
							 IFCAP_RXCSUM_IPV6)));

			if (features & IFCAP_TSO4)
				error |= lio_set_tso4(ifp);

			if (features & IFCAP_TSO6)
				error |= lio_set_tso6(ifp);

			if (features & IFCAP_LRO)
				error |= lio_set_lro(ifp);

			if (features & IFCAP_VLAN_HWTAGGING)
				if_togglecapenable(ifp, IFCAP_VLAN_HWTAGGING);

			if (features & IFCAP_VLAN_HWFILTER)
				if_togglecapenable(ifp, IFCAP_VLAN_HWFILTER);

			if (features & IFCAP_VLAN_HWTSO)
				if_togglecapenable(ifp, IFCAP_VLAN_HWTSO);

			VLAN_CAPABILITIES(ifp);
			break;
		}
	default:
		lio_dev_dbg(lio->oct_dev, "ioctl: UNKNOWN (0x%X)\n", (int)cmd);
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static int
lio_set_tso4(struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);

	if (if_getcapabilities(ifp) & IFCAP_TSO4) {
		if_togglecapenable(ifp, IFCAP_TSO4);
		if (if_getcapenable(ifp) & IFCAP_TSO4)
			if_sethwassistbits(ifp, CSUM_IP_TSO, 0);
		else
			if_sethwassistbits(ifp, 0, CSUM_IP_TSO);
	} else {
		lio_dev_info(lio->oct_dev, "TSO4 capability not supported\n");
		return (EINVAL);
	}

	return (0);
}

static int
lio_set_tso6(struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);

	if (if_getcapabilities(ifp) & IFCAP_TSO6) {
		if_togglecapenable(ifp, IFCAP_TSO6);
		if (if_getcapenable(ifp) & IFCAP_TSO6)
			if_sethwassistbits(ifp, CSUM_IP6_TSO, 0);
		else
			if_sethwassistbits(ifp, 0, CSUM_IP6_TSO);
	} else {
		lio_dev_info(lio->oct_dev, "TSO6 capability not supported\n");
		return (EINVAL);
	}

	return (0);
}

static int
lio_set_rx_csum(struct ifnet *ifp, uint32_t data)
{
	struct lio	*lio = if_getsoftc(ifp);
	int	ret = 0;

	if (if_getcapabilities(ifp) & (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6)) {
		if_togglecapenable(ifp, (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6));

		if (data) {
			/* LRO requires RXCSUM */
			if ((if_getcapabilities(ifp) & IFCAP_LRO) &&
			    (if_getcapenable(ifp) & IFCAP_LRO)) {
				ret = lio_set_feature(ifp, LIO_CMD_LRO_DISABLE,
						      LIO_LROIPV4 |
						      LIO_LROIPV6);
				if_togglecapenable(ifp, IFCAP_LRO);
			}
		}
	} else {
		lio_dev_info(lio->oct_dev, "Rx checksum offload capability not supported\n");
		return (ENODEV);
	}

	return ((ret) ? EINVAL : 0);
}

static int
lio_set_lro(struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);
	int	ret = 0;

	if (!(if_getcapabilities(ifp) & IFCAP_LRO)) {
		lio_dev_info(lio->oct_dev, "LRO capability not supported\n");
		return (ENODEV);
	}

	if ((!(if_getcapenable(ifp) & IFCAP_LRO)) &&
	    (if_getcapenable(ifp) & IFCAP_RXCSUM) &&
	    (if_getcapenable(ifp) & IFCAP_RXCSUM_IPV6)) {
		if_togglecapenable(ifp, IFCAP_LRO);

		if (lio_hwlro)
			ret = lio_set_feature(ifp, LIO_CMD_LRO_ENABLE, LIO_LROIPV4 |
					      LIO_LROIPV6);

	} else if (if_getcapenable(ifp) & IFCAP_LRO) {
		if_togglecapenable(ifp, IFCAP_LRO);

		if (lio_hwlro)
			ret = lio_set_feature(ifp, LIO_CMD_LRO_DISABLE, LIO_LROIPV4 |
					      LIO_LROIPV6);
	} else
		lio_dev_info(lio->oct_dev, "LRO requires RXCSUM");

	return ((ret) ? EINVAL : 0);
}

static void
lio_mtu_ctl_callback(struct octeon_device *oct, uint32_t status, void *buf)
{
	struct lio_soft_command	*sc = buf;
	volatile int		*mtu_sc_ctx;

	mtu_sc_ctx = sc->ctxptr;

	if (status) {
		lio_dev_err(oct, "MTU updation ctl instruction failed. Status: %llx\n",
			    LIO_CAST64(status));
		*mtu_sc_ctx = -1;
		/*
		 * This barrier is required to be sure that the
		 * response has been written fully.
		 */
		wmb();
		return;
	}

	*mtu_sc_ctx = 1;

	/*
	 * This barrier is required to be sure that the response has been
	 * written fully.
	 */
	wmb();
}

/* @param ifp is network device */
static int
lio_change_mtu(struct ifnet *ifp, int new_mtu)
{
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	struct lio_soft_command	*sc;
	union octeon_cmd	*ncmd;
	volatile int		*mtu_sc_ctx;
	int	retval = 0;

	if (lio->mtu == new_mtu)
		return (0);

	/*
	 * Limit the MTU to make sure the ethernet packets are between
	 * LIO_MIN_MTU_SIZE bytes and LIO_MAX_MTU_SIZE bytes
	 */
	if ((new_mtu < LIO_MIN_MTU_SIZE) || (new_mtu > LIO_MAX_MTU_SIZE)) {
		lio_dev_err(oct, "Invalid MTU: %d\n", new_mtu);
		lio_dev_err(oct, "Valid range %d and %d\n",
			    LIO_MIN_MTU_SIZE, LIO_MAX_MTU_SIZE);
		return (EINVAL);
	}

	sc = lio_alloc_soft_command(oct, OCTEON_CMD_SIZE, 16,
				    sizeof(*mtu_sc_ctx));
	if (sc == NULL)
		return (ENOMEM);

	ncmd = (union octeon_cmd *)sc->virtdptr;
	mtu_sc_ctx = sc->ctxptr;

	*mtu_sc_ctx = 0;

	ncmd->cmd64 = 0;
	ncmd->s.cmd = LIO_CMD_CHANGE_MTU;
	ncmd->s.param1 = new_mtu;

	lio_swap_8B_data((uint64_t *)ncmd, (OCTEON_CMD_SIZE >> 3));

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	lio_prepare_soft_command(oct, sc, LIO_OPCODE_NIC,
				 LIO_OPCODE_NIC_CMD, 0, 0, 0);

	sc->callback = lio_mtu_ctl_callback;
	sc->callback_arg = sc;
	sc->wait_time = 5000;

	retval = lio_send_soft_command(oct, sc);
	if (retval == LIO_IQ_SEND_FAILED) {
		lio_dev_info(oct,
			     "Failed to send MTU update Control message\n");
		retval = EBUSY;
		goto mtu_updation_failed;
	}

	/*
	 * Sleep on a wait queue till the cond flag indicates that the
	 * response arrived or timed-out.
	 */
	lio_sleep_cond(oct, mtu_sc_ctx);

	if (*mtu_sc_ctx < 0) {
		retval = EBUSY;
		goto mtu_updation_failed;
	}
	lio_dev_info(oct, "MTU Changed from %d to %d\n", if_getmtu(ifp),
		     new_mtu);
	if_setmtu(ifp, new_mtu);
	lio->mtu = new_mtu;
	retval = 0;			/*
				         * this updation is make sure that LIO_IQ_SEND_STOP case
				         * also success
				         */

mtu_updation_failed:
	lio_free_soft_command(oct, sc);

	return (retval);
}

/* @param ifp network device */
int
lio_set_mac(struct ifnet *ifp, uint8_t *p)
{
	struct lio_ctrl_pkt	nctrl;
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	int	ret = 0;

	if (!lio_is_valid_ether_addr(p))
		return (EADDRNOTAVAIL);

	bzero(&nctrl, sizeof(struct lio_ctrl_pkt));

	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = LIO_CMD_CHANGE_MACADDR;
	nctrl.ncmd.s.param1 = 0;
	nctrl.ncmd.s.more = 1;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;
	nctrl.wait_time = 100;

	nctrl.udd[0] = 0;
	/* The MAC Address is presented in network byte order. */
	memcpy((uint8_t *)&nctrl.udd[0] + 2, p, ETHER_HDR_LEN);

	ret = lio_send_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		lio_dev_err(oct, "MAC Address change failed\n");
		return (ENOMEM);
	}

	memcpy(((uint8_t *)&lio->linfo.hw_addr) + 2, p, ETHER_HDR_LEN);

	return (0);
}

/*
 * \brief Converts a mask based on ifp flags
 * @param ifp network device
 *
 * This routine generates a lio_ifflags mask from the ifp flags
 * received from the OS.
 */
static inline enum lio_ifflags
lio_get_new_flags(struct ifnet *ifp)
{
	enum lio_ifflags f = LIO_IFFLAG_UNICAST;

	if (if_getflags(ifp) & IFF_PROMISC)
		f |= LIO_IFFLAG_PROMISC;

	if (if_getflags(ifp) & IFF_ALLMULTI)
		f |= LIO_IFFLAG_ALLMULTI;

	if (if_getflags(ifp) & IFF_MULTICAST) {
		f |= LIO_IFFLAG_MULTICAST;

		/*
		 * Accept all multicast addresses if there are more than we
		 * can handle
		 */
		if (if_getamcount(ifp) > LIO_MAX_MULTICAST_ADDR)
			f |= LIO_IFFLAG_ALLMULTI;
	}
	if (if_getflags(ifp) & IFF_BROADCAST)
		f |= LIO_IFFLAG_BROADCAST;

	return (f);
}

/* @param ifp network device */
static int
lio_set_mcast_list(struct ifnet *ifp)
{
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	struct lio_ctrl_pkt	nctrl;
	struct ifmultiaddr	*ifma;
	uint64_t		*mc;
	int	mc_count = 0;
	int	ret;

	bzero(&nctrl, sizeof(struct lio_ctrl_pkt));

	/* Create a ctrl pkt command to be sent to core app. */
	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = LIO_CMD_SET_MULTI_LIST;
	nctrl.ncmd.s.param1 = lio_get_new_flags(ifp);
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;

	/* copy all the addresses into the udd */
	mc = &nctrl.udd[0];

	/* to protect access to if_multiaddrs */
	if_maddr_rlock(ifp);

	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		*mc = 0;
		memcpy(((uint8_t *)mc) + 2,
		       LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		       ETHER_ADDR_LEN);
		/* no need to swap bytes */

		mc_count++;
		if (++mc > &nctrl.udd[LIO_MAX_MULTICAST_ADDR])
			break;
	}

	if_maddr_runlock(ifp);

	/*
	 * Apparently, any activity in this call from the kernel has to
	 * be atomic. So we won't wait for response.
	 */
	nctrl.wait_time = 0;
	nctrl.ncmd.s.param2 = mc_count;
	nctrl.ncmd.s.more = mc_count;

	ret = lio_send_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		lio_dev_err(oct, "DEVFLAGS change failed in core (ret: 0x%x)\n",
			    ret);
	}

	return ((ret) ? EINVAL : 0);
}
