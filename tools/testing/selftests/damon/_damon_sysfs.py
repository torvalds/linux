# SPDX-License-Identifier: GPL-2.0

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

class Damos:
    action = None
    access_pattern = None
    # todo: Support quotas, watermarks, stats, tried_regions
    idx = None
    context = None

    def __init__(self, action='stat', access_pattern=DamosAccessPattern()):
        self.action = action
        self.access_pattern = access_pattern
        self.access_pattern.scheme = self

class DamonTarget:
    pid = None
    # todo: Support target regions if test is made
    idx = None
    context = None

    def __init__(self, pid):
        self.pid = pid

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

class Kdamonds:
    kdamonds = []

    def __init__(self, kdamonds=[]):
        self.kdamonds = kdamonds
        for idx, kdamond in enumerate(self.kdamonds):
            kdamond.idx = idx
            kdamond.kdamonds = self
