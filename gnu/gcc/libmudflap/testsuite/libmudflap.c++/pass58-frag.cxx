// PR 19319
struct k {
  int data;
  k(int j): data(j) {}
};
k make_k () { return k(1); }

int main ()
{
  k foo = make_k ();
  return 0;
}
