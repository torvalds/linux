# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) NXP 2019

import gdb

from linux.utils import CachedType
from linux.utils import container_of
from linux.lists import list_for_each_entry


device_private_type = CachedType('struct device_private')
device_type = CachedType('struct device')

subsys_private_type = CachedType('struct subsys_private')
kobject_type = CachedType('struct kobject')
kset_type = CachedType('struct kset')

bus_type = CachedType('struct bus_type')
class_type = CachedType('struct class')


def dev_name(dev):
    dev_init_name = dev['init_name']
    if dev_init_name:
        return dev_init_name.string()
    return dev['kobj']['name'].string()


def kset_for_each_object(kset):
    return list_for_each_entry(kset['list'],
            kobject_type.get_type().pointer(), "entry")


def for_each_bus():
    for kobj in kset_for_each_object(gdb.parse_and_eval('bus_kset')):
        subsys = container_of(kobj, kset_type.get_type().pointer(), 'kobj')
        subsys_priv = container_of(subsys, subsys_private_type.get_type().pointer(), 'subsys')
        yield subsys_priv['bus']


def for_each_class():
    for kobj in kset_for_each_object(gdb.parse_and_eval('class_kset')):
        subsys = container_of(kobj, kset_type.get_type().pointer(), 'kobj')
        subsys_priv = container_of(subsys, subsys_private_type.get_type().pointer(), 'subsys')
        yield subsys_priv['class']


def get_bus_by_name(name):
    for item in for_each_bus():
        if item['name'].string() == name:
            return item
    raise gdb.GdbError("Can't find bus type {!r}".format(name))


def get_class_by_name(name):
    for item in for_each_class():
        if item['name'].string() == name:
            return item
    raise gdb.GdbError("Can't find device class {!r}".format(name))


klist_type = CachedType('struct klist')
klist_node_type = CachedType('struct klist_node')


def klist_for_each(klist):
    return list_for_each_entry(klist['k_list'],
                klist_node_type.get_type().pointer(), 'n_node')


def bus_for_each_device(bus):
    for kn in klist_for_each(bus['p']['klist_devices']):
        dp = container_of(kn, device_private_type.get_type().pointer(), 'knode_bus')
        yield dp['device']


def class_for_each_device(cls):
    for kn in klist_for_each(cls['p']['klist_devices']):
        dp = container_of(kn, device_private_type.get_type().pointer(), 'knode_class')
        yield dp['device']


def device_for_each_child(dev):
    for kn in klist_for_each(dev['p']['klist_children']):
        dp = container_of(kn, device_private_type.get_type().pointer(), 'knode_parent')
        yield dp['device']


def _show_device(dev, level=0, recursive=False):
    gdb.write('{}dev {}:\t{}\n'.format('\t' * level, dev_name(dev), dev))
    if recursive:
        for child in device_for_each_child(dev):
            _show_device(child, level + 1, recursive)


class LxDeviceListBus(gdb.Command):
    '''Print devices on a bus (or all buses if not specified)'''

    def __init__(self):
        super(LxDeviceListBus, self).__init__('lx-device-list-bus', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        if not arg:
            for bus in for_each_bus():
                gdb.write('bus {}:\t{}\n'.format(bus['name'].string(), bus))
                for dev in bus_for_each_device(bus):
                    _show_device(dev, level=1)
        else:
            bus = get_bus_by_name(arg)
            if not bus:
                raise gdb.GdbError("Can't find bus {!r}".format(arg))
            for dev in bus_for_each_device(bus):
                _show_device(dev)


class LxDeviceListClass(gdb.Command):
    '''Print devices in a class (or all classes if not specified)'''

    def __init__(self):
        super(LxDeviceListClass, self).__init__('lx-device-list-class', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        if not arg:
            for cls in for_each_class():
                gdb.write("class {}:\t{}\n".format(cls['name'].string(), cls))
                for dev in class_for_each_device(cls):
                    _show_device(dev, level=1)
        else:
            cls = get_class_by_name(arg)
            for dev in class_for_each_device(cls):
                _show_device(dev)


class LxDeviceListTree(gdb.Command):
    '''Print a device and its children recursively'''

    def __init__(self):
        super(LxDeviceListTree, self).__init__('lx-device-list-tree', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        if not arg:
            raise gdb.GdbError('Please provide pointer to struct device')
        dev = gdb.parse_and_eval(arg)
        if dev.type != device_type.get_type().pointer():
            raise gdb.GdbError('Please provide pointer to struct device')
        _show_device(dev, level=0, recursive=True)


class LxDeviceFindByBusName(gdb.Function):
    '''Find struct device by bus and name (both strings)'''

    def __init__(self):
        super(LxDeviceFindByBusName, self).__init__('lx_device_find_by_bus_name')

    def invoke(self, bus, name):
        name = name.string()
        bus = get_bus_by_name(bus.string())
        for dev in bus_for_each_device(bus):
            if dev_name(dev) == name:
                return dev


class LxDeviceFindByClassName(gdb.Function):
    '''Find struct device by class and name (both strings)'''

    def __init__(self):
        super(LxDeviceFindByClassName, self).__init__('lx_device_find_by_class_name')

    def invoke(self, cls, name):
        name = name.string()
        cls = get_class_by_name(cls.string())
        for dev in class_for_each_device(cls):
            if dev_name(dev) == name:
                return dev


LxDeviceListBus()
LxDeviceListClass()
LxDeviceListTree()
LxDeviceFindByBusName()
LxDeviceFindByClassName()
