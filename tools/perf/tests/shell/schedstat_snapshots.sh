#!/bin/sh
# Validate CPU and domain pairing in perf sched stats snapshots (no_mlock)
# SPDX-License-Identifier: GPL-2.0

set -e

# shellcheck source=lib/setup_python.sh
. "$(dirname "$0")/lib/setup_python.sh"

if ! perf version --build-options | grep -q 'libtraceevent:.*on'; then
	echo "[Skip] perf sched requires libtraceevent"
	exit 2
fi

$PYTHON - <<'PY'
import os
import re
import struct
import subprocess
import sys
import tempfile

# Native-endian perf.data with only NRCPUS and CPU_DOMAIN_INFO features.
endian = '<' if sys.byteorder == 'little' else '>'


def pack(fmt, *values):
    return struct.pack(endian + fmt, *values)


def string(value):
    data = value.encode() + b'\0'
    return pack('I', len(data)) + data


def cpu(cpu_id, timestamp, value, version):
    return pack('IHHQIHH6I3Q', 85, 0, 72, timestamp, cpu_id, version, 0,
                *([value] * 9))


def domain(cpu_id, domain_id, timestamp, value, version):
    # All supported versions use the largest union member's record size.
    return pack('IHHQIHH45I4x', 86, 0, 208, timestamp, cpu_id, version,
                domain_id, *([value] * 45))


def snapshot(timestamp, value, version=17, cpus=(0, 1, 2), domains=(0, 1)):
    records = []
    for cpu_id in cpus:
        records.append(cpu(cpu_id, timestamp, value + cpu_id * 100, version))
        for domain_id in domains:
            records.append(domain(cpu_id, domain_id, timestamp,
                                  value + cpu_id * 100, version))
    return records


def write_file(path, records, version=17):
    metadata = pack('II', version, 2)
    for cpu_id in range(3):
        metadata += pack('II', cpu_id, 2)
        for domain_id in range(2):
            metadata += pack('I', domain_id)
            if version >= 17:
                metadata += string('SMT' if domain_id == 0 else 'MC')
            metadata += string('7') + string('0-2')
    features = [pack('II', 3, 3), metadata]
    data = b''.join(records)
    offset = 104 + len(data) + 16 * len(features)
    sections = b''
    for feature in features:
        sections += pack('QQ', offset, len(feature))
        offset += len(feature)
    header = pack('13Q', 0x32454c4946524550, 104, 144, 104, 0,
                  104, len(data), 0, 0, (1 << 7) | (1 << 32), 0, 0, 0)
    with open(path, 'wb') as output:
        output.write(header + data + sections + b''.join(features))


def run(args, valid, domains=True):
    result = subprocess.run(['perf', 'sched', 'stats'] + args,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, timeout=10)
    if valid:
        assert result.returncode == 0, result.stderr
        assert re.search(r'^yld_count\s+:\s+10\b', result.stdout, re.M), result.stdout
        if domains:
            assert re.search(r'^busy_lb_count\s+:\s+10\b', result.stdout, re.M), result.stdout
        else:
            assert 'busy_lb_count' not in result.stdout, result.stdout
    else:
        assert result.returncode > 0, (args, result.returncode, result.stdout)
        assert 'Incompatible or incomplete schedstat snapshots' in result.stderr
        assert not result.stdout, result.stdout
    assert 'Sanitizer' not in result.stderr, result.stderr


with tempfile.TemporaryDirectory(prefix='perf-schedstat-') as directory:
    good = os.path.join(directory, 'good.data')
    test = os.path.join(directory, 'test.data')
    before = snapshot(100, 1000)
    after = snapshot(200, 1010)
    write_file(good, before + after)

    for version in (15, 16, 17):
        for timestamp in (100, 200):
            write_file(test, snapshot(100, 1000, version) +
                       snapshot(timestamp, 1010, version), version)
            run(['report', '-C', '0,1,2', '-i', test], True)
            run(['diff', test, test], True)
    print('Matching snapshots, including equal timestamps: [Success]')

    write_file(test, snapshot(100, 1000, domains=()) +
               snapshot(200, 1010, domains=()))
    run(['report', '-C', '0,1,2', '-i', test], True, domains=False)
    run(['diff', test, test], True, domains=False)
    print('CPUs without domains: [Success]')

    write_file(test, before + snapshot(200, 1010, cpus=(0, 1)))
    run(['report', '-C', '0,1', '-i', test], True)
    run(['report', '-C', '1', '-i', good], True)
    write_file(test, snapshot(100, 1000, cpus=(0, 1)) +
               snapshot(200, 1010, cpus=(0, 1)))
    run(['diff', good, test], True)
    print('CPU filtering and different CPU sets across files: [Success]')

    write_file(test, snapshot(100, 0xfffffffa, cpus=(0,)) +
               snapshot(200, 4, cpus=(0,)))
    run(['report', '-C', '0', '-i', test], True)
    print('Wrapping 32-bit counters: [Success]')

    cases = {
        'missing first CPU': before + snapshot(200, 1010, cpus=(1, 2)),
        'missing middle CPU': before + snapshot(200, 1010, cpus=(0, 2)),
        'missing last CPU': before + snapshot(200, 1010, cpus=(0, 1)),
        'added CPU': snapshot(100, 1000, cpus=(0, 1)) + after,
        'reordered CPUs': before + snapshot(200, 1010, cpus=(0, 2, 1)),
        'equal timestamp, missing first CPU': before + snapshot(100, 1010, cpus=(1, 2)),
        'missing first domain': before + snapshot(200, 1010, domains=(1,)),
        'missing last domain': before + after[:-1],
        'added domain': snapshot(100, 1000, domains=(0,)) + after,
        'reordered domains': before + snapshot(200, 1010, domains=(1, 0)),
        'domain without CPU': before[1:] + after,
        'wrong domain CPU': before + [after[0], domain(1, 0, 200, 1010, 17)] + after[2:],
        'changed version': before + snapshot(200, 1010, version=16),
        'backwards timestamp': before + snapshot(50, 1010),
        'third snapshot': before + after + snapshot(300, 1020),
        'missing second snapshot': before,
    }
    for name, records in cases.items():
        write_file(test, records)
        run(['report', '-C', '0,1,2', '-i', test], False)
        run(['diff', test, good], False)
        run(['diff', good, test], False)
        print(name + ': [Success]')
PY
