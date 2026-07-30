#include "wmbus_decode/formula.h"
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

namespace wmb {

// ---------------------------------------------------------------------------
// Civil calendar (Howard Hinnant's algorithms, public domain style)
// ---------------------------------------------------------------------------
long f_days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

void f_civil_from_days(long z, int* y, unsigned* m, unsigned* d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yy + (*m <= 2);
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
struct Parser {
    const char* p;
    FResolver resolver;
    void* ctx;
    bool err;
};

static void skip_ws(Parser* ps) {
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r') ps->p++;
}

static FVal mk(double v) { return FVal{ v, FUnit::None, true }; }
static FVal bad() { return FVal{ 0, FUnit::None, false }; }

static FVal parse_expr(Parser* ps);

// Add `n` calendar months to a date value (days since epoch).
static double date_add_months(double days, double n) {
    int y; unsigned m, d;
    f_civil_from_days((long)days, &y, &m, &d);
    long total = (long)y * 12 + (long)m - 1 + (long)n;
    int ny = (int)(total / 12);
    unsigned nm = (unsigned)(total % 12) + 1;
    // clamp day to month length
    static const unsigned MDAYS[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    unsigned md = MDAYS[nm - 1];
    if (nm == 2 && ((ny % 4 == 0 && ny % 100 != 0) || ny % 400 == 0)) md = 29;
    if (d > md) d = md;
    return (double)f_days_from_civil(ny, nm, d);
}

static FVal eval_add(FVal a, FVal b, int sign) {
    if (!a.ok || !b.ok) return bad();
    if (a.unit == FUnit::Date) {
        if (b.unit == FUnit::Month) return FVal{ date_add_months(a.v, sign * b.v), FUnit::Date, true };
        if (b.unit == FUnit::Hour)  return FVal{ a.v + sign * b.v / 24.0, FUnit::Date, true };
        if (b.unit == FUnit::Day)   return FVal{ a.v + sign * b.v, FUnit::Date, true };
        return FVal{ a.v + sign * b.v, FUnit::Date, true };
    }
    if (b.unit == FUnit::Date && sign > 0) {
        if (a.unit == FUnit::Month) return FVal{ date_add_months(b.v, a.v), FUnit::Date, true };
        if (a.unit == FUnit::Hour)  return FVal{ b.v + a.v / 24.0, FUnit::Date, true };
        return FVal{ b.v + a.v, FUnit::Date, true };
    }
    return mk(a.v + sign * b.v);
}

static FVal parse_primary(Parser* ps) {
    skip_ws(ps);
    char c = *ps->p;

    if (c == '(') {
        ps->p++;
        FVal v = parse_expr(ps);
        skip_ws(ps);
        if (*ps->p != ')') return bad();
        ps->p++;
        return v;
    }

    if (c == '"') { // date literal "YYYY-MM-DD"
        ps->p++;
        int y = 0, m = 0, d = 0;
        if (sscanf(ps->p, "%4d-%2d-%2d", &y, &m, &d) != 3) return bad();
        while (*ps->p && *ps->p != '"') ps->p++;
        if (*ps->p != '"') return bad();
        ps->p++;
        return FVal{ (double)f_days_from_civil(y, (unsigned)m, (unsigned)d), FUnit::Date, true };
    }

    if (isdigit((unsigned char)c) || c == '.') {
        char* end = nullptr;
        double v = strtod(ps->p, &end);
        ps->p = end;
        // optional unit suffix: letters (m3, kwh, counter, month, h, s, day, min)
        char unit[12] = {0};
        int ui = 0;
        while (isalpha((unsigned char)*ps->p) && ui < 10) unit[ui++] = *ps->p++;
        if (isdigit((unsigned char)*ps->p) && ui < 10) unit[ui++] = *ps->p++; // m3
        unit[ui] = 0;
        if (strcmp(unit, "month") == 0 || strcmp(unit, "months") == 0) return FVal{ v, FUnit::Month, true };
        if (strcmp(unit, "h") == 0)     return FVal{ v, FUnit::Hour, true };
        if (strcmp(unit, "day") == 0 || strcmp(unit, "d") == 0) return FVal{ v, FUnit::Day, true };
        return mk(v);
    }

    if (isalpha((unsigned char)c) || c == '_') {
        char ident[48];
        int ii = 0;
        while ((isalnum((unsigned char)*ps->p) || *ps->p == '_') && ii < 46) ident[ii++] = *ps->p++;
        ident[ii] = 0;
        skip_ws(ps);
        if (*ps->p == '(') { // function call
            ps->p++;
            FVal args[3];
            int nargs = 0;
            skip_ws(ps);
            if (*ps->p != ')') {
                for (;;) {
                    if (nargs >= 3) return bad();
                    args[nargs++] = parse_expr(ps);
                    skip_ws(ps);
                    if (*ps->p == ',') { ps->p++; continue; }
                    break;
                }
            }
            skip_ws(ps);
            if (*ps->p != ')') return bad();
            ps->p++;
            if (strcmp(ident, "sqrt") == 0 && nargs == 1) return mk(sqrt(args[0].v));
            if (strcmp(ident, "abs") == 0 && nargs == 1)  return mk(fabs(args[0].v));
            if (strcmp(ident, "floor") == 0 && nargs == 1) return mk(floor(args[0].v));
            if (strcmp(ident, "ceil") == 0 && nargs == 1)  return mk(ceil(args[0].v));
            if (strcmp(ident, "round") == 0 && nargs == 1) return mk(round(args[0].v));
            if (strcmp(ident, "min") == 0 && nargs == 2)  return mk(fmin(args[0].v, args[1].v));
            if (strcmp(ident, "max") == 0 && nargs == 2)  return mk(fmax(args[0].v, args[1].v));
            if (strcmp(ident, "pow") == 0 && nargs == 2)  return mk(pow(args[0].v, args[1].v));
            return bad();
        }
        if (strcmp(ident, "true") == 0) return mk(1.0);
        if (strcmp(ident, "false") == 0) return mk(0.0);
        double out;
        if (ps->resolver && ps->resolver(ident, &out, ps->ctx)) return mk(out);
        return bad();
    }

    return bad();
}

static FVal parse_unary(Parser* ps) {
    skip_ws(ps);
    char c = *ps->p;
    if (c == '-') { ps->p++; FVal v = parse_unary(ps); return mk(-v.v); }
    if (c == '+') { ps->p++; return parse_unary(ps); }
    if (c == '!') { ps->p++; FVal v = parse_unary(ps); return mk(v.v == 0 ? 1.0 : 0.0); }
    if (c == '~') { ps->p++; FVal v = parse_unary(ps); return mk((double)~(int64_t)v.v); }
    return parse_primary(ps);
}

static FVal parse_mul(Parser* ps) {
    FVal a = parse_unary(ps);
    for (;;) {
        skip_ws(ps);
        char c = *ps->p;
        if (c != '*' && c != '/' && c != '%') return a;
        ps->p++;
        FVal b = parse_unary(ps);
        if (!a.ok || !b.ok) return bad();
        if (c == '*') {
            // month unit propagation: N * 1month
            if (b.unit == FUnit::Month) { a = FVal{ a.v * b.v, FUnit::Month, true }; continue; }
            if (a.unit == FUnit::Month) { a = FVal{ a.v * b.v, FUnit::Month, true }; continue; }
            a = mk(a.v * b.v);
        } else if (c == '/') {
            if (b.v == 0) return mk(0);
            a = mk(a.v / b.v);
        } else {
            if ((int64_t)b.v == 0) return mk(0);
            a = mk((double)((int64_t)a.v % (int64_t)b.v));
        }
    }
}

static FVal parse_add(Parser* ps) {
    FVal a = parse_mul(ps);
    for (;;) {
        skip_ws(ps);
        char c = *ps->p;
        if (c != '+' && c != '-') return a;
        ps->p++;
        FVal b = parse_mul(ps);
        a = eval_add(a, b, c == '+' ? 1 : -1);
    }
}

static FVal parse_shift(Parser* ps) {
    FVal a = parse_add(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->p[0] == '<' && ps->p[1] == '<') {
            ps->p += 2;
            FVal b = parse_add(ps);
            a = mk((double)((int64_t)a.v << (int)b.v));
        } else if (ps->p[0] == '>' && ps->p[1] == '>') {
            ps->p += 2;
            FVal b = parse_add(ps);
            a = mk((double)((int64_t)a.v >> (int)b.v));
        } else return a;
    }
}

static FVal parse_rel(Parser* ps) {
    FVal a = parse_shift(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->p[0] == '<' && ps->p[1] == '=') { ps->p += 2; FVal b = parse_shift(ps); a = mk(a.v <= b.v ? 1 : 0); }
        else if (ps->p[0] == '>' && ps->p[1] == '=') { ps->p += 2; FVal b = parse_shift(ps); a = mk(a.v >= b.v ? 1 : 0); }
        else if (ps->p[0] == '<') { ps->p++; FVal b = parse_shift(ps); a = mk(a.v < b.v ? 1 : 0); }
        else if (ps->p[0] == '>') { ps->p++; FVal b = parse_shift(ps); a = mk(a.v > b.v ? 1 : 0); }
        else return a;
    }
}

static FVal parse_eq(Parser* ps) {
    FVal a = parse_rel(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->p[0] == '=' && ps->p[1] == '=') { ps->p += 2; FVal b = parse_rel(ps); a = mk(a.v == b.v ? 1 : 0); }
        else if (ps->p[0] == '!' && ps->p[1] == '=') { ps->p += 2; FVal b = parse_rel(ps); a = mk(a.v != b.v ? 1 : 0); }
        else return a;
    }
}

static FVal parse_band(Parser* ps) {
    FVal a = parse_eq(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->p[0] == '&' && ps->p[1] != '&') { ps->p++; FVal b = parse_eq(ps); a = mk((double)((int64_t)a.v & (int64_t)b.v)); }
        else return a;
    }
}

static FVal parse_bxor(Parser* ps) {
    FVal a = parse_band(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->p[0] == '^') { ps->p++; FVal b = parse_band(ps); a = mk((double)((int64_t)a.v ^ (int64_t)b.v)); }
        else return a;
    }
}

static FVal parse_bitor(Parser* ps) {
    FVal a = parse_bxor(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->p[0] == '|' && ps->p[1] != '|') { ps->p++; FVal b = parse_bxor(ps); a = mk((double)((int64_t)a.v | (int64_t)b.v)); }
        else return a;
    }
}

static FVal parse_land(Parser* ps) {
    FVal a = parse_bitor(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->p[0] == '&' && ps->p[1] == '&') {
            ps->p += 2;
            FVal b = parse_bitor(ps);
            a = mk((a.v != 0 && b.v != 0) ? 1 : 0);
        } else return a;
    }
}

static FVal parse_lor(Parser* ps) {
    FVal a = parse_land(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->p[0] == '|' && ps->p[1] == '|') {
            ps->p += 2;
            FVal b = parse_land(ps);
            a = mk((a.v != 0 || b.v != 0) ? 1 : 0);
        } else return a;
    }
}

static FVal parse_expr(Parser* ps) {
    return parse_lor(ps);
}

FVal formula_eval(const char* expr, FResolver resolver, void* ctx) {
    Parser ps{ expr, resolver, ctx, false };
    FVal v = parse_expr(&ps);
    skip_ws(&ps);
    if (!v.ok) return v;
    return v;
}

} // namespace wmb
