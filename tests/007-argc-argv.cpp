#include <iostream>
using namespace std;

int main(int argc, const char** argv)
{
	cout << "argc = " << argc << endl;
	for (int i = 0; i < argc; ++i) {
		cout << "argv[" << i << "] = \"" << argv[i] << "\"" << endl;
	}
	return 0;
}
