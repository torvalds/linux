# Check that --show-used-features works correctly.
#
# RUN: %{lit} %{inputs}/show-used-features --show-used-features | FileCheck %s
# CHECK: my-require-feature-1 my-require-feature-2 my-require-feature-3
# CHECK: my-unsupported-feature-1 my-unsupported-feature-2 my-unsupported-feature-3
# CHECK: my-xfail-feature-1 my-xfail-feature-2 my-xfail-feature-3
# CHECK: {{my-[{][{]\[require\]\*[}][}]-feature-4}}
# CHECK: {{my-[{][{]\[unsupported\]\*[}][}]-feature-4}}
# CHECK: {{my-[{][{]\[xfail\]\*[}][}]-feature-4}}
