#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
using namespace std;

enum token_type { unknown = -1, symbol, number, text, op };

bool source = false;
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
			"||", "|=", "|", "&&", "&=", "&", "::", ":", "^", "*=", "*", "/=", "/", "%=", "%", "?", "~=", "~", ";", ".", "{", "}", "[", "]", "(", ")", ",", nullptr };
		for (const char** q = ops; *q; ++q) { size_t l = strlen(*q); if (memcmp(p, *q, l) == 0) { type = op; token = *q; p += l; return; } }
		type = unknown; token = *p++;
	}
}

void expect_token(string t, string stat)
{
	if (token != t) { cerr << "missing '" << t << "' for '" << stat << "'!" << endl; exit(1); }
}

void skip_until(string t, string stat)
{
	next(); while (!token.empty() && token != t) next();
	if (token.empty()) { cerr << "missing '" << t << "' for '" << stat << "'!" << endl; exit(1); }
	next();
}

void parse_expression()
{
	next();
}

void parse_statments()
{
	if (token == "{") {
		next(); while (!token.empty() && token != "}") parse_statments();
		expect_token("}", "{");
	} if (token == "if") {
		next(); expect_token("(", "if");
		next(); parse_expression(); expect_token(")", "if");
		next(); parse_statments();
	} else if (token == "for") {
		next(); expect_token("(", "for");
		next(); parse_expression(); expect_token(";", "for");
		next(); parse_expression(); expect_token(";", "for");
		next(); parse_expression(); expect_token(")", "for");
		expect_token(")", "for");
		next(); parse_statments();
	} else if (token == "while") {
		next(); expect_token("(", "while");
		next(); parse_expression(); expect_token(")", "while");
		next(); parse_statments();
	} else if (token == "do") {
		next(); expect_token("{", "do");
		parse_statments();
		expect_token("while", "do");
		next(); expect_token("(", "do");
		next(); parse_expression();
		next(); expect_token(")", "do");
		next(); expect_token(";", "do");
	} else if (token == "return") {
		next(); parse_expression(); expect_token(";", "return");
		next();
	} else if (token == "typedef") {
		skip_until(";", "typedef");
	} else if (type == symbol) {
		string left = token; next();
		while (token != ";") {
			string op = token; next();
			string right = token; next();
			if (left == "cout" && op == "<<") {
				code.push_back(make_pair("PUSH", "cout"));
				if (type == symbol) {
					code.push_back(make_pair("LEA", right));
					code.push_back(make_pair("PUSH", ""));
				} else if (type == number) {
				} else if (type == text) {
					auto s = eval(token);
					code.push_back(make_pair("PUSH", s));
				} else {
				}
				code.push_back(make_pair("<<", right));
			}
		}
		next();
	} else {
		next();
	}
}

void parse_source()
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
			skip_until(")", "function");
			expect_token("{", "function");
			next();
			while (token != "}") {
				parse_statments();
			}
			next();
		} else if (token == ";") {
			next();
		}
	}
}

int main(int argc, char** argv)
{
	--argc; ++argv;
	if (argc > 0 && **argv == '-' && (*argv)[1] == 's') { source = true; --argc; ++argv; }
	if (argc < 1) { cout << "usage: icpp [-s] <foo.cpp>" << endl; return 1; }
	ifstream file(argv[0]); if (!file.is_open()) { cerr << "failed to open file '" << argv[0] << "'!" << endl; return 1; }
	size_t line_no = 0; string line; while (getline(file, line)) {
		++line_no; if (source) { printf("%5zd ", line_no); for (auto c : line) { if (c == '\t') printf("    "); else printf("%c", c); }; printf("\n"); }
		if (!line.empty() && line[0] != '#') src += line + "\n";
	}
	//while (next(), !token.empty()) cout << '[' << token << ']' << endl;
	for (p = src.c_str(), next(); !token.empty(); ) parse_source();
	if (source) return 0;
	if (start < 0) { cerr << "main() not defined!" << endl; return 1; }
	for (size_t i = static_cast<size_t>(start); i < code.size(); ++i) {
		auto op = code[i].first;
		auto val = code[i].second;
		if (op == "<<") {
			if (val == "endl") {
				cout << endl;
			} else {
				cout << eval(val);
			}
		}
	}
	return 0;
}
