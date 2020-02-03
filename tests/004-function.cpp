#include <iostream>
using namespace std;

int add(int a, int b)
{
	return a + b;
}

int main()
{
	int a = 1, b = 2;
	cout << "add(a, b) = " << add(a, b) << endl;
	return 0;
}
