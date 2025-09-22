void f(int *p);

void testUseMiddleArgAfterDelete(int *p) {
  delete p;
  f(p); // warn: use after free
}

class SomeClass {
public:
  void f();
};

void test() {
  SomeClass *c = new SomeClass;
  delete c;
  c->f(); // warn: use after free
}

void test() {
  int *p = (int *)__builtin_alloca(sizeof(int));
  delete p; // warn: deleting memory allocated by alloca
}

void test() {
  int *p = new int;
  delete p;
  delete p; // warn: attempt to free released
}

void test() {
  int i;
  delete &i; // warn: delete address of local
}

void test() {
  int *p = new int[1];
  delete[] (++p);
    // warn: argument to 'delete[]' is offset by 4 bytes
    // from the start of memory allocated by 'new[]'
}

