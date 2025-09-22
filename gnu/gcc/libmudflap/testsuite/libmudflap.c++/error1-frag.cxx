// PR 26789
// { dg-do compile }

struct A;
A a; // { dg-error "incomplete" }
