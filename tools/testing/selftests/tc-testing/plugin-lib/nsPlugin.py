import os
import signal
from string import Template
import subprocess
import time
from TdcPlugin import TdcPlugin

from tdc_config import *

class SubPlugin(TdcPlugin):
    def __init__(self):
        self.sub_class = 'ns/SubPlugin'
        super().__init__()

    def pre_suite(self, testcount, testidlist):
        '''run commands before test_runner goes into a test loop'''
        super().pre_suite(testcount, testidlist)

        if self.args.namespace:
            self._ns_create()
        else:
            self._ports_create()

    def post_suite(self, index):
        '''run commands after test_runner goes into a test loop'''
        super().post_suite(index)
        if self.args.verbose:
            print('{}.post_suite'.format(self.sub_class))

        if self.args.namespace:
            self._ns_destroy()
        else:
            self._ports_destroy()

    def add_args(self, parser):
        super().add_args(parser)
        self.argparser_group = self.argparser.add_argument_group(
            'netns',
            'options for nsPlugin(run commands in net namespace)')
        self.argparser_group.add_argument(
            '-N', '--no-namespace', action='store_false', default=True,
            dest='namespace', help='Don\'t run commands in namespace')
        return self.argparser

    def adjust_command(self, stage, command):
        super().adjust_command(stage, command)
        cmdform = 'list'
        cmdlist = list()

        if not self.args.namespace:
            return command

        if self.args.verbose:
            print('{}.adjust_command'.format(self.sub_class))

        if not isinstance(command, list):
            cmdform = 'str'
            cmdlist = command.split()
        else:
            cmdlist = command
        if stage == 'setup' or stage == 'execute' or stage == 'verify' or stage == 'teardown':
            if self.args.verbose:
                print('adjust_command:  stage is {}; inserting netns stuff in command [{}] list [{}]'.format(stage, command, cmdlist))
            cmdlist.insert(0, self.args.NAMES['NS'])
            cmdlist.insert(0, 'exec')
            cmdlist.insert(0, 'netns')
            cmdlist.insert(0, self.args.NAMES['IP'])
        else:
            pass

        if cmdform == 'str':
            command = ' '.join(cmdlist)
        else:
            command = cmdlist

        if self.args.verbose:
            print('adjust_command:  return command [{}]'.format(command))
        return command

    def _ports_create(self):
        cmd = '$IP link add $DEV0 type veth peer name $DEV1'
        self._exec_cmd('pre', cmd)
        cmd = '$IP link set $DEV0 up'
        self._exec_cmd('pre', cmd)
        if not self.args.namespace:
            cmd = '$IP link set $DEV1 up'
            self._exec_cmd('pre', cmd)

    def _ports_destroy(self):
        cmd = '$IP link del $DEV0'
        self._exec_cmd('post', cmd)

    def _ns_create(self):
        '''
        Create the network namespace in which the tests will be run and set up
        the required network devices for it.
        '''
        self._ports_create()
        if self.args.namespace:
            cmd = '$IP netns add {}'.format(self.args.NAMES['NS'])
            self._exec_cmd('pre', cmd)
            cmd = '$IP link set $DEV1 netns {}'.format(self.args.NAMES['NS'])
            self._exec_cmd('pre', cmd)
            cmd = '$IP -n {} link set $DEV1 up'.format(self.args.NAMES['NS'])
            self._exec_cmd('pre', cmd)
            if self.args.device:
                cmd = '$IP link set $DEV2 netns {}'.format(self.args.NAMES['NS'])
                self._exec_cmd('pre', cmd)
                cmd = '$IP -n {} link set $DEV2 up'.format(self.args.NAMES['NS'])
                self._exec_cmd('pre', cmd)

    def _ns_destroy(self):
        '''
        Destroy the network namespace for testing (and any associated network
        devices as well)
        '''
        if self.args.namespace:
            cmd = '$IP netns delete {}'.format(self.args.NAMES['NS'])
            self._exec_cmd('post', cmd)

    def _exec_cmd(self, stage, command):
        '''
        Perform any required modifications on an executable command, then run
        it in a subprocess and return the results.
        '''
        if '$' in command:
            command = self._replace_keywords(command)

        self.adjust_command(stage, command)
        if self.args.verbose:
            print('_exec_cmd:  command "{}"'.format(command))
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

    def _replace_keywords(self, cmd):
        """
        For a given executable command, substitute any known
        variables contained within NAMES with the correct values
        """
        tcmd = Template(cmd)
        subcmd = tcmd.safe_substitute(self.args.NAMES)
        return subcmd
