#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Tests for the nlctrl genetlink family (family info and policy dumps).
"""

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, ksft_ge, ksft_true, ksft_in, ksft_not_in
from lib.py import NetdevFamily, EthtoolFamily, NlctrlFamily


def getfamily_do(ctrl) -> None:
    """Query a single family by name and validate its ops."""
    fam = ctrl.getfamily({'family-name': 'netdev'})
    ksft_eq(fam['family-name'], 'netdev')
    ksft_true(fam['family-id'] > 0)

    # The format of ops is quite odd, [{$idx: {"id"...}}, {$idx: {"id"...}}]
    # Discard the indices and re-key by command id.
    ops_by_id = {v['id']: v for op in fam['ops'] for v in op.values()}
    ksft_eq(len(ops_by_id), len(fam['ops']))

    # All ops should have a policy (either do or dump has one)
    for op in ops_by_id.values():
        ksft_in('cmd-cap-haspol', op['flags'],
                comment=f"op {op['id']} missing haspol")

    # dev-get (id 1) should support both do and dump
    ksft_in('cmd-cap-do', ops_by_id[1]['flags'])
    ksft_in('cmd-cap-dump', ops_by_id[1]['flags'])

    # qstats-get (id 12) is dump-only
    ksft_not_in('cmd-cap-do', ops_by_id[12]['flags'])
    ksft_in('cmd-cap-dump', ops_by_id[12]['flags'])

    # napi-set (id 14) is do-only and requires admin
    ksft_in('cmd-cap-do', ops_by_id[14]['flags'])
    ksft_not_in('cmd-cap-dump', ops_by_id[14]['flags'])
    ksft_in('admin-perm', ops_by_id[14]['flags'])

    # Notification-only commands (dev-add/del/change-ntf etc.) must
    # not appear in the ops list since they have no do/dump handlers.
    for ntf_id in [2, 3, 4, 6, 7, 8]:
        ksft_not_in(ntf_id, ops_by_id,
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
    ksft_in('ifindex', pol.attrs,
            comment="dev-get do policy should have ifindex")
    ksft_eq(pol.attrs.get('ifindex', {}).get('type'), 'u32')

    pol_dump = ndev.get_policy('dev-get', 'dump')
    ksft_eq(len(pol_dump.attrs), 0,
            comment="dev-get should not accept any attrs")

    # napi-get: both do and dump have real policies
    pol_do = ndev.get_policy('napi-get', 'do')
    ksft_ge(len(pol_do.attrs), 1)

    pol_dump = ndev.get_policy('napi-get', 'dump')
    ksft_ge(len(pol_dump.attrs), 1)

    # -- ethtool (full ops) --
    et = EthtoolFamily()

    # strset-get (has both do and dump, full ops share policy)
    pol_do = et.get_policy('strset-get', 'do')
    ksft_ge(len(pol_do.attrs), 1, comment="strset-get should have a do policy")

    pol_dump = et.get_policy('strset-get', 'dump')
    ksft_ge(len(pol_dump.attrs), 1,
            comment="strset-get should have a dump policy")

    # Same policy means same attribute names
    ksft_eq(set(pol_do.attrs.keys()), set(pol_dump.attrs.keys()))

    # linkinfo-set is do-only (SET command), no dump
    pol_do = et.get_policy('linkinfo-set', 'do')
    ksft_ge(len(pol_do.attrs), 1,
            comment="linkinfo-set should have a do policy")

    pol_dump = et.get_policy('linkinfo-set', 'dump')
    ksft_eq(pol_dump, None,
            comment="linkinfo-set should not have a dump policy")


def getpolicy_by_op(_ctrl) -> None:
    """Query policy for specific ops, check attr names are resolved."""
    ndev = NetdevFamily()

    # dev-get do policy should have named attributes from the spec
    pol = ndev.get_policy('dev-get', 'do')
    ksft_ge(len(pol.attrs), 1)
    # All attr names should be resolved (no 'attr-N' fallbacks)
    for name in pol.attrs:
        ksft_true(not name.startswith('attr-'),
                  comment=f"unresolved attr name: {name}")


def main() -> None:
    """ Ksft boiler plate main """
    ctrl = NlctrlFamily()
    ksft_run([getfamily_do,
              getfamily_dump,
              getpolicy_dump,
              getpolicy_by_op],
             args=(ctrl, ))
    ksft_exit()


if __name__ == "__main__":
    main()
