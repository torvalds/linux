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
    parser.add_argument(
        '-p', '--path', type=str,
        help='The full path to the tc executable to use')
    sg = parser.add_argument_group(
        'selection', 'select which test cases: ' +
        'files plus directories; filtered by categories plus testids')
    ag = parser.add_argument_group(
        'action', 'select action to perform on selected test cases')

    sg.add_argument(
        '-D', '--directory', nargs='+', metavar='DIR',
        help='Collect tests from the specified directory(ies) ' +
        '(default [tc-tests])')
    sg.add_argument(
        '-f', '--file', nargs='+', metavar='FILE',
        help='Run tests from the specified file(s)')
    sg.add_argument(
        '-c', '--category', nargs='*', metavar='CATG', default=['+c'],
        help='Run tests only from the specified category/ies, ' +
        'or if no category/ies is/are specified, list known categories.')
    sg.add_argument(
        '-e', '--execute', nargs='+', metavar='ID',
        help='Execute the specified test cases with specified IDs')
    ag.add_argument(
        '-l', '--list', action='store_true',
        help='List all test cases, or those only within the specified category')
    ag.add_argument(
        '-s', '--show', action='store_true', dest='showID',
        help='Display the selected test cases')
    ag.add_argument(
        '-i', '--id', action='store_true', dest='gen_id',
        help='Generate ID numbers for new test cases')
    parser.add_argument(
        '-v', '--verbose', action='count', default=0,
        help='Show the commands that are being run')
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
    # print('check_case_id:  idl is {}'.format(idl))
    # answer = list()
    # for x in idl:
    #     print('Looking at {}'.format(x))
    #     print('what the heck is idl.count(x)???   {}'.format(idl.count(x)))
    #     if idl.count(x) > 1:
    #         answer.append(x)
    #         print(' ... append it {}'.format(x))
    return [x for x in idl if idl.count(x) > 1]
    return answer


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

def filter_tests_by_id(args, testlist):
    '''
    Remove tests from testlist that are not in the named id list.
    If id list is empty, return empty list.
    '''
    newlist = list()
    if testlist and args.execute:
        target_ids = args.execute

        if isinstance(target_ids, list) and (len(target_ids) > 0):
            newlist = list(filter(lambda x: x['id'] in target_ids, testlist))
    return newlist

def filter_tests_by_category(args, testlist):
    '''
    Remove tests from testlist that are not in a named category.
    '''
    answer = list()
    if args.category and testlist:
        test_ids = list()
        for catg in set(args.category):
            if catg == '+c':
                continue
            print('considering category {}'.format(catg))
            for tc in testlist:
                if catg in tc['category'] and tc['id'] not in test_ids:
                    answer.append(tc)
                    test_ids.append(tc['id'])

    return answer

def get_test_cases(args):
    """
    If a test case file is specified, retrieve tests from that file.
    Otherwise, glob for all json files in subdirectories and load from
    each one.
    Also, if requested, filter by category, and add tests matching
    certain ids.
    """
    import fnmatch

    flist = []
    testdirs = ['tc-tests']

    if args.file:
        # at least one file was specified - remove the default directory
        testdirs = []

        for ff in args.file:
            if not os.path.isfile(ff):
                print("IGNORING file " + ff + " \n\tBECAUSE does not exist.")
            else:
                flist.append(os.path.abspath(ff))

    if args.directory:
        testdirs = args.directory

    for testdir in testdirs:
        for root, dirnames, filenames in os.walk(testdir):
            for filename in fnmatch.filter(filenames, '*.json'):
                candidate = os.path.abspath(os.path.join(root, filename))
                if candidate not in testdirs:
                    flist.append(candidate)

    alltestcases = list()
    for casefile in flist:
        alltestcases = alltestcases + (load_from_file(casefile))

    allcatlist = get_test_categories(alltestcases)
    allidlist = get_id_list(alltestcases)

    testcases_by_cats = get_categorized_testlist(alltestcases, allcatlist)
    idtestcases = filter_tests_by_id(args, alltestcases)
    cattestcases = filter_tests_by_category(args, alltestcases)

    cat_ids = [x['id'] for x in cattestcases]
    if args.execute:
        if args.category:
            alltestcases = cattestcases + [x for x in idtestcases if x['id'] not in cat_ids]
        else:
            alltestcases = idtestcases
    else:
        if cat_ids:
            alltestcases = cattestcases
        else:
            # just accept the existing value of alltestcases,
            # which has been filtered by file/directory
            pass

    return allcatlist, allidlist, testcases_by_cats, alltestcases


def set_operation_mode(args):
    """
    Load the test case data and process remaining arguments to determine
    what the script should do for this run, and call the appropriate
    function.
    """
    ucat, idlist, testcases, alltests = get_test_cases(args)

    if args.gen_id:
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

    if args.showID:
        for atest in alltests:
            print_test_case(atest)
        exit(0)

    if isinstance(args.category, list) and (len(args.category) == 0):
        print("Available categories:")
        print_sll(ucat)
        exit(0)

    if args.list:
        if args.list:
            list_test_cases(alltests)
            exit(0)

    if (os.geteuid() != 0):
        print("This script must be run with root privileges.\n")
        exit(1)

    ns_create()

    catresults = test_runner(alltests, args)
    print('All test results: \n\n{}'.format(catresults))

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
