"""
Set random values recursively in nested choices.

Kconfig can create a choice-in-choice structure by using 'if' statement.
randconfig should correctly set random choice values.

Related Linux commit: 3b9a19e08960e5cdad5253998637653e592a3c29
"""


def test(conf):
    for i in range(20):
        assert conf.randconfig() == 0
        assert (conf.config_contains('expected_stdout0') or
                conf.config_contains('expected_stdout1') or
                conf.config_contains('expected_stdout2'))
