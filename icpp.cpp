#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
using namespace std;

//const size_t MEM_SIZE = 32 * 1024 * 1024; // 32MB * sizeof(size_t)
const size_t MEM_SIZE = 1000;
vector<int> m(MEM_SIZE);

enum CODE                 { EXIT,   PUSH,   POP,   MOVE,   LEA,   LOAD,   ENTER,   LEAVE,   CALL,   SYSTEM   };
bool CODE_WITH_PARAM[]  = { false,  false,  false, true,   true,  true,   true,    true,    true,   true     };
const char* CODE_TEXT[] = { "EXIT", "PUSH", "POP", "MOVE", "LEA", "LOAD", "ENTER", "LEAVE", "CALL", "SYSTEM" };

enum DATA_TYPE                 { BYTE,   WORD,  };
const char* DATA_TYPE_TEXT[] = { "byte", "word" };

#define COLOR_NORMAL  "\x1B[0m"
#define COLOR_RED     "\x1B[31m"
#define COLOR_GREEN   "\x1B[32m"
#define COLOR_YELLOW  "\x1B[33m"
#define COLOR_BLUE    "\x1B[34m"
#define COLOR_MAGENTA "\x1B[35m"
#define COLOR_CYAN    "\x1B[36m"
#define COLOR_WHITE   "\x1B[37m"

enum token_type { unknown = 0, symbol, number, text, op, stk };
const char* token_type_text[] = { "unknown", "symbol", "number", "text", "op", "stk" };

int verbose = 0;
bool source = false;
vector<string> src;
const char* p = nullptr; // position of source code parsing
size_t line_no = 0;
token_type type = unknown;
string token;

vector<pair<string, string>> stack;
unordered_set<string> returned_functions;

unordered_map<string, size_t> functions;

vector<int> code;
vector<int> offset_in_code;
vector<int> data2;
vector<tuple<string, size_t, size_t, DATA_TYPE, string, string>> symbols; // [name, offset, size, data_type, type, ret_type]
unordered_map<string, size_t> symbol_to_index;
unordered_map<string, size_t> symbol_to_offset;
unordered_map<size_t, string> offset_to_symbol;

unordered_map<string, size_t> labels; // label (symbol name) => offset
unordered_map<size_t, pair<size_t, size_t>> offset; // line_no => [ offset_start, offset_end ]

void add_label(string name)
{
	labels.insert(make_pair(name, code.size()));
}

void add_assembly_code(CODE action, size_t param = 0)
{
	auto it = offset.find(line_no);
	if (it == offset.end()) {
		offset.insert(make_pair(line_no, make_pair(code.size(), code.size())));
	} else {
		it->second.second = code.size();
	}
	code.push_back(action);
	if (CODE_WITH_PARAM[action]) {
		if (action == LEA || action == LOAD) {
			offset_in_code.push_back(code.size());
		}
		code.push_back(param);
	}
}

unordered_map<string, int> o;

void print_source_code_line(size_t n)
{
	fprintf(stderr, "%4zd ", n + 1);
	for (const char* p = src[n].c_str(); *p; ++p) {
		if (*p == '\t') fprintf(stderr, "    "); else fprintf(stderr, "%c", *p);
	}
	fprintf(stderr, "\n");
}

void next()
{
	bool in_comment = false;
retry:
	if (!p || !*p) {
		if (line_no >= src.size()) { token = ""; type = unknown; return; } // end of source code
		p = src[line_no++].c_str(); while (*p == ' ' || *p == '\t') ++p;   // next line and skip leading spaces
		if (*p == '#') { while (*p) ++p; goto retry; }                     // skip '#'-leading line
		if (!*p) goto retry;
	}
	if (in_comment) { while (*p) { if (*p == '*' && *(p+1) == '/') { p += 2; in_comment = false; goto retry; }; ++p; }; goto retry; } // skip '/* ... */' comments
	if (*p == ' ' || *p == '\t') { ++p; while (*p && (*p == ' ' || *p == '\t')) ++p; goto retry; } // skip spaces
	if (*p == '/' && *(p+1) == '/') { p += 2; while (*p) ++p; goto retry; } // skip '// ...' comments
	if (*p == '/' && *(p+1) == '*') { p += 2; in_comment = true; goto retry; } // found '/* ... */' comments
	if (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'a' && *p <= 'z')) { // symbol
		type = symbol; token = *p++; while (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'a' && *p <= 'z')) token += *p++;
	} else if ((*p >= '0' && *p <= '9') || (*p == '.' && *(p+1) >= '0' && *(p+1) <= '9')) { // number
		type = number; token = *p++; while ((*p >= '0' && *p <= '9') || (*p == '.') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) token += *p++;
	} else if (*p == '\"' || *p == '\'') { // string
		type = text; char c = *p; token = *p++; while (*p && *p != c) { if (*p == '\\') { token += *p++; }; token += *p++; } token += *p++;
	} else { // operator
		static const char* ops[] = { "==", "=", "!=", "!", "++", "+=", "+", "--", "-=", "->*", "->", "-", "<=>", "<=", "<<=", "<<", "<", ">=", ">>=", ">>", ">",
			"||", "|=", "|", "&&", "&=", "&", "::", ":", "^", "*=", "*", "/=", "/", "%=", "%", "?", "~=", "~", ";", ".*", ".", "{", "}", "[", "]", "(", ")", ",", nullptr };
		for (const char** q = ops; *q; ++q) { size_t l = strlen(*q); if (memcmp(p, *q, l) == 0) { type = op; token = *q; p += l; return; } }
		type = unknown; token = *p++;
	}
}

void print_current()
{
	print_source_code_line(line_no - 1);
	fprintf(stderr, "     ");
	for (const char* q = src[line_no - 1].c_str(); *q && q + token.size() != p; ++q) {
		if (*q == '\t') fprintf(stderr, "    "); else fprintf(stderr, " ");
	}
	for (size_t i = 0; i < token.size(); ++i) {
		fprintf(stderr, "^");
	}
	fprintf(stderr, "--(token='%s',type='%s')\n", token.c_str(), token_type_text[type]);
}

void print_error(string msg)
{
	fprintf(stderr, "%s\n", msg.c_str());
	print_current();
	exit(1);
}

void expect_token(string t, string stat)
{
	if (token != t) {
		print_error("Error: missing '" + t + "' for '" + stat + "'! current token: '" + token + "'");
	}
}

void skip_until(string t, string stat)
{
	next();
	while (!token.empty() && token != t) {
		next();
	}
	if (token.empty()) {
		expect_token(t, stat);
	}
}

int precedence(string token)
{
	if (o.empty()) {
		// ref: https://en.cppreference.com/w/cpp/language/operator_precedence
		o.insert(make_pair("::", 1));

#if 0
		o.insert(make_pair("++", 2)); // suffix/postfix
		o.insert(make_pair("--", 2));
		o.insert(make_pair("{", 2));
		o.insert(make_pair("[", 2));
		o.insert(make_pair(".", 2));
		o.insert(make_pair("->", 2));
#endif

		o.insert(make_pair("++", 3)); // prefix
		o.insert(make_pair("--", 3));
		o.insert(make_pair("+", 3));
		o.insert(make_pair("-", 3));
		o.insert(make_pair("!", 3));
		o.insert(make_pair("~", 3));
		o.insert(make_pair("*", 3));
		o.insert(make_pair("&", 3));

		o.insert(make_pair(".*", 4));
		o.insert(make_pair("->*", 4));

		o.insert(make_pair("*", 5));
		o.insert(make_pair("/", 5));
		o.insert(make_pair("%", 5));

		o.insert(make_pair("+", 6));
		o.insert(make_pair("-", 6));

		o.insert(make_pair("<<", 7));
		o.insert(make_pair(">>", 7));

		o.insert(make_pair("<=>", 8));

		o.insert(make_pair("<", 9));
		o.insert(make_pair("<=", 9));
		o.insert(make_pair(">", 9));
		o.insert(make_pair(">=", 9));

		o.insert(make_pair("==", 10));
		o.insert(make_pair("!=", 10));

		o.insert(make_pair("&", 11));

		o.insert(make_pair("^", 12));

		o.insert(make_pair("|", 13));

		o.insert(make_pair("&&", 14));

		o.insert(make_pair("||", 15));

		o.insert(make_pair("?", 16));
		o.insert(make_pair(":", 16));
		o.insert(make_pair("=", 16));
		o.insert(make_pair("+=", 16));
		o.insert(make_pair("-=", 16));
		o.insert(make_pair("*=", 16));
		o.insert(make_pair("/=", 16));
		o.insert(make_pair("%=", 16));
		o.insert(make_pair(">>=", 16));
		o.insert(make_pair("<<=", 16));
		o.insert(make_pair("&=", 16));
		o.insert(make_pair("^=", 16));
		o.insert(make_pair("|=", 16));

		o.insert(make_pair(",", 17));

		o.insert(make_pair("(", 99));
		o.insert(make_pair("#", 99));
	}
	auto it = o.find(token);
	return (it == o.end() ? 0 : 1);
}

string vector_to_string(const vector<string>& a)
{
	string s = "[";
	for (size_t i = 0; i < a.size(); ++i) {
		if (i > 0) s += ",";
		s += "'" + a[i] + "'";
	}
	s += "]";
	return s;
}

string code_vector_to_string(const vector<pair<token_type, string>>& a)
{
	string s = "[";
	for (size_t i = 0; i < a.size(); ++i) {
		if (i > 0) s += ",";
		s += "'" + a[i].second + "|" + token_type_text[a[i].first] + "'";
	}
	s += "]";
	return s;
}

void dump(const vector<string>& stack, const vector<pair<token_type, string>>& postfix)
{
	fprintf(stderr, "> token='%s', type=%s, stack=", token.c_str(), token_type_text[type]);
	cout << vector_to_string(stack);
	fprintf(stderr, ", postfix=");
	cout << code_vector_to_string(postfix);
	fprintf(stderr, "\n");
}

vector<pair<token_type, string>> parse_expression()
{
	if (verbose > 2) {
		fprintf(stderr, "%s:\n", __FUNCTION__);
		if (verbose > 3) print_current();
	}

	vector<string> stack{"#"};
	vector<pair<token_type, string>> postfix;
	bool last_is_num = false;
	bool last_is_symbol = false;
	for (; !token.empty(); next()) {
		if (verbose > 2) {
			dump(stack, postfix);
		}
		if (type != op) {
			postfix.push_back(make_pair(type, token));
			last_is_num = true;
			last_is_symbol = (type == symbol);
		} else if (token == ";") {
			break;
		} else if (token == "(") {
			if (last_is_symbol) {
				string func_name = postfix.back().second;
				stack.push_back("()");
				for (;;) {
					next();
					parse_expression();
					if (token == ")") break;
					expect_token(",", "function " + func_name);
					next();
				}
				next();
			} else {
				stack.push_back(token);
			}
		} else if (token == ")") {
			while (stack.back() != "(" && stack.back() != "#") {
				postfix.push_back(make_pair(op, stack.back()));
				stack.pop_back();
			}
			if (stack.back() == "#") {
				break;
			}
			stack.pop_back(); // pop out "("
		} else {
			while (precedence(token) >= precedence(stack.back()) &&
					stack.back() != "#") {
				postfix.push_back(make_pair(op, stack.back()));
				stack.pop_back();
			}
			if (!last_is_num) {
				postfix.push_back(make_pair(unknown, ""));
			}
			stack.push_back(token);
			last_is_num = false;
			last_is_symbol = false;
		}
	}
	while (stack.back() != "#") {
		postfix.push_back(make_pair(op, stack.back()));
		stack.pop_back();
	}
	if (verbose > 2) {
		fprintf(stderr, "expression parsed, postfix=");
		cout << code_vector_to_string(postfix) << endl;
	}
	return postfix;
}

void dump_postfix(string where, const vector<pair<token_type, string>>& postfix)
{
	if (verbose > 1) {
		fprintf(stderr, "[DEBUG] => %s - expression [%zd]\n", where.c_str(), postfix.size());
		for (size_t i = 0; i < postfix.size(); ++i) {
			fprintf(stderr, "[DEBUG]   (%zd) '%s' (type = %s)\n", i + 1, postfix[i].second.c_str(), token_type_text[postfix[i].first]);
		}
	}
}

string eval_string(string s)
{
	string r;
	for (size_t i = 1; i + 1 < s.size(); ++i) {
		if (s[i] != '\\') {
			r += s[i];
		} else {
			char c = s[++i];
			switch (c) {
			default: r += c; break;
			case 'r': r += '\r'; break;
			case 'n': r += '\n'; break;
			case 't': r += '\t'; break;
			case '\'': r += '\''; break;
			case '\"': r += '\"'; break;
			}
		}
	}
	return r;
}

size_t eval_val(string s)
{
	return 1234;
}

void add_symbol(string name, size_t offset, size_t size, DATA_TYPE data_type, string type, string ret_type)
{
	if (verbose > 1) {
		fprintf(stderr, "add symbol: (%s, %zd, %zd, %s)\n", name.c_str(), offset, size, type.c_str());
	}
	size_t index = symbols.size();
	symbols.push_back(make_tuple(name, offset, size, data_type, type, ret_type));
	symbol_to_index.insert(make_pair(name, index));
	symbol_to_offset.insert(make_pair(name, offset));
	offset_to_symbol.insert(make_pair(offset, name));
}

size_t add_symbol(string name, string type, int value, string ret_type)
{
	size_t offset = data2.size();
	data2.push_back(value);
	add_symbol(name, offset, 1, WORD, type, ret_type);
	return offset;
}

size_t add_global_string(string s)
{
	size_t bytes = s.size() + 1;
	size_t size = (bytes + sizeof(int) + 1) / sizeof(int);
	size_t offset = data2.size();
	data2.resize(offset + size);
	memcpy(&data2[offset], s.c_str(), bytes);
	static size_t counter = 0;
	string name = "@s" + to_string(++counter); 
	string type = "string";
	add_symbol(name, offset, size, BYTE, type, "");
	return offset;
}

string get_type(const pair<token_type, string>& x)
{
	auto [ type, val ] = x;
	if (type == number) {
		return "number"; // TODO: 'int' and other number types
	} else if (type == text) {
		return "string";
	} else if (type == symbol) {
		auto it = symbol_to_index.find(val);
		if (it == symbol_to_index.end()) {
			fprintf(stderr, "Error: Unknown symbol '%s'!", val.c_str());
			exit(1);
		}
		auto [ name, offset, size, data_type, type, ret_type ] = symbols[it->second];
		return type;
	} else if (type == stk) {
		return val;
	} else {
		fprintf(stderr, "Error: Invalid token type '%s'!\n", token_type_text[type]);
		exit(1);
	}
}

void process_var(const pair<token_type, string>& x)
{
	auto [ type, s ] = x;
	if (type == number) {
		size_t v = eval_val(s);
		add_assembly_code(MOVE, v);
		add_assembly_code(PUSH);
	} else if (type == text) {
		string t = eval_string(s);
		add_assembly_code(LEA, add_global_string(t));
		add_assembly_code(PUSH);
	} else if (type == symbol) {
		auto it = symbol_to_offset.find(s);
		if (it == symbol_to_offset.end()) {
			fprintf(stderr, "Error: Unknown symbol '%s'!\n", s.c_str());
			exit(1);
		}
		add_assembly_code(LEA, it->second);
		add_assembly_code(PUSH);
	} else if (type == stk) {
		if (verbose > 1) {
			fprintf(stderr, "[DEBUG] Skip value that already in stack\n");
		}
	} else {
		fprintf(stderr, "Error: Invalid type!");
		exit(1);
	}
}

size_t print_code(const vector<int>& mem, size_t ip, bool color, int data_offset)
{
	if (color) { fprintf(stderr, COLOR_YELLOW); }
	fprintf(stderr, "%zd\t", ip);
	if (color) { fprintf(stderr, COLOR_BLUE); }
	size_t i = mem[ip++];
	if (i <= SYSTEM) { 
		fprintf(stderr, "%s", CODE_TEXT[i]);
	} else {
		fprintf(stderr, "<Invalid-Code> (0x%08zX)", i);
	}
	if (CODE_WITH_PARAM[i]) {
		size_t v = mem[ip++];
		fprintf(stderr, "\t0x%08zX (%zd)", v, v);
		if (i == LEA || i == LOAD || i == CALL || i == SYSTEM) {
			if (i == LEA || i == LOAD) {
				v -= data_offset;
			}
			auto it = offset_to_symbol.find(v);
			if (it != offset_to_symbol.end()) {
				fprintf(stderr, "\t; %s", it->second.c_str());
			}
		}
	}
	fprintf(stderr, "\n");
	return ip;
}

void build_code(const vector<pair<token_type, string>>& postfix)
{
	if (verbose > 1) {
		dump_postfix("build_code", postfix);
	}

	vector<pair<token_type, string>> stack;
	for (size_t i = 0; i < postfix.size(); ++i) {

#if 0
	fprintf(stderr, "===================\n");
	fprintf(stderr, "===> build_code (i = %zd)\n", i);
	for (size_t i = 0; i < code.size(); ++i) {
		print_code(code, i, false);
	}
	fprintf(stderr, "===================\n");
#endif
		if (postfix[i].first != op) {
			stack.push_back(postfix[i]);
		} else {
			if (stack.size() < 2) {
				fprintf(stderr, "Error: Invalid postfix!\n");
				exit(1);
			}
			size_t n = stack.size();
			auto& a = stack[n - 2];
			auto& b = stack[n - 1];

			process_var(a);
			process_var(b);

			string func_name = "operator" + postfix[i].second + "(" + get_type(a) + "," + get_type(b) + ")";
			string func_ret_type;

			if (functions.find(func_name) != functions.end()) {
				add_assembly_code(CALL, functions[func_name]); // adjust required
			} else if (symbol_to_offset.find(func_name) != symbol_to_offset.end()) {
				auto it = symbol_to_index.find(func_name);
				auto [ name, offset, size, data_type, type, ret_type ] = symbols[it->second];
				func_ret_type = ret_type;
				add_assembly_code(SYSTEM, symbol_to_offset[func_name]);
			} else {
				fprintf(stderr, "Error: Unknown function '%s'\n", func_name.c_str());
				exit(1);
			}
			//add_assembly_code(POP);

			stack.pop_back();
			stack.pop_back();
			stack.push_back(make_pair(stk, func_ret_type));
		}
	}
	//add_assembly_code(POP);
}

void parse_statements()
{
	if (verbose > 2) {
		fprintf(stderr, "[DEBUG] %s\n", __FUNCTION__);
		if (verbose > 3) print_current();
	}
	if (token == "{") {
		next(); while (!token.empty() && token != "}") parse_statements();
		expect_token("}", "{");
		next();
	} if (token == "if") {
		next(); expect_token("(", "if");
		next(); parse_expression(); expect_token(")", "if");
		next(); parse_statements();
		if (token == "else") {
			next();
			parse_statements();
		}
	} else if (token == "for") {
		next(); expect_token("(", "for");
		next(); parse_expression(); expect_token(";", "for");
		next(); parse_expression(); expect_token(";", "for");
		next(); parse_expression(); expect_token(")", "for");
		expect_token(")", "for");
		next(); parse_statements();
	} else if (token == "while") {
		next(); expect_token("(", "while");
		next(); parse_expression(); expect_token(")", "while");
		next(); parse_statements();
	} else if (token == "do") {
		next(); expect_token("{", "do");
		parse_statements();
		expect_token("while", "do");
		next(); expect_token("(", "do");
		next(); parse_expression();
		next(); expect_token(")", "do");
		next(); expect_token(";", "do");
	} else if (token == "return") {
		next();
		if (token != ";") {
			auto postfix = parse_expression();
			build_code(postfix);
			add_assembly_code(PUSH);
		}
		expect_token(";", "return");
		add_assembly_code(LEAVE);
		next();
		if (stack.empty()) {
			print_error("Error: unexpected 'return' statement!");
		}
		string the_type = stack.back().first;
		string name = stack.back().second;
		if (the_type != "function") {
			print_error("Error: unexpected 'return' statement! now it is in '" + the_type + "':'" + name + "'");
		}
		returned_functions.insert(name);
	} else if (token == "typedef") {
		skip_until(";", "typedef");
		next();
	} else {
		if (verbose > 2) {
			fprintf(stderr, ">>>>> parse_expression() in parse_statements()\n");
			print_current();
		}
		auto postfix = parse_expression();
		build_code(postfix);
		expect_token(";", "statement");
		next();
	}
}

void parse_enum()
{
	next(); // skip 'enum'

	if (type != symbol) {
		print_error("Error: missing symbol for 'enum' name!");
	}
	string name = token;
	next(); // skip name

	expect_token("{", "enum " + name);
	next(); // skip '{'

	while (!token.empty() && token != "}") {
		if (type != symbol) {
			print_error("Error: invalid token '" + token + "' for 'enum' value!");
		}
		string enum_key = token;
		next(); // skip

		string value;
		if (token == "=") {
			next(); // skip '='
			if (type != symbol && type != number) {
				print_error("Error: invalid token '" + token + "' for 'enum' declearation!");
			}
			value = token;
			next();
		}
		if (verbose > 1) {
			fprintf(stderr, "[DEBUG] enum %s: %s", name.c_str(), enum_key.c_str());
			if (!value.empty()) {
				fprintf(stderr, " = %s", value.c_str());
			}
			fprintf(stderr, "\n");
		}

		if (token == "}") break;
		expect_token(",", "enum " + name);
		next();
	}
	expect_token("}", "enum " + name);
	next(); // skip '}'
	expect_token(";", "enum " + name);
	next(); // skip ';'
	if (verbose > 1) {
		fprintf(stderr, "[DEBUG] end of enum %s\n", name.c_str());
	}
}

string parse_the_type()
{
	string the_type = token; next();
	if (the_type == "const") {
		the_type += " " + token;
		next();
	}
	vector<string> stack;
	while (type != symbol) {
		if (token == "<") {
			while (!token.empty()) {
				if (token == "<") {
					stack.push_back(token);
				} else if (token == ">>") {
					if (stack.size() < 2) {
						print_error("Error: unexpected '>>'!");
					}
					stack.pop_back();
					stack.pop_back();
					if (stack.empty()) {
						the_type += token;
						next();
						break;
					}
				} else if (token == ">") {
					stack.pop_back();
					if (stack.empty()) {
						the_type += token;
						next();
						break;
					}
				}
				the_type += token;
				next();
			}
		} else {
			the_type += token;
			next();
		}
	}
	return the_type;
}

void parse_define()
{
	if (verbose > 1) {
		fprintf(stderr, "[DEBUG] parse_define\n");
		if (verbose > 2) print_current();
	}
	string prefix = "";
	if (token == "static" || token == "extern") {
		prefix = token;
		next(); // skip this prefix
	}
	if (stack.empty() || stack.back().first != "function") {
		string the_type = parse_the_type();
		string name = token; next();
		bool function = false;
		if (token == "(") {
			function = true;
			skip_until(")", "function " + name);
			next();
			stack.push_back(make_pair("function", name));
			expect_token("{", "function " + name);
			add_label(name);
			add_assembly_code(ENTER, 0);
			next();
		} else {
			skip_until(";", "");
			next();
		}

		if (verbose > 1) {
			if (function) {
				fprintf(stderr, "[DEBUG] => function '%s', return-type='%s'\n", name.c_str(), the_type.c_str());
			} else {
				fprintf(stderr, "[DEBUG] => variable '%s', type='%s'\n", name.c_str(), the_type.c_str());
			}
			if (verbose > 2) print_current();
		}
	} else {
		parse_statements();
	}
}

void parse_source()
{
	if (verbose > 1) {
		fprintf(stderr, "[DEBUG] parse_source(), line_no = %zd, token=\"%s\", type=%s, stack=[",
				line_no, token.c_str(), token_type_text[type]);
		for (size_t i = 0; i < stack.size(); ++i) {
			fprintf(stderr, "(%s|%s)", stack[i].first.c_str(), stack[i].second.c_str());
		}
		fprintf(stderr, "]\n");
		if (verbose > 2) print_current();
	}

	if (token == "using") {
		next(); while (!token.empty() && token != ";") next();
		if (token.empty()) { print_error("Error: missing ';' for 'using'!"); }
		next();
		if (verbose > 1) fprintf(stderr, "[DEBUG] => 'using' statement skipped\n");
	} else if (token == "typedef") {
		skip_until(";", token);
		next();
		if (verbose > 1) fprintf(stderr, "[DEBUG] => 'typedef' statement skipped\n");
	} else if (token == "enum") {
		parse_enum();
	} else if (token == "union" || token == "struct" || token == "class" || token == "namespace") {
		string keyword = token;
		next(); 
		string name = token;
		next();
		expect_token("{", keyword + " " + name);
		stack.push_back(make_pair(keyword, name));
		next();
		if (verbose > 1) fprintf(stderr, "[DEBUG] => (%s %s) start\n", keyword.c_str(), name.c_str());
	} else if (token == "template") {
		skip_until(";", token);
		next();
		if (verbose > 1) fprintf(stderr, "[DEBUG] => 'template' statement skipped\n");
	} else if (token == ";") {
		next();
		if (!stack.empty()) {
			stack.pop_back();
		}
		if (verbose > 1) fprintf(stderr, "[DEBUG] => ';' - end of statement\n");
	} else if (token == "}") {
		string keyword;
		string name;
		if (!stack.empty()) {
			keyword = stack.back().first;
			name = stack.back().second;
			stack.pop_back();
		}
		if (keyword == "function") {
			if (returned_functions.find(name) == returned_functions.end()) {
				add_assembly_code(LEAVE);
			}
		}
		if (verbose > 1) fprintf(stderr, "[DEBUG] => '}' - end of block (%s,%s)\n", keyword.c_str(), name.c_str());
		next();
	} else {
		parse_define();
	}
}

bool load(string filename)
{
	ifstream file(filename);
	if (!file.is_open()) {
		cerr << "failed to open file '" << filename << "'!" << endl;
		return false;
	}
	string line;
	while (getline(file, line)) {
		src.push_back(line);
	}
	file.close();
	return true;
}

void init_symbol()
{
	add_symbol("cout", "ostream", 1, "");
	add_symbol("cerr", "ostream", 2, "");
	add_symbol("endl", "function", 0, "");
	add_symbol("operator<<(ostream,string)", "function", 0, "ostream");
	add_symbol("operator<<(ostream,function)", "function", 0, "ostream");
}

bool parse()
{
	init_symbol();

	for (next(); !token.empty();) {
		parse_source();
	}
	return true;
}

int show()
{
	for (size_t i = 0; i < src.size(); ++i) {
		print_source_code_line(i);
		auto it = offset.find(i + 1);
		if (it != offset.end()) {
			fprintf(stderr, COLOR_BLUE);
			for (size_t j = it->second.first; j <= it->second.second;) {
				j = print_code(code, j, true, 0);
			}
			fprintf(stderr, COLOR_NORMAL);
		}
	}
	fprintf(stderr, "\n");

	for (auto [ name, offset, size, data_type, type, ret_type ] : symbols) {
		fprintf(stderr, COLOR_YELLOW "%s:\n", name.c_str());
		fprintf(stderr, COLOR_BLUE "\t.%s\t%s\t", DATA_TYPE_TEXT[data_type], type.c_str());
		if (type == "string") {
			fprintf(stderr, "\"");
			const char* s = reinterpret_cast<const char*>(&data2[offset]);
			for (size_t i = 0; i < size * sizeof(int); ++i) {
				switch (s[i]) {
				default: fprintf(stderr, "%c", s[i]); break;
				case '\t': fprintf(stderr, "\\t"); break;
				case '\r': fprintf(stderr, "\\r"); break;
				case '\n': fprintf(stderr, "\\n"); break;
				case '\\': fprintf(stderr, "\\\\"); break;
				case '\'': fprintf(stderr, "\\\'"); break;
				case '\"': fprintf(stderr, "\\\""); break;
				}
			}
			fprintf(stderr, "\"");
		} else {
			for (size_t i = 0; i < size; ++i) {
				fprintf(stderr, "0x%08X", static_cast<unsigned int>(data2[offset + i]));
			}
		}
		fprintf(stderr, COLOR_NORMAL "\n");
	}
	return 0;
}

void print_vm_env(size_t ax, size_t ip, size_t sp, size_t bp)
{
	fprintf(stderr, "\tax = %08zX, ip = %08zX, sp = %08zX, bp = %08zX\n", ax, ip, sp, bp);
	fprintf(stderr, "\t[stack]: ");
	size_t i = 0;
	for (; i < 6 && sp + i < MEM_SIZE; ++i) {
		if (i > 0) { fprintf(stderr, ", "); }
		fprintf(stderr, "0x%08X", m[sp + i]);
	}
	if (sp + i < MEM_SIZE) {
		fprintf(stderr, ", ...");
	}
	fprintf(stderr, "\n");
	for (size_t i = 0; bp != MEM_SIZE; ++i) {
		fprintf(stderr, "\t[#%zd backtrace]: %08zX\n", i, bp);
		bp = m[bp];
	}
	fprintf(stderr, "\n");
	return;
}

string get_symbol(size_t offset, size_t data_offset)
{
	auto it = offset_to_symbol.find(offset - data_offset);
	if (it == offset_to_symbol.end()) {
		fprintf(stderr, "Error: Unknown symbol offset '%zd'\n", offset);
		exit(1);
	}
	return it->second;
}

void system_call(size_t offset, int& sp, size_t data_offset)
{
	if (verbose > 1) {
		fprintf(stderr, "[DEBUG] system_call(%zd)\n", offset);
	}
	auto func_name = get_symbol(offset, 0);
	if (func_name == "operator<<(ostream,string)") {
		size_t b = m[sp++]; string bb = get_symbol(b, data_offset);
		size_t a = m[sp++]; string aa = get_symbol(a, data_offset);
		if (verbose > 3) {
			fprintf(stderr, "a = %s(%zd), b = %s(%zd)\n", aa.c_str(), a, bb.c_str(), b);
		}
		const char* s = reinterpret_cast<const char*>(&m[b]);
		if (aa == "cout") {
			cout << s;
		} else if (aa == "cerr") {
			cerr << s;
		} else {
			fprintf(stderr, "Error: Unsupported operator<< for '%s'\n", aa.c_str());
			exit(1);
		}
		m[--sp] = a;
	} else if (func_name == "operator<<(ostream,function)") {
		size_t b = m[sp++]; string bb = get_symbol(b, data_offset);
		size_t a = m[sp++]; string aa = get_symbol(a, data_offset);
		if (verbose > 1) {
			fprintf(stderr, "a = %s(%zd), b = %s(%zd)\n", aa.c_str(), a, bb.c_str(), b);
		}
		if (bb != "endl") {
			fprintf(stderr, "Error: Only 'endl' is supported now!\n");
			exit(1);
		}
		if (aa == "cout") {
			cout << endl;
		} else if (aa == "cerr") {
			cerr << endl;
		} else {
			fprintf(stderr, "Error: Unsupported operator<< for '%s'\n", aa.c_str());
			exit(1);
		}
		m[--sp] = a;
	} else {
		fprintf(stderr, "Error: Unsupported function '%s'\n", func_name.c_str());
		exit(1);
	}
}

int run(int argc, const char** argv)
{
	// vm register
	int ax = 0, ip = 0, sp = MEM_SIZE, bp = MEM_SIZE;

	auto it = labels.find("main");
	if (it == labels.end()) {
		cerr << "Error: main() not defined!" << endl;
		return -1;
	}
	ip = it->second;

	if (verbose) {
		fprintf(stderr, "\nSystem Information:\n");
		fprintf(stderr, "  sizeof(int) = %zd\n", sizeof(int));
		fprintf(stderr, "  sizeof(void*) = %zd\n", sizeof(void*));
		fprintf(stderr, "\n");
	}

	// load code & data
	if (verbose > 0) {
		fprintf(stderr, "Loading program\n  code: %zd byte(s)\n  data: %zd byte(s)\n", code.size(), data2.size());
	}
	size_t offset = 0;
	for (size_t i = 0; i < code.size(); ++i) {
		m[offset++] = code[i];
	}
	size_t data_offset = offset;
	if (verbose > 0) {
		fprintf(stderr, "data_offset = %zd\n", data_offset);
	}
	for (size_t i = 0; i < data2.size(); ++i) {
		m[offset++] = data2[i];
	}
	for (size_t i = 0; i < offset_in_code.size(); ++i) {
		if (verbose > 1) {
			fprintf(stderr, "relocate: 0x%08X, 0x%08X", offset_in_code[i], m[offset_in_code[i]]);
		}
		m[offset_in_code[i]] += data_offset;
		if (verbose > 1) {
			fprintf(stderr, " => 0x%08X\n", m[offset_in_code[i]]);
		}
	}
	if (verbose > 0) {
		fprintf(stderr, "\n");
	}

	// prepare stack
	m[--sp] = EXIT;
	m[--sp] = PUSH; size_t t = sp;
	m[--sp] = argc;
	m[--sp] = reinterpret_cast<size_t>(argv);
	m[--sp] = t;

	size_t cycle = 0;
	for (;;) {
		++cycle;
		if (verbose > 0) {
			fprintf(stderr, "%zd:\t", cycle);
			print_code(m, ip, false, data_offset);
			if (verbose > 1) {
				print_vm_env(ax, ip, sp, bp);
			}
		}
		size_t i = m[ip++];
		if      (i == EXIT  ) { break;                                      } // exit the program
		else if (i == PUSH  ) { m[--sp] = ax;                               } // push ax to stack
		else if (i == POP   ) { ax = m[sp++];                               } // pop ax from stack
		else if (i == MOVE  ) { ax = m[ip++];                               } // move immediate to ax
		else if (i == LEA   ) { ax = m[ip++];                               } // load address to ax
		else if (i == LOAD  ) { ax = m[m[ip++]];                            } // load value to ax
		else if (i == ENTER ) { m[--sp] = bp; bp = sp; sp = sp - m[ip++];   } // enter subroutine
		else if (i == LEAVE ) { sp = bp; bp = m[sp++]; ip = m[sp++];        } // leave subroutine
		else if (i == CALL  ) { m[--sp] = ip + 1; ip = m[ip];               } // call subroutine
		else if (i == SYSTEM) { system_call(m[ip++], sp, data_offset);      } // system call
		else { fprintf(stderr, "WARNING: unknown instruction: '%zd'\n", i); }
	}
	if (verbose > 0) {
		fprintf(stderr, "Total: %zd cycle(s), return %d\n", cycle, m[sp]);
	}
	return m[sp];
}

int main(int argc, const char** argv)
{
	const char* filename = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-') {
			if (argv[i][1] == 'v') { ++verbose; }
			if (argv[i][1] == 's') { source = true; }
		} else {
			filename = argv[i];
		}
	}
	if (!filename) {
		cout << "usage: icpp [-s] <foo.cpp> ..." << endl;
		return false;
	}
	if (!load(filename) || !parse()) return -1;
	return source ? show() : run(argc, argv);
}
