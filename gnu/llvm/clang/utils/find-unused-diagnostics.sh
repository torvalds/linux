#!/usr/bin/env bash
#
# This script produces a list of all diagnostics that are defined
# in Diagnostic*.td files but not used in sources.
#

# Gather all diagnostic identifiers from the .td files.
ALL_DIAGS=$(grep -E --only-matching --no-filename '(err_|warn_|ext_|note_)[a-z_]+' ./include/clang/Basic/Diagnostic*.td)

# Now look for all potential identifiers in the source files.
ALL_SOURCES=$(find lib include tools utils -name \*.cpp -or -name \*.h)
DIAGS_IN_SOURCES=$(grep -E --only-matching --no-filename '(err_|warn_|ext_|note_)[a-z_]+' $ALL_SOURCES)

# Print all diags that occur in the .td files but not in the source.
comm -23 <(sort -u <<< "$ALL_DIAGS") <(sort -u <<< "$DIAGS_IN_SOURCES")
