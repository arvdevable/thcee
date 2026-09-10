struct Expr {
    virtual ~Expr() = default;
};

struct NumberExpr : Expr {
    int64_t value;
};

struct StringExpr : Expr {
    std::string value;
};

struct VariableExpr : Expr {
    std::string name;
};

struct BinaryExpr : Expr {
    std::unique_ptr<Expr> left;
    std::string op;
    std::unique_ptr<Expr> right;
};

struct StoreStmt : Statement {
    std::string name;
    std::unique_ptr<Expr> value;
};
struct AddStmt : Statement {
    std::string variable;
    std::unique_ptr<Expr> value;
};
struct PrintStmt : Statement {
    std::unique_ptr<Expr> value;
};