class foo {
  char z [10];
public:
  virtual char *get_z () { return & this->z[0]; }
};

class bar: public foo {
  char q [20];
public:
  char *get_z () { return & this->q[0]; }
};

int main () {
foo *x = new bar ();

x->get_z()[9] = 'a';

delete x;
return 0;
}
