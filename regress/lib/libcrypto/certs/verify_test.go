package verify

import (
	"crypto/x509"
	"encoding/pem"
	"io/ioutil"
	"path/filepath"
	"testing"
)

func TestVerify(t *testing.T) {
	tests := []struct {
		id         string
		wantChains int
	}{
		{"1a", 1},
		{"2a", 1},
		{"2b", 0},
		{"2c", 1},
		{"3a", 1},
		{"3b", 0},
		{"3c", 0},
		{"3d", 0},
		{"3e", 1},
		{"4a", 2},
		{"4b", 1},
		{"4c", 1},
		{"4d", 1},
		{"4e", 1},
		{"4f", 2},
		{"4g", 1},
		{"4h", 1},
		{"5a", 2},
		{"5b", 1},
		{"5c", 1},
		{"5d", 1},
		{"5e", 1},
		{"5f", 1},
		{"5g", 2},
		{"5h", 1},
		{"5i", 1},
		{"6a", 1},  // Expired root.
		{"6b", 1},  // Expired root.
		{"7a", 1},  // Expired root.
		{"7b", 1},  // Expired root.
		{"8a", 0},  // Expired leaf.
		{"9a", 0},  // Expired intermediate.
		{"10a", 1}, // Cross signed with expired intermediate.
		{"10b", 1}, // Cross signed with expired intermediate.
		{"11a", 1}, // Cross signed with expired intermediate.
		{"11b", 1}, // Cross signed with expired intermediate.
		{"12a", 1}, // Cross signed with expired intermediate.
		{"13a", 1}, // Cross signed with expired root.
	}

	for _, test := range tests {
		t.Run(test.id, func(t *testing.T) {
			rootsPEM, err := ioutil.ReadFile(filepath.Join(test.id, "roots.pem"))
			if err != nil {
				t.Fatalf("Failed to read roots PEM: %v", err)
			}
			bundlePEM, err := ioutil.ReadFile(filepath.Join(test.id, "bundle.pem"))
			if err != nil {
				t.Fatalf("Failed to read bundle PEM: %v", err)
			}

			// Pull the leaf certificate off the top of the bundle.
			block, intermediatesPEM := pem.Decode(bundlePEM)
			if block == nil {
				t.Fatal("Failed to parse leaf from bundle")
			}
			cert, err := x509.ParseCertificate(block.Bytes)
			if err != nil {
				t.Fatalf("Failed to parse certificate: %v", err)
			}

			roots := x509.NewCertPool()
			if !roots.AppendCertsFromPEM(rootsPEM) {
				t.Fatal("Failed to parse root certificates")
			}
			intermediates := x509.NewCertPool()
			if len(intermediatesPEM) > 0 {
				if !intermediates.AppendCertsFromPEM(intermediatesPEM) {
					t.Fatal("Failed to parse intermediate certificates")
				}
			}

			opts := x509.VerifyOptions{
				Roots:         roots,
				Intermediates: intermediates,
			}

			chains, err := cert.Verify(opts)
			if err != nil {
				if test.wantChains > 0 {
					t.Errorf("Failed to verify certificate: %v", err)
				}
				return
			}
			t.Logf("Found %d chains", len(chains))
			if got, want := len(chains), test.wantChains; got != want {
				t.Errorf("Got %d chains, want %d", got, want)
			}
			for i, chain := range chains {
				t.Logf("Chain %d\n", i)
				for _, cert := range chain {
					t.Logf("  %v\n", cert.Subject)
				}
			}
		})
	}
}
