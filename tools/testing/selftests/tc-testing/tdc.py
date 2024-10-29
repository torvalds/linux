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
from TdcResults import *

class PluginDependencyException(Exception):
    def __init__(self, missing_pg):
        self.missing_pg = missing_pg

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
        self.failed_plugins = {}
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

    def load_plugin(self, pgdir, pgname):
        pgname = pgname[0:-3]
        foo = importlib.import_module('{}.{}'.format(pgdir, pgname))
        self.plugins[pgname] = foo
        self.plugin_instances.append(foo.SubPlugin())
        self.plugin_instances[-1].check_args(self.args, None)

    def get_required_plugins(self, testlist):
        '''
        Get all required plugins from the list of test cases and return
        all unique items.
        '''
        reqs = []
        for t in testlist:
            try:
                if 'requires' in t['plugins']:
                    if isinstance(t['plugins']['requires'], list):
                        reqs.extend(t['plugins']['requires'])
                    else:
                        reqs.append(t['plugins']['requires'])
            except KeyError:
                continue
        reqs = get_unique_item(reqs)
        return reqs

    def load_required_plugins(self, reqs, parser, args, remaining):
        '''
        Get all required plugins from the list of test cases and load any plugin
        that is not already enabled.
        '''
        pgd = ['plugin-lib', 'plugin-lib-custom']
        pnf = []

        for r in reqs:
            if r not in self.plugins:
                fname = '{}.py'.format(r)
                source_path = []
                for d in pgd:
                    pgpath = '{}/{}'.format(d, fname)
                    if os.path.isfile(pgpath):
                        source_path.append(pgpath)
                if len(source_path) == 0:
                    print('ERROR: unable to find required plugin {}'.format(r))
                    pnf.append(fname)
                    continue
                elif len(source_path) > 1:
                    print('WARNING: multiple copies of plugin {} found, using version found')
                    print('at {}'.format(source_path[0]))
                pgdir = source_path[0]
                pgdir = pgdir.split('/')[0]
                self.load_plugin(pgdir, fname)
        if len(pnf) > 0:
            raise PluginDependencyException(pnf)

        parser = self.call_add_args(parser)
        (args, remaining) = parser.parse_known_args(args=remaining, namespace=args)
        return args

    def call_pre_suite(self, testcount, testidlist):
        for pgn_inst in self.plugin_instances:
            pgn_inst.pre_suite(testcount, testidlist)

    def call_post_suite(self, index):
        for pgn_inst in reversed(self.plugin_instances):
            pgn_inst.post_suite(index)

    def call_pre_case(self, caseinfo, *, test_skip=False):
        for pgn_inst in self.plugin_instances:
            try:
                pgn_inst.pre_case(caseinfo, test_skip)
            except Exception as ee:
                print('exception {} in call to pre_case for {} plugin'.
                      format(ee, pgn_inst.__class__))
                print('testid is {}'.format(caseinfo['id']))
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

    def set_args(self, args):
        self.args = args

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

    try:
        (rawout, serr) = proc.communicate(timeout=NAMES['TIMEOUT'])
        if proc.returncode != 0 and len(serr) > 0:
            foutput = serr.decode("utf-8", errors="ignore")
        else:
            foutput = rawout.decode("utf-8", errors="ignore")
    except subprocess.TimeoutExpired:
        foutput = "Command \"{}\" timed out\n".format(command)
        proc.returncode = 255

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
    res = TestResult(tidx['id'], tidx['name'])
    if args.verbose > 0:
        print("\t====================\n=====> ", end="")
    print("Test " + tidx["id"] + ": " + tidx["name"])

    if 'skip' in tidx:
        if tidx['skip'] == 'yes':
            res = TestResult(tidx['id'], tidx['name'])
            res.set_result(ResultState.skip)
            res.set_errormsg('Test case designated as skipped.')
            pm.call_pre_case(tidx, test_skip=True)
            pm.call_post_execute()
            return res

    # populate NAMES with TESTID for this test
    NAMES['TESTID'] = tidx['id']

    pm.call_pre_case(tidx)
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
        print("exit: {!r}".format(exit_code))
        print("exit: {}".format(int(tidx["expExitCode"])))
        #print("exit: {!r} {}".format(exit_code, int(tidx["expExitCode"])))
        res.set_result(ResultState.fail)
        res.set_failmsg('Command exited with {}, expected {}\n{}'.format(exit_code, tidx["expExitCode"], procout))
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
                res.set_result(ResultState.fail)
                res.set_failmsg('Could not match regex pattern. Verify command output:\n{}'.format(procout))
            else:
                res.set_result(ResultState.success)
        elif int(tidx["matchCount"]) != 0:
            res.set_result(ResultState.fail)
            res.set_failmsg('No output generated by verify command.')
        else:
            res.set_result(ResultState.success)

    prepare_env(args, pm, 'teardown', '-----> teardown stage', tidx['teardown'], procout)
    pm.call_post_case()

    index += 1

    # remove TESTID from NAMES
    del(NAMES['TESTID'])
    return res

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

    tsr = TestSuiteReport()

    try:
        pm.call_pre_suite(tcount, [tidx['id'] for tidx in testlist])
    except Exception as ee:
        ex_type, ex, ex_tb = sys.exc_info()
        print('Exception {} {} (caught in pre_suite).'.
              format(ex_type, ex))
        traceback.print_tb(ex_tb)
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
            errmsg = "Tests using the DEV2 variable must define the name of a "
            errmsg += "physical NIC with the -d option when running tdc.\n"
            errmsg += "Test has been skipped."
            if args.verbose > 1:
                print(errmsg)
            res = TestResult(tidx['id'], tidx['name'])
            res.set_result(ResultState.skip)
            res.set_errormsg(errmsg)
            tsr.add_resultdata(res)
            continue
        try:
            badtest = tidx  # in case it goes bad
            res = run_one_test(pm, args, index, tidx)
            tsr.add_resultdata(res)
        except PluginMgrTestFail as pmtf:
            ex_type, ex, ex_tb = sys.exc_info()
            stage = pmtf.stage
            message = pmtf.message
            output = pmtf.output
            res = TestResult(tidx['id'], tidx['name'])
            res.set_result(ResultState.skip)
            res.set_errormsg(pmtf.message)
            res.set_failmsg(pmtf.output)
            tsr.add_resultdata(res)
            index += 1
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

    if tcount + 1 != count:
        for tidx in testlist[count - 1:]:
            res = TestResult(tidx['id'], tidx['name'])
            res.set_result(ResultState.skip)
            msg = 'skipped - previous {} failed {} {}'.format(stage,
                index, badtest.get('id', '--Unknown--'))
            res.set_errormsg(msg)
            tsr.add_resultdata(res)
            count += 1

    if args.pause:
        print('Want to pause\nPress enter to continue ...')
        if input(sys.stdin):
            print('got something on stdin')

    pm.call_post_suite(index)

    return tsr

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
        '--outfile', type=str,
        help='Path to the file in which results should be saved. ' +
        'Default target is the current directory.')
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
        '--format', default='tap', const='tap', nargs='?',
        choices=['none', 'xunit', 'tap'],
        help='Specify the format for test results. (Default: TAP)')
    parser.add_argument('-d', '--device',
                        help='Execute test cases that use a physical device, ' +
                        'where DEVICE is its name. (If not defined, tests ' +
                        'that require a physical device will be skipped)')
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
    if 'TIMEOUT' not in NAMES:
        NAMES['TIMEOUT'] = None
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


def set_operation_mode(pm, parser, args, remaining):
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
        list_test_cases(alltests)
        exit(0)

    exit_code = 0 # KSFT_PASS
    if len(alltests):
        req_plugins = pm.get_required_plugins(alltests)
        try:
            args = pm.load_required_plugins(req_plugins, parser, args, remaining)
        except PluginDependencyException as pde:
            print('The following plugins were not found:')
            print('{}'.format(pde.missing_pg))
        catresults = test_runner(pm, args, alltests)
        if catresults.count_failures() != 0:
            exit_code = 1 # KSFT_FAIL
        if args.format == 'none':
            print('Test results output suppression requested\n')
        else:
            print('\nAll test results: \n')
            if args.format == 'xunit':
                suffix = 'xml'
                res = catresults.format_xunit()
            elif args.format == 'tap':
                suffix = 'tap'
                res = catresults.format_tap()
            print(res)
            print('\n\n')
            if not args.outfile:
                fname = 'test-results.{}'.format(suffix)
            else:
                fname = args.outfile
            with open(fname, 'w') as fh:
                fh.write(res)
                fh.close()
                if os.getenv('SUDO_UID') is not None:
                    os.chown(fname, uid=int(os.getenv('SUDO_UID')),
                        gid=int(os.getenv('SUDO_GID')))
    else:
        print('No tests found\n')
        exit_code = 4 # KSFT_SKIP
    exit(exit_code)

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
    pm.set_args(args)
    check_default_settings(args, remaining, pm)
    if args.verbose > 2:
        print('args is {}'.format(args))

    set_operation_mode(pm, parser, args, remaining)

if __name__ == "__main__":
    main()
