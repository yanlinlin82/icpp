#include <iostream>
using namespace std;

int fibonacci(int n)
{
	if (n <= 2) return 1;
	return fibonacci(n - 2) + fibonacci(n - 1);
}

int main()
{
	for (size_t i = 1; i < 20; ++i) {
		cout << "fibonacci(" << i << ") = " << fibonacci(i) << endl;
	}
	return 0;
}
