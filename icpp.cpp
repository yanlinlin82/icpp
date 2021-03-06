#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <cstring>
#include <cstdarg>
#include <cassert>
using namespace std;

//--------------------------------------------------------//
// message print

#define COLOR_NORMAL "\x1B[0m"
#define COLOR_RED    "\x1B[31m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_YELLOW "\x1B[33m"
#define COLOR_BLUE   "\x1B[34m"

static int verbose = 0;
static void (*on_err)() = nullptr;

template <int level = 0> inline int log(const char* fmt, va_list ap) { return (verbose < level) ? 0 : vfprintf(stderr, fmt, ap); }
template <int level = 0> inline int log(const char* fmt, ...) { va_list ap; va_start(ap, fmt); return log<level>(fmt, ap); }

inline void err(const char* fmt, ...) { va_list ap; va_start(ap, fmt); log(COLOR_RED "Error: "); log(fmt, ap); log(COLOR_NORMAL); if (on_err) on_err(); }
inline void warn(const char* fmt, ...) { va_list ap; va_start(ap, fmt); log(COLOR_YELLOW "Warning: "); log(fmt, ap); log(COLOR_NORMAL); }

//--------------------------------------------------------//
// instruction definition

enum instruction {
	EXIT,  PUSH,  POP,  ADJ,
	MOV,   LEA,   GET,  PUT, LLEA, LGET, LPUT,
	SGET,  SPUT,
	ADD,   SUB,   MUL,  DIV, MOD,  NEG,  INC,  DEC,
	SHL,   SHR,   AND,  OR,  NOT,
	EQ,    NE,    GE,   GT,  LE,   LT,   LAND, LOR,  LNOT,
	ENTER, LEAVE, CALL, RET, JMP,  JZ,   JNZ,
	INVALID,
};

const char* instruction_name =
	"EXIT  PUSH  POP   ADJ   "
	"MOV   LEA   GET   PUT   LLEA  LGET  LPUT  "
	"SGET  SPUT  "
	"ADD   SUB   MUL   DIV   MOD   NEG   INC   DEC   "
	"SHL   SHR   AND   OR    NOT   "
	"EQ    NE    GE    GT    LE    LT    LAND  LOR   LNOT  "
	"ENTER LEAVE CALL  RET   JMP   JZ    JNZ   ";

inline bool instruction_has_parameter(int code)
{
	return (code == ADJ || code == MOV ||
			code == LEA || code == GET || code == PUT ||
			code == LLEA || code == LGET || code == LPUT ||
			code == ENTER || code == CALL || code == RET ||
			code == JMP || code == JZ || code == JNZ);
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

	{ "(", 99 }, { ")", 99 }, { "]", 99 }, { "}", 99 }, { ";", 99 }
};

//--------------------------------------------------------//
// global variables

const int MEM_SIZE = 1024 * 1024; // 1 MB * sizeof(size_t)
vector<int> m(MEM_SIZE);

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

size_t external_data_size = 0;
size_t external_code_size = 0;

// [ { offset-of-instru, [ symbol-name => { offset-in-stack-frame, size, type } ] } ]
vector<pair<int, unordered_map<string, tuple<int, int, string>>>> stack_frame_table;
unordered_map<size_t, string> comments;

unordered_map<string, tuple<bool, size_t, size_t, string, string, int>> symbols; // name => { is_code, offset, size, type, ret_type, arg_count }
unordered_map<size_t, string> data_symbol_dict; // offset => name
unordered_map<size_t, string> code_symbol_dict; // offset => name
unordered_map<string, unordered_set<string>> override_functions;

unordered_map<string, pair<int, vector<int>>> symbol_dim; // name => [ size, dim ]

unordered_map<size_t, pair<size_t, size_t>> offset; // line_no => [ offset_start, offset_end ]

tuple<string, string, string, int> current_function; // name, arg_types, ret_type, arg_count

size_t ext_symbol_counter = 0;

size_t next_display_source_code = 0;
size_t next_display_instruction = 0;

unordered_map<string, unordered_map<string, int>> enum_values; // enum-name => { name => value }
unordered_map<string, pair<string, int>> enum_types; // name => { enum-name, value }

void dump_enum()
{
	for (auto it = enum_values.begin(); it != enum_values.end(); ++it) {
		log("[DEBUG] enum '%s': [ ", it->first.c_str());
		size_t i = 0;
		for (auto e : it->second) {
			log("%s%s = %d", (++i == 1 ? "" : ", "), e.first.c_str(), e.second);
		}
		log(" ]\n");
	}
	for (auto it = enum_types.begin(); it != enum_types.end(); ++it) {
		log("[DEBUG] '%s' => [ '%s', %d ]\n", it->first.c_str(),
				it->second.first.c_str(), it->second.second);
	}
}

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

void print_current_and_exit()
{
	print_current();
	exit(1);
}

void add_symbol(string name, bool is_code, size_t offset, size_t size, string type, string ret_type, int arg_count)
{
	log<3>("[DEBUG] add %s symbol: '%s', offset=%zd, size=%zd, type='%s', ret_type='%s', arg_count = %d\n",
			(is_code ? "code" : "data"), name.c_str(),
			offset, size, type.c_str(), ret_type.c_str(), arg_count);
	symbols[name] = make_tuple(is_code, offset, size, type, ret_type, arg_count);
	if (is_code) {
		code_symbol_dict.insert(make_pair(offset, name));
	} else {
		data_symbol_dict.insert(make_pair(offset, name));
	}
}

size_t add_const_string(string name, vector<int> val, string type)
{
	size_t offset = data_sec.size();
	add_symbol(name, false, offset, val.size(), type, "", 0);
	data_sec.insert(data_sec.end(), val.begin(), val.end());
	return offset;
}

bool is_global_variable()
{
	return stack_frame_table.empty();
}

auto add_variable(string name, int size, string type) -> pair<bool, size_t> // { is-global, offset }
{
	log<3>("[DEBUG] add variable '%s', size = %d, type = '%s'\n",
			name.c_str(), size, type.c_str());
	if (stack_frame_table.empty()) {
		size_t offset = data_sec.size();
		add_symbol(name, false, offset, size, type, "", 0);
		data_sec.resize(data_sec.size() + size);
		return make_pair(true, offset);
	} else {
		size_t stack_frame_offset = stack_frame_table.back().first;
		code_sec[stack_frame_offset] += size;
		size_t symbol_offset = -code_sec[stack_frame_offset];
		stack_frame_table.back().second.insert(make_pair(name, make_tuple(symbol_offset, size, type)));
		return make_pair(false, symbol_offset);
	}
}

void print_stack_frame()
{
	if (verbose >= 4) {
		log("[DEBUG] current stack frame has %zd variable\n", stack_frame_table.back().second.size());
		size_t i = 0;
		for (auto it = stack_frame_table.back().second.begin();
				it != stack_frame_table.back().second.end();
				++it) {
			auto [ offset, size, type ] = it->second;
			log("[DEBUG] [%zd] '%s': offset=%d, size=%d, type='%s'\n", i++, it->first.c_str(), offset, size, type.c_str());
		}
	}
}

void add_argument(string name, string type, size_t offset)
{
	log<3>("[DEBUG] add argument '%s', type = '%s'\n", name.c_str(), type.c_str());
	auto& stack_frame = stack_frame_table.back().second;
	stack_frame.insert(make_pair(name, make_tuple(offset, 1, type)));
	print_stack_frame();
}

void add_code_symbol(string name, string args_type, string ret_type, int arg_count)
{
	override_functions[name].insert(name + args_type);
	add_symbol(name + args_type, true, code_sec.size(), 0, args_type, ret_type, arg_count);
}

size_t print_code(const vector<int>& mem, size_t ip, size_t code_loading_position = 0)
{
	size_t code_offset = ip;
	log(COLOR_YELLOW "%-10zd" COLOR_BLUE, ip);
	size_t i = mem[ip++];
	if (i < INVALID) {
		log("%-14.6s", &instruction_name[i * 6]);
	} else {
		log("<0x%08zX>  ", i);
	}
	if (instruction_has_parameter(i)) {
		int v = mem[ip++];
		char buf[64];
		snprintf(buf, sizeof(buf), "0x%08X (%d)", v, v);
		log("%-25s", buf);
		auto it = comments.find(code_offset - code_loading_position);
		if (it != comments.end()) {
			log(" ; %s", it->second.c_str());
		} else if (i == CALL || i == JMP || i == JZ || i == JNZ) {
			log(" ; address %d", ip + v);
		}
	}
	log(COLOR_NORMAL "\n");
	return ip;
}

size_t add_assembly_code(instruction code, int param = 0, string comment = "")
{
	auto it = offset.find(line_no);
	if (it == offset.end()) {
		offset.insert(make_pair(line_no, make_pair(code_sec.size(), code_sec.size())));
	} else {
		it->second.second = code_sec.size();
	}
	size_t code_offset = code_sec.size();
	code_sec.push_back(code);
	if (instruction_has_parameter(code)) {
		if (code == CALL || code == JMP || code == JZ || code == JNZ) {
			param -= code_sec.size() + 1; // use relative address
		}
		code_sec.push_back(param);
	}
	if (!comment.empty()) {
		comments.insert(make_pair(code_offset, comment));
	}
	if (verbose >= 3) {
		if (next_display_instruction < code_sec.size()) {
			log(COLOR_BLUE);
			while (next_display_instruction < code_sec.size()) {
				next_display_instruction = print_code(code_sec, next_display_instruction);
			}
			log(COLOR_NORMAL);
		}
	}
	return code_offset;
}

void update_relative_address_here(size_t instrument_offset)
{
	int i = code_sec[instrument_offset];
	assert(i == CALL || i == JMP || i == JZ || i == JNZ);
	code_sec[instrument_offset + 1] = code_sec.size() - (instrument_offset + 2);
}

void add_external_symbol(string name, string args_type, string ret_type = "", int arg_count = 0)
{
	// for variable arguments, arg_count is negative, and the number is fixed arguments.
	// for example
	//   `int printf(const char* fmt, ...)`, arg_count is -1
	//   `int fprintf(FILE*, const char* fmt, ...)`, arg_count is -2
	// in runtime, an integer is appended after variable arguments, specifying their numbers.
	//   the number, as in stdcall calling convention (which means the arguments are pushed into
	//   stack as the order in source code, and this interpreter follows this rule), could be
	//   found as [bp + 1] in subroutine (before ENTER)
	if (ret_type.empty()) { // data
		size_t offset = data_sec.size();
		add_symbol(name, false, offset, 1, args_type, "", 0);
		data_sec.resize(offset + 1);
	} else { // code
		size_t offset = code_sec.size();
		string name_with_args = name + "(" + args_type + ")";
		override_functions[name].insert(name_with_args);
		add_symbol(name_with_args, true, offset, 2, args_type, ret_type, arg_count);
		add_assembly_code(RET, (arg_count >= 0 ? arg_count : 0), ret_type + " " + name_with_args);
	}
}

vector<int> prepare_string(const string& s)
{
	size_t bytes = s.size() + 1;
	size_t size = (bytes + sizeof(int) - 1) / sizeof(int);
	vector<int> a(size);
	memcpy(&a[0], s.c_str(), bytes);
	return a;
}

string alloc_name()
{
	static size_t counter = 0;
	return "@" + to_string(++counter);
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
		err("missing '%s' for '%s'! current token: '%s'\n",
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
	auto it = operator_precedence.find(token);
	if (it == operator_precedence.end()) {
		err("unknown operator '%s'!\n", token.c_str());
	}
	return it->second;
}

string vector_to_string(const vector<string>& a, string sep = ",")
{
	string s; for (auto e : a) s += (s.empty() ? "" : sep) + e; return s;
}

int eval_number(string s) // TODO: support long long and float/double
{
	int n = 0;
	bool minus = false;
	const char* p = s.c_str();
	if (*p == '-') { ++p; minus = true; }
	int base = 10;
	if (*p == '0') {
		++p; base = 8;
		if (*p == 'x') {
			++p; base = 16;
		}
	}
	for (; *p; ++p) {
		if (*p == '.') break;
		if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f'))) {
			err("unexpected character '%c' in number '%s'!", *p, s.c_str());
		}
		int x = *p - (*p >= 'a' ? ('a' - 10) : (*p >= 'A' ? ('A' - 10) : '0'));
		if (x >= base) err("Invalid number '%s'!", s.c_str());
		n = n * base + x;
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
			token == "signed" || token == "unsigned" ||
			token == "size_t");
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
				if (--angle_bracket < 0) { err("unexpected '>'!\n"); }
				a.push_back(make_pair(type, token));
				next();
			} else if (token == ">>") {
				if (angle_bracket < 2) { err("unexpected '>>'!\n"); }
				angle_bracket -= 2;
				a.push_back(make_pair(type, ">"));
				a.push_back(make_pair(type, ">"));
				next();
			} else {
				a.push_back(make_pair(type, token));
				next();
			}
		}
		token_type last_token_type = unknown;
		for (auto e : a) {
			if (e.first == symbol && last_token_type == symbol && !type_name.empty()) {
				type_name += " ";
			}
			type_name += e.second;
			last_token_type = e.first;
		}
	}
	if (type_name == "size_t") { // TODO: support typedef
		type_name = "int";
	}
	return type_name;
}

auto query_function(string name, vector<string>& arg_types) -> tuple<size_t, string, bool, string, int> // offset, arg_types, is_code, type_name, arg_count
{
	auto it = override_functions.find(name);
	if (it == override_functions.end()) {
		err("function '%s' not defined!\n", name.c_str());
	}
	string type_name;
	if (name == "printf") {
		if (arg_types.empty()) err("missing parameter in printf()!\n");
		type_name = arg_types[0] + ",...";
	} else {
		type_name = vector_to_string(arg_types);
	}
	for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
		if (*it2 == name + "(" + type_name + ")") {
			auto it3 = symbols.find(*it2);
			if (it3 == symbols.end()) {
				err("unexpected runtime error!\n");
			}
			auto [ is_code, offset, size, symbol_type_name, ret_type, arg_count ] = it3->second;
			return make_tuple(offset, ret_type, is_code, type_name, arg_count);
		}
	}
	err("function '%s' not matched!\n", name.c_str());
	exit(1);
}

auto query_symbol(string s) -> tuple<bool, size_t, string, bool> // { is_global, offset, name, is_code }
{
	log<4>("[DEBUG] query symbol: '%s'\n", s.c_str());
	print_stack_frame();

	bool found = false;
	bool is_global = false;
	size_t offset = 0;
	string type_name;
	bool is_code = false;
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
				err("unknown symbol '%s'!\n", s.c_str());
			}
			if (it2->second.size() > 1) {
				err("undetermined override symbol '%s'!\n", s.c_str());
			}
			it = symbols.find(*(it2->second.begin()));
			if (it == symbols.end()) {
				err("unknown symbol '%s'!\n", s.c_str());
			}
		}
		auto [ is_code_2, offset_2, size, t, ret_type, arg_count ] = it->second;
		is_code = is_code_2;
		offset = offset_2;
		type_name = t;
	}
	log<3>("[DEBUG] symbol '%s': '%s', type='%s', offset=%zd, type='%s'\n",
			s.c_str(), (is_code ? "code" : "data"), (is_global ? "global" : "local"), offset, type_name.c_str());
	return make_tuple(is_global, offset, type_name, is_code);
}

string build_code_for_op2(string a_type, string op_name, string b_type)
{
	if      (op_name == "+=" ) add_assembly_code(ADD);
	else if (op_name == "-=" ) add_assembly_code(SUB);
	else if (op_name == "*=" ) add_assembly_code(MUL);
	else if (op_name == "/=" ) add_assembly_code(DIV);
	else if (op_name == "%=" ) add_assembly_code(MOD);
	else if (op_name == "<<=") add_assembly_code(SHL);
	else if (op_name == ">>=") add_assembly_code(SHR);
	else if (op_name == "&=" ) add_assembly_code(AND);
	else if (op_name == "|=" ) add_assembly_code(OR);
	else if (op_name == "&&=") add_assembly_code(LAND);
	else if (op_name == "||=") add_assembly_code(LOR);
	else err("Unsupported operator '%s'\n", op_name.c_str());
	return "int";
}

string build_code_for_op(string a_type, string op_name, string b_type)
{
	if (a_type == "int" && b_type == "int") {
		if      (op_name == "+" ) add_assembly_code(ADD);
		else if (op_name == "-" ) add_assembly_code(SUB);
		else if (op_name == "*" ) add_assembly_code(MUL);
		else if (op_name == "/" ) add_assembly_code(DIV);
		else if (op_name == "%" ) add_assembly_code(MOD);
		else if (op_name == "<<") add_assembly_code(SHL);
		else if (op_name == ">>") add_assembly_code(SHR);
		else if (op_name == "&" ) add_assembly_code(AND);
		else if (op_name == "|" ) add_assembly_code(OR);
		else if (op_name == "==") add_assembly_code(EQ);
		else if (op_name == "!=") add_assembly_code(NE);
		else if (op_name == ">=") add_assembly_code(GE);
		else if (op_name == ">" ) add_assembly_code(GT);
		else if (op_name == "<=") add_assembly_code(LE);
		else if (op_name == "<" ) add_assembly_code(LT);
		else if (op_name == "&&") add_assembly_code(LAND);
		else if (op_name == "||") add_assembly_code(LOR);
		else err("Unsupported operator '%s'\n", op_name.c_str());
		return "int";
	} else {
		string name = "operator" + op_name + "(" + a_type + "," + b_type + ")";
		auto it = symbols.find(name);
		if (it == symbols.end()) {
			err("Unknown function '%s'\n", name.c_str());
		}
		auto [ is_code, offset, size, type_name, ret_type, arg_count ] = it->second;
		if (!is_code) {
			err("symbol '%s' is not a function!\n", name.c_str());
		}
		add_assembly_code(PUSH);
		add_assembly_code(CALL, offset, ret_type + " " + name);
		return ret_type;
	}
}

string parse_expression(string stop_token = ";", int depth = 0, bool generate_code = true);

string parse_function(string name)
{
	log<3>("[DEBUG] %s: '%s'\n", __FUNCTION__, name.c_str());
	next();
	vector<string> arg_types;
	if (token != ")") {
		for (;;) {
			string type = parse_expression(",");
			arg_types.push_back(type);
			add_assembly_code(PUSH);
			if (token == ")") break;
			expect_token(",", "function '" + name + "'");
			next();
		}
	}
	expect_token(")", "function '" + name + "'");
	log<3>("[DEBUG] function '%s' has %zd args\n", name.c_str(), arg_types.size());
	auto [ offset, ret_type, is_code, type_name, arg_count ] = query_function(name, arg_types);
	if (arg_count < 0) {
		add_assembly_code(MOV, arg_types.size() + arg_count, "variable parameter count");
		add_assembly_code(PUSH);
	}
	add_assembly_code(CALL, offset, ret_type + " " + name + "(" + type_name + ")");
	if (arg_count < 0) {
		add_assembly_code(ADJ, arg_types.size());
	}
	log<3>("[DEBUG] ret_type = '%s'\n", ret_type.c_str());
	next();
	return ret_type;
}

string array_suffix(const vector<int>& dim)
{
	string s; for (auto e : dim) s += "[" + to_string(e) + "]"; return s;
}

int get_type_size(string type_name)
{
	if (type_name.substr(type_name.size() - 1) == "*") { // pointer
		return sizeof(int); // we use 'int' as word size
	} else {
		return sizeof(int); // we use 'int' as default type
	}
}

string parse_pointer_derefer(string name, string symbol_type_name,
		int offset, bool is_global, bool generate_code, int depth)
{
	assert(symbol_type_name.substr(symbol_type_name.size() - 1) == "*");
	string type_name = symbol_type_name;
	if (is_global) {
		if (generate_code) add_assembly_code(GET, offset, name + "\t" + type_name);
	} else {
		if (generate_code) add_assembly_code(LGET, offset, name + "\t" + type_name);
	}
	for (size_t i = 0; ; ++i) {
		if (generate_code) add_assembly_code(PUSH);
		next();
		parse_expression(";", depth, generate_code);
		if (type_name.substr(type_name.size() - 1) != "*") {
			err("too many level of dereferencing on a pointer!\n");
		}
		type_name = type_name.substr(0, type_name.size() - 1);
		int element_size = get_type_size(type_name);
		if (element_size != sizeof(int)) {
			if (generate_code) add_assembly_code(PUSH);
			if (generate_code) add_assembly_code(MOV, element_size / sizeof(int));
			if (generate_code) add_assembly_code(MUL);
		}
		if (generate_code) add_assembly_code(ADD);
		if (generate_code) add_assembly_code(PUSH);
		if (generate_code) add_assembly_code(SGET);
		expect_token("]", "[");
		next();
		if (token != "[") break;
	}
	return type_name;
}

string parse_array_element(string name, string symbol_type_name,
		int offset, bool is_global, bool generate_code, int depth)
{
	assert(symbol_type_name.substr(symbol_type_name.size() - 1) == "]");
	auto it = symbol_dim.find(name);
	if (it == symbol_dim.end()) {
		err("symbol '%s' (type = '%s') is not an array!\n",
				name.c_str(), symbol_type_name.c_str());
	}
	const auto& dim = it->second.second;
	if (is_global) {
		if (generate_code) add_assembly_code(LEA, offset, name + "\t" + symbol_type_name);
	} else {
		if (generate_code) add_assembly_code(LLEA, offset, name + "\t" + symbol_type_name);
	}
	if (generate_code) add_assembly_code(PUSH);
	next();
	for (size_t i = 0; ; ++i) {
		if (i > 0) {
			if (generate_code) add_assembly_code(PUSH);
			if (generate_code) add_assembly_code(MOV, dim[i]);
			if (generate_code) add_assembly_code(MUL);
		}
		parse_expression(";", depth, generate_code);
		if (i > 0) {
			if (generate_code) add_assembly_code(ADD);
		}
		expect_token("]", "[");
		next();
		if (token != "[") {
			int factor = 1;
			for (++i; i + 1 < dim.size(); ++i) {
				factor *= dim[i];
			}
			if (factor > 1) {
				if (generate_code) add_assembly_code(PUSH);
				if (generate_code) add_assembly_code(MOV, factor);
				if (generate_code) add_assembly_code(MUL);
			}
			break;
		}
		next();
	}
	if (generate_code) add_assembly_code(ADD);
	if (generate_code) add_assembly_code(PUSH);
	if (generate_code) add_assembly_code(SGET);
	if (!symbol_type_name.empty()) {
		if (symbol_type_name[symbol_type_name.size() - 1] == '*') {
			symbol_type_name = symbol_type_name.substr(0, symbol_type_name.size() - 1);
			while (!symbol_type_name.empty() && symbol_type_name[symbol_type_name.size() - 1] == ' ') {
				symbol_type_name = symbol_type_name.substr(0, symbol_type_name.size() - 1);
			}
		}
	}
	return symbol_type_name;
}

string parse_expression(string stop_token, int depth, bool generate_code)
{
	log<3>("[DEBUG] >(%d) %s (stop at '%s', token = '%s'):\n",
			depth, __FUNCTION__, stop_token.c_str(), token.c_str());

	if (type == number) {
		int v = eval_number(token);
		if (generate_code) add_assembly_code(MOV, v);
		next();
		return "int";
	} else if (type == text) {
		string v = eval_string(token);
		auto mem = prepare_string(v);
		string name = alloc_name();
		string type_name = "const char*";
		size_t offset = add_const_string(name, mem, type_name);
		if (generate_code) add_assembly_code(MOV, offset, name + "\t" + type_name);
		next();
		return "const char*";
	} else if (token == "sizeof") {
		next(); expect_token("(", "sizeof");
		next(); parse_expression(";", depth + 1, false);
		expect_token(")", "sizeof");
		int size = sizeof(int);
		if (generate_code) add_assembly_code(MOV, size);
		next(); // TODO: support sizeof()
		return "int";
	} else if (token == "(") {
		next();
		string type_name = parse_expression(";", depth + 1, generate_code);
		expect_token(")", "'(' in parse_expression");
		next();
		return type_name;
	}

	string type_name;
	if (type != symbol) { // prefix
		string op_name = token;
		next();
		if (op_name == "++" || op_name == "--") {
			if (type != symbol) err("unexpected token ('%s') after '%s'!\n", token.c_str(), op_name.c_str());
			string name = token;
			auto [ is_global, offset, symbol_type_name, is_code ] = query_symbol(name);
			if (symbol_type_name != "int") err("Operator '++' and '--' supports only 'int'!\n");
			if (is_global) {
				if (generate_code) add_assembly_code(GET, offset, name + "\t" + symbol_type_name);
			} else {
				if (generate_code) add_assembly_code(LGET, offset, name + "\t" + symbol_type_name);
			}
			if (op_name == "++") {
				if (generate_code) add_assembly_code(INC);
			} else {
				if (generate_code) add_assembly_code(DEC);
			}
			if (is_global) {
				if (generate_code) add_assembly_code(PUT, offset, name + "\t" + symbol_type_name);
			} else {
				if (generate_code) add_assembly_code(LPUT, offset, name + "\t" + symbol_type_name);
			}
			next();
			type_name = "int";
		} else {
			type_name = parse_expression(op_name, depth + 1, generate_code);
		}
	} else {
		string name = token;
		next();
		while (token == "::") {
			name += token; next();
			if (type != symbol) { err("unexpected token after '::'!\n"); }
			name += token; next();
		}
		if (token == "(") {
			type_name = parse_function(name);
		} else if (token == "=") {
			auto [ is_global, offset, symbol_type_name, is_code ] = query_symbol(name);
			if (is_global) {
				if (generate_code) add_assembly_code(LEA, offset, name + "\t" + type_name);
			} else {
				if (generate_code) add_assembly_code(LLEA, offset, name + "\t" + type_name);
			}
			if (generate_code) add_assembly_code(PUSH);
			next();
			parse_expression(",", depth + 1, generate_code);
			if (generate_code) add_assembly_code(SPUT, offset, name + "\t" + type_name);
			type_name = symbol_type_name;
		} else if (token == "+=" || token == "-=" || token == "*=" || token == "/=" || token == "%=" ||
				token == "<<=" || token == ">>=" || token == "&=" || token == "|=" || token == "&&=" || token == "||=") {
			string op_name = token;
			auto [ is_global, offset, symbol_type_name, is_code ] = query_symbol(name);
			if (is_global) {
				if (generate_code) add_assembly_code(LEA, offset, name + "\t" + type_name);
			} else {
				if (generate_code) add_assembly_code(LLEA, offset, name + "\t" + type_name);
			}
			if (generate_code) add_assembly_code(PUSH);
			next();
			string b_type = parse_expression(",", depth + 1, generate_code);
			if (generate_code) add_assembly_code(SPUT, offset, name + "\t" + type_name);
			type_name = build_code_for_op2(symbol_type_name, op_name, b_type);
			if (is_global) {
				if (generate_code) add_assembly_code(PUT, offset, name + "\t" + type_name);
			} else {
				if (generate_code) add_assembly_code(LPUT, offset, name + "\t" + type_name);
			}
		} else if (token == "++" || token == "--") { // suffix/postfix
			auto [ is_global, offset, symbol_type_name, is_code ] = query_symbol(name);
			if (is_global) {
				if (generate_code) add_assembly_code(GET, offset, name + "\t" + type_name);
			} else {
				if (generate_code) add_assembly_code(LGET, offset, name + "\t" + type_name);
			}
			if (generate_code) add_assembly_code(PUSH);
			if (token == "++") {
				if (generate_code) add_assembly_code(INC);
			} else {
				if (generate_code) add_assembly_code(DEC);
			}
			if (is_global) {
				if (generate_code) add_assembly_code(PUT, offset, name + "\t" + type_name);
			} else {
				if (generate_code) add_assembly_code(LPUT, offset, name + "\t" + type_name);
			}
			if (generate_code) add_assembly_code(POP);
			next();
			type_name = symbol_type_name;
		} else if (token == "{") {
			// TODO: initializer
			skip_until("}", "");
		} else if (token == "[") {
			auto [ is_global, offset, symbol_type_name, is_code ] = query_symbol(name);
			log<3>("symbol_type_name: '%s'\n", symbol_type_name.c_str());
			if (symbol_type_name.substr(symbol_type_name.size() - 1) == "*") {
				type_name = parse_pointer_derefer(name, symbol_type_name,
						offset, is_global, generate_code, depth + 1);
			} else {
				type_name = parse_array_element(name, symbol_type_name,
						offset, is_global, generate_code, depth + 1);
			}
		} else if (token == "." || token == "->") {
			// TODO: find member
			next();
		} else if (token == ".*" || token == "->*") {
			// TODO: find member
			next();
		} else {
			auto it = enum_types.find(name);
			if (it != enum_types.end()) {
				int value = it->second.second;
				if (generate_code) add_assembly_code(MOV, value);
				type_name = "int";
			} else {
				auto [ is_global, offset, symbol_type_name, is_code ] = query_symbol(name);
				if (is_global) {
					if (symbol_type_name == "int") {
						if (generate_code) add_assembly_code(GET, offset, name + "\t" + symbol_type_name);
					} else {
						if (generate_code) add_assembly_code(LEA, offset, name + "\t" + symbol_type_name);
					}
				} else {
					if (symbol_type_name == "int") {
						if (generate_code) add_assembly_code(LGET, offset, name + "\t" + symbol_type_name);
					} else {
						if (generate_code) add_assembly_code(LLEA, offset, name + "\t" + symbol_type_name);
					}
				}
				if (is_code) {
					type_name = "(*)(" + symbol_type_name + ")";
				} else {
					type_name = symbol_type_name;
				}
			}
		}
	}

	while (precedence(token) < precedence(stop_token)) {
		string op_name = token;
		next();
		if (generate_code) add_assembly_code(PUSH);
		string b_type = parse_expression(op_name, depth + 1, generate_code);
		type_name = build_code_for_op(type_name, op_name, b_type);
	}

	log<3>("[DEBUG] >(%d) %s (stop at '%s', token = '%s') return '%s'\n",
			depth, __FUNCTION__, stop_token.c_str(), token.c_str(), type_name.c_str());
	return type_name;
}

void parse_init_value(vector<int>& dim, vector<int>& dim2, vector<int>& cursor,
		vector<pair<vector<int>, int>>& init)
{
	if (token == "{") {
		if (cursor.size() >= dim.size()) err("too many level in init val!\n");
		next();
		size_t i = cursor.size();
		for (cursor.push_back(0); ; ++cursor[i]) {
			if (dim[i] == 0) {
				if (cursor[i] >= dim2[i]) dim2[i] = cursor[i] + 1;
			} else {
				if (cursor[i] >= dim[i]) err("array init overflow!\n");
			}
			parse_init_value(dim, dim2, cursor, init);
			if (token == "}") break;
			expect_token(",", "init-value");
			next();
		}
		cursor.pop_back();
		expect_token("}", "init-value");
		next();
	} else {
		//parse_expression(",");
		int v = eval_number(token);
		next();
		init.push_back(make_pair(cursor, v));
	}
}

void parse_declare()
{
	string type_name = parse_type_name();
	string type_prefix = type_name;
	string name = token; next();
	if (token == "(") { // function
		log<3>("[DEBUG] => function '%s', type='%s'\n", name.c_str(), type_name.c_str());
		if (!scopes.empty() && scopes.back().first != "function") {
			err("nesting function is not allowed!\n");
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
			add_argument(args[i].second, args[i].first, args.size() - i + 1);
		}
	} else { // variable
		log<3>("[DEBUG] => variable '%s', type='%s'\n", name.c_str(), type_name.c_str());
		for (;;) {
			if (token == "[") {
				next();
				vector<int> dim;
				for (;;) {
					int size = 0; // size undetermined
					if (token != "]") {
						if (type != number) err("invalid array size '%s'! it should be a number.\n", token.c_str());
						size = eval_number(token);
						if (size <= 0) err("invalid array size '%s'! it should be a positive integer!\n", token.c_str());
						next();
						expect_token("]", "array");
						next();
					}
					dim.push_back(size);
					if (token != "[") break;
					next();
				}
				int size = 1; for (auto d : dim) size *= d;
				if (verbose >= 3) {
					log("array dim = ["); for (size_t i = 0; i < dim.size(); ++i) log("%s%d", (i > 0 ? "," : ""), dim[i]); log("]\n");
				}
				if (!size && token != "=") err("missing array size!\n");
				if (token == "=") {
					next();
					vector<int> dim2 = dim;
					vector<int> cursor;
					vector<pair<vector<int>, int>> init;
					parse_init_value(dim, dim2, cursor, init);
					if (verbose >= 3) {
						for (size_t i = 0; i < init.size(); ++i) {
							log("init[%zd]: [", i);
							for (size_t j = 0; j < init[i].first.size(); ++j) {
								log("%s%d", (j == 0 ? "" : ", "), init[i].first[j]);
							}
							log("] = %d\n", init[i].second);
						}
					}
					size = 1; for (auto d : dim2) size *= d;
					assert(size > 0);
					type_name += array_suffix(dim2);
					auto [ is_global, offset ] = add_variable(name, size, type_name);
					unordered_map<int, pair<string, int>> index_to_val;
					for (size_t i = 0; i < init.size(); ++i) {
						const auto& cursor = init[i].first;
						int val = init[i].second;
						size_t index = 0;
						for (size_t j = 0; j < dim2.size(); ++j) {
							index = index * dim2[j] + cursor[j];
						}
						string s = name + "[";
						for (size_t j = 0; j < cursor.size(); ++j) {
							if (j > 0) s += "][";
							s += to_string(cursor[j]);
						}
						s += "]";
						index_to_val.insert(make_pair(index, make_pair(s, val)));
					}
					if (is_global) {
						for (int i = 0; i < size; ++i) {
							auto it = index_to_val.find(i);
							data_sec[offset + i] = (it == index_to_val.end() ? 0 : it->second.second);
						}
					} else {
						for (int i = 0; i < size; ++i) {
							auto it = index_to_val.find(i);
							add_assembly_code(MOV, (it == index_to_val.end() ? 0 : it->second.second));
							add_assembly_code(LPUT, offset + i, it->second.first);
						}
					}
					symbol_dim.insert(make_pair(name, make_pair(size, dim2)));
				} else {
					type_name += array_suffix(dim);
					add_variable(name, size, type_name);
				}
			} else {
				auto [ is_global, offset ] = add_variable(name, 1, type_name); // TODO: support non-int type
				if (token == "=") {
					next();
					parse_expression(",");
					if (is_global) {
						add_assembly_code(PUT, offset, name + "\t" + type_name);
					} else {
						add_assembly_code(LPUT, offset, name + "\t" + type_name);
					}
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
			log<3>("[DEBUG] => another variable '%s', type='%s'\n", name.c_str(), type_name.c_str());
		}
		expect_token(";", "variable");
	}
}

void parse_init_statement()
{
	log<3>("[DEBUG] > %s:\n", __FUNCTION__);

	if (token == "auto" || token == "const" || token == "static" ||
				token == "extern" || is_built_in_type()) { // start as type
		parse_declare();
	} else {
		parse_expression();
		expect_token(";", "statement");
	}
}

void parse_statements(int depth = 0)
{
	log<3>("[DEBUG] >(%d) %s: (token = '%s')\n", depth, __FUNCTION__, token.c_str());
	if (token == "{") {
		log<3>("[DEBUG] =>(%d) statement '{'\n", depth);
		next(); while (!token.empty() && token != "}") parse_statements(depth + 1);
		expect_token("}", "{");
		next();
	} else if (token == "if") {
		log<3>("[DEBUG] =>(%d) statement 'if'\n", depth);
		next(); expect_token("(", "if");
		next(); parse_expression(); expect_token(")", "if");
		size_t code_offset_1 = add_assembly_code(JZ, code_sec.size() + 2);
		next(); parse_statements(depth + 1);
		if (token == "else") {
			size_t code_offset_2 = add_assembly_code(JMP, code_sec.size() + 2);
			update_relative_address_here(code_offset_1);
			next();
			parse_statements(depth + 1);
			update_relative_address_here(code_offset_2);
		} else {
			update_relative_address_here(code_offset_1);
		}
	} else if (token == "for") {
		log<3>("[DEBUG] =>(%d) statement 'for'\n", depth);
		next(); expect_token("(", "for");
		next(); parse_init_statement(); expect_token(";", "for");
		size_t code_offset_1 = code_sec.size();
		next(); parse_expression(); expect_token(";", "for");
		size_t code_offset_2 = add_assembly_code(JZ, code_sec.size() + 2);
		size_t code_offset_3 = add_assembly_code(JMP, code_sec.size() + 2);
		size_t code_offset_4 = code_sec.size();
		next(); parse_expression(); expect_token(")", "for");
		add_assembly_code(JMP, code_offset_1);
		update_relative_address_here(code_offset_3);
		expect_token(")", "for");
		next(); parse_statements(depth + 1);
		add_assembly_code(JMP, code_offset_4);
		update_relative_address_here(code_offset_2);
	} else if (token == "while") {
		log<3>("[DEBUG] =>(%d) statement 'while'\n", depth);
		next(); expect_token("(", "while");
		size_t code_offset_1 = code_sec.size();
		next(); parse_expression(); expect_token(")", "while");
		size_t code_offset_2 = add_assembly_code(JZ, code_sec.size() + 2);
		next(); parse_statements(depth + 1);
		add_assembly_code(JMP, code_offset_1);
		update_relative_address_here(code_offset_2);
	} else if (token == "do") {
		log<3>("[DEBUG] =>(%d) statement 'do'\n", depth);
		next(); expect_token("{", "do");
		size_t code_offset_1 = code_sec.size();
		parse_statements(depth + 1);
		expect_token("while", "do");
		next(); expect_token("(", "do");
		next(); parse_expression();
		add_assembly_code(JNZ, code_offset_1);
		expect_token(")", "do");
		next(); expect_token(";", "do");
	} else if (token == "return") {
		log<3>("[DEBUG] =>(%d) statement 'return'\n", depth);
		next();
		if (scopes.empty() || scopes.back().first != "function") {
			err("unexpected 'return' statement!\n");
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
		log<3>("[DEBUG] =>(%d) statement 'typedef'\n", depth);
		skip_until(";", "typedef");
		next();
	} else {
		log<3>("[DEBUG] =>(%d) init-statement\n", depth);
		parse_init_statement();
		next();
	}
	log<3>("[DEBUG] >(%d) %s return: (token = '%s')\n", depth, __FUNCTION__, token.c_str());
}

void parse_enum()
{
	next(); // skip 'enum'

	if (type != symbol) {
		err("missing symbol for 'enum' name!\n");
	}
	string name = token;
	next(); // skip name

	expect_token("{", "enum " + name);
	next(); // skip '{'

	int value = 0;
	while (!token.empty() && token != "}") {
		if (type != symbol) {
			err("invalid token '%s' for 'enum' value!\n", token.c_str());
		}
		string enum_key = token;
		next(); // skip

		string value_txt;
		if (token == "=") {
			next(); // skip '='
			if (type != symbol && type != number) {
				err("invalid token '%s' for 'enum' declearation!\n", token.c_str());
			}
			value_txt = token;
			value = eval_number(value_txt);
			next();
		}
		log<3>("[DEBUG] enum '%s': %s%s%s\n", name.c_str(), enum_key.c_str(),
				(value_txt.empty() ? "" : " = "), value_txt.c_str());

		auto it = enum_values.find(name);
		if (it == enum_values.end()) {
			enum_values.insert(make_pair(name, unordered_map<string, int>()));
			it = enum_values.find(name);
		}
		auto it2 = it->second.find(enum_key);
		if (it2 != it->second.end()) {
			err("duplidated enum key '%s'!\n", enum_key.c_str());
		}
		it->second.insert(make_pair(enum_key, value++));
		auto it3 = enum_types.find(enum_key);
		if (it3 != enum_types.end()) {
			err("symbol '%s' has existed!\n", enum_key.c_str());
		}
		enum_types.insert(make_pair(enum_key, make_pair(name, value)));

		if (token == "}") break;
		expect_token(",", "enum " + name);
		next();
	}
	expect_token("}", "enum " + name);
	next(); // skip '}'
	expect_token(";", "enum " + name);
	next(); // skip ';'
	log<3>("[DEBUG] end of enum %s\n", name.c_str());
	if (verbose >= 4) dump_enum();
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
	log<3>("[DEBUG] prepare external symbols\n");
	add_external_symbol("cout", "ostream");
	add_external_symbol("cerr", "ostream");
	add_external_symbol("endl", "endl_t", "void", 1);
	add_external_symbol("operator<<", "ostream,int", "ostream", 2);
	add_external_symbol("operator<<", "ostream,double", "ostream", 2);
	add_external_symbol("operator<<", "ostream,const char*", "ostream", 2);
	add_external_symbol("operator<<", "ostream,(*)(endl_t)", "ostream", 2);
	add_external_symbol("printf", "const char*,...", "int", -1);
	log<3>("[DEBUG] total %zd symbols are prepared\n", symbols.size());
	external_data_size = data_sec.size();
	external_code_size = code_sec.size();
	if (verbose >= 3) {
		size_t i = 0;
		for (auto it = symbols.begin(); it != symbols.end(); ++it) {
			auto name = it->first;
			auto [ is_code, offset, size, type_name, ret_type, arg_count ] = it->second;
			log("[DEBUG] [%zd] '%s': %s, offset=%zd, size=%zd, type='%s', ret_type='%s', arg_count=%d\n",
					i++, name.c_str(), (is_code ? "code" : "data"), offset, size, type_name.c_str(), ret_type.c_str(), arg_count);
		}
	}
}

void parse()
{
	init_symbol();

	for (next(); !token.empty();) {
		if (token == "using") {
			next(); while (!token.empty() && token != ";") next();
			if (token.empty()) { err("missing ';' for 'using'!\n"); }
			log<3>("[DEBUG] => 'using' statement skipped\n");
			next();
		} else if (token == "typedef") {
			skip_until(";", token);
			log<3>("[DEBUG] => 'typedef' statement skipped\n");
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
			log<3>("[DEBUG] => (%s %s) start\n", keyword.c_str(), name.c_str());
			next();
		} else if (token == "template") {
			skip_until(";", token);
			log<3>("[DEBUG] => 'template' statement skipped\n");
			next();
		} else if (token == ";") {
			log<3>("[DEBUG] => ';' - end of statement\n");
			next();
		} else if (token == "}") {
			string scope_type;
			string scope_name;
			if (!scopes.empty()) {
				scope_type = scopes.back().first;
				scope_name = scopes.back().second;
				scopes.pop_back();
			}
			log<3>("[DEBUG] => '}' - end of block (%s,%s)\n", scope_type.c_str(), scope_name.c_str());
			if (scope_type == "function") {
				if (returned_functions.find(scope_name) == returned_functions.end()) {
					add_assembly_code(LEAVE);
					add_assembly_code(RET, get<3>(current_function));
				}
				current_function = make_tuple("", "", "", 0);
				stack_frame_table.pop_back();
			}
			next();
		} else {
			parse_statements();
		}
	}
}

int show()
{
	if (verbose >= 1) {
		for (size_t i = 0; i < external_code_size;) {
			i = print_code(code_sec, i);
		}
	}
	for (size_t i = 0; i < src.size(); ++i) {
		print_source_code_line(i);
		auto it = offset.find(i + 1);
		if (it != offset.end()) {
			log(COLOR_BLUE);
			for (size_t j = it->second.first; j <= it->second.second;) {
				j = print_code(code_sec, j);
			}
			log(COLOR_NORMAL);
		}
	}
	log("\n");

	for (size_t i = (verbose >= 1 ? 0 : external_data_size); i < data_sec.size(); ++i) {
		auto it = data_symbol_dict.find(i);
		if (it != data_symbol_dict.end()) {
			auto name = it->second;
			auto [ is_code, offset, size, type, ret_type, arg_count ] = symbols[name];
			string data_type = "word";
			if (type == "const char*") data_type = "byte";
			log(COLOR_YELLOW "%-10zd", i);
			log(COLOR_BLUE ".%-13s", data_type.c_str());
			size_t width = 0;
			if (type == "const char*") {
				log("\""); ++width;
				const char* s = reinterpret_cast<const char*>(&data_sec[offset]);
				for (size_t i = 0; i + 1 < size * sizeof(int); ++i) {
					switch (s[i]) {
						default: log("%c", s[i]); ++width; break;
						case '\t': log("\\t"); width += 2; break;
						case '\r': log("\\r"); width += 2; break;
						case '\n': log("\\n"); width += 2; break;
						case '\\': log("\\\\"); width += 2; break;
						case '\'': log("\\\'"); width += 2; break;
						case '\"': log("\\\""); width += 2; break;
						case '\0': log("\\0"); width += 2; break;
					}
				}
				log("\""); ++width;
			} else {
				for (size_t i = 0; i < size; ++i) {
					if (i > 0) { log("  "); width += 2; }
					log("0x%08X", static_cast<unsigned int>(data_sec[offset + i]));
					width += 10;
				}
			}
			if (width > 25) {
				log("\n%*s", (10 + 14 + 25), "");
			} else {
				log("%*s", 25 - width, "");
			}
			log(" ; %s %s" COLOR_NORMAL "\n", type.c_str(), name.c_str());
		}
	}
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
		log("\t[#%zd backtrace]: bp = %08X", i, bp);
		if (bp == m[bp]) break;
		log("\tm[bp] = %08X", m[bp]);
		log("\tm[bp+1] = %08X", m[bp+1]);
		log("\tm[bp+2] = %08X", m[bp+2]);
		log("\tm[bp+3] = %08X", m[bp+3]);
		log("\tm[bp+4] = %08X", m[bp+4]);
		bp = m[bp];
		log("\n");
	}
	if (i == 10) {
		log("...\n");
	}
	log("\n");
	return;
}

string get_data_symbol(size_t offset)
{
	auto it = data_symbol_dict.find(offset);
	if (it == data_symbol_dict.end()) {
		err("Unknown data symbol offset '%zd'\n", offset);
		exit(1);
	}
	return it->second;
}

int call_ext(const string& name, int sp)
{
	log<3>("[DEBUG] external call: %s\n", name.c_str());
	if (name == "operator<<(ostream,int)") {
		int b = m[sp + 1];
		int a = m[sp + 2];
		log<3>("[DEBUG] args: %d, %d\n", a, b);
		string aa = get_data_symbol(a);
		if (aa == "cout") {
			cout << b;
		} else if (aa == "cerr") {
			cerr << b;
		} else {
			err("Unsupported operator<< for %d('%s')\n", m[a], aa.c_str());
			exit(1);
		}
		return a;
	} else if (name == "operator<<(ostream,const char*)") {
		int b = m[sp + 1];
		int a = m[sp + 2];
		log<3>("[DEBUG] args: %d, %d\n", a, b);
		string aa = get_data_symbol(a);
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
	} else if (name == "operator<<(ostream,(*)(endl_t))") {
		int b = m[sp + 1];
		int a = m[sp + 2];
		log<3>("[DEBUG] args: %d, %d\n", a, b);
		string aa = get_data_symbol(a);
		if (aa == "cout") {
			cout << endl;
		} else if (aa == "cerr") {
			cerr << endl;
		} else {
			err("Unsupported operator<< for %d('%s')\n", m[a], aa.c_str());
			exit(1);
		}
		return a;
	} else if (name == "printf(const char*,...)") {
		int var_arg_count = m[sp + 1];
		int var_arg_start = sp + 1 + var_arg_count;
		const char* fmt = reinterpret_cast<const char*>(&m[m[var_arg_start + 1]]);
		int n = 0;
		for (int i = 0; *fmt; ++fmt) {
			if (*fmt == '%') {
				char c = *++fmt;
				if (c == 'd' || c == 'c') {
					if (i >= var_arg_count) { n += printf("<missing>"); }
					else { n += printf((c == 'd' ? "%d" : "%c"), m[var_arg_start - i]); }
					++i;
				} else if (c == 's' || c == 'p') {
					if (i >= var_arg_count) { n += printf("<missing>"); }
					else { n += printf((c == 's' ? "%s" : "%p"), reinterpret_cast<const char*>(&m[m[var_arg_start - i]])); }
					++i;
				} else {
					n += printf("%c", c);
				}
			} else {
				n += printf("%c", *fmt);
			}
		}
		return n;
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
	log<1>("Loading program\n  data: %zd word(s)\n  code: %zd word(s)\n\n",
			data_sec.size(), code_sec.size());

	size_t loaded = 0;
	for (size_t i = 0; i < data_sec.size(); ++i) {
		m[loaded++] = data_sec[i];
	}
	int code_loading_position = loaded;
	for (size_t i = 0; i < code_sec.size(); ++i) {
		m[loaded++] = code_sec[i];
	}

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
	ip = code_loading_position + get<1>(it2->second);

	// prepare argc & argv
	sp -= argc + 1;
	int argv_copy = sp;
	for (int i = 0; i < argc; ++i) {
		size_t len = strlen(argv[i]);
		size_t size = (len + sizeof(int)) / sizeof(int);
		m[sp - 1] = 0;
		sp -= size;
		m[argv_copy + i] = sp;
		memcpy(&m[sp], argv[i], len);
	}
	m[argv_copy + argc] = 0;
	if (verbose >= 3) {
		log("[DEBUG] prepare argc & argv:\n");
		for (int i = sp; i < MEM_SIZE; i += 4) {
			log("[DEBUG] %08X:  ", i);
			for (int j = 0; j < 4; ++j) {
				if (i + j < MEM_SIZE) {
					const unsigned char* p = reinterpret_cast<const unsigned char*>(&m[i + j]);
					log("%02X %02X %02X %02X  ", *p, *(p+1), *(p+2), *(p+3));
				} else {
					log("%*s", 13, "");
				}
			}
			for (int j = 0; j < 4 && i + j < MEM_SIZE; ++j) {
				const char* p = reinterpret_cast<const char*>(&m[i + j]);
				for (int k = 0; k < 4; ++k) {
					char c = *(p+k);
					log("%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
				}
			}
			log("\n");
		}
		log("[DEBUG] argv_copy = 0x%08X\n\n", argv_copy);
	}

	// prepare stack for main()
	bp = sp - 1;
	m[--sp] = bp;
	m[--sp] = EXIT; int exit_addr = sp;
	m[--sp] = argc;
	m[--sp] = argv_copy;
	m[--sp] = exit_addr;
	if (verbose >= 3) {
		log("[DEBUG] stack for main():\n");
		log("[DEBUG] stack: m[sp]... = [ %08X, %08X, %08X, %08X, %08X ]\n", m[sp], m[sp+1], m[sp+2], m[sp+3], m[sp+4]);
		log("[DEBUG] ax = %08X, ip = %08X, bp = %08X, sp = %08X\n\n", ax, ip, bp, sp);
	}

	log<1>("System Information:\n"
			"  sizeof(int) = %zd\n"
			"  sizeof(void*) = %zd\n"
			"\n", sizeof(int), sizeof(void*));

	size_t cycle = 0;
	for (;;) {
		++cycle;
		if (verbose >= 1) {
			log("%zd:\t", cycle);
			print_code(m, ip, code_loading_position);
			if (verbose >= 2) {
				print_vm_env(ax, ip, sp, bp);
			}
		}
		size_t i = m[ip++];

		if      (i == EXIT) { break;                } // exit the program
		else if (i == PUSH) { m[--sp] = ax;         } // push ax to stack
		else if (i == POP ) { ax = m[sp++];         } // pop ax from stack
		else if (i == ADJ ) { sp -= m[ip++];        } // adjust stack pointer

		else if (i == MOV ) { ax = m[ip++];         } // move immediate to ax
		else if (i == LEA ) { ax = m[ip++];         } // load address to ax
		else if (i == GET ) { ax = m[m[ip++]];      } // get memory to ax
		else if (i == PUT ) { m[m[ip++]] = ax;      } // put ax to memory
		else if (i == LLEA) { ax = bp + m[ip++];    } // load local address to ax
		else if (i == LGET) { ax = m[bp + m[ip++]]; } // get local to ax
		else if (i == LPUT) { m[bp + m[ip++]] = ax; } // put ax to local

		else if (i == SGET) { ax = m[m[sp++]];      } // get [stack] to ax
		else if (i == SPUT) { m[m[sp++]] = ax;      } // put ax to [stack]

		else if (i == ADD ) { ax = m[sp++] + ax;    } // stack (top) + ax, and pop out
		else if (i == SUB ) { ax = m[sp++] - ax;    } // stack (top) - ax, and pop out
		else if (i == MUL ) { ax = m[sp++] * ax;    } // stack (top) * ax, and pop out
		else if (i == DIV ) { ax = m[sp++] / ax;    } // stack (top) / ax, and pop out
		else if (i == MOD ) { ax = m[sp++] % ax;    } // stack (top) % ax, and pop out
		else if (i == NEG ) { ax = -ax;             }
		else if (i == INC ) { ++ax;                 }
		else if (i == DEC ) { --ax;                 }

		else if (i == SHL ) { ax = m[sp++] >> ax;   } // stack (top) >> ax, and pop out
		else if (i == SHR ) { ax = m[sp++] << ax;   } // stack (top) << ax, and pop out
		else if (i == AND ) { ax = m[sp++] & ax;    } // stack (top) & ax, and pop out
		else if (i == OR  ) { ax = m[sp++] | ax;    } // stack (top) | ax, and pop out
		else if (i == NOT ) { ax = ~ax;             }

		else if (i == EQ  ) { ax = m[sp++] == ax;   } // stack (top) == ax, and pop out
		else if (i == NE  ) { ax = m[sp++] != ax;   } // stack (top) != ax, and pop out
		else if (i == GE  ) { ax = m[sp++] >= ax;   } // stack (top) >= ax, and pop out
		else if (i == GT  ) { ax = m[sp++] >  ax;   } // stack (top) >  ax, and pop out
		else if (i == LE  ) { ax = m[sp++] <= ax;   } // stack (top) <= ax, and pop out
		else if (i == LT  ) { ax = m[sp++] <  ax;   } // stack (top) <  ax, and pop out
		else if (i == LAND) { ax = m[sp++] && ax;   } // stack (top) && ax, and pop out
		else if (i == LOR ) { ax = m[sp++] || ax;   } // stack (top) || ax, and pop out
		else if (i == LNOT) { ax = !ax;             }

		else if (i == ENTER) { m[--sp] = bp; bp = sp; sp -= m[ip++];   } // enter stack frame
		else if (i == LEAVE) { sp = bp; bp = m[sp++];                  } // leave stack frame
		else if (i == CALL ) { int n = m[ip++]; m[--sp] = ip; ip += n; } // call subroutine
		else if (i == RET  ) { int n = m[ip]; ip = m[sp++]; sp += n;   } // exit subroutine
		else if (i == JMP  ) { int n = m[ip++]; ip += n;               } // goto
		else if (i == JZ   ) { int n = m[ip++]; if (!ax) ip += n;      } // goto if !ax
		else if (i == JNZ  ) { int n = m[ip++]; if (ax) ip += n;       } // goto if ax

		else { warn("unknown instruction: '%zd'\n", i); }

		if (ip && ip < static_cast<int>(code_loading_position + external_code_size)) {
			auto it = code_symbol_dict.find(ip - code_loading_position);
			if (it != code_symbol_dict.end()) {
				ax = call_ext(it->second, sp);
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
	for (--argc, ++argv; argc > 0 && !filename; --argc, ++argv) {
		if (**argv == '-') {
			if (*(*argv+1) == 'v') { ++verbose; }
			if (*(*argv+1) == 's') { assembly = true; }
		} else {
			filename = *argv;
		}
	}
	if (!filename) {
		log("usage: icpp [-s] [-v] <foo.cpp> ...\n");
		return false;
	}
	on_err = print_current_and_exit;
	if (load(filename)) parse();
	return assembly ? show() : run(argc, argv);
}
