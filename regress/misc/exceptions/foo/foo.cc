#include <iostream>

using namespace std;

int
main ()
{
	cout << "Throwing up" << endl;
	try {
		throw false;
	}
	catch (...) {
		cout << "Wew, that was close!" << endl;
		return 0;
	}
	cout << "Done" << endl;
	return 1;
}
