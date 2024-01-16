#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from subprocess import PIPE, Popen
import json
import time
import argparse
import collections
import sys

#
# Test port split configuration using devlink-port lanes attribute.
# The test is skipped in case the attribute is not available.
#
# First, check that all the ports with 1 lane fail to split.
# Second, check that all the ports with more than 1 lane can be split
# to all valid configurations (e.g., split to 2, split to 4 etc.)
#


# Kselftest framework requirement - SKIP code is 4
KSFT_SKIP=4
Port = collections.namedtuple('Port', 'bus_info name')


def run_command(cmd, should_fail=False):
    """
    Run a command in subprocess.
    Return: Tuple of (stdout, stderr).
    """

    p = Popen(cmd, stdout=PIPE, stderr=PIPE, shell=True)
    stdout, stderr = p.communicate()
    stdout, stderr = stdout.decode(), stderr.decode()

    if stderr != "" and not should_fail:
        print("Error sending command: %s" % cmd)
        print(stdout)
        print(stderr)
    return stdout, stderr


class devlink_ports(object):
    """
    Class that holds information on the devlink ports, required to the tests;
    if_names: A list of interfaces in the devlink ports.
    """

    def get_if_names(dev):
        """
        Get a list of physical devlink ports.
        Return: Array of tuples (bus_info/port, if_name).
        """

        arr = []

        cmd = "devlink -j port show"
        stdout, stderr = run_command(cmd)
        assert stderr == ""
        ports = json.loads(stdout)['port']

        validate_devlink_output(ports, 'flavour')

        for port in ports:
            if dev in port:
                if ports[port]['flavour'] == 'physical':
                    arr.append(Port(bus_info=port, name=ports[port]['netdev']))

        return arr

    def __init__(self, dev):
        self.if_names = devlink_ports.get_if_names(dev)


def get_max_lanes(port):
    """
    Get the $port's maximum number of lanes.
    Return: number of lanes, e.g. 1, 2, 4 and 8.
    """

    cmd = "devlink -j port show %s" % port
    stdout, stderr = run_command(cmd)
    assert stderr == ""
    values = list(json.loads(stdout)['port'].values())[0]

    if 'lanes' in values:
        lanes = values['lanes']
    else:
        lanes = 0
    return lanes


def get_split_ability(port):
    """
    Get the $port split ability.
    Return: split ability, true or false.
    """

    cmd = "devlink -j port show %s" % port.name
    stdout, stderr = run_command(cmd)
    assert stderr == ""
    values = list(json.loads(stdout)['port'].values())[0]

    return values['splittable']


def split(k, port, should_fail=False):
    """
    Split $port into $k ports.
    If should_fail == True, the split should fail. Otherwise, should pass.
    Return: Array of sub ports after splitting.
            If the $port wasn't split, the array will be empty.
    """

    cmd = "devlink port split %s count %s" % (port.bus_info, k)
    stdout, stderr = run_command(cmd, should_fail=should_fail)

    if should_fail:
        if not test(stderr != "", "%s is unsplittable" % port.name):
            print("split an unsplittable port %s" % port.name)
            return create_split_group(port, k)
    else:
        if stderr == "":
            return create_split_group(port, k)
        print("didn't split a splittable port %s" % port.name)

    return []


def unsplit(port):
    """
    Unsplit $port.
    """

    cmd = "devlink port unsplit %s" % port
    stdout, stderr = run_command(cmd)
    test(stderr == "", "Unsplit port %s" % port)


def exists(port, dev):
    """
    Check if $port exists in the devlink ports.
    Return: True is so, False otherwise.
    """

    return any(dev_port.name == port
               for dev_port in devlink_ports.get_if_names(dev))


def exists_and_lanes(ports, lanes, dev):
    """
    Check if every port in the list $ports exists in the devlink ports and has
    $lanes number of lanes after splitting.
    Return: True if both are True, False otherwise.
    """

    for port in ports:
        max_lanes = get_max_lanes(port)
        if not exists(port, dev):
            print("port %s doesn't exist in devlink ports" % port)
            return False
        if max_lanes != lanes:
            print("port %s has %d lanes, but %s were expected"
                  % (port, lanes, max_lanes))
            return False
    return True


def test(cond, msg):
    """
    Check $cond and print a message accordingly.
    Return: True is pass, False otherwise.
    """

    if cond:
        print("TEST: %-60s [ OK ]" % msg)
    else:
        print("TEST: %-60s [FAIL]" % msg)

    return cond


def create_split_group(port, k):
    """
    Create the split group for $port.
    Return: Array with $k elements, which are the split port group.
    """

    return list(port.name + "s" + str(i) for i in range(k))


def split_unsplittable_port(port, k):
    """
    Test that splitting of unsplittable port fails.
    """

    # split to max
    new_split_group = split(k, port, should_fail=True)

    if new_split_group != []:
        unsplit(port.bus_info)


def split_splittable_port(port, k, lanes, dev):
    """
    Test that splitting of splittable port passes correctly.
    """

    new_split_group = split(k, port)

    # Once the split command ends, it takes some time to the sub ifaces'
    # to get their names. Use udevadm to continue only when all current udev
    # events are handled.
    cmd = "udevadm settle"
    stdout, stderr = run_command(cmd)
    assert stderr == ""

    if new_split_group != []:
        test(exists_and_lanes(new_split_group, lanes/k, dev),
             "split port %s into %s" % (port.name, k))

    unsplit(port.bus_info)


def validate_devlink_output(devlink_data, target_property=None):
    """
    Determine if test should be skipped by checking:
      1. devlink_data contains values
      2. The target_property exist in devlink_data
    """
    skip_reason = None
    if any(devlink_data.values()):
        if target_property:
            skip_reason = "{} not found in devlink output, test skipped".format(target_property)
            for key in devlink_data:
                if target_property in devlink_data[key]:
                    skip_reason = None
    else:
        skip_reason = 'devlink output is empty, test skipped'

    if skip_reason:
        print(skip_reason)
        sys.exit(KSFT_SKIP)


def make_parser():
    parser = argparse.ArgumentParser(description='A test for port splitting.')
    parser.add_argument('--dev',
                        help='The devlink handle of the device under test. ' +
                             'The default is the first registered devlink ' +
                             'handle.')

    return parser


def main(cmdline=None):
    parser = make_parser()
    args = parser.parse_args(cmdline)

    dev = args.dev
    if not dev:
        cmd = "devlink -j dev show"
        stdout, stderr = run_command(cmd)
        assert stderr == ""

        validate_devlink_output(json.loads(stdout))
        devs = json.loads(stdout)['dev']
        dev = list(devs.keys())[0]

    cmd = "devlink dev show %s" % dev
    stdout, stderr = run_command(cmd)
    if stderr != "":
        print("devlink device %s can not be found" % dev)
        sys.exit(1)

    ports = devlink_ports(dev)

    found_max_lanes = False
    for port in ports.if_names:
        max_lanes = get_max_lanes(port.name)

        # If max lanes is 0, do not test port splitting at all
        if max_lanes == 0:
            continue

        # If 1 lane, shouldn't be able to split
        elif max_lanes == 1:
            test(not get_split_ability(port),
                 "%s should not be able to split" % port.name)
            split_unsplittable_port(port, max_lanes)

        # Else, splitting should pass and all the split ports should exist.
        else:
            lane = max_lanes
            test(get_split_ability(port),
                 "%s should be able to split" % port.name)
            while lane > 1:
                split_splittable_port(port, lane, max_lanes, dev)

                lane //= 2
        found_max_lanes = True

    if not found_max_lanes:
        print(f"Test not started, no port of device {dev} reports max_lanes")
        sys.exit(KSFT_SKIP)


if __name__ == "__main__":
    main()
