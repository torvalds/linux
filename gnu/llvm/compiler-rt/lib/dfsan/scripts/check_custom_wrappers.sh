#!/bin/sh

DFSAN_DIR=$(dirname "$0")/../
DFSAN_CUSTOM_TESTS=${DFSAN_DIR}/../../test/dfsan/custom.cpp
DFSAN_CUSTOM_WRAPPERS=${DFSAN_DIR}/dfsan_custom.cpp
DFSAN_ABI_LIST=${DFSAN_DIR}/done_abilist.txt

DIFFOUT=$(mktemp -q /tmp/tmp.XXXXXXXXXX)
ERRORLOG=$(mktemp -q /tmp/tmp.XXXXXXXXXX)
DIFF_A=$(mktemp -q /tmp/tmp.XXXXXXXXXX)
DIFF_B=$(mktemp -q /tmp/tmp.XXXXXXXXXX)

on_exit() {
  rm -f ${DIFFOUT} 2> /dev/null
  rm -f ${ERRORLOG} 2> /dev/null
  rm -f ${DIFF_A} 2> /dev/null
  rm -f ${DIFF_B} 2> /dev/null
}

# Ignore __sanitizer_cov_trace* because they are implemented elsewhere.
trap on_exit EXIT
grep -E "^fun:.*=custom" ${DFSAN_ABI_LIST} \
  | grep -v "dfsan_get_label\|dfsan_get_origin\|__sanitizer_cov_trace" \
  | sed "s/^fun:\(.*\)=custom.*/\1/" | sort > $DIFF_A
grep -E "__dfsw.*\(" ${DFSAN_CUSTOM_WRAPPERS} \
  | grep -v "__sanitizer_cov_trace" \
  | sed "s/.*__dfsw_\(.*\)(.*/\1/" | sort | uniq > $DIFF_B
diff -u $DIFF_A $DIFF_B > ${DIFFOUT}
if [ $? -ne 0 ]
then
  echo -n "The following differences between the ABI list and ">> ${ERRORLOG}
  echo "the implemented custom wrappers have been found:" >> ${ERRORLOG}
  cat ${DIFFOUT} >> ${ERRORLOG}
fi

grep -E __dfsw_ ${DFSAN_CUSTOM_WRAPPERS} \
  | grep -v "__sanitizer_cov_trace" \
  | sed "s/.*__dfsw_\([^(]*\).*/\1/" | sort | uniq > $DIFF_A
grep -E "^[[:space:]]*test_.*\(\);" ${DFSAN_CUSTOM_TESTS} \
  | sed "s/.*test_\(.*\)();/\1/" | sort > $DIFF_B
diff -u $DIFF_A $DIFF_B > ${DIFFOUT}
if [ $? -ne 0 ]
then
  echo -n "The following differences between the implemented " >> ${ERRORLOG}
  echo "custom wrappers and the tests have been found:" >> ${ERRORLOG}
  cat ${DIFFOUT} >> ${ERRORLOG}
fi

if [ -s ${ERRORLOG} ]
then
  cat ${ERRORLOG}
  exit 1
fi

