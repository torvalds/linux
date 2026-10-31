# SPDX-License-Identifier: GPL-2.0

import configparser
import errno
import os
import sys
import glob
import argparse
import platform
import tempfile
import logging
import re
import shutil
import subprocess

log = logging.getLogger('test')

def data_equal(a, b):
    # Allow multiple values in assignment separated by '|'
    a_list = a.split('|')
    b_list = b.split('|')

    for a_item in a_list:
        for b_item in b_list:
            if (a_item == b_item):
                return True
            elif (a_item == '*') or (b_item == '*'):
                return True

    return False

CORE_PMU_AVAILABLE = None
CORE_PMU_COUNT = None

def core_pmu_available():
    """
    Is there a core PMU on this machine? Without one hardware events aren't
    supported and get replaced by the software fallback, so there's no way to
    check the expectations that pin their legacy encodings.  The answer can't
    change while the tests run, so probe the binding only once.
    """
    core_pmu_probe()
    return CORE_PMU_AVAILABLE

def core_pmu_probe():
    """
    Ask perf itself, through its python binding, to parse and open 'cycles'
    once: a parse failure or an ENOENT means no PMU can count it, any other
    error means we can't tell whether there is one, and the number of events
    it expanded to is the number of core PMUs, see
    parse_events_multi_pmu_add(), which is what core_pmu_count() asks for.
    """
    global CORE_PMU_AVAILABLE, CORE_PMU_COUNT

    if CORE_PMU_AVAILABLE is not None:
        return

    try:
        import perf
        parse_events = perf.parse_events
    except (ImportError, AttributeError):
        # Nothing to ask about it, let the expectations speak.
        CORE_PMU_AVAILABLE = True
        return

    try:
        evlist = parse_events('cycles')
    except OSError:
        # No PMU that can count it, the same as the ENOENT below.
        CORE_PMU_AVAILABLE = False
        return

    CORE_PMU_COUNT = len(evlist)
    try:
        evlist.open()
    except OSError as err:
        # Anything else is a permission problem or alike, not our business here.
        CORE_PMU_AVAILABLE = err.errno != errno.ENOENT
        return
    evlist.close()

    CORE_PMU_AVAILABLE = True

PERF_TYPE_HARDWARE = 0
PERF_TYPE_HW_CACHE = 3
PERF_SAMPLE_ID = 1 << 6
PERF_SAMPLE_IDENTIFIER = 1 << 16
PERF_TYPE_SOFTWARE = 1
PERF_COUNT_SW_DUMMY = 9

def is_dummy_tracking_event(event):
    """
    Is this the dummy event perf opens to carry the tracking events (mmap,
    comm, task) when the requested ones can't, like when there is more than one
    core PMU and each core PMU's events run on only part of the CPUs?
    """
    return (event.get('type') == str(PERF_TYPE_SOFTWARE) and
            event.get('config') == str(PERF_COUNT_SW_DUMMY))

# With more than one core PMU the tracking terms go to the dummy tracking event,
# leaving the requested events without them, so they can't be matched there.
TRACKING_TERMS = ['mmap', 'comm', 'task', 'mmap_data']

# The legacy event types, where perf may qualify 'config' with the core PMU
# type in its top 32 bits when there is more than one core PMU.
LEGACY_TYPES = (str(PERF_TYPE_HARDWARE), str(PERF_TYPE_HW_CACHE))

def mask_bits(value, bits):
    """
    Take the bits in 'bits' out of each of the '|'-separated alternatives in
    'value', keeping the rest of the expression, wildcards included.
    """
    masked = []

    for alternative in value.split('|'):
        try:
            alternative = str(int(alternative, 0) & ~bits)
        except ValueError:
            pass
        masked.append(alternative)

    return '|'.join(masked)

def hide_core_pmu_type(value):
    """
    The kernel can be told which core PMU a legacy type event is meant for by
    putting its type in the top 32 bits of 'config', which perf only does when
    there is more than one core PMU, so take those bits out to keep the
    expectations written for the single core PMU case matching on
    heterogeneous machines.
    """
    return mask_bits(value, 0xffffffff00000000)

# The two sample id bits mean the same thing here: how the samples of the
# events in the evlist get associated with the event that produced them.
SAMPLE_ID_BITS = PERF_SAMPLE_ID | PERF_SAMPLE_IDENTIFIER

# No real sample_type bit is this high, so it can stand for "either of the
# sample id bits" when comparing the two sides.
SAMPLE_ID_EITHER = 1 << 40

PERF_FORMAT_GROUP = 1 << 3

def core_pmu_count():
    """
    How many core PMUs does perf see?  An event with no PMU named in it
    becomes one event per core PMU, see parse_events_multi_pmu_add(), and
    that is the expansion that leaves the clones for the other core PMUs
    without any group members and that makes perf qualify 'config' with the
    type of the PMU it is for, so this is the number of events perf makes of
    'cycles', from the probe core_pmu_available() shares.
    """
    core_pmu_probe()
    return CORE_PMU_COUNT

def single_core_pmu():
    """
    Does perf see only one core PMU?  Asked for by the comparisons that need
    it, not at import time, as a run may never get to one.
    """
    return core_pmu_count() == 1

def hide_group_format(expected, recorded):
    """
    read_format's PERF_FORMAT_GROUP bit asks for the group members to be read
    along with the leader, which aside from this bit is verified with the
    fd/group_fd based group checking, and with more than one core PMU a group
    whose members are pinned to one of them, say 'default_core', leaves the
    clones for the other core PMUs without any members, so that bit isn't
    there: whether it is there is a consequence of how the evlist got
    expanded to more than one core PMU, not of what the expectations are
    about, so take it out on both sides.  On a machine with a single core PMU
    there is nothing to expand, so the expectations keep verifying it.
    """
    if single_core_pmu():
        return expected, recorded

    return (mask_bits(expected, PERF_FORMAT_GROUP),
            mask_bits(recorded, PERF_FORMAT_GROUP))

def hide_sample_id(expected, recorded):
    """
    perf asks for a way to tell the samples apart, PERF_SAMPLE_IDENTIFIER where
    available, PERF_SAMPLE_ID otherwise, see evlist__config(): whenever the
    evlist has more than one event, and when asked to outright, with
    --sample-identifier, by an auxtrace session, or by record__config() for a
    pipe run, none of which depends on the sample types, as those only choose
    between the two bits:
    with more than one core PMU the dummy tracking events make the sample types
    differ, so IDENTIFIER is asked for even for events whose expectations ask
    for ID, written on a machine with a single core PMU.

    So treat the two as the same thing: when the expectation asks for either
    of them, ask for "an id of some sort" on both sides, and when the
    expectation doesn't ask for any, take the bits out of what got recorded.
    """
    try:
        asked = int(expected, 0) & SAMPLE_ID_BITS
    except ValueError:
        # The expectation is a wildcard or a set of alternatives.
        return expected, recorded

    try:
        recorded_id_bits = int(recorded, 0) & SAMPLE_ID_BITS
    except ValueError:
        # The recorded side is a wildcard or a set of alternatives.
        return expected, mask_bits(recorded, SAMPLE_ID_BITS)

    if not asked:
        if single_core_pmu():
            return expected, recorded
        return expected, mask_bits(recorded, SAMPLE_ID_BITS)

    return (str(int(expected, 0) & ~SAMPLE_ID_BITS | SAMPLE_ID_EITHER),
            str(int(recorded, 0) & ~SAMPLE_ID_BITS |
                (SAMPLE_ID_EITHER if recorded_id_bits else 0)))

class Fail(Exception):
    def __init__(self, test, msg):
        self.msg = msg
        self.test = test
    def getMsg(self):
        return '\'%s\' - %s' % (self.test.path, self.msg)

class Notest(Exception):
    def __init__(self, test, reason):
        self.reason = reason
        self.test = test
    def getMsg(self):
        return '[%s] \'%s\'' % (self.reason, self.test.path)

class Unsup(Exception):
    def __init__(self, test, ret, expected):
        self.test = test
        self.ret = ret
        self.expected = expected
    def getMsg(self):
        return '\'%s\' exit %d, expected %s, no attrs recorded to verify' % (
            self.test.path, self.ret, self.expected)

class Event(dict):
    terms = [
        'cpu',
        'flags',
        'type',
        'size',
        'config',
        'sample_period',
        'sample_type',
        'read_format',
        'disabled',
        'inherit',
        'pinned',
        'exclusive',
        'exclude_user',
        'exclude_kernel',
        'exclude_hv',
        'exclude_idle',
        'mmap',
        'comm',
        'freq',
        'inherit_stat',
        'enable_on_exec',
        'task',
        'watermark',
        'precise_ip',
        'mmap_data',
        'sample_id_all',
        'exclude_host',
        'exclude_guest',
        'exclude_callchain_kernel',
        'exclude_callchain_user',
        'wakeup_events',
        'bp_type',
        'config1',
        'config2',
        'branch_sample_type',
        'sample_regs_user',
        'sample_stack_user',
    ]

    def add(self, data):
        for key, val in data:
            log.debug("      %s = %s", key, val)
            self[key] = val

    def __init__(self, name, data, base):
        log.debug("    Event %s", name)
        self.name  = name
        self.group = ''
        self.add(base)
        self.add(data)

    def hide_core_pmu_type_terms(self, other):
        """
        Is this a legacy type event? Then the core PMU type that perf may have
        put in the top 32 bits of 'config' to tell the kernel which core PMU it
        wants isn't part of what we asked for, see hide_core_pmu_type().
        """
        for event in (self, other):
            for type in event.get('type', '').split('|'):
                if type in LEGACY_TYPES:
                    return True

        return False

    def hide_core_pmu_type(self, term, value, other):
        if term == 'config' and self.hide_core_pmu_type_terms(other):
            return hide_core_pmu_type(value)

        return value

    def hide_event_count_dependent_terms(self, term, expected, recorded):
        if term == 'sample_type':
            return hide_sample_id(expected, recorded)
        if term == 'read_format':
            return hide_group_format(expected, recorded)

        return expected, recorded

    def recorded_side(self, other):
        """
        Which one is what perf recorded? Both directions get compared, see
        Test.compare(), so this can't be taken from the arguments' order.
        """
        if getattr(other, 'recorded', False):
            return other

        return self if getattr(self, 'recorded', False) else None

    def hide_tracking_via_dummy(self, term, other):
        """
        The tracking terms are set on the tracking event, which is the dummy
        event when there is more than one core PMU, leaving the requested
        events without them, nothing to match on then.
        """
        if term not in TRACKING_TERMS:
            return False

        for event in (self, other):
            if term in getattr(event, 'tracking_moved_to_dummy', ()):
                return True

        return False

    def equal(self, other):
        for t in Event.terms:
            log.debug("      [%s] %s %s", t, self[t], other[t])
            if t not in self or t not in other:
                return False
            if self.hide_tracking_via_dummy(t, other):
                continue

            recorded = self.recorded_side(other)
            if recorded is self:
                expected, got_event = other, self
            else:
                expected, got_event = self, other
            exp = self.hide_core_pmu_type(t, expected[t], other)
            got = self.hide_core_pmu_type(t, got_event[t], other)
            if recorded is not None:
                exp, got = self.hide_event_count_dependent_terms(t, exp, got)
            if not data_equal(exp, got):
                return False
        return True

    def optional(self):
        if 'optional' in self and self['optional'] == '1':
            return True
        return False

    def diff(self, other):
        for t in Event.terms:
            if t not in self or t not in other:
                continue
            if self.hide_tracking_via_dummy(t, other):
                continue

            recorded = self.recorded_side(other)
            if recorded is self:
                expected, got_event = other, self
            else:
                expected, got_event = self, other
            exp = self.hide_core_pmu_type(t, expected[t], other)
            got = self.hide_core_pmu_type(t, got_event[t], other)
            if recorded is not None:
                exp, got = self.hide_event_count_dependent_terms(t, exp, got)
            if not data_equal(exp, got):
                log.warning("expected %s=%s, got %s", t, expected[t],
                            got_event[t])

def parse_version(version):
    if not version:
        return None
    return [int(v) for v in version.split(".")[0:2]]

def format_version(version):
    return ".".join(str(v) for v in version)

# Test file description needs to have following sections:
# [config]
#   - just single instance in file
#   - needs to specify:
#     'command' - perf command name
#     'args'    - special command arguments
#     'ret'     - Skip test if Perf doesn't exit with this value (0 by default)
#     'test_ret'- If set to 'true', fail test instead of skipping for 'ret' argument
#     'arch'    - architecture specific test (optional)
#                 comma separated list, ! at the beginning
#                 negates it.
#     'requires' - Comma separated list of things the test needs, it is
#                 skipped when any of them isn't there, currently:
#                   core-pmu - the machine can count hardware events.
#     'auxv'    - Truthy statement that is evaled in the scope of the auxv map. When false,
#                 the test is skipped. For example 'auxv["AT_HWCAP"] == 10'. (optional)
#     'kernel_since' - Inclusive kernel version from which the test will start running. Only the
#                      first two values are supported, for example "6.1" (optional)
#     'kernel_until' - Exclusive kernel version from which the test will stop running. (optional)
# [eventX:base]
#   - one or multiple instances in file
#   - expected values assignments
class Test(object):
    def __init__(self, path, options):
        parser = configparser.ConfigParser()
        parser.read(path)

        log.warning("running '%s'", path)

        self.path     = path
        self.test_dir = options.test_dir
        self.perf     = options.perf
        self.command  = parser.get('config', 'command')
        self.args     = parser.get('config', 'args')

        try:
            self.ret  = parser.get('config', 'ret')
        except Exception:
            self.ret  = 0

        self.test_ret = parser.getboolean('config', 'test_ret', fallback=False)

        try:
            self.arch  = parser.get('config', 'arch')
            log.warning("test limitation '%s'", self.arch)
        except Exception:
            self.arch  = ''

        self.auxv = parser.get('config', 'auxv', fallback=None)
        self.requires = parser.get('config', 'requires', fallback='')
        self.kernel_since = parse_version(parser.get('config', 'kernel_since', fallback=None))
        self.kernel_until = parse_version(parser.get('config', 'kernel_until', fallback=None))
        self.expect   = {}
        self.result   = {}
        log.debug("  loading expected events")
        self.load_events(path, self.expect)

    def is_event(self, name):
        if name.find("event") == -1:
            return False
        else:
            return True

    def skip_test_kernel_since(self):
        if not self.kernel_since:
            return None
        if self.kernel_since <= parse_version(platform.release()):
            return None
        return "kernel older than %s" % format_version(self.kernel_since)

    def skip_test_kernel_until(self):
        if not self.kernel_until:
            return None
        if parse_version(platform.release()) < self.kernel_until:
            return None
        return "kernel newer than %s" % format_version(self.kernel_until)

    def skip_test_auxv(self):
        def new_auxv(a, pattern):
            items = list(filter(None, pattern.split(a)))
            # AT_HWCAP is hex but doesn't have a prefix, so special case it
            if items[0] == "AT_HWCAP":
                value = int(items[-1], 16)
            else:
                try:
                    value = int(items[-1], 0)
                except ValueError:
                    value = items[-1]
            return (items[0], value)

        if not self.auxv:
            return None
        auxv = subprocess.check_output("LD_SHOW_AUXV=1 sleep 0", shell=True) \
               .decode(sys.stdout.encoding)
        pattern = re.compile(r"[: ]+")
        auxv = dict([new_auxv(a, pattern) for a in auxv.splitlines()])
        if eval(self.auxv):
            return None

        return "the auxv condition doesn't hold here"

    def skip_test_arch(self, myarch):
        # If architecture not set always run test
        if self.arch == '':
            return None

        # Allow multiple values in assignment separated by ','
        arch_list = self.arch.split(',')

        # Handle negated list such as !s390x,ppc
        if arch_list[0][0] == '!':
            arch_list[0] = arch_list[0][1:]
            log.warning("excluded architecture list %s", arch_list)
            for arch_item in arch_list:
                if arch_item == myarch:
                    return "arch = %s, machine = %s" % (self.arch, myarch)
            return None

        for arch_item in arch_list:
            if arch_item == myarch:
                return None

        return "arch = %s, machine = %s" % (self.arch, myarch)

    def skip_test_requires(self):
        for requirement in filter(None, map(str.strip, self.requires.split(','))):
            if requirement == 'core-pmu':
                if not core_pmu_available():
                    log.warning("test limitation 'requires %s'", requirement)
                    return "needs a core PMU, none here"
            else:
                log.warning("unknown requirement '%s', ignored", requirement)

        return None

    def restore_sample_rate(self, value=10000):
        try:
            # Check value of sample_rate
            with open("/proc/sys/kernel/perf_event_max_sample_rate", "r", encoding="utf-8") as fIn:
                curr_value = fIn.readline()
            # If too low restore to reasonable value
            if not curr_value or int(curr_value) < int(value):
                with open("/proc/sys/kernel/perf_event_max_sample_rate", "w", encoding="utf-8") as fOut:
                    fOut.write(str(value))

        except IOError as e:
            log.warning("couldn't restore sample_rate value: I/O error %s", e)
        except ValueError as e:
            log.warning("couldn't restore sample_rate value: Value error %s", e)
        except TypeError as e:
            log.warning("couldn't restore sample_rate value: Type error %s", e)

    def load_events(self, path, events):
        parser_event = configparser.ConfigParser()
        parser_event.read(path)

        # The event record section header contains 'event' word,
        # optionaly followed by ':' allowing to load 'parent
        # event' first as a base
        for section in filter(self.is_event, parser_event.sections()):

            parser_items = parser_event.items(section)
            base_items   = {}

            # Read parent event if there's any
            if (':' in section):
                base = section[section.index(':') + 1:]
                parser_base = configparser.ConfigParser()
                parser_base.read(self.test_dir + '/' + base)
                base_items = parser_base.items('event')

            e = Event(section, parser_items, base_items)
            events[section] = e

    def run_cmd(self, tempdir):
        _junk1, _junk2, _junk3, _junk4, myarch = (os.uname())

        arch = self.skip_test_arch(myarch)
        if arch:
            log.debug("  detail: the test file has arch = %s, the machine reports %s",
                     self.arch, myarch)
            raise Notest(self, arch)

        auxv = self.skip_test_auxv()
        if auxv:
            log.debug("  detail: the auxv condition %s doesn't hold, the bits come "
                     "from the LD_SHOW_AUXV=1 output", self.auxv)
            raise Notest(self, auxv)

        kernel = self.skip_test_kernel_since()
        if kernel:
            log.debug("  detail: the test file's kernel_since = %s, this kernel is %s",
                     format_version(self.kernel_since), platform.release())
            raise Notest(self, kernel)

        kernel = self.skip_test_kernel_until()
        if kernel:
            log.debug("  detail: the test file's kernel_until = %s, this kernel is %s",
                     format_version(self.kernel_until), platform.release())
            raise Notest(self, kernel)

        requirement = self.skip_test_requires()
        if requirement:
            log.debug("  detail: requires core-pmu, nothing countable here, check "
                     "kernel.perf_event_paranoid and the PMU nodes under "
                     "/sys/bus/event_source/")
            raise Notest(self, requirement)

        self.restore_sample_rate()
        cmd = "PERF_TEST_ATTR=%s %s %s -o %s/perf.data %s" % (tempdir,
              self.perf, self.command, tempdir, self.args)
        ret = os.WEXITSTATUS(os.system(cmd))

        log.info("  '%s' ret '%s', expected '%s'", cmd, str(ret), str(self.ret))

        if not data_equal(str(ret), str(self.ret)):
            plain_cmd = "%s %s -o /dev/null %s" % (
                self.perf, self.command,
                self.args.split('>/dev/null')[0].rstrip())
            if self.test_ret:
                raise Fail(self, "Perf exit code failure")
            else:
                log.debug("  detail: '%s' exited with %d, the expectations want the "
                         "workload's %s exit, so there are no attrs recorded to "
                         "verify; this is normally a capability problem, run '%s' "
                         "to see why", plain_cmd, ret, self.ret, plain_cmd)
                raise Unsup(self, ret, str(self.ret))

    def drop_dummy_tracking_events(self):
        """
        Leave out the dummy event perf itself opened, see
        is_dummy_tracking_event(), unless the expectations ask for one.
        """
        if any(is_dummy_tracking_event(e) for e in self.expect.values()):
            return

        if not any(is_dummy_tracking_event(e) for e in self.result.values()):
            return

        moved = set()
        for name, event in list(self.result.items()):
            if is_dummy_tracking_event(event):
                log.debug("      dropping dummy tracking event [%s]", name)
                moved.update(t for t in TRACKING_TERMS if event.get(t) == '1')
                del self.result[name]

        # The tracking terms the dropped dummies carried, flag the remaining
        # events so that the match leaves only those out.
        for event in self.result.values():
            event.tracking_moved_to_dummy = moved

    def drop_other_core_pmu_events(self, devices='/sys/bus/event_source/devices'):
        """
        Leave out the events of the other core PMUs, which have their own
        encodings, when the expectations name one of the core PMUs.
        """
        asked = set()
        for event in self.expect.values():
            types = event.get('type', '').split('|')
            if '*' in types:
                return
            asked.update(types)

        try:
            names = os.listdir(devices)
        except OSError:
            return

        core = set()
        for name in names:
            path = os.path.join(devices, name)
            has_cpus = os.path.exists(os.path.join(path, 'cpus'))
            if name not in ('cpu', 'cpum_cf') and not has_cpus:
                continue
            try:
                with open(os.path.join(path, 'type'), encoding='utf-8') as f:
                    core.add(f.read().strip())
            except OSError:
                pass

        if not core & asked:
            return

        others = core - asked
        for name, event in list(self.result.items()):
            if event.get('type') in others:
                log.debug("      dropping other core PMU event [%s]", name)
                del self.result[name]

    def compare(self, expect, result, strict_groups=True):
        match = {}

        log.debug("  compare")

        # For each expected event find all matching
        # events in result. Fail if there's not any.
        for exp_name, exp_event in expect.items():
            exp_list = []
            res_event = {}
            log.debug("    matching [%s]", exp_name)
            for res_name, res_event in result.items():
                log.debug("      to [%s]", res_name)
                if (exp_event.equal(res_event)):
                    exp_list.append(res_name)
                    log.debug("    ->OK")
                else:
                    log.debug("    ->FAIL")

            log.debug("    match: [%s] matches %s", exp_name, str(exp_list))

            # we did not any matching event - fail
            if not exp_list:
                if exp_event.optional():
                    log.debug("    %s does not match, but is optional", exp_name)
                else:
                    if not res_event:
                        log.debug("    res_event is empty")
                    else:
                        exp_event.diff(res_event)
                    raise Fail(self, 'match failure')

            match[exp_name] = exp_list

        # Check that each expected group is matched in the result; an event
        # opened in a group may match an expectation that doesn't declare
        # one, as those only verify the event attributes.
        for exp_name, exp_event in expect.items():
            group = exp_event.group

            if (group == ''):
                continue

            for res_name in match[exp_name]:
                res_group = result[res_name].group
                if res_group == '' and not strict_groups:
                    continue
                if res_group not in match[group]:
                    raise Fail(self, 'group failure')

                log.debug("    group: [%s] matches group leader %s",
                          exp_name, str(match[group]))

        log.debug("  matched")

    def resolve_groups(self, events):
        for name, event in events.items():
            group_fd = event['group_fd']
            if group_fd == '-1':
                continue

            for iname, ievent in events.items():
                if (ievent['fd'] == group_fd):
                    event.group = iname
                    log.debug('[%s] has group leader [%s]', name, iname)
                    break

    def run(self):
        tempdir = tempfile.mkdtemp()

        try:
            # run the test script
            self.run_cmd(tempdir)

            # load events expectation for the test
            log.debug("  loading result events")
            for f in glob.glob(tempdir + '/event*'):
                self.load_events(f, self.result)
            for event in self.result.values():
                event.recorded = True

            self.drop_dummy_tracking_events()
            self.drop_other_core_pmu_events()

            # resolve group_fd to event names
            self.resolve_groups(self.expect)
            self.resolve_groups(self.result)

            # do the expectation - results matching - both ways
            self.compare(self.expect, self.result)
            self.compare(self.result, self.expect, strict_groups=False)

        finally:
            # cleanup
            shutil.rmtree(tempdir)


def run_tests(options):
    for f in glob.glob(options.test_dir + '/' + options.test):
        try:
            Test(f, options).run()
        except Unsup as obj:
            log.warning("unsupp  %s", obj.getMsg())
        except Notest as obj:
            log.warning("skipped %s", obj.getMsg())

def setup_log(verbose):
    global log
    level = logging.CRITICAL

    if verbose == 1:
        level = logging.WARNING
    if verbose == 2:
        level = logging.INFO
    if verbose >= 3:
        level = logging.DEBUG

    log = logging.getLogger('test')
    log.setLevel(level)
    ch  = logging.StreamHandler()
    ch.setLevel(level)
    formatter = logging.Formatter('%(message)s')
    ch.setFormatter(formatter)
    log.addHandler(ch)

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("-t", "--test",
                        action="store", dest="test", help="single test")
    parser.add_argument("-d", "--test-dir",
                        action="store", dest="test_dir", help="tests dir")
    parser.add_argument("-p", "--perf",
                        action="store", dest="perf", help="perf binary")
    parser.add_argument("-v", "--verbose",
                        default=0, action="count", dest="verbose",
                        help="verbose level")

    options = parser.parse_args()

    setup_log(options.verbose)

    if not options.test_dir:
        print('FAILED no -d option specified')
        sys.exit(-1)

    if not options.test:
        options.test = 'test*'

    try:
        run_tests(options)

    except Fail as obj:
        print("FAILED %s" % obj.getMsg())
        sys.exit(-1)

    sys.exit(0)

if __name__ == '__main__':
    main()
