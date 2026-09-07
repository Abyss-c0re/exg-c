#include "np_algo.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *np_algo_name(int id)
{
    static const char *n[NP_ALGO_N] = {"detect", "sign", "mean", "energy",
                                       "delta",  "fold", "proton", "compare"};
    if (id < 0 || id >= NP_ALGO_N) {
        return "detect";
    }
    return n[id];
}

const char *np_algo_rule(int id)
{
    switch (id) {
    case NP_ALGO_DETECT:
        return "1 if ID is SIGNAL";
    case NP_ALGO_SIGN:
        return "1 if last > 0";
    case NP_ALGO_MEAN:
        return "1 if |last| > 0.85·mean|x|";
    case NP_ALGO_ENERGY:
        return "1 if rms > 1.05·mean|x|";
    case NP_ALGO_DELTA:
        return "1 if |step| > 1.10·mean|dx|";
    case NP_ALGO_FOLD:
        return "1 if majority of samples > 0";
    case NP_ALGO_PROTON:
        return "1 if +energy > half total";
    case NP_ALGO_COMPARE:
        return "1 if ch1 < ch5";
    default:
        return "1 if ID is SIGNAL";
    }
}

static float mean_abs(const float *x, int n)
{
    int i;
    double s = 0;
    if (n < 1) {
        return 0.f;
    }
    for (i = 0; i < n; i++) {
        s += fabs((double)x[i]);
    }
    return (float)(s / n);
}

int np_algo_bit(int id, const float *x, int n, int detect_bit)
{
    float last, ma, rms, md;
    int i, above;
    double e = 0, ep = 0;

    if (id == NP_ALGO_DETECT) {
        return detect_bit ? 1 : 0;
    }
    if (id == NP_ALGO_CUSTOM) {
        return 0;
    }
    if (!x || n < 2) {
        return 0;
    }
    last = x[n - 1];
    ma = mean_abs(x, n);
    if (id == NP_ALGO_SIGN) {
        return last > 0.f ? 1 : 0;
    }
    if (id == NP_ALGO_MEAN) {
        return fabsf(last) > ma * 0.85f ? 1 : 0;
    }
    for (i = 0; i < n; i++) {
        e += (double)x[i] * (double)x[i];
        if (x[i] > 0.f) {
            ep += (double)x[i] * (double)x[i];
        }
    }
    rms = (float)sqrt(e / (double)n);
    if (id == NP_ALGO_ENERGY) {
        return rms > ma * 1.05f ? 1 : 0;
    }
    if (id == NP_ALGO_DELTA) {
        md = 0.f;
        for (i = 1; i < n; i++) {
            md += fabsf(x[i] - x[i - 1]);
        }
        md /= (float)(n - 1);
        return fabsf(last - x[n - 2]) > md * 1.10f ? 1 : 0;
    }
    if (id == NP_ALGO_FOLD) {
        above = 0;
        for (i = 0; i < n; i++) {
            if (x[i] > 0.f) {
                above++;
            }
        }
        return above > n / 2 ? 1 : 0;
    }
    if (id == NP_ALGO_PROTON) {
        return (e > 1e-12 && ep > 0.50 * e) ? 1 : 0;
    }
    return detect_bit ? 1 : 0;
}

const char *np_algo_def_src(int id)
{
    static const char *s[NP_ALGO_N] = {
        "if signal == ON then ON\nelse OFF\n",
        "if ch > 0 then ON\nelse OFF\n",
        "if abs(ch) > 0.85 * mean then ON\nelse OFF\n",
        "if rms > 1.05 * mean then ON\nelse OFF\n",
        "if abs(ch - prev) > 1.10 * dxmean then ON\nelse OFF\n",
        "if above > 0.5 then ON\nelse OFF\n",
        "if pos > 0.5 then ON\nelse OFF\n",
        "if ch2 < ch5 then ch3 ON\nelse ch3 OFF\n",
    };
    if (id < 0 || id >= NP_ALGO_N) {
        return s[NP_ALGO_COMPARE];
    }
    return s[id];
}

enum {
    OP_HALT = 0,
    OP_PUSHC,
    OP_PUSHV,
    OP_PUSHCH,
    OP_PUSHMEAN,
    OP_PUSHRMS,
    OP_PUSHN,
    OP_PUSHCHN,
    OP_PUSHMEANN,
    OP_PUSHRMSN,
    OP_PUSHNN,
    OP_PUSHPREV,
    OP_PUSHDX,
    OP_PUSHABOVE,
    OP_PUSHPOS,
    OP_PUSHSIG,
    OP_PUSHPREVN,
    OP_PUSHDXN,
    OP_PUSHABOVEN,
    OP_PUSHPOSN,
    OP_PUSHSIGN,
    OP_ABS,
    OP_NEG,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_LT,
    OP_GT,
    OP_LE,
    OP_GE,
    OP_EQ,
    OP_NE,
    OP_AND,
    OP_OR,
    OP_NOT,
    OP_JZ,
    OP_JMP,
    OP_STORE,
    OP_BIT,
    OP_BITN
};

#define NP_ALGO_OPS 96
#define NP_ALGO_VARS 8
#define NP_ALGO_STACK 16

struct np_op {
    unsigned char op;
    float f;
    int i;
};

struct np_prog {
    struct np_op op[NP_ALGO_OPS];
    int n;
    char vname[NP_ALGO_VARS][12];
    int nv;
};

enum { TK_EOF = 0, TK_ID, TK_NUM, TK_OP };

struct np_lex {
    const char *s;
    int line;
    int kind;
    char tok[32];
    float num;
};

static int kw_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void lex_skip(struct np_lex *L)
{
    for (;;) {
        while (*L->s == ' ' || *L->s == '\t' || *L->s == '\r') {
            L->s++;
        }
        if (*L->s == '\n') {
            L->line++;
            L->s++;
            continue;
        }
        if (L->s[0] == '#' || (L->s[0] == '/' && L->s[1] == '/')) {
            while (*L->s && *L->s != '\n') {
                L->s++;
            }
            continue;
        }
        return;
    }
}

static void lex_next(struct np_lex *L)
{
    const char *p;
    int n;
    lex_skip(L);
    if (!L->s[0]) {
        L->kind = TK_EOF;
        L->tok[0] = 0;
        return;
    }
    if (isalpha((unsigned char)L->s[0]) || L->s[0] == '_') {
        p = L->s;
        while (isalnum((unsigned char)*L->s) || *L->s == '_') {
            L->s++;
        }
        n = (int)(L->s - p);
        if (n > 31) {
            n = 31;
        }
        memcpy(L->tok, p, (size_t)n);
        L->tok[n] = 0;
        if (kw_eq(L->tok, "on")) {
            L->kind = TK_NUM;
            L->num = 1.f;
        } else if (kw_eq(L->tok, "off")) {
            L->kind = TK_NUM;
            L->num = 0.f;
        } else if (kw_eq(L->tok, "and")) {
            snprintf(L->tok, sizeof(L->tok), "%s", "AND");
            L->kind = TK_OP;
        } else if (kw_eq(L->tok, "or")) {
            snprintf(L->tok, sizeof(L->tok), "%s", "OR");
            L->kind = TK_OP;
        } else if (kw_eq(L->tok, "not")) {
            snprintf(L->tok, sizeof(L->tok), "%s", "NOT");
            L->kind = TK_OP;
        } else {
            L->kind = TK_ID;
        }
        return;
    }
    if (isdigit((unsigned char)L->s[0]) ||
        (L->s[0] == '.' && isdigit((unsigned char)L->s[1]))) {
        char *end = NULL;
        L->num = (float)strtod(L->s, &end);
        snprintf(L->tok, sizeof(L->tok), "%s", "num");
        L->s = end ? end : L->s + 1;
        L->kind = TK_NUM;
        return;
    }
    if ((L->s[0] == '<' || L->s[0] == '>' || L->s[0] == '=' || L->s[0] == '!') &&
        L->s[1] == '=') {
        L->tok[0] = L->s[0];
        L->tok[1] = '=';
        L->tok[2] = 0;
        L->s += 2;
        L->kind = TK_OP;
        return;
    }
    if (L->s[0] == '&' && L->s[1] == '&') {
        snprintf(L->tok, sizeof(L->tok), "%s", "AND");
        L->s += 2;
        L->kind = TK_OP;
        return;
    }
    if (L->s[0] == '|' && L->s[1] == '|') {
        snprintf(L->tok, sizeof(L->tok), "%s", "OR");
        L->s += 2;
        L->kind = TK_OP;
        return;
    }
    L->tok[0] = L->s[0];
    L->tok[1] = 0;
    L->s++;
    L->kind = TK_OP;
}

static int emit(struct np_prog *P, unsigned char op, float f, int i, char *err,
                int errn, int line)
{
    if (P->n >= NP_ALGO_OPS) {
        snprintf(err, (size_t)errn, "line %d: program too long", line);
        return -1;
    }
    P->op[P->n].op = op;
    P->op[P->n].f = f;
    P->op[P->n].i = i;
    P->n++;
    return 0;
}

static int find_var(struct np_prog *P, const char *name, int create)
{
    int i;
    for (i = 0; i < P->nv; i++) {
        if (kw_eq(P->vname[i], name)) {
            return i;
        }
    }
    if (!create || P->nv >= NP_ALGO_VARS) {
        return -1;
    }
    snprintf(P->vname[P->nv], sizeof(P->vname[0]), "%.11s", name);
    return P->nv++;
}

static int name_ix(const char *tok, const char *pre)
{
    int n = 0, i = 0;
    while (pre[i]) {
        if (tolower((unsigned char)tok[i]) != pre[i]) {
            return -1;
        }
        i++;
    }
    if (!isdigit((unsigned char)tok[i])) {
        return -1;
    }
    while (isdigit((unsigned char)tok[i])) {
        n = n * 10 + (tok[i] - '0');
        i++;
    }
    if (tok[i] || n < 1 || n > 8) {
        return -1;
    }
    return n - 1;
}

static int reserved(const char *name)
{
    return kw_eq(name, "ch") || kw_eq(name, "last") || kw_eq(name, "mean") ||
           kw_eq(name, "rms") || kw_eq(name, "n") || kw_eq(name, "bit") ||
           kw_eq(name, "if") || kw_eq(name, "then") || kw_eq(name, "else") ||
           kw_eq(name, "elif") || kw_eq(name, "end") || kw_eq(name, "let") ||
           kw_eq(name, "abs") || kw_eq(name, "and") || kw_eq(name, "or") ||
           kw_eq(name, "not") || kw_eq(name, "prev") || kw_eq(name, "dxmean") ||
           kw_eq(name, "above") || kw_eq(name, "pos") ||
           kw_eq(name, "signal") || name_ix(name, "ch") >= 0 ||
           name_ix(name, "last") >= 0 || name_ix(name, "mean") >= 0 ||
           name_ix(name, "rms") >= 0 || name_ix(name, "prev") >= 0 ||
           name_ix(name, "dxmean") >= 0 || name_ix(name, "above") >= 0 ||
           name_ix(name, "pos") >= 0 || name_ix(name, "signal") >= 0;
}

static int parse_expr(struct np_lex *L, struct np_prog *P, char *err, int errn);

static int parse_unary(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    if ((L->kind == TK_OP && L->tok[0] == '-' && L->tok[1] == 0)) {
        lex_next(L);
        if (parse_unary(L, P, err, errn) != 0) {
            return -1;
        }
        return emit(P, OP_NEG, 0, 0, err, errn, L->line);
    }
    if (L->kind == TK_OP &&
        (strcmp(L->tok, "NOT") == 0 || (L->tok[0] == '!' && L->tok[1] == 0))) {
        lex_next(L);
        if (parse_unary(L, P, err, errn) != 0) {
            return -1;
        }
        return emit(P, OP_NOT, 0, 0, err, errn, L->line);
    }
    if (L->kind == TK_ID && kw_eq(L->tok, "abs")) {
        lex_next(L);
        if (!(L->kind == TK_OP && L->tok[0] == '(')) {
            snprintf(err, (size_t)errn, "line %d: abs needs ()", L->line);
            return -1;
        }
        lex_next(L);
        if (parse_expr(L, P, err, errn) != 0) {
            return -1;
        }
        if (!(L->kind == TK_OP && L->tok[0] == ')')) {
            snprintf(err, (size_t)errn, "line %d: missing )", L->line);
            return -1;
        }
        lex_next(L);
        return emit(P, OP_ABS, 0, 0, err, errn, L->line);
    }
    if (L->kind == TK_NUM) {
        if (emit(P, OP_PUSHC, L->num, 0, err, errn, L->line) != 0) {
            return -1;
        }
        lex_next(L);
        return 0;
    }
    if (L->kind == TK_OP && L->tok[0] == '(') {
        lex_next(L);
        if (parse_expr(L, P, err, errn) != 0) {
            return -1;
        }
        if (!(L->kind == TK_OP && L->tok[0] == ')')) {
            snprintf(err, (size_t)errn, "line %d: missing )", L->line);
            return -1;
        }
        lex_next(L);
        return 0;
    }
    if (L->kind == TK_ID) {
        int slot, ix;
        if ((ix = name_ix(L->tok, "ch")) >= 0 ||
            (ix = name_ix(L->tok, "last")) >= 0) {
            if (emit(P, OP_PUSHCHN, 0, ix, err, errn, L->line) != 0) {
                return -1;
            }
        } else if ((ix = name_ix(L->tok, "mean")) >= 0) {
            if (emit(P, OP_PUSHMEANN, 0, ix, err, errn, L->line) != 0) {
                return -1;
            }
        } else if ((ix = name_ix(L->tok, "rms")) >= 0) {
            if (emit(P, OP_PUSHRMSN, 0, ix, err, errn, L->line) != 0) {
                return -1;
            }
        } else if ((ix = name_ix(L->tok, "prev")) >= 0) {
            if (emit(P, OP_PUSHPREVN, 0, ix, err, errn, L->line) != 0) {
                return -1;
            }
        } else if ((ix = name_ix(L->tok, "dxmean")) >= 0) {
            if (emit(P, OP_PUSHDXN, 0, ix, err, errn, L->line) != 0) {
                return -1;
            }
        } else if ((ix = name_ix(L->tok, "above")) >= 0) {
            if (emit(P, OP_PUSHABOVEN, 0, ix, err, errn, L->line) != 0) {
                return -1;
            }
        } else if ((ix = name_ix(L->tok, "pos")) >= 0) {
            if (emit(P, OP_PUSHPOSN, 0, ix, err, errn, L->line) != 0) {
                return -1;
            }
        } else if ((ix = name_ix(L->tok, "signal")) >= 0) {
            if (emit(P, OP_PUSHSIGN, 0, ix, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "ch") || kw_eq(L->tok, "last")) {
            if (emit(P, OP_PUSHCH, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "mean")) {
            if (emit(P, OP_PUSHMEAN, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "rms")) {
            if (emit(P, OP_PUSHRMS, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "n")) {
            if (emit(P, OP_PUSHN, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "prev")) {
            if (emit(P, OP_PUSHPREV, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "dxmean")) {
            if (emit(P, OP_PUSHDX, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "above")) {
            if (emit(P, OP_PUSHABOVE, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "pos")) {
            if (emit(P, OP_PUSHPOS, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else if (kw_eq(L->tok, "signal")) {
            if (emit(P, OP_PUSHSIG, 0, 0, err, errn, L->line) != 0) {
                return -1;
            }
        } else {
            slot = find_var(P, L->tok, 0);
            if (slot < 0) {
                snprintf(err, (size_t)errn, "line %d: unknown %s", L->line,
                         L->tok);
                return -1;
            }
            if (emit(P, OP_PUSHV, 0, slot, err, errn, L->line) != 0) {
                return -1;
            }
        }
        lex_next(L);
        return 0;
    }
    snprintf(err, (size_t)errn, "line %d: expected value", L->line);
    return -1;
}

static int parse_mul(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    if (parse_unary(L, P, err, errn) != 0) {
        return -1;
    }
    while (L->kind == TK_OP && (L->tok[0] == '*' || L->tok[0] == '/') &&
           L->tok[1] == 0) {
        char op = L->tok[0];
        lex_next(L);
        if (parse_unary(L, P, err, errn) != 0) {
            return -1;
        }
        if (emit(P, op == '*' ? OP_MUL : OP_DIV, 0, 0, err, errn, L->line) !=
            0) {
            return -1;
        }
    }
    return 0;
}

static int parse_add(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    if (parse_mul(L, P, err, errn) != 0) {
        return -1;
    }
    while (L->kind == TK_OP && (L->tok[0] == '+' || L->tok[0] == '-') &&
           L->tok[1] == 0) {
        char op = L->tok[0];
        lex_next(L);
        if (parse_mul(L, P, err, errn) != 0) {
            return -1;
        }
        if (emit(P, op == '+' ? OP_ADD : OP_SUB, 0, 0, err, errn, L->line) !=
            0) {
            return -1;
        }
    }
    return 0;
}

static int parse_cmp(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    unsigned char op;
    if (parse_add(L, P, err, errn) != 0) {
        return -1;
    }
    while (L->kind == TK_OP &&
           (strcmp(L->tok, "<") == 0 || strcmp(L->tok, ">") == 0 ||
            strcmp(L->tok, "<=") == 0 || strcmp(L->tok, ">=") == 0 ||
            strcmp(L->tok, "==") == 0 || strcmp(L->tok, "!=") == 0)) {
        if (strcmp(L->tok, "<") == 0) {
            op = OP_LT;
        } else if (strcmp(L->tok, ">") == 0) {
            op = OP_GT;
        } else if (strcmp(L->tok, "<=") == 0) {
            op = OP_LE;
        } else if (strcmp(L->tok, ">=") == 0) {
            op = OP_GE;
        } else if (strcmp(L->tok, "==") == 0) {
            op = OP_EQ;
        } else {
            op = OP_NE;
        }
        lex_next(L);
        if (parse_add(L, P, err, errn) != 0) {
            return -1;
        }
        if (emit(P, op, 0, 0, err, errn, L->line) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_and(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    if (parse_cmp(L, P, err, errn) != 0) {
        return -1;
    }
    while (L->kind == TK_OP && strcmp(L->tok, "AND") == 0) {
        lex_next(L);
        if (parse_cmp(L, P, err, errn) != 0) {
            return -1;
        }
        if (emit(P, OP_AND, 0, 0, err, errn, L->line) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_expr(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    if (parse_and(L, P, err, errn) != 0) {
        return -1;
    }
    while (L->kind == TK_OP && strcmp(L->tok, "OR") == 0) {
        lex_next(L);
        if (parse_and(L, P, err, errn) != 0) {
            return -1;
        }
        if (emit(P, OP_OR, 0, 0, err, errn, L->line) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_stmt(struct np_lex *L, struct np_prog *P, char *err, int errn);
static int parse_block(struct np_lex *L, struct np_prog *P, char *err, int errn);

static int parse_onoff(struct np_lex *L, float *v, char *err, int errn)
{
    if (L->kind == TK_ID && kw_eq(L->tok, "is")) {
        lex_next(L);
    }
    if (L->kind == TK_OP && L->tok[0] == '=' && L->tok[1] == 0) {
        lex_next(L);
    }
    if (L->kind != TK_NUM) {
        snprintf(err, (size_t)errn, "line %d: expected ON or OFF", L->line);
        return -1;
    }
    *v = L->num;
    lex_next(L);
    return 0;
}

static int parse_set_ch(struct np_lex *L, struct np_prog *P, int ch, char *err,
                        int errn)
{
    float v;
    if (parse_onoff(L, &v, err, errn) != 0) {
        return -1;
    }
    if (emit(P, OP_PUSHC, v, 0, err, errn, L->line) != 0) {
        return -1;
    }
    if (ch < 0) {
        return emit(P, OP_BIT, 0, 0, err, errn, L->line);
    }
    return emit(P, OP_BITN, 0, ch, err, errn, L->line);
}

static int parse_then_body(struct np_lex *L, struct np_prog *P, char *err,
                           int errn)
{
    int ix;
    if (L->kind == TK_NUM) {
        return parse_set_ch(L, P, -1, err, errn);
    }
    if (L->kind == TK_ID && (ix = name_ix(L->tok, "ch")) >= 0) {
        lex_next(L);
        return parse_set_ch(L, P, ix, err, errn);
    }
    return parse_block(L, P, err, errn);
}

static int at_if_end(const struct np_lex *L)
{
    return L->kind == TK_EOF ||
           (L->kind == TK_ID && (kw_eq(L->tok, "else") || kw_eq(L->tok, "elif") ||
                                 kw_eq(L->tok, "end")));
}

static int parse_block(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    while (!at_if_end(L)) {
        if (parse_stmt(L, P, err, errn) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_if(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    int jz, jmp, endpc;
    lex_next(L); /* skip IF / ELIF */
    if (parse_expr(L, P, err, errn) != 0) {
        return -1;
    }
    if (!(L->kind == TK_ID && kw_eq(L->tok, "then"))) {
        snprintf(err, (size_t)errn, "line %d: expected THEN", L->line);
        return -1;
    }
    lex_next(L);
    jz = P->n;
    if (emit(P, OP_JZ, 0, 0, err, errn, L->line) != 0) {
        return -1;
    }
    if (parse_then_body(L, P, err, errn) != 0) {
        return -1;
    }
    jmp = P->n;
    if (emit(P, OP_JMP, 0, 0, err, errn, L->line) != 0) {
        return -1;
    }
    P->op[jz].i = P->n;
    if (L->kind == TK_ID && kw_eq(L->tok, "elif")) {
        if (parse_if(L, P, err, errn) != 0) {
            return -1;
        }
        P->op[jmp].i = P->n;
        return 0;
    }
    if (L->kind == TK_ID && kw_eq(L->tok, "else")) {
        lex_next(L);
        if (parse_then_body(L, P, err, errn) != 0) {
            return -1;
        }
    }
    if (L->kind == TK_ID && kw_eq(L->tok, "end")) {
        lex_next(L);
    }
    endpc = P->n;
    P->op[jmp].i = endpc;
    return 0;
}

static int parse_stmt(struct np_lex *L, struct np_prog *P, char *err, int errn)
{
    int ix;
    if (L->kind == TK_ID && kw_eq(L->tok, "if")) {
        return parse_if(L, P, err, errn);
    }
    if (L->kind == TK_ID && (ix = name_ix(L->tok, "ch")) >= 0) {
        lex_next(L);
        return parse_set_ch(L, P, ix, err, errn);
    }
    if (L->kind == TK_ID && kw_eq(L->tok, "let")) {
        char name[32];
        int slot;
        lex_next(L);
        if (L->kind != TK_ID) {
            snprintf(err, (size_t)errn, "line %d: LET needs a name", L->line);
            return -1;
        }
        snprintf(name, sizeof(name), "%s", L->tok);
        lex_next(L);
        if (!(L->kind == TK_OP && L->tok[0] == '=' && L->tok[1] == 0)) {
            snprintf(err, (size_t)errn, "line %d: LET needs =", L->line);
            return -1;
        }
        lex_next(L);
        if (parse_expr(L, P, err, errn) != 0) {
            return -1;
        }
        if (kw_eq(name, "bit")) {
            return emit(P, OP_BIT, 0, 0, err, errn, L->line);
        }
        if ((ix = name_ix(name, "ch")) >= 0) {
            return emit(P, OP_BITN, 0, ix, err, errn, L->line);
        }
        if (reserved(name)) {
            snprintf(err, (size_t)errn, "line %d: %s is reserved", L->line, name);
            return -1;
        }
        slot = find_var(P, name, 1);
        if (slot < 0) {
            snprintf(err, (size_t)errn, "line %d: too many names", L->line);
            return -1;
        }
        return emit(P, OP_STORE, 0, slot, err, errn, L->line);
    }
    snprintf(err, (size_t)errn, "line %d: expected IF or LET", L->line);
    return -1;
}

int np_algo_compile(const char *src, char *err, int errn)
{
    struct np_lex L;
    struct np_prog P;
    if (err && errn > 0) {
        err[0] = 0;
    }
    if (!src || !src[0]) {
        snprintf(err, (size_t)errn, "empty");
        return -1;
    }
    memset(&P, 0, sizeof(P));
    memset(&L, 0, sizeof(L));
    L.s = src;
    L.line = 1;
    lex_next(&L);
    if (L.kind == TK_EOF) {
        snprintf(err, (size_t)errn, "empty");
        return -1;
    }
    while (L.kind != TK_EOF) {
        if (parse_stmt(&L, &P, err, errn) != 0) {
            return -1;
        }
    }
    if (emit(&P, OP_HALT, 0, 0, err, errn, L.line) != 0) {
        return -1;
    }
    return 0;
}

static int compile_full(const char *src, struct np_prog *P, char *err, int errn)
{
    struct np_lex L;
    memset(P, 0, sizeof(*P));
    memset(&L, 0, sizeof(L));
    L.s = src;
    L.line = 1;
    lex_next(&L);
    if (L.kind == TK_EOF) {
        snprintf(err, (size_t)errn, "empty");
        return -1;
    }
    while (L.kind != TK_EOF) {
        if (parse_stmt(&L, P, err, errn) != 0) {
            return -1;
        }
    }
    return emit(P, OP_HALT, 0, 0, err, errn, L.line);
}

static float bank_at(const float *v, int i)
{
    if (i < 0 || i > 7) {
        return 0.f;
    }
    return v[i];
}

static int eval_prog(const struct np_prog *P, const struct np_algo_bank *bank,
                     struct np_algo_out *out)
{
    float st[NP_ALGO_STACK];
    float var[NP_ALGO_VARS];
    float ch = 0, ma = 0, rms = 0, nn = 0;
    float prev = 0, dx = 0, above = 0, pos = 0, sig = 0;
    int sp = 0, pc = 0;
    int bit = 0;
    int self = bank ? bank->self : -1;
    struct np_algo_out local;

    if (!out) {
        out = &local;
    }
    memset(out, 0, sizeof(*out));
    memset(var, 0, sizeof(var));
    if (bank && self >= 0 && self < 8) {
        ch = bank->last[self];
        ma = bank->mean[self];
        rms = bank->rms[self];
        nn = bank->nn[self];
        prev = bank->prev[self];
        dx = bank->dxmean[self];
        above = bank->above[self];
        pos = bank->pos[self];
        sig = bank->signal[self];
    }
    while (pc >= 0 && pc < P->n) {
        const struct np_op *o = &P->op[pc];
        float a, b;
        switch (o->op) {
        case OP_HALT:
            return bit ? 1 : 0;
        case OP_PUSHC:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = o->f;
            pc++;
            break;
        case OP_PUSHV:
            if (sp >= NP_ALGO_STACK || o->i < 0 || o->i >= NP_ALGO_VARS) {
                return 0;
            }
            st[sp++] = var[o->i];
            pc++;
            break;
        case OP_PUSHCH:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = ch;
            pc++;
            break;
        case OP_PUSHMEAN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = ma;
            pc++;
            break;
        case OP_PUSHRMS:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = rms;
            pc++;
            break;
        case OP_PUSHN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = nn;
            pc++;
            break;
        case OP_PUSHCHN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->last, o->i) : 0.f;
            pc++;
            break;
        case OP_PUSHMEANN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->mean, o->i) : 0.f;
            pc++;
            break;
        case OP_PUSHRMSN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->rms, o->i) : 0.f;
            pc++;
            break;
        case OP_PUSHNN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->nn, o->i) : 0.f;
            pc++;
            break;
        case OP_PUSHPREV:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = prev;
            pc++;
            break;
        case OP_PUSHDX:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = dx;
            pc++;
            break;
        case OP_PUSHABOVE:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = above;
            pc++;
            break;
        case OP_PUSHPOS:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = pos;
            pc++;
            break;
        case OP_PUSHSIG:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = sig;
            pc++;
            break;
        case OP_PUSHPREVN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->prev, o->i) : 0.f;
            pc++;
            break;
        case OP_PUSHDXN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->dxmean, o->i) : 0.f;
            pc++;
            break;
        case OP_PUSHABOVEN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->above, o->i) : 0.f;
            pc++;
            break;
        case OP_PUSHPOSN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->pos, o->i) : 0.f;
            pc++;
            break;
        case OP_PUSHSIGN:
            if (sp >= NP_ALGO_STACK) {
                return 0;
            }
            st[sp++] = bank ? bank_at(bank->signal, o->i) : 0.f;
            pc++;
            break;
        case OP_ABS:
            if (sp < 1) {
                return 0;
            }
            st[sp - 1] = fabsf(st[sp - 1]);
            pc++;
            break;
        case OP_NEG:
            if (sp < 1) {
                return 0;
            }
            st[sp - 1] = -st[sp - 1];
            pc++;
            break;
        case OP_NOT:
            if (sp < 1) {
                return 0;
            }
            st[sp - 1] = st[sp - 1] != 0.f ? 0.f : 1.f;
            pc++;
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
        case OP_EQ:
        case OP_NE:
        case OP_AND:
        case OP_OR:
            if (sp < 2) {
                return 0;
            }
            b = st[--sp];
            a = st[--sp];
            if (o->op == OP_ADD) {
                a = a + b;
            } else if (o->op == OP_SUB) {
                a = a - b;
            } else if (o->op == OP_MUL) {
                a = a * b;
            } else if (o->op == OP_DIV) {
                a = (b == 0.f) ? 0.f : a / b;
            } else if (o->op == OP_LT) {
                a = a < b ? 1.f : 0.f;
            } else if (o->op == OP_GT) {
                a = a > b ? 1.f : 0.f;
            } else if (o->op == OP_LE) {
                a = a <= b ? 1.f : 0.f;
            } else if (o->op == OP_GE) {
                a = a >= b ? 1.f : 0.f;
            } else if (o->op == OP_EQ) {
                a = a == b ? 1.f : 0.f;
            } else if (o->op == OP_NE) {
                a = a != b ? 1.f : 0.f;
            } else if (o->op == OP_AND) {
                a = (a != 0.f && b != 0.f) ? 1.f : 0.f;
            } else if (o->op == OP_OR) {
                a = (a != 0.f || b != 0.f) ? 1.f : 0.f;
            } else {
                a = 0.f;
            }
            st[sp++] = a;
            pc++;
            break;
        case OP_JZ:
            if (sp < 1) {
                return 0;
            }
            a = st[--sp];
            pc = (a == 0.f) ? o->i : pc + 1;
            break;
        case OP_JMP:
            pc = o->i;
            break;
        case OP_STORE:
            if (sp < 1 || o->i < 0 || o->i >= NP_ALGO_VARS) {
                return 0;
            }
            var[o->i] = st[--sp];
            pc++;
            break;
        case OP_BIT:
            if (sp < 1) {
                return 0;
            }
            bit = st[--sp] != 0.f ? 1 : 0;
            out->self_bit = bit;
            if (self >= 0 && self < 8) {
                out->bit[self] = (uint8_t)bit;
                out->wrote[self] = 1;
            }
            pc++;
            break;
        case OP_BITN:
            if (sp < 1) {
                return 0;
            }
            bit = st[--sp] != 0.f ? 1 : 0;
            if (o->i >= 0 && o->i < 8) {
                out->bit[o->i] = (uint8_t)bit;
                out->wrote[o->i] = 1;
            }
            pc++;
            break;
        default:
            return 0;
        }
    }
    return out->self_bit ? 1 : 0;
}

#define NP_ALGO_CACHE 16

static struct {
    char src[NP_ALGO_SRC];
    struct np_prog P;
    int ok;
} g_acache[NP_ALGO_CACHE];

static const struct np_prog *prog_cached(const char *src)
{
    int i, empty = -1;
    char err[80];
    if (!src || !src[0]) {
        src = NP_ALGO_SRC_DEFAULT;
    }
    for (i = 0; i < NP_ALGO_CACHE; i++) {
        if (g_acache[i].ok && strcmp(g_acache[i].src, src) == 0) {
            return &g_acache[i].P;
        }
        if (!g_acache[i].ok && empty < 0) {
            empty = i;
        }
    }
    i = empty >= 0 ? empty : 0;
    if (compile_full(src, &g_acache[i].P, err, (int)sizeof(err)) != 0) {
        g_acache[i].ok = 0;
        return NULL;
    }
    snprintf(g_acache[i].src, sizeof(g_acache[i].src), "%s", src);
    g_acache[i].ok = 1;
    return &g_acache[i].P;
}

void np_algo_bank_clear(struct np_algo_bank *b)
{
    if (!b) {
        return;
    }
    memset(b, 0, sizeof(*b));
    b->self = -1;
}

void np_algo_bank_set(struct np_algo_bank *b, int ch, const float *x, int n)
{
    np_algo_bank_set_ex(b, ch, x, n, 0);
}

void np_algo_bank_set_ex(struct np_algo_bank *b, int ch, const float *x, int n,
                         int signal)
{
    int i, above = 0;
    double e = 0, ep = 0, md = 0;
    if (!b || ch < 0 || ch > 7) {
        return;
    }
    b->nn[ch] = (float)n;
    b->signal[ch] = signal ? 1.f : 0.f;
    if (!x || n < 1) {
        b->last[ch] = 0;
        b->mean[ch] = 0;
        b->rms[ch] = 0;
        b->prev[ch] = 0;
        b->dxmean[ch] = 0;
        b->above[ch] = 0;
        b->pos[ch] = 0;
        return;
    }
    b->last[ch] = x[n - 1];
    b->prev[ch] = n >= 2 ? x[n - 2] : x[n - 1];
    b->mean[ch] = mean_abs(x, n);
    for (i = 0; i < n; i++) {
        e += (double)x[i] * (double)x[i];
        if (x[i] > 0.f) {
            above++;
            ep += (double)x[i] * (double)x[i];
        }
        if (i > 0) {
            md += fabs((double)x[i] - (double)x[i - 1]);
        }
    }
    b->rms[ch] = (float)sqrt(e / (double)n);
    b->dxmean[ch] = n > 1 ? (float)(md / (double)(n - 1)) : 0.f;
    b->above[ch] = (float)above / (float)n;
    b->pos[ch] = e > 1e-12 ? (float)(ep / e) : 0.f;
}

int np_algo_custom_out(const char *src, const struct np_algo_bank *b,
                       struct np_algo_out *o)
{
    const struct np_prog *P = prog_cached(src);
    if (!o) {
        return 0;
    }
    memset(o, 0, sizeof(*o));
    if (!P) {
        return 0;
    }
    return eval_prog(P, b, o);
}

int np_algo_custom_bank(const char *src, const struct np_algo_bank *b)
{
    struct np_algo_out o;
    int self;
    np_algo_custom_out(src, b, &o);
    self = b ? b->self : -1;
    if (self >= 0 && self < 8 && o.wrote[self]) {
        return o.bit[self] ? 1 : 0;
    }
    return o.self_bit ? 1 : 0;
}

int np_algo_custom(const char *src, const float *x, int n)
{
    struct np_algo_bank b;
    np_algo_bank_clear(&b);
    np_algo_bank_set(&b, 0, x, n);
    b.self = 0;
    return np_algo_custom_bank(src, &b);
}
