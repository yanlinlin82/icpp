#include <iostream>
using namespace std;

int main()
{
	int a = 1, b = 2, c = 3, d = 4, e = 5;
	int f = a * (b - c / (d + e));
	cout << "f = " << a << " * (" << b << " - " << c << " / (" << d << " + " << e << ")) = " << f << endl;
	return f;
}
