#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 Collabora Ltd
#
# This script tests for presence and driver binding of devices from discoverable
# buses (ie USB, PCI).
#
# The per-platform YAML file defining the devices to be tested is stored inside
# the boards/ directory and chosen based on DT compatible or DMI IDs (sys_vendor
# and product_name).
#
# See boards/google,spherion.yaml and boards/'Dell Inc.,XPS 13 9300.yaml' for
# the description and examples of the file structure and vocabulary.
#

import argparse
import glob
import os
import re
import sys
import yaml

# Allow ksft module to be imported from different directory
this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(this_dir, "../../kselftest/"))

import ksft

pci_controllers = []
usb_controllers = []

sysfs_usb_devices = "/sys/bus/usb/devices/"


def find_pci_controller_dirs():
    sysfs_devices = "/sys/devices"
    pci_controller_sysfs_dir = "pci[0-9a-f]{4}:[0-9a-f]{2}"

    dir_regex = re.compile(pci_controller_sysfs_dir)
    for path, dirs, _ in os.walk(sysfs_devices):
        for d in dirs:
            if dir_regex.match(d):
                pci_controllers.append(os.path.join(path, d))


def find_usb_controller_dirs():
    usb_controller_sysfs_dir = r"usb[\d]+"

    dir_regex = re.compile(usb_controller_sysfs_dir)
    for d in os.scandir(sysfs_usb_devices):
        if dir_regex.match(d.name):
            usb_controllers.append(os.path.realpath(d.path))


def get_dt_mmio(sysfs_dev_dir):
    re_dt_mmio = re.compile("OF_FULLNAME=.*@([0-9a-f]+)")
    dt_mmio = None

    # PCI controllers' sysfs don't have an of_node, so have to read it from the
    # parent
    while not dt_mmio:
        try:
            with open(os.path.join(sysfs_dev_dir, "uevent")) as f:
                dt_mmio = re_dt_mmio.search(f.read()).group(1)
                return dt_mmio
        except:
            pass
        sysfs_dev_dir = os.path.dirname(sysfs_dev_dir)


def get_of_fullname(sysfs_dev_dir):
    re_of_fullname = re.compile("OF_FULLNAME=(.*)")
    of_full_name = None

    # PCI controllers' sysfs don't have an of_node, so have to read it from the
    # parent
    while not of_full_name:
        try:
            with open(os.path.join(sysfs_dev_dir, "uevent")) as f:
                of_fullname = re_of_fullname.search(f.read()).group(1)
                return of_fullname
        except:
            pass
        sysfs_dev_dir = os.path.dirname(sysfs_dev_dir)


def get_acpi_uid(sysfs_dev_dir):
    with open(os.path.join(sysfs_dev_dir, "firmware_node", "uid")) as f:
        return f.read()


def get_usb_version(sysfs_dev_dir):
    re_usb_version = re.compile(r"PRODUCT=.*/(\d)/.*")
    with open(os.path.join(sysfs_dev_dir, "uevent")) as f:
        return int(re_usb_version.search(f.read()).group(1))


def get_usb_busnum(sysfs_dev_dir):
    re_busnum = re.compile("BUSNUM=(.*)")
    with open(os.path.join(sysfs_dev_dir, "uevent")) as f:
        return int(re_busnum.search(f.read()).group(1))


def find_controller_in_sysfs(controller, parent_sysfs=None):
    if controller["type"] == "pci-controller":
        controllers = pci_controllers
    elif controller["type"] == "usb-controller":
        controllers = usb_controllers

    result_controllers = []

    for c in controllers:
        if parent_sysfs and parent_sysfs not in c:
            continue

        if controller.get("dt-mmio"):
            if str(controller["dt-mmio"]) != get_dt_mmio(c):
                continue

        if controller.get("of-fullname-regex"):
            re_of_fullname = re.compile(str(controller["of-fullname-regex"]))
            if not re_of_fullname.match(get_of_fullname(c)):
                continue

        if controller.get("usb-version"):
            if controller["usb-version"] != get_usb_version(c):
                continue

        if controller.get("acpi-uid"):
            if controller["acpi-uid"] != get_acpi_uid(c):
                continue

        result_controllers.append(c)

    return result_controllers


def is_controller(device):
    return device.get("type") and "controller" in device.get("type")


def path_to_dir(parent_sysfs, dev_type, path):
    if dev_type == "usb-device":
        usb_dev_sysfs_fmt = "{}-{}"
        busnum = get_usb_busnum(parent_sysfs)
        dirname = os.path.join(
            sysfs_usb_devices, usb_dev_sysfs_fmt.format(busnum, path)
        )
        return [os.path.realpath(dirname)]
    else:
        pci_dev_sysfs_fmt = "????:??:{}"
        path_glob = ""
        for dev_func in path.split("/"):
            dev_func = dev_func.zfill(4)
            path_glob = os.path.join(path_glob, pci_dev_sysfs_fmt.format(dev_func))

        dir_list = glob.glob(os.path.join(parent_sysfs, path_glob))

        return dir_list


def find_in_sysfs(device, parent_sysfs=None):
    if parent_sysfs and device.get("path"):
        pathdirs = path_to_dir(
            parent_sysfs, device["meta"]["type"], str(device["path"])
        )
        if len(pathdirs) != 1:
            # Early return to report error
            return pathdirs
        pathdir = pathdirs[0]
        sysfs_path = os.path.join(parent_sysfs, pathdir)
    else:
        sysfs_path = parent_sysfs

    if is_controller(device):
        return find_controller_in_sysfs(device, sysfs_path)
    else:
        return [sysfs_path]


def check_driver_presence(sysfs_dir, current_node):
    if current_node["meta"]["type"] == "usb-device":
        usb_intf_fmt = "*-*:*.{}"

        interfaces = []
        for i in current_node["interfaces"]:
            interfaces.append((i, usb_intf_fmt.format(i)))

        for intf_num, intf_dir_fmt in interfaces:
            test_name = f"{current_node['meta']['pathname']}.{intf_num}.driver"

            intf_dirs = glob.glob(os.path.join(sysfs_dir, intf_dir_fmt))
            if len(intf_dirs) != 1:
                ksft.test_result_fail(test_name)
                continue
            intf_dir = intf_dirs[0]

            driver_link = os.path.join(sysfs_dir, intf_dir, "driver")
            ksft.test_result(os.path.isdir(driver_link), test_name)
    else:
        driver_link = os.path.join(sysfs_dir, "driver")
        test_name = current_node["meta"]["pathname"] + ".driver"
        ksft.test_result(os.path.isdir(driver_link), test_name)


def generate_pathname(device):
    pathname = ""

    if device.get("path"):
        pathname = str(device["path"])

    if device.get("type"):
        dev_type = device["type"]
        if device.get("usb-version"):
            dev_type = dev_type.replace("usb", "usb" + str(device["usb-version"]))
        if device.get("acpi-uid") is not None:
            dev_type = dev_type.replace("pci", "pci" + str(device["acpi-uid"]))
        pathname = pathname + "/" + dev_type

    if device.get("dt-mmio"):
        pathname += "@" + str(device["dt-mmio"])

    if device.get("of-fullname-regex"):
        pathname += "-" + str(device["of-fullname-regex"])

    if device.get("name"):
        pathname = pathname + "/" + device["name"]

    return pathname


def fill_meta_keys(child, parent=None):
    child["meta"] = {}

    if parent:
        child["meta"]["type"] = parent["type"].replace("controller", "device")

    pathname = generate_pathname(child)
    if parent:
        pathname = parent["meta"]["pathname"] + "/" + pathname
    child["meta"]["pathname"] = pathname


def parse_device_tree_node(current_node, parent_sysfs=None):
    if not parent_sysfs:
        fill_meta_keys(current_node)

    sysfs_dirs = find_in_sysfs(current_node, parent_sysfs)
    if len(sysfs_dirs) != 1:
        if len(sysfs_dirs) == 0:
            ksft.test_result_fail(
                f"Couldn't find in sysfs: {current_node['meta']['pathname']}"
            )
        else:
            ksft.test_result_fail(
                f"Found multiple sysfs entries for {current_node['meta']['pathname']}: {sysfs_dirs}"
            )
        return
    sysfs_dir = sysfs_dirs[0]

    if not is_controller(current_node):
        ksft.test_result(
            os.path.exists(sysfs_dir), current_node["meta"]["pathname"] + ".device"
        )
        check_driver_presence(sysfs_dir, current_node)
    else:
        for child_device in current_node["devices"]:
            fill_meta_keys(child_device, current_node)
            parse_device_tree_node(child_device, sysfs_dir)


def count_tests(device_trees):
    test_count = 0

    def parse_node(device):
        nonlocal test_count
        if device.get("devices"):
            for child in device["devices"]:
                parse_node(child)
        else:
            if device.get("interfaces"):
                test_count += len(device["interfaces"])
            else:
                test_count += 1
            test_count += 1

    for device_tree in device_trees:
        parse_node(device_tree)

    return test_count


def get_board_filenames():
    filenames = []

    platform_compatible_file = "/proc/device-tree/compatible"
    if os.path.exists(platform_compatible_file):
        with open(platform_compatible_file) as f:
            for line in f:
                filenames.extend(line.split("\0"))
    else:
        dmi_id_dir = "/sys/devices/virtual/dmi/id"
        vendor_dmi_file = os.path.join(dmi_id_dir, "sys_vendor")
        product_dmi_file = os.path.join(dmi_id_dir, "product_name")

        with open(vendor_dmi_file) as f:
            vendor = f.read().replace("\n", "")
        with open(product_dmi_file) as f:
            product = f.read().replace("\n", "")

        filenames = [vendor + "," + product]

    return filenames


def run_test(yaml_file):
    ksft.print_msg(f"Using board file: {yaml_file}")

    with open(yaml_file) as f:
        device_trees = yaml.safe_load(f)

    ksft.set_plan(count_tests(device_trees))

    for device_tree in device_trees:
        parse_device_tree_node(device_tree)


parser = argparse.ArgumentParser()
parser.add_argument(
    "--boards-dir", default="boards", help="Directory containing the board YAML files"
)
args = parser.parse_args()

find_pci_controller_dirs()
find_usb_controller_dirs()

ksft.print_header()

if not os.path.exists(args.boards_dir):
    ksft.print_msg(f"Boards directory '{args.boards_dir}' doesn't exist")
    ksft.exit_fail()

board_file = ""
for board_filename in get_board_filenames():
    full_board_filename = os.path.join(args.boards_dir, board_filename + ".yaml")

    if os.path.exists(full_board_filename):
        board_file = full_board_filename
        break

if not board_file:
    ksft.print_msg("No matching board file found")
    ksft.exit_fail()

run_test(board_file)

ksft.finished()
