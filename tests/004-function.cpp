#include <cstdio>

int fibonacci(int n)
{
	if (n <= 2) return n;
	return fibonacci(n - 2) + fibonacci(n - 1);
}

int main()
{
	for (size_t i = 1; i < 10; ++i) {
		cout << "fibonacci(" << i << ") = " << fibonacci(i) << endl;
	}
	return 0;
}
