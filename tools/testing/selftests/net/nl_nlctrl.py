#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Tests for the nlctrl genetlink family (family info and policy dumps).
"""

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, ksft_ge, ksft_true, ksft_in, ksft_not_in
from lib.py import NetdevFamily, EthtoolFamily, NlctrlFamily

# Families we can expect to always be around, and which between them
# cover ops with a do, with a dump, and with both.
FAMILIES = ('nlctrl', 'netdev')


def _get_ops(ctrl, name):
    """Get the ops of a family, keyed by command id."""
    fam = ctrl.getfamily({'family-name': name})
    ksft_eq(fam['family-name'], name)
    ksft_true(fam['family-id'] > 0)

    # The format of ops is quite odd, [{$idx: {"id"...}}, {$idx: {"id"...}}]
    # Discard the indices and re-key by command id.
    ops_by_id = {v['id']: v for op in fam['ops'] for v in op.values()}
    ksft_eq(len(ops_by_id), len(fam['ops']),
            comment=f"{name} lists a command twice")
    return ops_by_id


def _get_policy_map(ctrl, req):
    """
    The policy map in the Netlink replies looks like this:

         [{'family-id': 16, 'op-policy': {'do': 0, 'dump': 0, 'op-id': 3}},
          {'family-id': 16, 'op-policy': {'dump': 1, 'op-id': 4}}, ...]

    Return the mapping:

         {3:{'do','dump'}, 4:{'dump'}}

    The policy itself is discarded here, only return which command has policy.
    """
    pol_map = {}
    for msg in ctrl.getpolicy(req, dump=True):
        if 'op-policy' not in msg:
            continue
        modes = dict(msg['op-policy'])
        cmd = modes.pop('op-id')
        ksft_not_in(cmd, pol_map, comment=f"command {cmd} reported twice")
        pol_map[cmd] = set(modes.keys())
    return pol_map


def getfamily_do(ctrl) -> None:
    """Query single families by name and validate their ops."""
    ops = {name: _get_ops(ctrl, name) for name in FAMILIES}

    for name, ops_by_id in ops.items():
        for op in ops_by_id.values():
            # All ops in nlctrl and netdev have a policy
            ksft_in('cmd-cap-haspol', op['flags'],
                    comment=f"{name} op {op['id']} missing haspol")
            ksft_true(op['flags'] & {'cmd-cap-do', 'cmd-cap-dump'},
                      comment=f"{name} op {op['id']} has no handler")

    # nlctrl getfamily (id 3) does both, getpolicy (id 10) is dump-only
    ksft_in('cmd-cap-do', ops['nlctrl'][3]['flags'])
    ksft_in('cmd-cap-dump', ops['nlctrl'][3]['flags'])
    ksft_not_in('cmd-cap-do', ops['nlctrl'][10]['flags'])
    ksft_in('cmd-cap-dump', ops['nlctrl'][10]['flags'])

    netdev = ops['netdev']

    # dev-get (id 1) should support both do and dump
    ksft_in('cmd-cap-do', netdev[1]['flags'])
    ksft_in('cmd-cap-dump', netdev[1]['flags'])

    # qstats-get (id 12) is dump-only
    ksft_not_in('cmd-cap-do', netdev[12]['flags'])
    ksft_in('cmd-cap-dump', netdev[12]['flags'])

    # napi-set (id 14) is do-only and requires admin
    ksft_in('cmd-cap-do', netdev[14]['flags'])
    ksft_not_in('cmd-cap-dump', netdev[14]['flags'])
    ksft_in('admin-perm', netdev[14]['flags'])

    # Notification-only commands (dev-add/del/change-ntf etc.) must
    # not appear in the ops list since they have no do/dump handlers.
    for ntf_id in [2, 3, 4, 6, 7, 8]:
        ksft_not_in(ntf_id, netdev,
                    comment=f"ntf-only cmd {ntf_id} should not be in ops")


def getfamily_dump(ctrl) -> None:
    """Dump all families and verify expected entries."""
    families = ctrl.getfamily({}, dump=True)
    ksft_ge(len(families), 2)

    names = [f['family-name'] for f in families]
    ksft_in('nlctrl', names, comment="nlctrl not found in family dump")
    ksft_in('netdev', names, comment="netdev not found in family dump")


def getpolicy_dump(_ctrl) -> None:
    """Dump policies for ops using get_policy() and validate results.

    Test with netdev (split ops) where do and dump can have different
    policies, and with ethtool (full ops) where they always share one.
    """
    # -- netdev (split ops) --
    ndev = NetdevFamily()

    # dev-get: do has a real policy with ifindex, dump has no policy
    # (only the reject-all policy with maxattr=0)
    pol = ndev.get_policy('dev-get', 'do')
    ksft_in('ifindex', pol, comment="dev-get do policy should have ifindex")
    ksft_eq(pol['ifindex'].type, 'u32')

    pol_dump = ndev.get_policy('dev-get', 'dump')
    ksft_eq(len(pol_dump), 0, comment="dev-get should not accept any attrs")

    # napi-get: both do and dump have real policies
    pol_do = ndev.get_policy('napi-get', 'do')
    ksft_ge(len(pol_do), 1)

    pol_dump = ndev.get_policy('napi-get', 'dump')
    ksft_ge(len(pol_dump), 1)

    # -- ethtool (full ops) --
    et = EthtoolFamily()

    # strset-get (has both do and dump, full ops share policy)
    pol_do = et.get_policy('strset-get', 'do')
    ksft_ge(len(pol_do), 1, comment="strset-get should have a do policy")

    pol_dump = et.get_policy('strset-get', 'dump')
    ksft_ge(len(pol_dump), 1, comment="strset-get should have a dump policy")

    # Same policy means same attribute names
    ksft_eq(set(pol_do.keys()), set(pol_dump.keys()))

    # linkinfo-set is do-only (SET command), no dump
    pol_do = et.get_policy('linkinfo-set', 'do')
    ksft_ge(len(pol_do), 1, comment="linkinfo-set should have a do policy")

    pol_dump = et.get_policy('linkinfo-set', 'dump')
    ksft_eq(pol_dump, None,
            comment="linkinfo-set should not have a dump policy")


def getpolicy_op_map(ctrl) -> None:
    """Check the op-to-policy map consistency. Each op with 'haspol' flag
    has to have a policy. The policy back-references must name only
    real ops that exist, have given modes (do vs dump) and have 'haspol'.
    """
    for name in FAMILIES:
        ops_by_id = _get_ops(ctrl, name)
        haspol = {cmd for cmd, op in ops_by_id.items()
                  if 'cmd-cap-haspol' in op['flags']}

        pol_map = _get_policy_map(ctrl, {'family-name': name})
        ksft_eq(set(pol_map), haspol,
                comment=f"{name} policy map does not match the op list")

        # Walk the op list rather than the map, the map may be missing
        # the very op we are after. Asking for a command the family does
        # not have is an error, so it must not come from the map either.
        for cmd in sorted(haspol):
            modes = pol_map.get(cmd, set())

            # The kernel only reports a mode the op actually has.
            if 'do' in modes:
                ksft_in('cmd-cap-do', ops_by_id[cmd]['flags'],
                        comment=f"{name} cmd {cmd} has no do")
            if 'dump' in modes:
                ksft_in('cmd-cap-dump', ops_by_id[cmd]['flags'],
                        comment=f"{name} cmd {cmd} has no dump")

            # Asking for one op builds the map in a different place in
            # the kernel, it has to report what the full dump did.
            single = _get_policy_map(ctrl, {'family-name': name, 'op': cmd})
            ksft_eq(single, {cmd: modes},
                    comment=f"{name} cmd {cmd} policy differs from the dump")


def getpolicy_by_op(_ctrl) -> None:
    """Query policy for specific ops, check attr names are resolved."""
    ndev = NetdevFamily()

    # dev-get do policy should have named attributes from the spec
    pol = ndev.get_policy('dev-get', 'do')
    ksft_ge(len(pol), 1)
    # All attr names should be resolved (no 'attr-N' fallbacks)
    for name in pol:
        ksft_true(not name.startswith('attr-'),
                  comment=f"unresolved attr name: {name}")


def main() -> None:
    """ Ksft boiler plate main """
    ctrl = NlctrlFamily()
    ksft_run([getfamily_do,
              getfamily_dump,
              getpolicy_dump,
              getpolicy_op_map,
              getpolicy_by_op],
             args=(ctrl, ))
    ksft_exit()


if __name__ == "__main__":
    main()
