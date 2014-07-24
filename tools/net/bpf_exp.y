/*
 * BPF asm code parser
 *
 * This program is free software; you can distribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Syntax kept close to:
 *
 * Steven McCanne and Van Jacobson. 1993. The BSD packet filter: a new
 * architecture for user-level packet capture. In Proceedings of the
 * USENIX Winter 1993 Conference Proceedings on USENIX Winter 1993
 * Conference Proceedings (USENIX'93). USENIX Association, Berkeley,
 * CA, USA, 2-2.
 *
 * Copyright 2013 Daniel Borkmann <borkmann@redhat.com>
 * Licensed under the GNU General Public License, version 2.0 (GPLv2)
 */

%{

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <linux/filter.h>

#include "bpf_exp.yacc.h"

enum jmp_type { JTL, JFL, JKL };

extern FILE *yyin;
extern int yylex(void);
extern void yyerror(const char *str);

extern void bpf_asm_compile(FILE *fp, bool cstyle);
static void bpf_set_curr_instr(uint16_t op, uint8_t jt, uint8_t jf, uint32_t k);
static void bpf_set_curr_label(char *label);
static void bpf_set_jmp_label(char *label, enum jmp_type type);

%}

%union {
	char *label;
	uint32_t number;
}

%token OP_LDB OP_LDH OP_LD OP_LDX OP_ST OP_STX OP_JMP OP_JEQ OP_JGT OP_JGE
%token OP_JSET OP_ADD OP_SUB OP_MUL OP_DIV OP_AND OP_OR OP_XOR OP_LSH OP_RSH
%token OP_RET OP_TAX OP_TXA OP_LDXB OP_MOD OP_NEG OP_JNEQ OP_JLT OP_JLE OP_LDI
%token OP_LDXI

%token K_PKT_LEN K_PROTO K_TYPE K_NLATTR K_NLATTR_NEST K_MARK K_QUEUE K_HATYPE
%token K_RXHASH K_CPU K_IFIDX K_VLANT K_VLANP K_POFF K_RAND

%token ':' ',' '[' ']' '(' ')' 'x' 'a' '+' 'M' '*' '&' '#' '%'

%token number label

%type <label> label
%type <number> number

%%

prog
	: line
	| prog line
	;

line
	: instr
	| labelled_instr
	;

labelled_instr
	: labelled instr
	;

instr
	: ldb
	| ldh
	| ld
	| ldi
	| ldx
	| ldxi
	| st
	| stx
	| jmp
	| jeq
	| jneq
	| jlt
	| jle
	| jgt
	| jge
	| jset
	| add
	| sub
	| mul
	| div
	| mod
	| neg
	| and
	| or
	| xor
	| lsh
	| rsh
	| ret
	| tax
	| txa
	;

labelled
	: label ':' { bpf_set_curr_label($1); }
	;

ldb
	: OP_LDB '[' 'x' '+' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_IND, 0, 0, $5); }
	| OP_LDB '[' '%' 'x' '+' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_IND, 0, 0, $6); }
	| OP_LDB '[' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0, $3); }
	| OP_LDB K_PROTO {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PROTOCOL); }
	| OP_LDB K_TYPE {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PKTTYPE); }
	| OP_LDB K_IFIDX {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_IFINDEX); }
	| OP_LDB K_NLATTR {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_NLATTR); }
	| OP_LDB K_NLATTR_NEST {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_NLATTR_NEST); }
	| OP_LDB K_MARK {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_MARK); }
	| OP_LDB K_QUEUE {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_QUEUE); }
	| OP_LDB K_HATYPE {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_HATYPE); }
	| OP_LDB K_RXHASH {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_RXHASH); }
	| OP_LDB K_CPU {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_CPU); }
	| OP_LDB K_VLANT {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_VLAN_TAG); }
	| OP_LDB K_VLANP {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT); }
	| OP_LDB K_POFF {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PAY_OFFSET); }
	| OP_LDB K_RAND {
		bpf_set_curr_instr(BPF_LD | BPF_B | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_RANDOM); }
	;

ldh
	: OP_LDH '[' 'x' '+' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_IND, 0, 0, $5); }
	| OP_LDH '[' '%' 'x' '+' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_IND, 0, 0, $6); }
	| OP_LDH '[' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0, $3); }
	| OP_LDH K_PROTO {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PROTOCOL); }
	| OP_LDH K_TYPE {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PKTTYPE); }
	| OP_LDH K_IFIDX {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_IFINDEX); }
	| OP_LDH K_NLATTR {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_NLATTR); }
	| OP_LDH K_NLATTR_NEST {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_NLATTR_NEST); }
	| OP_LDH K_MARK {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_MARK); }
	| OP_LDH K_QUEUE {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_QUEUE); }
	| OP_LDH K_HATYPE {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_HATYPE); }
	| OP_LDH K_RXHASH {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_RXHASH); }
	| OP_LDH K_CPU {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_CPU); }
	| OP_LDH K_VLANT {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_VLAN_TAG); }
	| OP_LDH K_VLANP {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT); }
	| OP_LDH K_POFF {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PAY_OFFSET); }
	| OP_LDH K_RAND {
		bpf_set_curr_instr(BPF_LD | BPF_H | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_RANDOM); }
	;

ldi
	: OP_LDI '#' number {
		bpf_set_curr_instr(BPF_LD | BPF_IMM, 0, 0, $3); }
	| OP_LDI number {
		bpf_set_curr_instr(BPF_LD | BPF_IMM, 0, 0, $2); }
	;

ld
	: OP_LD '#' number {
		bpf_set_curr_instr(BPF_LD | BPF_IMM, 0, 0, $3); }
	| OP_LD K_PKT_LEN {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_LEN, 0, 0, 0); }
	| OP_LD K_PROTO {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PROTOCOL); }
	| OP_LD K_TYPE {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PKTTYPE); }
	| OP_LD K_IFIDX {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_IFINDEX); }
	| OP_LD K_NLATTR {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_NLATTR); }
	| OP_LD K_NLATTR_NEST {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_NLATTR_NEST); }
	| OP_LD K_MARK {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_MARK); }
	| OP_LD K_QUEUE {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_QUEUE); }
	| OP_LD K_HATYPE {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_HATYPE); }
	| OP_LD K_RXHASH {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_RXHASH); }
	| OP_LD K_CPU {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_CPU); }
	| OP_LD K_VLANT {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_VLAN_TAG); }
	| OP_LD K_VLANP {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT); }
	| OP_LD K_POFF {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_PAY_OFFSET); }
	| OP_LD K_RAND {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0,
				   SKF_AD_OFF + SKF_AD_RANDOM); }
	| OP_LD 'M' '[' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_MEM, 0, 0, $4); }
	| OP_LD '[' 'x' '+' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_IND, 0, 0, $5); }
	| OP_LD '[' '%' 'x' '+' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_IND, 0, 0, $6); }
	| OP_LD '[' number ']' {
		bpf_set_curr_instr(BPF_LD | BPF_W | BPF_ABS, 0, 0, $3); }
	;

ldxi
	: OP_LDXI '#' number {
		bpf_set_curr_instr(BPF_LDX | BPF_IMM, 0, 0, $3); }
	| OP_LDXI number {
		bpf_set_curr_instr(BPF_LDX | BPF_IMM, 0, 0, $2); }
	;

ldx
	: OP_LDX '#' number {
		bpf_set_curr_instr(BPF_LDX | BPF_IMM, 0, 0, $3); }
	| OP_LDX K_PKT_LEN {
		bpf_set_curr_instr(BPF_LDX | BPF_W | BPF_LEN, 0, 0, 0); }
	| OP_LDX 'M' '[' number ']' {
		bpf_set_curr_instr(BPF_LDX | BPF_MEM, 0, 0, $4); }
	| OP_LDXB number '*' '(' '[' number ']' '&' number ')' {
		if ($2 != 4 || $9 != 0xf) {
			fprintf(stderr, "ldxb offset not supported!\n");
			exit(0);
		} else {
			bpf_set_curr_instr(BPF_LDX | BPF_MSH | BPF_B, 0, 0, $6); } }
	| OP_LDX number '*' '(' '[' number ']' '&' number ')' {
		if ($2 != 4 || $9 != 0xf) {
			fprintf(stderr, "ldxb offset not supported!\n");
			exit(0);
		} else {
			bpf_set_curr_instr(BPF_LDX | BPF_MSH | BPF_B, 0, 0, $6); } }
	;

st
	: OP_ST 'M' '[' number ']' {
		bpf_set_curr_instr(BPF_ST, 0, 0, $4); }
	;

stx
	: OP_STX 'M' '[' number ']' {
		bpf_set_curr_instr(BPF_STX, 0, 0, $4); }
	;

jmp
	: OP_JMP label {
		bpf_set_jmp_label($2, JKL);
		bpf_set_curr_instr(BPF_JMP | BPF_JA, 0, 0, 0); }
	;

jeq
	: OP_JEQ '#' number ',' label ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_jmp_label($7, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, $3); }
	| OP_JEQ 'x' ',' label ',' label {
		bpf_set_jmp_label($4, JTL);
		bpf_set_jmp_label($6, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 0); }
	| OP_JEQ '%' 'x' ',' label ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_jmp_label($7, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 0); }
	| OP_JEQ '#' number ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, $3); }
	| OP_JEQ 'x' ',' label {
		bpf_set_jmp_label($4, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 0); }
	| OP_JEQ '%' 'x' ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 0); }
	;

jneq
	: OP_JNEQ '#' number ',' label {
		bpf_set_jmp_label($5, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, $3); }
	| OP_JNEQ 'x' ',' label {
		bpf_set_jmp_label($4, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 0); }
	| OP_JNEQ '%' 'x' ',' label {
		bpf_set_jmp_label($5, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 0); }
	;

jlt
	: OP_JLT '#' number ',' label {
		bpf_set_jmp_label($5, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_K, 0, 0, $3); }
	| OP_JLT 'x' ',' label {
		bpf_set_jmp_label($4, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 0); }
	| OP_JLT '%' 'x' ',' label {
		bpf_set_jmp_label($5, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 0); }
	;

jle
	: OP_JLE '#' number ',' label {
		bpf_set_jmp_label($5, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_K, 0, 0, $3); }
	| OP_JLE 'x' ',' label {
		bpf_set_jmp_label($4, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_X, 0, 0, 0); }
	| OP_JLE '%' 'x' ',' label {
		bpf_set_jmp_label($5, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_X, 0, 0, 0); }
	;

jgt
	: OP_JGT '#' number ',' label ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_jmp_label($7, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_K, 0, 0, $3); }
	| OP_JGT 'x' ',' label ',' label {
		bpf_set_jmp_label($4, JTL);
		bpf_set_jmp_label($6, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_X, 0, 0, 0); }
	| OP_JGT '%' 'x' ',' label ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_jmp_label($7, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_X, 0, 0, 0); }
	| OP_JGT '#' number ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_K, 0, 0, $3); }
	| OP_JGT 'x' ',' label {
		bpf_set_jmp_label($4, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_X, 0, 0, 0); }
	| OP_JGT '%' 'x' ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGT | BPF_X, 0, 0, 0); }
	;

jge
	: OP_JGE '#' number ',' label ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_jmp_label($7, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_K, 0, 0, $3); }
	| OP_JGE 'x' ',' label ',' label {
		bpf_set_jmp_label($4, JTL);
		bpf_set_jmp_label($6, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 0); }
	| OP_JGE '%' 'x' ',' label ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_jmp_label($7, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 0); }
	| OP_JGE '#' number ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_K, 0, 0, $3); }
	| OP_JGE 'x' ',' label {
		bpf_set_jmp_label($4, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 0); }
	| OP_JGE '%' 'x' ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 0); }
	;

jset
	: OP_JSET '#' number ',' label ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_jmp_label($7, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JSET | BPF_K, 0, 0, $3); }
	| OP_JSET 'x' ',' label ',' label {
		bpf_set_jmp_label($4, JTL);
		bpf_set_jmp_label($6, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JSET | BPF_X, 0, 0, 0); }
	| OP_JSET '%' 'x' ',' label ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_jmp_label($7, JFL);
		bpf_set_curr_instr(BPF_JMP | BPF_JSET | BPF_X, 0, 0, 0); }
	| OP_JSET '#' number ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JSET | BPF_K, 0, 0, $3); }
	| OP_JSET 'x' ',' label {
		bpf_set_jmp_label($4, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JSET | BPF_X, 0, 0, 0); }
	| OP_JSET '%' 'x' ',' label {
		bpf_set_jmp_label($5, JTL);
		bpf_set_curr_instr(BPF_JMP | BPF_JSET | BPF_X, 0, 0, 0); }
	;

add
	: OP_ADD '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_ADD | BPF_K, 0, 0, $3); }
	| OP_ADD 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_ADD | BPF_X, 0, 0, 0); }
	| OP_ADD '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_ADD | BPF_X, 0, 0, 0); }
	;

sub
	: OP_SUB '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_SUB | BPF_K, 0, 0, $3); }
	| OP_SUB 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_SUB | BPF_X, 0, 0, 0); }
	| OP_SUB '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_SUB | BPF_X, 0, 0, 0); }
	;

mul
	: OP_MUL '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_MUL | BPF_K, 0, 0, $3); }
	| OP_MUL 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_MUL | BPF_X, 0, 0, 0); }
	| OP_MUL '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_MUL | BPF_X, 0, 0, 0); }
	;

div
	: OP_DIV '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_DIV | BPF_K, 0, 0, $3); }
	| OP_DIV 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_DIV | BPF_X, 0, 0, 0); }
	| OP_DIV '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_DIV | BPF_X, 0, 0, 0); }
	;

mod
	: OP_MOD '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_MOD | BPF_K, 0, 0, $3); }
	| OP_MOD 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_MOD | BPF_X, 0, 0, 0); }
	| OP_MOD '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_MOD | BPF_X, 0, 0, 0); }
	;

neg
	: OP_NEG {
		bpf_set_curr_instr(BPF_ALU | BPF_NEG, 0, 0, 0); }
	;

and
	: OP_AND '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_AND | BPF_K, 0, 0, $3); }
	| OP_AND 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_AND | BPF_X, 0, 0, 0); }
	| OP_AND '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_AND | BPF_X, 0, 0, 0); }
	;

or
	: OP_OR '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_OR | BPF_K, 0, 0, $3); }
	| OP_OR 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_OR | BPF_X, 0, 0, 0); }
	| OP_OR '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_OR | BPF_X, 0, 0, 0); }
	;

xor
	: OP_XOR '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_XOR | BPF_K, 0, 0, $3); }
	| OP_XOR 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_XOR | BPF_X, 0, 0, 0); }
	| OP_XOR '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_XOR | BPF_X, 0, 0, 0); }
	;

lsh
	: OP_LSH '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_LSH | BPF_K, 0, 0, $3); }
	| OP_LSH 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_LSH | BPF_X, 0, 0, 0); }
	| OP_LSH '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_LSH | BPF_X, 0, 0, 0); }
	;

rsh
	: OP_RSH '#' number {
		bpf_set_curr_instr(BPF_ALU | BPF_RSH | BPF_K, 0, 0, $3); }
	| OP_RSH 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_RSH | BPF_X, 0, 0, 0); }
	| OP_RSH '%' 'x' {
		bpf_set_curr_instr(BPF_ALU | BPF_RSH | BPF_X, 0, 0, 0); }
	;

ret
	: OP_RET 'a' {
		bpf_set_curr_instr(BPF_RET | BPF_A, 0, 0, 0); }
	| OP_RET '%' 'a' {
		bpf_set_curr_instr(BPF_RET | BPF_A, 0, 0, 0); }
	| OP_RET 'x' {
		bpf_set_curr_instr(BPF_RET | BPF_X, 0, 0, 0); }
	| OP_RET '%' 'x' {
		bpf_set_curr_instr(BPF_RET | BPF_X, 0, 0, 0); }
	| OP_RET '#' number {
		bpf_set_curr_instr(BPF_RET | BPF_K, 0, 0, $3); }
	;

tax
	: OP_TAX {
		bpf_set_curr_instr(BPF_MISC | BPF_TAX, 0, 0, 0); }
	;

txa
	: OP_TXA {
		bpf_set_curr_instr(BPF_MISC | BPF_TXA, 0, 0, 0); }
	;

%%

static int curr_instr = 0;
static struct sock_filter out[BPF_MAXINSNS];
static char **labels, **labels_jt, **labels_jf, **labels_k;

static void bpf_assert_max(void)
{
	if (curr_instr >= BPF_MAXINSNS) {
		fprintf(stderr, "only max %u insns allowed!\n", BPF_MAXINSNS);
		exit(0);
	}
}

static void bpf_set_curr_instr(uint16_t code, uint8_t jt, uint8_t jf,
			       uint32_t k)
{
	bpf_assert_max();
	out[curr_instr].code = code;
	out[curr_instr].jt = jt;
	out[curr_instr].jf = jf;
	out[curr_instr].k = k;
	curr_instr++;
}

static void bpf_set_curr_label(char *label)
{
	bpf_assert_max();
	labels[curr_instr] = label;
}

static void bpf_set_jmp_label(char *label, enum jmp_type type)
{
	bpf_assert_max();
	switch (type) {
	case JTL:
		labels_jt[curr_instr] = label;
		break;
	case JFL:
		labels_jf[curr_instr] = label;
		break;
	case JKL:
		labels_k[curr_instr] = label;
		break;
	}
}

static int bpf_find_insns_offset(const char *label)
{
	int i, max = curr_instr, ret = -ENOENT;

	for (i = 0; i < max; i++) {
		if (labels[i] && !strcmp(label, labels[i])) {
			ret = i;
			break;
		}
	}

	if (ret == -ENOENT) {
		fprintf(stderr, "no such label \'%s\'!\n", label);
		exit(0);
	}

	return ret;
}

static void bpf_stage_1_insert_insns(void)
{
	yyparse();
}

static void bpf_reduce_k_jumps(void)
{
	int i;

	for (i = 0; i < curr_instr; i++) {
		if (labels_k[i]) {
			int off = bpf_find_insns_offset(labels_k[i]);
			out[i].k = (uint32_t) (off - i - 1);
		}
	}
}

static void bpf_reduce_jt_jumps(void)
{
	int i;

	for (i = 0; i < curr_instr; i++) {
		if (labels_jt[i]) {
			int off = bpf_find_insns_offset(labels_jt[i]);
			out[i].jt = (uint8_t) (off - i -1);
		}
	}
}

static void bpf_reduce_jf_jumps(void)
{
	int i;

	for (i = 0; i < curr_instr; i++) {
		if (labels_jf[i]) {
			int off = bpf_find_insns_offset(labels_jf[i]);
			out[i].jf = (uint8_t) (off - i - 1);
		}
	}
}

static void bpf_stage_2_reduce_labels(void)
{
	bpf_reduce_k_jumps();
	bpf_reduce_jt_jumps();
	bpf_reduce_jf_jumps();
}

static void bpf_pretty_print_c(void)
{
	int i;

	for (i = 0; i < curr_instr; i++)
		printf("{ %#04x, %2u, %2u, %#010x },\n", out[i].code,
		       out[i].jt, out[i].jf, out[i].k);
}

static void bpf_pretty_print(void)
{
	int i;

	printf("%u,", curr_instr);
	for (i = 0; i < curr_instr; i++)
		printf("%u %u %u %u,", out[i].code,
		       out[i].jt, out[i].jf, out[i].k);
	printf("\n");
}

static void bpf_init(void)
{
	memset(out, 0, sizeof(out));

	labels = calloc(BPF_MAXINSNS, sizeof(*labels));
	assert(labels);
	labels_jt = calloc(BPF_MAXINSNS, sizeof(*labels_jt));
	assert(labels_jt);
	labels_jf = calloc(BPF_MAXINSNS, sizeof(*labels_jf));
	assert(labels_jf);
	labels_k = calloc(BPF_MAXINSNS, sizeof(*labels_k));
	assert(labels_k);
}

static void bpf_destroy_labels(void)
{
	int i;

	for (i = 0; i < curr_instr; i++) {
		free(labels_jf[i]);
		free(labels_jt[i]);
		free(labels_k[i]);
		free(labels[i]);
	}
}

static void bpf_destroy(void)
{
	bpf_destroy_labels();
	free(labels_jt);
	free(labels_jf);
	free(labels_k);
	free(labels);
}

void bpf_asm_compile(FILE *fp, bool cstyle)
{
	yyin = fp;

	bpf_init();
	bpf_stage_1_insert_insns();
	bpf_stage_2_reduce_labels();
	bpf_destroy();

	if (cstyle)
		bpf_pretty_print_c();
	else
		bpf_pretty_print();

	if (fp != stdin)
		fclose(yyin);
}

void yyerror(const char *str)
{
	exit(1);
}
