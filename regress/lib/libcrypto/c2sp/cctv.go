/*	$OpenBSD: cctv.go,v 1.1.1.1 2023/04/23 13:43:46 tb Exp $ */

/*
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

// cctv runs test vectors from CCTV against libcrypto.
package main

/*
#cgo LDFLAGS: -lcrypto

#include <openssl/evp.h>
*/
import "C"

import (
	"crypto/ed25519"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"unsafe"
)

const testVectorPath = "/usr/local/share/c2sp-testvectors"
const ed25519Json = "ed25519/ed25519vectors.json"

type ed25519Vectors []ed25519Vector

type ed25519Vector struct {
	Number    int    `json:"number"`
	PublicKey string `json:"key"`
	Signature string `json:"sig"`
	Message   string `json:"msg"`
	Flags     Flags  `json:"flags"`
}

type Flags int

const (
	LowOrderR Flags = 1 << iota
	LowOrderA
	LowOrderComponentR
	LowOrderComponentA
	LowOrderResidue
	NonCanonicalA
	NonCanonicalR
	ReencodedK
)

func (f Flags) String() string {
	var flags []string
	if f&LowOrderR != 0 {
		flags = append(flags, "low_order_R")
	}
	if f&LowOrderA != 0 {
		flags = append(flags, "low_order_A")
	}
	if f&LowOrderComponentR != 0 {
		flags = append(flags, "low_order_component_R")
	}
	if f&LowOrderComponentA != 0 {
		flags = append(flags, "low_order_component_A")
	}
	if f&LowOrderResidue != 0 {
		flags = append(flags, "low_order_residue")
	}
	if f&NonCanonicalA != 0 {
		flags = append(flags, "non_canonical_A")
	}
	if f&NonCanonicalR != 0 {
		flags = append(flags, "non_canonical_R")
	}
	if f&ReencodedK != 0 {
		flags = append(flags, "reencoded_k")
	}
	return fmt.Sprintf("%v", flags)
}

func (f *Flags) UnmarshalJSON(b []byte) error {
	var v []string

	if err := json.Unmarshal(b, &v); err != nil {
		return err
	}
	for _, flag := range v {
		switch flag {
		case "low_order_A":
			*f |= LowOrderA
		case "low_order_R":
			*f |= LowOrderR
		case "low_order_component_A":
			*f |= LowOrderComponentA
		case "low_order_component_R":
			*f |= LowOrderComponentR
		case "low_order_residue":
			*f |= LowOrderResidue
		case "non_canonical_A":
			*f |= NonCanonicalA
		case "non_canonical_R":
			*f |= NonCanonicalR
		case "reencoded_k":
			*f |= ReencodedK
		default:
			log.Fatalf("unknown flag %q", flag)
		}
	}

	return nil
}

func evpEd25519Verify(pubkey, msg, sig []byte) bool {
	pkey := C.EVP_PKEY_new_raw_public_key(C.EVP_PKEY_ED25519, nil, (*C.uchar)(unsafe.Pointer(&pubkey[0])), (C.size_t)(len(pubkey)))
	if pkey == nil {
		log.Fatalf("EVP_PKEY_new_raw_public_key failed")
	}
	defer C.EVP_PKEY_free(pkey)

	mdctx := C.EVP_MD_CTX_new()
	if mdctx == nil {
		log.Fatal("EVP_MD_CTX_new failed")
	}
	defer C.EVP_MD_CTX_free(mdctx)

	if C.EVP_DigestVerifyInit(mdctx, nil, nil, nil, pkey) != 1 {
		log.Fatal("EVP_DigestVerifyInit failed")
	}
	ret := C.EVP_DigestVerify(mdctx, (*C.uchar)(unsafe.Pointer(&sig[0])), (C.size_t)(len(sig)), (*C.uchar)(unsafe.Pointer(&msg[0])), (C.size_t)(len(msg)))
	if ret < 0 {
		log.Fatalf("EVP_DigestVerify errored %d", ret)
	}

	return ret == 1
}

func runEd25519Test(tv ed25519Vector) bool {
	pubkey, err := hex.DecodeString(tv.PublicKey)
	if err != nil {
		log.Fatalf("Failed to decode key %q: %v", tv.PublicKey, err)
	}

	sig, err := hex.DecodeString(tv.Signature)
	if err != nil {
		log.Fatalf("Failed to decode Signature %q: %v", tv.Signature, err)
	}

	msg := []byte(tv.Message)

	// Implementations derived from "ref10" reject `LowOrderResidue` and
	// `NonCanonicalR` and accept everything else.
	reject := LowOrderResidue | NonCanonicalR
	want_verify := (tv.Flags & reject) == 0

	c_verified := evpEd25519Verify(pubkey, msg, sig)
	go_verified := ed25519.Verify(pubkey, msg, sig)

	success := true
	if c_verified != want_verify || go_verified != want_verify {
		fmt.Printf("FAIL: Test case %d (flags: %v) - C: %t, want: %t, go: %t\n", tv.Number, tv.Flags, c_verified, want_verify, go_verified)
		success = false
	}
	return success
}

func main() {
	if _, err := os.Stat(testVectorPath); os.IsNotExist(err) {
		fmt.Printf("package cc-testvectors is required for this regress\n")
		fmt.Printf("SKIPPED\n")
		os.Exit(0)
	}

	b, err := ioutil.ReadFile(filepath.Join(testVectorPath, ed25519Json))
	if err != nil {
		log.Fatalf("Failed to read test vectors: %v", err)
	}

	edv := &ed25519Vectors{}
	if err := json.Unmarshal(b, edv); err != nil {
		log.Fatalf("Failed to unmarshal JSON: %v", err)
	}

	success := true

	for _, vector := range *edv {
		if !runEd25519Test(vector) {
			success = false
		}
	}

	if !success {
		os.Exit(1)
	}
}
