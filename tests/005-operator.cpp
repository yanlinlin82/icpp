#include <iostream>
using namespace std;

int main()
{
	int a = 0x1234, b = 0x5678, c;
	cout << "a = " << a << endl;
	cout << "b = " << b << endl;
	c = a +  b; cout << "a +  b = " << c << endl;
	c = a -  b; cout << "a -  b = " << c << endl;
	c = a *  b; cout << "a *  b = " << c << endl;
	c = a /  b; cout << "a /  b = " << c << endl;
	c = a %  b; cout << "a %  b = " << c << endl;
	c = a >> b; cout << "a >> b = " << c << endl;
	c = a << b; cout << "a << b = " << c << endl;
	c = a &  b; cout << "a &  b = " << c << endl;
	c = a |  b; cout << "a |  b = " << c << endl;
	c = a == b; cout << "a == b = " << c << endl;
	c = a != b; cout << "a != b = " << c << endl;
	c = a >= b; cout << "a >= b = " << c << endl;
	c = a >  b; cout << "a >  b = " << c << endl;
	c = a <= b; cout << "a <= b = " << c << endl;
	c = a <  b; cout << "a <  b = " << c << endl;
	c = a && b; cout << "a && b = " << c << endl;
	c = a || b; cout << "a || b = " << c << endl;
	return 0;
}
