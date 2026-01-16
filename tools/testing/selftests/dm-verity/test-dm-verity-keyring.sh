#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test script for dm-verity keyring functionality
#
# This script has two modes depending on kernel configuration:
#
# 1. keyring_unsealed=1 AND require_signatures=1:
#    - Upload a test key to the .dm-verity keyring
#    - Seal the keyring
#    - Create a dm-verity device with a signed root hash
#    - Verify signature verification works
#
# 2. keyring_unsealed=0 (default) OR require_signatures=0:
#    - Verify the keyring is already sealed (if unsealed=0)
#    - Verify keys cannot be added to a sealed keyring
#    - Verify the keyring is inactive (not used for verification)
#
# Requirements:
# - Root privileges
# - openssl
# - veritysetup (cryptsetup)
# - keyctl (keyutils)

set -e

WORK_DIR=""
DATA_DEV=""
HASH_DEV=""
DM_NAME="verity-test-$$"
CLEANUP_DONE=0

# Module parameters (detected at runtime)
KEYRING_UNSEALED=""
REQUIRE_SIGNATURES=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $*"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*" >&2
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $*"
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $*" >&2
}

log_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $*"
}

cleanup() {
    if [ "$CLEANUP_DONE" -eq 1 ]; then
        return
    fi
    CLEANUP_DONE=1

    log_info "Cleaning up..."

    # Remove dm-verity device if it exists
    if dmsetup info "$DM_NAME" &>/dev/null; then
        dmsetup remove "$DM_NAME" 2>/dev/null || true
    fi

    # Detach loop devices
    if [ -n "$DATA_DEV" ] && [[ "$DATA_DEV" == /dev/loop* ]]; then
        losetup -d "$DATA_DEV" 2>/dev/null || true
    fi
    if [ -n "$HASH_DEV" ] && [[ "$HASH_DEV" == /dev/loop* ]]; then
        losetup -d "$HASH_DEV" 2>/dev/null || true
    fi

    # Remove work directory
    if [ -n "$WORK_DIR" ] && [ -d "$WORK_DIR" ]; then
        rm -rf "$WORK_DIR"
    fi
}

trap cleanup EXIT

die() {
    log_error "$*"
    exit 1
}

find_dm_verity_keyring() {
    # The .dm-verity keyring is not linked to user-accessible keyrings,
    # so we need to find it via /proc/keys
    local serial_hex
    serial_hex=$(awk '/\.dm-verity/ {print $1}' /proc/keys 2>/dev/null)

    if [ -z "$serial_hex" ]; then
        return 1
    fi

    # Convert hex to decimal for keyctl
    echo $((16#$serial_hex))
}

get_module_param() {
    local param="$1"
    local path="/sys/module/dm_verity/parameters/$param"

    if [ -f "$path" ]; then
        cat "$path"
    else
        echo ""
    fi
}

check_requirements() {
    log_info "Checking requirements..."

    # Check for root
    if [ "$(id -u)" -ne 0 ]; then
        die "This script must be run as root"
    fi

    # Check for required tools
    for cmd in openssl veritysetup keyctl losetup dmsetup dd awk; do
        if ! command -v "$cmd" &>/dev/null; then
            die "Required command not found: $cmd"
        fi
    done

    # Check for dm-verity module
    if ! modprobe -n dm-verity &>/dev/null; then
        die "dm-verity module not available"
    fi

    # Verify OpenSSL can create signatures
    # OpenSSL cms -sign with -binary -outform DER creates detached signatures by default
    log_info "Using OpenSSL for PKCS#7 signatures"
}

load_dm_verity_module() {
    local keyring_unsealed="${1:-0}"
    local require_signatures="${2:-0}"

    log_info "Loading dm-verity module with keyring_unsealed=$keyring_unsealed require_signatures=$require_signatures"

    # Unload if already loaded
    if lsmod | grep -q '^dm_verity'; then
        log_info "Unloading existing dm-verity module..."
        modprobe -r dm-verity 2>/dev/null || \
            die "Failed to unload dm-verity module (may be in use)"
        sleep 1
    fi

    # Load with specified parameters
    modprobe dm-verity keyring_unsealed="$keyring_unsealed" require_signatures="$require_signatures" || \
        die "Failed to load dm-verity module"

    # Wait for keyring to be created (poll with timeout)
    local keyring_id=""
    local timeout=50  # 5 seconds (50 * 0.1s)
    while [ $timeout -gt 0 ]; do
        keyring_id=$(find_dm_verity_keyring) && break
        sleep 0.1
        timeout=$((timeout - 1))
    done

    if [ -z "$keyring_id" ]; then
        die "dm-verity keyring not found after module load (timeout)"
    fi

    log_info "Found .dm-verity keyring: $keyring_id"
    echo "$keyring_id" > "$WORK_DIR/keyring_id"

    # Read and display module parameters
    KEYRING_UNSEALED=$(get_module_param "keyring_unsealed")
    REQUIRE_SIGNATURES=$(get_module_param "require_signatures")

    log_info "Module parameters:"
    log_info "  keyring_unsealed=$KEYRING_UNSEALED"
    log_info "  require_signatures=$REQUIRE_SIGNATURES"
}

unload_dm_verity_module() {
    log_info "Unloading dm-verity module..."

    # Clean up any dm-verity devices first
    local dm_dev
    while read -r dm_dev _; do
        [ -n "$dm_dev" ] || continue
        log_info "Removing dm-verity device: $dm_dev"
        dmsetup remove "$dm_dev" 2>/dev/null || true
    done < <(dmsetup ls --target verity 2>/dev/null)

    if lsmod | grep -q '^dm_verity'; then
        modprobe -r dm-verity 2>/dev/null || \
            log_warn "Failed to unload dm-verity module"
        sleep 1
    fi
}

generate_keys() {
    log_info "Generating signing key pair..."

    # Generate private key (2048-bit for faster test execution)
    openssl genrsa -out "$WORK_DIR/private.pem" 2048 2>/dev/null

    # Create OpenSSL config for certificate extensions
    # The kernel requires digitalSignature key usage for signature verification
    # Both subjectKeyIdentifier and authorityKeyIdentifier are needed for
    # the kernel to match keys in the keyring (especially for self-signed certs)
    cat > "$WORK_DIR/openssl.cnf" << 'EOF'
[req]
distinguished_name = req_distinguished_name
x509_extensions = v3_ca
prompt = no

[req_distinguished_name]
CN = dm-verity-test-key

[v3_ca]
basicConstraints = critical,CA:FALSE
keyUsage = digitalSignature
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid
EOF

    # Generate self-signed certificate with proper extensions
    openssl req -new -x509 -key "$WORK_DIR/private.pem" \
        -out "$WORK_DIR/cert.pem" -days 365 \
        -config "$WORK_DIR/openssl.cnf" 2>/dev/null

    # Convert certificate to DER format for kernel
    openssl x509 -in "$WORK_DIR/cert.pem" -outform DER \
        -out "$WORK_DIR/cert.der"

    # Show certificate info for debugging
    log_info "Certificate details:"
    openssl x509 -in "$WORK_DIR/cert.pem" -noout -text 2>/dev/null | \
        grep -E "Subject:|Issuer:|Key Usage|Extended" | head -10

    log_info "Keys generated successfully"
}

seal_keyring() {
    log_info "Sealing the .dm-verity keyring..."

    local keyring_id
    keyring_id=$(cat "$WORK_DIR/keyring_id")

    keyctl restrict_keyring "$keyring_id" || \
        die "Failed to seal keyring"

    log_info "Keyring sealed successfully"
}

create_test_device() {
    log_info "Creating test device images..."

    # Create data image with random content (8MB is sufficient for testing)
    dd if=/dev/urandom of="$WORK_DIR/data.img" bs=1M count=8 status=none

    # Create hash image (will be populated by veritysetup)
    dd if=/dev/zero of="$WORK_DIR/hash.img" bs=1M count=1 status=none

    # Setup loop devices
    DATA_DEV=$(losetup --find --show "$WORK_DIR/data.img")
    HASH_DEV=$(losetup --find --show "$WORK_DIR/hash.img")

    log_info "Data device: $DATA_DEV"
    log_info "Hash device: $HASH_DEV"
}

create_verity_hash() {
    log_info "Creating dm-verity hash tree..."

    local root_hash output
    output=$(veritysetup format "$DATA_DEV" "$HASH_DEV" 2>&1)
    root_hash=$(echo "$output" | grep "Root hash:" | awk '{print $3}')

    if [ -z "$root_hash" ]; then
        log_error "veritysetup format output:"
        echo "$output" | sed 's/^/  /'
        die "Failed to get root hash from veritysetup format"
    fi

    echo "$root_hash" > "$WORK_DIR/root_hash"
    log_info "Root hash: $root_hash"
}

create_detached_signature() {
    local infile="$1"
    local outfile="$2"
    local cert="$3"
    local key="$4"

    # Use openssl smime (not cms) for PKCS#7 signatures compatible with kernel
    # Flags from working veritysetup example:
    #   -nocerts: don't include certificate in signature
    #   -noattr: no signed attributes
    #   -binary: binary input mode
    if openssl smime -sign -nocerts -noattr -binary \
        -in "$infile" \
        -inkey "$key" \
        -signer "$cert" \
        -outform der \
        -out "$outfile" 2>/dev/null; then
        return 0
    fi

    log_error "Failed to create signature"
    return 1
}

activate_verity_device() {
    local with_sig="$1"
    local root_hash
    root_hash=$(cat "$WORK_DIR/root_hash")

    # Clear dmesg and capture any kernel messages during activation
    dmesg -C 2>/dev/null || true

    if [ "$with_sig" = "yes" ]; then
        log_info "Activating dm-verity device with signature..."
        veritysetup open "$DATA_DEV" "$DM_NAME" "$HASH_DEV" "$root_hash" \
            --root-hash-signature="$WORK_DIR/root_hash.p7s" 2>&1
        local ret=$?
    else
        log_info "Activating dm-verity device without signature..."
        veritysetup open "$DATA_DEV" "$DM_NAME" "$HASH_DEV" "$root_hash" 2>&1
        local ret=$?
    fi

    # Show relevant kernel messages
    local kmsg
    kmsg=$(dmesg 2>/dev/null | grep -i -E 'verity|pkcs|signature|asymmetric|key' | tail -10)
    if [ -n "$kmsg" ]; then
        log_info "Kernel messages:"
        echo "$kmsg" | while read -r line; do echo "  $line"; done
    fi

    return $ret
}

deactivate_verity_device() {
    if dmsetup info "$DM_NAME" &>/dev/null; then
        dmsetup remove "$DM_NAME" 2>/dev/null || true
    fi
}

show_keyring_status() {
    log_info "Keyring status:"

    local keyring_id
    keyring_id=$(find_dm_verity_keyring) || true

    if [ -n "$keyring_id" ]; then
        echo "  Keyring ID: $keyring_id"
        keyctl show "$keyring_id" 2>/dev/null || true
        grep '\.dm-verity' /proc/keys 2>/dev/null || true
    fi
}

list_keyring_keys() {
    log_info "Keys in .dm-verity keyring:"

    local keyring_id
    keyring_id=$(cat "$WORK_DIR/keyring_id" 2>/dev/null) || \
        keyring_id=$(find_dm_verity_keyring) || true

    if [ -z "$keyring_id" ]; then
        log_warn "Could not find keyring"
        return
    fi

    # List all keys in the keyring
    local keys
    keys=$(keyctl list "$keyring_id" 2>/dev/null)
    if [ -z "$keys" ] || [ "$keys" = "keyring is empty" ]; then
        echo "  (empty)"
    else
        echo "$keys" | while read -r line; do
            echo "  $line"
        done

        # Show detailed info for each key
        log_info "Key details:"
        keyctl list "$keyring_id" 2>/dev/null | awk '{print $1}' | grep -E '^[0-9]+$' | while read -r key_id; do
            echo "  Key $key_id:"
            keyctl describe "$key_id" 2>/dev/null | sed 's/^/    /'
        done
    fi
}

generate_named_key() {
    local name="$1"
    local key_dir="$WORK_DIR/keys/$name"

    mkdir -p "$key_dir"

    # Log to stderr so it doesn't interfere with return value
    echo "[INFO] Generating key pair: $name" >&2

    # Generate private key
    openssl genrsa -out "$key_dir/private.pem" 2048 2>/dev/null

    # Create OpenSSL config for certificate extensions
    # Both subjectKeyIdentifier and authorityKeyIdentifier are needed for
    # the kernel to match keys in the keyring (especially for self-signed certs)
    cat > "$key_dir/openssl.cnf" << EOF
[req]
distinguished_name = req_distinguished_name
x509_extensions = v3_ca
prompt = no

[req_distinguished_name]
CN = dm-verity-test-$name

[v3_ca]
basicConstraints = critical,CA:FALSE
keyUsage = digitalSignature
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid
EOF

    # Generate self-signed certificate with proper extensions
    openssl req -new -x509 -key "$key_dir/private.pem" \
        -out "$key_dir/cert.pem" -days 365 \
        -config "$key_dir/openssl.cnf" 2>/dev/null

    # Convert certificate to DER format for kernel
    openssl x509 -in "$key_dir/cert.pem" -outform DER \
        -out "$key_dir/cert.der"

    # Return the key directory path (only this goes to stdout)
    echo "$key_dir"
}

upload_named_key() {
    local name="$1"
    local key_dir="$2"

    local keyring_id
    keyring_id=$(cat "$WORK_DIR/keyring_id")

    log_info "Uploading key '$name' to keyring..."

    local key_id
    if key_id=$(keyctl padd asymmetric "$name" "$keyring_id" \
        < "$key_dir/cert.der" 2>&1); then
        log_info "Key '$name' uploaded with ID: $key_id"
        echo "$key_id" > "$key_dir/key_id"
        return 0
    else
        log_error "Failed to upload key '$name': $key_id"
        return 1
    fi
}

#
# Test: Verify sealed keyring rejects key additions
#
test_sealed_keyring_rejects_keys() {
    log_info "TEST: Verify sealed keyring rejects key additions"

    local keyring_id
    keyring_id=$(cat "$WORK_DIR/keyring_id")

    generate_keys

    # Try to add a key - should fail
    if keyctl padd asymmetric "dm-verity-test" "$keyring_id" \
        < "$WORK_DIR/cert.der" 2>/dev/null; then
        log_fail "Key addition should have been rejected on sealed keyring"
        return 1
    else
        log_pass "Sealed keyring correctly rejected key addition"
        return 0
    fi
}

#
# Test: Multiple keys in keyring
#
test_multiple_keys() {
    log_info "TEST: Multiple keys in keyring"

    local key1_dir key2_dir key3_dir

    # Generate three different keys
    key1_dir=$(generate_named_key "vendor-a")
    key2_dir=$(generate_named_key "vendor-b")
    key3_dir=$(generate_named_key "vendor-c")

    # Upload all three keys
    upload_named_key "vendor-a" "$key1_dir" || return 1
    upload_named_key "vendor-b" "$key2_dir" || return 1
    upload_named_key "vendor-c" "$key3_dir" || return 1

    log_info ""
    log_info "Keys in keyring before sealing:"
    list_keyring_keys
    show_keyring_status

    # Seal the keyring
    log_info ""
    seal_keyring

    # List keys after sealing
    log_info ""
    log_info "Keys in keyring after sealing:"
    list_keyring_keys
    show_keyring_status

    log_pass "Key upload and keyring sealing succeeded"

    # Create test device
    log_info ""
    create_test_device
    create_verity_hash

    # Test 1: Sign with key1, should verify successfully
    log_info ""
    log_info "Sub-test: Verify with vendor-a key"
    if ! sign_root_hash_with_key "$key1_dir"; then
        log_fail "Failed to sign with vendor-a key"
        return 1
    fi
    if activate_verity_device "yes"; then
        log_pass "Verification with vendor-a key succeeded"
        deactivate_verity_device
    else
        log_fail "Verification with vendor-a key should succeed"
        return 1
    fi

    # Test 2: Sign with key2, should also verify successfully
    log_info ""
    log_info "Sub-test: Verify with vendor-b key"
    if ! sign_root_hash_with_key "$key2_dir"; then
        log_fail "Failed to sign with vendor-b key"
        return 1
    fi
    if activate_verity_device "yes"; then
        log_pass "Verification with vendor-b key succeeded"
        deactivate_verity_device
    else
        log_fail "Verification with vendor-b key should succeed"
        return 1
    fi

    # Test 3: Sign with key3, should also verify successfully
    log_info ""
    log_info "Sub-test: Verify with vendor-c key"
    if ! sign_root_hash_with_key "$key3_dir"; then
        log_fail "Failed to sign with vendor-c key"
        return 1
    fi
    if activate_verity_device "yes"; then
        log_pass "Verification with vendor-c key succeeded"
        deactivate_verity_device
    else
        log_fail "Verification with vendor-c key should succeed"
        return 1
    fi

    # Test 4: Generate a key NOT in the keyring, should fail
    log_info ""
    log_info "Sub-test: Verify with unknown key (should fail)"
    local unknown_key_dir
    unknown_key_dir=$(generate_named_key "unknown-vendor")
    if ! sign_root_hash_with_key "$unknown_key_dir"; then
        log_fail "Failed to sign with unknown-vendor key"
        return 1
    fi
    if activate_verity_device "yes"; then
        log_fail "Verification with unknown key should fail"
        deactivate_verity_device
        return 1
    else
        log_pass "Verification with unknown key correctly rejected"
    fi

    log_info ""
    log_pass "Multiple keys test completed successfully"
    return 0
}

sign_root_hash_with_key() {
    local key_dir="$1"

    local root_hash
    root_hash=$(cat "$WORK_DIR/root_hash")

    # Create the data to sign (hex string, not binary)
    echo -n "$root_hash" > "$WORK_DIR/root_hash.txt"

    # Debug: show exactly what we're signing
    log_info "Root hash (hex): $root_hash"
    log_info "Root hash hex string size: $(wc -c < "$WORK_DIR/root_hash.txt") bytes"

    # Create detached PKCS#7 signature
    if ! create_detached_signature "$WORK_DIR/root_hash.txt" "$WORK_DIR/root_hash.p7s" \
            "$key_dir/cert.pem" "$key_dir/private.pem"; then
        log_error "Failed to sign root hash with key from $key_dir"
        return 1
    fi

    # Debug: show signing certificate info
    log_info "Signed with certificate:"
    openssl x509 -in "$key_dir/cert.pem" -noout -subject 2>/dev/null | sed 's/^/  /'

    # Debug: verify signature locally
    # -nointern: cert not in signature, use -certfile
    # -noverify: skip certificate chain validation (self-signed)
    if openssl smime -verify -binary -inform der -nointern -noverify \
        -in "$WORK_DIR/root_hash.p7s" \
        -content "$WORK_DIR/root_hash.txt" \
        -certfile "$key_dir/cert.pem" \
        -out /dev/null 2>/dev/null; then
        log_info "Local signature verification: PASSED"
    else
        log_warn "Local signature verification: FAILED"
    fi
    return 0
}

#
# Test: Verify corrupted signatures are rejected
#
test_corrupted_signature() {
    log_info "TEST: Verify corrupted signatures are rejected"

    # This test requires a valid setup from test_multiple_keys or similar
    # It modifies the signature file and verifies rejection

    if [ ! -f "$WORK_DIR/root_hash.p7s" ]; then
        log_warn "No signature file found, skipping corrupted signature test"
        return 0
    fi

    # Save original signature
    cp "$WORK_DIR/root_hash.p7s" "$WORK_DIR/root_hash.p7s.orig"

    # Test 1: Truncated signature
    log_info "Sub-test: Truncated signature (should fail)"
    head -c 100 "$WORK_DIR/root_hash.p7s.orig" > "$WORK_DIR/root_hash.p7s"
    if activate_verity_device "yes"; then
        log_fail "Truncated signature should be rejected"
        deactivate_verity_device
        cp "$WORK_DIR/root_hash.p7s.orig" "$WORK_DIR/root_hash.p7s"
        return 1
    else
        log_pass "Truncated signature correctly rejected"
    fi

    # Test 2: Corrupted signature (flip some bytes)
    log_info "Sub-test: Corrupted signature bytes (should fail)"
    cp "$WORK_DIR/root_hash.p7s.orig" "$WORK_DIR/root_hash.p7s"
    # Corrupt bytes in the middle of the signature
    local sig_size
    sig_size=$(wc -c < "$WORK_DIR/root_hash.p7s")
    local corrupt_offset=$((sig_size / 2))
    printf '\xff\xff\xff\xff' | dd of="$WORK_DIR/root_hash.p7s" bs=1 seek=$corrupt_offset conv=notrunc 2>/dev/null
    if activate_verity_device "yes"; then
        log_fail "Corrupted signature should be rejected"
        deactivate_verity_device
        cp "$WORK_DIR/root_hash.p7s.orig" "$WORK_DIR/root_hash.p7s"
        return 1
    else
        log_pass "Corrupted signature correctly rejected"
    fi

    # Test 3: Signature over wrong data (sign different content)
    log_info "Sub-test: Signature over wrong data (should fail)"
    # Create a different root hash (all zeros as hex string)
    printf '%064d' 0 > "$WORK_DIR/wrong_hash.txt"
    # Get the first key directory that was used
    local key_dir="$WORK_DIR/keys/vendor-a"
    if [ -d "$key_dir" ]; then
        create_detached_signature "$WORK_DIR/wrong_hash.txt" "$WORK_DIR/root_hash.p7s" \
            "$key_dir/cert.pem" "$key_dir/private.pem"
        if activate_verity_device "yes"; then
            log_fail "Signature over wrong data should be rejected"
            deactivate_verity_device
            cp "$WORK_DIR/root_hash.p7s.orig" "$WORK_DIR/root_hash.p7s"
            return 1
        else
            log_pass "Signature over wrong data correctly rejected"
        fi
    else
        log_warn "Key directory not found, skipping wrong data test"
    fi

    # Restore original signature
    cp "$WORK_DIR/root_hash.p7s.orig" "$WORK_DIR/root_hash.p7s"

    log_pass "Corrupted signature test completed successfully"
    return 0
}

#
# Test: Verify keyring is sealed when keyring_unsealed=0
#
test_keyring_sealed_by_default() {
    log_info "TEST: Verify keyring is sealed by default (keyring_unsealed=0)"

    local keyring_id
    keyring_id=$(cat "$WORK_DIR/keyring_id")

    log_info "Current keyring state (should be empty and sealed):"
    list_keyring_keys
    show_keyring_status

    generate_keys

    # Try to add a key - should fail if keyring is sealed
    log_info "Attempting to add key to sealed keyring..."
    if keyctl padd asymmetric "dm-verity-test" "$keyring_id" \
        < "$WORK_DIR/cert.der" 2>/dev/null; then
        log_fail "Keyring should be sealed when keyring_unsealed=0"
        list_keyring_keys
        return 1
    else
        log_pass "Keyring is correctly sealed when keyring_unsealed=0"
        log_info "Keyring state after failed add attempt:"
        list_keyring_keys
        return 0
    fi
}

#
# Test: Verify dm-verity keyring is inactive when sealed empty
#
test_keyring_inactive_when_empty() {
    log_info "TEST: Verify dm-verity keyring is inactive when sealed empty"

    # When keyring_unsealed=0, the keyring is sealed immediately while empty
    # This means it should NOT be used for verification (nr_leaves_on_tree=0)

    log_info "Keyring state (should be empty and sealed):"
    list_keyring_keys
    show_keyring_status

    create_test_device
    create_verity_hash

    # Without any keys in the dm-verity keyring, and with it sealed,
    # verification should fall through to the secondary/platform keyrings
    # and likely succeed (if require_signatures=0) or fail (if =1)

    log_info "Sub-test: Device activation with sealed empty keyring"
    if [ "$REQUIRE_SIGNATURES" = "Y" ] || [ "$REQUIRE_SIGNATURES" = "1" ]; then
        if activate_verity_device "no"; then
            log_fail "Device should NOT activate without signature when require_signatures=1"
            deactivate_verity_device
            return 1
        else
            log_pass "Device correctly rejected (require_signatures=1, no valid signature)"
        fi
    else
        if activate_verity_device "no"; then
            log_pass "Device activated (require_signatures=0, empty dm-verity keyring is inactive)"
            deactivate_verity_device
        else
            log_fail "Device should activate when require_signatures=0"
            return 1
        fi
    fi

    return 0
}

main() {
    local rc=0

    log_info "=== dm-verity keyring test ==="
    log_info ""

    # Create work directory
    WORK_DIR=$(mktemp -d -t dm-verity-test.XXXXXX)
    log_info "Work directory: $WORK_DIR"

    check_requirements

    #
    # Test 1: UNSEALED keyring mode (keyring_unsealed=1)
    #
    log_info ""
    log_info "========================================"
    log_info "=== TEST MODE: UNSEALED KEYRING ==="
    log_info "========================================"
    log_info ""

    load_dm_verity_module 1 1  # keyring_unsealed=1, require_signatures=1
    show_keyring_status

    log_info ""
    if ! test_multiple_keys; then
        rc=1
    fi

    # After sealing, verify it rejects new keys
    log_info ""
    if ! test_sealed_keyring_rejects_keys; then
        rc=1
    fi

    # Test corrupted signatures are rejected
    log_info ""
    if ! test_corrupted_signature; then
        rc=1
    fi

    # Clean up devices before reloading module
    deactivate_verity_device
    if [ -n "$DATA_DEV" ] && [[ "$DATA_DEV" == /dev/loop* ]]; then
        losetup -d "$DATA_DEV" 2>/dev/null || true
        DATA_DEV=""
    fi
    if [ -n "$HASH_DEV" ] && [[ "$HASH_DEV" == /dev/loop* ]]; then
        losetup -d "$HASH_DEV" 2>/dev/null || true
        HASH_DEV=""
    fi

    #
    # Test 2: SEALED keyring mode (keyring_unsealed=0, default)
    #
    log_info ""
    log_info "========================================"
    log_info "=== TEST MODE: SEALED KEYRING (default) ==="
    log_info "========================================"
    log_info ""

    load_dm_verity_module 0 0  # keyring_unsealed=0, require_signatures=0
    show_keyring_status

    log_info ""
    if ! test_keyring_sealed_by_default; then
        rc=1
    fi

    log_info ""
    if ! test_keyring_inactive_when_empty; then
        rc=1
    fi

    #
    # Summary
    #
    log_info ""
    log_info "========================================"
    if [ $rc -eq 0 ]; then
        log_info "=== All tests PASSED ==="
    else
        log_error "=== Some tests FAILED ==="
    fi
    log_info "========================================"

    return $rc
}

main "$@"
