#!/usr/bin/python
# SPDX-License-Identifier: GPL-2.0

import subprocess
import json as j
import random


class SkipTest(Exception):
    pass


class RandomValuePicker:
    """
    Class for storing shared buffer configuration. Can handle 3 different
    objects, pool, tcbind and portpool. Provide an interface to get random
    values for a specific object type as the follow:
      1. Pool:
         - random size

      2. TcBind:
         - random pool number
         - random threshold

      3. PortPool:
         - random threshold
    """
    def __init__(self, pools):
        self._pools = []
        for pool in pools:
            self._pools.append(pool)

    def _cell_size(self):
        return self._pools[0]["cell_size"]

    def _get_static_size(self, th):
        # For threshold of 16, this works out to be about 12MB on Spectrum-1,
        # and about 17MB on Spectrum-2.
        return th * 8000 * self._cell_size()

    def _get_size(self):
        return self._get_static_size(16)

    def _get_thtype(self):
        return "static"

    def _get_th(self, pool):
        # Threshold value could be any integer between 3 to 16
        th = random.randint(3, 16)
        if pool["thtype"] == "dynamic":
            return th
        else:
            return self._get_static_size(th)

    def _get_pool(self, direction):
        ing_pools = []
        egr_pools = []
        for pool in self._pools:
            if pool["type"] == "ingress":
                ing_pools.append(pool)
            else:
                egr_pools.append(pool)
        if direction == "ingress":
            arr = ing_pools
        else:
            arr = egr_pools
        return arr[random.randint(0, len(arr) - 1)]

    def get_value(self, objid):
        if isinstance(objid, Pool):
            if objid["pool"] in [4, 8, 9, 10]:
                # The threshold type of pools 4, 8, 9 and 10 cannot be changed
                raise SkipTest()
            else:
                return (self._get_size(), self._get_thtype())
        if isinstance(objid, TcBind):
            if objid["tc"] >= 8:
                # Multicast TCs cannot be changed
                raise SkipTest()
            else:
                pool = self._get_pool(objid["type"])
                th = self._get_th(pool)
                pool_n = pool["pool"]
                return (pool_n, th)
        if isinstance(objid, PortPool):
            pool_n = objid["pool"]
            pool = self._pools[pool_n]
            assert pool["pool"] == pool_n
            th = self._get_th(pool)
            return (th,)


class RecordValuePickerException(Exception):
    pass


class RecordValuePicker:
    """
    Class for storing shared buffer configuration. Can handle 2 different
    objects, pool and tcbind. Provide an interface to get the stored values per
    object type.
    """
    def __init__(self, objlist):
        self._recs = []
        for item in objlist:
            self._recs.append({"objid": item, "value": item.var_tuple()})

    def get_value(self, objid):
        if isinstance(objid, Pool) and objid["pool"] in [4, 8, 9, 10]:
            # The threshold type of pools 4, 8, 9 and 10 cannot be changed
            raise SkipTest()
        if isinstance(objid, TcBind) and objid["tc"] >= 8:
            # Multicast TCs cannot be changed
            raise SkipTest()
        for rec in self._recs:
            if rec["objid"].weak_eq(objid):
                return rec["value"]
        raise RecordValuePickerException()


def run_cmd(cmd, json=False):
    out = subprocess.check_output(cmd, shell=True)
    if json:
        return j.loads(out)
    return out


def run_json_cmd(cmd):
    return run_cmd(cmd, json=True)


def log_test(test_name, err_msg=None):
    if err_msg:
        print("\t%s" % err_msg)
        print("TEST: %-80s  [FAIL]" % test_name)
    else:
        print("TEST: %-80s  [ OK ]" % test_name)


class CommonItem(dict):
    varitems = []

    def var_tuple(self):
        ret = []
        self.varitems.sort()
        for key in self.varitems:
            ret.append(self[key])
        return tuple(ret)

    def weak_eq(self, other):
        for key in self:
            if key in self.varitems:
                continue
            if self[key] != other[key]:
                return False
        return True


class CommonList(list):
    def get_by(self, by_obj):
        for item in self:
            if item.weak_eq(by_obj):
                return item
        return None

    def del_by(self, by_obj):
        for item in self:
            if item.weak_eq(by_obj):
                self.remove(item)


class Pool(CommonItem):
    varitems = ["size", "thtype"]

    def dl_set(self, dlname, size, thtype):
        run_cmd("devlink sb pool set {} sb {} pool {} size {} thtype {}".format(dlname, self["sb"],
                                                                                self["pool"],
                                                                                size, thtype))


class PoolList(CommonList):
    pass


def get_pools(dlname, direction=None):
    d = run_json_cmd("devlink sb pool show -j")
    pools = PoolList()
    for pooldict in d["pool"][dlname]:
        if not direction or direction == pooldict["type"]:
            pools.append(Pool(pooldict))
    return pools


def do_check_pools(dlname, pools, vp):
    for pool in pools:
        pre_pools = get_pools(dlname)
        try:
            (size, thtype) = vp.get_value(pool)
        except SkipTest:
            continue
        pool.dl_set(dlname, size, thtype)
        post_pools = get_pools(dlname)
        pool = post_pools.get_by(pool)

        err_msg = None
        if pool["size"] != size:
            err_msg = "Incorrect pool size (got {}, expected {})".format(pool["size"], size)
        if pool["thtype"] != thtype:
            err_msg = "Incorrect pool threshold type (got {}, expected {})".format(pool["thtype"], thtype)

        pre_pools.del_by(pool)
        post_pools.del_by(pool)
        if pre_pools != post_pools:
            err_msg = "Other pool setup changed as well"
        log_test("pool {} of sb {} set verification".format(pool["pool"],
                                                            pool["sb"]), err_msg)


def check_pools(dlname, pools):
    # Save defaults
    record_vp = RecordValuePicker(pools)

    # For each pool, set random size and static threshold type
    do_check_pools(dlname, pools, RandomValuePicker(pools))

    # Restore defaults
    do_check_pools(dlname, pools, record_vp)


class TcBind(CommonItem):
    varitems = ["pool", "threshold"]

    def __init__(self, port, d):
        super(TcBind, self).__init__(d)
        self["dlportname"] = port.name

    def dl_set(self, pool, th):
        run_cmd("devlink sb tc bind set {} sb {} tc {} type {} pool {} th {}".format(self["dlportname"],
                                                                                     self["sb"],
                                                                                     self["tc"],
                                                                                     self["type"],
                                                                                     pool, th))


class TcBindList(CommonList):
    pass


def get_tcbinds(ports, verify_existence=False):
    d = run_json_cmd("devlink sb tc bind show -j -n")
    tcbinds = TcBindList()
    for port in ports:
        err_msg = None
        if port.name not in d["tc_bind"] or len(d["tc_bind"][port.name]) == 0:
            err_msg = "No tc bind for port"
        else:
            for tcbinddict in d["tc_bind"][port.name]:
                tcbinds.append(TcBind(port, tcbinddict))
        if verify_existence:
            log_test("tc bind existence for port {} verification".format(port.name), err_msg)
    return tcbinds


def do_check_tcbind(ports, tcbinds, vp):
    for tcbind in tcbinds:
        pre_tcbinds = get_tcbinds(ports)
        try:
            (pool, th) = vp.get_value(tcbind)
        except SkipTest:
            continue
        tcbind.dl_set(pool, th)
        post_tcbinds = get_tcbinds(ports)
        tcbind = post_tcbinds.get_by(tcbind)

        err_msg = None
        if tcbind["pool"] != pool:
            err_msg = "Incorrect pool (got {}, expected {})".format(tcbind["pool"], pool)
        if tcbind["threshold"] != th:
            err_msg = "Incorrect threshold (got {}, expected {})".format(tcbind["threshold"], th)

        pre_tcbinds.del_by(tcbind)
        post_tcbinds.del_by(tcbind)
        if pre_tcbinds != post_tcbinds:
            err_msg = "Other tc bind setup changed as well"
        log_test("tc bind {}-{} of sb {} set verification".format(tcbind["dlportname"],
                                                                  tcbind["tc"],
                                                                  tcbind["sb"]), err_msg)


def check_tcbind(dlname, ports, pools):
    tcbinds = get_tcbinds(ports, verify_existence=True)

    # Save defaults
    record_vp = RecordValuePicker(tcbinds)

    # Bind each port and unicast TC (TCs < 8) to a random pool and a random
    # threshold
    do_check_tcbind(ports, tcbinds, RandomValuePicker(pools))

    # Restore defaults
    do_check_tcbind(ports, tcbinds, record_vp)


class PortPool(CommonItem):
    varitems = ["threshold"]

    def __init__(self, port, d):
        super(PortPool, self).__init__(d)
        self["dlportname"] = port.name

    def dl_set(self, th):
        run_cmd("devlink sb port pool set {} sb {} pool {} th {}".format(self["dlportname"],
                                                                         self["sb"],
                                                                         self["pool"], th))


class PortPoolList(CommonList):
    pass


def get_portpools(ports, verify_existence=False):
    d = run_json_cmd("devlink sb port pool -j -n")
    portpools = PortPoolList()
    for port in ports:
        err_msg = None
        if port.name not in d["port_pool"] or len(d["port_pool"][port.name]) == 0:
            err_msg = "No port pool for port"
        else:
            for portpooldict in d["port_pool"][port.name]:
                portpools.append(PortPool(port, portpooldict))
        if verify_existence:
            log_test("port pool existence for port {} verification".format(port.name), err_msg)
    return portpools


def do_check_portpool(ports, portpools, vp):
    for portpool in portpools:
        pre_portpools = get_portpools(ports)
        (th,) = vp.get_value(portpool)
        portpool.dl_set(th)
        post_portpools = get_portpools(ports)
        portpool = post_portpools.get_by(portpool)

        err_msg = None
        if portpool["threshold"] != th:
            err_msg = "Incorrect threshold (got {}, expected {})".format(portpool["threshold"], th)

        pre_portpools.del_by(portpool)
        post_portpools.del_by(portpool)
        if pre_portpools != post_portpools:
            err_msg = "Other port pool setup changed as well"
        log_test("port pool {}-{} of sb {} set verification".format(portpool["dlportname"],
                                                                    portpool["pool"],
                                                                    portpool["sb"]), err_msg)


def check_portpool(dlname, ports, pools):
    portpools = get_portpools(ports, verify_existence=True)

    # Save defaults
    record_vp = RecordValuePicker(portpools)

    # For each port pool, set a random threshold
    do_check_portpool(ports, portpools, RandomValuePicker(pools))

    # Restore defaults
    do_check_portpool(ports, portpools, record_vp)


class Port:
    def __init__(self, name):
        self.name = name


class PortList(list):
    pass


def get_ports(dlname):
    d = run_json_cmd("devlink port show -j")
    ports = PortList()
    for name in d["port"]:
        if name.find(dlname) == 0 and d["port"][name]["flavour"] == "physical":
            ports.append(Port(name))
    return ports


def get_device():
    devices_info = run_json_cmd("devlink -j dev info")["info"]
    for d in devices_info:
        if "mlxsw_spectrum" in devices_info[d]["driver"]:
            return d
    return None


class UnavailableDevlinkNameException(Exception):
    pass


def test_sb_configuration():
    # Use static seed
    random.seed(0)

    dlname = get_device()
    if not dlname:
        raise UnavailableDevlinkNameException()

    ports = get_ports(dlname)
    pools = get_pools(dlname)

    check_pools(dlname, pools)
    check_tcbind(dlname, ports, pools)
    check_portpool(dlname, ports, pools)


test_sb_configuration()
