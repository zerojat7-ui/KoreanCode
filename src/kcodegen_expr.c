/*
 * kcodegen_expr.c  —  C 코드 생성기 표현식(Expression) 생성
 * version : v13.0.0
 *
 * ★ 이 파일은 kcodegen.c(unity build)에 #include 되어 컴파일됩니다.
 *    단독 컴파일 대상이 아닙니다.
 *
 * 담당 역할:
 *   - 전방 선언 — 서로 참조하는 static 함수들의 순환 의존 해결
 *   - op_to_c()         : 연산자 토큰 → C 연산자 문자열 변환
 *   - gen_builtin_call(): 한글 내장 함수 이름 → C 함수 호출 코드 생성
 *       포함 범위: 출력/출력없이/입력/길이/범위 / 수학(절댓값·제곱·삼각) /
 *                  AI(MSE·교차엔트로피·소프트맥스·수열) / 글자 21종 /
 *                  통계 13종 / AI 활성함수 3종 / 호감도 / 파일 17종
 *   - gen_expr()        : 표현식 노드 C 코드 생성 디스패처
 *       포함 범위: 리터럴(정수/실수/논리/문자/글자/없음) / 식별자 /
 *                  이진·단항 연산 / 함수 호출 / 배열 인덱스 / 멤버 접근 /
 *                  람다(정적 함수 변환) / 배열·딕셔너리 리터럴
 *
 * 분리 이전: kcodegen.c 내 lines 248~1103
 *
 * MIT License
 * zerojat7
 */

/* ================================================================
 *  전방 선언
 * ================================================================ */
static void gen_expr(Codegen *cg, Node *n);
static void gen_stmt(Codegen *cg, Node *n);
static void gen_block(Codegen *cg, Node *n);
static void gen_contract(Codegen *cg, Node *n);
static void gen_checkpoint(Codegen *cg, Node *n);
/* ★ v5.1.0 — 법위반 래퍼 시스템 전방선언 */
static int  is_postcond_fn(Codegen *cg, const char *fn_name);
static void emit_postcond_check(Codegen *cg, Node *n);
static void emit_all_postconds(Codegen *cg, Node *node, const char *fn_name);
static void gen_postcond_wrapper(Codegen *cg, Node *fn_node, Node *program);

/* ================================================================
 *  연산자 토큰 → C 연산자 문자열
 * ================================================================ */
static const char *op_to_c(TokenType op) {
    switch (op) {
        case TOK_PLUS:       return "+";
        case TOK_MINUS:      return "-";
        case TOK_STAR:       return "*";
        case TOK_SLASH:      return "/";
        case TOK_PERCENT:    return "%";
        case TOK_EQEQ:       return "==";
        case TOK_BANGEQ:     return "!=";
        case TOK_LT:         return "<";
        case TOK_GT:         return ">";
        case TOK_LTEQ:       return "<=";
        case TOK_GTEQ:       return ">=";
        case TOK_KW_AND:     return "&&";   /* 그리고 */
        case TOK_KW_OR:      return "||";   /* 또는   */
        case TOK_KW_NOT:     return "!";    /* 아니다 */
        case TOK_AMP:        return "&";
        case TOK_PIPE:       return "|";
        case TOK_CARET:      return "^";
        case TOK_TILDE:      return "~";
        case TOK_LTLT:       return "<<";
        case TOK_GTGT:       return ">>";
        case TOK_EQ:         return "=";
        case TOK_PLUSEQ:     return "+=";
        case TOK_MINUSEQ:    return "-=";
        case TOK_STAREQ:     return "*=";
        case TOK_SLASHEQ:    return "/=";
        case TOK_PERCENTEQ:  return "%=";
        default:             return "??";
    }
}

/* ================================================================
 *  내장 함수 이름 → C 호출로 변환
 * ================================================================ */
static int gen_builtin_call(Codegen *cg, Node *n) {
    /* n = NODE_CALL, n->children[0] = NODE_IDENT(함수명) */
    if (n->child_count < 1) return 0;
    Node *fn = n->children[0];
    if (!fn || fn->type != NODE_IDENT || !fn->sval) return 0;

    const char *name = fn->sval;

    /* 출력 → printf + "\n" */
    if (strcmp(name, "\xEC\xB6\x9C\xEB\xa0\xa5") == 0) { /* 출력 */
        emit(cg, "kc_print(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        return 1;
    }
    /* 출력없이 → printf (줄바꿈 없음) */
    if (strcmp(name, "\xEC\xB6\x9C\xEB\xa0\xa5\xEC\x97\x86\xEC\x9D\xB4") == 0) { /* 출력없이 */
        emit(cg, "kc_print_no_newline(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        return 1;
    }
    /* 입력 → fgets */
    if (strcmp(name, "\xEC\x9E\x85\xEB\xa0\xa5") == 0) { /* 입력 */
        emit(cg, "kc_input()");
        return 1;
    }
    /* 길이 → kc_len */
    if (strcmp(name, "\xEA\xB8\xB8\xEC\x9D\xB4") == 0) { /* 길이 */
        emit(cg, "kc_len(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 범위 → kc_range */
    if (strcmp(name, "\xEB\xB2\x94\xEC\x9C\x84") == 0) { /* 범위 */
        emit(cg, "kc_range(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        return 1;
    }
    /* 수학 내장 */
    if (strcmp(name, "\xEC\xA0\x88\xEB\x8C\x93\xEA\xB0\x92") == 0) { /* 절댓값 */
        emit(cg, "kc_abs("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEC\xB5\x9C\xEB\x8C\x80") == 0) { /* 최대 */
        emit(cg, "kc_max(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ", KC_SENTINEL)");
        return 1;
    }
    if (strcmp(name, "\xEC\xB5\x9C\xEC\x86\x8C") == 0) { /* 최소 */
        emit(cg, "kc_min(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ", KC_SENTINEL)");
        return 1;
    }
    if (strcmp(name, "\xEC\xA0\x9C\xEA\xB3\xB1\xEA\xB7\xBC") == 0) { /* 제곱근 */
        emit(cg, "sqrt("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEC\xA0\x9C\xEA\xB3\xB1") == 0) { /* 제곱 */
        emit(cg, "pow(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        if (n->child_count > 2) gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── AI / 수학 내장 함수 (v3.7.1) ──────────────────────── */

    /* 평균제곱오차(예측, 실제) → kc_mse(arr1, arr2) */
    if (strcmp(name, "\xED\x8F\x89\xEA\xB7\xA0\xEC\xA0\x9C\xEA\xB3\xB1\xEC\x98\xA4\xEC\xB0\xA8") == 0) { /* 평균제곱오차 */
        emit(cg, "kc_mse(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 교차엔트로피(P_star, P) → kc_cross_entropy(arr1, arr2) */
    if (strcmp(name, "\xEA\xB5\x90\xEC\xB0\xA8\xEC\x97\x94\xED\x8A\xB8\xEB\xA1\x9C\xED\x94\xBC") == 0) { /* 교차엔트로피 */
        emit(cg, "kc_cross_entropy(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 소프트맥스(배열) → kc_softmax(arr) */
    if (strcmp(name, "\xEC\x86\x8C\xED\x94\x84\xED\x8A\xB8\xEB\xA7\xA5\xEC\x8A\xA4") == 0) { /* 소프트맥스 */
        emit(cg, "kc_softmax(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 위치인코딩(위치, 차원수) → kc_positional_encoding(pos, d) */
    if (strcmp(name, "\xEC\x9C\x84\xEC\xB9\x98\xEC\x9D\xB8\xEC\xBD\x94\xEB\x94\xA9") == 0) { /* 위치인코딩 */
        emit(cg, "kc_positional_encoding(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 등비수열합(a, r) → kc_geom_series(a, r) */
    if (strcmp(name, "\xEB\x93\xB1\xEB\xB9\x84\xEC\x88\x98\xEC\x97\xB4\xED\x95\xA9") == 0) { /* 등비수열합 */
        emit(cg, "kc_geom_series(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 등차수열합(a, d, n) → kc_arith_series(a, d, n) */
    if (strcmp(name, "\xEB\x93\xB1\xEC\xB0\xA8\xEC\x88\x98\xEC\x97\xB4\xED\x95\xA9") == 0) { /* 등차수열합 */
        emit(cg, "kc_arith_series(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }
    /* 점화식값(a1, r, n) → kc_recur_geom(a1, r, n) */
    if (strcmp(name, "\xEC\xA0\x90\xED\x99\x94\xEC\x8B\x9D\xEA\xB0\x92") == 0) { /* 점화식값 */
        emit(cg, "kc_recur_geom(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }
    /* 형변환 */
    if (strcmp(name, "\xEC\xa0\x95\xEC\x88\x98") == 0) { /* 정수 */
        emit(cg, "(int64_t)("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEC\x8B\xA4\xEC\x88\x98") == 0) { /* 실수 */
        emit(cg, "(double)("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEB\xAC\xB8\xEC\x9E\x90") == 0) { /* 글자 */
        emit(cg, "kc_to_string("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }

    /* ── 수학 기초 (v3.7.1) ─────────────────────────────────── */
    if (strcmp(name, "\xEC\x98\xAC\xEB\xA6\xBC") == 0) { /* 올림 */
        emit(cg, "(int64_t)ceil((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEB\x82\xB4\xEB\xA6\xBC") == 0) { /* 내림 */
        emit(cg, "(int64_t)floor((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEB\xB0\x98\xEC\x98\xAC\xEB\xA6\xBC") == 0) { /* 반올림(값, 자릿수=0) */
        if (n->child_count >= 3) {
            /* 자릿수 있음: round(v * 10^d) / 10^d */
            emit(cg, "kc_round_digits(");
            gen_expr(cg, n->children[1]); emit(cg, ", ");
            gen_expr(cg, n->children[2]);
            emit(cg, ")");
        } else {
            emit(cg, "(int64_t)round((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        }
        return 1;
    }
    if (strcmp(name, "\xEC\x82\xAC\xEC\x9D\xB8") == 0) { /* 사인 */
        emit(cg, "sin((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEC\xBD\x94\xEC\x82\xAC\xEC\x9D\xB8") == 0) { /* 코사인 */
        emit(cg, "cos((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xED\x83\x84\xEC\xA0\xA0\xED\x8A\xB8") == 0) { /* 탄젠트 */
        emit(cg, "tan((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEC\x9E\x90\xEC\x97\xB0\xEB\xA1\x9C\xEA\xB7\xB8") == 0) { /* 자연로그 */
        emit(cg, "log((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEB\xA1\x9C\xEA\xB7\xB8") == 0) { /* 로그(밑, 값) */
        emit(cg, "kc_log_base(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEC\xA7\x80\xEC\x88\x98") == 0) { /* 지수 */
        emit(cg, "exp((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }

    /* ── 글자 함수 21종 (v9.6.0) ──────────────────────────────
     *  모든 함수는 kc_runtime.h 에 선언된 kc_str_* 헬퍼를 호출.
     *  인수 수:
     *    1인수 : args[1]
     *    2인수 : args[1], args[2]
     *    3인수 : args[1], args[2], args[3]
     *  가변 인수(포맷): args[1..] 전체 전달
     * ============================================================ */

    /* ── 1단계: 기본 글자 조작 ── */

    /* 자르기(글자, 시작, 끝) → kc_str_sub(s, start, end) */
    if (strcmp(name, "\xEC\x9E\x90\xEB\xA5\xB4\xEA\xB8\xB0") == 0) { /* 자르기 */
        emit(cg, "kc_str_sub(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }
    /* 분할(글자, 구분자) → kc_str_split(s, delim) */
    if (strcmp(name, "\xEB\xB6\x84\xED\x95\xA0") == 0) { /* 분할 */
        emit(cg, "kc_str_split(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 합치기(배열, 구분자) → kc_str_join(arr, delim) */
    if (strcmp(name, "\xED\x95\xA9\xEC\xB9\x98\xEA\xB8\xB0") == 0) { /* 합치기 */
        emit(cg, "kc_str_join(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 반복글자(글자, 횟수) → kc_str_repeat(s, n) */
    if (strcmp(name, "\xEB\xB0\x98\xEB\xB3\xB5\xEA\xB8\x80\xEC\x9E\x90") == 0) { /* 반복글자 */
        emit(cg, "kc_str_repeat(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 역순(글자) → kc_str_reverse(s) */
    if (strcmp(name, "\xEC\x97\xAD\xEC\x88\x9C") == 0) { /* 역순 */
        emit(cg, "kc_str_reverse(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 2단계: 문자열 검색/비교 ── */

    /* 포함(글자, 찾을글자) → kc_str_contains(s, sub) */
    if (strcmp(name, "\xED\x8F\xAC\xED\x95\xA8") == 0) { /* 포함 */
        emit(cg, "kc_str_contains(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 위치(글자, 찾을글자) → kc_str_indexof(s, sub) */
    if (strcmp(name, "\xEC\x9C\x84\xEC\xB9\x98") == 0) { /* 위치 */
        emit(cg, "kc_str_indexof(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 시작(글자, 접두어) → kc_str_startswith(s, prefix) */
    if (strcmp(name, "\xEC\x8B\x9C\xEC\x9E\x91") == 0) { /* 시작 */
        emit(cg, "kc_str_startswith(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 끝확인(글자, 접미어) → kc_str_endswith(s, suffix) */
    if (strcmp(name, "\xEB\x81\x9D\xED\x99\x95\xEC\x9D\xB8") == 0) { /* 끝확인 */
        emit(cg, "kc_str_endswith(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 비교(글자1, 글자2) → kc_str_compare(s1, s2) */
    if (strcmp(name, "\xEB\xB9\x84\xEA\xB5\x90") == 0) { /* 비교 */
        emit(cg, "kc_str_compare(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── 3단계: 글자 변환 ── */

    /* 대문자(글자) → kc_str_upper(s) */
    if (strcmp(name, "\xEB\x8C\x80\xEB\xAC\xB8\xEC\x9E\x90") == 0) { /* 대문자 */
        emit(cg, "kc_str_upper(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 소문자(글자) → kc_str_lower(s) */
    if (strcmp(name, "\xEC\x86\x8C\xEB\xAC\xB8\xEC\x9E\x90") == 0) { /* 소문자 */
        emit(cg, "kc_str_lower(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 제목식(글자) → kc_str_title(s) */
    if (strcmp(name, "\xEC\xA0\x9C\xEB\xAA\xA9\xEC\x8B\x9D") == 0) { /* 제목식 */
        emit(cg, "kc_str_title(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 대체(글자, 찾을글자, 바꿀글자) → kc_str_replace(s, from, to) */
    if (strcmp(name, "\xEB\x8C\x80\xEC\xB2\xB4") == 0) { /* 대체 */
        emit(cg, "kc_str_replace(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }
    /* 한번대체(글자, 찾을글자, 바꿀글자) → kc_str_replace_once(s, from, to) */
    if (strcmp(name, "\xED\x95\x9C\xEB\xB2\x88\xEB\x8C\x80\xEC\xB2\xB4") == 0) { /* 한번대체 */
        emit(cg, "kc_str_replace_once(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }

    /* ── 4단계: 공백 및 정제 ── */

    /* 앞공백제거(글자) → kc_str_ltrim(s) */
    if (strcmp(name, "\xEC\x95\x9E\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0") == 0) { /* 앞공백제거 */
        emit(cg, "kc_str_ltrim(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 뒤공백제거(글자) → kc_str_rtrim(s) */
    if (strcmp(name, "\xEB\x92\xA4\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0") == 0) { /* 뒤공백제거 */
        emit(cg, "kc_str_rtrim(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 공백제거(글자) → kc_str_trim(s) */
    if (strcmp(name, "\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0") == 0) { /* 공백제거 */
        emit(cg, "kc_str_trim(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 5단계: 고급 기능 ── */

    /* 반복확인(글자, 패턴) → kc_str_regex(s, pattern) — 정규식 */
    if (strcmp(name, "\xEB\xB0\x98\xEB\xB3\xB5\xED\x99\x95\xEC\x9D\xB8") == 0) { /* 반복확인 */
        emit(cg, "kc_str_regex(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 분석(글자) → kc_str_parse(s) — 숫자/논리 등 자동 변환 */
    if (strcmp(name, "\xEB\xB6\x84\xEC\x84\x9D") == 0) { /* 분석 */
        emit(cg, "kc_str_parse(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 포맷(형식, 인수...) → kc_str_format(fmt, ...) */
    if (strcmp(name, "\xED\x8F\xAC\xEB\xA7\xB7") == 0) { /* 포맷 */
        emit(cg, "kc_str_format(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        return 1;
    }

    /* ── 글자(문자열) 형변환 (v9.6.0 누락) ── */
    /* 글자(값) → kc_to_string(val) */
    if (strcmp(name, "\xEA\xB8\x80\xEC\x9E\x90") == 0) { /* 글자 */
        emit(cg, "kc_to_string(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 배열 조작 ── */
    /* 추가(배열, 값) → kc_array_push(arr, val) */
    if (strcmp(name, "\xEC\xB6\x94\xEA\xB0\x80") == 0) { /* 추가 */
        emit(cg, "kc_array_push(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── 통계 함수 13종 (v3.8.0) ── */
    /* 합계(배열) → kc_stat_sum(arr) */
    if (strcmp(name, "\xED\x95\xA9\xEA\xB3\x84") == 0) { /* 합계 */
        emit(cg, "kc_stat_sum(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 평균(배열) → kc_stat_mean(arr) */
    if (strcmp(name, "\xED\x8F\x89\xEA\xB7\xA0") == 0) { /* 평균 */
        emit(cg, "kc_stat_mean(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 분산(배열) → kc_stat_variance(arr) */
    if (strcmp(name, "\xEB\xB6\x84\xEC\x82\xB0") == 0) { /* 분산 */
        emit(cg, "kc_stat_variance(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 표준편차(배열) → kc_stat_stddev(arr) */
    if (strcmp(name, "\xED\x91\x9C\xEC\xA4\x80\xED\x8E\xB8\xEC\xB0\xA8") == 0) { /* 표준편차 */
        emit(cg, "kc_stat_stddev(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 중앙값(배열) → kc_stat_median(arr) */
    if (strcmp(name, "\xEC\xA4\x91\xEC\x95\x99\xEA\xB0\x92") == 0) { /* 중앙값 */
        emit(cg, "kc_stat_median(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 최빈값(배열) → kc_stat_mode(arr) */
    if (strcmp(name, "\xEC\xB5\x9C\xEB\xB9\x88\xEA\xB0\x92") == 0) { /* 최빈값 */
        emit(cg, "kc_stat_mode(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 누적합(배열) → kc_stat_cumsum(arr) */
    if (strcmp(name, "\xEB\x88\x84\xEC\xA0\x81\xED\x95\xA9") == 0) { /* 누적합 */
        emit(cg, "kc_stat_cumsum(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 공분산(배열1, 배열2) → kc_stat_covariance(a, b) */
    if (strcmp(name, "\xEA\xB3\xB5\xEB\xB6\x84\xEC\x82\xB0") == 0) { /* 공분산 */
        emit(cg, "kc_stat_covariance(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 상관계수(배열1, 배열2) → kc_stat_correlation(a, b) */
    if (strcmp(name, "\xEC\x83\x81\xEA\xB4\x80\xEA\xB3\x84\xEC\x88\x98") == 0) { /* 상관계수 */
        emit(cg, "kc_stat_correlation(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 정규화(배열) → kc_stat_normalize(arr) */
    if (strcmp(name, "\xEC\xA0\x95\xEA\xB7\x9C\xED\x99\x94") == 0) { /* 정규화 */
        emit(cg, "kc_stat_normalize(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 표준화(배열) → kc_stat_standardize(arr) */
    if (strcmp(name, "\xED\x91\x9C\xEC\xA4\x80\xED\x99\x94") == 0) { /* 표준화 */
        emit(cg, "kc_stat_standardize(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 배열정렬(배열) → kc_arr_sort(arr) */
    if (strcmp(name, "\xEB\xB0\xB0\xEC\x97\xB4\xEC\xA0\x95\xEB\xA0\xAC") == 0) { /* 배열정렬 */
        emit(cg, "kc_arr_sort(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 배열뒤집기(배열) → kc_arr_reverse(arr) */
    if (strcmp(name, "\xEB\xB0\xB0\xEC\x97\xB4\xEB\x92\xA4\xEC\xA7\x91\xEA\xB8\xB0") == 0) { /* 배열뒤집기 */
        emit(cg, "kc_arr_reverse(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── AI 활성함수 3종 (v3.8.0) ── */
    /* 시그모이드(값) → kc_sigmoid(x) */
    if (strcmp(name, "\xEC\x8B\x9C\xEA\xB7\xB8\xEB\xAA\xA8\xEC\x9D\xB4\xEB\x93\x9C") == 0) { /* 시그모이드 */
        emit(cg, "kc_sigmoid(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 렐루(값) → kc_relu(x) */
    if (strcmp(name, "\xEB\xA0\xB0\xEB\xA3\xA8") == 0) { /* 렐루 */
        emit(cg, "kc_relu(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 쌍곡탄젠트(값) → kc_tanh_fn(x) */
    if (strcmp(name, "\xEC\x8C\x8D\xEA\xB3\xA1\xED\x83\x84\xEC\xA0\xA0\xED\x8A\xB8") == 0) { /* 쌍곡탄젠트 */
        emit(cg, "kc_tanh_fn(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 관계 심리 함수 (v3.8.0) ── */
    /* 호감도(배열1, 배열2) → kc_attraction(a, b) */
    if (strcmp(name, "\xED\x98\xB8\xEA\xB0\x90\xEB\x8F\x84") == 0) { /* 호감도 */
        emit(cg, "kc_attraction(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── 파일 내장 함수 17종 (v5.0.0) ── */
    /* 파일열기(경로, 모드) → kc_file_open(path, mode) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x97\xB4\xEA\xB8\xB0") == 0) { /* 파일열기 */
        emit(cg, "kc_file_open(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일닫기(핸들) → kc_file_close(f) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEB\x8B\xAB\xEA\xB8\xB0") == 0) { /* 파일닫기 */
        emit(cg, "kc_file_close(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일읽기(핸들) → kc_file_read(f) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xBD\xEA\xB8\xB0") == 0) { /* 파일읽기 */
        emit(cg, "kc_file_read(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일줄읽기(핸들) → kc_file_readline(f) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA4\x84\xEC\x9D\xBD\xEA\xB8\xB0") == 0) { /* 파일줄읽기 */
        emit(cg, "kc_file_readline(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일쓰기(핸들, 내용) → kc_file_write(f, s) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x93\xB0\xEA\xB8\xB0") == 0) { /* 파일쓰기 */
        emit(cg, "kc_file_write(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일줄쓰기(핸들, 내용) → kc_file_writeline(f, s) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA4\x84\xEC\x93\xB0\xEA\xB8\xB0") == 0) { /* 파일줄쓰기 */
        emit(cg, "kc_file_writeline(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일있음(경로) → kc_file_exists(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9E\x88\xEC\x9D\x8C") == 0) { /* 파일있음 */
        emit(cg, "kc_file_exists(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일크기(경로) → kc_file_size(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xED\x81\xAC\xEA\xB8\xB0") == 0) { /* 파일크기 */
        emit(cg, "kc_file_size(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일목록(경로) → kc_file_list(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEB\xAA\xA9\xEB\xA1\x9D") == 0) { /* 파일목록 */
        emit(cg, "kc_file_list(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일이름(경로) → kc_file_name(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4\xEB\xA6\x84") == 0) { /* 파일이름 */
        emit(cg, "kc_file_name(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일확장자(경로) → kc_file_ext(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xED\x99\x95\xEC\x9E\xA5\xEC\x9E\x90") == 0) { /* 파일확장자 */
        emit(cg, "kc_file_ext(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 폴더만들기(경로) → kc_dir_make(path) */
    if (strcmp(name, "\xED\x8F\xB4\xEB\x8D\x94\xEB\xA7\x8C\xEB\x93\xA4\xEA\xB8\xB0") == 0) { /* 폴더만들기 */
        emit(cg, "kc_dir_make(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일지우기(경로) → kc_file_delete(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA7\x80\xEC\x9A\xB0\xEA\xB8\xB0") == 0) { /* 파일지우기 */
        emit(cg, "kc_file_delete(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일복사(원본, 대상) → kc_file_copy(src, dst) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEB\xB3\xB5\xEC\x82\xAC") == 0) { /* 파일복사 */
        emit(cg, "kc_file_copy(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일이동(원본, 대상) → kc_file_move(src, dst) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4\xEB\x8F\x99") == 0) { /* 파일이동 */
        emit(cg, "kc_file_move(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일전체읽기(경로) → kc_file_readall(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA0\x84\xEC\xB2\xB4\xEC\x9D\xBD\xEA\xB8\xB0") == 0) { /* 파일전체읽기 */
        emit(cg, "kc_file_readall(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일전체쓰기(경로, 내용) → kc_file_writeall(path, s) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA0\x84\xEC\xB2\xB4\xEC\x93\xB0\xEA\xB8\xB0") == 0) { /* 파일전체쓰기 */
        emit(cg, "kc_file_writeall(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── 텐서 내장 함수 12종 (v12.0.0) ── */
    /* 텐서생성(데이터배열, 형태배열) → kc_tensor_from_flat_args(...) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xEC\x83\x9D\xEC\x84\xB1") == 0) { /* 텐서생성 */
        emit(cg, "kc_tensor_builtin_create(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        if (n->child_count > 2) { emit(cg, ", "); gen_expr(cg, n->children[2]); }
        emit(cg, ")");
        return 1;
    }
    /* 텐서형태(t) → kc_tensor_shape_str(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xED\x98\x95\xED\x83\x9C") == 0) { /* 텐서형태 */
        emit(cg, "kc_tensor_shape_str(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서크기(t) → kc_tensor_numel(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xED\x81\xAC\xEA\xB8\xB0") == 0) { /* 텐서크기 */
        emit(cg, "kc_tensor_numel(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서차원(t) → kc_tensor_ndim(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xEC\xB0\xA8\xEC\x9B\x90") == 0) { /* 텐서차원 */
        emit(cg, "kc_tensor_ndim(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서합(t) → kc_tensor_sum(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xED\x95\xA9") == 0) { /* 텐서합 */
        emit(cg, "kc_tensor_sum(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서평균(t) → kc_tensor_mean(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xED\x8F\x89\xEA\xB7\xA0") == 0) { /* 텐서평균 */
        emit(cg, "kc_tensor_mean(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서최대(t) → kc_tensor_max(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xEC\xB5\x9C\xEB\x8C\x80") == 0) { /* 텐서최대 */
        emit(cg, "kc_tensor_max(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서최소(t) → kc_tensor_min(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xEC\xB5\x9C\xEC\x86\x8C") == 0) { /* 텐서최소 */
        emit(cg, "kc_tensor_min(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서행렬곱(a, b) → kc_tensor_matmul(a, b) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1") == 0) { /* 텐서행렬곱 */
        emit(cg, "kc_tensor_matmul(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        if (n->child_count > 2) { emit(cg, ", "); gen_expr(cg, n->children[2]); }
        emit(cg, ")");
        return 1;
    }
    /* 텐서전치(t) → kc_tensor_transpose(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xEC\xA0\x84\xEC\xB9\x98") == 0) { /* 텐서전치 */
        emit(cg, "kc_tensor_transpose(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서변형(t, 형태배열) → kc_tensor_builtin_reshape(t, shape_arr) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xEB\xB3\x80\xED\x98\x95") == 0) { /* 텐서변형 */
        emit(cg, "kc_tensor_builtin_reshape(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        if (n->child_count > 2) { emit(cg, ", "); gen_expr(cg, n->children[2]); }
        emit(cg, ")");
        return 1;
    }
    /* 텐서평탄화(t) → kc_tensor_flatten(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xED\x8F\x89\xED\x83\x84\xED\x99\x94") == 0) { /* 텐서평탄화 */
        emit(cg, "kc_tensor_flatten(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 텐서복사(t) → kc_tensor_copy(t) */
    if (strcmp(name, "\xED\x85\x90\xEC\x84\x9C\xEB\xB3\xB5\xEC\x82\xAC") == 0) { /* 텐서복사 */
        emit(cg, "kc_tensor_copy(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 자동미분 함수 2종 (v13.0.0) ── */
    /* 역전파(t) → kc_autograd_backward(t) */
    if (strcmp(name, "\xEC\x97\xAD\xEC\xA0\x84\xED\x8C\x8C") == 0) { /* 역전파 */
        emit(cg, "kc_autograd_backward(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 기울기초기화(t) → kc_autograd_zero_grad(t) */
    if (strcmp(name, "\xEA\xB8\xB0\xEC\x9A\xB8\xEA\xB8\xB0\xEC\xB4\x88\xEA\xB8\xB0\xED\x99\x94") == 0) { /* 기울기초기화 */
        emit(cg, "kc_autograd_zero_grad(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    return 0; /* 내장 함수 아님 */
}

/* ================================================================
 *  표현식 생성
 * ================================================================ */
static void gen_expr(Codegen *cg, Node *n) {
    if (!n) { emit(cg, "0"); return; }

    sourcemap_add(cg, n->line, n->col);

    switch (n->type) {

    case NODE_INT_LIT:
        emit(cg, "%lldLL", (long long)n->val.ival);
        break;

    case NODE_FLOAT_LIT:
        emit(cg, "%g", n->val.fval);
        break;

    case NODE_STRING_LIT:
        escape_string(cg, n->sval ? n->sval : "");
        break;

    case NODE_CHAR_LIT:
        emit(cg, "0x%X", (unsigned)n->val.ival);
        break;

    case NODE_BOOL_LIT:
        emit(cg, "%s", n->val.bval ? "1" : "0");
        break;

    case NODE_NULL_LIT:
        emit(cg, "NULL");
        break;

    case NODE_IDENT:
        /* 식별자는 그대로 출력 (한글 → C는 변수명 맹글링 필요없음,
           kc_mangle로 ASCII 안전 이름으로 변환) */
        emit(cg, "kc_%s", n->sval ? n->sval : "_unknown");
        break;

    case NODE_BINARY:
        /* 거듭제곱은 pow() 사용 */
        if (n->op == TOK_STARSTAR) {
            emit(cg, "pow((double)(");
            gen_expr(cg, n->children[0]);
            emit(cg, "), (double)(");
            gen_expr(cg, n->children[1]);
            emit(cg, "))");
        } else {
            emit(cg, "(");
            gen_expr(cg, n->children[0]);
            emit(cg, " %s ", op_to_c(n->op));
            gen_expr(cg, n->children[1]);
            emit(cg, ")");
        }
        break;

    case NODE_UNARY:
        emit(cg, "(%s", op_to_c(n->op));
        gen_expr(cg, n->children[0]);
        emit(cg, ")");
        break;

    case NODE_ASSIGN:
        gen_expr(cg, n->children[0]);
        emit(cg, " %s ", op_to_c(n->op));
        gen_expr(cg, n->children[1]);
        break;

    case NODE_CALL: {
        /* 내장 함수 먼저 확인 */
        if (gen_builtin_call(cg, n)) break;
        /* 일반 함수 호출 */
        gen_expr(cg, n->children[0]);
        emit(cg, "(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        break;
    }

    case NODE_INDEX:
        emit(cg, "kc_arr_get(");
        gen_expr(cg, n->children[0]);
        emit(cg, ", ");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        break;

    case NODE_MEMBER: {
        /* 배열.길이 → kc_arr_len(arr) */
        const char *mname = n->sval ? n->sval : "";
        /* 길이 */
        if (strcmp(mname, "\xEA\xB8\xB8\xEC\x9D\xB4") == 0) {
            emit(cg, "kc_arr_len(");
            gen_expr(cg, n->children[0]);
            emit(cg, ")");
        } else {
            gen_expr(cg, n->children[0]);
            emit(cg, ".%s", mname);
        }
        break;
    }

    case NODE_ARRAY_LIT: {
        emit(cg, "kc_arr_literal(%d", n->child_count);
        for (int i = 0; i < n->child_count; i++) {
            emit(cg, ", ");
            /* 배열 요소를 kc_value_t로 래핑 */
            emit(cg, "KC_VAL(");
            gen_expr(cg, n->children[i]);
            emit(cg, ")");
        }
        emit(cg, ")");
        break;
    }

    case NODE_LAMBDA: {
        /* 람다 → 미리 생성된 정적 함수 이름 참조 */
        /* (람다는 1패스에서 전방 선언됨) */
        emit(cg, "kc_lambda_%d", cg->tmp_counter++);
        break;
    }

    default:
        emit(cg, "/* [미지원 표현식: %d] */0", n->type);
        break;
    }
}
