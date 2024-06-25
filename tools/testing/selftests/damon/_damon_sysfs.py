# SPDX-License-Identifier: GPL-2.0

import os

ksft_skip=4

sysfs_root = None
with open('/proc/mounts', 'r') as f:
    for line in f:
        dev_name, mount_point, dev_fs = line.split()[:3]
        if dev_fs == 'sysfs':
            sysfs_root = '%s/kernel/mm/damon/admin' % mount_point
            break
if sysfs_root is None:
    print('Seems sysfs not mounted?')
    exit(ksft_skip)

def write_file(path, string):
    "Returns error string if failed, or None otherwise"
    string = '%s' % string
    try:
        with open(path, 'w') as f:
            f.write(string)
    except Exception as e:
        return '%s' % e
    return None

def read_file(path):
    '''Returns the read content and error string.  The read content is None if
    the reading failed'''
    try:
        with open(path, 'r') as f:
            return f.read(), None
    except Exception as e:
        return None, '%s' % e

class DamosAccessPattern:
    size = None
    nr_accesses = None
    age = None
    scheme = None

    def __init__(self, size=None, nr_accesses=None, age=None):
        self.size = size
        self.nr_accesses = nr_accesses
        self.age = age

        if self.size is None:
            self.size = [0, 2**64 - 1]
        if self.nr_accesses is None:
            self.nr_accesses = [0, 2**64 - 1]
        if self.age is None:
            self.age = [0, 2**64 - 1]

    def sysfs_dir(self):
        return os.path.join(self.scheme.sysfs_dir(), 'access_pattern')

    def stage(self):
        err = write_file(
                os.path.join(self.sysfs_dir(), 'sz', 'min'), self.size[0])
        if err is not None:
            return err
        err = write_file(
                os.path.join(self.sysfs_dir(), 'sz', 'max'), self.size[1])
        if err is not None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'nr_accesses', 'min'),
                self.nr_accesses[0])
        if err is not None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'nr_accesses', 'max'),
                self.nr_accesses[1])
        if err is not None:
            return err
        err = write_file(
                os.path.join(self.sysfs_dir(), 'age', 'min'), self.age[0])
        if err is not None:
            return err
        err = write_file(
                os.path.join(self.sysfs_dir(), 'age', 'max'), self.age[1])
        if err is not None:
            return err

qgoal_metric_user_input = 'user_input'
qgoal_metric_some_mem_psi_us = 'some_mem_psi_us'
qgoal_metrics = [qgoal_metric_user_input, qgoal_metric_some_mem_psi_us]

class DamosQuotaGoal:
    metric = None
    target_value = None
    current_value = None
    effective_bytes = None
    quota = None            # owner quota
    idx = None

    def __init__(self, metric, target_value=10000, current_value=0):
        self.metric = metric
        self.target_value = target_value
        self.current_value = current_value

    def sysfs_dir(self):
        return os.path.join(self.quota.sysfs_dir(), 'goals', '%d' % self.idx)

    def stage(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'target_metric'),
                         self.metric)
        if err is not None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'target_value'),
                         self.target_value)
        if err is not None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'current_value'),
                         self.current_value)
        if err is not None:
            return err
        return None

class DamosQuota:
    sz = None                   # size quota, in bytes
    ms = None                   # time quota
    goals = None                # quota goals
    reset_interval_ms = None    # quota reset interval
    scheme = None               # owner scheme

    def __init__(self, sz=0, ms=0, goals=None, reset_interval_ms=0):
        self.sz = sz
        self.ms = ms
        self.reset_interval_ms = reset_interval_ms
        self.goals = goals if goals is not None else []
        for idx, goal in enumerate(self.goals):
            goal.idx = idx
            goal.quota = self

    def sysfs_dir(self):
        return os.path.join(self.scheme.sysfs_dir(), 'quotas')

    def stage(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'bytes'), self.sz)
        if err is not None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'ms'), self.ms)
        if err is not None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'reset_interval_ms'),
                         self.reset_interval_ms)
        if err is not None:
            return err

        nr_goals_file = os.path.join(self.sysfs_dir(), 'goals', 'nr_goals')
        content, err = read_file(nr_goals_file)
        if err is not None:
            return err
        if int(content) != len(self.goals):
            err = write_file(nr_goals_file, len(self.goals))
            if err is not None:
                return err
        for goal in self.goals:
            err = goal.stage()
            if err is not None:
                return err
        return None

class DamosStats:
    nr_tried = None
    sz_tried = None
    nr_applied = None
    sz_applied = None
    qt_exceeds = None

    def __init__(self, nr_tried, sz_tried, nr_applied, sz_applied, qt_exceeds):
        self.nr_tried = nr_tried
        self.sz_tried = sz_tried
        self.nr_applied = nr_applied
        self.sz_applied = sz_applied
        self.qt_exceeds = qt_exceeds

class DamosTriedRegion:
    def __init__(self, start, end, nr_accesses, age):
        self.start = start
        self.end = end
        self.nr_accesses = nr_accesses
        self.age = age

class Damos:
    action = None
    access_pattern = None
    quota = None
    apply_interval_us = None
    # todo: Support watermarks, stats
    idx = None
    context = None
    tried_bytes = None
    stats = None
    tried_regions = None

    def __init__(self, action='stat', access_pattern=DamosAccessPattern(),
                 quota=DamosQuota(), apply_interval_us=0):
        self.action = action
        self.access_pattern = access_pattern
        self.access_pattern.scheme = self
        self.quota = quota
        self.quota.scheme = self
        self.apply_interval_us = apply_interval_us

    def sysfs_dir(self):
        return os.path.join(
                self.context.sysfs_dir(), 'schemes', '%d' % self.idx)

    def stage(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'action'), self.action)
        if err is not None:
            return err
        err = self.access_pattern.stage()
        if err is not None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'apply_interval_us'),
                         '%d' % self.apply_interval_us)
        if err is not None:
            return err

        err = self.quota.stage()
        if err is not None:
            return err

        # disable watermarks
        err = write_file(
                os.path.join(self.sysfs_dir(), 'watermarks', 'metric'), 'none')
        if err is not None:
            return err

        # disable filters
        err = write_file(
                os.path.join(self.sysfs_dir(), 'filters', 'nr_filters'), '0')
        if err is not None:
            return err

class DamonTarget:
    pid = None
    # todo: Support target regions if test is made
    idx = None
    context = None

    def __init__(self, pid):
        self.pid = pid

    def sysfs_dir(self):
        return os.path.join(
                self.context.sysfs_dir(), 'targets', '%d' % self.idx)

    def stage(self):
        err = write_file(
                os.path.join(self.sysfs_dir(), 'regions', 'nr_regions'), '0')
        if err is not None:
            return err
        return write_file(
                os.path.join(self.sysfs_dir(), 'pid_target'), self.pid)

class DamonAttrs:
    sample_us = None
    aggr_us = None
    update_us = None
    min_nr_regions = None
    max_nr_regions = None
    context = None

    def __init__(self, sample_us=5000, aggr_us=100000, update_us=1000000,
            min_nr_regions=10, max_nr_regions=1000):
        self.sample_us = sample_us
        self.aggr_us = aggr_us
        self.update_us = update_us
        self.min_nr_regions = min_nr_regions
        self.max_nr_regions = max_nr_regions

    def interval_sysfs_dir(self):
        return os.path.join(self.context.sysfs_dir(), 'monitoring_attrs',
                'intervals')

    def nr_regions_range_sysfs_dir(self):
        return os.path.join(self.context.sysfs_dir(), 'monitoring_attrs',
                'nr_regions')

    def stage(self):
        err = write_file(os.path.join(self.interval_sysfs_dir(), 'sample_us'),
                self.sample_us)
        if err is not None:
            return err
        err = write_file(os.path.join(self.interval_sysfs_dir(), 'aggr_us'),
                self.aggr_us)
        if err is not None:
            return err
        err = write_file(os.path.join(self.interval_sysfs_dir(), 'update_us'),
                self.update_us)
        if err is not None:
            return err

        err = write_file(
                os.path.join(self.nr_regions_range_sysfs_dir(), 'min'),
                self.min_nr_regions)
        if err is not None:
            return err

        err = write_file(
                os.path.join(self.nr_regions_range_sysfs_dir(), 'max'),
                self.max_nr_regions)
        if err is not None:
            return err

class DamonCtx:
    ops = None
    monitoring_attrs = None
    targets = None
    schemes = None
    kdamond = None
    idx = None

    def __init__(self, ops='paddr', monitoring_attrs=DamonAttrs(), targets=[],
            schemes=[]):
        self.ops = ops
        self.monitoring_attrs = monitoring_attrs
        self.monitoring_attrs.context = self

        self.targets = targets
        for idx, target in enumerate(self.targets):
            target.idx = idx
            target.context = self

        self.schemes = schemes
        for idx, scheme in enumerate(self.schemes):
            scheme.idx = idx
            scheme.context = self

    def sysfs_dir(self):
        return os.path.join(self.kdamond.sysfs_dir(), 'contexts',
                '%d' % self.idx)

    def stage(self):
        err = write_file(
                os.path.join(self.sysfs_dir(), 'operations'), self.ops)
        if err is not None:
            return err
        err = self.monitoring_attrs.stage()
        if err is not None:
            return err

        nr_targets_file = os.path.join(
                self.sysfs_dir(), 'targets', 'nr_targets')
        content, err = read_file(nr_targets_file)
        if err is not None:
            return err
        if int(content) != len(self.targets):
            err = write_file(nr_targets_file, '%d' % len(self.targets))
            if err is not None:
                return err
        for target in self.targets:
            err = target.stage()
            if err is not None:
                return err

        nr_schemes_file = os.path.join(
                self.sysfs_dir(), 'schemes', 'nr_schemes')
        content, err = read_file(nr_schemes_file)
        if err is not None:
            return err
        if int(content) != len(self.schemes):
            err = write_file(nr_schemes_file, '%d' % len(self.schemes))
            if err is not None:
                return err
        for scheme in self.schemes:
            err = scheme.stage()
            if err is not None:
                return err
        return None

class Kdamond:
    state = None
    pid = None
    contexts = None
    idx = None      # index of this kdamond between siblings
    kdamonds = None # parent

    def __init__(self, contexts=[]):
        self.contexts = contexts
        for idx, context in enumerate(self.contexts):
            context.idx = idx
            context.kdamond = self

    def sysfs_dir(self):
        return os.path.join(self.kdamonds.sysfs_dir(), '%d' % self.idx)

    def start(self):
        nr_contexts_file = os.path.join(self.sysfs_dir(),
                'contexts', 'nr_contexts')
        content, err = read_file(nr_contexts_file)
        if err is not None:
            return err
        if int(content) != len(self.contexts):
            err = write_file(nr_contexts_file, '%d' % len(self.contexts))
            if err is not None:
                return err

        for context in self.contexts:
            err = context.stage()
            if err is not None:
                return err
        err = write_file(os.path.join(self.sysfs_dir(), 'state'), 'on')
        return err

    def stop(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'state'), 'off')
        return err

    def update_schemes_tried_regions(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'state'),
                         'update_schemes_tried_regions')
        if err is not None:
            return err
        for context in self.contexts:
            for scheme in context.schemes:
                tried_regions = []
                tried_regions_dir = os.path.join(
                        scheme.sysfs_dir(), 'tried_regions')
                for filename in os.listdir(
                        os.path.join(scheme.sysfs_dir(), 'tried_regions')):
                    tried_region_dir = os.path.join(tried_regions_dir, filename)
                    if not os.path.isdir(tried_region_dir):
                        continue
                    region_values = []
                    for f in ['start', 'end', 'nr_accesses', 'age']:
                        content, err = read_file(
                                os.path.join(tried_region_dir, f))
                        if err is not None:
                            return err
                        region_values.append(int(content))
                    tried_regions.append(DamosTriedRegion(*region_values))
                scheme.tried_regions = tried_regions

    def update_schemes_tried_bytes(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'state'),
                'update_schemes_tried_bytes')
        if err is not None:
            return err
        for context in self.contexts:
            for scheme in context.schemes:
                content, err = read_file(os.path.join(scheme.sysfs_dir(),
                    'tried_regions', 'total_bytes'))
                if err is not None:
                    return err
                scheme.tried_bytes = int(content)

    def update_schemes_stats(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'state'),
                'update_schemes_stats')
        if err is not None:
            return err
        for context in self.contexts:
            for scheme in context.schemes:
                stat_values = []
                for stat in ['nr_tried', 'sz_tried', 'nr_applied',
                             'sz_applied', 'qt_exceeds']:
                    content, err = read_file(
                            os.path.join(scheme.sysfs_dir(), 'stats', stat))
                    if err is not None:
                        return err
                    stat_values.append(int(content))
                scheme.stats = DamosStats(*stat_values)

    def update_schemes_effective_quotas(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'state'),
                         'update_schemes_effective_quotas')
        if err is not None:
            return err
        for context in self.contexts:
            for scheme in context.schemes:
                for goal in scheme.quota.goals:
                    content, err = read_file(
                            os.path.join(scheme.quota.sysfs_dir(),
                                         'effective_bytes'))
                    if err is not None:
                        return err
                    goal.effective_bytes = int(content)
        return None

    def commit(self):
        nr_contexts_file = os.path.join(self.sysfs_dir(),
                'contexts', 'nr_contexts')
        content, err = read_file(nr_contexts_file)
        if err is not None:
            return err
        if int(content) != len(self.contexts):
            err = write_file(nr_contexts_file, '%d' % len(self.contexts))
            if err is not None:
                return err

        for context in self.contexts:
            err = context.stage()
            if err is not None:
                return err
        err = write_file(os.path.join(self.sysfs_dir(), 'state'), 'commit')
        return err


    def commit_schemes_quota_goals(self):
        for context in self.contexts:
            for scheme in context.schemes:
                for goal in scheme.quota.goals:
                    err = goal.stage()
                    if err is not None:
                        print('commit_schemes_quota_goals failed stagign: %s'%
                              err)
                        exit(1)
        return write_file(os.path.join(self.sysfs_dir(), 'state'),
                         'commit_schemes_quota_goals')

class Kdamonds:
    kdamonds = []

    def __init__(self, kdamonds=[]):
        self.kdamonds = kdamonds
        for idx, kdamond in enumerate(self.kdamonds):
            kdamond.idx = idx
            kdamond.kdamonds = self

    def sysfs_dir(self):
        return os.path.join(sysfs_root, 'kdamonds')

    def start(self):
        err = write_file(os.path.join(self.sysfs_dir(),  'nr_kdamonds'),
                '%s' % len(self.kdamonds))
        if err is not None:
            return err
        for kdamond in self.kdamonds:
            err = kdamond.start()
            if err is not None:
                return err
        return None

    def stop(self):
        for kdamond in self.kdamonds:
            err = kdamond.stop()
            if err is not None:
                return err
        return None
