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
	EXIT,  PUSH,  POP,
	ADD,   SUB,   MUL,  DIV, MOD,
	MOV,   LEA,   GET,  PUT, LLEA, LGET, LPUT,
	ENTER, LEAVE, CALL, RET
};

const char* machine_code_name[] = {
	"EXIT",  "PUSH",  "POP",
	"ADD",   "SUB",   "MUL",  "DIV", "MOD",
	"MOV",   "LEA",   "GET",  "PUT", "LLEA", "LGET", "LPUT",
	"ENTER", "LEAVE", "CALL", "RET",
};

inline bool machine_code_has_parameter(int code)
{
	return (code == MOV ||
			code == LEA || code == GET || code == PUT ||
			code == LLEA || code == LGET || code == LPUT ||
			code == ENTER || code == CALL || code == RET);
}

//--------------------------------------------------------//
// operator precedence in c/c++
// ref: https://en.cppreference.com/w/cpp/language/operator_precedence

unordered_map<string, int> operator_precedence = {
	{ "::", 1 },

	// suffix/postfix
	//{ "++", 2 }, { "--", 2 }, { "{", 2 }, { "[", 2 }, { ".", 2 }, { "->", 2 },

	// prefix
	{ "++", 3 }, { "--", 3 }, /*{ "+", 3 }, { "-", 3 },*/
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
// global variables

enum token_type { unknown = 0, symbol, number, text, op };
const char* token_type_text[] = { "unknown", "symbol", "number", "text", "op" };

vector<string> src;
const char* p = nullptr; // position of source code parsing
size_t line_no = 0;
token_type type = unknown;
string token;

vector<pair<string, string>> scopes; // [ < type, name > ]
unordered_set<string> returned_functions;

vector<int> code_sec;
vector<int> data_sec;

// [ { offset-of-instru, [ symbol-name => { offset-in-stack-frame, size, type } ] } ]
vector<pair<int, unordered_map<string, tuple<int, int, string>>>> stack_frame_table;
vector<int> reloc_table;
vector<int> ext_table;
unordered_map<size_t, string> comments;

enum symbol_type { data_symbol = 0, code_symbol, ext_symbol };
const char* symbol_type_text[] = { "data", "code", "external" };
unordered_map<string, tuple<symbol_type, size_t, size_t, string, string, int>> symbols; // name => { symbol_type, offset, size, type, ret_type, arg_count }
unordered_map<size_t, string> offset_to_symbol[3];
unordered_map<string, unordered_set<string>> override_functions;

unordered_map<size_t, pair<size_t, size_t>> offset; // line_no => [ offset_start, offset_end ]

tuple<string, string, string, int> current_function; // name, arg_types, ret_type, arg_count

size_t ext_symbol_counter = 0;

size_t next_display_source_code = 0;
size_t next_display_machine_code = 0;

//--------------------------------------------------------//

void print_source_code_line(size_t n)
{
	log("%4zd ", n + 1);
	for (const char* p = src[n].c_str(); *p; ++p) {
		if (*p == '\t') log("    "); else log("%c", *p);
	}
	log("\n");
}

void print_current(bool with_source_code = true)
{
	if (with_source_code) {
		print_source_code_line(line_no - 1);
	}
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
		size_t offset, size_t size, string type, string ret_type, int arg_count)
{
	log<3>("[DEBUG] add %s symbol: '%s', offset=%zd, size=%zd, type='%s', ret_type='%s', arg_count = %d\n",
			symbol_type_text[stype], name.c_str(),
			offset, size, type.c_str(), ret_type.c_str(), arg_count);
	symbols[name] = make_tuple(stype, offset, size, type, ret_type, arg_count);
	if (offset_to_symbol[stype].find(offset) != offset_to_symbol[stype].end()) {
		print_error("offset %zd has already existsed! existed '%s', now adding '%s'\n",
				offset, offset_to_symbol[stype][offset].c_str(), name.c_str());
	}
	offset_to_symbol[stype][offset] = name;
}

size_t add_const_string(string name, vector<int> val, string type)
{
	size_t offset = data_sec.size();
	add_symbol(name, data_symbol, offset, val.size(), type, "", 0);
	data_sec.insert(data_sec.end(), val.begin(), val.end());
	return offset;
}

auto add_variable(string name, vector<int> val, string type) -> pair<bool, size_t> // { is-global, offset }
{
	log<3>("[DEBUG] add variable '%s', type = '%s'\n", name.c_str(), type.c_str());
	if (stack_frame_table.empty()) {
		size_t offset = data_sec.size();
		add_symbol(name, data_symbol, offset, val.size(), type, "", 0);
		data_sec.insert(data_sec.end(), val.begin(), val.end());
		return make_pair(true, offset);
	} else {
		size_t stack_frame_offset = stack_frame_table.back().first;
		size_t symbol_offset = code_sec[stack_frame_offset] + 1;
		code_sec[stack_frame_offset] += val.size();
		stack_frame_table.back().second.insert(make_pair(name, make_tuple(-symbol_offset, val.size(), type)));
		return make_pair(false, -symbol_offset);
	}
}

void print_stack_frame()
{
	log("[DEBUG] current stack frame has %zd variable\n", stack_frame_table.back().second.size());
	size_t i = 0;
	for (auto it = stack_frame_table.back().second.begin();
			it != stack_frame_table.back().second.end();
			++it) {
		auto [ offset, size, type ] = it->second;
		log("[DEBUG] [%zd] '%s': offset=%d, size=%d, type='%s'\n", i++, it->first.c_str(), offset, size, type.c_str());
	}
}

void add_argument(string name, string type, size_t offset)
{
	log<3>("[DEBUG] add argument '%s', type = '%s'\n", name.c_str(), type.c_str());
	auto& stack_frame = stack_frame_table.back().second;
	stack_frame.insert(make_pair(name, make_tuple(2 + offset, 1, type)));
	if (verbose >= 4) print_stack_frame();
}

void add_code_symbol(string name, string args_type, string ret_type, int arg_count)
{
	override_functions[name].insert(name + args_type);
	add_symbol(name + args_type, code_symbol, code_sec.size(), 0, args_type, ret_type, arg_count);
}

void add_external_symbol(string name, string args_type, string ret_type = "", int arg_count = 0)
{
	size_t offset = ++ext_symbol_counter;
	string name2 = name + (ret_type.empty() ? "" : args_type);
	override_functions[name].insert(name2);
	add_symbol(name2, ext_symbol, offset, 0, args_type, ret_type, arg_count);
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

size_t print_code(const vector<int>& mem, size_t ip, int data_offset, int ext_offset)
{
	size_t code_offset = ip;
	log(COLOR_YELLOW "%zd\t" COLOR_BLUE, ip);
	size_t i = mem[ip++];
	if (i <= RET) {
		log("%s", machine_code_name[i]);
	} else {
		log("<invalid-code> (0x%08zX)", i);
	}
	if (machine_code_has_parameter(i)) {
		bool is_reloc = (binary_search(reloc_table.begin(), reloc_table.end(), ip));
		bool is_ext = (binary_search(ext_table.begin(), ext_table.end(), ip));
		int v = mem[ip++];
		if (is_reloc || is_ext) log(is_reloc ? COLOR_GREEN : COLOR_RED);
		log("\t0x%08zX (%d)", v, v);
		if (is_reloc || is_ext) log(COLOR_BLUE);

		auto it = comments.find(code_offset);
		if (it != comments.end()) {
			log("\t; %s", it->second.c_str());
		}
	}
	log(COLOR_NORMAL "\n");
	return ip;
}

size_t add_assembly_code(machine_code action, int param = 0,
		bool relocate = false, bool extranal = false, string comment = "")
{
	auto it = offset.find(line_no);
	if (it == offset.end()) {
		offset.insert(make_pair(line_no, make_pair(code_sec.size(), code_sec.size())));
	} else {
		it->second.second = code_sec.size();
	}
	size_t code_offset = code_sec.size();
	code_sec.push_back(action);
	if (machine_code_has_parameter(action)) {
		size_t addr_offset = code_sec.size();
		if (relocate) reloc_table.push_back(addr_offset);
		if (extranal) ext_table.push_back(addr_offset);
		if (action == CALL) {
			param -= code_sec.size() + 1; // use relative address
		}
		code_sec.push_back(param);
	}
	if (!comment.empty()) {
		comments.insert(make_pair(code_offset, comment));
	}
	if (verbose >= 3) {
		if (next_display_machine_code < code_sec.size()) {
			log(COLOR_BLUE);
			while (next_display_machine_code < code_sec.size()) {
				next_display_machine_code = print_code(code_sec, next_display_machine_code, 0, 0);
			}
			log(COLOR_NORMAL);
		}
	}
	return code_offset;
}

void next()
{
	bool in_comment = false;
retry:
	if (!p || !*p) {
		if (line_no >= src.size()) { token = ""; type = unknown; goto end; } // end of source code
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
		for (const char** q = ops; *q; ++q) { size_t l = strlen(*q); if (memcmp(p, *q, l) == 0) { type = op; token = *q; p += l; goto end; } }
		type = unknown; token = *p++;
	}
end:
	if (verbose >= 3) {
		if (next_display_source_code < line_no) {
			log("[DEBUG] parsing source code (up to line %zd):\n" COLOR_GREEN, line_no);
			while (next_display_source_code < line_no) {
				print_source_code_line(next_display_source_code++);
			}
			log(COLOR_YELLOW);
			print_current(false);
			log(COLOR_NORMAL);
		}
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
	string s; for (auto e : a) s += (s.empty() ? "" : ",") + e; return "[" + s + "]";
}

string code_vector_to_string(const vector<pair<token_type, string>>& a)
{
	string s; for (auto e : a) s += (s.empty() ? "" : "','") + e.second; return "['" + s + "']";
}

int eval_number(string s) // TODO: support other number types
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

auto query_function(string name, vector<string>& arg_types) -> tuple<size_t, string, symbol_type> // offset, arg_types, stype
{
	auto it = override_functions.find(name);
	if (it == override_functions.end()) {
		print_error("function '%s' not defined!\n", name.c_str());
	}
	string type_name;
	for (auto e : arg_types) type_name += (type_name.empty() ? "" : ",") + e;
	for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
		if (*it2 == name + "(" + type_name + ")") {
			auto it3 = symbols.find(*it2);
			if (it3 == symbols.end()) {
				print_error("unexpected runtime error!\n");
			}
			auto [ stype, offset, size, type_name, ret_type, arg_count ] = it3->second;
			return make_tuple(offset, ret_type, stype);
		}
	}
	print_error("function '%s' not matched!\n", name.c_str());
	exit(1);
}

auto query_symbol(string s) -> tuple<bool, size_t, string, symbol_type> // { is_global, offset, name, stype }
{
	log<4>("[DEBUG] query symbol: '%s'\n", s.c_str());
	if (verbose >= 4) print_stack_frame();

	bool found = false;
	bool is_global = false;
	size_t offset = 0;
	string type_name;
	symbol_type stype;
	for (size_t i = stack_frame_table.size(); i > 0; --i) {
		auto& stack_frame = stack_frame_table[i - 1].second;
		auto it = stack_frame.find(s);
		if (it != stack_frame.end()) {
			found = true;
			is_global = false;
			offset = get<0>(it->second);
			type_name = get<2>(it->second);
			break;
		}
	}
	if (!found) {
		is_global = true;
		auto it = symbols.find(s);
		if (it == symbols.end()) {
			auto it2 = override_functions.find(s);
			if (it2 == override_functions.end() || it2->second.empty()) {
				print_error("unknown symbol '%s'!\n", s.c_str());
			}
			if (it2->second.size() > 1) {
				print_error("undetermined override symbol '%s'!\n", s.c_str());
			}
			it = symbols.find(*(it2->second.begin()));
			if (it == symbols.end()) {
				print_error("unknown symbol '%s'!\n", s.c_str());
			}
		}
		auto [ stype_2, offset_2, size, t, ret_type, arg_count ] = it->second;
		stype = stype_2;
		offset = offset_2;
		if (!ret_type.empty()) {
			type_name = "(*)" + t;
		} else {
			type_name = t;
		}
	}
	log<3>("[DEBUG] symbol '%s': type='%s', offset=%zd, type='%s', style='%s'\n",
			s.c_str(), (is_global ? "global" : "local"), offset,
			type_name.c_str(), symbol_type_text[stype]);
	return make_tuple(is_global, offset, type_name, stype);
}

void build_code_for_op(string op_name, vector<string>& type_names)
{
	if (type_names.size() < 2) {
		print_error("unexpected array size of type_names!\n");
	}
	string b_type = type_names.back(); type_names.pop_back();
	string a_type = type_names.back(); type_names.pop_back();
	if (a_type == "int" && b_type == "int") {
		if (op_name == "+") {
			add_assembly_code(ADD);
		} else if (op_name == "-") {
			add_assembly_code(SUB);
		} else if (op_name == "*") {
			add_assembly_code(MUL);
		} else if (op_name == "/") {
			add_assembly_code(DIV);
		} else if (op_name == "%") {
			add_assembly_code(MOD);
		} else {
			print_error("Unsupported operator '%s'\n", op_name.c_str());
		}
		type_names.push_back("int");
	} else {
		string name = "operator" + op_name + "(" + a_type + "," + b_type + ")";
		auto it = symbols.find(name);
		if (it == symbols.end()) {
			print_error("Unknown function '%s'\n", name.c_str());
		}
		auto [ stype, offset, size, type_name, ret_type, arg_count ] = it->second;
		if (stype != code_symbol && stype != ext_symbol) {
			print_error("symbol '%s' is not a function!\n", name.c_str());
		}
		add_assembly_code(PUSH);
		add_assembly_code(CALL, offset, false, (stype == ext_symbol), ret_type + " " + name);
		type_names.push_back(ret_type);
	}
}

string parse_expression(bool before_comma = false)
{
	log<3>("[DEBUG] > %s:\n", __FUNCTION__);

	vector<string> stack{"#"};
	vector<string> type_names;
	string last_name = "";
	while (!token.empty()) {
		log<4>("> token='%s', type=%s, stack=%s, type_names=%s\n",
				token.c_str(), token_type_text[type],
				vector_to_string(stack).c_str(), vector_to_string(type_names).c_str());
		if (type == number) {
			if (!type_names.empty()) add_assembly_code(PUSH);
			int v = eval_number(token);
			add_assembly_code(MOV, v);
			type_names.push_back("int");
			next();
		} else if (type == text) {
			if (!type_names.empty()) add_assembly_code(PUSH);
			string v = eval_string(token);
			auto mem = prepare_string(v);
			string name = alloc_name();
			string type_name = "const char*";
			size_t offset = add_const_string(name, mem, type_name);
			add_assembly_code(MOV, offset, true, false, name + "\t" + type_name);
			type_names.push_back("const char*");
			next();
		} else if (type == symbol) {
			if (!type_names.empty()) add_assembly_code(PUSH);
			string name = token;
			next();
			while (token == "::") {
				name += token; next();
				if (type != symbol) { print_error("unexpected token after '::'!\n"); }
				name += token; next();
			}
			if (token == "(") {
				// TODO: function calling
				log<3>("[DEBUG] call function '%s'\n", name.c_str());
				next();
				vector<string> arg_types;
				if (token != ")") {
					for (;;) {
						string type = parse_expression(true);
						arg_types.push_back(type);
						add_assembly_code(PUSH, 0, false, false, "");
						if (token == ")") break;
						expect_token(",", "function '" + name + "'");
						next();
					}
				}
				expect_token(")", "function '" + name + "'");
				next();
				log<3>("[DEBUG] function '%s' has %zd args\n", name.c_str(), arg_types.size());
				auto [ offset, ret_type, stype ] = query_function(name, arg_types);
				string type_name;
				for (auto e : arg_types) type_name += (type_name.empty() ? "" : ",") + e;
				add_assembly_code(CALL, offset, false, (stype == ext_symbol),
						ret_type + " " + name + "(" + type_name + ")");
				type_names.push_back(ret_type);
				log<3>("[DEBUG] ret_type = '%s'\n", ret_type.c_str());
			} else if (token == "++" || token == "--") {
				// TODO: operator++ | operator--
				next();
			} else if (token == "{") {
				// TODO: initializer
				skip_until("}", "");
			} else if (token == "[") {
				// TODO: array
				skip_until("]", "");
			} else if (token == "." || token == "->") {
				// TODO: find member
				next();
			} else if (token == ".*" || token == "->*") {
				// TODO: find member
				next();
			} else {
				auto [ is_global, offset, type_name, stype ] = query_symbol(name);
				bool is_reloc = (stype == data_symbol);
				bool is_ext = (stype == ext_symbol);
				if (is_global) {
					if (type_name == "int") {
						add_assembly_code(GET, offset, is_reloc, is_ext, name + "\t" + type_name);
					} else {
						add_assembly_code(LEA, offset, is_reloc, is_ext, name + "\t" + type_name);
					}
				} else {
					if (type_name == "int") {
						add_assembly_code(LGET, offset, false, false, name + "\t" + type_name);
					} else {
						add_assembly_code(LLEA, offset, false, false, name + "\t" + type_name);
					}
				}
				type_names.push_back(type_name);
				last_name = name;
			}
		} else if (token == ";") {
			break;
		} else if (token == "(") {
			stack.push_back(token);
			next();
		} else if (token == ")") {
			while (stack.back() != "(" && stack.back() != "#") {
				build_code_for_op(stack.back(), type_names);
				stack.pop_back();
			}
			if (stack.back() == "#") {
				break;
			}
			stack.pop_back(); // pop out "("
			next();
		} else {
			if (token == "," && before_comma) break;
			log<4>("[DEBUG] compare op in parse_expression: '%s' => %d, '%s' => %d\n",
					token.c_str(), precedence(token),
					stack.back().c_str(), precedence(stack.back()));
			while (precedence(token) >= precedence(stack.back()) &&
					stack.back() != "#") {
				build_code_for_op(stack.back(), type_names);
				stack.pop_back();
			}
			stack.push_back(token);
			next();
		}
	}
	while (stack.back() != "#") {
		build_code_for_op(stack.back(), type_names);
		stack.pop_back();
	}
	if (type_names.size() != 1) {
		print_error("Unexpected type_names size: %zd!\n", type_names.size());
	}
	return type_names.back();
}

void parse_init_statement()
{
	log<3>("[DEBUG] > %s:\n", __FUNCTION__);

	string type_name = parse_type_name();
	string type_prefix = type_name;
	string name = token; next();
	if (verbose >= 3) {
		log("[DEBUG] => function/variable '%s', type='%s'\n",
				name.c_str(), type_name.c_str());
	}
	if (token == "(") { // function
		if (!scopes.empty() && scopes.back().first != "function") {
			print_error("nesting function is not allowed!\n");
		}
		next();
		vector<pair<string, string>> args; // [ { type, name } ]
		if (token != ")") {
			for (;;) {
				string arg_type_name = parse_type_name();
				string arg_name = token; next();
				args.push_back(make_pair(arg_type_name, arg_name));
				if (token != ",") break;
				next();
			}
		}
		expect_token(")", "function " + name);
		string args_type = "(";
		for (size_t i = 0; i < args.size(); ++i) {
			args_type += (i == 0 ? "" : ",") + args[i].first;
		}
		args_type += ")";
		add_code_symbol(name, args_type, type_name, args.size());
		scopes.push_back(make_pair("function", name));
		current_function = make_tuple(name, args_type, type_name, args.size());
		next();
		expect_token("{", "function '" + name + "', '" + name + type_name + "'");
		size_t offset = add_assembly_code(ENTER);
		stack_frame_table.push_back(make_pair(offset + 1, unordered_map<string, tuple<int, int, string>>()));
		for (size_t i = 0; i < args.size(); ++i) {
			add_argument(args[i].second, args[i].first, i);
		}
		next();
	} else { // variable
		for (;;) {
			vector<int> init(1);
			if (token == "=") {
				next();
				parse_expression(true);
				auto [ is_global, offset ] = add_variable(name, init, type_name);
				if (is_global) {
					add_assembly_code(PUT, offset, true, false, name + "\t" + type_name);
				} else {
					add_assembly_code(LPUT, offset, false, false, name + "\t" + type_name);
				}
			}
			if (token != ",") break;
			next();
			type_name = type_prefix;
			while (token == "*" || token == "&") {
				type_name += token;
				next();
			}
			name = token; next();
			if (verbose >= 3) {
				log("[DEBUG] => another variable '%s', type='%s'\n",
						name.c_str(), type_name.c_str());
			}
		}
		expect_token(";", "variable");
		next();
	}
}

void parse_statements()
{
	log<3>("[DEBUG] > %s:\n", __FUNCTION__);
	if (token == "{") {
		log<3>("[DEBUG] => statement '{'\n");
		next(); while (!token.empty() && token != "}") parse_statements();
		expect_token("}", "{");
		next();
	} if (token == "if") {
		log<3>("[DEBUG] => statement 'if'\n");
		next(); expect_token("(", "if");
		next(); parse_expression(); expect_token(")", "if");
		next(); parse_statements();
		if (token == "else") {
			next();
			parse_statements();
		}
	} else if (token == "for") {
		log<3>("[DEBUG] => statement 'for'\n");
		next(); expect_token("(", "for");
		next(); parse_init_statement(); expect_token(";", "for");
		next(); parse_expression(); expect_token(";", "for");
		next(); parse_expression(); expect_token(")", "for");
		expect_token(")", "for");
		next(); parse_statements();
	} else if (token == "while") {
		log<3>("[DEBUG] => statement 'while'\n");
		next(); expect_token("(", "while");
		next(); parse_expression(); expect_token(")", "while");
		next(); parse_statements();
	} else if (token == "do") {
		log<3>("[DEBUG] => statement 'do'\n");
		next(); expect_token("{", "do");
		parse_statements();
		expect_token("while", "do");
		next(); expect_token("(", "do");
		next(); parse_expression();
		next(); expect_token(")", "do");
		next(); expect_token(";", "do");
	} else if (token == "return") {
		log<3>("[DEBUG] => statement 'return'\n");
		next();
		if (scopes.empty() || scopes.back().first != "function") {
			print_error("unexpected 'return' statement!\n");
		}
		string name = scopes.back().second;
		if (token != ";") {
			parse_expression();
		}
		expect_token(";", "return");
		add_assembly_code(LEAVE);
		add_assembly_code(RET, get<3>(current_function));
		next();
		returned_functions.insert(get<0>(current_function));
	} else if (token == "typedef") {
		log<3>("[DEBUG] => statement 'typedef'\n");
		skip_until(";", "typedef");
		next();
	} else if (token == "auto" || token == "const" || token == "static" ||
			token == "extern" || is_built_in_type()) { // start as type
		parse_init_statement();
	} else {
		parse_expression();
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

bool load(string filename)
{
	ifstream file(filename);
	if (!file.is_open()) {
		err("failed to open file '%s'!\n", filename.c_str());
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
	log<2>("[INFO] prepare external symbols\n");
	add_external_symbol("cout", "ostream");
	add_external_symbol("cerr", "ostream");
	add_external_symbol("endl", "(endl_t)", "void");
	add_external_symbol("operator<<", "(ostream,int)",         "ostream");
	add_external_symbol("operator<<", "(ostream,double)",      "ostream");
	add_external_symbol("operator<<", "(ostream,const char*)", "ostream");
	add_external_symbol("operator<<", "(ostream,(*)(endl_t))",   "ostream");
	log<1>("[INFO] total %zd symbols are prepared\n", symbols.size());
	if (verbose >= 2) {
		size_t i = 0;
		for (auto it = symbols.begin(); it != symbols.end(); ++it) {
			auto name = it->first;
			auto [ stype, offset, size, type_name, ret_type, arg_count ] = it->second;
			log("[INFO] [%zd] '%s': type='%s', offset=%zd, size=%zd, type='%s', ret_type='%s', arg_count\n",
					i++, name.c_str(), symbol_type_text[stype], offset, size, type_name.c_str(), ret_type.c_str(), arg_count);
		}
	}
}

string get_scopes_text()
{
	string s; for (auto e : scopes) s += (s.empty() ? "" : "::") + e.second; return s;
}

void parse()
{
	init_symbol();

	for (next(); !token.empty();) {
		if (token == "using") {
			next(); while (!token.empty() && token != ";") next();
			if (token.empty()) { print_error("missing ';' for 'using'!\n"); }
			log<2>("[INFO] => 'using' statement skipped\n");
			next();
		} else if (token == "typedef") {
			skip_until(";", token);
			log<2>("[INFO] => 'typedef' statement skipped\n");
			next();
		} else if (token == "enum") {
			parse_enum();
		} else if (token == "union" || token == "struct" || token == "class" || token == "namespace") {
			string keyword = token;
			next();
			string name = token;
			next();
			expect_token("{", keyword + " " + name);
			scopes.push_back(make_pair(keyword, name));
			log<2>("[INFO] => (%s %s) start\n", keyword.c_str(), name.c_str());
			next();
		} else if (token == "template") {
			skip_until(";", token);
			log<2>("[INFO] => 'template' statement skipped\n");
			next();
		} else if (token == ";") {
			log<2>("[INFO] => ';' - end of statement\n");
			next();
		} else if (token == "}") {
			string scope_type;
			string scope_name;
			if (!scopes.empty()) {
				scope_type = scopes.back().first;
				scope_name = scopes.back().second;
				scopes.pop_back();
			}
			if (scope_type == "function") {
				if (returned_functions.find(scope_name) == returned_functions.end()) {
					add_assembly_code(LEAVE);
					add_assembly_code(RET, get<3>(current_function));
				}
				current_function = make_tuple("", "", "", 0);
			}
			log<2>("[INFO] => '}' - end of block (%s,%s)\n",
					scope_type.c_str(), scope_name.c_str());
			stack_frame_table.pop_back();
			next();
		} else {
			parse_statements();
		}
	}
}

int show()
{
	for (size_t i = 0; i < src.size(); ++i) {
		print_source_code_line(i);
		auto it = offset.find(i + 1);
		if (it != offset.end()) {
			log(COLOR_BLUE);
			for (size_t j = it->second.first; j <= it->second.second;) {
				j = print_code(code_sec, j, 0, 0);
			}
			log(COLOR_NORMAL);
		}
	}
	log("\n");

	for (size_t i = 0; i < data_sec.size(); ++i) {
		auto it = offset_to_symbol[data_symbol].find(i);
		if (it != offset_to_symbol[data_symbol].end()) {
			auto name = it->second;
			auto [ stype, offset, size, type, ret_type, arg_count ] = symbols[name];
			string data_type = "word";
			if (type == "const char*") data_type = "byte";
			log(COLOR_YELLOW "%d", i);
			log(COLOR_BLUE "\t.%s\t", data_type.c_str());
			if (type == "const char*") {
				log("\"");
				const char* s = reinterpret_cast<const char*>(&data_sec[offset]);
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
					log("0x%08X", static_cast<unsigned int>(data_sec[offset + i]));
				}
			}
			log("\t; %s", name.c_str());
			log("\t%s", type.c_str());
			log(COLOR_NORMAL "\n");
		}
	}

	log<1>("relocate table (%zd items):\n", reloc_table.size());
	for (size_t i = 0; i < reloc_table.size(); ++i) {
		log<1>("\t0x%08x", reloc_table[i]);
		if (i % 5 == 4) log<1>("\n");
	}
	if (reloc_table.size() % 5 != 0) log<1>("\n");

	log<1>("external table (%zd items):\n", ext_table.size());
	for (size_t i = 0; i < ext_table.size(); ++i) {
		log<1>("\t0x%08x", ext_table[i]);
		if (i % 5 == 4) log<1>("\n");
	}
	if (ext_table.size() % 5 != 0) log<1>("\n");
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
	for (size_t i = 0; i < 10 && bp != MEM_SIZE; ++i) {
		log("\t[#%zd backtrace]: %08X\n", i, bp);
		if (bp == m[bp]) break;
		bp = m[bp];
	}
	if (i == 10) {
		log("...\n");
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
	log<2>("offset_to_symbol[%d].size() = %zd\n", stype, offset_to_symbol[stype].size());
	for (auto it = offset_to_symbol[stype].begin(); it != offset_to_symbol[stype].end(); ++it) {
		log<2>("offset_to_symbol[%s]: %zd => '%s'\n", symbol_type_text[stype], it->first, it->second.c_str());
	}
	auto it = offset_to_symbol[stype].find(original_offset);
	if (it == offset_to_symbol[stype].end()) {
		err("Unknown symbol offset '%zd' (stype = '%s', original_offset = %zd)\n",
				offset, symbol_type_text[stype], original_offset);
		exit(1);
	}
	return it->second;
}

auto call_ext(const string& name, int sp, size_t data_offset, size_t ext_offset) -> pair<int, int> // [ ax, RET <n> ]
{
	log<3>("[DEBUG] external call: %s\n", name.c_str());
	if (name == "operator<<(ostream,int)") {
		int b = m[sp + 1];
		int a = m[sp + 2];
		log<2>("[DEBUG] args: %d, %d\n", a, b);
		string aa = get_symbol(data_symbol, a, data_offset, ext_offset);
		if (aa == "cout") {
			cout << b;
		} else if (aa == "cerr") {
			cerr << b;
		} else {
			err("Unsupported operator<< for %d('%s')\n", m[a], aa.c_str());
			exit(1);
		}
		return make_pair(a, 2);
	} else if (name == "operator<<(ostream,const char*)") {
		int b = m[sp + 1];
		int a = m[sp + 2];
		log<2>("[DEBUG] args: %d, %d\n", a, b);
		string aa = get_symbol(data_symbol, a, data_offset, ext_offset);
		const char* s = reinterpret_cast<const char*>(&m[b]);
		if (aa == "cout") {
			cout << s;
		} else if (aa == "cerr") {
			cerr << s;
		} else {
			err("Unsupported operator<< for %d('%s')\n", m[a], aa.c_str());
			exit(1);
		}
		return make_pair(a, 2);
	} else if (name == "operator<<(ostream,(*)(endl_t))") {
		int b = m[sp + 1];
		int a = m[sp + 2];
		log<2>("[DEBUG] args: %d, %d\n", a, b);
		string aa = get_symbol(data_symbol, a, data_offset, ext_offset);
		if (aa == "cout") {
			cout << endl;
		} else if (aa == "cerr") {
			cerr << endl;
		} else {
			err("Unsupported operator<< for %d('%s')\n", m[a], aa.c_str());
			exit(1);
		}
		return make_pair(a, 2);
	} else {
		err("Unsupported function '%s'\n", name.c_str());
		exit(1);
	}
}

int run(int argc, const char** argv)
{
	// vm register
	int ax = 0, ip = 0, sp = MEM_SIZE, bp = MEM_SIZE;

	// load code & data
	const int loading_position = 0;
	log<1>("Loading program\n  code: %zd word(s)\n  data: %zd word(s)\n\n",
			code_sec.size(), data_sec.size());

	size_t offset = loading_position;
	for (size_t i = 0; i < code_sec.size(); ++i) {
		m[offset++] = code_sec[i];
	}
	size_t data_offset = offset;
	log<2>("data_offset = %zd\n", data_offset);
	for (size_t i = 0; i < data_sec.size(); ++i) {
		m[offset++] = data_sec[i];
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

	// find start entry
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
	ip = loading_position + get<1>(it2->second);

	// prepare stack
	m[--sp] = EXIT; // the last code (at the bottom of stack)
	int t = sp;
	m[--sp] = argc; // prepare stack for main() return
	m[--sp] = static_cast<int>(reinterpret_cast<size_t>(argv)); // TODO: fix truncated
	m[--sp] = t;

	log<1>("System Information:\n"
			"  sizeof(int) = %zd\n"
			"  sizeof(void*) = %zd\n"
			"\n", sizeof(int), sizeof(void*));

	size_t cycle = 0;
	for (;;) {
		++cycle;
		if (verbose >= 1) {
			log("%zd:\t", cycle);
			print_code(m, ip, data_offset, ext_offset);
			if (verbose >= 2) {
				print_vm_env(ax, ip, sp, bp);
			}
		}
		size_t i = m[ip++];

		if      (i == EXIT) { break;                } // exit the program

		else if (i == PUSH) { m[--sp] = ax;         } // push ax to stack
		else if (i == POP ) { ax = m[sp++];         } // pop ax from stack

		else if (i == MOV ) { ax = m[ip++];         } // move immediate to ax
		else if (i == LEA ) { ax = m[ip++];         } // load address to ax
		else if (i == GET ) { ax = m[m[ip++]];      } // get memory to ax
		else if (i == PUT ) { m[m[ip++]] = ax;      } // put ax to memory
		else if (i == LLEA) { ax = bp - m[ip++];    } // load local address to ax
		else if (i == LGET) { ax = m[bp + m[ip++]]; } // get local to ax
		else if (i == LPUT) { m[bp + m[ip++]] = ax; } // put ax to local

		else if (i == ADD ) { ax = m[sp++] + ax;    } // stack (top) + a, and pop out
		else if (i == SUB ) { ax = m[sp++] - ax;    } // stack (top) - a, and pop out
		else if (i == MUL ) { ax = m[sp++] * ax;    } // stack (top) * a, and pop out
		else if (i == DIV ) { ax = m[sp++] / ax;    } // stack (top) / a, and pop out
		else if (i == MOD ) { ax = m[sp++] % ax;    } // stack (top) % a, and pop out

		else if (i == ENTER) { m[--sp] = bp; bp = sp; sp -= m[ip++]; } // enter stack frame
		else if (i == LEAVE) { sp = bp; bp = m[sp++];                } // leave stack frame
		else if (i == CALL ) { m[--sp] = ip + 1; ip += m[ip++];      } // call subroutine
		else if (i == RET  ) { int n = m[ip]; ip = m[sp++]; sp += n; } // exit subroutine

		else { warn("unknown instruction: '%zd'\n", i); }

		if (static_cast<size_t>(ip) >= ext_offset) {
			auto it = offset_to_symbol[ext_symbol].find(ip - ext_offset);
			if (it != offset_to_symbol[ext_symbol].end()) {
				auto [ r, n ] = call_ext(it->second, sp, data_offset, ext_offset);
				ax = r; ip = m[sp++]; sp += n;
			}
		}
	}
	log<0>(COLOR_YELLOW "Total: %zd cycle(s), return %d\n" COLOR_NORMAL, cycle, ax);
	return ax;
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
	if (load(filename)) parse();
	return assembly ? show() : run(argc, argv);
}
