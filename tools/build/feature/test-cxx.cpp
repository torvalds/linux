#include <iostream>
#include <memory>

static void print_str(std::string s)
{
	std::cout << s << std::endl;
}

int main()
{
	std::string s("Hello World!");
	print_str(std::move(s));
	std::cout << "|" << s << "|" << std::endl;
	return 0;
}
