'''
run the command under test, under valgrind and collect memory leak info
as a separate test.
'''


import os
import re
import signal
from string import Template
import subprocess
import time
from TdcPlugin import TdcPlugin
from TdcResults import *

from tdc_config import *

def vp_extract_num_from_string(num_as_string_maybe_with_commas):
    return int(num_as_string_maybe_with_commas.replace(',',''))

class SubPlugin(TdcPlugin):
    def __init__(self):
        self.sub_class = 'valgrind/SubPlugin'
        self.tap = ''
        self._tsr = TestSuiteReport()
        super().__init__()

    def pre_suite(self, testcount, testist):
        '''run commands before test_runner goes into a test loop'''
        self.testidlist = [tidx['id'] for tidx in testlist]
        super().pre_suite(testcount, testlist)
        if self.args.verbose > 1:
            print('{}.pre_suite'.format(self.sub_class))
        if self.args.valgrind:
            self._add_to_tap('1..{}\n'.format(self.testcount))

    def post_suite(self, index):
        '''run commands after test_runner goes into a test loop'''
        super().post_suite(index)
        if self.args.verbose > 1:
            print('{}.post_suite'.format(self.sub_class))
        #print('{}'.format(self.tap))
        for xx in range(index - 1, self.testcount):
            res = TestResult('{}-mem'.format(self.testidlist[xx]), 'Test skipped')
            res.set_result(ResultState.skip)
            res.set_errormsg('Skipped because of prior setup/teardown failure')
            self._add_results(res)
        if self.args.verbose < 4:
            subprocess.check_output('rm -f vgnd-*.log', shell=True)

    def add_args(self, parser):
        super().add_args(parser)
        self.argparser_group = self.argparser.add_argument_group(
            'valgrind',
            'options for valgrindPlugin (run command under test under Valgrind)')

        self.argparser_group.add_argument(
            '-V', '--valgrind', action='store_true',
            help='Run commands under valgrind')

        return self.argparser

    def adjust_command(self, stage, command):
        super().adjust_command(stage, command)
        cmdform = 'list'
        cmdlist = list()

        if not self.args.valgrind:
            return command

        if self.args.verbose > 1:
            print('{}.adjust_command'.format(self.sub_class))

        if not isinstance(command, list):
            cmdform = 'str'
            cmdlist = command.split()
        else:
            cmdlist = command

        if stage == 'execute':
            if self.args.verbose > 1:
                print('adjust_command:  stage is {}; inserting valgrind stuff in command [{}] list [{}]'.
                      format(stage, command, cmdlist))
            cmdlist.insert(0, '--track-origins=yes')
            cmdlist.insert(0, '--show-leak-kinds=definite,indirect')
            cmdlist.insert(0, '--leak-check=full')
            cmdlist.insert(0, '--log-file=vgnd-{}.log'.format(self.args.testid))
            cmdlist.insert(0, '-v')  # ask for summary of non-leak errors
            cmdlist.insert(0, ENVIR['VALGRIND_BIN'])
        else:
            pass

        if cmdform == 'str':
            command = ' '.join(cmdlist)
        else:
            command = cmdlist

        if self.args.verbose > 1:
            print('adjust_command:  return command [{}]'.format(command))
        return command

    def post_execute(self):
        if not self.args.valgrind:
            return

        res = TestResult('{}-mem'.format(self.args.testid),
              '{} memory leak check'.format(self.args.test_name))
        if self.args.test_skip:
            res.set_result(ResultState.skip)
            res.set_errormsg('Test case designated as skipped.')
            self._add_results(res)
            return

        self.definitely_lost_re = re.compile(
            r'definitely lost:\s+([,0-9]+)\s+bytes in\s+([,0-9]+)\sblocks', re.MULTILINE | re.DOTALL)
        self.indirectly_lost_re = re.compile(
            r'indirectly lost:\s+([,0-9]+)\s+bytes in\s+([,0-9]+)\s+blocks', re.MULTILINE | re.DOTALL)
        self.possibly_lost_re = re.compile(
            r'possibly lost:\s+([,0-9]+)bytes in\s+([,0-9]+)\s+blocks', re.MULTILINE | re.DOTALL)
        self.non_leak_error_re = re.compile(
            r'ERROR SUMMARY:\s+([,0-9]+) errors from\s+([,0-9]+)\s+contexts', re.MULTILINE | re.DOTALL)

        def_num = 0
        ind_num = 0
        pos_num = 0
        nle_num = 0

        # what about concurrent test runs?  Maybe force them to be in different directories?
        with open('vgnd-{}.log'.format(self.args.testid)) as vfd:
            content = vfd.read()
            def_mo = self.definitely_lost_re.search(content)
            ind_mo = self.indirectly_lost_re.search(content)
            pos_mo = self.possibly_lost_re.search(content)
            nle_mo = self.non_leak_error_re.search(content)

            if def_mo:
                def_num = int(def_mo.group(2))
            if ind_mo:
                ind_num = int(ind_mo.group(2))
            if pos_mo:
                pos_num = int(pos_mo.group(2))
            if nle_mo:
                nle_num = int(nle_mo.group(1))

        mem_results = ''
        if (def_num > 0) or (ind_num > 0) or (pos_num > 0) or (nle_num > 0):
            mem_results += 'not '
            res.set_result(ResultState.fail)
            res.set_failmsg('Memory leak detected')
            res.append_failmsg(content)
        else:
            res.set_result(ResultState.success)

        self._add_results(res)


    def _add_results(self, res):
        self._tsr.add_resultdata(res)

    def _add_to_tap(self, more_tap_output):
        self.tap += more_tap_output
