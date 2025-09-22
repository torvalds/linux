#!/usr/bin/env bash

# This script is intended as a FileCheck replacement to update the test
# expectations in a -ast-dump test.
#
# Usage (to generate normal AST dump tests):
#
# $ lit -DFileCheck=$PWD/utils/make-ast-dump-check.sh test/AST/ast-dump-openmp-*
#
# Usage (to generate serialization AST dump tests):
#
# $ lit -DFileCheck="generate_serialization_test=1 $PWD/utils/make-ast-dump-check.sh"
#     test/AST/ast-dump-openmp-*

prefix=CHECK

if [ -z ${generate_serialization_test+x} ];
  then generate_serialization_test=0;
fi

while [[ "$#" -ne 0 ]]; do
  case "$1" in
  --check-prefix)
    shift
    prefix="$1"
    ;;
  --implicit-check-not)
    shift
    ;;
  -*)
    ;;
  *)
    file="$1"
    ;;
  esac
  shift
done

testdir="$(dirname "$file")"

read -r -d '' script <<REWRITE
BEGIN {
  skipping_builtins = 0
  matched_last_line = 0
}

/^[\`|].* line:/ {
  skipping_builtins = 0
}

/^[\`|].* col:/ {
  skipping_builtins = 0
}

{
  if (skipping_builtins == 1) {
    matched_last_line = 0
    next
  }
}

/TranslationUnitDecl/ {
  skipping_builtins = 1
}

{
  s = \$0
  gsub("0x[0-9a-fA-F]+", "{{.*}}", s)
  gsub("$testdir/", "{{.*}}", s)
  if ($generate_serialization_test == 1) {
    gsub(" imported", "{{( imported)?}}", s)
    gsub(" <undeserialized declarations>", "{{( <undeserialized declarations>)?}}", s)
    gsub("line:[0-9]+:[0-9]+", "line:{{.*}}", s)
    gsub("line:[0-9]+", "line:{{.*}}", s)
    gsub("col:[0-9]+", "col:{{.*}}", s)
    gsub(":[0-9]+:[0-9]+", "{{.*}}", s)
  }
}

matched_last_line == 0 {
  print "// ${prefix}:" s
}

matched_last_line == 1 {
  print "// ${prefix}-NEXT:" s
}

{
  matched_last_line = 1
}
REWRITE

echo "$script"

{
  cat "$file" | grep -v "$prefix"
  awk "$script"
} > "$file.new"

mv "$file.new" "$file"
