#include "ExprEval.h"
#include <cmath>
#include <QVector>

namespace {

// Recursive-descent parser evaluated in one pass. Grammar:
//   expr    := term (('+' | '-') term)*
//   term    := unary (('*' | '/') unary)*
//   unary   := '-' unary | power
//   power   := atom ('^' unary)?      (right-assoc)
//   atom    := number | ident '(' expr (',' expr)* ')' | ident | '(' expr ')'
class Parser {
public:
    Parser(const QString &s, const QMap<QString, double> &vars) : m_s(s), m_vars(vars), m_pos(0) {}

    double parse() {
        double v = parseExpr();
        skipWs();
        if (m_pos != m_s.size())
            throw std::runtime_error("unexpected trailing characters in expression");
        return v;
    }

private:
    QString m_s;
    const QMap<QString, double> &m_vars;
    int m_pos;

    void skipWs() { while (m_pos < m_s.size() && m_s[m_pos].isSpace()) m_pos++; }
    QChar peek() { skipWs(); return m_pos < m_s.size() ? m_s[m_pos] : QChar(); }

    double parseExpr() {
        double v = parseTerm();
        for (;;) {
            QChar c = peek();
            if (c == '+') { m_pos++; v += parseTerm(); }
            else if (c == '-') { m_pos++; v -= parseTerm(); }
            else break;
        }
        return v;
    }

    double parseTerm() {
        double v = parseUnary();
        for (;;) {
            QChar c = peek();
            if (c == '*') { m_pos++; v *= parseUnary(); }
            else if (c == '/') { m_pos++; v /= parseUnary(); }
            else break;
        }
        return v;
    }

    double parseUnary() {
        QChar c = peek();
        if (c == '-') { m_pos++; return -parseUnary(); }
        if (c == '+') { m_pos++; return parseUnary(); }
        return parsePower();
    }

    double parsePower() {
        double base = parseAtom();
        QChar c = peek();
        if (c == '^') { m_pos++; double exp = parseUnary(); return std::pow(base, exp); }
        return base;
    }

    double parseAtom() {
        skipWs();
        if (m_pos >= m_s.size()) throw std::runtime_error("unexpected end of expression");
        QChar c = m_s[m_pos];

        if (c == '(') {
            m_pos++;
            double v = parseExpr();
            skipWs();
            if (m_pos >= m_s.size() || m_s[m_pos] != ')')
                throw std::runtime_error("missing closing parenthesis");
            m_pos++;
            return v;
        }

        if (c.isDigit() || c == '.') {
            int start = m_pos;
            while (m_pos < m_s.size() && (m_s[m_pos].isDigit() || m_s[m_pos] == '.')) m_pos++;
            if (m_pos < m_s.size() && (m_s[m_pos] == 'e' || m_s[m_pos] == 'E')) {
                int save = m_pos;
                m_pos++;
                if (m_pos < m_s.size() && (m_s[m_pos] == '+' || m_s[m_pos] == '-')) m_pos++;
                if (m_pos < m_s.size() && m_s[m_pos].isDigit()) {
                    while (m_pos < m_s.size() && m_s[m_pos].isDigit()) m_pos++;
                } else {
                    m_pos = save;
                }
            }
            bool ok = false;
            double v = m_s.mid(start, m_pos - start).toDouble(&ok);
            if (!ok) throw std::runtime_error("bad numeric literal");
            return v;
        }

        if (c.isLetter() || c == '_') {
            int start = m_pos;
            while (m_pos < m_s.size() && (m_s[m_pos].isLetterOrNumber() || m_s[m_pos] == '_')) m_pos++;
            QString ident = m_s.mid(start, m_pos - start);
            skipWs();
            if (m_pos < m_s.size() && m_s[m_pos] == '(') {
                m_pos++;
                QVector<double> args;
                args.push_back(parseExpr());
                skipWs();
                while (m_pos < m_s.size() && m_s[m_pos] == ',') {
                    m_pos++;
                    args.push_back(parseExpr());
                    skipWs();
                }
                if (m_pos >= m_s.size() || m_s[m_pos] != ')')
                    throw std::runtime_error("missing closing parenthesis in function call");
                m_pos++;
                return callFunc(ident, args);
            }
            return lookupIdent(ident);
        }

        throw std::runtime_error(QString("unexpected character '%1'").arg(c).toStdString());
    }

    double lookupIdent(const QString &name) {
        if (name == "pi") return M_PI;
        if (name == "e") return M_E;
        if (m_vars.contains(name)) return m_vars[name];
        throw std::runtime_error(QString("unknown identifier '%1'").arg(name).toStdString());
    }

    double callFunc(const QString &name, const QVector<double> &args) {
        auto arg1 = [&](const char *fname) -> double {
            if (args.size() != 1) throw std::runtime_error(QString("%1() takes exactly 1 argument").arg(fname).toStdString());
            return args[0];
        };
        if (name == "sin") return std::sin(arg1("sin"));
        if (name == "cos") return std::cos(arg1("cos"));
        if (name == "tan") return std::tan(arg1("tan"));
        if (name == "asin") return std::asin(arg1("asin"));
        if (name == "acos") return std::acos(arg1("acos"));
        if (name == "atan") return std::atan(arg1("atan"));
        if (name == "sinh") return std::sinh(arg1("sinh"));
        if (name == "cosh") return std::cosh(arg1("cosh"));
        if (name == "tanh") return std::tanh(arg1("tanh"));
        if (name == "exp") return std::exp(arg1("exp"));
        if (name == "log") return std::log(arg1("log"));
        if (name == "log10") return std::log10(arg1("log10"));
        if (name == "log2") return std::log2(arg1("log2"));
        if (name == "sqrt") return std::sqrt(arg1("sqrt"));
        if (name == "abs") return std::fabs(arg1("abs"));
        if (name == "power") {
            if (args.size() != 2) throw std::runtime_error("power() takes exactly 2 arguments");
            return std::pow(args[0], args[1]);
        }
        throw std::runtime_error(QString("unknown function '%1'").arg(name).toStdString());
    }
};

} // namespace

ExprEval::ExprEval(const QString &expr) : m_expr(expr) {}

double ExprEval::evaluate(const QMap<QString, double> &vars) const {
    Parser p(m_expr, vars);
    return p.parse();
}
