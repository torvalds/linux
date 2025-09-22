package tls

import (
	"crypto/tls"
	"encoding/pem"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"strings"
	"testing"
	"time"
)

const (
	httpContent = "Hello, TLS!"

	certHash = "SHA256:1153aa0230ee0481b36bdd83ddb04b607340dbda35f3a4fff0615e4d9292d687"
)

var (
	certNotBefore = time.Unix(0, 0)
	certNotAfter  = certNotBefore.Add(1000000 * time.Hour)

	// Generated with:
	//   go run crypto/tls/generate_cert.go --rsa-bits 2048 \
	//     --host 127.0.0.1,::1,example.com --ca \
	//     --start-date "Jan 1 00:00:00 1970" --duration=1000000h`
	testServerCert = []byte(`-----BEGIN CERTIFICATE-----
MIIDGTCCAgGgAwIBAgIRAJHZkrBTk/yTKT3L2Z+dgZcwDQYJKoZIhvcNAQELBQAw
EjEQMA4GA1UEChMHQWNtZSBDbzAgFw03MDAxMDEwMDAwMDBaGA8yMDg0MDEyOTE2
MDAwMFowEjEQMA4GA1UEChMHQWNtZSBDbzCCASIwDQYJKoZIhvcNAQEBBQADggEP
ADCCAQoCggEBANxPCe2pafj2pWdA+hnN+Ne9Auh2YdoSPbQqIPQMVTT/3j6w9LlW
JirXCLWNuoarOA2iCgVa4Y607O/f2FTN7cKY2lvhAkuftCUJzB/lJVH5bWZgSrks
3AaOBlBcyMKBajFoEOIgSMwHGAZO2XsWIvdbuQw3EKY50vfBvxQspjbMruhpZoKd
9tHx6XUBoYSf5t9X+FrG2UjihnfcfBcsKGq7lbujt3+3QvlU6w1ZGOX+f9b+3+yw
RQkswxvkzKpfmgvr8GWWUm8w9wkImhAmA2UAhsM8OwnKVltvMih5mb9L2hw5+qBV
W1V+CSR0tDI9D8eiL26B3dvOilpZjttp3fsCAwEAAaNoMGYwDgYDVR0PAQH/BAQD
AgKkMBMGA1UdJQQMMAoGCCsGAQUFBwMBMA8GA1UdEwEB/wQFMAMBAf8wLgYDVR0R
BCcwJYILZXhhbXBsZS5jb22HBH8AAAGHEAAAAAAAAAAAAAAAAAAAAAEwDQYJKoZI
hvcNAQELBQADggEBAA+tFyWOZLoUjN2SKcIXnN5zs4VZedybLFVZoJSDakgo8awS
HPkD/1ReIOzT41Hmzjs/4CeVw6FSeKnYtEqTPOJoRVrXAIqyHGHJ2cEWpUXvg2b0
u7lkFfElRqcBjsDZYr+rJUxkjlCa11ylCbgdwoDMIbKNldcJoLB0iwQWUE7j19xe
CF32aISt/nGxCYcO81tn+ionwzZyf5zh5k/SmAqrPy4O/qxn8oEaox4Z7BfoZlAS
gmPA2gedTWORfthamJdT2irz3rdHjV7NWxwTsgOAx9y+P3fqmMCyMwxFJkmP118W
yM5xDRR+ldYKoRts5qkPR6LVtCw9kn+dJKQm0Bc=
-----END CERTIFICATE-----`)
	testServerKey = []byte(`-----BEGIN PRIVATE KEY-----
MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDcTwntqWn49qVn
QPoZzfjXvQLodmHaEj20KiD0DFU0/94+sPS5ViYq1wi1jbqGqzgNogoFWuGOtOzv
39hUze3CmNpb4QJLn7QlCcwf5SVR+W1mYEq5LNwGjgZQXMjCgWoxaBDiIEjMBxgG
Ttl7FiL3W7kMNxCmOdL3wb8ULKY2zK7oaWaCnfbR8el1AaGEn+bfV/haxtlI4oZ3
3HwXLChqu5W7o7d/t0L5VOsNWRjl/n/W/t/ssEUJLMMb5MyqX5oL6/BlllJvMPcJ
CJoQJgNlAIbDPDsJylZbbzIoeZm/S9ocOfqgVVtVfgkkdLQyPQ/Hoi9ugd3bzopa
WY7bad37AgMBAAECggEAKSmnbD1sLHVBIFK2qB2g8gZBxddgnxMrbj802wR24ykv
iD0d7IcZKIX2/Z0WjdTt6zYscyWw4S4S2xrS272CQAq5OdOHz0NusEDtX8Q7vd5B
v5AcRg8IqTzeFyPO6vCtO7/675UipORqa7tNzT6sl9UOdSbQuI4zSdFsd0OEZtZs
oqKkv+jdvKaVJQ1vsoQSup23V0bvVCRydBNFZ2mQ3etZcRRYXyu1digJ7/oMyCHf
1F37lJmjPJ8WwsGJMk2ngJiUOQx21bbTZ22c/8sMMLJjhhC2qu984keVRanSsJwC
Z80XcCxL/yoLF42A5ReMtutFs83rW7VX7VJGxoOaKQKBgQD91dCs9jGBVxHr4vxy
NJC2n92R5/JpqXygXS3L6RDP+we6p/fmSd4/QllwIVeC9kepsFz4//ArmLOy9JHu
rkXa7W9G+XbmYXSftmG78roLfAtvdudoytLQJg2bu8J3bzPVibRCUq0OORFyqOHR
QGcQtvPxwFHctIkOjajbLpbL7QKBgQDeMAbvjzD8wU1gp2U8Bl5l5ohYgQpcs15R
eBPS5DlRzICAeuQWi+BRjW5BPZVmTr3Ml3KiMWcalXMeH1TKoPON8RjmWgY2Fxvh
nS7gV+rJm4H0T+bBEXNAraracwGYd6JgcH9BPD9znHRmyFR4pMPqkSa7o/OExH6W
O32KqTMkhwKBgQCoX7wb/vK3qNnqbpal6thTS5fdwM276QESHrzSFbdhPlLSLbjy
uO0DaS+KgZNa+6JtnN8PDDZztMb+XdyvRkpv/i9iFPgZuWtyxbfuxANEuvOa7HRz
vpY4HAXK17EXKFxpuP4pQE4qsRAxznR8KQw0uib2pWunytlfHfhz62N7wQKBgQC+
+TTc74zBkzx42SiwYSD+IRoMSE2pxBpLmBQh7jw+TLIevIITxwJ11kRwGwiwuPl2
Qq4rLp9aQB6EQ5XT3Ge7FwG57KLuFwrF7x59gdOymdEnNw414FPZwevae4Nhk2Kj
1c3rOmenbVC3j3TbhXNHyJ8sJQ2IjoPniRas+iWVPQKBgQDcZzKh6U+e9efYLAt0
qdaKhm5MCAgzD9X/Tx2OvOqgWnSXt2Q2AhK2UsHnrGBn1SDNTNDGlniQy2OM2Ywn
n71nH1QUmgoiwBrFUh0gLxv878vwgUATqfFRTlirmK9XxHuTX9Jh6elmRebjYWyc
Oo0CJBeMABu71Y+VkCxURT1bzw==
-----END PRIVATE KEY-----`)
)

type handshakeError string

func (he handshakeError) Error() string {
	return string(he)
}

// createCAFile writes a PEM encoded version of the certificate out to a
// temporary file, for use by libtls.
func createCAFile(cert []byte) (string, error) {
	f, err := ioutil.TempFile("", "tls")
	if err != nil {
		return "", fmt.Errorf("failed to create file: %v", err)
	}
	defer f.Close()
	block := &pem.Block{
		Type:  "CERTIFICATE",
		Bytes: cert,
	}
	if err := pem.Encode(f, block); err != nil {
		return "", fmt.Errorf("failed to encode certificate: %v", err)
	}
	return f.Name(), nil
}

func newTestServer(tlsCfg *tls.Config) (*httptest.Server, *url.URL, string, error) {
	ts := httptest.NewUnstartedServer(
		http.HandlerFunc(
			func(w http.ResponseWriter, r *http.Request) {
				fmt.Fprintln(w, httpContent)
			},
		),
	)
	if tlsCfg == nil {
		tlsCfg = &tls.Config{}
	}
	if len(tlsCfg.Certificates) == 0 {
		cert, err := tls.X509KeyPair(testServerCert, testServerKey)
		if err != nil {
			return nil, nil, "", fmt.Errorf("failed to load key pair: %v", err)
		}
		tlsCfg.Certificates = []tls.Certificate{cert}
	}
	ts.TLS = tlsCfg
	ts.StartTLS()

	u, err := url.Parse(ts.URL)
	if err != nil {
		return nil, nil, "", fmt.Errorf("failed to parse URL %q: %v", ts.URL, err)
	}

	caFile, err := createCAFile(ts.TLS.Certificates[0].Certificate[0])
	if err != nil {
		return nil, nil, "", fmt.Errorf("failed to create CA file: %v", err)
	}

	return ts, u, caFile, nil
}

func handshakeVersionTest(tlsCfg *tls.Config) (ProtocolVersion, error) {
	ts, u, caFile, err := newTestServer(tlsCfg)
	if err != nil {
		return 0, fmt.Errorf("failed to start test server: %v", err)
	}
	defer os.Remove(caFile)
	defer ts.Close()

	if err := Init(); err != nil {
		return 0, err
	}

	cfg, err := NewConfig()
	if err != nil {
		return 0, err
	}
	defer cfg.Free()
	if err := cfg.SetCAFile(caFile); err != nil {
		return 0, err
	}
	if err := cfg.SetCiphers("compat"); err != nil {
		return 0, err
	}
	if err := cfg.SetProtocols(ProtocolsAll); err != nil {
		return 0, err
	}

	tls, err := NewClient(cfg)
	if err != nil {
		return 0, err
	}
	defer tls.Free()

	if err := tls.Connect(u.Host, ""); err != nil {
		return 0, err
	}
	if err := tls.Handshake(); err != nil {
		return 0, handshakeError(err.Error())
	}
	version, err := tls.ConnVersion()
	if err != nil {
		return 0, err
	}
	if err := tls.Close(); err != nil {
		return 0, err
	}
	return version, nil
}

func TestTLSBasic(t *testing.T) {
	ts, u, caFile, err := newTestServer(nil)
	if err != nil {
		t.Fatalf("Failed to start test server: %v", err)
	}
	defer os.Remove(caFile)
	defer ts.Close()

	if err := Init(); err != nil {
		t.Fatal(err)
	}

	cfg, err := NewConfig()
	if err != nil {
		t.Fatal(err)
	}
	defer cfg.Free()
	if err := cfg.SetCAFile(caFile); err != nil {
		t.Fatal(err)
	}

	tls, err := NewClient(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer tls.Free()

	t.Logf("Connecting to %s", u.Host)

	if err := tls.Connect(u.Host, ""); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := tls.Close(); err != nil {
			t.Fatalf("Close failed: %v", err)
		}
	}()

	n, err := tls.Write([]byte("GET / HTTP/1.0\n\n"))
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("Wrote %d bytes...", n)

	buf := make([]byte, 1024)
	n, err = tls.Read(buf)
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("Read %d bytes...", n)

	if !strings.Contains(string(buf), httpContent) {
		t.Errorf("Response does not contain %q", httpContent)
	}
}

func TestTLSVersions(t *testing.T) {
	tests := []struct {
		minVersion       uint16
		maxVersion       uint16
		wantVersion      ProtocolVersion
		wantHandshakeErr bool
	}{
		{tls.VersionTLS10, tls.VersionTLS13, ProtocolTLSv13, false},
		{tls.VersionSSL30, tls.VersionTLS12, ProtocolTLSv12, false},
		{tls.VersionTLS10, tls.VersionTLS12, ProtocolTLSv12, false},
		{tls.VersionTLS11, tls.VersionTLS12, ProtocolTLSv12, false},
		{tls.VersionSSL30, tls.VersionTLS11, ProtocolTLSv11, true},
		{tls.VersionSSL30, tls.VersionTLS10, ProtocolTLSv10, true},
		{tls.VersionSSL30, tls.VersionSSL30, 0, true},
		{tls.VersionTLS10, tls.VersionTLS10, ProtocolTLSv10, true},
		{tls.VersionTLS11, tls.VersionTLS11, ProtocolTLSv11, true},
		{tls.VersionTLS12, tls.VersionTLS12, ProtocolTLSv12, false},
	}
	for i, test := range tests {
		t.Logf("Testing handshake with protocols %x:%x", test.minVersion, test.maxVersion)
		tlsCfg := &tls.Config{
			MinVersion: test.minVersion,
			MaxVersion: test.maxVersion,
		}
		version, err := handshakeVersionTest(tlsCfg)
		switch {
		case test.wantHandshakeErr && err == nil:
			t.Errorf("Test %d - handshake %x:%x succeeded, want handshake error",
				i, test.minVersion, test.maxVersion)
		case test.wantHandshakeErr && err != nil:
			if _, ok := err.(handshakeError); !ok {
				t.Errorf("Test %d - handshake %x:%x; got unknown error, want handshake error: %v",
					i, test.minVersion, test.maxVersion, err)
			}
		case !test.wantHandshakeErr && err != nil:
			t.Errorf("Test %d - handshake %x:%x failed: %v", i, test.minVersion, test.maxVersion, err)
		case !test.wantHandshakeErr && err == nil:
			if got, want := version, test.wantVersion; got != want {
				t.Errorf("Test %d - handshake %x:%x; got protocol version %v, want %v",
					i, test.minVersion, test.maxVersion, got, want)
			}
		}
	}
}

func TestTLSSingleByteReadWrite(t *testing.T) {
	ts, u, caFile, err := newTestServer(nil)
	if err != nil {
		t.Fatalf("Failed to start test server: %v", err)
	}
	defer os.Remove(caFile)
	defer ts.Close()

	if err := Init(); err != nil {
		t.Fatal(err)
	}

	cfg, err := NewConfig()
	if err != nil {
		t.Fatal(err)
	}
	defer cfg.Free()
	if err := cfg.SetCAFile(caFile); err != nil {
		t.Fatal(err)
	}

	tls, err := NewClient(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer tls.Free()

	t.Logf("Connecting to %s", u.Host)

	if err := tls.Connect(u.Host, ""); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := tls.Close(); err != nil {
			t.Fatalf("Close failed: %v", err)
		}
	}()

	for _, b := range []byte("GET / HTTP/1.0\n\n") {
		n, err := tls.Write([]byte{b})
		if err != nil {
			t.Fatal(err)
		}
		if n != 1 {
			t.Fatalf("Wrote byte %v, got length %d, want 1", b, n)
		}
	}

	var body []byte
	for {
		buf := make([]byte, 1)
		n, err := tls.Read(buf)
		if err != nil {
			t.Fatal(err)
		}
		if n == 0 {
			break
		}
		if n != 1 {
			t.Fatalf("Read single byte, got length %d, want 1", n)
		}
		body = append(body, buf...)
	}

	if !strings.Contains(string(body), httpContent) {
		t.Errorf("Response does not contain %q", httpContent)
	}
}

func TestTLSInfo(t *testing.T) {
	ts, u, caFile, err := newTestServer(nil)
	if err != nil {
		t.Fatalf("Failed to start test server: %v", err)
	}
	defer os.Remove(caFile)
	defer ts.Close()

	if err := Init(); err != nil {
		t.Fatal(err)
	}

	cfg, err := NewConfig()
	if err != nil {
		t.Fatal(err)
	}
	defer cfg.Free()
	if err := cfg.SetCAFile(caFile); err != nil {
		t.Fatal(err)
	}

	tls, err := NewClient(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer tls.Free()

	t.Logf("Connecting to %s", u.Host)

	if err := tls.Connect(u.Host, ""); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := tls.Close(); err != nil {
			t.Fatalf("Close failed: %v", err)
		}
	}()

	// All of these should fail since the handshake has not completed.
	if _, err := tls.ConnVersion(); err == nil {
		t.Error("ConnVersion() return nil error, want error")
	}
	if _, err := tls.ConnCipher(); err == nil {
		t.Error("ConnCipher() return nil error, want error")
	}
	if _, err := tls.ConnCipherStrength(); err == nil {
		t.Error("ConnCipherStrength() return nil error, want error")
	}

	if got, want := tls.PeerCertProvided(), false; got != want {
		t.Errorf("PeerCertProvided() = %v, want %v", got, want)
	}
	for _, name := range []string{"127.0.0.1", "::1", "example.com"} {
		if got, want := tls.PeerCertContainsName(name), false; got != want {
			t.Errorf("PeerCertContainsName(%q) = %v, want %v", name, got, want)
		}
	}

	if _, err := tls.PeerCertIssuer(); err == nil {
		t.Error("PeerCertIssuer() returned nil error, want error")
	}
	if _, err := tls.PeerCertSubject(); err == nil {
		t.Error("PeerCertSubject() returned nil error, want error")
	}
	if _, err := tls.PeerCertCommonName(); err == nil {
		t.Error("PeerCertCommonName() returned nil error, want error")
	}
	if _, err := tls.PeerCertHash(); err == nil {
		t.Error("PeerCertHash() returned nil error, want error")
	}
	if _, err := tls.PeerCertNotBefore(); err == nil {
		t.Error("PeerCertNotBefore() returned nil error, want error")
	}
	if _, err := tls.PeerCertNotAfter(); err == nil {
		t.Error("PeerCertNotAfter() returned nil error, want error")
	}

	// Complete the handshake...
	if err := tls.Handshake(); err != nil {
		t.Fatalf("Handshake failed: %v", err)
	}

	if version, err := tls.ConnVersion(); err != nil {
		t.Errorf("ConnVersion() returned error: %v", err)
	} else {
		t.Logf("Protocol version: %v", version)
	}
	if cipher, err := tls.ConnCipher(); err != nil {
		t.Errorf("ConnCipher() returned error: %v", err)
	} else {
		t.Logf("Cipher: %v", cipher)
	}
	if strength, err := tls.ConnCipherStrength(); err != nil {
		t.Errorf("ConnCipherStrength() return ederror: %v", err)
	} else {
		t.Logf("Cipher Strength: %v bits", strength)
	}

	if got, want := tls.PeerCertProvided(), true; got != want {
		t.Errorf("PeerCertProvided() = %v, want %v", got, want)
	}
	for _, name := range []string{"127.0.0.1", "::1", "example.com"} {
		if got, want := tls.PeerCertContainsName(name), true; got != want {
			t.Errorf("PeerCertContainsName(%q) = %v, want %v", name, got, want)
		}
	}

	if issuer, err := tls.PeerCertIssuer(); err != nil {
		t.Errorf("PeerCertIssuer() returned error: %v", err)
	} else {
		t.Logf("Issuer: %v", issuer)
	}
	if subject, err := tls.PeerCertSubject(); err != nil {
		t.Errorf("PeerCertSubject() returned error: %v", err)
	} else {
		t.Logf("Subject: %v", subject)
	}
	if commonName, err := tls.PeerCertCommonName(); err != nil {
		t.Errorf("PeerCertCommonName() returned error: %v", err)
	} else {
		t.Logf("Subject: %v", commonName)
	}
	if hash, err := tls.PeerCertHash(); err != nil {
		t.Errorf("PeerCertHash() returned error: %v", err)
	} else if hash != certHash {
		t.Errorf("Got cert hash %q, want %q", hash, certHash)
	} else {
		t.Logf("Hash: %v", hash)
	}
	if notBefore, err := tls.PeerCertNotBefore(); err != nil {
		t.Errorf("PeerCertNotBefore() returned error: %v", err)
	} else if !certNotBefore.Equal(notBefore) {
		t.Errorf("Got cert notBefore %v, want %v", notBefore.UTC(), certNotBefore.UTC())
	} else {
		t.Logf("NotBefore: %v", notBefore.UTC())
	}
	if notAfter, err := tls.PeerCertNotAfter(); err != nil {
		t.Errorf("PeerCertNotAfter() returned error: %v", err)
	} else if !certNotAfter.Equal(notAfter) {
		t.Errorf("Got cert notAfter %v, want %v", notAfter.UTC(), certNotAfter.UTC())
	} else {
		t.Logf("NotAfter: %v", notAfter.UTC())
	}
}
