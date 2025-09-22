// See http://llvm.org/bugs/show_bug.cgi?id=11468
#include <stdio.h>
#include <string>

class Action {
 public:
  Action() {}
  void PrintString(const std::string& msg) const {
    fprintf(stderr, "%s\n", msg.c_str());
  }
  void Throw(const char& arg) const {
    PrintString("PrintString called!");  // this line is important
    throw arg;
  }
};

int main() {
  const Action a;
  fprintf(stderr, "&a before = %p\n", &a);
  try {
    a.Throw('c');
  } catch(const char&) {
    fprintf(stderr, "&a in catch = %p\n", &a);
  }
  fprintf(stderr, "&a final = %p\n", &a);
  return 0;
}
