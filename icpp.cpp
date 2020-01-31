#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
using namespace std;

//--------------------------------------------------------//
// message print

#define COLOR_NORMAL "\x1B[0m"
#define COLOR_RED    "\x1B[31m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_YELLOW "\x1B[33m"
#define COLOR_BLUE   "\x1B[34m"

int verbose = 0;

template <int level = 0> inline int log(const char* fmt, va_list ap) { return (verbose < level) ? 0 : vfprintf(stderr, fmt, ap); }
template <int level = 0> inline int log(const char* fmt, ...) { va_list ap; va_start(ap, fmt); return log<level>(fmt, ap); }

inline void err(const char* fmt, va_list ap) { log(COLOR_RED "Error: "); log(fmt, ap); log(COLOR_NORMAL); }
inline void err(const char* fmt, ...) { va_list ap; va_start(ap, fmt); err(fmt, ap); }

inline void warn(const char* fmt, va_list ap) { log(COLOR_YELLOW "Warning: "); log(fmt, ap); log(COLOR_NORMAL); }
inline void warn(const char* fmt, ...) { va_list ap; va_start(ap, fmt); warn(fmt, ap); }

//--------------------------------------------------------//
// machine code definition

const size_t MEM_SIZE = 1024 * 1024; // 1 MB * sizeof(size_t)
vector<int> m(MEM_SIZE);

enum machine_code {
	// single-word instrument (without parameter)
	EXIT, PUSH, POP,

	// two-word instrument (with one parameter)
	MOV,   LEA,   GET,   PUT,
	ADD,   SUB,   MUL,   DIV,   MOD,   // operation with an immediate number
	ADDM,  SUBM,  MULM,  DIVM,  MODM,  // operation with a memory number
	ENTER, LEAVE, CALL,  SYS,
};

const char* machine_code_name[] = {
	"EXIT",  "PUSH",  "POP",
	"MOV",   "LEA",   "GET",   "PUT",
	"ADD",   "SUB",   "MUL",   "DIV",   "MOD",
	"ADDM",  "SUBM",  "MULM",  "DIVM",  "MODM",
	"ENTER", "LEAVE", "CALL",  "SYS"
};

inline bool machine_code_has_parameter(int code)
{
	return (code >= MOV);
}

//--------------------------------------------------------//
// operator precedence in c/c++
// ref: https://en.cppreference.com/w/cpp/language/operator_precedence

unordered_map<string, int> operator_precedence = {
	{ "::", 1 },

	// suffix/postfix
	//{ "++", 2 }, { "--", 2 }, { "{", 2 }, { "[", 2 }, { ".", 2 }, { "->", 2 },

	// prefix
	{ "++", 3 }, { "--", 3 }, { "+", 3 }, { "-", 3 },
	{ "!", 3 }, { "~", 3 }, { "*", 3 }, { "&", 3 },

	{ ".*", 4 }, { "->*", 4 },

	{ "*", 5 }, { "/", 5 }, { "%", 5 },

	{ "+", 6 }, { "-", 6 },

	{ "<<", 7 }, { ">>", 7 },

	{ "<=>", 8 },

	{ "<", 9 }, { "<=", 9 }, { ">", 9 }, { ">=", 9 },

	{ "==", 10 }, { "!=", 10 },

	{ "&", 11 },

	{ "^", 12 },

	{ "|", 13 },

	{ "&&", 14 },

	{ "||", 15 },

	{ "?", 16 }, { ":", 16 }, { "=", 16 },
	{ "+=", 16 }, { "-=", 16 }, { "*=", 16 }, { "/=", 16 }, { "%=", 16 },
	{ ">>=", 16 }, { "<<=", 16 }, { "&=", 16 }, { "^=", 16 }, { "|=", 16 },

	{ ",", 17 },

	{ "(", 99 }, { "#", 99 },
};

//--------------------------------------------------------//

enum token_type { unknown = 0, symbol, number, text, op };
const char* token_type_text[] = { "unknown", "symbol", "number", "text", "op" };

vector<string> src;
const char* p = nullptr; // position of source code parsing
size_t line_no = 0;
token_type type = unknown;
string token;

vector<pair<string, string>> stack;
unordered_set<string> returned_functions;

vector<int> code_section;
vector<int> data_section;

vector<int> reloc_table;
vector<int> ext_table;

enum symbol_type { data_symbol = 0, code_symbol, ext_symbol };
const char* symbol_type_text[] = { "data", "code", "pseudo" };
unordered_map<string, tuple<symbol_type, size_t, size_t, string, string>> symbols; // name => { symbol_type, offset, size, type, ret_type }
unordered_map<size_t, string> offset_to_symbol[3];
unordered_map<string, unordered_set<string>> override_functions;

unordered_map<size_t, pair<size_t, size_t>> offset; // line_no => [ offset_start, offset_end ]

size_t ext_symbol_counter = 0;

void print_source_code_line(size_t n)
{
	log("%4zd ", n + 1);
	for (const char* p = src[n].c_str(); *p; ++p) {
		if (*p == '\t') log("    "); else log("%c", *p);
	}
	log("\n");
}

void print_current()
{
	print_source_code_line(line_no - 1);
	log("     ");
	for (const char* q = src[line_no - 1].c_str(); *q && q + token.size() != p; ++q) {
		if (*q == '\t') log("    "); else log(" ");
	}
	for (size_t i = 0; i < token.size(); ++i) {
		log("^");
	}
	log("--(token='%s',type='%s')\n", token.c_str(), token_type_text[type]);
}

void print_error(const char* fmt, ...)
{
	va_list ap; va_start(ap, fmt); err(fmt, ap);
	print_current();
	exit(1);
}

void add_symbol(string name, symbol_type stype,
		size_t offset, size_t size, string type, string ret_type)
{
	log<2>("[DEBUG] add symbol: ('%s', %s, %zd, %zd, '%s', '%s')\n",
			name.c_str(), symbol_type_text[stype],
			offset, size, type.c_str(), ret_type.c_str());
	symbols[name] = make_tuple(stype, offset, size, type, ret_type);
	if (offset_to_symbol[stype].find(offset) != offset_to_symbol[stype].end()) {
		print_error("offset %zd has already existsed! existed '%s', now adding '%s'\n",
				offset, offset_to_symbol[stype][offset].c_str(), name.c_str());
	}
	offset_to_symbol[stype][offset] = name;
}

size_t add_data_symbol(string name, vector<int> val, string type)
{
	size_t offset = data_section.size();
	add_symbol(name, data_symbol, offset, val.size(), type, "");
	data_section.insert(data_section.end(), val.begin(), val.end());
	return offset;
}

void add_code_symbol(string name, string args_type, string ret_type)
{
	override_functions[name].insert(name + args_type);
	add_symbol(name + args_type, code_symbol, code_section.size(), 0, args_type, ret_type);
}

void add_external_symbol(string name, string args_type, string ret_type = "")
{
	size_t offset = ++ext_symbol_counter;
	string name2 = name + (ret_type.empty() ? "" : args_type);
	override_functions[name].insert(name2);
	add_symbol(name2, ext_symbol, offset, 0, args_type, ret_type);
}

vector<int> prepare_string(const string& s)
{
	size_t bytes = s.size() + 1;
	size_t size = (bytes + sizeof(int) + 1) / sizeof(int);
	vector<int> a(size);
	memcpy(&a[0], s.c_str(), bytes);
	return a;
}

string alloc_name()
{
	static size_t counter = 0;
	return "@" + to_string(++counter);
}

void add_assembly_code(machine_code action, int param, bool relocate, bool extranal)
{
	auto it = offset.find(line_no);
	if (it == offset.end()) {
		offset.insert(make_pair(line_no, make_pair(code_section.size(), code_section.size())));
	} else {
		it->second.second = code_section.size();
	}
	code_section.push_back(action);
	if (machine_code_has_parameter(action)) {
		size_t offset = code_section.size();
		if (relocate) reloc_table.push_back(offset);
		if (extranal) ext_table.push_back(offset);
		code_section.push_back(param);
	}
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

void expect_token(string expected_token, string statement)
{
	if (token != expected_token) {
		print_error("missing '%s' for '%s'! current token: '%s'\n",
				expected_token.c_str(), statement.c_str(), token.c_str());
	}
}

void skip_until(string expected_token, string stat)
{
	next();
	while (!token.empty() && token != expected_token) {
		next();
	}
	if (token.empty()) {
		expect_token(expected_token, stat);
	}
}

int precedence(string token)
{
	if (operator_precedence.empty()) {
		operator_precedence.insert(make_pair("::", 1));
	}
	auto it = operator_precedence.find(token);
	return (it == operator_precedence.end() ? 0 : it->second);
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

int eval_number(string s)
{
	int n = 0;
	bool minus = false;
	const char* p = s.c_str();
	if (*p == '-') {
		minus = true;
		++p;
	}
	while (*p) {
		if (*p >= '0' && *p <= '9') {
			n = n * 10 + (*p - '0');
			++p;
		} else if (*p == '.') {
			break;
		}
	}
	return (minus ? -n : n);
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

pair<string, vector<int>> eval_item(const pair<token_type, string>& a)
{
	if (a.first == number) {
		int v = eval_number(a.second);
		vector<int> a{v};
		return make_pair("int", a);
	} else if (a.first == text) {
		string s = eval_string(a.second);
		vector<int> a = prepare_string(s);
		return make_pair("const char*", a);
	} else {
		print_error("eval_item failed!\n");
		exit(1);
	}
}

pair<string, vector<int>> eval_op(const pair<string, vector<int>>& a,
		const pair<token_type, string>& op, const pair<string, vector<int>>& b)
{
	printf("eval_op: op = {%s:%s}\n", token_type_text[op.first], op.second.c_str());

	if (op.second == "+") {
		if (a.first == "int" and b.first == "int") {
		}
	}
	print_error("failed");
	exit(1);
}

pair<string, vector<int>> evaluate_postfix(const vector<pair<token_type, string>>& a, string type)
{
	vector<pair<string, vector<int>>> stack;
	for (auto e : a) {
		if (e.first == symbol) {
			print_error("evaluate_postfix failed! there is some symbol!\n");
		} else if (e.first != op) {
			auto x = eval_item(e);
			stack.push_back(x);
		} else {
			if (stack.size() < 2) { print_error("evaluate_postfix failed! stack overflow!\n"); }
			auto b = stack.back(); stack.pop_back();
			auto a = stack.back(); stack.pop_back();
			stack.push_back(eval_op(a, e, b));
		}
	}
	if (stack.size() != 1) { print_error("evaluate_postfix failed! content may be error!\n"); }
	return stack.back();
}

bool can_be_evaluated(const vector<pair<token_type, string>>& a)
{
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].first == symbol) return false;
	}
	return true;
}

vector<pair<token_type, string>> parse_expression(bool before_comma = false)
{
	log<2>("%s:\n", __FUNCTION__);
	if (verbose >= 3) print_current();

	vector<string> stack{"#"};
	vector<pair<token_type, string>> postfix;
	bool last_is_num = false;
	bool last_is_symbol = false;
	for (; !token.empty(); next()) {
		log<2>("> token='%s', type=%s, stack=%s, postfix=%s\n",
				token.c_str(), token_type_text[type],
				vector_to_string(stack).c_str(), code_vector_to_string(postfix).c_str());
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
			if (token == "," && before_comma) break;
			log<2>("[DEBUG] compare op in parse_expression: '%s' => %d, '%s' => %d\n",
					token.c_str(), precedence(token),
					stack.back().c_str(), precedence(stack.back()));
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
	log<2>("expression parsed, postfix=%s\n", code_vector_to_string(postfix).c_str());
	return postfix;
}

size_t print_code(const vector<int>& mem, size_t ip, bool color, int data_offset, int ext_offset)
{
	if (color) { log(COLOR_YELLOW); }
	log("%zd\t", ip);
	if (color) { log(COLOR_BLUE); }
	size_t i = mem[ip++];
	if (i <= SYS) {
		log("%s", machine_code_name[i]);
	} else {
		log("<invalid-code> (0x%08zX)", i);
	}
	if (machine_code_has_parameter(i)) {
		bool is_reloc = (binary_search(reloc_table.begin(), reloc_table.end(), ip));
		bool is_ext = (binary_search(ext_table.begin(), ext_table.end(), ip));
		size_t v = mem[ip++];
		if (color && is_reloc) log(COLOR_GREEN);
		if (color && is_ext) log(COLOR_RED);
		log("\t0x%08zX (%zd)", v, v);
		if (color && (is_reloc || is_ext)) log(COLOR_BLUE);

		symbol_type stype = data_symbol;
		if (is_reloc) {
			stype = data_symbol;
			v -= data_offset;
		} else if (is_ext) {
			stype = ext_symbol;
			v -= ext_offset;
		} else {
			if (i == CALL) stype = code_symbol;
			if (i == SYS) stype = ext_symbol;
		}
		auto it = offset_to_symbol[stype].find(v);
		if (it != offset_to_symbol[stype].end()) {
			auto it2 = symbols.find(it->second);
			if (it2 != symbols.end()) {
				auto [ stype, offset, size, type_name, ret_type ] = it2->second;
				if (stype == data_symbol) {
					log("\t; %s\t%s", it->second.c_str(), type_name.c_str());
				} else {
					log("\t; %s %s", ret_type.c_str(), it->second.c_str());
				}
			}
		}
	}
	log("\n");
	return ip;
}

enum var_mode { mode_imm = 0, mode_var, mode_func, mode_stack };

tuple<int, var_mode, string, string> process_var(const pair<token_type, string>& x)
{
	auto [ type, s ] = x;
	if (type == number) {
		int v = eval_number(s);
		return make_tuple(v, mode_imm, "int", "");
	} else if (type == text) {
		string v = eval_string(s);
		auto mem = prepare_string(v);
		size_t offset = add_data_symbol(alloc_name(), mem, "const char*");
		return make_tuple(offset, mode_imm, "const char*", "");
	} else if (type == symbol) {
		auto it = symbols.find(s);
		if (it == symbols.end()) {
			auto it2 = override_functions.find(s);
			if (it2 == override_functions.end() || it2->second.empty()) {
				print_error("unknown symbol '%s'!\n", s.c_str());
			}
			it = symbols.find(*(it2->second.begin()));
			if (it == symbols.end()) {
				print_error("unknown symbol '%s'!\n", s.c_str());
			}
		}
		auto [ stype, offset, size, type_name, ret_type ] = it->second;
		var_mode mode = (stype == data_symbol ? mode_var : mode_func);
		return make_tuple(offset, mode, type_name, ret_type);
	} else {
		log("error: unknown symbol type '%s' for '%s'\n",
				token_type_text[type], s.c_str());
		exit(1);
	}
}

void process_var_from_stack(int val, var_mode mode, string type_name)
{
	if (mode == mode_imm) { // immediate number
		add_assembly_code(MOV, val, (type_name != "int"), false);
		add_assembly_code(PUSH, 0, false, false);
	} else if (mode == mode_var) { // variable
		if (type_name == "int") {
			add_assembly_code(GET, val, true, false);
		} else {
			add_assembly_code(LEA, val, true, false);
		}
		add_assembly_code(PUSH, 0, false, false);
	} else if (mode == mode_func) { // function (address)
		add_assembly_code(LEA, val, false, true);
		add_assembly_code(PUSH, 0, false, false);
	}
}

void process_two_number_op(machine_code op_n, machine_code op_m,
		var_mode a_mode, int a_val, var_mode b_mode, int b_val)
{
	if (a_mode == mode_imm) {
		add_assembly_code(MOV, a_val, false, false);
	} else {
		add_assembly_code(GET, a_val, true, false);
	}
	if (b_mode == mode_imm) {
		add_assembly_code(op_n, b_val, false, false);
	} else {
		add_assembly_code(op_m, b_val, true, false);
	}
}

void build_code(const vector<pair<token_type, string>>& postfix)
{
	if (verbose >= 2) {
		log("[DEBUG] => build_code - expression [%zd]\n", postfix.size());
		for (size_t i = 0; i < postfix.size(); ++i) {
			log("[DEBUG]   (%zd) '%s' (type = %s)\n", i + 1,
					postfix[i].second.c_str(), token_type_text[postfix[i].first]);
		}
	}
	vector<tuple<int, var_mode, string, string>> stack; // [ { stype, value/offset, mode, type_name, ret_type } ]
	for (size_t i = 0; i < postfix.size(); ++i) {
		if (postfix[i].first != op) {
			auto v = process_var(postfix[i]);
			if (verbose >= 2) {
				auto [ val, immedate, type_name, ret_type ] = v;
				printf("[DEBUG] [%s:%s] => (%d:%s:%s:%s)\n",
						postfix[i].second.c_str(),
						token_type_text[postfix[i].first],
						val, (immedate ? "v" : "r"),
						type_name.c_str(), ret_type.c_str());
			}
			stack.push_back(v);
		} else {
			if (stack.size() < 2) {
				err("Invalid postfix!\n");
				exit(1);
			}
			auto b = stack.back(); stack.pop_back();
			auto a = stack.back(); stack.pop_back();
			auto [ a_val, a_type, a_type_name, a_ret_type ] = a;
			auto [ b_val, b_type, b_type_name, b_ret_type ] = b;

			string opr = postfix[i].second;
			string ret_type;
			if (a_type_name == "int" && b_type_name == "int") {
				if (opr == "+") {
					process_two_number_op(ADD, ADDM, a_type, a_val, b_type, b_val);
				} else if (opr == "-") {
					process_two_number_op(SUB, SUBM, a_type, a_val, b_type, b_val);
				} else if (opr == "*") {
					process_two_number_op(MUL, MULM, a_type, a_val, b_type, b_val);
				} else if (opr == "/") {
					process_two_number_op(DIV, DIVM, a_type, a_val, b_type, b_val);
				} else if (opr == "%") {
					process_two_number_op(MOD, MODM, a_type, a_val, b_type, b_val);
				} else {
					print_error("Unsupported operator '%s'\n", opr.c_str());
				}
				ret_type = "int";
			} else {
				string name = "operator" + opr + "(" +
					(a_ret_type.empty() ? "" : "(*)") + a_type_name + "," +
					(b_ret_type.empty() ? "" : "(*)") + b_type_name + ")";
				if (verbose >= 2) {
					printf("[DEBUG] build_code => function: '%s'\n", name.c_str());
					printf("[DEBUG]   a: %d, %d, %s\n", a_val, a_type, a_type_name.c_str());
					printf("[DEBUG]   b: %d, %d, %s\n", b_val, b_type, b_type_name.c_str());
				}
				auto it = symbols.find(name);
				if (it == symbols.end()) {
					print_error("Unknown function '%s'\n", name.c_str());
				}
				auto [ stype, offset, size, type_name, the_ret_type ] = it->second;
				if (stype != code_symbol && stype != ext_symbol) {
					print_error("symbol '%s' is not a function!\n", name.c_str());
				}
				process_var_from_stack(a_val, a_type, a_type_name);
				process_var_from_stack(b_val, b_type, b_type_name);
				if (stype == code_symbol) {
					add_assembly_code(CALL, offset, false, false);
				} else {
					add_assembly_code(SYS, offset, false, false);
				}
				ret_type = the_ret_type;
			}
			if (i + 1 < postfix.size()) {
				add_assembly_code(PUSH, 0, false, false);
				stack.push_back(make_tuple(0, mode_stack, ret_type, ""));
			}
		}
	}
}

void parse_statements()
{
	log<2>("[DEBUG] %s\n", __FUNCTION__);
	if (verbose >= 3) print_current();
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
			add_assembly_code(PUSH, 0, false, false);
		}
		expect_token(";", "return");
		add_assembly_code(LEAVE, 0, false, false);
		next();
		if (stack.empty()) {
			print_error("unexpected 'return' statement!\n");
		}
		string type_name = stack.back().first;
		string name = stack.back().second;
		if (type_name != "function") {
			print_error("unexpected 'return' statement! now it is in '%s':'%s'!\n", type_name.c_str(), name.c_str());
		}
		returned_functions.insert(name);
	} else if (token == "typedef") {
		skip_until(";", "typedef");
		next();
	} else {
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
		print_error("missing symbol for 'enum' name!\n");
	}
	string name = token;
	next(); // skip name

	expect_token("{", "enum " + name);
	next(); // skip '{'

	while (!token.empty() && token != "}") {
		if (type != symbol) {
			print_error("invalid token '%s' for 'enum' value!\n", token.c_str());
		}
		string enum_key = token;
		next(); // skip

		string value;
		if (token == "=") {
			next(); // skip '='
			if (type != symbol && type != number) {
				print_error("invalid token '%s' for 'enum' declearation!\n", token.c_str());
			}
			value = token;
			next();
		}
		if (verbose >= 2) {
			log("[DEBUG] enum %s: %s", name.c_str(), enum_key.c_str());
			if (!value.empty()) {
				log(" = %s", value.c_str());
			}
			log("\n");
		}

		if (token == "}") break;
		expect_token(",", "enum " + name);
		next();
	}
	expect_token("}", "enum " + name);
	next(); // skip '}'
	expect_token(";", "enum " + name);
	next(); // skip ';'
	log<1>("[DEBUG] end of enum %s\n", name.c_str());
}

bool is_built_in_type()
{
	return (token == "char" || token == "short" || token == "int" ||
			token == "long" || token == "longlong" ||
			token == "float" || token == "double" ||
			token == "signed" || token == "unsigned");
}

string parse_type_name()
{
	string prefix;
	if (token == "static" || token == "extern") {
		prefix = token;
		next(); // skip this prefix
	}
	string type_name;
	if (token == "auto") {
		type_name = token; next();
	} else {
		vector<pair<token_type, string>> a;
		int angle_bracket = 0;
		while (token == "const" || is_built_in_type() ||
				token == "*" || token == "&" ||
				token == "<" || token == ">" || token == ">>") {
			if (token == "<") {
				++angle_bracket;
				a.push_back(make_pair(type, token));
				next();
			} else if (token == ">") {
				if (--angle_bracket < 0) { print_error("unexpected '>'!\n"); }
				a.push_back(make_pair(type, token));
				next();
			} else if (token == ">>") {
				if (angle_bracket < 2) { print_error("unexpected '>>'!\n"); }
				angle_bracket -= 2;
				a.push_back(make_pair(type, ">"));
				a.push_back(make_pair(type, ">"));
				next();
			} else {
				a.push_back(make_pair(type, token));
				next();
			}
		}
		for (auto e : a) {
			type_name += (type_name.empty() ? "" : " ");
			type_name += e.second;
		}
	}
	return type_name;
}

void parse_source()
{
	if (verbose >= 2) {
		log("[DEBUG] parse_source(), line_no = %zd, token=\"%s\", type=%s, stack=[",
				line_no, token.c_str(), token_type_text[type]);
		for (size_t i = 0; i < stack.size(); ++i) {
			log("(%s|%s)", stack[i].first.c_str(), stack[i].second.c_str());
		}
		log("]\n");
		if (verbose >= 3) print_current();
	}

	if (token == "using") {
		next(); while (!token.empty() && token != ";") next();
		if (token.empty()) { print_error("missing ';' for 'using'!\n"); }
		next();
		log<2>("[DEBUG] => 'using' statement skipped\n");
	} else if (token == "typedef") {
		skip_until(";", token);
		next();
		log<2>("[DEBUG] => 'typedef' statement skipped\n");
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
		log<2>("[DEBUG] => (%s %s) start\n", keyword.c_str(), name.c_str());
	} else if (token == "template") {
		skip_until(";", token);
		next();
		log<2>("[DEBUG] => 'template' statement skipped\n");
	} else if (token == ";") {
		next();
		if (!stack.empty()) {
			stack.pop_back();
		}
		log<2>("[DEBUG] => ';' - end of statement\n");
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
				add_assembly_code(LEAVE, 0, false, false);
			}
		}
		log<2>("[DEBUG] => '}' - end of block (%s,%s)\n", keyword.c_str(), name.c_str());
		next();
	} else if (token == "auto" || token == "const" ||
			token == "static" || token == "extern" ||
			is_built_in_type()) { // start as type
		string type_name = parse_type_name();
		string type_prefix = type_name;
		string name = token; next();
		if (verbose >= 2) {
			log("[DEBUG] => function/variable '%s', type='%s'\n",
					name.c_str(), type_name.c_str());
			if (verbose >= 3) print_current();
		}
		if (token == "(") { // function
			if (!stack.empty() && stack.back().first != "function") {
				print_error("nesting function is not allowed!\n");
			}
			skip_until(")", "function " + name); // TODO: parse arg types
			next();
			stack.push_back(make_pair("function", name));
			expect_token("{", "function " + name);
			string args_type = "(...)";
			add_code_symbol(name, args_type, type_name);
			add_assembly_code(ENTER, 0, false, false);
			next();
		} else { // variable
			for (;;) {
				vector<int> init(1);
				if (token == "=") {
					next();
					auto postfix = parse_expression(true);
					if (can_be_evaluated(postfix)) {
						auto val = evaluate_postfix(postfix, type_name);
						init = val.second;
					}
				}
				add_data_symbol(name, init, type_name);
				if (token != ",") break;
				next();
				type_name = type_prefix;
				while (token == "*" || token == "&") {
					type_name += token;
					next();
				}
				name = token; next();
				if (verbose >= 2) {
					log("[DEBUG] => another variable '%s', type='%s'\n",
							name.c_str(), type_name.c_str());
					if (verbose >= 3) print_current();
				}
			}
			expect_token(";", "variable");
			next();
		}
	} else {
		parse_statements();
	}
}

bool load(string filename)
{
	ifstream file(filename);
	if (!file.is_open()) {
		err("failed to open file '%s'!", filename.c_str());
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
	add_external_symbol("cout", "ostream");
	add_external_symbol("cerr", "ostream");
	add_external_symbol("endl", "(endl)", "void");
	add_external_symbol("operator<<", "(ostream,int)",         "ostream");
	add_external_symbol("operator<<", "(ostream,double)",      "ostream");
	add_external_symbol("operator<<", "(ostream,const char*)", "ostream");
	add_external_symbol("operator<<", "(ostream,(*)(endl))",   "ostream");
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
			log(COLOR_BLUE);
			for (size_t j = it->second.first; j <= it->second.second;) {
				j = print_code(code_section, j, true, 0, 0);
			}
			log(COLOR_NORMAL);
		}
	}
	log("\n");

	for (size_t i = 0; i < data_section.size(); ++i) {
		auto it = offset_to_symbol[data_symbol].find(i);
		if (it != offset_to_symbol[data_symbol].end()) {
			auto name = it->second;
			auto [ stype, offset, size, type, ret_type ] = symbols[name];
			string data_type = "word";
			if (type == "const char*") data_type = "byte";
			log(COLOR_YELLOW "%d", i);
			log(COLOR_BLUE "\t.%s\t", data_type.c_str());
			if (type == "const char*") {
				log("\"");
				const char* s = reinterpret_cast<const char*>(&data_section[offset]);
				for (size_t i = 0; i < size * sizeof(int); ++i) {
					switch (s[i]) {
						default: log("%c", s[i]); break;
						case '\t': log("\\t"); break;
						case '\r': log("\\r"); break;
						case '\n': log("\\n"); break;
						case '\\': log("\\\\"); break;
						case '\'': log("\\\'"); break;
						case '\"': log("\\\""); break;
					}
				}
				log("\"");
			} else {
				for (size_t i = 0; i < size; ++i) {
					log("0x%08X", static_cast<unsigned int>(data_section[offset + i]));
				}
			}
			log("\t; %s", name.c_str());
			log("\t%s", type.c_str());
			log(COLOR_NORMAL "\n");
		}
	}

	log<2>("relocate table:\n");
	for (size_t i = 0; i < reloc_table.size(); ++i) {
		log<2>("\t0x%08x", reloc_table[i]);
		if (i % 5 == 4) log<2>("\n");
	}
	if (reloc_table.size() % 5 != 0) log<2>("\n");

	log<2>("external table:\n");
	for (size_t i = 0; i < ext_table.size(); ++i) {
		log<2>("\t0x%08x", ext_table[i]);
		if (i % 5 == 4) log<2>("\n");
	}
	if (ext_table.size() % 5 != 0) log<2>("\n");
	return 0;
}

void print_vm_env(int ax, int ip, int sp, int bp)
{
	log("\tax = %08X, ip = %08X, sp = %08X, bp = %08X\n", ax, ip, sp, bp);
	log("\t[stack]: ");
	size_t i = 0;
	for (; i < 6 && sp + i < MEM_SIZE; ++i) {
		if (i > 0) { log(", "); }
		log("0x%08X", m[sp + i]);
	}
	if (sp + i < MEM_SIZE) {
		log(", ...");
	}
	log("\n");
	for (size_t i = 0; bp != MEM_SIZE; ++i) {
		log("\t[#%zd backtrace]: %08X\n", i, bp);
		if (bp == m[bp]) break;
		bp = m[bp];
	}
	log("\n");
	return;
}

string get_symbol(symbol_type stype, size_t offset,
		size_t data_offset, size_t ext_offset)
{
	size_t original_offset = offset;
	if (original_offset >= ext_offset) {
		original_offset -= ext_offset;
		stype = ext_symbol;
	} else if (original_offset >= data_offset) {
		original_offset -= data_offset;
		stype = data_symbol;
	}
	auto it = offset_to_symbol[stype].find(original_offset);
	if (it == offset_to_symbol[stype].end()) {
		err("Unknown symbol offset '%zd' (stype = '%s')\n",
				offset, symbol_type_text[stype]);
		exit(1);
	}
	return it->second;
}

int system_call(size_t offset, int& sp, size_t data_offset, size_t ext_offset)
{
	auto func_name = get_symbol(ext_symbol, offset, 0, 0);
	log<2>("[DEBUG] system_call: %s (%zd)\n", func_name.c_str(), offset);
	if (func_name == "operator+(int,int)") {
		int b = m[sp++];
		int a = m[sp++];
		return a + b;
	} else if (func_name == "operator+(double,double)") {
		int b = m[sp++];
		int a = m[sp++];
		return a + b;
	} else if (func_name == "operator<<(ostream,int)"
			|| func_name == "operator<<(ostream,double)") {
		int b = m[sp++];
		int a = m[sp++]; string aa = get_symbol(data_symbol, a, data_offset, ext_offset);
		log<3>("a = %s(%d), b = %d\n", aa.c_str(), a, b);
		if (aa == "cout") {
			cout << b;
		} else if (aa == "cerr") {
			cerr << b;
		} else {
			err("Unsupported operator<< for %d('%s')\n", m[a], aa.c_str());
			exit(1);
		}
		return a;
	} else if (func_name == "operator<<(ostream,const char*)") {
		int b = m[sp++]; string bb = get_symbol(data_symbol, b, data_offset, ext_offset);
		int a = m[sp++]; string aa = get_symbol(data_symbol, a, data_offset, ext_offset);
		log<3>("a = %s(%d), b = %s(%d)\n", aa.c_str(), a, bb.c_str(), b);
		const char* s = reinterpret_cast<const char*>(&m[b]);
		if (aa == "cout") {
			cout << s;
		} else if (aa == "cerr") {
			cerr << s;
		} else {
			err("Unsupported operator<< for %d('%s')\n", m[a], aa.c_str());
			exit(1);
		}
		return a;
	} else if (func_name == "operator<<(ostream,(*)(endl))") {
		int b = m[sp++];
		int a = m[sp++]; string aa = get_symbol(data_symbol, a, data_offset, ext_offset);
		log<3>("a = %s(%d), b = %d\n", aa.c_str(), a, b);
		if (aa == "cout") {
			cout << endl;
		} else if (aa == "cerr") {
			cerr << endl;
		} else {
			err("Unsupported operator<< for %d('%s')\n", m[a], aa.c_str());
			exit(1);
		}
		return a;
	} else {
		err("Unsupported function '%s'\n", func_name.c_str());
		exit(1);
	}
}

int run(int argc, const char** argv)
{
	// vm register
	int ax = 0, ip = 0, sp = MEM_SIZE, bp = MEM_SIZE;

	auto it = override_functions.find("main");
	if (it == override_functions.end() || it->second.empty()) {
		err("main() not defined!\n");
		return -1;
	}
	if (it->second.size() > 1) {
		err("duplcated definition of main()!\n");
		return -1;
	}
	auto it2 = symbols.find(*(it->second.begin()));
	ip = get<1>(it2->second);

	if (verbose >= 1) {
		log("\nSystem Information:\n");
		log("  sizeof(int) = %zd\n", sizeof(int));
		log("  sizeof(void*) = %zd\n", sizeof(void*));
		log("\n");
	}

	// load code & data
	log<1>("Loading program\n"
			"  code: %zd byte(s)\n"
			"  data: %zd byte(s)\n"
			"\n",
			code_section.size(), data_section.size());

	size_t offset = 0;
	for (size_t i = 0; i < code_section.size(); ++i) {
		m[offset++] = code_section[i];
	}
	size_t data_offset = offset;
	log<2>("data_offset = %zd\n", data_offset);
	for (size_t i = 0; i < data_section.size(); ++i) {
		m[offset++] = data_section[i];
	}
	for (size_t i = 0; i < reloc_table.size(); ++i) {
		log<2>("relocate: 0x%08X, 0x%08X", reloc_table[i], m[reloc_table[i]]);
		m[reloc_table[i]] += data_offset;
		log<2>(" => 0x%08X\n", m[reloc_table[i]]);
	}
	size_t ext_offset = offset;
	log<2>("ext_offset = %zd\n", ext_offset);
	for (size_t i = 0; i < ext_table.size(); ++i) {
		log<2>("external: 0x%08X, 0x%08X", ext_table[i], m[ext_table[i]]);
		m[ext_table[i]] += ext_offset;
		log<2>(" => 0x%08X\n", m[ext_table[i]]);
	}
	log<2>("\n");

	// prepare stack
	m[--sp] = EXIT; // the last code (at the bottom of stack)
	int t = sp;
	m[--sp] = argc; // prepare stack for main() return
	m[--sp] = static_cast<int>(reinterpret_cast<size_t>(argv)); // TODO: fix truncated
	m[--sp] = t;

	size_t cycle = 0;
	for (;;) {
		++cycle;
		if (verbose >= 1) {
			log("%zd:\t", cycle);
			print_code(m, ip, false, data_offset, ext_offset);
			if (verbose >= 2) {
				print_vm_env(ax, ip, sp, bp);
			}
		}
		size_t i = m[ip++];

		if      (i == EXIT) { break;            } // exit the program

		else if (i == PUSH) { m[--sp] = ax;     } // push ax to stack
		else if (i == POP ) { ax = m[sp++];     } // pop ax from stack

		else if (i == MOV ) { ax = m[ip++];     } // move immediate to ax
		else if (i == LEA ) { ax = m[ip++];     } // load address to ax
		else if (i == GET ) { ax = m[m[ip++]];  } // get memory to ax
		else if (i == PUT ) { m[m[ip++]] = ax;  } // put ax to memory

		else if (i == ADD ) { ax += m[ip++];    } // add number to ax
		else if (i == SUB ) { ax -= m[ip++];    } // substract number from ax
		else if (i == MUL ) { ax *= m[ip++];    } // multiply ax by number
		else if (i == DIV ) { ax /= m[ip++];    } // devide ax by number
		else if (i == MOD ) { ax %= m[ip++];    } // devide ax and get mode

		else if (i == ADDM) { ax += m[m[ip++]]; } // add number to ax
		else if (i == SUBM) { ax -= m[m[ip++]]; } // substract number from ax
		else if (i == MULM) { ax *= m[m[ip++]]; } // multiply ax by number
		else if (i == DIVM) { ax /= m[m[ip++]]; } // devide ax by number
		else if (i == MODM) { ax %= m[m[ip++]]; } // devide ax and get mode

		else if (i == ENTER) { m[--sp] = bp; bp = sp; sp = sp - m[ip++];   } // enter subroutine
		else if (i == LEAVE) { sp = bp; bp = m[sp++]; ip = m[sp++];        } // leave subroutine
		else if (i == CALL ) { m[--sp] = ip + 1; ip = m[ip];               } // call subroutine
		else if (i == SYS  ) { ax = system_call(m[ip++], sp, data_offset, ext_offset); } // system call

		else { warn("unknown instruction: '%zd'\n", i); }
	}
	log<1>("Total: %zd cycle(s), return %d\n", cycle, ax);
	return m[sp];
}

int main(int argc, const char** argv)
{
	bool assembly = false;
	const char* filename = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-') {
			if (argv[i][1] == 'v') { ++verbose; }
			if (argv[i][1] == 's') { assembly = true; }
		} else {
			filename = argv[i];
		}
	}
	if (!filename) {
		log("usage: icpp [-s] [-v] <foo.cpp> ...\n");
		return false;
	}
	if (!load(filename) || !parse()) return -1;
	return assembly ? show() : run(argc, argv);
}
