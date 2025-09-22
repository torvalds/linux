#!/bin/sh
# Usage: framework-header-fix.sh <source header dir> <LLDB Version>

set -e

for file in `find $1 -name "*.h"`
do
  /usr/bin/sed -i.bak 's/\(#include\)[ ]*"lldb\/\(API\/\)\{0,1\}\(.*\)"/\1 <LLDB\/\3>/1' "$file"
  /usr/bin/sed -i.bak 's|<LLDB/Utility|<LLDB|' "$file"
  LLDB_VERSION=`echo $2 | /usr/bin/sed -E 's/^([0-9]+).([0-9]+).([0-9]+)(.[0-9]+)?$/\\1/g'`
  LLDB_REVISION=`echo $2 | /usr/bin/sed -E 's/^([0-9]+).([0-9]+).([0-9]+)(.[0-9]+)?$/\\3/g'`
  LLDB_VERSION_STRING=`echo $2`
  /usr/bin/sed -i.bak "s|//#define LLDB_VERSION$|#define LLDB_VERSION $LLDB_VERSION |" "$file"
  /usr/bin/sed -i.bak "s|//#define LLDB_REVISION|#define LLDB_REVISION $LLDB_REVISION |" "$file"
  /usr/bin/sed -i.bak "s|//#define LLDB_VERSION_STRING|#define LLDB_VERSION_STRING \"$LLDB_VERSION_STRING\" |" "$file"
  rm -f "$file.bak"
done
