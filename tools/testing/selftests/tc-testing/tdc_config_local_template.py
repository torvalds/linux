"""
tdc_config_local.py - tdc plugin-writer-specified values

Copyright (C) 2017 bjb@mojatatu.com
"""

import os

ENVIR = os.environ.copy()

ENV_LD_LIBRARY_PATH = os.getenv('LD_LIBRARY_PATH', '')
ENV_OTHER_LIB = os.getenv('OTHER_LIB', '')


# example adding value to NAMES, without editing tdc_config.py
EXTRA_NAMES = dict()
EXTRA_NAMES['SOME_BIN'] = os.path.join(os.getenv('OTHER_BIN', ''), 'some_bin')


# example adding values to ENVIR, without editing tdc_config.py
ENVIR['VALGRIND_LIB'] = '/usr/lib/valgrind'
ENVIR['VALGRIND_BIN'] = '/usr/bin/valgrind'
ENVIR['VGDB_BIN'] = '/usr/bin/vgdb'
