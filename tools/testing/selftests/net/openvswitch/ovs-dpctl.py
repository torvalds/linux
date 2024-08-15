#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

# Controls the openvswitch module.  Part of the kselftest suite, but
# can be used for some diagnostic purpose as well.

import argparse
import errno
import sys

try:
    from pyroute2 import NDB

    from pyroute2.netlink import NLM_F_ACK
    from pyroute2.netlink import NLM_F_REQUEST
    from pyroute2.netlink import genlmsg
    from pyroute2.netlink import nla
    from pyroute2.netlink.exceptions import NetlinkError
    from pyroute2.netlink.generic import GenericNetlinkSocket
    import pyroute2

except ModuleNotFoundError:
    print("Need to install the python pyroute2 package >= 0.6.")
    sys.exit(0)


OVS_DATAPATH_FAMILY = "ovs_datapath"
OVS_VPORT_FAMILY = "ovs_vport"
OVS_FLOW_FAMILY = "ovs_flow"
OVS_PACKET_FAMILY = "ovs_packet"
OVS_METER_FAMILY = "ovs_meter"
OVS_CT_LIMIT_FAMILY = "ovs_ct_limit"

OVS_DATAPATH_VERSION = 2
OVS_DP_CMD_NEW = 1
OVS_DP_CMD_DEL = 2
OVS_DP_CMD_GET = 3
OVS_DP_CMD_SET = 4

OVS_VPORT_CMD_NEW = 1
OVS_VPORT_CMD_DEL = 2
OVS_VPORT_CMD_GET = 3
OVS_VPORT_CMD_SET = 4


class ovs_dp_msg(genlmsg):
    # include the OVS version
    # We need a custom header rather than just being able to rely on
    # genlmsg because fields ends up not expressing everything correctly
    # if we use the canonical example of setting fields = (('customfield',),)
    fields = genlmsg.fields + (("dpifindex", "I"),)


class OvsDatapath(GenericNetlinkSocket):

    OVS_DP_F_VPORT_PIDS = 1 << 1
    OVS_DP_F_DISPATCH_UPCALL_PER_CPU = 1 << 3

    class dp_cmd_msg(ovs_dp_msg):
        """
        Message class that will be used to communicate with the kernel module.
        """

        nla_map = (
            ("OVS_DP_ATTR_UNSPEC", "none"),
            ("OVS_DP_ATTR_NAME", "asciiz"),
            ("OVS_DP_ATTR_UPCALL_PID", "array(uint32)"),
            ("OVS_DP_ATTR_STATS", "dpstats"),
            ("OVS_DP_ATTR_MEGAFLOW_STATS", "megaflowstats"),
            ("OVS_DP_ATTR_USER_FEATURES", "uint32"),
            ("OVS_DP_ATTR_PAD", "none"),
            ("OVS_DP_ATTR_MASKS_CACHE_SIZE", "uint32"),
            ("OVS_DP_ATTR_PER_CPU_PIDS", "array(uint32)"),
        )

        class dpstats(nla):
            fields = (
                ("hit", "=Q"),
                ("missed", "=Q"),
                ("lost", "=Q"),
                ("flows", "=Q"),
            )

        class megaflowstats(nla):
            fields = (
                ("mask_hit", "=Q"),
                ("masks", "=I"),
                ("padding", "=I"),
                ("cache_hits", "=Q"),
                ("pad1", "=Q"),
            )

    def __init__(self):
        GenericNetlinkSocket.__init__(self)
        self.bind(OVS_DATAPATH_FAMILY, OvsDatapath.dp_cmd_msg)

    def info(self, dpname, ifindex=0):
        msg = OvsDatapath.dp_cmd_msg()
        msg["cmd"] = OVS_DP_CMD_GET
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = ifindex
        msg["attrs"].append(["OVS_DP_ATTR_NAME", dpname])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == errno.ENODEV:
                reply = None
            else:
                raise ne

        return reply

    def create(self, dpname, shouldUpcall=False, versionStr=None):
        msg = OvsDatapath.dp_cmd_msg()
        msg["cmd"] = OVS_DP_CMD_NEW
        if versionStr is None:
            msg["version"] = OVS_DATAPATH_VERSION
        else:
            msg["version"] = int(versionStr.split(":")[0], 0)
        msg["reserved"] = 0
        msg["dpifindex"] = 0
        msg["attrs"].append(["OVS_DP_ATTR_NAME", dpname])

        dpfeatures = 0
        if versionStr is not None and versionStr.find(":") != -1:
            dpfeatures = int(versionStr.split(":")[1], 0)
        else:
            dpfeatures = OvsDatapath.OVS_DP_F_VPORT_PIDS

        msg["attrs"].append(["OVS_DP_ATTR_USER_FEATURES", dpfeatures])
        if not shouldUpcall:
            msg["attrs"].append(["OVS_DP_ATTR_UPCALL_PID", 0])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == errno.EEXIST:
                reply = None
            else:
                raise ne

        return reply

    def destroy(self, dpname):
        msg = OvsDatapath.dp_cmd_msg()
        msg["cmd"] = OVS_DP_CMD_DEL
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = 0
        msg["attrs"].append(["OVS_DP_ATTR_NAME", dpname])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == errno.ENODEV:
                reply = None
            else:
                raise ne

        return reply


class OvsVport(GenericNetlinkSocket):
    class ovs_vport_msg(ovs_dp_msg):
        nla_map = (
            ("OVS_VPORT_ATTR_UNSPEC", "none"),
            ("OVS_VPORT_ATTR_PORT_NO", "uint32"),
            ("OVS_VPORT_ATTR_TYPE", "uint32"),
            ("OVS_VPORT_ATTR_NAME", "asciiz"),
            ("OVS_VPORT_ATTR_OPTIONS", "none"),
            ("OVS_VPORT_ATTR_UPCALL_PID", "array(uint32)"),
            ("OVS_VPORT_ATTR_STATS", "vportstats"),
            ("OVS_VPORT_ATTR_PAD", "none"),
            ("OVS_VPORT_ATTR_IFINDEX", "uint32"),
            ("OVS_VPORT_ATTR_NETNSID", "uint32"),
        )

        class vportstats(nla):
            fields = (
                ("rx_packets", "=Q"),
                ("tx_packets", "=Q"),
                ("rx_bytes", "=Q"),
                ("tx_bytes", "=Q"),
                ("rx_errors", "=Q"),
                ("tx_errors", "=Q"),
                ("rx_dropped", "=Q"),
                ("tx_dropped", "=Q"),
            )

    def type_to_str(vport_type):
        if vport_type == 1:
            return "netdev"
        elif vport_type == 2:
            return "internal"
        elif vport_type == 3:
            return "gre"
        elif vport_type == 4:
            return "vxlan"
        elif vport_type == 5:
            return "geneve"
        return "unknown:%d" % vport_type

    def __init__(self):
        GenericNetlinkSocket.__init__(self)
        self.bind(OVS_VPORT_FAMILY, OvsVport.ovs_vport_msg)

    def info(self, vport_name, dpifindex=0, portno=None):
        msg = OvsVport.ovs_vport_msg()

        msg["cmd"] = OVS_VPORT_CMD_GET
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpifindex

        if portno is None:
            msg["attrs"].append(["OVS_VPORT_ATTR_NAME", vport_name])
        else:
            msg["attrs"].append(["OVS_VPORT_ATTR_PORT_NO", portno])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == errno.ENODEV:
                reply = None
            else:
                raise ne
        return reply


def print_ovsdp_full(dp_lookup_rep, ifindex, ndb=NDB()):
    dp_name = dp_lookup_rep.get_attr("OVS_DP_ATTR_NAME")
    base_stats = dp_lookup_rep.get_attr("OVS_DP_ATTR_STATS")
    megaflow_stats = dp_lookup_rep.get_attr("OVS_DP_ATTR_MEGAFLOW_STATS")
    user_features = dp_lookup_rep.get_attr("OVS_DP_ATTR_USER_FEATURES")
    masks_cache_size = dp_lookup_rep.get_attr("OVS_DP_ATTR_MASKS_CACHE_SIZE")

    print("%s:" % dp_name)
    print(
        "  lookups: hit:%d missed:%d lost:%d"
        % (base_stats["hit"], base_stats["missed"], base_stats["lost"])
    )
    print("  flows:%d" % base_stats["flows"])
    pkts = base_stats["hit"] + base_stats["missed"]
    avg = (megaflow_stats["mask_hit"] / pkts) if pkts != 0 else 0.0
    print(
        "  masks: hit:%d total:%d hit/pkt:%f"
        % (megaflow_stats["mask_hit"], megaflow_stats["masks"], avg)
    )
    print("  caches:")
    print("    masks-cache: size:%d" % masks_cache_size)

    if user_features is not None:
        print("  features: 0x%X" % user_features)

    # port print out
    vpl = OvsVport()
    for iface in ndb.interfaces:
        rep = vpl.info(iface.ifname, ifindex)
        if rep is not None:
            print(
                "  port %d: %s (%s)"
                % (
                    rep.get_attr("OVS_VPORT_ATTR_PORT_NO"),
                    rep.get_attr("OVS_VPORT_ATTR_NAME"),
                    OvsVport.type_to_str(rep.get_attr("OVS_VPORT_ATTR_TYPE")),
                )
            )


def main(argv):
    # version check for pyroute2
    prverscheck = pyroute2.__version__.split(".")
    if int(prverscheck[0]) == 0 and int(prverscheck[1]) < 6:
        print("Need to upgrade the python pyroute2 package to >= 0.6.")
        sys.exit(0)

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        help="Increment 'verbose' output counter.",
    )
    subparsers = parser.add_subparsers()

    showdpcmd = subparsers.add_parser("show")
    showdpcmd.add_argument(
        "showdp", metavar="N", type=str, nargs="?", help="Datapath Name"
    )

    adddpcmd = subparsers.add_parser("add-dp")
    adddpcmd.add_argument("adddp", help="Datapath Name")
    adddpcmd.add_argument(
        "-u",
        "--upcall",
        action="store_true",
        help="Leave open a reader for upcalls",
    )
    adddpcmd.add_argument(
        "-V",
        "--versioning",
        required=False,
        help="Specify a custom version / feature string",
    )

    deldpcmd = subparsers.add_parser("del-dp")
    deldpcmd.add_argument("deldp", help="Datapath Name")

    args = parser.parse_args()

    ovsdp = OvsDatapath()
    ndb = NDB()

    if hasattr(args, "showdp"):
        found = False
        for iface in ndb.interfaces:
            rep = None
            if args.showdp is None:
                rep = ovsdp.info(iface.ifname, 0)
            elif args.showdp == iface.ifname:
                rep = ovsdp.info(iface.ifname, 0)

            if rep is not None:
                found = True
                print_ovsdp_full(rep, iface.index, ndb)

        if not found:
            msg = "No DP found"
            if args.showdp is not None:
                msg += ":'%s'" % args.showdp
            print(msg)
    elif hasattr(args, "adddp"):
        rep = ovsdp.create(args.adddp, args.upcall, args.versioning)
        if rep is None:
            print("DP '%s' already exists" % args.adddp)
        else:
            print("DP '%s' added" % args.adddp)
    elif hasattr(args, "deldp"):
        ovsdp.destroy(args.deldp)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
