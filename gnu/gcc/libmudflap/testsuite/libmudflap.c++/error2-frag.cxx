// PR 26790
// { dg-do compile }

struct A;

A foo()  // { dg-error "incomplete" }
{
    A a; // { dg-error "incomplete" }
    return a;
}
