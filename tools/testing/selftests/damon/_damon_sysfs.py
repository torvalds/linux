# SPDX-License-Identifier: GPL-2.0

import os

sysfs_root = '/sys/kernel/mm/damon/admin'

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

        if self.size == None:
            self.size = [0, 2**64 - 1]
        if self.nr_accesses == None:
            self.nr_accesses = [0, 2**64 - 1]
        if self.age == None:
            self.age = [0, 2**64 - 1]

    def sysfs_dir(self):
        return os.path.join(self.scheme.sysfs_dir(), 'access_pattern')

    def stage(self):
        err = write_file(
                os.path.join(self.sysfs_dir(), 'sz', 'min'), self.size[0])
        if err != None:
            return err
        err = write_file(
                os.path.join(self.sysfs_dir(), 'sz', 'max'), self.size[1])
        if err != None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'nr_accesses', 'min'),
                self.nr_accesses[0])
        if err != None:
            return err
        err = write_file(os.path.join(self.sysfs_dir(), 'nr_accesses', 'max'),
                self.nr_accesses[1])
        if err != None:
            return err
        err = write_file(
                os.path.join(self.sysfs_dir(), 'age', 'min'), self.age[0])
        if err != None:
            return err
        err = write_file(
                os.path.join(self.sysfs_dir(), 'age', 'max'), self.age[1])
        if err != None:
            return err

class Damos:
    action = None
    access_pattern = None
    # todo: Support quotas, watermarks, stats, tried_regions
    idx = None
    context = None
    tried_bytes = None

    def __init__(self, action='stat', access_pattern=DamosAccessPattern()):
        self.action = action
        self.access_pattern = access_pattern
        self.access_pattern.scheme = self

    def sysfs_dir(self):
        return os.path.join(
                self.context.sysfs_dir(), 'schemes', '%d' % self.idx)

    def stage(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'action'), self.action)
        if err != None:
            return err
        err = self.access_pattern.stage()
        if err != None:
            return err

        # disable quotas
        err = write_file(os.path.join(self.sysfs_dir(), 'quotas', 'ms'), '0')
        if err != None:
            return err
        err = write_file(
                os.path.join(self.sysfs_dir(), 'quotas', 'bytes'), '0')
        if err != None:
            return err

        # disable watermarks
        err = write_file(
                os.path.join(self.sysfs_dir(), 'watermarks', 'metric'), 'none')
        if err != None:
            return err

        # disable filters
        err = write_file(
                os.path.join(self.sysfs_dir(), 'filters', 'nr_filters'), '0')
        if err != None:
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
        if err != None:
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
        if err != None:
            return err
        err = write_file(os.path.join(self.interval_sysfs_dir(), 'aggr_us'),
                self.aggr_us)
        if err != None:
            return err
        err = write_file(os.path.join(self.interval_sysfs_dir(), 'update_us'),
                self.update_us)
        if err != None:
            return err

        err = write_file(
                os.path.join(self.nr_regions_range_sysfs_dir(), 'min'),
                self.min_nr_regions)
        if err != None:
            return err

        err = write_file(
                os.path.join(self.nr_regions_range_sysfs_dir(), 'max'),
                self.max_nr_regions)
        if err != None:
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
        if err != None:
            return err
        err = self.monitoring_attrs.stage()
        if err != None:
            return err

        nr_targets_file = os.path.join(
                self.sysfs_dir(), 'targets', 'nr_targets')
        content, err = read_file(nr_targets_file)
        if err != None:
            return err
        if int(content) != len(self.targets):
            err = write_file(nr_targets_file, '%d' % len(self.targets))
            if err != None:
                return err
        for target in self.targets:
            err = target.stage()
            if err != None:
                return err

        nr_schemes_file = os.path.join(
                self.sysfs_dir(), 'schemes', 'nr_schemes')
        content, err = read_file(nr_schemes_file)
        if int(content) != len(self.schemes):
            err = write_file(nr_schemes_file, '%d' % len(self.schemes))
            if err != None:
                return err
        for scheme in self.schemes:
            err = scheme.stage()
            if err != None:
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
        if err != None:
            return err
        if int(content) != len(self.contexts):
            err = write_file(nr_contexts_file, '%d' % len(self.contexts))
            if err != None:
                return err

        for context in self.contexts:
            err = context.stage()
            if err != None:
                return err
        err = write_file(os.path.join(self.sysfs_dir(), 'state'), 'on')
        return err

    def update_schemes_tried_bytes(self):
        err = write_file(os.path.join(self.sysfs_dir(), 'state'),
                'update_schemes_tried_bytes')
        if err != None:
            return err
        for context in self.contexts:
            for scheme in context.schemes:
                content, err = read_file(os.path.join(scheme.sysfs_dir(),
                    'tried_regions', 'total_bytes'))
                if err != None:
                    return err
                scheme.tried_bytes = int(content)

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
        if err != None:
            return err
        for kdamond in self.kdamonds:
            err = kdamond.start()
            if err != None:
                return err
        return None
