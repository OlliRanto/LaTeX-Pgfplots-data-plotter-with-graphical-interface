#pragma once
#include <memory>
#include <string>

// Symbolic side of the expression language of ExprEval (same grammar), used by the Data editor so that
// plotted functions are edited as MATH (the result is a new formula) and not as sampled points.
//   parse()      text -> expression tree (throws std::runtime_error on a syntax error)
//   print()      tree -> text that ExprEval and the LaTeX export can read again
//   negate()     sign flip: + becomes - and - becomes + (term by term), -(a*b) -> (-a)*b, ...
//   substitute() replaces a variable by another expression (y -> (sin(x)))
//   derivative() d/dx; returns nullptr when a function has no known derivative
//   integral()   antiderivative of polynomials, powers of linear terms, sin/cos/exp/log/sqrt/... (nullptr otherwise)
namespace expr {

struct Node;
using NodeP = std::shared_ptr<const Node>;

NodeP parse(const std::string &text);
std::string print(const NodeP &n);
NodeP negate(const NodeP &n);
NodeP absolute(const NodeP &n);
NodeP substitute(const NodeP &n, const std::string &var, const NodeP &with);
NodeP simplify(const NodeP &n);
NodeP derivative(const NodeP &n, const std::string &var);   // nullptr if not possible
NodeP integral(const NodeP &n, const std::string &var);       // an antiderivative; nullptr if none is known
NodeP addConstant(const NodeP &n, double c);                   // n + c, written tidily (n - 2 for c = -2)
bool usesVariable(const NodeP &n, const std::string &var);

} // namespace expr
