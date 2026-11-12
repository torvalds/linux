#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Test kTLS hardware offload using a C helper binary."""

from collections import defaultdict

from lib.py import (
    CmdExitFailure,
    KsftNamedVariant,
    KsftSkipEx,
    NetDrvEpEnv,
    bkg,
    cmd,
    ksft_eq,
    ksft_exit,
    ksft_ge,
    ksft_pr,
    ksft_run,
    ksft_variants,
    rand_port,
    wait_port_listen,
)

# `ethtool -S` counters on the DUT NIC, from the minimum set every TLS
# offload driver should report (Documentation/networking/tls-offload.rst,
# "Statistics"). Drivers expose varying subsets: mlx5 has all four, nfp
# both *_packets but no *_tls_ctx, cxgb4 and funeth only the two tx_
# counters. Each counter is checked only when present.
# TlsTxDevice/TlsRxDevice in /proc/net/tls_stat only prove tls_dev_add()
# accepted the offload:
#  - tx_tls_encrypted_packets counts TX packets passed to the device for
#    encryption. Drivers bump it while building the descriptor, not on
#    completion, so it shows the driver used the offload path, not that
#    the NIC encrypted anything; a broken HW encrypt shows up instead as a
#    peer TlsDecryptError or an echo mismatch.
#  - rx_tls_decrypted_packets counts RX packets the NIC successfully
#    decrypted.
#  - Both *_packets count packets (mlx5 counts gso_segs), not records.
#  - *_ctx increment once per tls_dev_add().
# Missing NIC crypto counters remain optional.
HW_NIC_COUNTERS = {
    'Tx': {'crypto': 'tx_tls_encrypted_packets', 'ctx': 'tx_tls_ctx'},
    'Rx': {'crypto': 'rx_tls_decrypted_packets', 'ctx': 'rx_tls_ctx'},
}


def check_tls_support(cfg):
    """Require kTLS on both hosts and at least one DUT HW direction."""
    # The tls module is autoloaded lazily on the first TCP_ULP="tls"
    # setsockopt, so /proc/net/tls_stat (created from the module's pernet
    # init) may not exist yet on a freshly booted host. Load the module
    # explicitly before probing for it. Best effort only: with CONFIG_TLS=y
    # (what the config fragment asks for) there is nothing to load, and
    # modprobe may fail for reasons unrelated to kTLS (no modules.dep for a
    # locally built kernel, no modprobe on a minimal rootfs). The
    # /proc/net/tls_stat probe below is the authoritative check. Locally,
    # cmd() runs without a shell, so a missing modprobe raises
    # FileNotFoundError rather than returning a failed exit status.
    try:
        cmd("modprobe tls", fail=False)
    except FileNotFoundError:
        pass
    cmd("modprobe tls", host=cfg.remote, fail=False)
    try:
        cmd("test -f /proc/net/tls_stat")
        cmd("test -f /proc/net/tls_stat", host=cfg.remote)
    except CmdExitFailure as e:
        raise KsftSkipEx(f"kTLS not supported: {e}") from e

    try:
        features = cmd(f"ethtool -k {cfg.ifname}").stdout
    except CmdExitFailure as e:
        raise KsftSkipEx(f"Cannot determine TLS HW offload support: {e}") from e
    cfg.tls_hw_directions = {
        direction for direction in ('Tx', 'Rx')
        if f'tls-hw-{direction.lower()}-offload: on' in features
    }
    if not cfg.tls_hw_directions:
        raise KsftSkipEx("Device has no enabled TLS HW offload direction")


def require_tls_setup(cfg):
    """Deploy the helper and probe kTLS support once, on first use.

    This runs from within the test cases rather than main(): ksft_run()
    turns KsftSkipEx into a SKIP result only for an exception raised by a
    case, so a skip raised from main() would be reported as a failure.
    The outcome is cached so later cases skip with the same reason.
    """
    if cfg.tls_skip_reason:
        raise KsftSkipEx(cfg.tls_skip_reason)
    if cfg.bin_remote:
        return
    try:
        check_tls_support(cfg)
    except KsftSkipEx as e:
        cfg.tls_skip_reason = str(e)
        raise
    cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)


def read_tls_stats(host=None):
    """Snapshot the per-netns TLS MIB from /proc/net/tls_stat as a dict."""
    # /proc/net/tls_stat exposes the per-netns TLS MIB (TLS_INC_STATS on
    # sock_net(sk)). The test runs on a real NIC in the host namespace, so
    # these counters are shared with anything else doing kTLS there. The
    # strict before/after delta checks (zero error counters) assume no other
    # kTLS activity in this namespace during a
    # variant's window; concurrent kTLS users would perturb the deltas and
    # cause spurious failures. Don't run other kTLS workloads alongside this
    # test.
    stats = defaultdict(int)
    output = cmd("cat /proc/net/tls_stat", host=host)
    for line in output.stdout.strip().split('\n'):
        parts = line.split()
        if len(parts) == 2:
            stats[parts[0]] = int(parts[1])
    return stats


def read_nic_stats(cfg):
    """Snapshot the DUT NIC's `ethtool -S` counters as a dict."""
    # Driver per-record TLS counters from `ethtool -S` on the DUT NIC. Same
    # before/after-delta caveat as read_tls_stats(): these are device-wide,
    # so concurrent kTLS traffic on this NIC would perturb the deltas.
    stats = defaultdict(int)
    output = cmd(f"ethtool -S {cfg.ifname}").stdout
    for line in output.strip().split('\n'):
        key, sep, val = line.partition(':')
        if sep and val.strip().isdigit():
            stats[key.strip()] = int(val.strip())
    return stats


def stat_diff(before, after, key):
    """Return the delta of counter `key` between two stat snapshots."""
    return after[key] - before[key]


def check_hw_crypto(cfg, nic_before, nic_after, hw_directions):
    """Check DUT NIC offload-path packet counts and initial key installs."""
    for direction in hw_directions:
        names = HW_NIC_COUNTERS[direction]

        key = names['crypto']
        if key in nic_after:
            got = stat_diff(nic_before, nic_after, key)
            ksft_ge(got, 1,
                    comment=f"DUT {direction}: NIC reported no TLS "
                            f"offload packets ({key}={got})")
        else:
            ksft_pr(f"NOTE: DUT {direction}: '{key}' not exposed by "
                    f"{cfg.ifname}, skipping NIC packet count check")

        key = names['ctx']
        if key not in nic_after:
            ksft_pr(f"NOTE: DUT {direction}: '{key}' not exposed by "
                    f"{cfg.ifname}, skipping NIC context count check")
            continue
        ctx = stat_diff(nic_before, nic_after, key)
        ksft_ge(ctx, 1,
                comment=f"DUT {direction}: no HW context installed "
                        f"({key}={ctx})")


def check_path(before, after, direction, role, require_hw):
    """Require HW for enabled DUT directions; otherwise HW or SW is fine."""
    dev = stat_diff(before, after, f'Tls{direction}Device')
    sw = stat_diff(before, after, f'Tls{direction}Sw')
    if require_hw:
        ksft_ge(dev, 1,
                comment=f"{role} {direction}: HW offload not engaged "
                        f"(Device={dev}, Sw={sw})")
    else:
        ksft_ge(dev + sw, 1,
                comment=f"{role} {direction}: no TLS activity "
                        f"(Device={dev}, Sw={sw})")


def verify_tls_counters(stats_before, stats_after, is_dut, hw_directions=()):
    """Verify TLS counters on one side of the connection.

    is_dut: True for the local DUT.
    hw_directions: DUT directions with enabled HW offload; other directions
           may use SW kTLS but still undergo activity and error checks.
    """
    role = 'DUT' if is_dut else 'Peer'

    # Echo mode drives both directions on both sides.
    for direction in ('Tx', 'Rx'):
        check_path(stats_before, stats_after, direction, role,
                   require_hw=is_dut and direction in hw_directions)

    ksft_eq(stat_diff(stats_before, stats_after, 'TlsDecryptError'), 0,
            comment=f"{role}: TlsDecryptError expected 0")


def run_tls_test(cfg, cipher="128", tls_version="1.2",
                 buffer_size=None, random_max=None, ipver="4",
                 record_type=None):
    """Run the TLS offload test.

    The DUT runs the TLS client and the remote runs the echo server.

    ipver: '4' or '6' - IP version to run over. The C helper is forced to the
           matching family with -4/-6 and connects to the peer's v4/v6 address.
           Variants requesting '6' skip cleanly when the environment lacks IPv6
           connectivity (require_ipver()).

    The DUT (local) is the kernel under test; the remote is just a traffic
    source/sink and may run any kernel without HW offload. Both sides run
    kTLS because TLS is pairwise, but verify_tls_counters() requires HW
    offload only in enabled DUT directions. The peer and the DUT's other
    direction may use SW kTLS.
    """
    require_tls_setup(cfg)

    cfg.require_ipver(ipver)

    # Echo exercises both directions, allowing SW in a direction without
    # HW support.
    hw_directions = tuple(d for d in ('Tx', 'Rx') if d in cfg.tls_hw_directions)

    if record_type and 'Tx' not in hw_directions:
        raise KsftSkipEx("Record-type regression requires DUT TLS TX offload")

    port = rand_port()
    send_size = random_max or buffer_size

    server_parts = [f"{cfg.bin_remote} server -p {port} -c {cipher}",
                    f"-v {tls_version}", f"-{ipver}"]
    if send_size:
        server_parts.append(f"-b {send_size}")
    if record_type:
        server_parts.append(f"-t {record_type}")
    server_cmd = " ".join(server_parts)

    client_parts = [f"{cfg.bin_local} client -s {cfg.remote_addr_v[ipver]}",
                    f"-p {port} -c {cipher} -v {tls_version} -{ipver}"]
    if random_max:
        client_parts.append(f"-r {random_max}")
    elif buffer_size:
        client_parts.append(f"-b {buffer_size}")
    if record_type:
        client_parts.append(f"-t {record_type}")
    client_cmd = " ".join(client_parts)

    stats_before_local = read_tls_stats()
    stats_before_remote = read_tls_stats(host=cfg.remote)
    nic_before = read_nic_stats(cfg)

    # The remote server runs under bkg(), whose stdout pipe is only drained
    # once it exits, so the helper keeps its per-connection output small
    # (see the note on verbose in tls_hw_offload.c); do not pass -V.
    #
    # Both helpers exit on their own within HELPER_TIMEOUT_S (see
    # tls_hw_offload.c), so nothing outlives the variant even on failure.
    with bkg(server_cmd, host=cfg.remote, exit_wait=True):
        wait_port_listen(port, host=cfg.remote)
        cmd(client_cmd)

    cfg.wait_hw_stats_settle()
    stats_after_local = read_tls_stats()
    stats_after_remote = read_tls_stats(host=cfg.remote)
    nic_after = read_nic_stats(cfg)

    verify_tls_counters(stats_before_local, stats_after_local, is_dut=True,
                        hw_directions=hw_directions)
    check_hw_crypto(cfg, nic_before, nic_after, hw_directions)
    verify_tls_counters(stats_before_remote, stats_after_remote, is_dut=False)


# The cipher/version matrix runs over IPv4; the socket setup is the only
# IP-version-specific code path, so a single representative variant over
# IPv6 is enough to cover it (it skips cleanly without v6 connectivity).
# The suites below likewise stay on IPv4 to bound runtime.
@ksft_variants([
    KsftNamedVariant("tls12_aes128", "128", "1.2", "4"),
    KsftNamedVariant("tls12_aes256", "256", "1.2", "4"),
    KsftNamedVariant("tls12_aes128_ip6", "128", "1.2", "6"),
])
def test_tls_offload(cfg, cipher, tls_version, ipver):
    """Cipher/version matrix over the HW offload data path."""
    run_tls_test(cfg, cipher=cipher, tls_version=tls_version, ipver=ipver)


@ksft_variants([
    KsftNamedVariant("small_buf", 512),
    KsftNamedVariant("large_buf", 2097152),
    KsftNamedVariant("random_buf", None, 8192),
])
def test_tls_offload_sizes(cfg, buffer_size=None, random_max=None):
    """Echo-mode TLS 1.2 across a range of buffer sizes."""
    run_tls_test(cfg, cipher="128", tls_version="1.2",
                 buffer_size=buffer_size, random_max=random_max)


@ksft_variants([
    KsftNamedVariant("handshake", 22),
    KsftNamedVariant("alert", 21),
])
def test_tls_offload_record_type(cfg, record_type):
    """RX setup must preserve the HW TX hook which flushes an open record."""
    run_tls_test(cfg, record_type=record_type)


def main() -> None:
    """Set up the DUT/peer environment and run the offload test suites."""
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        cfg.bin_local = cfg.test_dir / "tls_hw_offload"
        cfg.bin_remote = None
        cfg.tls_skip_reason = None

        ksft_run([test_tls_offload, test_tls_offload_sizes,
                  test_tls_offload_record_type], args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
