#!/bin/sh

prog=$(basename $0)

# Expect to be run from the parent lit directory.
if [ ! -f setup.py ] || [ ! -d lit ]; then
    printf 1>&2 "%s: expected to be run from base lit directory\n" "$prog"
    exit 1
fi

# Parse command line arguments.
if [ "$1" = "--generate-html" ]; then
    GENERATE_HTML=1
    shift
fi

# If invoked with no arguments, run all the tests.
if [ $# = "0" ]; then
    set -- "tests"
fi

# Check that the active python has been modified to enable coverage in its
# sitecustomize.
if ! python -c \
      'import sitecustomize, sys; sys.exit("coverage" not in dir(sitecustomize))' \
      >/dev/null 2>&1; then
    printf 1>&2 "error: active python does not appear to enable coverage in its 'sitecustomize.py'\n"
    exit 1
fi

# First, remove any existing coverage data files.
rm -f tests/.coverage
find tests -name .coverage.\* -exec rm {} \;

# Next, run the tests.
lit -sv --param check-coverage=1 "$@"

# Next, move all the data files from subdirectories up.
find tests/* -name .coverage.\* -exec mv {} tests \;

# Combine all the data files.
(cd tests && python -m coverage combine)

# Finally, generate the report.
(cd tests && python -m coverage report)

# Generate the HTML report, if requested.
if [ ! -z "$GENERATE_HTML" ]; then
    (cd tests && python -m coverage html)
fi
