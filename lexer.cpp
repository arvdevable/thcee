// thc: a minimal reference interpreter for the thcee language.
// Build: clang++ -std=c++17 -O2 lexer.cpp -o thc.exe
#include <boost/multiprecision/cpp_int.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

using Big = boost::multiprecision::cpp_int;
namespace fs = std::filesystem;

struct SourcePos {
    std::string file;
    int line = 1, column = 1;
};
struct ThcError: std::runtime_error {
    SourcePos pos;
    ThcError(SourcePos p,
        const std::string & message): std::runtime_error(message), pos(std::move(p)) {}
};
[
    [noreturn]
] static void fail(const SourcePos & p,
    const std::string & message) {
    throw ThcError(p, message);
}

enum class TokenKind {
    word,
    integer,
    floating,
    string,
    symbol,
    end
};
struct Token {
    TokenKind kind;
    std::string text;
    SourcePos pos;
};

class Lexer {
    public: Lexer(std::string source, std::string filename): source_(std::move(source)),
    filename_(std::move(filename)) {}
    std::vector < Token > scan() {
        std::vector < Token > out;
        while (!atEnd()) {
            if (std::isspace(static_cast < unsigned char > (peek()))) {
                advance();
                continue;
            }
            SourcePos p = position();
            if (peek() == '*') {
                advance();
                if (match('*')) {
                    while (!atEnd() && !(peek() == '*' && peekNext() == '*')) advance();
                    if (atEnd()) fail(p, "unterminated multiline comment");
                    advance();
                    advance();
                } else {
                    while (!atEnd() && peek() != '\n') advance();
                }
                continue;
            }
            if (peek() == '"' || peek() == '\'') {
                out.push_back(string());
                continue;
            }
            if (std::isdigit(static_cast < unsigned char > (peek())) ||
                (peek() == '.' && std::isdigit(static_cast < unsigned char > (peekNext())))) {
                out.push_back(number());
                continue;
            }
            if (std::isalpha(static_cast < unsigned char > (peek())) || peek() == '_' ||
                (peek() == '.' && !std::isdigit(static_cast < unsigned char > (peekNext())))) {
                out.push_back(word());
                continue;
            }
            char c = advance();
            if (c == ':' && match(':')) out.push_back({
                TokenKind::symbol,
                "::",
                p
            });
            else if (std::string("{}[]();,:=!-+").find(c) != std::string::npos)
                out.push_back({
                    TokenKind::symbol,
                    std::string(1, c),
                    p
                });
            else fail(p, std::string("unexpected character '") + c + "'");
        }
        out.push_back({
            TokenKind::end,
            "",
            position()
        });
        return out;
    }
    private: std::string source_,
    filename_;size_t index_ = 0;int line_ = 1,
    column_ = 1;
    bool atEnd() const {
        return index_ >= source_.size();
    }
    char peek() const {
        return atEnd() ? '\0' : source_[index_];
    }
    char peekNext() const {
        return index_ + 1 >= source_.size() ? '\0' : source_[index_ + 1];
    }
    char advance() {
        char c = source_[index_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else ++column_;
        return c;
    }
    bool match(char c) {
        if (peek() != c) return false;
        advance();
        return true;
    }
    SourcePos position() const {
        return {
            filename_,
            line_,
            column_
        };
    }
    Token word() {
        SourcePos p = position();
        size_t start = index_;
        advance();
        while (std::isalnum(static_cast < unsigned char > (peek())) || peek() == '_' ||
            peek() == '.' || peek() == '/' || peek() == '\\') advance();
        std::string text = source_.substr(start, index_ - start);
        TokenKind kind = (text.find('.') != std::string::npos || text.find('/') != std::string::npos ||
            text.find('\\') != std::string::npos) ? TokenKind::string : TokenKind::word;
        return {
            kind,
            text,
            p
        };
    }
    Token number() {
        SourcePos p = position();
        size_t start = index_;
        bool dot = false;
        while (std::isdigit(static_cast < unsigned char > (peek())) || (!dot && peek() == '.')) {
            if (peek() == '.') dot = true;
            advance();
        }
        return {
            dot ? TokenKind::floating : TokenKind::integer,
            source_.substr(start, index_ - start),
            p
        };
    }
    Token string() {
        SourcePos p = position();
        char quote = advance();
        std::string value;
        while (!atEnd() && peek() != quote) {
            char c = advance();
            if (c == '\\') {
                if (atEnd()) fail(p, "unterminated escape sequence");
                char e = advance();
                if (e == 'n') value += '\n';
                else if (e == 't') value += '\t';
                else value += e;
            } else value += c;
        }
        if (atEnd()) fail(p, "unterminated string literal");
        advance();
        return {
            TokenKind::string,
            value,
            p
        };
    }
};

struct Expr;
struct Stmt;
struct Env;
struct Module;
struct Function {
    std::vector < std::shared_ptr < Stmt >> body;
    std::shared_ptr < Env > closure;
    std::string returnType;
};
struct Pointer {
    std::vector < Big > cells {
        0
    };
    size_t position = 0;
};
using Value = std::variant < std::monostate, bool, Big, double, std::string,
    std::shared_ptr < Function > , std::shared_ptr < Pointer > , std::shared_ptr < Module >> ;
enum class ExprKind {
    literal,
    name,
    deref,
    position,
    value,
    unary,
    binary
};
struct Expr {
    ExprKind kind;
    SourcePos pos;
    Value literal;
    std::string name, op;
    std::shared_ptr < Expr > left, right;
};
enum class StmtKind {
    store,
    modify,
    arithmetic,
    print,
    error,
    define,
    call,
    iff,
    ret,
    move,
    memory,
    pointerMove,
    pointerWrite,
    pointerRemove,
    pointerRun,
    load,
    run,
    forget,
    exportValue,
    fget,
    exit
};
struct Stmt {
    StmtKind kind;
    SourcePos pos;
    std::string name, aux;
    std::shared_ptr < Expr > expr, target;
    std::vector < std::shared_ptr < Stmt >> yes, no;
    bool optional = false, global = false;
    std::string returnType;
};

class Parser {
    public: Parser(std::vector < Token > tokens): tokens_(std::move(tokens)) {}
    std::vector < std::shared_ptr < Stmt >> program() {
        std::vector < std::shared_ptr < Stmt >> result;
        while (!isEnd()) result.push_back(statement());
        return result;
    }
    private: std::vector < Token > tokens_;size_t current_ = 0;
    const Token & peek() const {
        return tokens_[current_];
    }
    const Token & previous() const {
        return tokens_[current_ - 1];
    }
    bool isEnd() const {
        return peek().kind == TokenKind::end;
    }
    Token advance() {
        if (!isEnd()) ++current_;
        return previous();
    }
    bool check(const std::string & text) const {
        return !isEnd() && peek().text == text;
    }
    bool match(const std::string & text) {
        if (!check(text)) return false;
        advance();
        return true;
    }
    Token require(const std::string & text) {
        if (!match(text)) fail(peek().pos, "expected '" + text + "', got '" + peek().text + "'");
        return previous();
    }
    Token requireWord(const std::string & what) {
        if (peek().kind != TokenKind::word) fail(peek().pos, "expected " + what);
        return advance();
    }
    std::shared_ptr < Expr > make(ExprKind k, SourcePos p) {
        auto x = std::make_shared < Expr > ();
        x -> kind = k;
        x -> pos = std::move(p);
        return x;
    }
    std::string qualifiedName() {
        std::string result = requireWord("name").text;
        while (match(":")) result += ":" + requireWord("member name").text;
        return result;
    }
    std::shared_ptr < Expr > primary() {
        Token t = advance();
        if (t.kind == TokenKind::string) {
            auto x = make(ExprKind::literal, t.pos);
            x -> literal = t.text;
            return x;
        }
        if (t.kind == TokenKind::integer) {
            auto x = make(ExprKind::literal, t.pos);
            x -> literal = Big(t.text);
            return x;
        }
        if (t.kind == TokenKind::floating) {
            auto x = make(ExprKind::literal, t.pos);
            x -> literal = std::stod(t.text);
            return x;
        }
        if (t.text == "+" || t.text == "-") {
            auto right = primary();
            auto zero = make(ExprKind::literal, t.pos);
            zero -> literal = Big(0);
            auto x = make(ExprKind::binary, t.pos);
            x -> op = t.text == "-" ? "sub" : "add";
            x -> left = zero;
            x -> right = right;
            return x;
        }
        if (t.text == "[") {
            Token name = requireWord("pointer name");
            require("]");
            auto x = make(ExprKind::deref, t.pos);
            x -> name = name.text;
            return x;
        }
        if ((t.text == "pos" || t.text == "val") && match("[")) {
            Token name = requireWord("pointer name");
            require("]");
            auto x = make(t.text == "pos" ? ExprKind::position : ExprKind::value, t.pos);
            x -> name = name.text;
            return x;
        }
        if (t.text == "true" || t.text == "false") {
            auto x = make(ExprKind::literal, t.pos);
            x -> literal = t.text == "true";
            return x;
        }
        if (t.kind == TokenKind::word) {
            auto x = make(ExprKind::name, t.pos);
            x -> name = t.text;
            while (match(":")) x -> name += ":" + requireWord("member name").text;
            return x;
        }
        fail(t.pos, "expected value");
    }
    std::shared_ptr < Expr > expression() {
        if (match("!")) {
            auto x = make(ExprKind::unary, previous().pos);
            x -> op = "!";
            x -> left = primary();
            return x;
        }
        static
        const std::unordered_set < std::string > binary {
            "add",
            "sub",
            "mul",
            "div",
            "mod",
            "xor",
            "eq",
            "lt",
            "gt",
            "or"
        };
        if (binary.count(peek().text)) {
            Token op = advance();
            auto x = make(ExprKind::binary, op.pos);
            x -> op = op.text;
            x -> left = primary();
            x -> right = primary();
            return x;
        }
        return primary();
    }
    std::vector < std::shared_ptr < Stmt >> block() {
        require("{");
        std::vector < std::shared_ptr < Stmt >> result;
        while (!check("}") && !isEnd()) result.push_back(statement());
        require("}");
        return result;
    }
    std::shared_ptr < Stmt > arithmetic(Token op) {
        auto s = std::make_shared < Stmt > ();
        s -> kind = StmtKind::arithmetic;
        s -> pos = op.pos;
        s -> aux = op.text;
        s -> target = primary();
        s -> expr = expression();
        require(";");
        return s;
    }
    std::shared_ptr < Stmt > statement() {
        bool global = match("g");
        Token keyword = advance();
        if (keyword.kind == TokenKind::end) fail(keyword.pos, "unexpected end of file");
        if (keyword.text == "add" || keyword.text == "sub" || keyword.text == "mul" || keyword.text == "div" || keyword.text == "mod" || keyword.text == "xor") return arithmetic(keyword);
        auto s = std::make_shared < Stmt > ();
        s -> pos = keyword.pos;
        s -> global = global;
        if (keyword.text == "def") {
            s -> kind = StmtKind::define;
            s -> name = requireWord("function name").text;
            if (match("::")) {
                s -> returnType = requireWord("return type").text;
                while (match("::")) {
                    requireWord("parameter name");
                    require(":");
                    requireWord("parameter type");
                }
            }
            s -> yes = block();
            return s;
        }
        if (global) fail(keyword.pos, "'g' is only valid before 'def' or 'mem'");
        if (keyword.text == "if") {
            s -> kind = StmtKind::iff;
            auto left = expression();
            if (check("eq") || check("lt") || check("gt") || check("or")) {
                Token op = advance();
                s -> expr = make(ExprKind::binary, op.pos);
                s -> expr -> op = op.text;
                s -> expr -> left = left;
                s -> expr -> right = expression();
            } else s -> expr = left;
            s -> optional = match("nr");
            s -> yes = block();
            if (match("oth")) s -> no = block();
            match(";");
            return s;
        }
        if (keyword.text == "ret") {
            s -> kind = StmtKind::ret;
            if (!check(";")) s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.text == "prt" || keyword.text == "err") {
            s -> kind = keyword.text == "prt" ? StmtKind::print : StmtKind::error;
            s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.text == "sto" || keyword.text == "mdf") {
            s -> kind = keyword.text == "sto" ? StmtKind::store : StmtKind::modify;
            s -> target = primary();
            s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.text == "mov") {
            s -> kind = StmtKind::move;
            s -> target = primary();
            s -> expr = primary();
            require(";");
            return s;
        }
        if (keyword.text == "cal") {
            s -> kind = StmtKind::call;
            s -> name = qualifiedName();
            require(";");
            return s;
        }
        if (keyword.text == "lod" || keyword.text == "run") {
            if (keyword.text == "run" && check("[")) {
                s -> kind = StmtKind::pointerRun;
                s -> expr = primary();
                require(";");
                return s;
            }
            s -> kind = keyword.text == "lod" ? StmtKind::load : StmtKind::run;
            s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.text == "fget") {
            s -> kind = StmtKind::fget;
            s -> expr = expression();
            require("as");
            s -> name = requireWord("alias").text;
            require(";");
            return s;
        }
        if (keyword.text == "fgt") {
            s -> kind = StmtKind::forget;
            s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.text == "exp") {
            s -> kind = StmtKind::exportValue;
            s -> name = requireWord("name").text;
            require(";");
            return s;
        }
        if (keyword.text == "ext") {
            s -> kind = StmtKind::exit;
            require(";");
            return s;
        }
        if (keyword.text == "mem") {
            s -> kind = StmtKind::memory;
            s -> name = requireWord("memory name").text;
            require("new");
            require("[");
            s -> aux = requireWord("pointer name").text;
            require("]");
            if (!check(";")) s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.text == "pm") {
            s -> kind = StmtKind::pointerMove;
            if (match("[")) {
                s -> name = requireWord("pointer name").text;
                require("]");
            }
            s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.text == "prb") {
            s -> kind = StmtKind::pointerWrite;
            if (match("[")) {
                s -> name = requireWord("pointer name").text;
                require("]");
            }
            s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.text == "new") {
            s -> kind = StmtKind::memory;
            require("[");
            s -> name = requireWord("pointer name").text;
            require("]");
            s -> aux = s -> name;
            require(";");
            return s;
        }
        if (keyword.text == "rem") {
            s -> kind = StmtKind::pointerRemove;
            require("[");
            s -> name = requireWord("pointer name").text;
            require("]");
            require(";");
            return s;
        }
        if (keyword.kind == TokenKind::word && match("=")) {
            s -> kind = StmtKind::store;
            s -> target = make(ExprKind::name, keyword.pos);
            s -> target -> name = keyword.text;
            s -> expr = expression();
            require(";");
            return s;
        }
        if (keyword.kind == TokenKind::word) {
            s -> kind = StmtKind::call;
            s -> name = keyword.text;
            require(";");
            return s;
        }
        fail(keyword.pos, "unknown statement '" + keyword.text + "'");
    }
};

struct Module {
    std::string path;
    std::vector < std::shared_ptr < Stmt >> program;
    std::shared_ptr < Env > lastEnv;
    std::unordered_set < std::string > exports;
    bool running = false;
};
struct Env {
    std::unordered_map < std::string, Value > values;
    std::shared_ptr < Env > parent;
    std::shared_ptr < Module > owner;
    std::string activePointer;
    explicit Env(std::shared_ptr < Env > p = nullptr, std::shared_ptr < Module > m = nullptr): parent(std::move(p)), owner(std::move(m)) {}
    Value * find(const std::string & name) {
        if (auto it = values.find(name); it != values.end()) return & it -> second;
        return parent ? parent -> find(name) : nullptr;
    }
};
struct ReturnSignal {
    Value value;
};
struct ExitSignal {};

static std::string valueText(const Value & value) {
    if (std::holds_alternative < std::monostate > (value)) return "none";
    if (auto p = std::get_if < bool > ( & value)) return * p ? "true" : "false";
    if (auto p = std::get_if < Big > ( & value)) return p -> convert_to < std::string > ();
    if (auto p = std::get_if < double > ( & value)) {
        std::ostringstream out;
        out << * p;
        return out.str();
    }
    if (auto p = std::get_if < std::string > ( & value)) return * p;
    if (std::holds_alternative < std::shared_ptr < Function >> (value)) return "<function>";
    if (std::holds_alternative < std::shared_ptr < Pointer >> (value)) return "<pointer>";
    return "<module>";
}
static bool truthy(const Value & value) {
    if (auto p = std::get_if < bool > ( & value)) return * p;
    if (auto p = std::get_if < Big > ( & value)) return * p != 0;
    if (auto p = std::get_if < double > ( & value)) return * p != 0.0 && !std::isnan( * p);
    if (auto p = std::get_if < std::string > ( & value)) return !p -> empty();
    return !std::holds_alternative < std::monostate > (value);
}
static double asDouble(const Value & value,
    const SourcePos & pos) {
    if (auto p = std::get_if < Big > ( & value)) return p -> convert_to < double > ();
    if (auto p = std::get_if < double > ( & value)) return * p;
    fail(pos, "numeric operand required");
}
static Big asInt(const Value & value,
    const SourcePos & pos) {
    if (auto p = std::get_if < Big > ( & value)) return * p;
    fail(pos, "integer operand required");
}

class Runtime {
    public: explicit Runtime(bool warnings): warnings_(warnings),
    root_(std::make_shared < Env > ()) {}
    void execute(const std::vector < std::shared_ptr < Stmt >> & program,
        const std::shared_ptr < Env > & env) {
        for (const auto & stmt: program) {
            try {
                executeOne(stmt, env);
            } catch (const ThcError & error) {
                if (!stmt -> optional) throw;
                warning(error.pos, "suppressed by nr: " + std::string(error.what()));
            }
        }
    }
    private: bool warnings_;std::shared_ptr < Env > root_;std::unordered_map < std::string,
    std::shared_ptr < Module >> modules_;
    void warning(const SourcePos & p,
        const std::string & message) const {
        if (warnings_) std::cerr << p.file << ":" << p.line << ":" << p.column << ": warning: " << message << "\n";
    }
    Value lookup(const std::string & name,
        const SourcePos & pos,
            const std::shared_ptr < Env > & env) {
        auto colon = name.find(':');
        if (colon == std::string::npos) {
            Value * value = env -> find(name);
            if (!value) fail(pos, "undefined name '" + name + "'");
            return * value;
        }
        Value moduleValue = lookup(name.substr(0, colon), pos, env);
        auto module = std::get_if < std::shared_ptr < Module >> ( & moduleValue);
        if (!module || ! * module) fail(pos, "'" + name.substr(0, colon) + "' is not a loaded module");
        std::string member = name.substr(colon + 1);
        if (!( * module) -> lastEnv) fail(pos, "module '" + ( * module) -> path + "' has not been run");
        if (!( * module) -> exports.count(member)) fail(pos, "module member '" + member + "' is private");
        auto found = ( * module) -> lastEnv -> values.find(member);
        if (found == ( * module) -> lastEnv -> values.end()) fail(pos, "module does not define '" + member + "'");
        return found -> second;
    }
    void assign(const std::shared_ptr < Expr > & target, Value value,
        const std::shared_ptr < Env > & env) {
        if (target -> kind == ExprKind::name) {
            if (target -> name.find(':') != std::string::npos) fail(target -> pos, "cannot assign to a module member");
            if (Value * old = env -> find(target -> name)) * old = std::move(value);
            else env -> values[target -> name] = std::move(value);
            return;
        }
        if (target -> kind == ExprKind::deref || target -> kind == ExprKind::value) {
            Value ptrValue = lookup(target -> name, target -> pos, env);
            auto ptr = std::get_if < std::shared_ptr < Pointer >> ( & ptrValue);
            if (!ptr || ! * ptr) fail(target -> pos, "'" + target -> name + "' is not a pointer");
            ( * ptr) -> cells[( * ptr) -> position] = asInt(value, target -> pos);
            return;
        }
        fail(target -> pos, "assignment target must be a name or [pointer]");
    }
    std::shared_ptr < Pointer > pointer(const std::string & name,
        const SourcePos & pos,
            const std::shared_ptr < Env > & env) {
        Value value = lookup(name, pos, env);
        auto p = std::get_if < std::shared_ptr < Pointer >> ( & value);
        if (!p || ! * p) fail(pos, "'" + name + "' is not a pointer");
        return * p;
    }
    Value eval(const std::shared_ptr < Expr > & expr,
        const std::shared_ptr < Env > & env) {
        if (expr -> kind == ExprKind::literal) return expr -> literal;
        if (expr -> kind == ExprKind::name) return lookup(expr -> name, expr -> pos, env);
        if (expr -> kind == ExprKind::deref || expr -> kind == ExprKind::value) {
            auto p = pointer(expr -> name, expr -> pos, env);
            return p -> cells[p -> position];
        }
        if (expr -> kind == ExprKind::position) {
            auto p = pointer(expr -> name, expr -> pos, env);
            return Big(p -> position + 1);
        }
        if (expr -> kind == ExprKind::unary) return !truthy(eval(expr -> left, env));
        Value left = eval(expr -> left, env), right = eval(expr -> right, env);
        if (expr -> op == "or") return truthy(left) || truthy(right);
        if (expr -> op == "eq") {
            if (left.index() != right.index()) {
                if ((std::holds_alternative < Big > (left) || std::holds_alternative < double > (left)) && (std::holds_alternative < Big > (right) || std::holds_alternative < double > (right))) return asDouble(left, expr -> pos) == asDouble(right, expr -> pos);
                return false;
            }
            return left == right;
        }
        if (expr -> op == "lt") return asDouble(left, expr -> pos) < asDouble(right, expr -> pos);
        if (expr -> op == "gt") return asDouble(left, expr -> pos) > asDouble(right, expr -> pos);
        if (expr -> op == "xor") {
            if (std::holds_alternative < bool > (left) && std::holds_alternative < bool > (right)) return truthy(left) != truthy(right);
            return asInt(left, expr -> pos) ^ asInt(right, expr -> pos);
        }
        bool floating = std::holds_alternative < double > (left) || std::holds_alternative < double > (right);
        if (floating) {
            double a = asDouble(left, expr -> pos), b = asDouble(right, expr -> pos);
            if (expr -> op == "add") return a + b;
            if (expr -> op == "sub") return a - b;
            if (expr -> op == "mul") return a * b;
            if (expr -> op == "div") return a / b;
            if (expr -> op == "mod") return std::fmod(a, b);
        }
        Big a = asInt(left, expr -> pos), b = asInt(right, expr -> pos);
        if (expr -> op == "add") return a + b;
        if (expr -> op == "sub") return a - b;
        if (expr -> op == "mul") return a * b;
        if (expr -> op == "div") {
            if (b == 0) fail(expr -> pos, "integer division by zero");
            return a / b;
        }
        if (expr -> op == "mod") {
            if (b == 0) fail(expr -> pos, "integer modulo by zero");
            return a % b;
        }
        fail(expr -> pos, "unknown operation '" + expr -> op + "'");
    }
    std::shared_ptr < Module > loadModule(const std::string & raw,
        const SourcePos & pos) {
        fs::path path = raw;
        if (path.is_relative()) path = fs::absolute(path);
        std::string key = path.lexically_normal().string();
        if (auto it = modules_.find(key); it != modules_.end()) return it -> second;
        std::ifstream input(key);
        if (!input) fail(pos, "cannot load module '" + raw + "'");
        std::string source((std::istreambuf_iterator < char > (input)), std::istreambuf_iterator < char > ());
        auto module = std::make_shared < Module > ();
        module -> path = key;
        modules_[key] = module;
        try {
            module -> program = Parser(Lexer(source, key).scan()).program();
        } catch (...) {
            modules_.erase(key);
            throw;
        }
        return module;
    }
    Value callValue(Value value,
        const SourcePos & pos) {
        auto
        function = std::get_if < std::shared_ptr < Function >> ( & value);
        if (! function || ! * function) fail(pos, "attempted to call a non-function value");
        try {
            execute(( * function) -> body, std::make_shared < Env > (( * function) -> closure));
        } catch (const ReturnSignal & returned) {
            checkReturnType(( * function) -> returnType, returned.value, pos);
            return returned.value;
        }
        if (!( * function) -> returnType.empty() && ( * function) -> returnType != "nr")
            fail(pos, "function declared to return '" + ( * function) -> returnType + "' returned no value");
        return {};
    }
    static void checkReturnType(const std::string & type,
        const Value & value,
            const SourcePos & pos) {
        if (type.empty() || type == "nr") return;
        bool valid = (type == "int" || type == "integer") ? std::holds_alternative < Big > (value) :
            (type == "flo" || type == "float") ? std::holds_alternative < double > (value) :
            (type == "str" || type == "string") ? std::holds_alternative < std::string > (value) :
            (type == "bool" || type == "boolean") ? std::holds_alternative < bool > (value) : false;
        if (!valid) fail(pos, "function return does not match declared type '" + type + "'");
    }
    void executeOne(const std::shared_ptr < Stmt > & s,
        const std::shared_ptr < Env > & env) {
        switch (s -> kind) {
        case StmtKind::store:
            assign(s -> target, eval(s -> expr, env), env);
            break;
        case StmtKind::modify:
            if (!env -> find(s -> target -> name)) fail(s -> pos, "mdf requires an existing name");
            assign(s -> target, eval(s -> expr, env), env);
            break;
        case StmtKind::arithmetic: {
            auto operation = std::make_shared < Expr > ();
            operation -> kind = ExprKind::binary;
            operation -> pos = s -> pos;
            operation -> op = s -> aux;
            operation -> left = s -> target;
            operation -> right = s -> expr;
            assign(s -> target, eval(operation, env), env);
            break;
        }
        case StmtKind::print:
            std::cout << valueText(eval(s -> expr, env)) << '\n';
            break;
        case StmtKind::error:
            std::cerr << s -> pos.file << ":" << s -> pos.line << ":" << s -> pos.column << ": error: " << valueText(eval(s -> expr, env)) << '\n';
            break;
        case StmtKind::define: {
            auto f = std::make_shared < Function > ();
            f -> body = s -> yes;
            f -> closure = env;
            f -> returnType = s -> returnType;
            env -> values[s -> name] = f;
            if (s -> global && env -> owner) env -> owner -> exports.insert(s -> name);
            break;
        }
        case StmtKind::call:
            (void) callValue(lookup(s -> name, s -> pos, env), s -> pos);
            break;
        case StmtKind::iff:
            if (truthy(eval(s -> expr, env))) {
                try {
                    execute(s -> yes, env);
                } catch (const ThcError & e) {
                    if (!s -> optional) throw;
                    warning(e.pos, "suppressed by nr: " + std::string(e.what()));
                }
            } else execute(s -> no, env);
            break;
        case StmtKind::ret:
            throw ReturnSignal {
                s -> expr ? eval(s -> expr, env) : Value {}
            };
            // mov A B copies A into B; unlike mdf, its operands are source then destination.
        case StmtKind::move:
            assign(s -> expr, eval(s -> target, env), env);
            break;
        case StmtKind::memory: {
            size_t count = 1;
            if (s -> expr) {
                Big n = asInt(eval(s -> expr, env), s -> pos);
                if (n <= 0) fail(s -> pos, "memory size must be positive");
                count = n.convert_to < size_t > ();
            }
            auto p = std::make_shared < Pointer > ();
            p -> cells.assign(count, 0);
            env -> values[s -> name] = p;
            env -> values[s -> aux] = p;
            env -> activePointer = s -> aux;
            if (s -> global && env -> owner) env -> owner -> exports.insert(s -> name);
            break;
        }
        case StmtKind::pointerMove: {
            std::string name = s -> name.empty() ? env -> activePointer : s -> name;
            if (name.empty()) fail(s -> pos, "pm requires a pointer");
            auto p = pointer(name, s -> pos, env);
            Big delta = asInt(eval(s -> expr, env), s -> pos);
            Big next = Big(p -> position) + delta;
            if (next < 0 || next >= p -> cells.size()) fail(s -> pos, "pointer movement is out of bounds");
            p -> position = next.convert_to < size_t > ();
            break;
        }
        case StmtKind::pointerWrite: {
            std::string name = s -> name.empty() ? env -> activePointer : s -> name;
            if (name.empty()) fail(s -> pos, "prb requires a pointer");
            auto p = pointer(name, s -> pos, env);
            p -> cells[p -> position] = asInt(eval(s -> expr, env), s -> pos);
            break;
        }
        case StmtKind::pointerRemove:
            env -> values.erase(s -> name);
            if (env -> activePointer == s -> name) env -> activePointer.clear();
            break;
        case StmtKind::pointerRun:
            fail(s -> pos, "run[p] is not implemented: pointer-code execution is still TBA");
        case StmtKind::load: {
            std::string path = valueText(eval(s -> expr, env));
            auto module = loadModule(path, s -> pos);
            if (s -> expr -> kind == ExprKind::name && s -> expr -> name.find(':') == std::string::npos) env -> values[s -> expr -> name] = module;
            else env -> values[fs::path(path).stem().string()] = module;
            break;
        }
        case StmtKind::run: {
            Value value = eval(s -> expr, env);
            auto module = std::get_if < std::shared_ptr < Module >> ( & value);
            if (!module || ! * module) fail(s -> pos, "run requires a loaded module");
            if (( * module) -> running) fail(s -> pos, "circular module execution involving '" + ( * module) -> path + "'");
            ( * module) -> running = true;
            try {
                auto moduleEnv = std::make_shared < Env > (root_, * module);
                ( * module) -> lastEnv = moduleEnv;
                execute(( * module) -> program, moduleEnv);
                ( * module) -> running = false;
            } catch (...) {
                ( * module) -> running = false;
                throw;
            }
            break;
        }
        case StmtKind::forget: {
            if (s -> expr -> kind == ExprKind::name) {
                if (Value * value = env -> find(s -> expr -> name)) {
                    if (auto module = std::get_if < std::shared_ptr < Module >> (value); module && * module) modules_.erase(( * module) -> path);
                }
                env -> values.erase(s -> expr -> name);
            } else fail(s -> pos, "fgt requires a name");
            break;
        }
        case StmtKind::exportValue:
            if (!env -> owner) warning(s -> pos, "exp at top level has no module effect");
            else {
                if (!env -> values.count(s -> name)) fail(s -> pos, "cannot export undefined name '" + s -> name + "'");
                env -> owner -> exports.insert(s -> name);
            }
            break;
        case StmtKind::fget: {
            if (s -> expr -> kind != ExprKind::name || s -> expr -> name.find(':') == std::string::npos) fail(s -> pos, "fget requires module:function");
            env -> values[s -> name] = lookup(s -> expr -> name, s -> pos, env);
            break;
        }
        case StmtKind::exit:
            throw ExitSignal {};
        }
    }
};

static std::string readFile(const std::string & path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open '" + path + "'");
    return std::string((std::istreambuf_iterator < char > (input)), std::istreambuf_iterator < char > ());
}
static void usage() {
    std::cout << "thc - thcee reference interpreter\n\n" <<
        "Usage:\n  thc run <file.thc> [--no-warnings]\n  thc check <file.thc>\n  thc <file.thc>\n";
}
int main(int argc, char ** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
            usage();
            return argc < 2 ? 1 : 0;
        }
        bool check = false, warnings = true;
        std::string path;
        int start = 1;
        if (std::string(argv[1]) == "run" || std::string(argv[1]) == "check") {
            check = std::string(argv[1]) == "check";
            start = 2;
        }
        for (int i = start; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--no-warnings") warnings = false;
            else if (path.empty()) path = arg;
            else throw std::runtime_error("unexpected argument '" + arg + "'");
        }
        if (path.empty()) {
            usage();
            return 1;
        }
        auto program = Parser(Lexer(readFile(path), path).scan()).program();
        if (!check) Runtime(warnings).execute(program, std::make_shared < Env > ());
        return 0;
    } catch (const ExitSignal & ) {
        return 0;
    } catch (const ThcError & error) {
        std::cerr << error.pos.file << ":" << error.pos.line << ":" << error.pos.column << ": error: " << error.what() << "\n";
        return 1;
    } catch (const std::exception & error) {
        std::cerr << "thc: error: " << error.what() << "\n";
        return 1;
    }
}
