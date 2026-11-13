#pragma once
#include <QString>
#include <QMap>
#include <stdexcept>

// A tiny recursive-descent math expression evaluator, mirroring the
// whitelisted namespace exposed to custom fit functions in the Python app
// (only "x", user parameter names, and a fixed set of math functions --
// no way to import modules or otherwise escape the sandbox).
//
// Supports: + - * / ^ (power), unary -, parentheses, numeric literals,
// variables, and functions: sin cos tan asin acos atan sinh cosh tanh
// exp log log10 log2 sqrt abs. Constants: pi, e.
class ExprEval {
public:
    explicit ExprEval(const QString &expr);

    // Evaluate with the given variable bindings (e.g. {"x": 2.0, "a": 1.5}).
    // Throws std::runtime_error on parse/eval error.
    double evaluate(const QMap<QString, double> &vars) const;

private:
    QString m_expr;
};
