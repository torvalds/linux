# Load the discovery suite, but with a separate exec root.
import os

config.test_exec_root = os.path.dirname(__file__)
config.test_source_root = os.path.join(
    os.path.dirname(config.test_exec_root), "discovery"
)
lit_config.load_config(config, os.path.join(config.test_source_root, "lit.cfg"))
