// Package tls provides a Go interface to the libtls library.
package tls

/*
#cgo LDFLAGS: -ltls -lssl -lcrypto

#include <stdlib.h>

#include <tls.h>

typedef void *tls;
*/
import "C"

import (
	"errors"
	"fmt"
	"time"
	"unsafe"
)

var (
	errWantPollIn  = errors.New("want poll in")
	errWantPollOut = errors.New("want poll out")
)

// ProtocolVersion represents a TLS protocol version.
type ProtocolVersion uint32

// String returns the string representation of a protocol version.
func (pv ProtocolVersion) String() string {
	name, ok := protocolNames[pv]
	if !ok {
		return fmt.Sprintf("unknown protocol version %x", uint32(pv))
	}
	return name
}

const (
	ProtocolTLSv10 ProtocolVersion = C.TLS_PROTOCOL_TLSv1_0
	ProtocolTLSv11 ProtocolVersion = C.TLS_PROTOCOL_TLSv1_1
	ProtocolTLSv12 ProtocolVersion = C.TLS_PROTOCOL_TLSv1_2
	ProtocolTLSv13 ProtocolVersion = C.TLS_PROTOCOL_TLSv1_3
	ProtocolsAll   ProtocolVersion = C.TLS_PROTOCOLS_ALL
)

var protocolNames = map[ProtocolVersion]string{
	ProtocolTLSv10: "TLSv1",
	ProtocolTLSv11: "TLSv1.1",
	ProtocolTLSv12: "TLSv1.2",
	ProtocolTLSv13: "TLSv1.3",
	ProtocolsAll:   "all",
}

// ProtocolVersionFromString returns the protocol version with the given name.
func ProtocolVersionFromString(version string) (ProtocolVersion, error) {
	for proto, name := range protocolNames {
		if version == name {
			return proto, nil
		}
	}
	return 0, fmt.Errorf("unknown protocol version %q", version)
}

// TLSConfig provides configuration options for a TLS context.
type TLSConfig struct {
	tlsCfg *C.struct_tls_config
}

// TLS encapsulates the TLS context.
type TLS struct {
	cfg *TLSConfig
	ctx *C.struct_tls
}

// Init initialises the TLS library.
func Init() error {
	if C.tls_init() != 0 {
		return errors.New("initialisation failed")
	}
	return nil
}

// NewConfig returns a new TLS configuration.
func NewConfig() (*TLSConfig, error) {
	cfg := C.tls_config_new()
	if cfg == nil {
		return nil, errors.New("failed to allocate config")
	}
	return &TLSConfig{
		tlsCfg: cfg,
	}, nil
}

// Error returns the error message from the TLS configuration.
func (c *TLSConfig) Error() error {
	if msg := C.tls_config_error(c.tlsCfg); msg != nil {
		return errors.New(C.GoString(msg))
	}
	return errors.New("unknown error")
}

// SetCAFile sets the CA file to be used for connections.
func (c *TLSConfig) SetCAFile(filename string) error {
	caFile := C.CString(filename)
	defer C.free(unsafe.Pointer(caFile))
	if C.tls_config_set_ca_file(c.tlsCfg, caFile) != 0 {
		return c.Error()
	}
	return nil
}

// SetCiphers sets the cipher suites enabled for the connection.
func (c *TLSConfig) SetCiphers(ciphers string) error {
	cipherStr := C.CString(ciphers)
	defer C.free(unsafe.Pointer(cipherStr))
	if C.tls_config_set_ciphers(c.tlsCfg, cipherStr) != 0 {
		return c.Error()
	}
	return nil
}

// SetProtocols sets the protocol versions enabled for the connection.
func (c *TLSConfig) SetProtocols(proto ProtocolVersion) error {
	if C.tls_config_set_protocols(c.tlsCfg, C.uint32_t(proto)) != 0 {
		return c.Error()
	}
	return nil
}

// InsecureNoVerifyCert disables certificate verification for the connection.
func (c *TLSConfig) InsecureNoVerifyCert() {
	C.tls_config_insecure_noverifycert(c.tlsCfg)
}

// InsecureNoVerifyName disables server name verification for the connection.
func (c *TLSConfig) InsecureNoVerifyName() {
	C.tls_config_insecure_noverifyname(c.tlsCfg)
}

// SetSecure enables verification for the connection.
func (c *TLSConfig) SetVerify() {
	C.tls_config_verify(c.tlsCfg)
}

// Free frees resources associated with the TLS configuration.
func (c *TLSConfig) Free() {
	if c.tlsCfg == nil {
		return
	}
	C.tls_config_free(c.tlsCfg)
	c.tlsCfg = nil
}

// NewClient returns a new TLS client context, using the optional configuration.
// If no configuration is specified the default configuration will be used.
func NewClient(config *TLSConfig) (*TLS, error) {
	var sslCfg *C.struct_tls_config
	if config != nil {
		sslCfg = config.tlsCfg
	}
	ctx := C.tls_client()
	if ctx == nil {
		return nil, errors.New("tls client failed")
	}
	if C.tls_configure(ctx, sslCfg) != 0 {
		return nil, errors.New("tls configure failed")
	}
	return &TLS{
		cfg: config,
		ctx: ctx,
	}, nil
}

// Error returns the error message from the TLS context.
func (t *TLS) Error() error {
	if msg := C.tls_error(t.ctx); msg != nil {
		return errors.New(C.GoString(msg))
	}
	return errors.New("unknown error")
}

// PeerCertProvided returns whether the peer provided a certificate.
func (t *TLS) PeerCertProvided() bool {
	return C.tls_peer_cert_provided(t.ctx) == 1
}

// PeerCertContainsName checks whether the peer certificate contains
// the specified name.
func (t *TLS) PeerCertContainsName(name string) bool {
	n := C.CString(name)
	defer C.free(unsafe.Pointer(n))
	return C.tls_peer_cert_contains_name(t.ctx, n) == 1
}

// PeerCertIssuer returns the issuer of the peer certificate.
func (t *TLS) PeerCertIssuer() (string, error) {
	issuer := C.tls_peer_cert_issuer(t.ctx)
	if issuer == nil {
		return "", errors.New("no issuer returned")
	}
	return C.GoString(issuer), nil
}

// PeerCertCommonName returns the common name of the peer certificate.
func (t *TLS) PeerCertCommonName() (string, error) {
	commonName := C.tls_peer_cert_common_name(t.ctx)
	if commonName == nil {
		return "", errors.New("no peer cert common name returned")
	}
	return C.GoString(commonName), nil
}

// PeerCertSubject returns the subject of the peer certificate.
func (t *TLS) PeerCertSubject() (string, error) {
	subject := C.tls_peer_cert_subject(t.ctx)
	if subject == nil {
		return "", errors.New("no subject returned")
	}
	return C.GoString(subject), nil
}

// PeerCertHash returns a hash of the peer certificate.
func (t *TLS) PeerCertHash() (string, error) {
	hash := C.tls_peer_cert_hash(t.ctx)
	if hash == nil {
		return "", errors.New("no hash returned")
	}
	return C.GoString(hash), nil
}

// PeerCertNotBefore returns the notBefore time from the peer
// certificate.
func (t *TLS) PeerCertNotBefore() (time.Time, error) {
	notBefore := C.tls_peer_cert_notbefore(t.ctx)
	if notBefore == -1 {
		return time.Time{}, errors.New("no notBefore time returned")
	}
	return time.Unix(int64(notBefore), 0), nil
}

// PeerCertNotAfter returns the notAfter time from the peer
// certificate.
func (t *TLS) PeerCertNotAfter() (time.Time, error) {
	notAfter := C.tls_peer_cert_notafter(t.ctx)
	if notAfter == -1 {
		return time.Time{}, errors.New("no notAfter time")
	}
	return time.Unix(int64(notAfter), 0), nil
}

// ConnVersion returns the protocol version of the connection.
func (t *TLS) ConnVersion() (ProtocolVersion, error) {
	ver := C.tls_conn_version(t.ctx)
	if ver == nil {
		return 0, errors.New("no connection version")
	}
	return ProtocolVersionFromString(C.GoString(ver))
}

// ConnCipher returns the cipher suite used for the connection.
func (t *TLS) ConnCipher() (string, error) {
	cipher := C.tls_conn_cipher(t.ctx)
	if cipher == nil {
		return "", errors.New("no connection cipher")
	}
	return C.GoString(cipher), nil
}

// ConnCipherStrength returns the strength in bits for the symmetric
// cipher that is used for the connection.
func (t *TLS) ConnCipherStrength() (int, error) {
	strength := C.tls_conn_cipher_strength(t.ctx)
	if strength == 0 {
		return 0, errors.New("no connection cipher strength")
	}
	return int(strength), nil
}

// Connect attempts to establish an TLS connection to the specified host on
// the given port. The host may optionally contain a colon separated port
// value if the port string is specified as an empty string.
func (t *TLS) Connect(host, port string) error {
	h := C.CString(host)
	var p *C.char
	if port != "" {
		p = C.CString(port)
	}
	defer C.free(unsafe.Pointer(h))
	defer C.free(unsafe.Pointer(p))
	if C.tls_connect(t.ctx, h, p) != 0 {
		return t.Error()
	}
	return nil
}

// Handshake attempts to complete the TLS handshake.
func (t *TLS) Handshake() error {
	ret := C.tls_handshake(t.ctx)
	switch {
	case ret == C.TLS_WANT_POLLIN:
		return errWantPollIn
	case ret == C.TLS_WANT_POLLOUT:
		return errWantPollOut
	case ret != 0:
		return t.Error()
	}
	return nil
}

// Read reads data the TLS connection into the given buffer.
func (t *TLS) Read(buf []byte) (int, error) {
	ret := C.tls_read(t.ctx, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))
	switch {
	case ret == C.TLS_WANT_POLLIN:
		return -1, errWantPollIn
	case ret == C.TLS_WANT_POLLOUT:
		return -1, errWantPollOut
	case ret < 0:
		return -1, t.Error()
	}
	return int(ret), nil
}

// Write writes the given data to the TLS connection.
func (t *TLS) Write(buf []byte) (int, error) {
	p := C.CString(string(buf))
	defer C.free(unsafe.Pointer(p))
	ret := C.tls_write(t.ctx, unsafe.Pointer(p), C.size_t(len(buf)))
	switch {
	case ret == C.TLS_WANT_POLLIN:
		return -1, errWantPollIn
	case ret == C.TLS_WANT_POLLOUT:
		return -1, errWantPollOut
	case ret < 0:
		return -1, t.Error()
	}
	return int(ret), nil
}

// Close closes the TLS connection.
func (t *TLS) Close() error {
	ret := C.tls_close(t.ctx)
	switch {
	case ret == C.TLS_WANT_POLLIN:
		return errWantPollIn
	case ret == C.TLS_WANT_POLLOUT:
		return errWantPollOut
	case ret != 0:
		return t.Error()
	}
	return nil
}

// Free frees resources associated with the TLS context.
func (t *TLS) Free() {
	if t.ctx == nil {
		return
	}
	C.tls_free(t.ctx)
	t.ctx = nil
}
