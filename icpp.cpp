#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
using namespace std;

enum token_type { unknown = -1, symbol, number, text, op };

static string src;
static const char* p; // position of source code parsing
static token_type type = unknown;
static string token;
static int start = -1; // main function offset
vector<pair<string, string>> code;

string eval(string& s)
{
	if (s.empty()) return "";
	string r;
	if (s[0] == '"') {
		for (size_t i = 1; i < s.size() && s[i] != '"'; ++i) {
			if (s[i] == '\\') {
				switch (s[++i]) {
				case 'n': r += '\n'; break;
				case 'r': r += '\r'; break;
				case 't': r += '\t'; break;
				default: r += '\\'; r += s[i]; break;
				}
			} else {
				r += s[i];
			}
		}
	}
	return r;
}

void next()
{
retry:
	while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
	if (!*p) { token = ""; type = unknown; return; }
	if (*p == '/' && *(p+1) == '/') { p += 2; while (*p && *p != '\n') ++p; goto retry; }
	if (*p == '/' && *(p+1) == '*') { p += 2; while (*p && *p != '*' && *(p+1) != '/') ++p; goto retry; }
	if (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'a' && *p <= 'z')) {
		type = symbol; token = *p++; while (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'a' && *p <= 'z')) token += *p++;
	} else if ((*p >= '0' && *p <= '9') || (*p == '.' && *(p+1) >= '0' && *(p+1) <= '9')) {
		type = number; token = *p++; while ((*p >= '0' && *p <= '9') || (*p == '.') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) token += *p++;
	} else if (*p == '\"' || *p == '\'') {
		type = text; char c = *p; token = *p++; while (*p && *p != c) { if (*p == '\\') { token += *p++; }; token += *p++; } token += *p++;
	} else {
		static const char* ops[] = { "==", "=", "!=", "!", "++", "+=", "+", "--", "-=", "->", "-", "<=", "<<=", "<<", "<", ">=", ">>=", ">>", ">",
			"||", "|=", "|", "&&", "&=", "&", "::", ":", "^", "*=", "*", "/=", "/", "%=", "%", "?", "~=", "~", ";", ".", "{", "}", "[", "]", "(", ")", ",", NULL };
		for (const char** q = ops; *q; ++q) { size_t l = strlen(*q); if (memcmp(p, *q, l) == 0) { type = op; token = *q; p += l; return; } }
		type = unknown; token = *p++;
	}
}

void parse_statments()
{
	if (token == "typedef") {
		next(); while (!token.empty() && token != ";") next();
		if (token.empty()) { cerr << "missing ';' for 'typedef'!" << endl; exit(1); }
		next();
	} else if (token == "if") {
		next(); if (token != "(") { cerr << "missing '(' for 'for'!" << endl; exit(1); }
	} else if (token == "for") {
		next();
	} else if (token == "while") {
		next();
	} else if (token == "do") {
		next();
	} else if (token == "return") {
		next(); while (!token.empty() && token != ";") next();
		if (token.empty()) { cerr << "missing ';' for 'using'!" << endl; exit(1); }
		next();
	} else if (type == symbol) {
		string left = token; next();
		while (token != ";") {
			string op = token; next();
			string right = token; next();
			if (left == "cout" && op == "<<") {
				code.push_back(make_pair("cout<<", right));
			}
		}
		next();
	} else {
		next();
	}
}

void parse_declare()
{
	if (token == "using") {
		next(); while (!token.empty() && token != ";") next();
		if (token.empty()) { cerr << "missing ';' for 'using'!" << endl; exit(1); }
		next();
	} else if (token == "namespace" || token == "class" || token == "struct" || token == "template" || token == "typedef") {
		cerr << "'" << token << "' has not been implemented!" << endl; exit(1);
	} else {
		if (token == "static" || token == "extern") next();
		string ret_type = token; next();
		string name = token; next();
		if (name == "main") start = code.size();
		//cerr << "> type: '" << ret_type << "', name: '" << name << "', (next) token: '" << token << "'" << endl;
		if (token == "(") {
			while (!token.empty() && token != ")") next();
			if (token.empty()) { cerr << "missing ')' for function!" << endl; exit(1); }
			next();
			if (token != "{") { cerr << "unexpected token rather than '{'!" << endl; exit(1); }
			next();
			while (token != "}") {
				parse_statments();
			}
			next();
		}
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) { cout << "usage: icpp <foo.cpp>" << endl; return 1; }
	ifstream file(argv[1]); if (!file.is_open()) { cerr << "failed to open file '" << argv[1] << "'!" << endl; return 1; }
	string line; while (getline(file, line)) { if (!line.empty() && line[0] != '#') src += line + "\n"; }
	//while (next(), !token.empty()) cout << '[' << token << ']' << endl;
	for (p = src.c_str(), next(); !token.empty(); ) parse_declare();
	if (start < 0) { cerr << "main() not defined!" << endl; return 1; }
	for (size_t i = static_cast<size_t>(start); i < code.size(); ++i) {
		auto op = code[i].first;
		auto val = code[i].second;
		if (op == "cout<<") {
			if (val == "endl") {
				cout << endl;
			} else {
				cout << eval(val);
			}
		}
	}
	return 0;
}
