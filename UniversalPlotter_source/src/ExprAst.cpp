#include "ExprAst.h"
#include <cmath>
#include <cstdio>
#include <cctype>
#include <stdexcept>
#include <vector>

namespace expr {

enum class Kind { Num, Var, Neg, Add, Sub, Mul, Div, Pow, Call };

struct Node {
    Kind kind = Kind::Num;
    double num = 0;
    std::string name;            // Var: identifier, Call: function name
    std::vector<NodeP> args;     // Neg: 1, binary: 2, Call: n
};

namespace {

NodeP mk(Kind k, std::vector<NodeP> a, std::string name = {}, double v = 0) {
    auto n = std::make_shared<Node>();
    n->kind = k; n->args = std::move(a); n->name = std::move(name); n->num = v;
    return n;
}
NodeP num(double v) { return mk(Kind::Num, {}, {}, v); }
NodeP var(const std::string &s) { return mk(Kind::Var, {}, s); }
bool isNum(const NodeP &n) { return n->kind == Kind::Num; }
bool isNum(const NodeP &n, double v) { return n->kind == Kind::Num && n->num == v; }

// ---------------------------------------------------------------- parser (same grammar as ExprEval)
class Parser {
public:
    explicit Parser(const std::string &s) : s_(s) {}
    NodeP run() {
        NodeP v = expr();
        ws();
        if (p_ != s_.size()) throw std::runtime_error("unexpected trailing characters in expression");
        return v;
    }
private:
    const std::string &s_;
    size_t p_ = 0;
    void ws() { while (p_ < s_.size() && std::isspace((unsigned char)s_[p_])) ++p_; }
    char peek() { ws(); return p_ < s_.size() ? s_[p_] : '\0'; }

    NodeP expr() {
        NodeP v = term();
        for (;;) {
            char c = peek();
            if (c == '+') { ++p_; v = mk(Kind::Add, {v, term()}); }
            else if (c == '-') { ++p_; v = mk(Kind::Sub, {v, term()}); }
            else break;
        }
        return v;
    }
    NodeP term() {
        NodeP v = unary();
        for (;;) {
            char c = peek();
            if (c == '*') { ++p_; v = mk(Kind::Mul, {v, unary()}); }
            else if (c == '/') { ++p_; v = mk(Kind::Div, {v, unary()}); }
            else break;
        }
        return v;
    }
    NodeP unary() {
        char c = peek();
        if (c == '-') { ++p_; return mk(Kind::Neg, {unary()}); }
        if (c == '+') { ++p_; return unary(); }
        return power();
    }
    NodeP power() {
        NodeP b = atom();
        if (peek() == '^') { ++p_; return mk(Kind::Pow, {b, unary()}); }
        return b;
    }
    NodeP atom() {
        ws();
        if (p_ >= s_.size()) throw std::runtime_error("unexpected end of expression");
        char c = s_[p_];
        if (c == '(') {
            ++p_;
            NodeP v = expr();
            ws();
            if (p_ >= s_.size() || s_[p_] != ')') throw std::runtime_error("missing closing parenthesis");
            ++p_;
            return v;
        }
        if (std::isdigit((unsigned char)c) || c == '.') {
            size_t st = p_;
            while (p_ < s_.size() && (std::isdigit((unsigned char)s_[p_]) || s_[p_] == '.')) ++p_;
            if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
                size_t save = p_;
                ++p_;
                if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) ++p_;
                if (p_ < s_.size() && std::isdigit((unsigned char)s_[p_]))
                    while (p_ < s_.size() && std::isdigit((unsigned char)s_[p_])) ++p_;
                else p_ = save;
            }
            try { return num(std::stod(s_.substr(st, p_ - st))); }
            catch (...) { throw std::runtime_error("bad numeric literal"); }
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t st = p_;
            while (p_ < s_.size() && (std::isalnum((unsigned char)s_[p_]) || s_[p_] == '_')) ++p_;
            std::string id = s_.substr(st, p_ - st);
            ws();
            if (p_ < s_.size() && s_[p_] == '(') {
                ++p_;
                std::vector<NodeP> args{expr()};
                ws();
                while (p_ < s_.size() && s_[p_] == ',') { ++p_; args.push_back(expr()); ws(); }
                if (p_ >= s_.size() || s_[p_] != ')') throw std::runtime_error("missing closing parenthesis in function call");
                ++p_;
                return mk(Kind::Call, std::move(args), id);
            }
            return var(id);
        }
        throw std::runtime_error(std::string("unexpected character '") + c + "'");
    }
};

// ---------------------------------------------------------------- printer
// precedence: + - = 1, * / = 2, unary - = 3, ^ = 4, atoms / calls = 5
int precOf(const NodeP &n) {
    switch (n->kind) {
    case Kind::Add: case Kind::Sub: return 1;
    case Kind::Mul: case Kind::Div: return 2;
    case Kind::Neg: return 3;
    case Kind::Num: return n->num < 0 ? 3 : 5;
    case Kind::Pow: return 4;
    default: return 5;
    }
}

std::string fmtNum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.12g", v);
    return buf;
}

std::string pr(const NodeP &n, int minPrec) {
    std::string s;
    switch (n->kind) {
    case Kind::Num: s = fmtNum(n->num); break;
    case Kind::Var: s = n->name; break;
    case Kind::Neg: s = "-" + pr(n->args[0], 3); break;
    case Kind::Add: s = pr(n->args[0], 1) + " + " + pr(n->args[1], 2); break;
    case Kind::Sub: s = pr(n->args[0], 1) + " - " + pr(n->args[1], 2); break;
    case Kind::Mul: s = pr(n->args[0], 2) + "*" + pr(n->args[1], 3); break;
    case Kind::Div: s = pr(n->args[0], 2) + "/" + pr(n->args[1], 3); break;
    case Kind::Pow: s = pr(n->args[0], 5) + "^" + pr(n->args[1], 3); break;
    case Kind::Call:
        s = n->name + "(";
        for (size_t i = 0; i < n->args.size(); ++i) s += (i ? ", " : "") + pr(n->args[i], 0);
        s += ")";
        break;
    }
    return precOf(n) < minPrec ? "(" + s + ")" : s;
}

// ---------------------------------------------------------------- constructors that keep the sign tidy
NodeP neg(const NodeP &a);

// true when the printed term starts with a minus sign (-u, -3, (-2)*u, (-1)/u ...)
bool leadsNegative(const NodeP &n) {
    switch (n->kind) {
    case Kind::Num: return n->num < 0;
    case Kind::Neg: return true;
    case Kind::Mul: case Kind::Div: return leadsNegative(n->args[0]);
    default: return false;
    }
}

NodeP add(const NodeP &a, const NodeP &b) {            // a + b   (a + (-c) is written a - c)
    if (isNum(a) && isNum(b)) return num(a->num + b->num);
    if (isNum(b, 0)) return a;
    if (isNum(a, 0)) return b;
    if (leadsNegative(b)) return mk(Kind::Sub, {a, neg(b)});
    return mk(Kind::Add, {a, b});
}
NodeP sub(const NodeP &a, const NodeP &b) {            // a - b
    if (isNum(a) && isNum(b)) return num(a->num - b->num);
    if (isNum(b, 0)) return a;
    if (isNum(a, 0)) return neg(b);
    if (leadsNegative(b)) return mk(Kind::Add, {a, neg(b)});
    return mk(Kind::Sub, {a, b});
}
NodeP mul(const NodeP &a, const NodeP &b) {
    if (isNum(a) && isNum(b)) return num(a->num * b->num);
    if (isNum(a, 0) || isNum(b, 0)) return num(0);
    if (isNum(a, 1)) return b;
    if (isNum(b, 1)) return a;
    if (isNum(a, -1)) return neg(b);
    if (isNum(b, -1)) return neg(a);
    if (a->kind == Kind::Neg) return neg(mul(a->args[0], b));
    if (b->kind == Kind::Neg) return neg(mul(a, b->args[0]));
    if (isNum(a) && b->kind == Kind::Mul && isNum(b->args[0]))     // 2*(3*u) -> 6*u
        return mul(num(a->num * b->args[0]->num), b->args[1]);
    return mk(Kind::Mul, {a, b});
}
NodeP div(const NodeP &a, const NodeP &b) {
    if (isNum(a) && isNum(b) && b->num != 0) return num(a->num / b->num);
    if (isNum(a, 0) && !isNum(b, 0)) return num(0);
    if (isNum(b, 1)) return a;
    if (isNum(b) && b->num < 0 && b->num != 0) return neg(div(a, num(-b->num)));
    if (a->kind == Kind::Neg) return neg(div(a->args[0], b));
    if (b->kind == Kind::Neg) return neg(div(a, b->args[0]));
    return mk(Kind::Div, {a, b});
}
NodeP pw(const NodeP &a, const NodeP &b) {
    if (isNum(b, 1)) return a;
    if (isNum(b, 0)) return num(1);
    if (isNum(a) && isNum(b)) {
        double v = std::pow(a->num, b->num);
        if (std::isfinite(v)) return num(v);
    }
    return mk(Kind::Pow, {a, b});
}
NodeP call(const std::string &f, const NodeP &a) { return mk(Kind::Call, {a}, f); }

// Sign flip: every + becomes - and every - becomes + (at the top level of a sum), -(-a) = a.
NodeP neg(const NodeP &a) {
    switch (a->kind) {
    case Kind::Num: return num(-a->num);
    case Kind::Neg: return a->args[0];
    case Kind::Add: return sub(neg(a->args[0]), a->args[1]);     // -(a + b) = -a - b
    case Kind::Sub: return add(neg(a->args[0]), a->args[1]);     // -(a - b) = -a + b
    case Kind::Mul: return mk(Kind::Mul, {neg(a->args[0]), a->args[1]});
    case Kind::Div: return mk(Kind::Div, {neg(a->args[0]), a->args[1]});
    default: return mk(Kind::Neg, {a});
    }
}

} // namespace

// ---------------------------------------------------------------- public API
NodeP parse(const std::string &text) { return simplify(Parser(text).run()); }
std::string print(const NodeP &n) { return pr(n, 0); }
NodeP negate(const NodeP &n) { return neg(n); }
NodeP absolute(const NodeP &n) { return call("abs", n); }

bool usesVariable(const NodeP &n, const std::string &v) {
    if (n->kind == Kind::Var) return n->name == v;
    for (const auto &a : n->args) if (usesVariable(a, v)) return true;
    return false;
}

NodeP substitute(const NodeP &n, const std::string &v, const NodeP &with) {
    if (n->kind == Kind::Var) return n->name == v ? with : n;
    if (n->args.empty()) return n;
    std::vector<NodeP> a;
    for (const auto &c : n->args) a.push_back(substitute(c, v, with));
    return mk(n->kind, std::move(a), n->name, n->num);
}

NodeP simplify(const NodeP &n) {
    if (n->args.empty()) return n;
    std::vector<NodeP> a;
    for (const auto &c : n->args) a.push_back(simplify(c));
    switch (n->kind) {
    case Kind::Neg: return neg(a[0]);
    case Kind::Add: return add(a[0], a[1]);
    case Kind::Sub: return sub(a[0], a[1]);
    case Kind::Mul: return mul(a[0], a[1]);
    case Kind::Div: return div(a[0], a[1]);
    case Kind::Pow: return pw(a[0], a[1]);
    default: return mk(Kind::Call, std::move(a), n->name);
    }
}

NodeP derivative(const NodeP &n, const std::string &x) {
    switch (n->kind) {
    case Kind::Num: return num(0);
    case Kind::Var: return num(n->name == x ? 1 : 0);
    case Kind::Neg: { NodeP d = derivative(n->args[0], x); return d ? neg(d) : nullptr; }
    case Kind::Add: case Kind::Sub: {
        NodeP da = derivative(n->args[0], x), db = derivative(n->args[1], x);
        if (!da || !db) return nullptr;
        return n->kind == Kind::Add ? add(da, db) : sub(da, db);
    }
    case Kind::Mul: {
        const NodeP &u = n->args[0], &v = n->args[1];
        NodeP du = derivative(u, x), dv = derivative(v, x);
        if (!du || !dv) return nullptr;
        return add(mul(du, v), mul(u, dv));
    }
    case Kind::Div: {
        const NodeP &u = n->args[0], &v = n->args[1];
        NodeP du = derivative(u, x), dv = derivative(v, x);
        if (!du || !dv) return nullptr;
        if (isNum(dv, 0)) return div(du, v);
        return div(sub(mul(du, v), mul(u, dv)), pw(v, num(2)));
    }
    case Kind::Pow: {
        const NodeP &u = n->args[0], &v = n->args[1];
        NodeP du = derivative(u, x), dv = derivative(v, x);
        if (!du || !dv) return nullptr;
        if (isNum(dv, 0))                                        // u^c: c*u^(c-1)*u'
            return mul(mul(v, pw(u, sub(v, num(1)))), du);
        if (isNum(du, 0)) {                                      // c^v: c^v*ln(c)*v'
            NodeP lnc = (u->kind == Kind::Var && u->name == "e") ? num(1) : call("log", u);
            return mul(mul(n, lnc), dv);
        }
        // u^v*(v'*ln(u) + v*u'/u)
        return mul(n, add(mul(dv, call("log", u)), div(mul(v, du), u)));
    }
    case Kind::Call: {
        const std::string &f = n->name;
        if (f == "power" && n->args.size() == 2)
            return derivative(mk(Kind::Pow, {n->args[0], n->args[1]}), x);
        if (n->args.size() != 1) return nullptr;
        const NodeP &u = n->args[0];
        NodeP du = derivative(u, x);
        if (!du) return nullptr;
        NodeP outer;                                             // f'(u)
        if (f == "sin") outer = call("cos", u);
        else if (f == "cos") outer = neg(call("sin", u));
        else if (f == "tan") outer = div(num(1), pw(call("cos", u), num(2)));
        else if (f == "asin") outer = div(num(1), call("sqrt", sub(num(1), pw(u, num(2)))));
        else if (f == "acos") outer = neg(div(num(1), call("sqrt", sub(num(1), pw(u, num(2))))));
        else if (f == "atan") outer = div(num(1), add(num(1), pw(u, num(2))));
        else if (f == "sinh") outer = call("cosh", u);
        else if (f == "cosh") outer = call("sinh", u);
        else if (f == "tanh") outer = div(num(1), pw(call("cosh", u), num(2)));
        else if (f == "exp") outer = call("exp", u);
        else if (f == "log") outer = div(num(1), u);
        else if (f == "log10") outer = div(num(1), mul(u, num(std::log(10.0))));
        else if (f == "log2") outer = div(num(1), mul(u, num(std::log(2.0))));
        else if (f == "sqrt") outer = div(num(1), mul(num(2), call("sqrt", u)));
        else if (f == "abs") outer = div(u, call("abs", u));     // sign(u)
        else return nullptr;
        return mul(outer, du);
    }
    }
    return nullptr;
}

// ---------------------------------------------------------------- integration
namespace {

// u = a*x + b with a, b free of x (as expressions). Returns false when u is not linear in x.
bool linearIn(const NodeP &u, const std::string &x, NodeP &a, NodeP &b) {
    if (!usesVariable(u, x)) { a = num(0); b = u; return true; }
    switch (u->kind) {
    case Kind::Var: a = num(1); b = num(0); return true;
    case Kind::Neg: { NodeP a1, b1; if (!linearIn(u->args[0], x, a1, b1)) return false; a = neg(a1); b = neg(b1); return true; }
    case Kind::Add: case Kind::Sub: {
        NodeP a1, b1, a2, b2;
        if (!linearIn(u->args[0], x, a1, b1) || !linearIn(u->args[1], x, a2, b2)) return false;
        const bool plus = u->kind == Kind::Add;
        a = plus ? add(a1, a2) : sub(a1, a2);
        b = plus ? add(b1, b2) : sub(b1, b2);
        return true;
    }
    case Kind::Mul: {
        NodeP a1, b1, a2, b2;
        if (!linearIn(u->args[0], x, a1, b1) || !linearIn(u->args[1], x, a2, b2)) return false;
        if (isNum(a1, 0)) { a = mul(b1, a2); b = mul(b1, b2); return true; }     // const * linear
        if (isNum(a2, 0)) { a = mul(a1, b2); b = mul(b1, b2); return true; }     // linear * const
        return false;
    }
    case Kind::Div: {
        NodeP a1, b1;
        if (usesVariable(u->args[1], x) || !linearIn(u->args[0], x, a1, b1)) return false;
        a = div(a1, u->args[1]); b = div(b1, u->args[1]); return true;
    }
    default: return false;
    }
}

NodeP integ(const NodeP &n, const std::string &x);

// integral of u^e, u = a*x+b, e free of x
NodeP integPower(const NodeP &u, const NodeP &e, const NodeP &a) {
    if (isNum(e, -1)) return div(call("log", call("abs", u)), a);
    NodeP e1 = add(e, num(1));
    return div(pw(u, e1), mul(e1, a));
}

NodeP integ(const NodeP &n, const std::string &x) {
    if (!usesVariable(n, x)) return mul(n, var(x));              // constant: c*x
    switch (n->kind) {
    case Kind::Var: return div(pw(var(x), num(2)), num(2));
    case Kind::Neg: { NodeP i = integ(n->args[0], x); return i ? neg(i) : nullptr; }
    case Kind::Add: case Kind::Sub: {
        NodeP i1 = integ(n->args[0], x), i2 = integ(n->args[1], x);
        if (!i1 || !i2) return nullptr;
        return n->kind == Kind::Add ? add(i1, i2) : sub(i1, i2);
    }
    case Kind::Mul: {
        const NodeP &p = n->args[0], &q = n->args[1];
        if (!usesVariable(p, x)) { NodeP i = integ(q, x); return i ? mul(p, i) : nullptr; }
        if (!usesVariable(q, x)) { NodeP i = integ(p, x); return i ? mul(i, q) : nullptr; }
        return nullptr;
    }
    case Kind::Div: {
        const NodeP &p = n->args[0], &q = n->args[1];
        if (!usesVariable(q, x)) { NodeP i = integ(p, x); return i ? div(i, q) : nullptr; }
        if (usesVariable(p, x)) return nullptr;
        NodeP a, b;                                              // p / q^k  with q linear
        NodeP base = q, e = num(-1);
        if (q->kind == Kind::Pow && !usesVariable(q->args[1], x)) { base = q->args[0]; e = neg(q->args[1]); }
        if (!linearIn(base, x, a, b) || isNum(a, 0)) return nullptr;
        return mul(p, integPower(base, e, a));
    }
    case Kind::Pow: {
        const NodeP &u = n->args[0], &e = n->args[1];
        NodeP a, b;
        if (!usesVariable(e, x)) {
            if (u->kind == Kind::Call && u->args.size() == 1 && isNum(e, 2) && (u->name == "sin" || u->name == "cos")) {
                if (!linearIn(u->args[0], x, a, b) || isNum(a, 0)) return nullptr;
                NodeP arg = u->args[0];
                NodeP half = div(arg, num(2));
                NodeP s2 = div(call("sin", mul(num(2), arg)), num(4));
                return div(u->name == "sin" ? sub(half, s2) : add(half, s2), a);
            }
            if (!linearIn(u, x, a, b) || isNum(a, 0)) return nullptr;
            return integPower(u, e, a);
        }
        if (usesVariable(u, x) || !linearIn(e, x, a, b) || isNum(a, 0)) return nullptr;   // c^(a*x+b)
        const bool isE = u->kind == Kind::Var && u->name == "e";
        return isE ? div(n, a) : div(n, mul(a, call("log", u)));
    }
    case Kind::Call: {
        if (n->args.size() != 1) return nullptr;
        const NodeP &u = n->args[0];
        const std::string &f = n->name;
        NodeP a, b;
        if (!linearIn(u, x, a, b) || isNum(a, 0)) return nullptr;
        if (f == "sin") return div(neg(call("cos", u)), a);
        if (f == "cos") return div(call("sin", u), a);
        if (f == "exp") return div(call("exp", u), a);
        if (f == "sinh") return div(call("cosh", u), a);
        if (f == "cosh") return div(call("sinh", u), a);
        if (f == "tanh") return div(call("log", call("cosh", u)), a);
        if (f == "tan") return div(neg(call("log", call("abs", call("cos", u)))), a);
        if (f == "sqrt") return div(mul(num(2.0 / 3.0), pw(u, num(1.5))), a);
        if (f == "log") return div(sub(mul(u, call("log", u)), u), a);
        if (f == "log10") return div(sub(mul(u, call("log", u)), u), mul(a, num(std::log(10.0))));
        if (f == "log2") return div(sub(mul(u, call("log", u)), u), mul(a, num(std::log(2.0))));
        return nullptr;
    }
    default: return nullptr;
    }
}

} // namespace

NodeP integral(const NodeP &n, const std::string &x) {
    NodeP r = integ(n, x);
    return r ? simplify(r) : nullptr;
}

NodeP addConstant(const NodeP &n, double c) { return add(n, num(c)); }

} // namespace expr
