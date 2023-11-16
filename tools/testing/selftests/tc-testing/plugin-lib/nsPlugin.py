import os
import signal
from string import Template
import subprocess
import time
from multiprocessing import Pool
from functools import cached_property
from TdcPlugin import TdcPlugin

from tdc_config import *

try:
    from pyroute2 import netns
    from pyroute2 import IPRoute
    netlink = True
except ImportError:
    netlink = False
    print("!!! Consider installing pyroute2 !!!")

def prepare_suite(obj, test):
    original = obj.args.NAMES

    if 'skip' in test and test['skip'] == 'yes':
        return

    if 'nsPlugin' not in test['plugins']:
        return

    shadow = {}
    shadow['IP'] = original['IP']
    shadow['TC'] = original['TC']
    shadow['NS'] = '{}-{}'.format(original['NS'], test['random'])
    shadow['DEV0'] = '{}id{}'.format(original['DEV0'], test['id'])
    shadow['DEV1'] = '{}id{}'.format(original['DEV1'], test['id'])
    shadow['DUMMY'] = '{}id{}'.format(original['DUMMY'], test['id'])
    shadow['DEV2'] = original['DEV2']
    obj.args.NAMES = shadow

    if netlink == True:
        obj._nl_ns_create()
    else:
        obj._ns_create()

    # Make sure the netns is visible in the fs
    while True:
        obj._proc_check()
        try:
            ns = obj.args.NAMES['NS']
            f = open('/run/netns/{}'.format(ns))
            f.close()
            break
        except:
            time.sleep(0.1)
            continue

    obj.args.NAMES = original

class SubPlugin(TdcPlugin):
    def __init__(self):
        self.sub_class = 'ns/SubPlugin'
        super().__init__()

    def pre_suite(self, testcount, testlist):
        from itertools import cycle

        super().pre_suite(testcount, testlist)

        print("Setting up namespaces and devices...")

        with Pool(self.args.mp) as p:
            it = zip(cycle([self]), testlist)
            p.starmap(prepare_suite, it)

    def pre_case(self, caseinfo, test_skip):
        if self.args.verbose:
            print('{}.pre_case'.format(self.sub_class))

        if test_skip:
            return

    def post_case(self):
        if self.args.verbose:
            print('{}.post_case'.format(self.sub_class))

        self._ns_destroy()

    def post_suite(self, index):
        if self.args.verbose:
            print('{}.post_suite'.format(self.sub_class))

        # Make sure we don't leak resources
        for f in os.listdir('/run/netns/'):
            cmd = self._replace_keywords("$IP netns del {}".format(f))

            if self.args.verbose > 3:
                print('_exec_cmd:  command "{}"'.format(cmd))

            subprocess.run(cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def adjust_command(self, stage, command):
        super().adjust_command(stage, command)
        cmdform = 'list'
        cmdlist = list()

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

    def _nl_ns_create(self):
        ns = self.args.NAMES["NS"];
        dev0 = self.args.NAMES["DEV0"];
        dev1 = self.args.NAMES["DEV1"];
        dummy = self.args.NAMES["DUMMY"];

        if self.args.verbose:
            print('{}._nl_ns_create'.format(self.sub_class))

        netns.create(ns)
        netns.pushns(newns=ns)
        with IPRoute() as ip:
            ip.link('add', ifname=dev1, kind='veth', peer={'ifname': dev0, 'net_ns_fd':'/proc/1/ns/net'})
            ip.link('add', ifname=dummy, kind='dummy')
            while True:
                try:
                    dev1_idx = ip.link_lookup(ifname=dev1)[0]
                    dummy_idx = ip.link_lookup(ifname=dummy)[0]
                    ip.link('set', index=dev1_idx, state='up')
                    ip.link('set', index=dummy_idx, state='up')
                    break
                except:
                    time.sleep(0.1)
                    continue
        netns.popns()

        with IPRoute() as ip:
            while True:
                try:
                    dev0_idx = ip.link_lookup(ifname=dev0)[0]
                    ip.link('set', index=dev0_idx, state='up')
                    break
                except:
                    time.sleep(0.1)
                    continue

    def _ns_create_cmds(self):
        cmds = []

        ns = self.args.NAMES['NS']

        cmds.append(self._replace_keywords('netns add {}'.format(ns)))
        cmds.append(self._replace_keywords('link add $DEV1 type veth peer name $DEV0'))
        cmds.append(self._replace_keywords('link set $DEV1 netns {}'.format(ns)))
        cmds.append(self._replace_keywords('link add $DUMMY type dummy'.format(ns)))
        cmds.append(self._replace_keywords('link set $DUMMY netns {}'.format(ns)))
        cmds.append(self._replace_keywords('netns exec {} $IP link set $DEV1 up'.format(ns)))
        cmds.append(self._replace_keywords('netns exec {} $IP link set $DUMMY up'.format(ns)))
        cmds.append(self._replace_keywords('link set $DEV0 up'.format(ns)))

        if self.args.device:
            cmds.append(self._replace_keywords('link set $DEV2 netns {}'.format(ns)))
            cmds.append(self._replace_keywords('netns exec {} $IP link set $DEV2 up'.format(ns)))

        return cmds

    def _ns_create(self):
        '''
        Create the network namespace in which the tests will be run and set up
        the required network devices for it.
        '''
        self._exec_cmd_batched('pre', self._ns_create_cmds())

    def _ns_destroy_cmd(self):
        return self._replace_keywords('netns delete {}'.format(self.args.NAMES['NS']))

    def _ns_destroy(self):
        '''
        Destroy the network namespace for testing (and any associated network
        devices as well)
        '''
        self._exec_cmd('post', self._ns_destroy_cmd())

    @cached_property
    def _proc(self):
        ip = self._replace_keywords("$IP -b -")
        proc = subprocess.Popen(ip,
            shell=True,
            stdin=subprocess.PIPE,
            env=ENVIR)

        return proc

    def _proc_check(self):
        proc = self._proc

        proc.poll()

        if proc.returncode is not None and proc.returncode != 0:
            raise RuntimeError("iproute2 exited with an error code")

    def _exec_cmd(self, stage, command):
        '''
        Perform any required modifications on an executable command, then run
        it in a subprocess and return the results.
        '''

        if self.args.verbose > 3:
            print('_exec_cmd:  command "{}"'.format(command))

        proc = self._proc

        proc.stdin.write((command + '\n').encode())
        proc.stdin.flush()

        if self.args.verbose > 3:
            print('_exec_cmd proc: {}'.format(proc))

        self._proc_check()

    def _exec_cmd_batched(self, stage, commands):
        for cmd in commands:
            self._exec_cmd(stage, cmd)

    def _replace_keywords(self, cmd):
        """
        For a given executable command, substitute any known
        variables contained within NAMES with the correct values
        """
        tcmd = Template(cmd)
        subcmd = tcmd.safe_substitute(self.args.NAMES)
        return subcmd
