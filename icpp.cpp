#include <iostream>
#include <string>
#include <fstream>
#include <streambuf>
using namespace std;

int main(int argc, const char* const argv[])
{
	if (argc < 2) { cout << "Usage: icpp <foo.cpp>" << endl; return 1; }
	ifstream f(argv[1]);
	string s((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
	cout << s;
	return 0;
}
