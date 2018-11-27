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
import importlib
import json
import subprocess
import time
import traceback
from collections import OrderedDict
from string import Template

from tdc_config import *
from tdc_helper import *

import TdcPlugin


class PluginMgrTestFail(Exception):
    def __init__(self, stage, output, message):
        self.stage = stage
        self.output = output
        self.message = message

class PluginMgr:
    def __init__(self, argparser):
        super().__init__()
        self.plugins = {}
        self.plugin_instances = []
        self.args = []
        self.argparser = argparser

        # TODO, put plugins in order
        plugindir = os.getenv('TDC_PLUGIN_DIR', './plugins')
        for dirpath, dirnames, filenames in os.walk(plugindir):
            for fn in filenames:
                if (fn.endswith('.py') and
                    not fn == '__init__.py' and
                    not fn.startswith('#') and
                    not fn.startswith('.#')):
                    mn = fn[0:-3]
                    foo = importlib.import_module('plugins.' + mn)
                    self.plugins[mn] = foo
                    self.plugin_instances.append(foo.SubPlugin())

    def call_pre_suite(self, testcount, testidlist):
        for pgn_inst in self.plugin_instances:
            pgn_inst.pre_suite(testcount, testidlist)

    def call_post_suite(self, index):
        for pgn_inst in reversed(self.plugin_instances):
            pgn_inst.post_suite(index)

    def call_pre_case(self, test_ordinal, testid):
        for pgn_inst in self.plugin_instances:
            try:
                pgn_inst.pre_case(test_ordinal, testid)
            except Exception as ee:
                print('exception {} in call to pre_case for {} plugin'.
                      format(ee, pgn_inst.__class__))
                print('test_ordinal is {}'.format(test_ordinal))
                print('testid is {}'.format(testid))
                raise

    def call_post_case(self):
        for pgn_inst in reversed(self.plugin_instances):
            pgn_inst.post_case()

    def call_pre_execute(self):
        for pgn_inst in self.plugin_instances:
            pgn_inst.pre_execute()

    def call_post_execute(self):
        for pgn_inst in reversed(self.plugin_instances):
            pgn_inst.post_execute()

    def call_add_args(self, parser):
        for pgn_inst in self.plugin_instances:
            parser = pgn_inst.add_args(parser)
        return parser

    def call_check_args(self, args, remaining):
        for pgn_inst in self.plugin_instances:
            pgn_inst.check_args(args, remaining)

    def call_adjust_command(self, stage, command):
        for pgn_inst in self.plugin_instances:
            command = pgn_inst.adjust_command(stage, command)
        return command

    @staticmethod
    def _make_argparser(args):
        self.argparser = argparse.ArgumentParser(
            description='Linux TC unit tests')


def replace_keywords(cmd):
    """
    For a given executable command, substitute any known
    variables contained within NAMES with the correct values
    """
    tcmd = Template(cmd)
    subcmd = tcmd.safe_substitute(NAMES)
    return subcmd


def exec_cmd(args, pm, stage, command):
    """
    Perform any required modifications on an executable command, then run
    it in a subprocess and return the results.
    """
    if len(command.strip()) == 0:
        return None, None
    if '$' in command:
        command = replace_keywords(command)

    command = pm.call_adjust_command(stage, command)
    if args.verbose > 0:
        print('command "{}"'.format(command))
    proc = subprocess.Popen(command,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=ENVIR)
    (rawout, serr) = proc.communicate()

    if proc.returncode != 0 and len(serr) > 0:
        foutput = serr.decode("utf-8", errors="ignore")
    else:
        foutput = rawout.decode("utf-8", errors="ignore")

    proc.stdout.close()
    proc.stderr.close()
    return proc, foutput


def prepare_env(args, pm, stage, prefix, cmdlist, output = None):
    """
    Execute the setup/teardown commands for a test case.
    Optionally terminate test execution if the command fails.
    """
    if args.verbose > 0:
        print('{}'.format(prefix))
    for cmdinfo in cmdlist:
        if isinstance(cmdinfo, list):
            exit_codes = cmdinfo[1:]
            cmd = cmdinfo[0]
        else:
            exit_codes = [0]
            cmd = cmdinfo

        if not cmd:
            continue

        (proc, foutput) = exec_cmd(args, pm, stage, cmd)

        if proc and (proc.returncode not in exit_codes):
            print('', file=sys.stderr)
            print("{} *** Could not execute: \"{}\"".format(prefix, cmd),
                  file=sys.stderr)
            print("\n{} *** Error message: \"{}\"".format(prefix, foutput),
                  file=sys.stderr)
            print("returncode {}; expected {}".format(proc.returncode,
                                                      exit_codes))
            print("\n{} *** Aborting test run.".format(prefix), file=sys.stderr)
            print("\n\n{} *** stdout ***".format(proc.stdout), file=sys.stderr)
            print("\n\n{} *** stderr ***".format(proc.stderr), file=sys.stderr)
            raise PluginMgrTestFail(
                stage, output,
                '"{}" did not complete successfully'.format(prefix))

def run_one_test(pm, args, index, tidx):
    global NAMES
    result = True
    tresult = ""
    tap = ""
    if args.verbose > 0:
        print("\t====================\n=====> ", end="")
    print("Test " + tidx["id"] + ": " + tidx["name"])

    # populate NAMES with TESTID for this test
    NAMES['TESTID'] = tidx['id']

    pm.call_pre_case(index, tidx['id'])
    prepare_env(args, pm, 'setup', "-----> prepare stage", tidx["setup"])

    if (args.verbose > 0):
        print('-----> execute stage')
    pm.call_pre_execute()
    (p, procout) = exec_cmd(args, pm, 'execute', tidx["cmdUnderTest"])
    if p:
        exit_code = p.returncode
    else:
        exit_code = None

    pm.call_post_execute()

    if (exit_code is None or exit_code != int(tidx["expExitCode"])):
        result = False
        print("exit: {!r}".format(exit_code))
        print("exit: {}".format(int(tidx["expExitCode"])))
        #print("exit: {!r} {}".format(exit_code, int(tidx["expExitCode"])))
        print(procout)
    else:
        if args.verbose > 0:
            print('-----> verify stage')
        match_pattern = re.compile(
            str(tidx["matchPattern"]), re.DOTALL | re.MULTILINE)
        (p, procout) = exec_cmd(args, pm, 'verify', tidx["verifyCmd"])
        if procout:
            match_index = re.findall(match_pattern, procout)
            if len(match_index) != int(tidx["matchCount"]):
                result = False
        elif int(tidx["matchCount"]) != 0:
            result = False

    if not result:
        tresult += 'not '
    tresult += 'ok {} - {} # {}\n'.format(str(index), tidx['id'], tidx['name'])
    tap += tresult

    if result == False:
        if procout:
            tap += procout
        else:
            tap += 'No output!\n'

    prepare_env(args, pm, 'teardown', '-----> teardown stage', tidx['teardown'], procout)
    pm.call_post_case()

    index += 1

    # remove TESTID from NAMES
    del(NAMES['TESTID'])
    return tap

def test_runner(pm, args, filtered_tests):
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
    tap = ''
    badtest = None
    stage = None
    emergency_exit = False
    emergency_exit_message = ''

    if args.notap:
        if args.verbose:
            tap = 'notap requested:  omitting test plan\n'
    else:
        tap = str(index) + ".." + str(tcount) + "\n"
    try:
        pm.call_pre_suite(tcount, [tidx['id'] for tidx in testlist])
    except Exception as ee:
        ex_type, ex, ex_tb = sys.exc_info()
        print('Exception {} {} (caught in pre_suite).'.
              format(ex_type, ex))
        # when the extra print statements are uncommented,
        # the traceback does not appear between them
        # (it appears way earlier in the tdc.py output)
        # so don't bother ...
        # print('--------------------(')
        # print('traceback')
        traceback.print_tb(ex_tb)
        # print('--------------------)')
        emergency_exit_message = 'EMERGENCY EXIT, call_pre_suite failed with exception {} {}\n'.format(ex_type, ex)
        emergency_exit = True
        stage = 'pre-SUITE'

    if emergency_exit:
        pm.call_post_suite(index)
        return emergency_exit_message
    if args.verbose > 1:
        print('give test rig 2 seconds to stabilize')
    time.sleep(2)
    for tidx in testlist:
        if "flower" in tidx["category"] and args.device == None:
            if args.verbose > 1:
                print('Not executing test {} {} because DEV2 not defined'.
                      format(tidx['id'], tidx['name']))
            continue
        try:
            badtest = tidx  # in case it goes bad
            tap += run_one_test(pm, args, index, tidx)
        except PluginMgrTestFail as pmtf:
            ex_type, ex, ex_tb = sys.exc_info()
            stage = pmtf.stage
            message = pmtf.message
            output = pmtf.output
            print(message)
            print('Exception {} {} (caught in test_runner, running test {} {} {} stage {})'.
                  format(ex_type, ex, index, tidx['id'], tidx['name'], stage))
            print('---------------')
            print('traceback')
            traceback.print_tb(ex_tb)
            print('---------------')
            if stage == 'teardown':
                print('accumulated output for this test:')
                if pmtf.output:
                    print(pmtf.output)
            print('---------------')
            break
        index += 1

    # if we failed in setup or teardown,
    # fill in the remaining tests with ok-skipped
    count = index
    if not args.notap:
        tap += 'about to flush the tap output if tests need to be skipped\n'
        if tcount + 1 != index:
            for tidx in testlist[index - 1:]:
                msg = 'skipped - previous {} failed'.format(stage)
                tap += 'ok {} - {} # {} {} {}\n'.format(
                    count, tidx['id'], msg, index, badtest.get('id', '--Unknown--'))
                count += 1

        tap += 'done flushing skipped test tap output\n'

    if args.pause:
        print('Want to pause\nPress enter to continue ...')
        if input(sys.stdin):
            print('got something on stdin')

    pm.call_post_suite(index)

    return tap

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
    parser.add_argument(
        '-N', '--notap', action='store_true',
        help='Suppress tap results for command under test')
    parser.add_argument('-d', '--device',
                        help='Execute the test case in flower category')
    parser.add_argument(
        '-P', '--pause', action='store_true',
        help='Pause execution just before post-suite stage')
    return parser


def check_default_settings(args, remaining, pm):
    """
    Process any arguments overriding the default settings,
    and ensure the settings are correct.
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

    pm.call_check_args(args, remaining)


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
                newid = str('{:04x}'.format(random.randrange(16**4)))
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
        outfile.write("\n")
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
                print("IGNORING file " + ff + "\n\tBECAUSE does not exist.")
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


def set_operation_mode(pm, args):
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

    if len(alltests):
        catresults = test_runner(pm, args, alltests)
    else:
        catresults = 'No tests found\n'
    if args.notap:
        print('Tap output suppression requested\n')
    else:
        print('All test results: \n\n{}'.format(catresults))

def main():
    """
    Start of execution; set up argument parser and get the arguments,
    and start operations.
    """
    parser = args_parse()
    parser = set_args(parser)
    pm = PluginMgr(parser)
    parser = pm.call_add_args(parser)
    (args, remaining) = parser.parse_known_args()
    args.NAMES = NAMES
    check_default_settings(args, remaining, pm)
    if args.verbose > 2:
        print('args is {}'.format(args))

    set_operation_mode(pm, args)

    exit(0)


if __name__ == "__main__":
    main()
