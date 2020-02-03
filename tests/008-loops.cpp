#include <iostream>
using namespace std;

int main()
{
	int a = 1, b = 3, c = 5;

	cout << "if statement: ";
	if (a >= b) { cout << "yes"; } else { cout << "no"; }
	cout << endl;

	cout << "for statement: ";
	for (int i = a; i <= b; ++i) { cout << i << " "; }
	cout << endl;

	cout << "while statement: ";
	while (a < c) { cout << ++a << " "; }
	cout << endl;

	cout << "do statement: ";
	do { cout << ++b << " "; } while (b < c);
	cout << endl;

	return 0;
}
