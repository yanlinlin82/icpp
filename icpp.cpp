#include <cstring>
#include <iostream>
#include <string>
#include <fstream>
#include <streambuf>
#include <vector>
#include <unordered_set>
#include <unordered_map>
using namespace std;

string eval(string& s)
{
	if (s.empty()) return "";
	string r;
	if (s[0] == '"') {
		for (int i = 1; i < s.size() && s[i] != '"'; ++i) {
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

void next(string& src, string& token)
{
	const char* p = src.c_str();
	for (;;) {
		while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
		if (!*p) { src = ""; token = ""; return; }
		if (*p == '#') {
			while (*p && *p != '\n') ++p; // skip '#' lines
		} else if (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'a' && *p <= 'z')) {
			token = *p++; while (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'a' && *p <= 'z')) token += *p++;
			src = p; return;
		} else if ((*p >= '0' && *p <= '9') || (*p == '.' && *(p+1) >= '0' && *(p+1) <= '9')) {
			int base = 10;
			bool float_point = false;
			if (*p != '.') {
				if ((token = *p++) != "0") {
					while (*p >= '0' && *p <= '9') token += *p++;
				} else if (*p == 'x' || *p == 'x') {
					base = 16; token += *p++; while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'a' && *p <= 'f')) token += *p++;
				} else {
					base = 8; while (*p >= '0' && *p <= '7') token += *p++;
				}
			}
			if (*p == '.') {
				float_point = true;
				if (base != 10) { printf("float point number should be 10 base!\n"); exit(1); }
				token += *p++;
				while (*p >= '0' && *p <= '9') token += *p++;
				if (*p == 'e' || *p == 'E') {
					token += *p++;
					if (*p >= '0' && *p <= '9') { printf("missing character in scientific number!\n"); exit(1); }
					while (*p >= '0' && *p <= '9') token += *p++;
				}
			}
			if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
				char c = *p++; c -= ((c >= 'a' && c <= 'z') ? ('z' - 'Z') : 0);
				string suffix(1, c);
				while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) suffix += static_cast<char>((*p++) | '\x20');
				if (suffix != "ul" && suffix != "u") { printf("invalid number suffix '%s'!\n", suffix.c_str()); exit(1); }
			}
			src = p; return;
		} else if (*p == '/') {
			if (*(p+1) == '/') { while (*p && *p != '\n') ++p; } // skip '// ...' comment
			else if (*(p+1) == '*') { while (*p && *p != '*' && *(p+1) != '/') ++p; } // skip '/* ... */' comment
			else if (*(p+1) == '=') { token = "/="; src = p + 2; return; }
			else { token = *p++; src = p; return; }
		} else if (*p == '\"' || *p == '\'') {
			char c = *p; token = *p++;
			while (*p != c) {
				if (!*p) { printf("truncated string!\n"); exit(1); }
				if (*p == '\\') {
					token += *p++;
					if (*p == 'x') { token += *p++; token += *p++; token += *p++; }
					else if (*p == 'n' || *p == 'r' || *p == 't' || *p == '0' || *p == 'b' || *p == '\\' || *p == '"' || *p == '\'') { token += *p++; }
					else { printf("invalid escape character '%c'!\n", *p); exit(1); }
				} else { token += *p++; }
			}
			token += *p++; src = p; return;
		} else {
			static const char* op[] = { "==", "=", "!=", "!", "++", "+=", "+", "--", "-=", "->", "-", "<=", "<<=", "<<", "<", ">=", ">>=", ">>", ">",
				"||", "|=", "|", "&&", "&=", "&", "::", ":", "^", "*=", "*", "%=", "%", "?", "~=", "~", ";", ".", "{", "}", "[", "]", "(", ")", ",", NULL };
			for (const char** q = op; *q; ++q) { size_t l = strlen(*q); if (memcmp(p, *q, l) == 0) { token = *q; src = p + l; return; } }
			token = *p++; while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') token += *p++;
			src = p; return;
		}
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) { cout << "usage: icpp <foo.cpp>" << endl; return 1; }
	ifstream file(argv[1]);
	string src((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
	unordered_set<string> namespaces;
	unordered_map<string, size_t> symboles;
	vector<pair<string, string>> code;
	string token;
	//while (next(src, token), !token.empty()) cout << '[' << token << ']' << endl;
	for (next(src, token); !token.empty(); next(src, token)) {
		if (token == "using") {
			next(src, token);
			if (token == "namespace") { next(src, token); namespaces.insert(token); next(src, token); }
			else { while (token != ";") next(src, token); }
		} else if (token == "int") {
			string name; next(src, name); next(src, token);
			if (token == "(") { // function
				while (token != ")") next(src, token);
				next(src, token); // '{'
				symboles[name] = code.size();
				code.push_back(make_pair("enter", ""));
			}
		} else if (token == "}") {
			next(src, token); // skip it
			code.push_back(make_pair("leave", ""));
		} else if (token == "return") {
			while (token != ";") next(src, token);
			code.push_back(make_pair("leave", ""));
			next(src, token); // skip ';'
		} else if (token == "cout") {
			next(src, token);
			while (token == "<<") {
				next(src, token);
				code.push_back(make_pair("cout<<", token));
				next(src, token);
			}
		} else {
			next(src, token);
		}
	}
	if (symboles.find("main") == symboles.end()) { printf("main() not defined!\n"); return 1; }
	for (size_t i = symboles["main"]; i < code.size(); ++i) {
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
