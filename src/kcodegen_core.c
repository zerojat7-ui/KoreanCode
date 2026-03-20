/*
 * kcodegen_core.c  —  C 코드 생성기 핵심 유틸리티
 * version : v13.0.0
 *
 * ★ 이 파일은 kcodegen.c(unity build)에 #include 되어 컴파일됩니다.
 *    단독 컴파일 대상이 아닙니다.
 *    includes / defines 는 kcodegen.c(orchestrator)에서 상속됩니다.
 *
 * 담당 역할:
 *   - 동적 문자열 버퍼 관리 (buf_ensure / emit / emit_indent / emitln)
 *   - 소스맵 등록 (sourcemap_add) — .han 라인 ↔ 생성된 .c 라인 매핑
 *   - 오류 등록 (cg_error) — CodegenResult.errors 에 누적
 *   - C 자료형 이름 반환 (c_type) — 한글 토큰 → C 타입 문자열
 *   - 문자열 C 이스케이프 (escape_string)
 *
 * 분리 이전: kcodegen.c 내 lines 127~246
 *
 * MIT License
 * zerojat7
 */

static void buf_ensure(Codegen *cg, size_t extra) {
    while (cg->buf_len + extra + 1 >= cg->buf_cap) {
        cg->buf_cap *= 2;
        cg->buf = (char*)realloc(cg->buf, cg->buf_cap);
    }
}

/* 형식 문자열을 버퍼에 추가 */
static void emit(Codegen *cg, const char *fmt, ...) {
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0) return;

    buf_ensure(cg, (size_t)n);
    memcpy(cg->buf + cg->buf_len, tmp, (size_t)n);
    cg->buf_len += (size_t)n;
    cg->buf[cg->buf_len] = '\0';

    /* 줄바꿈 수 만큼 C 라인 카운터 증가 */
    for (int i = 0; i < n; i++) {
        if (tmp[i] == '\n') cg->c_line++;
    }
}

/* 들여쓰기 출력 */
static void emit_indent(Codegen *cg) {
    for (int i = 0; i < cg->indent * CG_INDENT_SIZE; i++) {
        buf_ensure(cg, 1);
        cg->buf[cg->buf_len++] = ' ';
    }
    cg->buf[cg->buf_len] = '\0';
}

/* 줄 시작 (들여쓰기 + 내용) */
static void emitln(Codegen *cg, const char *fmt, ...) {
    emit_indent(cg);
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        buf_ensure(cg, (size_t)n + 1);
        memcpy(cg->buf + cg->buf_len, tmp, (size_t)n);
        cg->buf_len += (size_t)n;
    }
    buf_ensure(cg, 1);
    cg->buf[cg->buf_len++] = '\n';
    cg->buf[cg->buf_len]   = '\0';
    cg->c_line++;
}

/* ================================================================
 *  소스맵 등록
 * ================================================================ */
static void sourcemap_add(Codegen *cg, int han_line, int han_col) {
    CodegenResult *r = cg->result;
    if (r->sourcemap_count >= CODEGEN_MAX_SOURCEMAP) return;
    SourceMapEntry *e = &r->sourcemap[r->sourcemap_count++];
    e->han_line = han_line;
    e->han_col  = han_col;
    e->c_line   = cg->c_line;
}

/* ================================================================
 *  오류 등록
 * ================================================================ */
static void cg_error(Codegen *cg, int line, int col, const char *fmt, ...) {
    CodegenResult *r = cg->result;
    if (r->error_count >= CODEGEN_MAX_ERRORS) return;
    CodegenError *e = &r->errors[r->error_count++];
    e->line = line;
    e->col  = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
    va_end(ap);
    r->had_error  = 1;
    cg->had_error = 1;
}

/* ================================================================
 *  C 자료형 이름 반환
 * ================================================================ */
static const char *c_type(TokenType dtype) {
    switch (dtype) {
        case TOK_KW_JEONGSU:  return "int64_t";
        case TOK_KW_SILSU:    return "double";
        case TOK_KW_NOLI:     return "int";         /* bool → int */
        case TOK_KW_GEULJA:   return "uint32_t";    /* 문자 (UTF-32) */
        case TOK_KW_MUNJA:    return "char*";
        case TOK_KW_EOPSEUM:  return "void*";
        case TOK_KW_BAELYEOL: return "kc_array_t*";
        default:              return "kc_value_t";  /* 범용 */
    }
}

/* ================================================================
 *  문자열 C 이스케이프
 * ================================================================ */
static void escape_string(Codegen *cg, const char *s) {
    emit(cg, "\"");
    for (; *s; s++) {
        switch (*s) {
            case '"':  emit(cg, "\\\""); break;
            case '\\': emit(cg, "\\\\"); break;
            case '\n': emit(cg, "\\n");  break;
            case '\r': emit(cg, "\\r");  break;
            case '\t': emit(cg, "\\t");  break;
            default:
                buf_ensure(cg, 1);
                cg->buf[cg->buf_len++] = *s;
                cg->buf[cg->buf_len]   = '\0';
        }
    }
    emit(cg, "\"");
}
