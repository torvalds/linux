// Synthetic benchmark for __tsan_func_entry/exit (spends ~75% there).

void foo(bool x);

int main() {
  volatile int kRepeat1 = 1 << 30;
  const int kRepeat = kRepeat1;
  for (int i = 0; i < kRepeat; i++)
    foo(false);
}

__attribute__((noinline)) void bar(volatile bool x) {
  if (x)
    foo(x);
}

__attribute__((noinline)) void foo(bool x) {
  if (__builtin_expect(x, false))
    bar(x);
}
