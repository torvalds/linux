# SPDX-License-Identifier: GPL-2.0-only
"""Validate KCONFIG_PROBABILITY without changing the supported distributions."""


def test_malformed_warns(conf):
    probabilities = [
        'invalid', ' ', '50%', '50 ', '0x32', '10 20', '10:20x',
        '10:20:invalid', '10:20:30x',
        ':50', '50:', '10::20', '10:20:', '10:20:30:', '10:20:30:40',
        '-0', '+0', '+', '-', '+100:0', ' \t100:0', '0: \t100:0',
    ]
    for probability in probabilities:
        assert conf._run_conf('--randconfig', extra_env={
            'KCONFIG_PROBABILITY': probability,
            'KCONFIG_SEED': '0',
        }) == 0, repr(probability)
        assert ('warning: KCONFIG_PROBABILITY has malformed format' in
                conf.stderr), repr(probability)
        assert conf.stderr.count('warning:') == 1, repr(probability)
        assert conf.config is not None, repr(probability)


def test_malformed_preserves_config(conf):
    probabilities = [('50%', '50:0:0'), ('-0', '0')]
    for seed in range(20):
        for probability, equivalent in probabilities:
            context = 'probability={!r}, equivalent={!r}, seed={}'.format(
                probability, equivalent, seed)
            assert conf._run_conf('--randconfig', extra_env={
                'KCONFIG_PROBABILITY': probability,
                'KCONFIG_SEED': hex(seed),
            }) == 0, context
            assert ('warning: KCONFIG_PROBABILITY has malformed format' in
                    conf.stderr), context
            assert conf.stderr.count('warning:') == 1, context
            malformed = conf.config

            assert conf._run_conf('--randconfig', extra_env={
                'KCONFIG_PROBABILITY': equivalent,
                'KCONFIG_SEED': hex(seed),
            }) == 0, context
            assert 'warning:' not in conf.stderr, context
            assert conf.config == malformed, context


def test_werror(conf):
    probabilities = [
        ('', 0), ('50', 0), ('50%', 1), ('-0', 1), ('10:20:30:40', 1),
    ]
    for probability, status in probabilities:
        for werror in ['', '0', '1']:
            context = 'probability={!r}, werror={!r}'.format(
                probability, werror)
            assert conf._run_conf('--randconfig', extra_env={
                'KCONFIG_PROBABILITY': probability,
                'KCONFIG_SEED': '0',
                'KCONFIG_WERROR': werror,
            }) == status, context
            if status:
                assert ('warning: KCONFIG_PROBABILITY has malformed format' in
                        conf.stderr), context
                assert conf.stderr.count('warning:') == 1, context
            else:
                assert 'warning:' not in conf.stderr, context


def test_out_of_range(conf):
    probabilities = [
        '-1', '101', '+101', '0:101', '0:0:101', '60:41', '0:60:41',
        '4294967296', '-4294967296',
        '999999999999999999999999', '-999999999999999999999999',
    ]
    for probability in probabilities:
        assert conf._run_conf('--randconfig', extra_env={
            'KCONFIG_PROBABILITY': probability,
            'KCONFIG_SEED': '0',
        }) == 1, repr(probability)
        assert 'KCONFIG_PROBABILITY:' in conf.stderr, repr(probability)


def test_valid(conf):
    probabilities = [
        ('0', 'n', 'n'),
        ('0:0', 'n', 'n'),
        ('100:0', 'y', 'y'),
        ('0:100', 'y', 'm'),
        ('100:0:0', 'y', 'n'),
        ('0:100:0', 'n', 'y'),
        ('0:0:100', 'n', 'm'),
        ('000:000:100', 'n', 'm'),
    ]
    for probability, boolean, tristate in probabilities:
        assert conf._run_conf('--randconfig', extra_env={
            'KCONFIG_PROBABILITY': probability,
            'KCONFIG_SEED': '0',
        }) == 0, repr(probability)
        assert 'warning:' not in conf.stderr, repr(probability)
        for symbol, value in [('BOOL', boolean), ('TRI', tristate)]:
            if value == 'n':
                expected = '# CONFIG_{} is not set'.format(symbol)
            else:
                expected = 'CONFIG_{}={}'.format(symbol, value)
            assert expected in conf.config.splitlines(), repr(probability)


def test_single_probability_matches_tristate_split(conf):
    for seed in range(20):
        context = 'seed={}'.format(seed)
        assert conf._run_conf('--randconfig', extra_env={
            'KCONFIG_PROBABILITY': '50',
            'KCONFIG_SEED': hex(seed),
        }) == 0, context
        assert 'warning:' not in conf.stderr, context
        single = conf.config

        # 50% boolean y; 25% tristate y, 25% m, and 50% n.
        assert conf._run_conf('--randconfig', extra_env={
            'KCONFIG_PROBABILITY': '50:25:25',
            'KCONFIG_SEED': hex(seed),
        }) == 0, context
        assert 'warning:' not in conf.stderr, context
        assert conf.config == single, context


def test_empty(conf):
    assert conf._run_conf('--randconfig', extra_env={
        'KCONFIG_PROBABILITY': '',
        'KCONFIG_SEED': '0',
    }) == 0
    assert 'warning:' not in conf.stderr
