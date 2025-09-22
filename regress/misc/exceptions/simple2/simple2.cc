#include <iostream>


class ex {
};

void f(void) {
	throw ex();
}

int main() {
	try {
		f();
	}
	catch (const ex & myex) {
		std::cout << "Catching..." << std::endl;
		exit(0);
	}
	exit(1);
}
