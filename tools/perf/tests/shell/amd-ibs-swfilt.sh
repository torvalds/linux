#!/bin/bash
# AMD IBS software filtering

ParanoidAndNotRoot() {
  [ "$(id -u)" != 0 ] && [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt $1 ]
}

echo "check availability of IBS swfilt"

# check if IBS PMU is available
if [ ! -d /sys/bus/event_source/devices/ibs_op ]; then
    echo "[SKIP] IBS PMU does not exist"
    exit 2
fi

# check if IBS PMU has swfilt format
if [ ! -f /sys/bus/event_source/devices/ibs_op/format/swfilt ]; then
    echo "[SKIP] IBS PMU does not have swfilt"
    exit 2
fi

echo "run perf record with modifier and swfilt"
err=0

# setting any modifiers should fail
perf record -B -e ibs_op//u -o /dev/null true 2> /dev/null
if [ $? -eq 0 ]; then
    echo "[FAIL] IBS PMU should not accept exclude_kernel"
    exit 1
fi

# setting it with swfilt should be fine
perf record -B -e ibs_op/swfilt/u -o /dev/null true
if [ $? -ne 0 ]; then
    echo "[FAIL] IBS op PMU cannot handle swfilt for exclude_kernel"
    exit 1
fi

if ! ParanoidAndNotRoot 1
then
    # setting it with swfilt=1 should be fine
    perf record -B -e ibs_op/swfilt=1/k -o /dev/null true
    if [ $? -ne 0 ]; then
        echo "[FAIL] IBS op PMU cannot handle swfilt for exclude_user"
        exit 1
    fi
else
    echo "[SKIP] not root and perf_event_paranoid too high for exclude_user"
    err=2
fi

# check ibs_fetch PMU as well
perf record -B -e ibs_fetch/swfilt/u -o /dev/null true
if [ $? -ne 0 ]; then
    echo "[FAIL] IBS fetch PMU cannot handle swfilt for exclude_kernel"
    exit 1
fi

# check system wide recording
if ! ParanoidAndNotRoot 0
then
    perf record -aB --synth=no -e ibs_op/swfilt/k -o /dev/null true
    if [ $? -ne 0 ]; then
        echo "[FAIL] IBS op PMU cannot handle swfilt in system-wide mode"
        exit 1
    fi
else
    echo "[SKIP] not root and perf_event_paranoid too high for system-wide/exclude_user"
    err=2
fi

echo "check number of samples with swfilt"

kernel_sample=$(perf record -e ibs_op/swfilt/u -o- true | perf script -i- -F misc | grep -c ^K)
if [ ${kernel_sample} -ne 0 ]; then
    echo "[FAIL] unexpected kernel samples: " ${kernel_sample}
    exit 1
fi

if ! ParanoidAndNotRoot 1
then
    user_sample=$(perf record -e ibs_fetch/swfilt/k -o- true | perf script -i- -F misc | grep -c ^U)
    if [ ${user_sample} -ne 0 ]; then
        echo "[FAIL] unexpected user samples: " ${user_sample}
        exit 1
    fi
else
    echo "[SKIP] not root and perf_event_paranoid too high for exclude_user"
    err=2
fi

exit $err
