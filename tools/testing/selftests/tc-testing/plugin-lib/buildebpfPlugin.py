'''
build ebpf program
'''

import os
import signal
from string import Template
import subprocess
import time
from TdcPlugin import TdcPlugin
from tdc_config import *

class SubPlugin(TdcPlugin):
    def __init__(self):
        self.sub_class = 'buildebpf/SubPlugin'
        self.tap = ''
        super().__init__()

    def pre_suite(self, testcount, testidlist):
        super().pre_suite(testcount, testidlist)

        if self.args.buildebpf:
            self._ebpf_makeall()

    def post_suite(self, index):
        super().post_suite(index)

        self._ebpf_makeclean()

    def add_args(self, parser):
        super().add_args(parser)

        self.argparser_group = self.argparser.add_argument_group(
            'buildebpf',
            'options for buildebpfPlugin')
        self.argparser_group.add_argument(
            '--nobuildebpf', action='store_false', default=True,
            dest='buildebpf',
            help='Don\'t build eBPF programs')

        return self.argparser

    def _ebpf_makeall(self):
        if self.args.buildebpf:
            self._make('all')

    def _ebpf_makeclean(self):
        if self.args.buildebpf:
            self._make('clean')

    def _make(self, target):
        command = 'make -C {} {}'.format(self.args.NAMES['EBPFDIR'], target)
        proc = subprocess.Popen(command,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=ENVIR)
        (rawout, serr) = proc.communicate()

        if proc.returncode != 0 and len(serr) > 0:
            foutput = serr.decode("utf-8")
        else:
            foutput = rawout.decode("utf-8")

        proc.stdout.close()
        proc.stderr.close()
        return proc, foutput
