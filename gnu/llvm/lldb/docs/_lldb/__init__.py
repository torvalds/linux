from unittest.mock import Mock
import sys
import types

# This package acts as a mock implementation of the native _lldb module so
# that generating the LLDB documentation doesn't actually require building all
# of LLDB.
module_name = "_lldb"
sys.modules[module_name] = Mock()
