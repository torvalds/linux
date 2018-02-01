#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
tdc.py - Linux tc (Traffic Control) unit test driver

Copyright (C) 2017 Lucas Bates <lucasb@mojatatu.com>
"""

import re
import os
import sys
import argparse
import json
import subprocess
from collections import OrderedDict
from string import Template

from tdc_config import *
from tdc_helper import *


USE_NS = True


def replace_keywords(cmd):
    """
    For a given executable command, substitute any known
    variables contained within NAMES with the correct values
    """
    tcmd = Template(cmd)
    subcmd = tcmd.safe_substitute(NAMES)
    return subcmd


def exec_cmd(command, nsonly=True):
    """
    Perform any required modifications on an executable command, then run
    it in a subprocess and return the results.
    """
    if (USE_NS and nsonly):
        command = 'ip netns exec $NS ' + command

    if '$' in command:
        command = replace_keywords(command)

    proc = subprocess.Popen(command,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    (rawout, serr) = proc.communicate()

    if proc.returncode != 0 and len(serr) > 0:
        foutput = serr.decode("utf-8")
    else:
        foutput = rawout.decode("utf-8")

    proc.stdout.close()
    proc.stderr.close()
    return proc, foutput


def prepare_env(cmdlist):
    """
    Execute the setup/teardown commands for a test case. Optionally
    terminate test execution if the command fails.
    """
    for cmdinfo in cmdlist:
        if (type(cmdinfo) == list):
            exit_codes = cmdinfo[1:]
            cmd = cmdinfo[0]
        else:
            exit_codes = [0]
            cmd = cmdinfo

        if (len(cmd) == 0):
            continue

        (proc, foutput) = exec_cmd(cmd)

        if proc.returncode not in exit_codes:
            print
            print("Could not execute:")
            print(cmd)
            print("\nError message:")
            print(foutput)
            print("\nAborting test run.")
            ns_destroy()
            exit(1)


def test_runner(filtered_tests, args):
    """
    Driver function for the unit tests.

    Prints information about the tests being run, executes the setup and
    teardown commands and the command under test itself. Also determines
    success/failure based on the information in the test case and generates
    TAP output accordingly.
    """
    testlist = filtered_tests
    tcount = len(testlist)
    index = 1
    tap = str(index) + ".." + str(tcount) + "\n"

    for tidx in testlist:
        result = True
        tresult = ""
        if "flower" in tidx["category"] and args.device == None:
            continue
        print("Test " + tidx["id"] + ": " + tidx["name"])
        prepare_env(tidx["setup"])
        (p, procout) = exec_cmd(tidx["cmdUnderTest"])
        exit_code = p.returncode

        if (exit_code != int(tidx["expExitCode"])):
            result = False
            print("exit:", exit_code, int(tidx["expExitCode"]))
            print(procout)
        else:
            match_pattern = re.compile(str(tidx["matchPattern"]), re.DOTALL)
            (p, procout) = exec_cmd(tidx["verifyCmd"])
            match_index = re.findall(match_pattern, procout)
            if len(match_index) != int(tidx["matchCount"]):
                result = False

        if result == True:
            tresult += "ok "
        else:
            tresult += "not ok "
        tap += tresult + str(index) + " " + tidx["id"] + " " + tidx["name"] + "\n"

        if result == False:
            tap += procout

        prepare_env(tidx["teardown"])
        index += 1

    return tap


def ns_create():
    """
    Create the network namespace in which the tests will be run and set up
    the required network devices for it.
    """
    if (USE_NS):
        cmd = 'ip netns add $NS'
        exec_cmd(cmd, False)
        cmd = 'ip link add $DEV0 type veth peer name $DEV1'
        exec_cmd(cmd, False)
        cmd = 'ip link set $DEV1 netns $NS'
        exec_cmd(cmd, False)
        cmd = 'ip link set $DEV0 up'
        exec_cmd(cmd, False)
        cmd = 'ip -n $NS link set $DEV1 up'
        exec_cmd(cmd, False)
        cmd = 'ip link set $DEV2 netns $NS'
        exec_cmd(cmd, False)
        cmd = 'ip -n $NS link set $DEV2 up'
        exec_cmd(cmd, False)


def ns_destroy():
    """
    Destroy the network namespace for testing (and any associated network
    devices as well)
    """
    if (USE_NS):
        cmd = 'ip netns delete $NS'
        exec_cmd(cmd, False)


def has_blank_ids(idlist):
    """
    Search the list for empty ID fields and return true/false accordingly.
    """
    return not(all(k for k in idlist))


def load_from_file(filename):
    """
    Open the JSON file containing the test cases and return them
    as list of ordered dictionary objects.
    """
    try:
        with open(filename) as test_data:
            testlist = json.load(test_data, object_pairs_hook=OrderedDict)
    except json.JSONDecodeError as jde:
        print('IGNORING test case file {}\n\tBECAUSE:  {}'.format(filename, jde))
        testlist = list()
    else:
        idlist = get_id_list(testlist)
        if (has_blank_ids(idlist)):
            for k in testlist:
                k['filename'] = filename
    return testlist


def args_parse():
    """
    Create the argument parser.
    """
    parser = argparse.ArgumentParser(description='Linux TC unit tests')
    return parser


def set_args(parser):
    """
    Set the command line arguments for tdc.
    """
    parser.add_argument('-p', '--path', type=str,
                        help='The full path to the tc executable to use')
    parser.add_argument('-c', '--category', type=str, nargs='?', const='+c',
                        help='Run tests only from the specified category, or if no category is specified, list known categories.')
    parser.add_argument('-f', '--file', type=str,
                        help='Run tests from the specified file')
    parser.add_argument('-l', '--list', type=str, nargs='?', const="++", metavar='CATEGORY',
                        help='List all test cases, or those only within the specified category')
    parser.add_argument('-s', '--show', type=str, nargs=1, metavar='ID', dest='showID',
                        help='Display the test case with specified id')
    parser.add_argument('-e', '--execute', type=str, nargs=1, metavar='ID',
                        help='Execute the single test case with specified ID')
    parser.add_argument('-i', '--id', action='store_true', dest='gen_id',
                        help='Generate ID numbers for new test cases')
    parser.add_argument('-d', '--device',
                        help='Execute the test case in flower category')
    return parser


def check_default_settings(args):
    """
    Process any arguments overriding the default settings, and ensure the
    settings are correct.
    """
    # Allow for overriding specific settings
    global NAMES

    if args.path != None:
         NAMES['TC'] = args.path
    if args.device != None:
         NAMES['DEV2'] = args.device
    if not os.path.isfile(NAMES['TC']):
        print("The specified tc path " + NAMES['TC'] + " does not exist.")
        exit(1)


def get_id_list(alltests):
    """
    Generate a list of all IDs in the test cases.
    """
    return [x["id"] for x in alltests]


def check_case_id(alltests):
    """
    Check for duplicate test case IDs.
    """
    idl = get_id_list(alltests)
    return [x for x in idl if idl.count(x) > 1]


def does_id_exist(alltests, newid):
    """
    Check if a given ID already exists in the list of test cases.
    """
    idl = get_id_list(alltests)
    return (any(newid == x for x in idl))


def generate_case_ids(alltests):
    """
    If a test case has a blank ID field, generate a random hex ID for it
    and then write the test cases back to disk.
    """
    import random
    for c in alltests:
        if (c["id"] == ""):
            while True:
                newid = str('%04x' % random.randrange(16**4))
                if (does_id_exist(alltests, newid)):
                    continue
                else:
                    c['id'] = newid
                    break

    ufilename = []
    for c in alltests:
        if ('filename' in c):
            ufilename.append(c['filename'])
    ufilename = get_unique_item(ufilename)
    for f in ufilename:
        testlist = []
        for t in alltests:
            if 'filename' in t:
                if t['filename'] == f:
                    del t['filename']
                    testlist.append(t)
        outfile = open(f, "w")
        json.dump(testlist, outfile, indent=4)
        outfile.close()


def get_test_cases(args):
    """
    If a test case file is specified, retrieve tests from that file.
    Otherwise, glob for all json files in subdirectories and load from
    each one.
    """
    import fnmatch
    if args.file != None:
        if not os.path.isfile(args.file):
            print("The specified test case file " + args.file + " does not exist.")
            exit(1)
        flist = [args.file]
    else:
        flist = []
        for root, dirnames, filenames in os.walk('tc-tests'):
            for filename in fnmatch.filter(filenames, '*.json'):
                flist.append(os.path.join(root, filename))
    alltests = list()
    for casefile in flist:
        alltests = alltests + (load_from_file(casefile))
    return alltests


def set_operation_mode(args):
    """
    Load the test case data and process remaining arguments to determine
    what the script should do for this run, and call the appropriate
    function.
    """
    alltests = get_test_cases(args)

    if args.gen_id:
        idlist = get_id_list(alltests)
        if (has_blank_ids(idlist)):
            alltests = generate_case_ids(alltests)
        else:
            print("No empty ID fields found in test files.")
        exit(0)

    duplicate_ids = check_case_id(alltests)
    if (len(duplicate_ids) > 0):
        print("The following test case IDs are not unique:")
        print(str(set(duplicate_ids)))
        print("Please correct them before continuing.")
        exit(1)

    ucat = get_test_categories(alltests)

    if args.showID:
        show_test_case_by_id(alltests, args.showID[0])
        exit(0)

    if args.execute:
        target_id = args.execute[0]
    else:
        target_id = ""

    if args.category:
        if (args.category == '+c'):
            print("Available categories:")
            print_sll(ucat)
            exit(0)
        else:
            target_category = args.category
    else:
        target_category = ""


    testcases = get_categorized_testlist(alltests, ucat)

    if args.list:
        if (args.list == "++"):
            list_test_cases(alltests)
            exit(0)
        elif(len(args.list) > 0):
            if (args.list not in ucat):
                print("Unknown category " + args.list)
                print("Available categories:")
                print_sll(ucat)
                exit(1)
            list_test_cases(testcases[args.list])
            exit(0)

    if (os.geteuid() != 0):
        print("This script must be run with root privileges.\n")
        exit(1)

    ns_create()

    if (len(target_category) == 0):
        if (len(target_id) > 0):
            alltests = list(filter(lambda x: target_id in x['id'], alltests))
            if (len(alltests) == 0):
                print("Cannot find a test case with ID matching " + target_id)
                exit(1)
        catresults = test_runner(alltests, args)
        print("All test results: " + "\n\n" + catresults)
    elif (len(target_category) > 0):
        if (target_category == "flower") and args.device == None:
            print("Please specify a NIC device (-d) to run category flower")
            exit(1)
        if (target_category not in ucat):
            print("Specified category is not present in this file.")
            exit(1)
        else:
            catresults = test_runner(testcases[target_category], args)
            print("Category " + target_category + "\n\n" + catresults)

    ns_destroy()


def main():
    """
    Start of execution; set up argument parser and get the arguments,
    and start operations.
    """
    parser = args_parse()
    parser = set_args(parser)
    (args, remaining) = parser.parse_known_args()
    check_default_settings(args)

    set_operation_mode(args)

    exit(0)


if __name__ == "__main__":
    main()
