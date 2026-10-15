#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Driver-related behavior tests for Pause-based Flow Control.
"""

import errno
import json
import os

from lib.py import KsftFailEx, KsftNamedVariant, KsftSkipEx
from lib.py import NetDrvEpEnv
from lib.py import cmd, defer, ethtool, ip, wait_file
from lib.py import ksft_disruptive, ksft_variants, ksft_run
from lib.py import ksft_eq, ksft_exit, ksft_in, ksft_not_in, ksft_pr

# Linkmodes to Pause params :
# Pause bit is set if rx == 1
# Asym_Pause bit is set if rx != tx
pauseparams_to_linkmodes = {
    0: {0: {"rx": 0, "tx": 0, "linkmodes": []},
        1: {"rx": 0, "tx": 1, "linkmodes": ["Asym_Pause"]}},
    1: {0: {"rx": 1, "tx": 0, "linkmodes": ["Pause", "Asym_Pause"]},
        1: {"rx": 1, "tx": 1, "linkmodes": ["Pause"]}},
}

def onoff(val):
    """ Convert a bool to on/off """
    return "on" if val else "off"

pauseparams_variants = [
    KsftNamedVariant(f"RX {onoff(p['rx'])} TX {onoff(p['tx'])}", p)
    for by_tx in pauseparams_to_linkmodes.values() for p in by_tx.values()
]

_strerrors = {os.strerror(e): e for e in errno.errorcode}

def ethtool_ret(command, is_get=True, host=None):
    """ Execute an ethtool command, returns the return code and JSON content

    :param command: the ethtool arguments
    :param is_get: Is the command a get or a set. Get commands return the loaded
                   JSON attributes
    :param host: The host on which to run the command on. None means local host.
    """
    json_flag = "--json" if is_get else ""
    cmd_res = cmd(f"ethtool {json_flag} {command}", host=host, fail=False)

    if cmd_res.ret != 0:
        # ethtool returns 1 upon error, not the netlink errcode. Try to get it
        # by parsing the stderr output, which looks like :
        # "netlink error: Operation not supported"
        for line in cmd_res.stderr.splitlines():
            err = _strerrors.get(line.rsplit(": ", 1)[-1].strip())
            if err:
                return err, None
        return cmd_res.ret, None

    # Not a get operation, we don't have any JSON output to parse
    if not is_get:
        return 0, None

    return 0, json.loads(cmd_res.stdout)[0]

def wait_for_link(cfg, timeout=15):
    """ Wait for the link to come up """

    wait_file(f"/sys/class/net/{cfg.ifname}/carrier",
              lambda x: x.strip() == "1", deadline=timeout)

def wait_for_aneg(cfg, link_drop=False, timeout=15):
    """ Wait for a renegotiation to complete

    :param link_drop: Set to true if the link HAS to flap.
    """
    try:
        wait_file(f"/sys/class/net/{cfg.ifname}/carrier",
                  lambda x: x.strip() == "0",
                  deadline=timeout if link_drop else 2)
    except TimeoutError:
        if link_drop:
            raise

    wait_for_link(cfg, timeout)

def controllable_lp(cfg):
    """ Whether the remote interface is the link partner of the local one.

    For low level ethtool tests, we need to have the remote directly connected
    to the local host (i.e. not through a switch).

    This is tested by taking triggering a link renegotiation and checking the
    carrier_changes counter on the partner.

    :returns: True if the remote is our link partner
    """
    controllable = getattr(cfg, "lp_controllable", None)
    if controllable is not None:
        return controllable

    before = int(cmd(f"cat /sys/class/net/{cfg.remote_ifname}/carrier_changes",
                     host=cfg.remote).stdout)

    ret, _ = ethtool_ret(f"-r {cfg.ifname}", is_get=False)
    if ret != 0:
        raise KsftSkipEx(f"ethtool -r {cfg.ifname} failed: {errno.errorcode.get(ret, ret)}")

    wait_for_aneg(cfg, link_drop=True)

    cfg.lp_controllable = int(cmd(f"cat /sys/class/net/{cfg.remote_ifname}/carrier_changes",
                                  host=cfg.remote).stdout) > before
    return cfg.lp_controllable

def require_controllable_lp(cfg):
    """ Skip if the remote isn't directly controllable, e.g. accessed through
        a switch
    """
    if not controllable_lp(cfg):
        raise KsftSkipEx(f"{cfg.remote_ifname} is not directly connected to {cfg.ifname}")

def forced_link_settings(cfg):
    """ Returns a string to pass to ethtool -s with speed/duplex corresponding
        to the current settings.

        Note that some devices don't return duplex info, so assume full duplex
        in that case.
    """
    link = ethtool(f"{cfg.ifname}", json=True)[0]
    if "speed" not in link:
        return ""

    return f"speed {link['speed']} duplex {link.get('duplex', 'Full').lower()}"

def _ethtool_pause_use_to_linkmodes(use):
    """ Convert the ethtool output for pause modes into pause linkmodes """
    if use == "Symmetric":
        return ["Pause"]
    elif use == "Symmetric Receive-only":
        return ["Pause", "Asym_Pause"]
    elif use == "Transmit-only":
        return ["Asym_Pause"]
    else:
        return []

def pause_to_linkmodes(rx, tx):
    """ Convert the bool rx/tx pause into Pause/Asym """
    return pauseparams_to_linkmodes[rx][tx]["linkmodes"]

def set_local_pauseparams(cfg, rx, tx, aneg):
    """ set pauseparams : ethtool -A """
    rx_param = onoff(rx)
    tx_param = onoff(tx)
    aneg_param = onoff(aneg)

    ret, _ = ethtool_ret(f"-A {cfg.ifname} rx {rx_param} tx {tx_param}"
                         f" autoneg {aneg_param}",
                         is_get = False)

    if ret != 0 and ret != errno.EOPNOTSUPP:
        raise KsftFailEx(f"ethtool -A {cfg.ifname} failed: {ret}")

    return ret

def get_local_pauseparams(cfg):
    """ get pauseparams : ethtool -a """
    return ethtool_ret(f"-a {cfg.ifname}", is_get=True)

def set_peer_pauseparams(cfg, rx, tx, aneg):
    """ set pauseparams on peer : ethtool -A """
    rx_param = onoff(rx)
    tx_param = onoff(tx)
    aneg_param = onoff(aneg)

    ret, _ = ethtool_ret(f"-A {cfg.remote_ifname} rx {rx_param} tx {tx_param}"
                         f" autoneg {aneg_param}",
                         is_get = False, host=cfg.remote)

    if ret != 0:
        raise KsftSkipEx(f"Can't set pauseparams on peer: {errno.errorcode.get(ret, ret)}")

    return ret

def get_peer_pauseparams(cfg):
    """ get pauseparams on peer : ethtool -a """

    return ethtool_ret(f"-a {cfg.remote_ifname}", is_get=True,
                       host=cfg.remote)

def get_local_pause_supported(cfg):
    """ Return the errcode and the local pause supported linkmodes """

    ret, data = ethtool_ret(f"{cfg.ifname}")
    if ret != 0:
        raise KsftFailEx(f"ethtool {cfg.ifname} failed: {ret}")

    if "supported-pause-frame-use" not in data:
        raise KsftSkipEx(f"{cfg.ifname} doesn't report its link modes")

    return ret, _ethtool_pause_use_to_linkmodes(data["supported-pause-frame-use"])

def get_local_pause_advertising(cfg):
    """ Return the errcode and the local pause advertised linkmodes """

    ret, data = ethtool_ret(f"{cfg.ifname}")
    if ret != 0:
        raise KsftFailEx(f"ethtool {cfg.ifname} failed: {ret}")

    if "advertised-pause-frame-use" not in data:
        raise KsftSkipEx(f"{cfg.ifname} doesn't report its link modes")

    return ret, _ethtool_pause_use_to_linkmodes(data["advertised-pause-frame-use"])

def get_local_pause_lp_advertising(cfg):
    """ Return the errcode and the local pause lp_advertised linkmodes,
        if any.
    """

    ret, data = ethtool_ret(f"{cfg.ifname}")
    if ret != 0:
        raise KsftFailEx(f"ethtool {cfg.ifname} failed: {errno.errorcode.get(ret, ret)}")

    if "link-partner-advertised-pause-frame-use" in data:
        return ret, _ethtool_pause_use_to_linkmodes(data["link-partner-advertised-pause-frame-use"])
    else:
        ksft_pr(f"Warning: {cfg.ifname} does not report the LP's advertising")
        return errno.EOPNOTSUPP, None

def get_peer_pause_supported(cfg):
    """ Return the errcode and the remote pause supported linkmodes """

    ret, data = ethtool_ret(f"{cfg.remote_ifname}", host = cfg.remote)
    if ret != 0:
        raise KsftFailEx(f"ethtool {cfg.remote_ifname} failed: {errno.errorcode.get(ret, ret)}")

    if "supported-pause-frame-use" not in data:
        raise KsftSkipEx(f"{cfg.remote_ifname} doesn't report its link modes")

    return ret, _ethtool_pause_use_to_linkmodes(data["supported-pause-frame-use"])


def get_peer_pause_advertising(cfg):
    """ Return the errcode and the remote pause advertised linkmodes """

    ret, data = ethtool_ret(f"{cfg.remote_ifname}", host = cfg.remote)
    if ret != 0:
        raise KsftFailEx(f"ethtool {cfg.remote_ifname} failed: {errno.errorcode.get(ret, ret)}")

    if "advertised-pause-frame-use" not in data:
        raise KsftSkipEx(f"{cfg.remote_ifname} doesn't report its link modes")

    return ret, _ethtool_pause_use_to_linkmodes(data["advertised-pause-frame-use"])

def get_peer_pause_lp_advertising(cfg):
    """ Return the errcode and the peer pause lp_advertised linkmodes,
        if any.
    """

    ret, data = ethtool_ret(f"{cfg.remote_ifname}", host = cfg.remote)
    if ret != 0:
        raise KsftFailEx(f"ethtool {cfg.remote_ifname} failed: {errno.errorcode.get(ret, ret)}")

    if "link-partner-advertised-pause-frame-use" in data:
        return ret, _ethtool_pause_use_to_linkmodes(data["link-partner-advertised-pause-frame-use"])
    else:
        ksft_pr(f"Warning: {cfg.remote_ifname} does not report the LP's advertising")
        return errno.EOPNOTSUPP, None

def require_pause_supported_allof(cfg, linkmodes):
    """ Skip if local device doesn't support all of the passed modes """

    ret, _ = get_local_pauseparams(cfg)
    if ret != 0:
        raise KsftSkipEx("device doesn't allow getting pauseparams")

    _, pause_support = get_local_pause_supported(cfg)
    for lm in linkmodes:
        if lm not in pause_support:
            raise KsftSkipEx(f"Local device doesn't support {lm}")

def require_peer_pause_supported_allof(cfg, linkmodes):
    """ Skip if local device doesn't support all of the passed modes """

    ret, _ = get_peer_pauseparams(cfg)
    if ret != 0:
        raise KsftSkipEx("Remote device doesn't allow getting pauseparams")

    _, pause_support = get_peer_pause_supported(cfg)
    for lm in linkmodes:
        if lm not in pause_support:
            raise KsftSkipEx(f"Remote device doesn't support {lm}")

def expect_pauseparams_set(cfg, pauseparams, aneg, supported, note):
    """ ethtool -A must work or be refused depending on what the local
        device supports
    """
    rx = pauseparams["rx"]
    tx = pauseparams["tx"]

    # Measure current pauseparams, re-setting the same params as the current
    # ones returns 0 even if unsupported.
    _, cur = get_local_pauseparams(cfg)
    noop = cur and (cur["rx"], cur["tx"], cur["autonegotiate"]) == \
           (bool(rx), bool(tx), aneg)

    ret, _ = ethtool_ret(f"-A {cfg.ifname} rx {onoff(rx)} tx {onoff(tx)}"
                         f" autoneg {onoff(aneg)}", is_get = False)

    if noop:
        ksft_eq(ret, 0, note)
    elif not supported:
        ksft_eq(ret, errno.EOPNOTSUPP, note)
    elif set(pauseparams["linkmodes"]).issubset(set(supported)):
        ksft_eq(ret, 0, note)
    else:
        ksft_in(ret, (errno.EOPNOTSUPP, errno.EINVAL), note)

def pause_setup(cfg):
    """ The starting conditions every test counts on, restored on exit:
        - both ports admin up
        - link autoneg on on both sides ifsupported
        - carrier on
    """

    # Get init pause parameters
    ret, params = ethtool_ret(f"-a {cfg.ifname}")
    if ret == 0:
        defer(cmd, f"ethtool -A {cfg.ifname} rx {onoff(params['rx'])} "
                   f"tx {onoff(params['tx'])} "
                   f"autoneg {onoff(params['autonegotiate'])}")

    # Get init link parameters
    link = ethtool(f"{cfg.ifname}", json=True)[0]
    if "auto-negotiation" not in link:
        raise KsftSkipEx(f"{cfg.ifname} doesn't report its link modes")

    if link["auto-negotiation"]:
        defer(cmd, f"ethtool -s {cfg.ifname} autoneg on")
    elif "speed" in link and "duplex" in link:
        defer(cmd, f"ethtool -s {cfg.ifname} autoneg off speed {link['speed']} "
                   f"duplex {link['duplex'].lower()}")

    # Local interface admin up, link aneg on
    ip(f"link set {cfg.ifname} up")
    if link["supports-auto-negotiation"] and not link["auto-negotiation"]:
        ethtool(f"-s {cfg.ifname} autoneg on")

    # Get remote pause params
    ret, params = ethtool_ret(f"-a {cfg.remote_ifname}", host=cfg.remote)
    if ret == 0:
        defer(cmd, f"ethtool -A {cfg.remote_ifname} rx {onoff(params['rx'])} "
                   f"tx {onoff(params['tx'])} "
                   f"autoneg {onoff(params['autonegotiate'])}",
                   host=cfg.remote)

    # Get remote link params
    link = ethtool(f"{cfg.remote_ifname}", json=True, host=cfg.remote)[0]
    if "auto-negotiation" not in link:
        raise KsftSkipEx(f"{cfg.remote_ifname} doesn't report its link modes")

    if link["auto-negotiation"]:
        defer(cmd, f"ethtool -s {cfg.remote_ifname} autoneg on",
              host=cfg.remote)
    elif "speed" in link and "duplex" in link:
        defer(cmd, f"ethtool -s {cfg.remote_ifname} autoneg off "
                   f"speed {link['speed']} duplex {link['duplex'].lower()}",
                   host=cfg.remote)

    # Remote interface admin up, link aneg on
    ip(f"link set {cfg.remote_ifname} up", host=cfg.remote)
    if link["supports-auto-negotiation"] and not link["auto-negotiation"]:
        ethtool(f"-s {cfg.remote_ifname} autoneg on", host=cfg.remote)

    wait_for_link(cfg)

# Pause support : Supported linkmodes vs ability to set/get pauseparams
@ksft_variants(pauseparams_variants)
@ksft_disruptive
def pause_test_support(cfg, pauseparams):
    """ Verify that the supported linkmodes Pause and Asym_Pause match the
        ability to configure the rx and tx pauseparams.

    Drivers are expected to reject pauseparams they don't support, and
    accept the ones they support. The supported modes are exposed by
    the MAC to the PHY layer through phylink mac_capabilities MAC_SYM_PAUSE
    and MAC_ASYM_PAUSE, or through phylib directly with the
    phy_support_sym_pause() and phy_support_asym_pause() helpers.

    The expectation is for drivers to refuse setting pauseparams that don't
    match the Pause and Asym_Pause bits in the supported linkmodes with a
    -EOPNOTSUPP return value. Unsupported pause params must be rejected.

    Failing this test likely means the MAC driver doesn't implement the
    set/get_pauseparam, but still sets flow control as supported through
    phylink mac_capabilities or phylib's pause API. Conversely, the MAC driver
    may have omitted to indicate its supported Pause modes. Finally, the PHY
    driver may incorrectly override the Pause and Asym_Pause bits in its
    supported fields.
    """

    pause_setup(cfg)

    forced = forced_link_settings(cfg)
    _, supported = get_local_pause_supported(cfg)

    # We check that what we can configure in the pause params matches what we
    # support under various contditions : Link aneg on/off, pause aneg on/off
    ret, _ = ethtool_ret(f"-s {cfg.ifname} autoneg on", is_get = False)
    if ret != 0:
        ksft_pr("link autoneg on refused, not tested")
    else:
        expect_pauseparams_set(cfg, pauseparams, False, supported,
                               "link autoneg on")
        expect_pauseparams_set(cfg, pauseparams, True, supported,
                               "link autoneg on")

    if not forced:
        ksft_pr("link speed unknown, the forced link is not tested")
        return

    ret, _ = ethtool_ret(f"-s {cfg.ifname} autoneg off {forced}", is_get = False)
    if ret != 0:
        ksft_pr(f"link autoneg off {forced} refused, not tested")
        return

    expect_pauseparams_set(cfg, pauseparams, False, supported, "link autoneg off")
    expect_pauseparams_set(cfg, pauseparams, True, supported, "link autoneg off")

@ksft_variants(pauseparams_variants)
@ksft_disruptive
def pause_advertising_test(cfg, pauseparams):
    """Pause advertisement

    Validate that changing pause params through the ETHTOOL_MSG_PAUSE command
    translates to a change in the advertised pause params, and that these
    parameters are correct w.r.t the supported pause params and requested pause
    params.

    This exercises the .set_pauseparam() ethtool ops for MAC configuration,
    as well as the reconfiguration of the PHY's advertising and negotiation.

    On non-phylink MACs, the MAC should call phy_set_sym_pause() to update the
    PHY's advertising, and restart a negotiation with phy_start_aneg() if
    need be. Failure to do so will result in the wrong advertising parameters.

    On phylink-enabled MACs, phylink deals with the PHY reconfiguration provided
    the MAC driver calls phylink_ethtool_set_pauseparam().

    Failing this test likely means that the PHY driver is not correctly
    advertising pause settings, either due to the MAC not triggering a PHY
    reconfiguration, a misconfiguration of the advertising registers by the PHY,
    or by mis-handling the phydev->advertising bitmap in the PHY driver directly.

    The validation is made by looking at the advertised modes locally, as well
    as what the peer's 'lp_advertising' values report.
    """

    require_pause_supported_allof(cfg, pauseparams["linkmodes"])
    pause_setup(cfg)
    lp = controllable_lp(cfg)

    tx = pauseparams["tx"]
    rx = pauseparams["rx"]
    adv = pauseparams["linkmodes"]
    not_adv = [ l for l in ["Pause", "Asym_Pause"] if l not in adv]

    # It's OK to skip here, we're already validating the EOPNOTSUPP behaviour
    # the pause_test_support test.
    ret = set_local_pauseparams(cfg, rx, tx, True)
    if ret == errno.EOPNOTSUPP:
        raise KsftSkipEx(f"RX {rx} TX {tx} not supported")

    # Wait for link parameters to re-negotiate and link to come back up. It must
    # come back up, otherwise that means changing pauseparams can bring the
    # link down.
    wait_for_aneg(cfg)

    _, linkmodes = get_local_pause_advertising(cfg)
    for mode in adv:
        ksft_in(mode, linkmodes,
                f"rx {rx} tx {tx} aneg on must advertise {adv}")

    for mode in not_adv:
        ksft_not_in(mode, linkmodes,
                    f"rx {rx} tx {tx} aneg on must not advertise {not_adv}")

    if not lp:
        return

    returncode, remote_linkmodes = get_peer_pause_lp_advertising(cfg)
    if returncode == errno.EOPNOTSUPP:
        return

    for mode in adv:
        ksft_in(mode, remote_linkmodes, f"PHY does not advertise {adv}")

    for mode in not_adv:
        ksft_not_in(mode, remote_linkmodes,
                    f"PHY incorrectly advertises {not_adv}")


# Pause autonegotiation resolution : Resolved pause settings vs configured
# pauseparams on local device and link partner
@ksft_variants([
    # We advertise nothing, all off
    KsftNamedVariant("local rx off tx off, remote rx off tx off",
        {"rx": 0, "tx": 0, "lp_rx": 0, "lp_tx": 0, "neg_rx": 0, "neg_tx": 0}),

    # We advertise nothing, all off
    KsftNamedVariant("local rx off tx off, remote rx off tx on",
        {"rx": 0, "tx": 0, "lp_rx": 0, "lp_tx": 1, "neg_rx": 0, "neg_tx": 0}),

    # We advertise nothing, all off
    KsftNamedVariant("local rx off tx off, remote rx on tx off",
        {"rx": 0, "tx": 0, "lp_rx": 1, "lp_tx": 0, "neg_rx": 0, "neg_tx": 0}),

    # We advertise nothing, all off
    KsftNamedVariant("local rx off tx off, remote rx on tx on",
        {"rx": 0, "tx": 0, "lp_rx": 1, "lp_tx": 1, "neg_rx": 0, "neg_tx": 0}),

    # LP advertises nothing, all off
    KsftNamedVariant("local rx off tx on, remote rx off tx off",
        {"rx": 0, "tx": 1, "lp_rx": 0, "lp_tx": 0, "neg_rx": 0, "neg_tx": 0}),

    # We advertise Asym, LP advertises Asym, all off
    KsftNamedVariant("local rx off tx on, remote rx off tx on",
        {"rx": 0, "tx": 1, "lp_rx": 0, "lp_tx": 1, "neg_rx": 0, "neg_tx": 0}),

    # We advertise Asym, LP advertises Pause + Asym, tx on
    KsftNamedVariant("local rx off tx on, remote rx on tx off",
        {"rx": 0, "tx": 1, "lp_rx": 1, "lp_tx": 0, "neg_rx": 0, "neg_tx": 1}),

    # Tricky case :
    # We advertise Asym, LP advertises Pause, resolves to all off
    KsftNamedVariant("local rx off tx on, remote rx on tx on",
        {"rx": 0, "tx": 1, "lp_rx": 1, "lp_tx": 1, "neg_rx": 0, "neg_tx": 0}),

    # LP advertises nothing, all off
    KsftNamedVariant("local rx on tx off, remote rx off tx off",
        {"rx": 1, "tx": 0, "lp_rx": 0, "lp_tx": 0, "neg_rx": 0, "neg_tx": 0}),

    # We advertise Pause + Asym , LP advertises Asym, rx on
    KsftNamedVariant("local rx on tx off, remote rx off tx on",
        {"rx": 1, "tx": 0, "lp_rx": 0, "lp_tx": 1, "neg_rx": 1, "neg_tx": 0}),

    # Also tricky: Only rx enabled on both ends, but we negotiate rx/tx
    # We advertise Pause + Asym, LP advertises Pause + Asym, all on
    KsftNamedVariant("local rx on tx off, remote rx on tx off",
        {"rx": 1, "tx": 0, "lp_rx": 1, "lp_tx": 0, "neg_rx": 1, "neg_tx": 1}),

    # We advertise Pause + Asym, LP advertises Pause, all on
    KsftNamedVariant("local rx on tx off, remote rx on tx on",
        {"rx": 1, "tx": 0, "lp_rx": 1, "lp_tx": 1, "neg_rx": 1, "neg_tx": 1}),

    # LP advertises nothing, all off
    KsftNamedVariant("local rx on tx on, remote rx off tx off",
        {"rx": 1, "tx": 1, "lp_rx": 0, "lp_tx": 0, "neg_rx": 0, "neg_tx": 0}),

    # Tricky case :
    # We advertise Pause, LP advertises Asym, resolves to all off
    KsftNamedVariant("local rx on tx on, remote rx off tx on",
        {"rx": 1, "tx": 1, "lp_rx": 0, "lp_tx": 1, "neg_rx": 0, "neg_tx": 0}),

    # We advertise Pause, LP advertises Pause + Asym, all on
    KsftNamedVariant("local rx on tx on, remote rx on tx off",
        {"rx": 1, "tx": 1, "lp_rx": 1, "lp_tx": 0, "neg_rx": 1, "neg_tx": 1}),

    # We advertise Pause, LP advertises Pause, all on
    KsftNamedVariant("local rx on tx on, remote rx on tx on",
        {"rx": 1, "tx": 1, "lp_rx": 1, "lp_tx": 1, "neg_rx": 1, "neg_tx": 1}),
])
@ksft_disruptive
def pause_aneg_resolution(cfg, settings):
    """ Verify that rx and tx pause parameters are negotiated according to 802.3

    802.3 dictates the rules for pause negotiation, all 16 cases are tested, one
    for each combination of Pause and Asym_Pause advertising on the local device
    and the link-partner.

    This test also verifies that the peer resolved the parameters correctly,
    to ensure the negotiation is triggered correctly.

    Failing this test can happen if :
     - The MAC accepts the pause parameters but doesn't trigger a link
       renegotiation
     - that the PHY driver manually overwrites the Pause negotiation result
     - that the MAC driver ignores the Pause resolution and sets its own
       pause parameters regardless
    """

    expected_local_rx = settings["neg_rx"]
    expected_local_tx = settings["neg_tx"]

    required_local_linkmodes = pause_to_linkmodes(settings["rx"],
                                                  settings["tx"])
    required_remote_linkmodes = pause_to_linkmodes(settings["lp_rx"],
                                                   settings["lp_tx"])

    require_pause_supported_allof(cfg, required_local_linkmodes)
    require_peer_pause_supported_allof(cfg, required_remote_linkmodes)
    pause_setup(cfg)
    require_controllable_lp(cfg)

    # There's symmetry between local device and LP on pause negotiation:
    # - if local resolves all off or all on, LP must resolve the same
    # - if local resolves RX only, remote must resolve to TX only
    # - if local resolves TX only, remote must resolve to RX only
    if expected_local_rx == expected_local_tx:
        expected_lp_rx = expected_local_rx
        expected_lp_tx = expected_local_tx
    else:
        expected_lp_rx = expected_local_tx
        expected_lp_tx = expected_local_rx

    # Set pauseparams
    ret = set_local_pauseparams(cfg, settings["rx"], settings["tx"], True)
    if ret == errno.EOPNOTSUPP:
        raise KsftSkipEx(f"RX {settings['rx']} TX {settings['tx']} not supported")

    wait_for_aneg(cfg)

    set_peer_pauseparams(cfg, settings["lp_rx"], settings["lp_tx"], True)

    # Wait for link to re-negotiate
    wait_for_aneg(cfg)

    if get_local_pause_lp_advertising(cfg)[0] != 0:
        raise KsftSkipEx("Local device doesn't report the LP's advertising")

    ret, local_pauseparams = get_local_pauseparams(cfg)
    if ret != 0 or "negotiated" not in local_pauseparams:
        raise KsftSkipEx("Local device doesn't report the negotiated pause params")

    # check adv
    _, linkmodes = get_local_pause_advertising(cfg)
    ksft_eq(linkmodes, required_local_linkmodes,
            f"local rx {settings['rx']} tx {settings['tx']} must advertise "
            f"{required_local_linkmodes}")

    _, linkmodes = get_peer_pause_advertising(cfg)
    ksft_eq(linkmodes, required_remote_linkmodes,
            f"remote rx {settings['lp_rx']} tx {settings['lp_tx']} must advertise "
            f"{required_remote_linkmodes}")

    # check lp_adv if available
    _, linkmodes = get_local_pause_lp_advertising(cfg)
    ksft_eq(linkmodes, required_remote_linkmodes,
            f"local lp_adv must show the remote's {required_remote_linkmodes}")

    # check lp_adv on remote
    ret, linkmodes = get_peer_pause_lp_advertising(cfg)
    if ret == 0:
        ksft_eq(linkmodes, required_local_linkmodes,
                f"remote lp_adv must show our {required_local_linkmodes}")

    # Check resolution
    _, local_pauseparams = get_local_pauseparams(cfg)
    ksft_eq(local_pauseparams["negotiated"]["rx"], expected_local_rx)
    ksft_eq(local_pauseparams["negotiated"]["tx"], expected_local_tx)

    ret, remote_pauseparams = get_peer_pauseparams(cfg)
    if ret == 0 and "negotiated" in remote_pauseparams:
        ksft_eq(remote_pauseparams["negotiated"]["rx"], expected_lp_rx)
        ksft_eq(remote_pauseparams["negotiated"]["tx"], expected_lp_tx)

def main():
    """ Entry point """
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        ksft_run([pause_test_support,
                  pause_advertising_test,
                  pause_aneg_resolution,
                  ],
                 args=(cfg, ))
    ksft_exit()

if __name__ == "__main__":
    main()
