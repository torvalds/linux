#!/bin/bash

# Copies the 'demangle' library, excluding 'DemangleConfig.h', to llvm. If no
# llvm directory is specified, then assume a monorepo layout.

set -e

cd $(dirname $0)
HDRS="ItaniumDemangle.h ItaniumNodes.def StringViewExtras.h Utility.h"
LLVM_DEMANGLE_DIR=$1

if [[ -z "$LLVM_DEMANGLE_DIR" ]]; then
    LLVM_DEMANGLE_DIR="../../../llvm/include/llvm/Demangle"
fi

if [[ ! -d "$LLVM_DEMANGLE_DIR" ]]; then
    echo "No such directory: $LLVM_DEMANGLE_DIR" >&2
    exit 1
fi

read -p "This will overwrite the copies of $FILES in $LLVM_DEMANGLE_DIR; are you sure? [y/N]" -n 1 -r ANSWER
echo

if [[ $ANSWER =~ ^[Yy]$ ]]; then
    cp -f README.txt $LLVM_DEMANGLE_DIR
    chmod -w $LLVM_DEMANGLE_DIR/README.txt
    for I in $HDRS ; do
	rm -f $LLVM_DEMANGLE_DIR/$I
	dash=$(echo "$I---------------------------" | cut -c -27 |\
		   sed 's|[^-]*||')
	sed -e '1s|^//=*-* .*\..* -*.*=*// *$|//===--- '"$I $dash"'-*- mode:c++;eval:(read-only-mode) -*-===//|' \
	    -e '2s|^// *$|//       Do not edit! See README.txt.|' \
	    $I >$LLVM_DEMANGLE_DIR/$I
	chmod -w $LLVM_DEMANGLE_DIR/$I
    done
fi
