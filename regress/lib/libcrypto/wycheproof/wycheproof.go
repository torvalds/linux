/* $OpenBSD: wycheproof.go,v 1.193 2025/09/16 15:45:34 tb Exp $ */
/*
 * Copyright (c) 2018,2023 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2018,2019,2022-2025 Theo Buehler <tb@openbsd.org>
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

// Wycheproof runs test vectors from Project Wycheproof against libcrypto.
package main

/*
#cgo LDFLAGS: -lcrypto

#include <limits.h>
#include <string.h>

#include <openssl/aes.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/cmac.h>
#include <openssl/curve25519.h>
#include <openssl/dsa.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/hmac.h>
#include <openssl/mlkem.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>

int
wp_EVP_PKEY_CTX_set_hkdf_md(EVP_PKEY_CTX *pctx, const EVP_MD *md)
{
	return EVP_PKEY_CTX_set_hkdf_md(pctx, md);
}

int
wp_EVP_PKEY_CTX_set1_hkdf_salt(EVP_PKEY_CTX *pctx, const unsigned char *salt, size_t salt_len)
{
	if (salt_len > INT_MAX)
		return 0;
	return EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt, salt_len);
}

int
wp_EVP_PKEY_CTX_set1_hkdf_key(EVP_PKEY_CTX *pctx, const unsigned char *ikm, size_t ikm_len)
{
	if (ikm_len > INT_MAX)
		return 0;
	return EVP_PKEY_CTX_set1_hkdf_key(pctx, ikm, ikm_len);
}

int
wp_EVP_PKEY_CTX_add1_hkdf_info(EVP_PKEY_CTX *pctx, const unsigned char *info, size_t info_len)
{
	if (info_len > INT_MAX)
		return 0;
	return EVP_PKEY_CTX_add1_hkdf_info(pctx, info, info_len);
}
*/
import "C"

import (
	"bytes"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"math/big"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"unsafe"
)

const testVectorPath = "/usr/local/share/wycheproof/testvectors_v1"

type testVariant int

const (
	Normal    testVariant = 0
	EcPoint   testVariant = 1
	P1363     testVariant = 2
	Webcrypto testVariant = 3
	Asn1      testVariant = 4
	Pem       testVariant = 5
	Jwk       testVariant = 6
	Skip      testVariant = 7
)

func (variant testVariant) String() string {
	variants := [...]string{
		"Normal",
		"EcPoint",
		"P1363",
		"Webcrypto",
		"Asn1",
		"Pem",
		"Jwk",
		"Skip",
	}
	return variants[variant]
}

func wycheproofFormatTestCase(tcid int, comment string, flags []string, result string) string {
	return fmt.Sprintf("Test case %d (%q) %v %v", tcid, comment, flags, result)
}

var testc *testCoordinator

type BigInt struct {
	*big.Int
}

func mustConvertBigIntToBigNum(bi *BigInt) *C.BIGNUM {
	value := bi.Bytes()
	if len(value) == 0 {
		value = append(value, 0)
	}
	bn := C.BN_new()
	if bn == nil {
		log.Fatal("BN_new failed")
	}
	if C.BN_bin2bn((*C.uchar)(unsafe.Pointer(&value[0])), C.int(len(value)), bn) == nil {
		log.Fatal("BN_bin2bn failed")
	}
	if bi.Sign() == -1 {
		C.BN_set_negative(bn, C.int(1))
	}
	return bn
}

func (bi *BigInt) UnmarshalJSON(data []byte) error {
	if len(data) < 2 || data[0] != '"' || data[len(data)-1] != '"' {
		log.Fatalf("Failed to decode %q: too short or unquoted", data)
	}
	data = data[1 : len(data)-1]
	if len(data)%2 == 1 {
		pad := make([]byte, 1, len(data)+1)
		if data[0] >= '0' && data[0] <= '7' {
			pad[0] = '0'
		} else {
			pad[0] = 'f'
		}
		data = append(pad, data...)
	}

	src := make([]byte, hex.DecodedLen(len(data)))
	_, err := hex.Decode(src, data)
	if err != nil {
		log.Fatalf("Failed to decode %q: %v", data, err)
	}

	bi.Int = &big.Int{}
	bi.Int.SetBytes(src)
	if data[0] >= '8' {
		y := &big.Int{}
		y.SetBit(y, 4*len(data), 1)
		bi.Int.Sub(bi.Int, y)
	}
	return nil
}

type wycheproofJWKPublic struct {
	Crv string `json:"crv"`
	KID string `json:"kid"`
	KTY string `json:"kty"`
	X   string `json:"x"`
	Y   string `json:"y"`
}

type wycheproofJWKPrivate struct {
	Crv string `json:"crv"`
	D   string `json:"d"`
	KID string `json:"kid"`
	KTY string `json:"kty"`
	X   string `json:"x"`
	Y   string `json:"y"`
}

type wycheproofTestGroupAesCbcPkcs5 struct {
	IVSize  int                          `json:"ivSize"`
	KeySize int                          `json:"keySize"`
	Type    string                       `json:"type"`
	Tests   []*wycheproofTestAesCbcPkcs5 `json:"tests"`
}

type wycheproofTestAesCbcPkcs5 struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Key     string   `json:"key"`
	IV      string   `json:"iv"`
	Msg     string   `json:"msg"`
	CT      string   `json:"ct"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestAesCbcPkcs5) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupAead struct {
	IVSize  int                   `json:"ivSize"`
	KeySize int                   `json:"keySize"`
	TagSize int                   `json:"tagSize"`
	Type    string                `json:"type"`
	Tests   []*wycheproofTestAead `json:"tests"`
}

type wycheproofTestGroupAesAead wycheproofTestGroupAead
type wycheproofTestGroupChaCha wycheproofTestGroupAead

type wycheproofTestAead struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Key     string   `json:"key"`
	IV      string   `json:"iv"`
	AAD     string   `json:"aad"`
	Msg     string   `json:"msg"`
	CT      string   `json:"ct"`
	Tag     string   `json:"tag"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestAead) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupAesCmac struct {
	KeySize int                      `json:"keySize"`
	TagSize int                      `json:"tagSize"`
	Type    string                   `json:"type"`
	Tests   []*wycheproofTestAesCmac `json:"tests"`
}

type wycheproofTestAesCmac struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Key     string   `json:"key"`
	Msg     string   `json:"msg"`
	Tag     string   `json:"tag"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestAesCmac) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofDSAKey struct {
	G       string `json:"g"`
	KeySize int    `json:"keySize"`
	P       string `json:"p"`
	Q       string `json:"q"`
	Type    string `json:"type"`
	Y       string `json:"y"`
}

type wycheproofTestDSA struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Msg     string   `json:"msg"`
	Sig     string   `json:"sig"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestDSA) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupDSA struct {
	Key    *wycheproofDSAKey    `json:"publicKey"`
	KeyDER string               `json:"publicKeyDer"`
	KeyPEM string               `json:"publicKeyPem"`
	SHA    string               `json:"sha"`
	Type   string               `json:"type"`
	Tests  []*wycheproofTestDSA `json:"tests"`
}

type wycheproofTestECDH struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Public  string   `json:"public"`
	Private string   `json:"private"`
	Shared  string   `json:"shared"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestECDH) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupECDH struct {
	Curve    string                `json:"curve"`
	Encoding string                `json:"encoding"`
	Type     string                `json:"type"`
	Tests    []*wycheproofTestECDH `json:"tests"`
}

type wycheproofTestECDHWebCrypto struct {
	TCID    int                   `json:"tcId"`
	Comment string                `json:"comment"`
	Public  *wycheproofJWKPublic  `json:"public"`
	Private *wycheproofJWKPrivate `json:"private"`
	Shared  string                `json:"shared"`
	Result  string                `json:"result"`
	Flags   []string              `json:"flags"`
}

func (wt *wycheproofTestECDHWebCrypto) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupECDHWebCrypto struct {
	Curve    string                         `json:"curve"`
	Encoding string                         `json:"encoding"`
	Type     string                         `json:"type"`
	Tests    []*wycheproofTestECDHWebCrypto `json:"tests"`
}

type wycheproofECDSAKey struct {
	Curve        string `json:"curve"`
	KeySize      int    `json:"keySize"`
	Type         string `json:"type"`
	Uncompressed string `json:"uncompressed"`
	WX           string `json:"wx"`
	WY           string `json:"wy"`
}

type wycheproofTestECDSA struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Msg     string   `json:"msg"`
	Sig     string   `json:"sig"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestECDSA) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupECDSA struct {
	Key    *wycheproofECDSAKey    `json:"publicKey"`
	KeyDER string                 `json:"publicKeyDer"`
	KeyPEM string                 `json:"publicKeyPem"`
	SHA    string                 `json:"sha"`
	Type   string                 `json:"type"`
	Tests  []*wycheproofTestECDSA `json:"tests"`
}

type wycheproofTestEcCurve struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Flags   []string `json:"flags"`
	Name    string   `json:"name"`
	OID     string   `json:"oid"`
	Ref     string   `json:"ref"`
	P       *BigInt  `json:"p"`
	N       *BigInt  `json:"n"`
	A       *BigInt  `json:"a"`
	B       *BigInt  `json:"b"`
	Gx      *BigInt  `json:"gx"`
	Gy      *BigInt  `json:"gy"`
	H       int      `json:"h"`
	Result  string   `json:"result"`
}

func (wt *wycheproofTestEcCurve) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupEcCurve struct {
	Type  string                   `json:"type"`
	Tests []*wycheproofTestEcCurve `json:"tests"`
}

type wycheproofJWKEdDSA struct {
	Crv string `json:"crv"`
	D   string `json:"d"`
	KID string `json:"kid"`
	KTY string `json:"kty"`
	X   string `json:"x"`
}

type wycheproofEdDSAKey struct {
	Curve   string `json:"curve"`
	KeySize int    `json:"keySize"`
	Pk      string `json:"pk"`
	Sk      string `json:"sk"`
	Type    string `json:"type"`
}

type wycheproofTestEdDSA struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Msg     string   `json:"msg"`
	Sig     string   `json:"sig"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestEdDSA) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupEdDSA struct {
	JWK    *wycheproofJWKEdDSA    `json:"publicKeyJwk"`
	Key    *wycheproofEdDSAKey    `json:"publicKey"`
	KeyDer string                 `json:"publicKeyDer"`
	KeyPem string                 `json:"publicKeyPem"`
	Type   string                 `json:"type"`
	Tests  []*wycheproofTestEdDSA `json:"tests"`
}

type wycheproofTestHkdf struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Ikm     string   `json:"ikm"`
	Salt    string   `json:"salt"`
	Info    string   `json:"info"`
	Size    int      `json:"size"`
	Okm     string   `json:"okm"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestHkdf) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupHkdf struct {
	Type    string                `json:"type"`
	KeySize int                   `json:"keySize"`
	Tests   []*wycheproofTestHkdf `json:"tests"`
}

type wycheproofTestHmac struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Key     string   `json:"key"`
	Msg     string   `json:"msg"`
	Tag     string   `json:"tag"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestHmac) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupHmac struct {
	KeySize int                   `json:"keySize"`
	TagSize int                   `json:"tagSize"`
	Type    string                `json:"type"`
	Tests   []*wycheproofTestHmac `json:"tests"`
}

type wycheproofTestKW struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Key     string   `json:"key"`
	Msg     string   `json:"msg"`
	CT      string   `json:"ct"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestKW) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupKW struct {
	KeySize int                 `json:"keySize"`
	Type    string              `json:"type"`
	Tests   []*wycheproofTestKW `json:"tests"`
}

type wycheproofTestMLKEM struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Seed    string   `json:"seed"`
	Ek      string   `json:"ek"`
	M       string   `json:"m"`
	C       string   `json:"c"`
	K       string   `json:"K"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestMLKEM) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupMLKEM struct {
	Type         string                 `json:"type"`
	ParameterSet string                 `json:"parameterSet"`
	Tests        []*wycheproofTestMLKEM `json:"tests"`
}

type wycheproofTestPbkdf struct {
	TCID           int      `json:"tcId"`
	Comment        string   `json:"comment"`
	Flags          []string `json:"string"`
	Password       string   `json:"password"`
	Salt           string   `json:"salt"`
	IterationCount int      `json:"iterationCount"`
	DkLen          int      `json:"dkLen"`
	Dk             string   `json:"dk"`
	Result         string   `json:"result"`
}

func (wt *wycheproofTestPbkdf) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupPbkdf2HmacSha struct {
	Type  string                 `json:"type"`
	Tests []*wycheproofTestPbkdf `json:"tests"`
}

type wycheproofTestPrimality struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Value   *BigInt  `json:"value"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestPrimality) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupPrimality struct {
	Type  string                     `json:"type"`
	Tests []*wycheproofTestPrimality `json:"tests"`
}

type wycheproofTestRSA struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Msg     string   `json:"msg"`
	Sig     string   `json:"sig"`
	Padding string   `json:"padding"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestRSA) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupRSA struct {
	PrivateKey *wycheproofRSAPrivateKey `json:"privateKey"`
	PublicKey  *wycheproofRSAPublicKey  `json:"publicKey"`
	KeyASN     string                   `json:"keyAsn"`
	KeyDER     string                   `json:"keyDer"`
	KeyPEM     string                   `json:"keyPem"`
	KeySize    int                      `json:"keysize"`
	SHA        string                   `json:"sha"`
	Type       string                   `json:"type"`
	Tests      []*wycheproofTestRSA     `json:"tests"`
}

type wycheproofRSAPublicKey struct {
	Modulus        string `json:"modulus"`
	PublicExponent string `json:"publicExponent"`
}

type wycheproofRSAPrivateKey struct {
	Modulus         string `json:"modulus"`
	PrivateExponent string `json:"privateExponent"`
	PublicExponent  string `json:"publicExponent"`
	Prime1          string `json:"prime1"`
	Prime2          string `json:"prime2"`
	Exponent1       string `json:"exponent1"`
	Exponent2       string `json:"exponent2"`
	Coefficient     string `json:"coefficient"`
}

type wycheproofPrivateKeyJwk struct {
	Alg string `json:"alg"`
	D   string `json:"d"`
	DP  string `json:"dp"`
	DQ  string `json:"dq"`
	E   string `json:"e"`
	KID string `json:"kid"`
	Kty string `json:"kty"`
	N   string `json:"n"`
	P   string `json:"p"`
	Q   string `json:"q"`
	QI  string `json:"qi"`
}

type wycheproofTestRsaes struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Msg     string   `json:"msg"`
	CT      string   `json:"ct"`
	Label   string   `json:"label"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestRsaes) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupRsaesOaep struct {
	Type            string                   `json:"type"`
	KeySize         int                      `json:"keysize"`
	SHA             string                   `json:"sha"`
	MGF             string                   `json:"mgf"`
	MGFSHA          string                   `json:"mgfSha"`
	PrivateKey      *wycheproofRSAPrivateKey `json:"privateKey"`
	PrivateKeyJwk   *wycheproofPrivateKeyJwk `json:"privateKeyJwk"`
	PrivateKeyPem   string                   `json:"privateKeyPem"`
	PrivateKeyPkcs8 string                   `json:"privateKeyPkcs8"`
	Tests           []*wycheproofTestRsaes   `json:"tests"`
}

type wycheproofTestGroupRsaesPkcs1 struct {
	Type            string                   `json:"type"`
	PrivateKey      *wycheproofRSAPrivateKey `json:"privateKey"`
	PrivateKeyJwk   *wycheproofPrivateKeyJwk `json:"privateKeyJwk"`
	PrivateKeyPem   string                   `json:"privateKeyPem"`
	PrivateKeyPkcs8 string                   `json:"privateKeyPkcs8"`
	KeySize         int                      `json:"keysize"`
	Tests           []*wycheproofTestRsaes   `json:"tests"`
}

type wycheproofTestRsassa struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Msg     string   `json:"msg"`
	Sig     string   `json:"sig"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestRsassa) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupRsassa struct {
	PrivateKey *wycheproofRSAPrivateKey `json:"privateKey"`
	PublicKey  *wycheproofRSAPublicKey  `json:"publicKey"`
	KeyASN     string                   `json:"keyAsn"`
	KeyDER     string                   `json:"keyDer"`
	KeyPEM     string                   `json:"keyPem"`
	KeySize    int                      `json:"keysize"`
	MGF        string                   `json:"mgf"`
	MGFSHA     string                   `json:"mgfSha"`
	SLen       int                      `json:"sLen"`
	SHA        string                   `json:"sha"`
	Type       string                   `json:"type"`
	Tests      []*wycheproofTestRsassa  `json:"tests"`
}

type wycheproofTestX25519 struct {
	TCID    int      `json:"tcId"`
	Comment string   `json:"comment"`
	Curve   string   `json:"curve"`
	Public  string   `json:"public"`
	Private string   `json:"private"`
	Shared  string   `json:"shared"`
	Result  string   `json:"result"`
	Flags   []string `json:"flags"`
}

func (wt *wycheproofTestX25519) String() string {
	return wycheproofFormatTestCase(wt.TCID, wt.Comment, wt.Flags, wt.Result)
}

type wycheproofTestGroupX25519 struct {
	Curve string                  `json:"curve"`
	Tests []*wycheproofTestX25519 `json:"tests"`
}

type wycheproofTestGroupRunner interface {
	run(string, testVariant) bool
}

type wycheproofTestVectorsV1 struct {
	Algorithm     string            `json:"algorithm"`
	Schema        string            `json:"schema"`
	NumberOfTests int               `json:"numberOfTests"`
	Header        []string          `json:"header"`
	Notes         json.RawMessage   `json:"notes"`
	TestGroups    []json.RawMessage `json:"testGroups"`
}

var nids = map[string]int{
	"brainpoolP224r1": C.NID_brainpoolP224r1,
	"brainpoolP256r1": C.NID_brainpoolP256r1,
	"brainpoolP320r1": C.NID_brainpoolP320r1,
	"brainpoolP384r1": C.NID_brainpoolP384r1,
	"brainpoolP512r1": C.NID_brainpoolP512r1,
	"brainpoolP224t1": C.NID_brainpoolP224t1,
	"brainpoolP256t1": C.NID_brainpoolP256t1,
	"brainpoolP320t1": C.NID_brainpoolP320t1,
	"brainpoolP384t1": C.NID_brainpoolP384t1,
	"brainpoolP512t1": C.NID_brainpoolP512t1,
	"FRP256v1":        C.NID_FRP256v1,
	"secp160k1":       C.NID_secp160k1,
	"secp160r1":       C.NID_secp160r1,
	"secp160r2":       C.NID_secp160r2,
	"secp192k1":       C.NID_secp192k1,
	"secp192r1":       C.NID_X9_62_prime192v1, // RFC 8422, Table 4, p.32
	"secp224k1":       C.NID_secp224k1,
	"secp224r1":       C.NID_secp224r1,
	"secp256k1":       C.NID_secp256k1,
	"P-256K":          C.NID_secp256k1,
	"secp256r1":       C.NID_X9_62_prime256v1, // RFC 8422, Table 4, p.32
	"P-256":           C.NID_X9_62_prime256v1,
	"sect283k1":       C.NID_sect283k1,
	"sect283r1":       C.NID_sect283r1,
	"secp384r1":       C.NID_secp384r1,
	"P-384":           C.NID_secp384r1,
	"sect409k1":       C.NID_sect409k1,
	"sect409r1":       C.NID_sect409r1,
	"secp521r1":       C.NID_secp521r1,
	"sect571k1":       C.NID_sect571k1,
	"sect571r1":       C.NID_sect571r1,
	"P-521":           C.NID_secp521r1,
	"SHA-1":           C.NID_sha1,
	"SHA-224":         C.NID_sha224,
	"SHA-256":         C.NID_sha256,
	"SHA-384":         C.NID_sha384,
	"SHA-512":         C.NID_sha512,
	"SHA-512/224":     C.NID_sha512_224,
	"SHA-512/256":     C.NID_sha512_256,
	"SHA3-224":        C.NID_sha3_224,
	"SHA3-256":        C.NID_sha3_256,
	"SHA3-384":        C.NID_sha3_384,
	"SHA3-512":        C.NID_sha3_512,
}

func nidFromString(ns string) (int, error) {
	nid, ok := nids[ns]
	if ok {
		return nid, nil
	}
	return -1, fmt.Errorf("unknown NID %q", ns)
}

func skipHash(hash string) bool {
	return hash == "SHAKE128" || hash == "SHAKE256"
}

func skipCurve(nid int) bool {
	switch C.int(nid) {
	case C.NID_secp160k1, C.NID_secp160r1, C.NID_secp160r2, C.NID_secp192k1, C.NID_X9_62_prime192v1:
		return true
	case C.NID_sect283k1, C.NID_sect283r1, C.NID_sect409k1, C.NID_sect409r1, C.NID_sect571k1, C.NID_sect571r1:
		return true
	}
	return false
}

var evpMds = map[string]*C.EVP_MD{
	"SHA-1":       C.EVP_sha1(),
	"SHA-224":     C.EVP_sha224(),
	"SHA-256":     C.EVP_sha256(),
	"SHA-384":     C.EVP_sha384(),
	"SHA-512":     C.EVP_sha512(),
	"SHA-512/224": C.EVP_sha512_224(),
	"SHA-512/256": C.EVP_sha512_256(),
	"SHA3-224":    C.EVP_sha3_224(),
	"SHA3-256":    C.EVP_sha3_256(),
	"SHA3-384":    C.EVP_sha3_384(),
	"SHA3-512":    C.EVP_sha3_512(),
	"SM3":         C.EVP_sm3(),
}

func hashEvpMdFromString(hs string) (*C.EVP_MD, error) {
	md, ok := evpMds[hs]
	if ok {
		return md, nil
	}
	return nil, fmt.Errorf("unknown hash %q", hs)
}

var aesCbcs = map[int]*C.EVP_CIPHER{
	128: C.EVP_aes_128_cbc(),
	192: C.EVP_aes_192_cbc(),
	256: C.EVP_aes_256_cbc(),
}

var aesCcms = map[int]*C.EVP_CIPHER{
	128: C.EVP_aes_128_ccm(),
	192: C.EVP_aes_192_ccm(),
	256: C.EVP_aes_256_ccm(),
}

var aesGcms = map[int]*C.EVP_CIPHER{
	128: C.EVP_aes_128_gcm(),
	192: C.EVP_aes_192_gcm(),
	256: C.EVP_aes_256_gcm(),
}

var aeses = map[string]map[int]*C.EVP_CIPHER{
	"AES-CBC": aesCbcs,
	"AES-CCM": aesCcms,
	"AES-GCM": aesGcms,
}

func cipherAes(algorithm string, size int) (*C.EVP_CIPHER, error) {
	cipher, ok := aeses[algorithm][size]
	if ok {
		return cipher, nil
	}
	return nil, fmt.Errorf("invalid key size: %d", size)
}

var aesAeads = map[int]*C.EVP_AEAD{
	128: C.EVP_aead_aes_128_gcm(),
	192: nil,
	256: C.EVP_aead_aes_256_gcm(),
}

func aeadAes(size int) (*C.EVP_AEAD, error) {
	aead, ok := aesAeads[size]
	if ok {
		return aead, nil
	}
	return nil, fmt.Errorf("invalid key size: %d", size)
}

func mustHashHexMessage(md *C.EVP_MD, message string) ([]byte, int) {
	size := C.EVP_MD_size(md)
	if size <= 0 || size > C.EVP_MAX_MD_SIZE {
		log.Fatalf("unexpected MD size %d", size)
	}

	msg, msgLen := mustDecodeHexString(message, "for hex message digest")

	digest := make([]byte, size)

	if C.EVP_Digest(unsafe.Pointer(&msg[0]), C.size_t(msgLen), (*C.uchar)(unsafe.Pointer(&digest[0])), nil, md, nil) != 1 {
		log.Fatalf("EVP_Digest failed")
	}

	return digest, int(size)
}

func mustDecodeHexString(str, descr string) (out []byte, outLen int) {
	out, err := hex.DecodeString(str)
	if err != nil {
		log.Fatalf("Failed to decode %s %q: %v", descr, str, err)
	}
	outLen = len(out)
	if outLen == 0 {
		out = append(out, 0)
	}
	return out, outLen
}

func checkAesCbcPkcs5(ctx *C.EVP_CIPHER_CTX, doEncrypt int, key []byte, keyLen int, iv []byte, ivLen int, in []byte, inLen int, out []byte, outLen int, wt *wycheproofTestAesCbcPkcs5) bool {
	var action string
	if doEncrypt == 1 {
		action = "encrypting"
	} else {
		action = "decrypting"
	}

	ret := C.EVP_CipherInit_ex(ctx, nil, nil, (*C.uchar)(unsafe.Pointer(&key[0])), (*C.uchar)(unsafe.Pointer(&iv[0])), C.int(doEncrypt))
	if ret != 1 {
		log.Fatalf("EVP_CipherInit_ex failed: %d", ret)
	}

	cipherOut := make([]byte, inLen+C.EVP_MAX_BLOCK_LENGTH)
	var cipherOutLen C.int

	ret = C.EVP_CipherUpdate(ctx, (*C.uchar)(unsafe.Pointer(&cipherOut[0])), &cipherOutLen, (*C.uchar)(unsafe.Pointer(&in[0])), C.int(inLen))
	if ret != 1 {
		if wt.Result == "invalid" {
			fmt.Printf("INFO: %s [%v] - EVP_CipherUpdate() = %d\n", wt, action, ret)
			return true
		}
		fmt.Printf("FAIL: %s [%v] - EVP_CipherUpdate() = %d\n", wt, action, ret)
		return false
	}

	var finallen C.int
	ret = C.EVP_CipherFinal_ex(ctx, (*C.uchar)(unsafe.Pointer(&cipherOut[cipherOutLen])), &finallen)
	if ret != 1 {
		if wt.Result == "invalid" {
			return true
		}
		fmt.Printf("FAIL: %s [%v] - EVP_CipherFinal_ex() = %d\n", wt, action, ret)
		return false
	}

	cipherOutLen += finallen
	if cipherOutLen != C.int(outLen) && wt.Result != "invalid" {
		fmt.Printf("FAIL: %s [%v] - open length mismatch: got %d, want %d\n", wt, action, cipherOutLen, outLen)
		return false
	}

	openedMsg := cipherOut[0:cipherOutLen]
	if outLen == 0 {
		out = nil
	}

	success := false
	if bytes.Equal(openedMsg, out) == (wt.Result != "invalid") {
		success = true
	} else {
		fmt.Printf("FAIL: %s [%v] - msg match: %t\n", wt, action, bytes.Equal(openedMsg, out))
	}
	return success
}

func runAesCbcPkcs5Test(ctx *C.EVP_CIPHER_CTX, wt *wycheproofTestAesCbcPkcs5) bool {
	key, keyLen := mustDecodeHexString(wt.Key, "key")
	iv, ivLen := mustDecodeHexString(wt.IV, "iv")
	ct, ctLen := mustDecodeHexString(wt.CT, "ct")
	msg, msgLen := mustDecodeHexString(wt.Msg, "message")

	openSuccess := checkAesCbcPkcs5(ctx, 0, key, keyLen, iv, ivLen, ct, ctLen, msg, msgLen, wt)
	sealSuccess := checkAesCbcPkcs5(ctx, 1, key, keyLen, iv, ivLen, msg, msgLen, ct, ctLen, wt)

	return openSuccess && sealSuccess
}

func (wtg *wycheproofTestGroupAesCbcPkcs5) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with IV size %d and key size %d...\n", algorithm, wtg.Type, wtg.IVSize, wtg.KeySize)

	cipher, err := cipherAes("AES-CBC", wtg.KeySize)
	if err != nil {
		log.Fatal(err)
	}

	ctx := C.EVP_CIPHER_CTX_new()
	if ctx == nil {
		log.Fatal("EVP_CIPHER_CTX_new() failed")
	}
	defer C.EVP_CIPHER_CTX_free(ctx)

	ret := C.EVP_CipherInit_ex(ctx, cipher, nil, nil, nil, 0)
	if ret != 1 {
		log.Fatalf("EVP_CipherInit_ex failed: %d", ret)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runAesCbcPkcs5Test(ctx, wt) {
			success = false
		}
	}
	return success
}

func checkAesAead(algorithm string, ctx *C.EVP_CIPHER_CTX, doEncrypt int, key []byte, keyLen int, iv []byte, ivLen int, aad []byte, aadLen int, in []byte, inLen int, out []byte, outLen int, tag []byte, tagLen int, wt *wycheproofTestAead) bool {
	var ctrlSetIVLen C.int
	var ctrlSetTag C.int
	var ctrlGetTag C.int

	doCCM := false
	switch algorithm {
	case "AES-CCM":
		doCCM = true
		ctrlSetIVLen = C.EVP_CTRL_CCM_SET_IVLEN
		ctrlSetTag = C.EVP_CTRL_CCM_SET_TAG
		ctrlGetTag = C.EVP_CTRL_CCM_GET_TAG
	case "AES-GCM":
		ctrlSetIVLen = C.EVP_CTRL_GCM_SET_IVLEN
		ctrlSetTag = C.EVP_CTRL_GCM_SET_TAG
		ctrlGetTag = C.EVP_CTRL_GCM_GET_TAG
	}

	setTag := unsafe.Pointer(nil)
	var action string

	if doEncrypt == 1 {
		action = "encrypting"
	} else {
		action = "decrypting"
		setTag = unsafe.Pointer(&tag[0])
	}

	ret := C.EVP_CipherInit_ex(ctx, nil, nil, nil, nil, C.int(doEncrypt))
	if ret != 1 {
		log.Fatalf("[%v] cipher init failed", action)
	}

	ret = C.EVP_CIPHER_CTX_ctrl(ctx, ctrlSetIVLen, C.int(ivLen), nil)
	if ret != 1 {
		if wt.Comment == "Nonce is too long" || wt.Comment == "Invalid nonce size" ||
			wt.Comment == "0 size IV is not valid" || wt.Comment == "Very long nonce" {
			return true
		}
		fmt.Printf("FAIL: %s [%v] - setting IV len to %d failed: %d.\n", wt, action, ivLen, ret)
		return false
	}

	if doEncrypt == 0 || doCCM {
		ret = C.EVP_CIPHER_CTX_ctrl(ctx, ctrlSetTag, C.int(tagLen), setTag)
		if ret != 1 {
			if wt.Comment == "Invalid tag size" {
				return true
			}
			fmt.Printf("FAIL: %s [%v] - setting tag length to %d failed: %d.\n", wt, action, tagLen, ret)
			return false
		}
	}

	ret = C.EVP_CipherInit_ex(ctx, nil, nil, (*C.uchar)(unsafe.Pointer(&key[0])), (*C.uchar)(unsafe.Pointer(&iv[0])), C.int(doEncrypt))
	if ret != 1 {
		fmt.Printf("FAIL: %s [%v] - setting key and IV failed: %d.\n", wt, action, ret)
		return false
	}

	var cipherOutLen C.int
	if doCCM {
		ret = C.EVP_CipherUpdate(ctx, nil, &cipherOutLen, nil, C.int(inLen))
		if ret != 1 {
			fmt.Printf("FAIL: %s [%v] - setting input length to %d failed: %d.\n", wt, action, inLen, ret)
			return false
		}
	}

	ret = C.EVP_CipherUpdate(ctx, nil, &cipherOutLen, (*C.uchar)(unsafe.Pointer(&aad[0])), C.int(aadLen))
	if ret != 1 {
		fmt.Printf("FAIL: %s [%v] - processing AAD failed: %d.\n", wt, action, ret)
		return false
	}

	cipherOutLen = 0
	cipherOut := make([]byte, inLen)
	if inLen == 0 {
		cipherOut = append(cipherOut, 0)
	}

	ret = C.EVP_CipherUpdate(ctx, (*C.uchar)(unsafe.Pointer(&cipherOut[0])), &cipherOutLen, (*C.uchar)(unsafe.Pointer(&in[0])), C.int(inLen))
	if ret != 1 {
		if wt.Result == "invalid" {
			return true
		}
		fmt.Printf("FAIL: %s [%v] - EVP_CipherUpdate() = %d.\n", wt, action, ret)
		return false
	}

	if doEncrypt == 1 {
		var tmpLen C.int
		dummyOut := make([]byte, 16)

		ret = C.EVP_CipherFinal_ex(ctx, (*C.uchar)(unsafe.Pointer(&dummyOut[0])), &tmpLen)
		if ret != 1 {
			fmt.Printf("FAIL: %s [%v] - EVP_CipherFinal_ex() = %d.\n", wt, action, ret)
			return false
		}
		cipherOutLen += tmpLen
	}

	if cipherOutLen != C.int(outLen) {
		fmt.Printf("FAIL: %s [%v] - cipherOutLen %d != outLen %d.\n", wt, action, cipherOutLen, outLen)
		return false
	}

	success := true
	if !bytes.Equal(cipherOut, out) {
		fmt.Printf("FAIL: %s [%v] - expected and computed output do not match.\n", wt, action)
		success = false
	}
	if doEncrypt == 1 {
		tagOut := make([]byte, tagLen)
		ret = C.EVP_CIPHER_CTX_ctrl(ctx, ctrlGetTag, C.int(tagLen), unsafe.Pointer(&tagOut[0]))
		if ret != 1 {
			fmt.Printf("FAIL: %s [%v] - EVP_CIPHER_CTX_ctrl() = %d.\n", wt, action, ret)
			return false
		}

		// There are no acceptable CCM cases. All acceptable GCM tests
		// pass. They have len(IV) <= 48. NIST SP 800-38D, 5.2.1.1, p.8,
		// allows 1 <= len(IV) <= 2^64-1, but notes:
		//   "For IVs it is recommended that implementations restrict
		//    support to the length of 96 bits, to promote
		//    interoperability, efficiency and simplicity of design."
		if bytes.Equal(tagOut, tag) != (wt.Result == "valid" || wt.Result == "acceptable") {
			fmt.Printf("FAIL: %s [%v] - expected and computed tag do not match: %d.\n", wt, action, ret)
			success = false
		}
	}
	return success
}

func runAesAeadTest(algorithm string, ctx *C.EVP_CIPHER_CTX, aead *C.EVP_AEAD, wt *wycheproofTestAead) bool {
	key, keyLen := mustDecodeHexString(wt.Key, "key")
	iv, ivLen := mustDecodeHexString(wt.IV, "iv")
	aad, aadLen := mustDecodeHexString(wt.AAD, "aad")
	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")
	ct, ctLen := mustDecodeHexString(wt.CT, "CT")
	tag, tagLen := mustDecodeHexString(wt.Tag, "tag")

	openEvp := checkAesAead(algorithm, ctx, 0, key, keyLen, iv, ivLen, aad, aadLen, ct, ctLen, msg, msgLen, tag, tagLen, wt)
	sealEvp := checkAesAead(algorithm, ctx, 1, key, keyLen, iv, ivLen, aad, aadLen, msg, msgLen, ct, ctLen, tag, tagLen, wt)

	openAead, sealAead := true, true
	if aead != nil {
		ctx := C.EVP_AEAD_CTX_new()
		if ctx == nil {
			log.Fatal("EVP_AEAD_CTX_new() failed")
		}
		defer C.EVP_AEAD_CTX_free(ctx)

		if C.EVP_AEAD_CTX_init(ctx, aead, (*C.uchar)(unsafe.Pointer(&key[0])), C.size_t(keyLen), C.size_t(tagLen), nil) != 1 {
			log.Fatal("Failed to initialize AEAD context")
		}

		// Make sure we don't accidentally prepend or compare against a 0.
		if ctLen == 0 {
			ct = nil
		}

		openAead = checkAeadOpen(ctx, iv, ivLen, aad, aadLen, msg, msgLen, ct, ctLen, tag, tagLen, wt)
		sealAead = checkAeadSeal(ctx, iv, ivLen, aad, aadLen, msg, msgLen, ct, ctLen, tag, tagLen, wt)
	}

	return openEvp && sealEvp && openAead && sealAead
}

func (wtg *wycheproofTestGroupAesAead) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with IV size %d, key size %d and tag size %d...\n", algorithm, wtg.Type, wtg.IVSize, wtg.KeySize, wtg.TagSize)

	cipher, err := cipherAes(algorithm, wtg.KeySize)
	if err != nil {
		fmt.Printf("INFO: Skipping tests with %s\n", err)
		return true
	}
	var aead *C.EVP_AEAD
	if algorithm == "AES-GCM" {
		aead, err = aeadAes(wtg.KeySize)
		if err != nil {
			log.Fatalf("%s", err)
		}
	}

	ctx := C.EVP_CIPHER_CTX_new()
	if ctx == nil {
		log.Fatal("EVP_CIPHER_CTX_new() failed")
	}
	defer C.EVP_CIPHER_CTX_free(ctx)

	C.EVP_CipherInit_ex(ctx, cipher, nil, nil, nil, 1)

	success := true
	for _, wt := range wtg.Tests {
		if !runAesAeadTest(algorithm, ctx, aead, wt) {
			success = false
		}
	}
	return success
}

func runAesCmacTest(cipher *C.EVP_CIPHER, wt *wycheproofTestAesCmac) bool {
	key, keyLen := mustDecodeHexString(wt.Key, "key")
	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")
	tag, tagLen := mustDecodeHexString(wt.Tag, "tag")

	mdctx := C.EVP_MD_CTX_new()
	if mdctx == nil {
		log.Fatal("EVP_MD_CTX_new failed")
	}
	defer C.EVP_MD_CTX_free(mdctx)

	pkey := C.EVP_PKEY_new_CMAC_key(nil, (*C.uchar)(unsafe.Pointer(&key[0])), C.size_t(keyLen), cipher)
	if pkey == nil {
		log.Fatal("CMAC_CTX_new failed")
	}
	defer C.EVP_PKEY_free(pkey)

	ret := C.EVP_DigestSignInit(mdctx, nil, nil, nil, pkey)
	if ret != 1 {
		fmt.Printf("FAIL: %s - EVP_DigestSignInit() = %d.\n", wt, ret)
		return false
	}

	var outLen C.size_t
	outTag := make([]byte, 16)

	ret = C.EVP_DigestSign(mdctx, (*C.uchar)(unsafe.Pointer(&outTag[0])), &outLen, (*C.uchar)(unsafe.Pointer(&msg[0])), C.size_t(msgLen))
	if ret != 1 {
		fmt.Printf("FAIL: %s - EVP_DigestSign() = %d.\n", wt, ret)
		return false
	}

	outTag = outTag[0:tagLen]

	success := true
	if bytes.Equal(tag, outTag) != (wt.Result == "valid") {
		fmt.Printf("FAIL: %s.\n", wt)
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupAesCmac) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with key size %d and tag size %d...\n", algorithm, wtg.Type, wtg.KeySize, wtg.TagSize)

	cipher, err := cipherAes("AES-CBC", wtg.KeySize)
	if err != nil {
		fmt.Printf("INFO: Skipping tests with %d.\n", err)
		return true
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runAesCmacTest(cipher, wt) {
			success = false
		}
	}
	return success
}

func checkAeadOpen(ctx *C.EVP_AEAD_CTX, iv []byte, ivLen int, aad []byte, aadLen int, msg []byte, msgLen int, ct []byte, ctLen int, tag []byte, tagLen int, wt *wycheproofTestAead) bool {
	maxOutLen := ctLen + tagLen

	opened := make([]byte, maxOutLen)
	if maxOutLen == 0 {
		opened = append(opened, 0)
	}
	var openedMsgLen C.size_t

	catCtTag := append(ct, tag...)
	catCtTagLen := len(catCtTag)
	if catCtTagLen == 0 {
		catCtTag = append(catCtTag, 0)
	}
	openRet := C.EVP_AEAD_CTX_open(ctx, (*C.uint8_t)(unsafe.Pointer(&opened[0])), (*C.size_t)(unsafe.Pointer(&openedMsgLen)), C.size_t(maxOutLen), (*C.uint8_t)(unsafe.Pointer(&iv[0])), C.size_t(ivLen), (*C.uint8_t)(unsafe.Pointer(&catCtTag[0])), C.size_t(catCtTagLen), (*C.uint8_t)(unsafe.Pointer(&aad[0])), C.size_t(aadLen))

	if openRet != 1 {
		if wt.Result == "invalid" {
			return true
		}
		fmt.Printf("FAIL: %s - EVP_AEAD_CTX_open() = %d.\n", wt, int(openRet))
		return false
	}

	if openedMsgLen != C.size_t(msgLen) {
		fmt.Printf("FAIL: %s - open length mismatch: got %d, want %d.\n", wt, openedMsgLen, msgLen)
		return false
	}

	openedMsg := opened[0:openedMsgLen]
	if msgLen == 0 {
		msg = nil
	}

	success := false
	if bytes.Equal(openedMsg, msg) == (wt.Result != "invalid") {
		success = true
	} else {
		fmt.Printf("FAIL: %s - msg match: %t.\n", wt, bytes.Equal(openedMsg, msg))
	}
	return success
}

func checkAeadSeal(ctx *C.EVP_AEAD_CTX, iv []byte, ivLen int, aad []byte, aadLen int, msg []byte, msgLen int, ct []byte, ctLen int, tag []byte, tagLen int, wt *wycheproofTestAead) bool {
	maxOutLen := msgLen + tagLen

	sealed := make([]byte, maxOutLen)
	if maxOutLen == 0 {
		sealed = append(sealed, 0)
	}
	var sealedLen C.size_t

	sealRet := C.EVP_AEAD_CTX_seal(ctx, (*C.uint8_t)(unsafe.Pointer(&sealed[0])), (*C.size_t)(unsafe.Pointer(&sealedLen)), C.size_t(maxOutLen), (*C.uint8_t)(unsafe.Pointer(&iv[0])), C.size_t(ivLen), (*C.uint8_t)(unsafe.Pointer(&msg[0])), C.size_t(msgLen), (*C.uint8_t)(unsafe.Pointer(&aad[0])), C.size_t(aadLen))

	if sealRet != 1 {
		success := (wt.Result == "invalid")
		if !success {
			fmt.Printf("FAIL: %s - EVP_AEAD_CTX_seal() = %d.\n", wt, int(sealRet))
		}
		return success
	}

	if sealedLen != C.size_t(maxOutLen) {
		fmt.Printf("FAIL: %s - seal length mismatch: got %d, want %d.\n", wt, sealedLen, maxOutLen)
		return false
	}

	sealedCt := sealed[0:msgLen]
	sealedTag := sealed[msgLen:maxOutLen]

	success := false
	if (bytes.Equal(sealedCt, ct) && bytes.Equal(sealedTag, tag)) == (wt.Result != "invalid") {
		success = true
	} else {
		fmt.Printf("FAIL: %s - EVP_AEAD_CTX_seal() = %d, ct match: %t, tag match: %t.\n", wt, int(sealRet), bytes.Equal(sealedCt, ct), bytes.Equal(sealedTag, tag))
	}
	return success
}

func runChaCha20Poly1305Test(algorithm string, wt *wycheproofTestAead) bool {
	var aead *C.EVP_AEAD
	switch algorithm {
	case "CHACHA20-POLY1305":
		aead = C.EVP_aead_chacha20_poly1305()
	case "XCHACHA20-POLY1305":
		aead = C.EVP_aead_xchacha20_poly1305()
	}

	key, keyLen := mustDecodeHexString(wt.Key, "key")
	iv, ivLen := mustDecodeHexString(wt.IV, "iv")
	aad, aadLen := mustDecodeHexString(wt.AAD, "aad")
	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")

	// ct and tag are concatenated in checkAeadOpen(), so don't use mustDecodeHexString()
	ct, err := hex.DecodeString(wt.CT)
	if err != nil {
		log.Fatalf("Failed to decode ct %q: %v", wt.CT, err)
	}
	tag, err := hex.DecodeString(wt.Tag)
	if err != nil {
		log.Fatalf("Failed to decode tag %q: %v", wt.Tag, err)
	}
	ctLen, tagLen := len(ct), len(tag)

	ctx := C.EVP_AEAD_CTX_new()
	if ctx == nil {
		log.Fatal("EVP_AEAD_CTX_new() failed")
	}
	defer C.EVP_AEAD_CTX_free(ctx)
	if C.EVP_AEAD_CTX_init(ctx, aead, (*C.uchar)(unsafe.Pointer(&key[0])), C.size_t(keyLen), C.size_t(tagLen), nil) != 1 {
		log.Fatal("Failed to initialize AEAD context")
	}

	openSuccess := checkAeadOpen(ctx, iv, ivLen, aad, aadLen, msg, msgLen, ct, ctLen, tag, tagLen, wt)
	sealSuccess := checkAeadSeal(ctx, iv, ivLen, aad, aadLen, msg, msgLen, ct, ctLen, tag, tagLen, wt)

	return openSuccess && sealSuccess
}

func runEvpChaCha20Poly1305Test(ctx *C.EVP_CIPHER_CTX, algorithm string, wt *wycheproofTestAead) bool {
	var aead *C.EVP_CIPHER
	switch algorithm {
	case "CHACHA20-POLY1305":
		aead = C.EVP_chacha20_poly1305()
	case "XCHACHA20-POLY1305":
		return true
	}

	key, _ := mustDecodeHexString(wt.Key, "key")
	iv, ivLen := mustDecodeHexString(wt.IV, "iv")
	aad, aadLen := mustDecodeHexString(wt.AAD, "aad")
	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")

	ct, err := hex.DecodeString(wt.CT)
	if err != nil {
		log.Fatalf("Failed to decode ct %q: %v", wt.CT, err)
	}
	tag, err := hex.DecodeString(wt.Tag)
	if err != nil {
		log.Fatalf("Failed to decode tag %q: %v", wt.Tag, err)
	}
	ctLen, tagLen := len(ct), len(tag)

	if C.EVP_EncryptInit_ex(ctx, aead, nil, nil, nil) != 1 {
		log.Fatal("Failed to initialize EVP_CIPHER_CTX with cipher")
	}
	if C.EVP_CIPHER_CTX_ctrl(ctx, C.EVP_CTRL_AEAD_SET_IVLEN, C.int(ivLen), nil) != 1 {
		log.Fatal("Failed EVP_CTRL_AEAD_SET_IVLEN")
	}
	if C.EVP_EncryptInit_ex(ctx, nil, nil, (*C.uchar)(unsafe.Pointer(&key[0])), nil) != 1 {
		log.Fatal("Failed EVP_EncryptInit_ex key")
	}
	if C.EVP_EncryptInit_ex(ctx, nil, nil, nil, (*C.uchar)(unsafe.Pointer(&iv[0]))) != 1 {
		log.Fatal("Failed EVP_EncryptInit_ex iv")
	}

	var len C.int

	if C.EVP_EncryptUpdate(ctx, nil, (*C.int)(unsafe.Pointer(&len)), (*C.uchar)(&aad[0]), (C.int)(aadLen)) != 1 {
		log.Fatal("Failed EVP_EncryptUpdate aad")
	}

	sealed := make([]byte, ctLen+tagLen)
	copy(sealed, msg)
	if C.EVP_EncryptUpdate(ctx, (*C.uchar)(unsafe.Pointer(&sealed[0])), (*C.int)(unsafe.Pointer(&len)), (*C.uchar)(unsafe.Pointer(&sealed[0])), (C.int)(msgLen)) != 1 {
		log.Fatal("Failed EVP_EncryptUpdate msg")
	}
	outLen := len
	if C.EVP_EncryptFinal_ex(ctx, (*C.uchar)(unsafe.Pointer(&sealed[outLen])), (*C.int)(unsafe.Pointer(&len))) != 1 {
		log.Fatal("Failed EVP_EncryptFinal msg")
	}
	outLen += len
	if C.EVP_CIPHER_CTX_ctrl(ctx, C.EVP_CTRL_AEAD_GET_TAG, (C.int)(tagLen), unsafe.Pointer(&sealed[outLen])) != 1 {
		log.Fatal("Failed EVP_CTRL_AEAD_GET_TAG")
	}
	outLen += (C.int)(tagLen)

	if (C.int)(ctLen+tagLen) != outLen {
		fmt.Printf("%s\n", wt)
	}

	sealSuccess := false
	ctMatch := bytes.Equal(ct, sealed[:ctLen])
	tagMatch := bytes.Equal(tag, sealed[ctLen:])
	if (ctMatch && tagMatch) == (wt.Result != "invalid") {
		sealSuccess = true
	} else {
		fmt.Printf("%s - ct match: %t tag match: %t\n", wt, ctMatch, tagMatch)
	}

	if C.EVP_DecryptInit_ex(ctx, aead, nil, nil, nil) != 1 {
		log.Fatal("Failed to initialize EVP_CIPHER_CTX with cipher")
	}
	if C.EVP_DecryptInit_ex(ctx, nil, nil, (*C.uchar)(unsafe.Pointer(&key[0])), nil) != 1 {
		log.Fatal("Failed EVP_EncryptInit_ex key")
	}

	if C.EVP_CIPHER_CTX_ctrl(ctx, C.EVP_CTRL_AEAD_SET_IVLEN, C.int(ivLen), nil) != 1 {
		log.Fatal("Failed EVP_CTRL_AEAD_SET_IVLEN")
	}
	if C.EVP_DecryptInit_ex(ctx, nil, nil, nil, (*C.uchar)(unsafe.Pointer(&iv[0]))) != 1 {
		log.Fatal("Failed EVP_EncryptInit_ex iv")
	}

	if C.EVP_CIPHER_CTX_ctrl(ctx, C.EVP_CTRL_AEAD_SET_TAG, (C.int)(tagLen), unsafe.Pointer(&tag[0])) != 1 {
		log.Fatal("Failed EVP_CTRL_AEAD_SET_TAG")
	}

	if ctLen == 0 {
		ct = append(ct, 0)
	}

	opened := make([]byte, msgLen+tagLen)
	copy(opened, ct)
	if msgLen+aadLen == 0 {
		opened = append(opened, 0)
	}

	if C.EVP_DecryptUpdate(ctx, nil, (*C.int)(unsafe.Pointer(&len)), (*C.uchar)(unsafe.Pointer(&aad[0])), C.int(aadLen)) != 1 {
		log.Fatal("Failed EVP_EncryptUpdate msg")
	}

	if C.EVP_DecryptUpdate(ctx, (*C.uchar)(unsafe.Pointer(&opened[0])), (*C.int)(unsafe.Pointer(&len)), (*C.uchar)(unsafe.Pointer(&opened[0])), (C.int)(ctLen)) != 1 {
		log.Fatal("Failed EVP_EncryptUpdate msg")
	}
	outLen = len

	var ret C.int
	if wt.Result != "invalid" {
		ret = 1
	}

	if C.EVP_DecryptFinal_ex(ctx, (*C.uchar)(unsafe.Pointer(&opened[outLen])), (*C.int)(unsafe.Pointer(&len))) != ret {
		log.Fatalf("Failed EVP_EncryptFinal msg %s\n", wt)
	}
	outLen += len

	openSuccess := true
	if (C.int)(msgLen) != outLen {
		openSuccess = false
		fmt.Printf("%s\n", wt)
	}

	if wt.Result != "invalid" && !bytes.Equal(opened[:outLen], msg[:msgLen]) {
		fmt.Printf("failed %s\n", wt)
		openSuccess = false
	}

	return sealSuccess && openSuccess
}

func (wtg *wycheproofTestGroupChaCha) run(algorithm string, variant testVariant) bool {
	// ChaCha20-Poly1305 currently only supports nonces of length 12 (96 bits)
	if algorithm == "CHACHA20-POLY1305" && wtg.IVSize != 96 {
		return true
	}

	fmt.Printf("Running %v test group %v with IV size %d, key size %d, tag size %d...\n", algorithm, wtg.Type, wtg.IVSize, wtg.KeySize, wtg.TagSize)

	ctx := C.EVP_CIPHER_CTX_new()
	if ctx == nil {
		log.Fatal("EVP_CIPHER_CTX_new() failed")
	}
	defer C.EVP_CIPHER_CTX_free(ctx)

	success := true
	for _, wt := range wtg.Tests {
		if !runChaCha20Poly1305Test(algorithm, wt) {
			success = false
		}
		if !runEvpChaCha20Poly1305Test(ctx, algorithm, wt) {
			success = false
		}
	}
	return success
}

// DER encode the signature (so DSA_verify() can decode and encode it again)
func encodeDSAP1363Sig(wtSig string) (*C.uchar, C.int) {
	cSig := C.DSA_SIG_new()
	if cSig == nil {
		log.Fatal("DSA_SIG_new() failed")
	}
	defer C.DSA_SIG_free(cSig)

	sigLen := len(wtSig)
	r := C.CString(wtSig[:sigLen/2])
	s := C.CString(wtSig[sigLen/2:])
	defer C.free(unsafe.Pointer(r))
	defer C.free(unsafe.Pointer(s))
	var sigR *C.BIGNUM
	var sigS *C.BIGNUM
	defer C.BN_free(sigR)
	defer C.BN_free(sigS)
	if C.BN_hex2bn(&sigR, r) == 0 {
		return nil, 0
	}
	if C.BN_hex2bn(&sigS, s) == 0 {
		return nil, 0
	}
	if C.DSA_SIG_set0(cSig, sigR, sigS) == 0 {
		return nil, 0
	}
	sigR = nil
	sigS = nil

	derLen := C.i2d_DSA_SIG(cSig, nil)
	if derLen == 0 {
		return nil, 0
	}
	cDer := (*C.uchar)(C.malloc(C.ulong(derLen)))
	if cDer == nil {
		log.Fatal("malloc failed")
	}

	p := cDer
	ret := C.i2d_DSA_SIG(cSig, (**C.uchar)(&p))
	if ret == 0 || ret != derLen {
		C.free(unsafe.Pointer(cDer))
		return nil, 0
	}

	return cDer, derLen
}

func runDSATest(dsa *C.DSA, md *C.EVP_MD, variant testVariant, wt *wycheproofTestDSA) bool {
	msg, msgLen := mustHashHexMessage(md, wt.Msg)

	var ret C.int
	if variant == P1363 {
		cDer, derLen := encodeDSAP1363Sig(wt.Sig)
		if cDer == nil {
			fmt.Print("FAIL: unable to decode signature")
			return false
		}
		defer C.free(unsafe.Pointer(cDer))

		ret = C.DSA_verify(0, (*C.uchar)(unsafe.Pointer(&msg[0])), C.int(msgLen), (*C.uchar)(unsafe.Pointer(cDer)), C.int(derLen), dsa)
	} else {
		sig, sigLen := mustDecodeHexString(wt.Sig, "sig")
		ret = C.DSA_verify(0, (*C.uchar)(unsafe.Pointer(&msg[0])), C.int(msgLen), (*C.uchar)(unsafe.Pointer(&sig[0])), C.int(sigLen), dsa)
	}

	success := true
	if ret == 1 != (wt.Result == "valid") {
		fmt.Printf("FAIL: %s - DSA_verify() = %d.\n", wt, wt.Result)
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupDSA) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v, key size %d and %v...\n", algorithm, wtg.Type, wtg.Key.KeySize, wtg.SHA)

	dsa := C.DSA_new()
	if dsa == nil {
		log.Fatal("DSA_new failed")
	}
	defer C.DSA_free(dsa)

	var bnG *C.BIGNUM
	wg := C.CString(wtg.Key.G)
	if C.BN_hex2bn(&bnG, wg) == 0 {
		log.Fatal("Failed to decode g")
	}
	C.free(unsafe.Pointer(wg))

	var bnP *C.BIGNUM
	wp := C.CString(wtg.Key.P)
	if C.BN_hex2bn(&bnP, wp) == 0 {
		log.Fatal("Failed to decode p")
	}
	C.free(unsafe.Pointer(wp))

	var bnQ *C.BIGNUM
	wq := C.CString(wtg.Key.Q)
	if C.BN_hex2bn(&bnQ, wq) == 0 {
		log.Fatal("Failed to decode q")
	}
	C.free(unsafe.Pointer(wq))

	ret := C.DSA_set0_pqg(dsa, bnP, bnQ, bnG)
	if ret != 1 {
		log.Fatalf("DSA_set0_pqg returned %d", ret)
	}

	var bnY *C.BIGNUM
	wy := C.CString(wtg.Key.Y)
	if C.BN_hex2bn(&bnY, wy) == 0 {
		log.Fatal("Failed to decode y")
	}
	C.free(unsafe.Pointer(wy))

	ret = C.DSA_set0_key(dsa, bnY, nil)
	if ret != 1 {
		log.Fatalf("DSA_set0_key returned %d", ret)
	}

	md, err := hashEvpMdFromString(wtg.SHA)
	if err != nil {
		log.Fatalf("Failed to get hash: %v", err)
	}

	der, derLen := mustDecodeHexString(wtg.KeyDER, "DER encoded key")

	Cder := (*C.uchar)(C.malloc(C.ulong(derLen)))
	if Cder == nil {
		log.Fatal("malloc failed")
	}
	C.memcpy(unsafe.Pointer(Cder), unsafe.Pointer(&der[0]), C.ulong(derLen))

	p := (*C.uchar)(Cder)
	dsaDER := C.d2i_DSA_PUBKEY(nil, (**C.uchar)(&p), C.long(derLen))
	defer C.DSA_free(dsaDER)
	C.free(unsafe.Pointer(Cder))

	keyPEM := C.CString(wtg.KeyPEM)
	bio := C.BIO_new_mem_buf(unsafe.Pointer(keyPEM), C.int(len(wtg.KeyPEM)))
	if bio == nil {
		log.Fatal("BIO_new_mem_buf failed")
	}
	defer C.free(unsafe.Pointer(keyPEM))
	defer C.BIO_free(bio)

	dsaPEM := C.PEM_read_bio_DSA_PUBKEY(bio, nil, nil, nil)
	if dsaPEM == nil {
		log.Fatal("PEM_read_bio_DSA_PUBKEY failed")
	}
	defer C.DSA_free(dsaPEM)

	success := true
	for _, wt := range wtg.Tests {
		if !runDSATest(dsa, md, variant, wt) {
			success = false
		}
		if !runDSATest(dsaDER, md, variant, wt) {
			success = false
		}
		if !runDSATest(dsaPEM, md, variant, wt) {
			success = false
		}
	}
	return success
}

func runECDHTest(nid int, variant testVariant, wt *wycheproofTestECDH) bool {
	privKey := C.EC_KEY_new_by_curve_name(C.int(nid))
	if privKey == nil {
		log.Fatalf("EC_KEY_new_by_curve_name failed")
	}
	defer C.EC_KEY_free(privKey)

	var bnPriv *C.BIGNUM
	wPriv := C.CString(wt.Private)
	if C.BN_hex2bn(&bnPriv, wPriv) == 0 {
		log.Fatal("Failed to decode wPriv")
	}
	C.free(unsafe.Pointer(wPriv))
	defer C.BN_free(bnPriv)

	ret := C.EC_KEY_set_private_key(privKey, bnPriv)
	if ret != 1 {
		fmt.Printf("FAIL: %s - EC_KEY_set_private_key() = %d.\n", wt, ret)
		return false
	}

	pub, pubLen := mustDecodeHexString(wt.Public, "public key")

	Cpub := (*C.uchar)(C.malloc(C.ulong(pubLen)))
	if Cpub == nil {
		log.Fatal("malloc failed")
	}
	C.memcpy(unsafe.Pointer(Cpub), unsafe.Pointer(&pub[0]), C.ulong(pubLen))

	p := (*C.uchar)(Cpub)
	var pubKey *C.EC_KEY
	if variant == EcPoint {
		pubKey = C.EC_KEY_new_by_curve_name(C.int(nid))
		if pubKey == nil {
			log.Fatal("EC_KEY_new_by_curve_name failed")
		}
		pubKey = C.o2i_ECPublicKey(&pubKey, (**C.uchar)(&p), C.long(pubLen))
	} else {
		pubKey = C.d2i_EC_PUBKEY(nil, (**C.uchar)(&p), C.long(pubLen))
	}
	defer C.EC_KEY_free(pubKey)
	C.free(unsafe.Pointer(Cpub))

	if pubKey == nil {
		if wt.Result == "invalid" || wt.Result == "acceptable" {
			return true
		}
		fmt.Printf("FAIL: %s - ASN decoding failed.\n", wt)
		return false
	}

	privGroup := C.EC_KEY_get0_group(privKey)

	secLen := (C.EC_GROUP_get_degree(privGroup) + 7) / 8

	secret := make([]byte, secLen)
	if secLen == 0 {
		secret = append(secret, 0)
	}

	pubPoint := C.EC_KEY_get0_public_key(pubKey)

	ret = C.ECDH_compute_key(unsafe.Pointer(&secret[0]), C.ulong(secLen), pubPoint, privKey, nil)
	if ret != C.int(secLen) {
		if wt.Result == "invalid" {
			return true
		}
		fmt.Printf("FAIL: %s - ECDH_compute_key() = %d, want %d.\n", wt, ret, int(secLen))
		return false
	}

	shared, sharedLen := mustDecodeHexString(wt.Shared, "shared secret")

	// XXX The shared fields of the secp224k1 test cases have a 0 byte preprended.
	if sharedLen == int(secLen)+1 && shared[0] == 0 {
		fmt.Printf("INFO: %s - prepending 0 byte.\n", wt)
		// shared = shared[1:];
		zero := make([]byte, 1, secLen+1)
		secret = append(zero, secret...)
	}

	success := true
	if !bytes.Equal(shared, secret) {
		fmt.Printf("FAIL: %s - expected and computed shared secret do not match.\n", wt)
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupECDH) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with curve %v and %v encoding...\n", algorithm, wtg.Type, wtg.Curve, wtg.Encoding)

	nid, err := nidFromString(wtg.Curve)
	if err != nil {
		log.Fatalf("Failed to get nid for curve: %v", err)
	}
	if skipCurve(nid) {
		return true
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runECDHTest(nid, variant, wt) {
			success = false
		}
	}
	return success
}

func runECDHWebCryptoTest(nid int, wt *wycheproofTestECDHWebCrypto) bool {
	privKey := C.EC_KEY_new_by_curve_name(C.int(nid))
	if privKey == nil {
		log.Fatalf("EC_KEY_new_by_curve_name failed")
	}
	defer C.EC_KEY_free(privKey)

	d, err := base64.RawURLEncoding.DecodeString(wt.Private.D)
	if err != nil {
		log.Fatalf("Failed to base64 decode d: %v", err)
	}
	bnD := C.BN_bin2bn((*C.uchar)(unsafe.Pointer(&d[0])), C.int(len(d)), nil)
	if bnD == nil {
		log.Fatal("Failed to decode D")
	}
	defer C.BN_free(bnD)

	ret := C.EC_KEY_set_private_key(privKey, bnD)
	if ret != 1 {
		fmt.Printf("FAIL: %s - EC_KEY_set_private_key() = %d.\n", wt, ret)
		return false
	}

	x, err := base64.RawURLEncoding.DecodeString(wt.Public.X)
	if err != nil {
		log.Fatalf("Failed to base64 decode x: %v", err)
	}
	bnX := C.BN_bin2bn((*C.uchar)(unsafe.Pointer(&x[0])), C.int(len(x)), nil)
	if bnX == nil {
		log.Fatal("Failed to decode X")
	}
	defer C.BN_free(bnX)

	y, err := base64.RawURLEncoding.DecodeString(wt.Public.Y)
	if err != nil {
		log.Fatalf("Failed to base64 decode y: %v", err)
	}
	bnY := C.BN_bin2bn((*C.uchar)(unsafe.Pointer(&y[0])), C.int(len(y)), nil)
	if bnY == nil {
		log.Fatal("Failed to decode Y")
	}
	defer C.BN_free(bnY)

	pubKey := C.EC_KEY_new_by_curve_name(C.int(nid))
	if pubKey == nil {
		log.Fatal("Failed to create EC_KEY")
	}
	defer C.EC_KEY_free(pubKey)

	ret = C.EC_KEY_set_public_key_affine_coordinates(pubKey, bnX, bnY)
	if ret != 1 {
		if wt.Result == "invalid" {
			return true
		}
		fmt.Printf("FAIL: %s - EC_KEY_set_public_key_affine_coordinates() = %d.\n", wt, ret)
		return false
	}
	pubPoint := C.EC_KEY_get0_public_key(pubKey)

	privGroup := C.EC_KEY_get0_group(privKey)

	secLen := (C.EC_GROUP_get_degree(privGroup) + 7) / 8

	secret := make([]byte, secLen)
	if secLen == 0 {
		secret = append(secret, 0)
	}

	ret = C.ECDH_compute_key(unsafe.Pointer(&secret[0]), C.ulong(secLen), pubPoint, privKey, nil)
	if ret != C.int(secLen) {
		if wt.Result == "invalid" {
			return true
		}
		fmt.Printf("FAIL: %s - ECDH_compute_key() = %d, want %d.\n", wt, ret, int(secLen))
		return false
	}

	shared, _ := mustDecodeHexString(wt.Shared, "shared secret")

	success := true
	if !bytes.Equal(shared, secret) {
		fmt.Printf("FAIL: %s - expected and computed shared secret do not match.\n", wt)
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupECDHWebCrypto) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with curve %v and %v encoding...\n", algorithm, wtg.Type, wtg.Curve, wtg.Encoding)

	nid, err := nidFromString(wtg.Curve)
	if err != nil {
		log.Fatalf("Failed to get nid for curve: %v", err)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runECDHWebCryptoTest(nid, wt) {
			success = false
		}
	}
	return success
}

func runECDSATest(ecKey *C.EC_KEY, md *C.EVP_MD, nid int, variant testVariant, wt *wycheproofTestECDSA) bool {
	msg, msgLen := mustHashHexMessage(md, wt.Msg)

	var ret C.int
	if variant == P1363 {
		order_bytes := int((C.EC_GROUP_order_bits(C.EC_KEY_get0_group(ecKey)) + 7) / 8)
		if len(wt.Sig)/2 != 2*order_bytes {
			if wt.Result == "valid" {
				fmt.Printf("FAIL: %s - incorrect signature length, %d, %d\n", wt, len(wt.Sig)/2, 2*order_bytes)
				return false
			}
			return true
		}

		cDer, derLen := encodeECDSAWebCryptoSig(wt.Sig)
		if cDer == nil {
			fmt.Print("FAIL: unable to decode signature")
			return false
		}
		defer C.free(unsafe.Pointer(cDer))

		ret = C.ECDSA_verify(0, (*C.uchar)(unsafe.Pointer(&msg[0])), C.int(msgLen), (*C.uchar)(unsafe.Pointer(cDer)), C.int(derLen), ecKey)
	} else {
		sig, sigLen := mustDecodeHexString(wt.Sig, "sig")

		ret = C.ECDSA_verify(0, (*C.uchar)(unsafe.Pointer(&msg[0])), C.int(msgLen), (*C.uchar)(unsafe.Pointer(&sig[0])), C.int(sigLen), ecKey)
	}

	// XXX audit acceptable cases...
	success := true
	if ret == 1 != (wt.Result == "valid") && wt.Result != "acceptable" {
		fmt.Printf("FAIL: %s - ECDSA_verify() = %d.\n", wt, int(ret))
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupECDSA) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with curve %v, key size %d and %v...\n", algorithm, wtg.Type, wtg.Key.Curve, wtg.Key.KeySize, wtg.SHA)

	nid, err := nidFromString(wtg.Key.Curve)
	if err != nil {
		log.Fatalf("Failed to get nid for curve: %v", err)
	}
	if skipCurve(nid) {
		return true
	}
	if skipHash(wtg.SHA) {
		return true
	}
	ecKey := C.EC_KEY_new_by_curve_name(C.int(nid))
	if ecKey == nil {
		log.Fatal("EC_KEY_new_by_curve_name failed")
	}
	defer C.EC_KEY_free(ecKey)

	var bnX *C.BIGNUM
	wx := C.CString(wtg.Key.WX)
	if C.BN_hex2bn(&bnX, wx) == 0 {
		log.Fatal("Failed to decode WX")
	}
	C.free(unsafe.Pointer(wx))
	defer C.BN_free(bnX)

	var bnY *C.BIGNUM
	wy := C.CString(wtg.Key.WY)
	if C.BN_hex2bn(&bnY, wy) == 0 {
		log.Fatal("Failed to decode WY")
	}
	C.free(unsafe.Pointer(wy))
	defer C.BN_free(bnY)

	if C.EC_KEY_set_public_key_affine_coordinates(ecKey, bnX, bnY) != 1 {
		log.Fatal("Failed to set EC public key")
	}

	nid, err = nidFromString(wtg.SHA)
	if err != nil {
		log.Fatalf("Failed to get MD NID: %v", err)
	}
	md, err := hashEvpMdFromString(wtg.SHA)
	if err != nil {
		log.Fatalf("Failed to get hash: %v", err)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runECDSATest(ecKey, md, nid, variant, wt) {
			success = false
		}
	}
	return success
}

// DER encode the signature (so that ECDSA_verify() can decode and encode it again...)
func encodeECDSAWebCryptoSig(wtSig string) (*C.uchar, C.int) {
	cSig := C.ECDSA_SIG_new()
	if cSig == nil {
		log.Fatal("ECDSA_SIG_new() failed")
	}
	defer C.ECDSA_SIG_free(cSig)

	sigLen := len(wtSig)
	r := C.CString(wtSig[:sigLen/2])
	s := C.CString(wtSig[sigLen/2:])
	defer C.free(unsafe.Pointer(r))
	defer C.free(unsafe.Pointer(s))
	var sigR *C.BIGNUM
	var sigS *C.BIGNUM
	defer C.BN_free(sigR)
	defer C.BN_free(sigS)
	if C.BN_hex2bn(&sigR, r) == 0 {
		return nil, 0
	}
	if C.BN_hex2bn(&sigS, s) == 0 {
		return nil, 0
	}
	if C.ECDSA_SIG_set0(cSig, sigR, sigS) == 0 {
		return nil, 0
	}
	sigR = nil
	sigS = nil

	derLen := C.i2d_ECDSA_SIG(cSig, nil)
	if derLen == 0 {
		return nil, 0
	}
	cDer := (*C.uchar)(C.malloc(C.ulong(derLen)))
	if cDer == nil {
		log.Fatal("malloc failed")
	}

	p := cDer
	ret := C.i2d_ECDSA_SIG(cSig, (**C.uchar)(&p))
	if ret == 0 || ret != derLen {
		C.free(unsafe.Pointer(cDer))
		return nil, 0
	}

	return cDer, derLen
}

func runEcCurveTest(wt *wycheproofTestEcCurve) bool {
	oid := C.CString(wt.OID)
	defer C.free(unsafe.Pointer(oid))

	nid := C.OBJ_txt2nid(oid)
	if nid == C.NID_undef {
		fmt.Printf("INFO: %s: %s: unknown OID %s\n", wt, wt.Name, wt.OID)
		return false
	}

	builtinGroup := C.EC_GROUP_new_by_curve_name(nid)
	defer C.EC_GROUP_free(builtinGroup)

	if builtinGroup == nil {
		fmt.Printf("INFO: %s: %s: no builtin curve for OID %s\n", wt, wt.Name, wt.OID)
		return true
	}

	p := mustConvertBigIntToBigNum(wt.P)
	defer C.BN_free(p)
	a := mustConvertBigIntToBigNum(wt.A)
	defer C.BN_free(a)
	b := mustConvertBigIntToBigNum(wt.B)
	defer C.BN_free(b)
	n := mustConvertBigIntToBigNum(wt.N)
	defer C.BN_free(n)
	x := mustConvertBigIntToBigNum(wt.Gx)
	defer C.BN_free(x)
	y := mustConvertBigIntToBigNum(wt.Gy)
	defer C.BN_free(y)

	group := C.EC_GROUP_new_curve_GFp(p, a, b, (*C.BN_CTX)(nil))
	defer C.EC_GROUP_free(group)

	if group == nil {
		log.Fatalf("EC_GROUP_new_curve_GFp failed")
	}

	point := C.EC_POINT_new(group)
	defer C.EC_POINT_free(point)

	if point == nil {
		log.Fatalf("EC_POINT_new failed")
	}

	if C.EC_POINT_set_affine_coordinates(group, point, x, y, (*C.BN_CTX)(nil)) == 0 {
		log.Fatalf("EC_POINT_set_affine_coordinates failed")
	}

	if C.EC_GROUP_set_generator(group, point, n, (*C.BIGNUM)(nil)) == 0 {
		log.Fatalf("EC_POINT_set_generator failed")
	}

	success := true
	if C.EC_GROUP_cmp(group, builtinGroup, (*C.BN_CTX)(nil)) != 0 {
		fmt.Printf("FAIL: %s %s builtin curve has wrong parameters\n", wt, wt.Name)
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupEcCurve) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v...\n", algorithm, wtg.Type)

	success := true
	for _, wt := range wtg.Tests {
		if !runEcCurveTest(wt) {
			success = false
		}
	}
	return success
}

func runEdDSATest(pkey *C.EVP_PKEY, wt *wycheproofTestEdDSA) bool {
	mdctx := C.EVP_MD_CTX_new()
	if mdctx == nil {
		log.Fatal("EVP_MD_CTX_new failed")
	}
	defer C.EVP_MD_CTX_free(mdctx)

	if C.EVP_DigestVerifyInit(mdctx, nil, nil, nil, pkey) != 1 {
		log.Fatal("EVP_DigestVerifyInit failed")
	}

	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")
	sig, sigLen := mustDecodeHexString(wt.Sig, "sig")

	ret := C.EVP_DigestVerify(mdctx, (*C.uchar)(unsafe.Pointer(&sig[0])), (C.size_t)(sigLen), (*C.uchar)(unsafe.Pointer(&msg[0])), (C.size_t)(msgLen))

	success := true
	if (ret == 1) != (wt.Result == "valid") {
		fmt.Printf("FAIL: %s - EVP_DigestVerify() = %d.\n", wt, int(ret))
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupEdDSA) run(algorithm string, variant testVariant) bool {
	if wtg.Key.Curve == "edwards25519" {
		fmt.Printf("Running %v test group %v...\n", algorithm, wtg.Type)
	} else {
		fmt.Printf("INFO: Skipping %v test group %v for %v...\n", algorithm, wtg.Type, wtg.Key.Curve)
		return true
	}

	pubKey, pubKeyLen := mustDecodeHexString(wtg.Key.Pk, "pubkey")

	pkey := C.EVP_PKEY_new_raw_public_key(C.EVP_PKEY_ED25519, nil, (*C.uchar)(unsafe.Pointer(&pubKey[0])), (C.size_t)(pubKeyLen))
	if pkey == nil {
		log.Fatal("EVP_PKEY_new_raw_public_key failed")
	}
	defer C.EVP_PKEY_free(pkey)

	success := true
	for _, wt := range wtg.Tests {
		if !runEdDSATest(pkey, wt) {
			success = false
		}
	}
	return success
}

func runHkdfTest(md *C.EVP_MD, wt *wycheproofTestHkdf) bool {
	ikm, ikmLen := mustDecodeHexString(wt.Ikm, "ikm")
	salt, saltLen := mustDecodeHexString(wt.Salt, "salt")
	info, infoLen := mustDecodeHexString(wt.Info, "info")

	outLen := wt.Size
	out := make([]byte, outLen)
	if outLen == 0 {
		out = append(out, 0)
	}

	pctx := C.EVP_PKEY_CTX_new_id(C.EVP_PKEY_HKDF, nil)
	if pctx == nil {
		log.Fatalf("EVP_PKEY_CTX_new_id failed")
	}
	defer C.EVP_PKEY_CTX_free(pctx)

	ret := C.EVP_PKEY_derive_init(pctx)
	if ret <= 0 {
		log.Fatalf("EVP_PKEY_derive_init failed, want 1, got %d", ret)
	}

	ret = C.wp_EVP_PKEY_CTX_set_hkdf_md(pctx, md)
	if ret <= 0 {
		log.Fatalf("EVP_PKEY_CTX_set_hkdf_md failed, want 1, got %d", ret)
	}

	ret = C.wp_EVP_PKEY_CTX_set1_hkdf_salt(pctx, (*C.uchar)(&salt[0]), C.size_t(saltLen))
	if ret <= 0 {
		log.Fatalf("EVP_PKEY_CTX_set1_hkdf_salt failed, want 1, got %d", ret)
	}

	ret = C.wp_EVP_PKEY_CTX_set1_hkdf_key(pctx, (*C.uchar)(&ikm[0]), C.size_t(ikmLen))
	if ret <= 0 {
		log.Fatalf("EVP_PKEY_CTX_set1_hkdf_key failed, want 1, got %d", ret)
	}

	ret = C.wp_EVP_PKEY_CTX_add1_hkdf_info(pctx, (*C.uchar)(&info[0]), C.size_t(infoLen))
	if ret <= 0 {
		log.Fatalf("EVP_PKEY_CTX_add1_hkdf_info failed, want 1, got %d", ret)
	}

	ret = C.EVP_PKEY_derive(pctx, (*C.uchar)(unsafe.Pointer(&out[0])), (*C.size_t)(unsafe.Pointer(&outLen)))
	if ret <= 0 {
		success := wt.Result == "invalid"
		if !success {
			fmt.Printf("FAIL: %s - got %d.\n", wt, ret)
		}
		return success
	}

	okm, _ := mustDecodeHexString(wt.Okm, "okm")
	if !bytes.Equal(out[:outLen], okm) {
		fmt.Printf("FAIL: %s - expected and computed output don't match.\n", wt)
	}

	return wt.Result == "valid"
}

func (wtg *wycheproofTestGroupHkdf) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with key size %d...\n", algorithm, wtg.Type, wtg.KeySize)
	md, err := hashEvpMdFromString(strings.TrimPrefix(algorithm, "HKDF-"))
	if err != nil {
		log.Fatalf("Failed to get hash: %v", err)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runHkdfTest(md, wt) {
			success = false
		}
	}
	return success
}

func runHmacTest(md *C.EVP_MD, tagBytes int, wt *wycheproofTestHmac) bool {
	key, keyLen := mustDecodeHexString(wt.Key, "key")
	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")

	got := make([]byte, C.EVP_MAX_MD_SIZE)
	var gotLen C.uint

	ret := C.HMAC(md, unsafe.Pointer(&key[0]), C.int(keyLen), (*C.uchar)(unsafe.Pointer(&msg[0])), C.size_t(msgLen), (*C.uchar)(unsafe.Pointer(&got[0])), &gotLen)

	success := true
	if ret == nil {
		if wt.Result != "invalid" {
			success = false
			fmt.Printf("FAIL: Test case %s - HMAC: got nil.\n", wt)
		}
		return success
	}

	if int(gotLen) < tagBytes {
		fmt.Printf("FAIL: %s - HMAC length: got %d, want %d.\n", wt, gotLen, tagBytes)
		return false
	}

	tag, _ := mustDecodeHexString(wt.Tag, "tag")
	success = bytes.Equal(got[:tagBytes], tag) == (wt.Result == "valid")

	if !success {
		fmt.Printf("FAIL: %s - got %t.\n", wt, success)
	}

	return success
}

func (wtg *wycheproofTestGroupHmac) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with key size %d and tag size %d...\n", algorithm, wtg.Type, wtg.KeySize, wtg.TagSize)
	prefix := "SHA-"
	if strings.HasPrefix(algorithm, "HMACSHA3-") {
		prefix = "SHA"
	}
	if algorithm == "HMACSM3" {
		prefix = ""
		algorithm = "SM3"
	}
	md, err := hashEvpMdFromString(prefix + strings.TrimPrefix(algorithm, "HMACSHA"))
	if err != nil {
		log.Fatalf("Failed to get hash: %v", err)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runHmacTest(md, wtg.TagSize/8, wt) {
			success = false
		}
	}
	return success
}

func runKWTestWrap(keySize int, key []byte, keyLen int, msg []byte, msgLen int, ct []byte, ctLen int, wt *wycheproofTestKW) bool {
	var aesKey C.AES_KEY

	ret := C.AES_set_encrypt_key((*C.uchar)(unsafe.Pointer(&key[0])), (C.int)(keySize), (*C.AES_KEY)(unsafe.Pointer(&aesKey)))
	if ret != 0 {
		fmt.Printf("FAIL: %s - AES_set_encrypt_key() = %d.\n", wt, int(ret))
		return false
	}

	outLen := msgLen
	out := make([]byte, outLen)
	copy(out, msg)
	out = append(out, make([]byte, 8)...)
	ret = C.AES_wrap_key((*C.AES_KEY)(unsafe.Pointer(&aesKey)), nil, (*C.uchar)(unsafe.Pointer(&out[0])), (*C.uchar)(unsafe.Pointer(&out[0])), (C.uint)(msgLen))
	success := false
	if ret == C.int(len(out)) && bytes.Equal(out, ct) {
		if wt.Result != "invalid" {
			success = true
		}
	} else if wt.Result != "valid" {
		success = true
	}
	if !success {
		fmt.Printf("FAIL: %s - msgLen = %d, AES_wrap_key() = %d.\n", wt, msgLen, int(ret))
	}
	return success
}

func runKWTestUnWrap(keySize int, key []byte, keyLen int, msg []byte, msgLen int, ct []byte, ctLen int, wt *wycheproofTestKW) bool {
	var aesKey C.AES_KEY

	ret := C.AES_set_decrypt_key((*C.uchar)(unsafe.Pointer(&key[0])), (C.int)(keySize), (*C.AES_KEY)(unsafe.Pointer(&aesKey)))
	if ret != 0 {
		fmt.Printf("FAIL: %s - AES_set_encrypt_key() = %d.\n", wt, int(ret))
		return false
	}

	out := make([]byte, ctLen)
	copy(out, ct)
	if ctLen == 0 {
		out = append(out, 0)
	}
	ret = C.AES_unwrap_key((*C.AES_KEY)(unsafe.Pointer(&aesKey)), nil, (*C.uchar)(unsafe.Pointer(&out[0])), (*C.uchar)(unsafe.Pointer(&out[0])), (C.uint)(ctLen))
	success := false
	if ret == C.int(ctLen-8) && bytes.Equal(out[0:ret], msg[0:ret]) {
		if wt.Result != "invalid" {
			success = true
		}
	} else if wt.Result != "valid" {
		success = true
	}
	if !success {
		fmt.Printf("FAIL: %s - keyLen = %d, AES_unwrap_key() = %d.\n", wt, keyLen, int(ret))
	}
	return success
}

func runKWTest(keySize int, wt *wycheproofTestKW) bool {
	key, keyLen := mustDecodeHexString(wt.Key, "key")
	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")
	ct, ctLen := mustDecodeHexString(wt.CT, "CT")

	wrapSuccess := runKWTestWrap(keySize, key, keyLen, msg, msgLen, ct, ctLen, wt)
	unwrapSuccess := runKWTestUnWrap(keySize, key, keyLen, msg, msgLen, ct, ctLen, wt)

	return wrapSuccess && unwrapSuccess
}

func (wtg *wycheproofTestGroupKW) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with key size %d...\n", algorithm, wtg.Type, wtg.KeySize)

	success := true
	for _, wt := range wtg.Tests {
		if !runKWTest(wtg.KeySize, wt) {
			success = false
		}
	}
	return success
}

func runMLKEMTestGroup(rank C.int, wt *wycheproofTestMLKEM) bool {
	privKey := C.MLKEM_private_key_new(rank)
	defer C.MLKEM_private_key_free(privKey)
	if privKey == nil {
		log.Fatal("MLKEM_private_key_new failed")
	}
	pubKey := C.MLKEM_public_key_new(rank)
	defer C.MLKEM_public_key_free(pubKey)
	if pubKey == nil {
		log.Fatal("MLKEM_public_key_new failed")
	}

	seed, seedLen := mustDecodeHexString(wt.Seed, "seed")
	ek, _ := mustDecodeHexString(wt.Ek, "ek")

	if C.MLKEM_private_key_from_seed(privKey, (*C.uchar)(unsafe.Pointer(&seed[0])), C.size_t(seedLen)) != 1 {
		fmt.Printf("%s - MLKEM_private_key_from_seed failed\n", wt)
		return false
	}

	if C.MLKEM_public_from_private(privKey, pubKey) != 1 {
		fmt.Printf("%s - MLKEM_public_from_private failed\n", wt)
		return false
	}

	var marshalledPubKey *C.uchar
	var marshalledPubKeyLen C.size_t
	defer C.free(unsafe.Pointer(marshalledPubKey))
	if C.MLKEM_marshal_public_key(pubKey, (**C.uchar)(unsafe.Pointer(&marshalledPubKey)), (*C.size_t)(unsafe.Pointer(&marshalledPubKeyLen))) != 1 {
		fmt.Printf("%s - MLKEM_marshal_private_key failed\n", wt)
		return false
	}
	gotEk := unsafe.Slice((*byte)(unsafe.Pointer(marshalledPubKey)), marshalledPubKeyLen)

	if !bytes.Equal(ek, gotEk) {
		fmt.Printf("FAIL: %s marshalledPubKey mismatch\n", wt)
		return false
	}

	c, cLen := mustDecodeHexString(wt.C, "c")

	var sharedSecret *C.uchar
	var sharedSecretLen C.size_t
	defer C.free(unsafe.Pointer(sharedSecret))
	if C.MLKEM_decap(privKey, (*C.uchar)(unsafe.Pointer(&c[0])), C.size_t(cLen), (**C.uchar)(unsafe.Pointer(&sharedSecret)), (*C.size_t)(unsafe.Pointer(&sharedSecretLen))) != 1 {
		fmt.Printf("%s - MLKEM_decap failed\n", wt)
		return false
	}
	gotK := unsafe.Slice((*byte)(unsafe.Pointer(sharedSecret)), sharedSecretLen)

	k, _ := mustDecodeHexString(wt.K, "K")

	if !bytes.Equal(k, gotK) {
		fmt.Printf("FAIL: %s sharedSecret mismatch\n", wt)
		return false
	}

	return true
}

func runMLKEMEncapsTestGroup(rank C.int, wt *wycheproofTestMLKEM) bool {
	pubKey := C.MLKEM_public_key_new(rank)
	defer C.MLKEM_public_key_free(pubKey)
	if pubKey == nil {
		log.Fatal("MLKEM_public_key_new failed")
	}

	ek, ekLen := mustDecodeHexString(wt.C, "eK")

	if C.MLKEM_parse_public_key(pubKey, (*C.uchar)(unsafe.Pointer(&ek[0])), (C.size_t)(ekLen)) != 0 || wt.Result != "invalid" {
		fmt.Printf("FAIL: %s MLKEM_parse_public_key succeeded\n", wt)
		return false
	}

	return true
}

func (wtg *wycheproofTestGroupMLKEM) run(algorithm string, variant testVariant) bool {
	var rank C.int

	switch wtg.ParameterSet {
	case "ML-KEM-512":
		fmt.Printf("INFO: skipping %v test group of type %v for %s\n", algorithm, wtg.Type, wtg.ParameterSet)
		return true
	case "ML-KEM-768":
		rank = C.RANK768
	case "ML-KEM-1024":
		rank = C.RANK1024
	default:
		log.Fatalf("Unknown ML-KEM parameterSet %v", wtg.ParameterSet)
	}
	fmt.Printf("Running %v test group of type %v\n", algorithm, wtg.Type)

	type MLKEMTestFunc func(C.int, *wycheproofTestMLKEM) bool
	var runTest MLKEMTestFunc

	switch wtg.Type {
	case "MLKEMTest":
		runTest = runMLKEMTestGroup
	case "MLKEMEncapsTest":
		runTest = runMLKEMEncapsTestGroup
	default:
		log.Fatalf("Unknown ML-KEM test type %v", wtg.Type)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runTest(rank, wt) {
			success = false
		}
	}
	return success
}

func runPbkdfTest(md *C.EVP_MD, wt *wycheproofTestPbkdf) bool {
	pw, pwLen := mustDecodeHexString(wt.Password, "password")
	salt, saltLen := mustDecodeHexString(wt.Salt, "salt")
	dk, _ := mustDecodeHexString(wt.Dk, "dk")

	out := make([]byte, wt.DkLen)

	ret := C.PKCS5_PBKDF2_HMAC((*C.char)(unsafe.Pointer(&pw[0])), C.int(pwLen), (*C.uchar)(unsafe.Pointer(&salt[0])), C.int(saltLen), C.int(wt.IterationCount), md, C.int(wt.DkLen), (*C.uchar)(unsafe.Pointer(&out[0])))

	success := true
	if ret != 1 || !bytes.Equal(dk, out) || wt.Result != "valid" {
		fmt.Printf("%s - %d\n", wt, int(ret))
		success = false
	}

	return success
}

func (wtg *wycheproofTestGroupPbkdf2HmacSha) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group of type %v...\n", algorithm, wtg.Type)

	md, err := hashEvpMdFromString("SHA-" + strings.TrimPrefix(algorithm, "PBKDF2-HMACSHA"))
	if err != nil {
		log.Fatalf("Failed to get hash: %v", err)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runPbkdfTest(md, wt) {
			success = false
		}
	}
	return success
}

func runPrimalityTest(wt *wycheproofTestPrimality) bool {
	bnValue := mustConvertBigIntToBigNum(wt.Value)
	defer C.BN_free(bnValue)

	ret := C.BN_is_prime_ex(bnValue, C.BN_prime_checks, (*C.BN_CTX)(unsafe.Pointer(nil)), (*C.BN_GENCB)(unsafe.Pointer(nil)))
	success := wt.Result == "acceptable" || (ret == 0 && wt.Result == "invalid") || (ret == 1 && wt.Result == "valid")
	if !success {
		fmt.Printf("FAIL: %s - got %d.\n", wt, ret)
	}
	return success
}

func (wtg *wycheproofTestGroupPrimality) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group...\n", algorithm)

	success := true
	for _, wt := range wtg.Tests {
		if !runPrimalityTest(wt) {
			success = false
		}
	}
	return success
}

func runRsaesOaepTest(rsa *C.RSA, sha *C.EVP_MD, mgfSha *C.EVP_MD, wt *wycheproofTestRsaes) bool {
	ct, ctLen := mustDecodeHexString(wt.CT, "CT")

	rsaSize := C.RSA_size(rsa)
	decrypted := make([]byte, rsaSize)

	success := true

	ret := C.RSA_private_decrypt(C.int(ctLen), (*C.uchar)(unsafe.Pointer(&ct[0])), (*C.uchar)(unsafe.Pointer(&decrypted[0])), rsa, C.RSA_NO_PADDING)

	if ret != rsaSize {
		success = (wt.Result == "invalid")

		if !success {
			fmt.Printf("FAIL: %s - RSA size got %d, want %d.\n", wt, ret, rsaSize)
		}
		return success
	}

	label, labelLen := mustDecodeHexString(wt.Label, "label")
	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")

	to := make([]byte, rsaSize)

	ret = C.RSA_padding_check_PKCS1_OAEP_mgf1((*C.uchar)(unsafe.Pointer(&to[0])), C.int(rsaSize), (*C.uchar)(unsafe.Pointer(&decrypted[0])), C.int(rsaSize), C.int(rsaSize), (*C.uchar)(unsafe.Pointer(&label[0])), C.int(labelLen), sha, mgfSha)

	if int(ret) != msgLen {
		success = (wt.Result == "invalid")

		if !success {
			fmt.Printf("FAIL: %s - got %d, want %d.\n", wt, ret, msgLen)
		}
		return success
	}

	to = to[:msgLen]
	if !bytes.Equal(msg[:msgLen], to) {
		success = false
		fmt.Printf("FAIL: %s - expected and calculated message differ.\n", wt)
	}

	return success
}

func (wtg *wycheproofTestGroupRsaesOaep) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with key size %d MGF %v and %v...\n", algorithm, wtg.Type, wtg.KeySize, wtg.MGFSHA, wtg.SHA)

	if skipHash(wtg.SHA) {
		return true
	}

	rsa := C.RSA_new()
	if rsa == nil {
		log.Fatal("RSA_new failed")
	}
	defer C.RSA_free(rsa)

	d := C.CString(wtg.PrivateKey.PrivateExponent)
	var rsaD *C.BIGNUM
	defer C.BN_free(rsaD)
	if C.BN_hex2bn(&rsaD, d) == 0 {
		log.Fatal("Failed to set RSA d")
	}
	C.free(unsafe.Pointer(d))

	e := C.CString(wtg.PrivateKey.PublicExponent)
	var rsaE *C.BIGNUM
	defer C.BN_free(rsaE)
	if C.BN_hex2bn(&rsaE, e) == 0 {
		log.Fatal("Failed to set RSA e")
	}
	C.free(unsafe.Pointer(e))

	n := C.CString(wtg.PrivateKey.Modulus)
	var rsaN *C.BIGNUM
	defer C.BN_free(rsaN)
	if C.BN_hex2bn(&rsaN, n) == 0 {
		log.Fatal("Failed to set RSA n")
	}
	C.free(unsafe.Pointer(n))

	if C.RSA_set0_key(rsa, rsaN, rsaE, rsaD) == 0 {
		log.Fatal("RSA_set0_key failed")
	}
	rsaN = nil
	rsaE = nil
	rsaD = nil

	sha, err := hashEvpMdFromString(wtg.SHA)
	if err != nil {
		log.Fatalf("Failed to get hash: %v", err)
	}

	mgfSha, err := hashEvpMdFromString(wtg.MGFSHA)
	if err != nil {
		log.Fatalf("Failed to get MGF hash: %v", err)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runRsaesOaepTest(rsa, sha, mgfSha, wt) {
			success = false
		}
	}
	return success
}

func runRsaesPkcs1Test(rsa *C.RSA, wt *wycheproofTestRsaes) bool {
	ct, ctLen := mustDecodeHexString(wt.CT, "CT")

	rsaSize := C.RSA_size(rsa)
	decrypted := make([]byte, rsaSize)

	success := true

	ret := C.RSA_private_decrypt(C.int(ctLen), (*C.uchar)(unsafe.Pointer(&ct[0])), (*C.uchar)(unsafe.Pointer(&decrypted[0])), rsa, C.RSA_PKCS1_PADDING)

	if ret == -1 {
		success = (wt.Result == "invalid")

		if !success {
			fmt.Printf("FAIL: %s - got %d, want %d.\n", wt, ret, len(wt.Msg)/2)
		}
		return success
	}

	msg, msgLen := mustDecodeHexString(wt.Msg, "msg")

	if int(ret) != msgLen {
		success = false
		fmt.Printf("FAIL: %s - got %d, want %d.\n", wt, ret, msgLen)
	} else if !bytes.Equal(msg[:msgLen], decrypted[:msgLen]) {
		success = false
		fmt.Printf("FAIL: %s - expected and calculated message differ.\n", wt)
	}

	return success
}

func (wtg *wycheproofTestGroupRsaesPkcs1) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with key size %d...\n", algorithm, wtg.Type, wtg.KeySize)
	rsa := C.RSA_new()
	if rsa == nil {
		log.Fatal("RSA_new failed")
	}
	defer C.RSA_free(rsa)

	d := C.CString(wtg.PrivateKey.PrivateExponent)
	var rsaD *C.BIGNUM
	defer C.BN_free(rsaD)
	if C.BN_hex2bn(&rsaD, d) == 0 {
		log.Fatal("Failed to set RSA d")
	}
	C.free(unsafe.Pointer(d))

	e := C.CString(wtg.PrivateKey.PublicExponent)
	var rsaE *C.BIGNUM
	defer C.BN_free(rsaE)
	if C.BN_hex2bn(&rsaE, e) == 0 {
		log.Fatal("Failed to set RSA e")
	}
	C.free(unsafe.Pointer(e))

	n := C.CString(wtg.PrivateKey.Modulus)
	var rsaN *C.BIGNUM
	defer C.BN_free(rsaN)
	if C.BN_hex2bn(&rsaN, n) == 0 {
		log.Fatal("Failed to set RSA n")
	}
	C.free(unsafe.Pointer(n))

	if C.RSA_set0_key(rsa, rsaN, rsaE, rsaD) == 0 {
		log.Fatal("RSA_set0_key failed")
	}
	rsaN = nil
	rsaE = nil
	rsaD = nil

	success := true
	for _, wt := range wtg.Tests {
		if !runRsaesPkcs1Test(rsa, wt) {
			success = false
		}
	}
	return success
}

func runRsassaTest(rsa *C.RSA, sha *C.EVP_MD, mgfSha *C.EVP_MD, sLen int, wt *wycheproofTestRsassa) bool {
	msg, _ := mustHashHexMessage(sha, wt.Msg)
	sig, sigLen := mustDecodeHexString(wt.Sig, "sig")

	sigOut := make([]byte, C.RSA_size(rsa)-11)
	if sigLen == 0 {
		sigOut = append(sigOut, 0)
	}

	ret := C.RSA_public_decrypt(C.int(sigLen), (*C.uchar)(unsafe.Pointer(&sig[0])), (*C.uchar)(unsafe.Pointer(&sigOut[0])), rsa, C.RSA_NO_PADDING)
	if ret == -1 {
		if wt.Result == "invalid" {
			return true
		}
		fmt.Printf("FAIL: %s - RSA_public_decrypt() = %d.\n", wt, int(ret))
		return false
	}

	ret = C.RSA_verify_PKCS1_PSS_mgf1(rsa, (*C.uchar)(unsafe.Pointer(&msg[0])), sha, mgfSha, (*C.uchar)(unsafe.Pointer(&sigOut[0])), C.int(sLen))

	success := false
	if ret == 1 && (wt.Result == "valid" || wt.Result == "acceptable") {
		// All acceptable cases that pass use SHA-1 and are flagged:
		// "WeakHash" : "The key for this test vector uses a weak hash function."
		success = true
	} else if ret == 0 && (wt.Result == "invalid" || wt.Result == "acceptable") {
		success = true
	} else {
		fmt.Printf("FAIL: %s - RSA_verify_PKCS1_PSS_mgf1() = %d.\n", wt, int(ret))
	}
	return success
}

func (wtg *wycheproofTestGroupRsassa) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with key size %d and %v...\n", algorithm, wtg.Type, wtg.KeySize, wtg.SHA)

	if skipHash(wtg.SHA) {
		return true
	}

	rsa := C.RSA_new()
	if rsa == nil {
		log.Fatal("RSA_new failed")
	}
	defer C.RSA_free(rsa)

	var publicExponent, modulus string
	if wtg.PublicKey != nil {
		publicExponent = wtg.PublicKey.PublicExponent
		modulus = wtg.PublicKey.Modulus
	} else if wtg.PrivateKey != nil {
		publicExponent = wtg.PrivateKey.PublicExponent
		modulus = wtg.PrivateKey.Modulus
	}
	if publicExponent == "" || modulus == "" {
		return true
	}

	e := C.CString(publicExponent)
	var rsaE *C.BIGNUM
	defer C.BN_free(rsaE)
	if C.BN_hex2bn(&rsaE, e) == 0 {
		log.Fatal("Failed to set RSA e")
	}
	C.free(unsafe.Pointer(e))

	n := C.CString(modulus)
	var rsaN *C.BIGNUM
	defer C.BN_free(rsaN)
	if C.BN_hex2bn(&rsaN, n) == 0 {
		log.Fatal("Failed to set RSA n")
	}
	C.free(unsafe.Pointer(n))

	if C.RSA_set0_key(rsa, rsaN, rsaE, nil) == 0 {
		log.Fatal("RSA_set0_key failed")
	}
	rsaN = nil
	rsaE = nil

	sha, err := hashEvpMdFromString(wtg.SHA)
	if err != nil {
		log.Fatalf("Failed to get hash: %v", err)
	}

	mgfSha, err := hashEvpMdFromString(wtg.MGFSHA)
	if err != nil {
		log.Fatalf("Failed to get MGF hash: %v", err)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runRsassaTest(rsa, sha, mgfSha, wtg.SLen, wt) {
			success = false
		}
	}
	return success
}

func runRSATest(rsa *C.RSA, md *C.EVP_MD, nid int, wt *wycheproofTestRSA) bool {
	msg, msgLen := mustHashHexMessage(md, wt.Msg)
	sig, sigLen := mustDecodeHexString(wt.Sig, "sig")

	ret := C.RSA_verify(C.int(nid), (*C.uchar)(unsafe.Pointer(&msg[0])), C.uint(msgLen), (*C.uchar)(unsafe.Pointer(&sig[0])), C.uint(sigLen), rsa)

	// XXX audit acceptable cases...
	success := true
	if ret == 1 != (wt.Result == "valid") && wt.Result != "acceptable" {
		fmt.Printf("FAIL: %s - RSA_verify() = %d.\n", wt, int(ret))
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupRSA) run(algorithm string, variant testVariant) bool {
	fmt.Printf("Running %v test group %v with key size %d and %v...\n", algorithm, wtg.Type, wtg.KeySize, wtg.SHA)

	rsa := C.RSA_new()
	if rsa == nil {
		log.Fatal("RSA_new failed")
	}
	defer C.RSA_free(rsa)

	var publicExponent, modulus string
	if wtg.PublicKey != nil {
		publicExponent = wtg.PublicKey.PublicExponent
		modulus = wtg.PublicKey.Modulus
	} else if wtg.PrivateKey != nil {
		publicExponent = wtg.PrivateKey.PublicExponent
		modulus = wtg.PrivateKey.Modulus
	}
	if publicExponent == "" || modulus == "" {
		return true
	}

	e := C.CString(publicExponent)
	var rsaE *C.BIGNUM
	defer C.BN_free(rsaE)
	if C.BN_hex2bn(&rsaE, e) == 0 {
		log.Fatal("Failed to set RSA e")
	}
	C.free(unsafe.Pointer(e))

	n := C.CString(modulus)
	var rsaN *C.BIGNUM
	defer C.BN_free(rsaN)
	if C.BN_hex2bn(&rsaN, n) == 0 {
		log.Fatal("Failed to set RSA n")
	}
	C.free(unsafe.Pointer(n))

	if C.RSA_set0_key(rsa, rsaN, rsaE, nil) == 0 {
		log.Fatal("RSA_set0_key failed")
	}
	rsaN = nil
	rsaE = nil

	nid, err := nidFromString(wtg.SHA)
	if err != nil {
		log.Fatalf("Failed to get MD NID: %v", err)
	}
	md, err := hashEvpMdFromString(wtg.SHA)
	if err != nil {
		log.Fatalf("Failed to get hash: %v", err)
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runRSATest(rsa, md, nid, wt) {
			success = false
		}
	}
	return success
}

func runX25519Test(wt *wycheproofTestX25519) bool {
	public, _ := mustDecodeHexString(wt.Public, "public")
	private, _ := mustDecodeHexString(wt.Private, "private")
	shared, _ := mustDecodeHexString(wt.Shared, "shared")

	got := make([]byte, C.X25519_KEY_LENGTH)
	result := true

	if C.X25519((*C.uint8_t)(unsafe.Pointer(&got[0])), (*C.uint8_t)(unsafe.Pointer(&private[0])), (*C.uint8_t)(unsafe.Pointer(&public[0]))) != 1 {
		result = false
	} else {
		result = bytes.Equal(got, shared)
	}

	// XXX audit acceptable cases...
	success := true
	if result != (wt.Result == "valid") && wt.Result != "acceptable" {
		fmt.Printf("FAIL: %s - X25519().\n", wt)
		success = false
	}
	return success
}

func (wtg *wycheproofTestGroupX25519) run(algorithm string, variant testVariant) bool {
	if wtg.Curve == "curve25519" {
		fmt.Printf("Running %v test group with curve %v...\n", algorithm, wtg.Curve)
	} else {
		fmt.Printf("INFO: Skipping %v test group with curve %v...\n", algorithm, wtg.Curve)
		return true
	}

	success := true
	for _, wt := range wtg.Tests {
		if !runX25519Test(wt) {
			success = false
		}
	}
	return success
}

func testGroupFromTestVector(wtv *wycheproofTestVectorsV1) (wycheproofTestGroupRunner, testVariant) {
	variant := Normal

	switch wtv.Algorithm {
	case "A128CBC-HS256", "A192CBC-HS384", "A256CBC-HS512":
		return nil, Skip
	case "AEGIS128", "AEGIS128L", "AEGIS256":
		return nil, Skip
	case "AEAD-AES-SIV-CMAC":
		return nil, Skip
	case "AES-CBC-PKCS5":
		return &wycheproofTestGroupAesCbcPkcs5{}, variant
	case "AES-CCM", "AES-GCM":
		return &wycheproofTestGroupAesAead{}, variant
	case "AES-CMAC":
		return &wycheproofTestGroupAesCmac{}, variant
	case "AES-EAX", "AES-FF1", "AES-GCM-SIV", "AES-GMAC", "AES-KWP", "AES-SIV-CMAC", "AES-XTS":
		return nil, Skip
	case "AES-WRAP":
		return &wycheproofTestGroupKW{}, variant
	case "ARIA-CBC-PKCS5", "ARIA-CCM", "ARIA-CMAC", "ARIA-GCM", "ARIA-KWP", "ARIA-WRAP":
		return nil, Skip
	case "ASCON128", "ASCON128A", "ASCON80PQ":
		return nil, Skip
	case "CAMELLIA-CBC-PKCS5", "CAMELLIA-CCM", "CAMELLIA-CMAC", "CAMELLIA-WRAP":
		return nil, Skip
	case "CHACHA20-POLY1305", "XCHACHA20-POLY1305":
		return &wycheproofTestGroupChaCha{}, variant
	case "DSA":
		if wtv.Schema == "dsa_p1363_verify_schema_v1.json" {
			variant = P1363
		}
		return &wycheproofTestGroupDSA{}, variant
	case "EcCurveTest":
		return &wycheproofTestGroupEcCurve{}, variant
	case "ECDH":
		if wtv.Schema == "ecdh_webcrypto_test_schema_v1.json" {
			return &wycheproofTestGroupECDHWebCrypto{}, Webcrypto
		}
		if wtv.Schema == "ecdh_ecpoint_test_schema_v1.json" {
			variant = EcPoint
		}
		if wtv.Schema == "ecdh_pem_test_schema_v1.json" {
			variant = Skip
		}
		return &wycheproofTestGroupECDH{}, variant
	case "ECDSA":
		if wtv.Schema == "ecdsa_bitcoin_verify_schema.json" {
			variant = Skip
		}
		if wtv.Schema == "ecdsa_p1363_verify_schema_v1.json" {
			variant = P1363
		}
		return &wycheproofTestGroupECDSA{}, variant
	case "EDDSA":
		return &wycheproofTestGroupEdDSA{}, variant
	case "HKDF-SHA-1", "HKDF-SHA-256", "HKDF-SHA-384", "HKDF-SHA-512":
		return &wycheproofTestGroupHkdf{}, variant
	case "HMACSHA1", "HMACSHA224", "HMACSHA256", "HMACSHA384", "HMACSHA512", "HMACSHA512/224", "HMACSHA512/256", "HMACSHA3-224", "HMACSHA3-256", "HMACSHA3-384", "HMACSHA3-512", "HMACSM3":
		return &wycheproofTestGroupHmac{}, variant
	case "KMAC128", "KMAC256":
		return nil, Skip
	case "ML-DSA-44", "ML-DSA-65", "ML-DSA-87":
		return nil, Skip
	case "ML-KEM":
		return &wycheproofTestGroupMLKEM{}, Normal
	case "MORUS640", "MORUS1280":
		return nil, Skip
	case "PbeWithHmacSha1AndAes_128", "PbeWithHmacSha1AndAes_192", "PbeWithHmacSha1AndAes_256", "PbeWithHmacSha224AndAes_128", "PbeWithHmacSha224AndAes_192", "PbeWithHmacSha224AndAes_256", "PbeWithHmacSha256AndAes_128", "PbeWithHmacSha256AndAes_192", "PbeWithHmacSha256AndAes_256", "PbeWithHmacSha384AndAes_128", "PbeWithHmacSha384AndAes_192", "PbeWithHmacSha384AndAes_256", "PbeWithHmacSha512AndAes_128", "PbeWithHmacSha512AndAes_192", "PbeWithHmacSha512AndAes_256":
		return nil, Skip
	case "PBKDF2-HMACSHA1", "PBKDF2-HMACSHA224", "PBKDF2-HMACSHA256", "PBKDF2-HMACSHA384", "PBKDF2-HMACSHA512":
		return &wycheproofTestGroupPbkdf2HmacSha{}, Skip
	case "PrimalityTest":
		return &wycheproofTestGroupPrimality{}, variant
	case "RSAES-OAEP":
		return &wycheproofTestGroupRsaesOaep{}, variant
	case "RSAES-PKCS1-v1_5":
		return &wycheproofTestGroupRsaesPkcs1{}, variant
	case "RSASSA-PSS":
		return &wycheproofTestGroupRsassa{}, variant
	case "RSASSA-PKCS1-v1_5", "RSASig":
		return &wycheproofTestGroupRSA{}, variant
	case "SEED-CCM", "SEED-GCM", "SEED-WRAP":
		return nil, Skip
	case "SipHash-1-3", "SipHash-2-4", "SipHash-4-8", "SipHashX-2-4", "SipHashX-4-8":
		return nil, Skip
	case "SM4-CCM", "SM4-GCM":
		return nil, Skip
	case "VMAC-AES":
		return nil, Skip
	case "XDH":
		switch wtv.Schema {
		case "xdh_asn_comp_schema_v1.json", "xdh_jwk_comp_schema_v1.json", "xdh_pem_comp_schema_v1.json":
			variant = Skip
		case "xdh_comp_schema_v1.json":
			variant = Normal
		}
		return &wycheproofTestGroupX25519{}, variant
	default:
		// XXX - JOSE tests don't set an Algorithm...
		if strings.HasPrefix(wtv.Schema, "json_web_") {
			return nil, Skip
		}
		return nil, Normal
	}
}

func runTestVectors(path string) bool {
	b, err := ioutil.ReadFile(path)
	if err != nil {
		log.Fatalf("Failed to read test vectors: %v", err)
	}
	wtv := &wycheproofTestVectorsV1{}
	if err := json.Unmarshal(b, wtv); err != nil {
		log.Fatalf("Failed to unmarshal JSON: %v", err)
	}
	fmt.Printf("Loaded Wycheproof test vectors for %v with %d tests from %q\n", wtv.Algorithm, wtv.NumberOfTests, filepath.Base(path))

	success := true
	for _, tg := range wtv.TestGroups {
		wtg, variant := testGroupFromTestVector(wtv)
		if variant == Skip {
			fmt.Printf("INFO: Skipping tests from \"%s\"\n", filepath.Base(path))
			return true
		}
		if wtg == nil {
			log.Fatalf("INFO: Unknown test vector algorithm %qin \"%s\"", wtv.Algorithm, filepath.Base(path))
		}
		if err := json.Unmarshal(tg, wtg); err != nil {
			log.Fatalf("Failed to unmarshal test groups JSON: %v", err)
		}
		testc.runTest(func() bool {
			return wtg.run(wtv.Algorithm, variant)
		})
	}
	for _ = range wtv.TestGroups {
		result := <-testc.resultCh
		if !result {
			success = false
		}
	}
	return success
}

type testCoordinator struct {
	testFuncCh chan func() bool
	resultCh   chan bool
}

func newTestCoordinator() *testCoordinator {
	runnerCount := runtime.NumCPU()
	tc := &testCoordinator{
		testFuncCh: make(chan func() bool, runnerCount),
		resultCh:   make(chan bool, 1024),
	}
	for i := 0; i < runnerCount; i++ {
		go tc.testRunner(tc.testFuncCh, tc.resultCh)
	}
	return tc
}

func (tc *testCoordinator) testRunner(testFuncCh <-chan func() bool, resultCh chan<- bool) {
	for testFunc := range testFuncCh {
		select {
		case resultCh <- testFunc():
		default:
			log.Fatal("result channel is full")
		}
	}
}

func (tc *testCoordinator) runTest(testFunc func() bool) {
	tc.testFuncCh <- testFunc
}

func (tc *testCoordinator) shutdown() {
	close(tc.testFuncCh)
}

func main() {
	path := testVectorPath
	if _, err := os.Stat(path); os.IsNotExist(err) {
		fmt.Printf("package wycheproof-testvectors is required for this regress\n")
		fmt.Printf("SKIPPED\n")
		os.Exit(0)
	}

	success := true

	var wg sync.WaitGroup

	vectorsRateLimitCh := make(chan bool, 4)
	for i := 0; i < cap(vectorsRateLimitCh); i++ {
		vectorsRateLimitCh <- true
	}
	resultCh := make(chan bool, 1024)

	testc = newTestCoordinator()

	tvs, err := filepath.Glob(filepath.Join(path, "*.json"))
	if err != nil {
		log.Fatalf("Failed to glob test vectors: %v", err)
	}
	if len(tvs) == 0 {
		log.Fatalf("Failed to find test vectors at %q\n", path)
	}
	for _, tv := range tvs {
		wg.Add(1)
		<-vectorsRateLimitCh
		go func(tv string) {
			select {
			case resultCh <- runTestVectors(tv):
			default:
				log.Fatal("result channel is full")
			}
			vectorsRateLimitCh <- true
			wg.Done()
		}(tv)
	}

	wg.Wait()
	close(resultCh)

	for result := range resultCh {
		if !result {
			success = false
		}
	}

	testc.shutdown()

	C.OPENSSL_cleanup()

	if !success {
		os.Exit(1)
	}
}
