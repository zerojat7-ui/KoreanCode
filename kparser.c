#define _POSIX_C_SOURCE 200809L
/*
 * kparser.c  —  Kcode 한글 프로그래밍 언어 파서 구현
 * version : v23.0.0
 *
 * v23.0.0 변경 (가속기 재설계 v2.0 — 내장 연산 토큰 인식):
 *   - is_accel_op_tok() 신규 — TOK_KW_ACCEL_* 5종 토큰 판별
 *   - parse_gpu_block() — IDENT 기반 strcmp 대신 토큰 우선 인식으로 전환
 *   - is_callable_kw() — TOK_KW_ACCEL_* 5종 추가 (가속기 블록 외부 호출 대비)
 *
 * v22.0.0 변경 (Concept Identity 단계 9):
 *   - parse_stmt() 에 CI 4종 분기 추가
 *     parse_semantic_infer() — 의미추론 블록
 *     parse_vectorize()      — 벡터화 블록
 *     parse_sem_recon()      — 의미복원 블록
 *     parse_repro_label()    — 재생산라벨 블록
 *   - NODE_NAMES 배열 ONT 6종 + CI 4종 추가
 *
 * v6.1.0 변경:
 *   - #포함 파일명 파싱 추가 (꺽쇠/따옴표/식별자 3종 방식)
 *   - 지원 확장자: .han .hg .c .h .cpp .hpp .py .js .ts .java
 *
 * v6.0.0 변경:
 *   - 인터럽트 시스템 3종 파싱 추가
 *   - parse_signal_handler() — 신호받기 블록
 *   - parse_signal_ctrl()    — 신호무시/신호기본/신호보내기
 *   - parse_isr_handler()    — 간섭 블록 (하드웨어 ISR)
 *   - parse_isr_ctrl()       — 간섭잠금/간섭허용
 *   - parse_event_handler()  — 행사등록 블록
 *   - parse_event_ctrl()     — 행사시작/중단/발생/해제
 *   - parse_stmt() 에 6종 신호/간섭/행사 분기 추가
 *
 * v1.4.0 변경사항:
 *   - 헌법/법률 단일 라인 파싱 추가 (parse_constitution, parse_statute)
 *   - 규정 블록 파싱 추가 (parse_regulation)
 *   - 법령/법위반 블록 방식으로 전면 개정 (parse_contract)
 *   - 파일 내장 함수 키워드 is_callable_kw 등록
 *   - NODE_NAMES 배열 갱신
 *
 * 재귀 하강(Recursive Descent) 파서
 * Python 방식 INDENT/DEDENT 기반 블록 파싱
 *
 * 계약 문법 (v4.2.0):
 *   헌법 [조건], [제재]
 *   법률 [조건], [제재]
 *   규정 [객체명]:
 *       조항 [조건]
 *       [제재]
 *   규정끝
 *   법령 [함수명]:
 *       조항 [조건]
 *       [제재]
 *   법령끝
 *   법위반 [함수명]:
 *       조항 [조건]
 *       [제재]
 *   법위반끝
 *
 * 재귀 하강(Recursive Descent) 파서
 * Python 방식 INDENT/DEDENT 기반 블록 파싱
 *
 * 문법 개요:
 *   program      → stmt* EOF
 *   stmt         → import | class | func | void | if | while
 *                | for_range | for_each | switch | try
 *                | return | break | continue | goto | label
 *                | var_decl | const_decl | pp_stmt | gpu_block
 *                | expr_stmt
 *   block        → NEWLINE INDENT stmt* DEDENT
 *   expr         → assignment
 *   assignment   → (lvalue assign_op) expr | logic_or
 *   logic_or     → logic_and { '또는' logic_and }
 *   logic_and    → logic_not { '그리고' logic_not }
 *   logic_not    → '아니다' logic_not | comparison
 *   comparison   → bitwise { cmp_op bitwise }
 *   bitwise      → shift { ('&'|'|'|'^') shift }
 *   shift        → addition { ('<<'|'>>') addition }
 *   addition     → mult { ('+'|'-') mult }
 *   mult         → power { ('*'|'/'|'%') power }
 *   power        → unary { '**' unary }
 *   unary        → ('-'|'~') unary | postfix
 *   postfix      → primary { '.' IDENT | '[' expr ']' | '(' args ')' }
 *   primary      → literal | IDENT | '(' expr ')' | array | dict | lambda
 *
 * MIT License
 * zerojat7
 */

#include "kparser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ================================================================
 *  노드 관리
 * ================================================================ */

Node *node_new(NodeType type, int line, int col) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (!n) { fprintf(stderr, "메모리 할당 실패\n"); exit(1); }
    n->type  = type;
    n->line  = line;
    n->col   = col;
    n->dtype = TOK_EOF;  /* 자료형 없음 기본값 */
    n->op    = TOK_EOF;
    return n;
}

void node_add_child(Node *parent, Node *child) {
    if (!parent || !child) return;
    if (parent->child_count >= parent->child_cap) {
        int new_cap = parent->child_cap == 0 ? 4 : parent->child_cap * 2;
        parent->children = (Node **)realloc(parent->children,
                                            sizeof(Node *) * (size_t)new_cap);
        if (!parent->children) { fprintf(stderr, "메모리 할당 실패\n"); exit(1); }
        parent->child_cap = new_cap;
    }
    parent->children[parent->child_count++] = child;
}

void node_free(Node *node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++)
        node_free(node->children[i]);
    free(node->children);
    free(node->sval);
    free(node);
}

/* sval 설정 헬퍼 (토큰 텍스트 → 복사) */
static void node_set_sval_tok(Node *n, const Token *tok) {
    if (!tok || tok->length == 0) { n->sval = NULL; return; }
    n->sval = (char *)malloc(tok->length + 1);
    if (!n->sval) { fprintf(stderr, "메모리 할당 실패\n"); exit(1); }
    memcpy(n->sval, tok->start, tok->length);
    n->sval[tok->length] = '\0';
}

static void node_set_sval_str(Node *n, const char *s) {
    if (!s) { n->sval = NULL; return; }
    size_t len = strlen(s);
    n->sval = (char *)malloc(len + 1);
    if (!n->sval) { fprintf(stderr, "메모리 할당 실패\n"); exit(1); }
    memcpy(n->sval, s, len + 1);
}

/* ================================================================
 *  파서 내부 헬퍼
 * ================================================================ */

/* 오류 메시지 추가 */
static void parser_error_at(Parser *p, const Token *tok, const char *msg) {
    if (p->panic_mode) return;  /* 이미 복구 중 */
    p->panic_mode = 1;
    p->had_error  = 1;

    if (p->error_count < KCODE_PARSER_MAX_ERRORS) {
        snprintf(p->errors[p->error_count++], 256,
                 "파서 오류 %d:%d — %s", tok->line, tok->col, msg);
    }
    fprintf(stderr, "[파서 오류] %d:%d — %s\n", tok->line, tok->col, msg);
}

static void parser_error(Parser *p, const char *msg) {
    parser_error_at(p, &p->current, msg);
}

/* 다음 토큰 가져오기 */
static Token parser_advance(Parser *p) {
    p->previous = p->current;
    p->current  = lexer_next(p->lexer);
    if (p->current.type == TOK_ERROR) {
        char msg[128];
        token_to_str(&p->current, msg, sizeof(msg));
        parser_error(p, msg);
    }
    return p->previous;
}

/* 현재 토큰이 type 인지 확인 (소비 안 함) */
static int check(Parser *p, TokenType type) {
    return p->current.type == type;
}

/* 현재 토큰이 type 이면 소비하고 1 반환 */
static int match(Parser *p, TokenType type) {
    if (!check(p, type)) return 0;
    parser_advance(p);
    return 1;
}

/* 현재 토큰이 type 이어야 함, 아니면 오류 */
static Token consume(Parser *p, TokenType type, const char *msg) {
    if (check(p, type)) return parser_advance(p);
    parser_error(p, msg);
    return p->current;
}

/* NEWLINE 토큰 건너뜀 */
static void skip_newlines(Parser *p) {
    while (check(p, TOK_NEWLINE)) parser_advance(p);
}

/* 구문 끝 처리 (NEWLINE 또는 EOF) */
static void consume_stmt_end(Parser *p) {
    if (check(p, TOK_NEWLINE) || check(p, TOK_EOF))
        parser_advance(p);
}

/* 오류 복구: 다음 구문 시작까지 토큰 버리기 */
static void synchronize(Parser *p) {
    p->panic_mode = 0;
    while (!check(p, TOK_EOF)) {
        if (p->previous.type == TOK_NEWLINE) return;
        switch (p->current.type) {
            case TOK_KW_HAMSU:
            case TOK_KW_JEONGUI:
            case TOK_KW_MANYAK:
            case TOK_KW_DONGAN:
            case TOK_KW_BANBOG:
            case TOK_KW_BANHWAN:
            case TOK_KW_GAEGCHE:
            case TOK_KW_SIDO:
                return;
            default:
                break;
        }
        parser_advance(p);
    }
}

/* 현재 토큰이 자료형 키워드인지 확인 */
static int is_type_kw(TokenType t) {
    switch (t) {
        case TOK_KW_JEONGSU: case TOK_KW_SILSU:  case TOK_KW_GEULJA:
        case TOK_KW_MUNJA:   case TOK_KW_NOLI:   case TOK_KW_EOPSEUM:
        case TOK_KW_HAENGNYEOL: case TOK_KW_SAJIN: case TOK_KW_GEURIM:
        case TOK_KW_HAMSUFORM:  case TOK_KW_BAELYEOL: case TOK_KW_SAJEON:
        case TOK_KW_MOGLOG:  case TOK_KW_GAEGCHE: case TOK_KW_YEOLGEEO:
        case TOK_KW_HWAMYEON: case TOK_KW_HWAMYEON3D:
            return 1;
        default:
            return 0;
    }
}

/* 대입 연산자인지 확인 */
static int is_assign_op(TokenType t) {
    switch (t) {
        case TOK_EQ: case TOK_PLUSEQ: case TOK_MINUSEQ:
        case TOK_STAREQ: case TOK_SLASHEQ: case TOK_PERCENTEQ:
            return 1;
        default: return 0;
    }
}

/*
 * 이름으로 사용 가능한 토큰인지 확인
 * — 식별자뿐 아니라 Kcode에서 이름으로 흔히 쓰이는 키워드도 허용
 *   예) 생성(constructor), 자신(self), 부모(super) 등
 */
static int is_name_token(TokenType t) {
    if (t == TOK_IDENT) return 1;
    switch (t) {
        /* 자료구조/OOP 맥락에서 이름으로 쓰이는 키워드 */
        case TOK_KW_SAENGSEONG: /* 생성 */
        case TOK_KW_JASIN:      /* 자신 */
        case TOK_KW_BUMO:       /* 부모 */
        case TOK_KW_MOGLOG:     /* 목록 */
        case TOK_KW_YEOLGEEO:   /* 열거 */
        case TOK_KW_TEL:        /* 틀   */
        case TOK_KW_IDONG:      /* 이동 */
            return 1;
        default:
            return 0;
    }
}

/* 내장 호출 가능 키워드인지 확인 (출력, 입력, 오류 등) */
static int is_callable_kw(TokenType t) {
    switch (t) {
        /* 기존 내장 함수 */
        case TOK_KW_CHULRYEOK: case TOK_KW_CHULNO:
        case TOK_KW_IBRYEOK:   case TOK_KW_FILEOPEN:
        case TOK_KW_ORU:       case TOK_KW_SAJIN_OPEN:
        case TOK_KW_GEURIM_MAKE: case TOK_KW_AI_CONNECT:
        /* 파일 내장 함수 v4.2.0 */
        case TOK_KW_FILE_CLOSE:    case TOK_KW_FILE_READ:
        case TOK_KW_FILE_READLINE: case TOK_KW_FILE_WRITE:
        case TOK_KW_FILE_WRITELINE:case TOK_KW_FILE_READALL:
        case TOK_KW_FILE_WRITEALL: case TOK_KW_FILE_EXISTS:
        case TOK_KW_FILE_SIZE:     case TOK_KW_FILE_LIST:
        case TOK_KW_FILE_NAME:     case TOK_KW_FILE_EXT:
        case TOK_KW_DIR_MAKE:      case TOK_KW_FILE_DELETE:
        case TOK_KW_FILE_COPY:     case TOK_KW_FILE_MOVE:
        /* 산업/임베디드 내장 함수 v16.0.0 */
        case TOK_KW_GPIO_WRITE:    case TOK_KW_GPIO_READ:
        case TOK_KW_I2C_CONNECT:   case TOK_KW_I2C_READ:   case TOK_KW_I2C_WRITE:
        case TOK_KW_SPI_SEND:      case TOK_KW_SPI_READ:
        case TOK_KW_UART_SETUP:    case TOK_KW_UART_SEND:  case TOK_KW_UART_READ:
        case TOK_KW_MODBUS_CONNECT:case TOK_KW_MODBUS_READ:case TOK_KW_MODBUS_WRITE:
        case TOK_KW_MODBUS_DISCONNECT:
        case TOK_KW_CAN_FILTER:    case TOK_KW_CAN_SEND:   case TOK_KW_CAN_READ:
        case TOK_KW_MQTT_CONNECT:  case TOK_KW_MQTT_PUBLISH:
        case TOK_KW_MQTT_SUBSCRIBE:case TOK_KW_MQTT_DISCONNECT:
        case TOK_KW_ROS2_PUBLISH:  case TOK_KW_ROS2_SUBSCRIBE:
        /* 안전 규격 내장 함수 v17.0.0 */
        case TOK_KW_FAILSAFE:      case TOK_KW_EMERG_STOP:
        /* 온디바이스 AI 내장 함수 v18.0.0 */
        case TOK_KW_AI_LOAD:       case TOK_KW_AI_PREDICT:
        case TOK_KW_AI_TRAIN_STEP: case TOK_KW_AI_SAVE:
        /* 가속기 내장 연산 v23.0.0 */
        case TOK_KW_ACCEL_MATMUL:    case TOK_KW_ACCEL_MATADD:
        case TOK_KW_ACCEL_CONV:      case TOK_KW_ACCEL_ACTIVATE:
        case TOK_KW_ACCEL_TRANSPOSE:
            return 1;
        default: return 0;
    }
}

/* ================================================================
 *  전방 선언
 * ================================================================ */
static Node *parse_stmt(Parser *p);
static Node *parse_block(Parser *p);
static Node *parse_expr(Parser *p);
static Node *parse_params(Parser *p, Node *func_node);

/* ================================================================
 *  블록 파싱: NEWLINE INDENT stmt* DEDENT
 * ================================================================ */
static Node *parse_block(Parser *p) {
    /* ':' 뒤에 NEWLINE 기대 */
    consume(p, TOK_NEWLINE, "':' 뒤에 줄바꿈이 필요합니다");
    skip_newlines(p);
    consume(p, TOK_INDENT, "블록이 들여쓰기로 시작해야 합니다");

    Node *block = node_new(NODE_BLOCK, p->previous.line, p->previous.col);

    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) break;
        Node *s = parse_stmt(p);
        if (s) node_add_child(block, s);
        if (p->panic_mode) synchronize(p);
    }

    consume(p, TOK_DEDENT, "블록 들여쓰기가 올바르게 닫히지 않았습니다");
    return block;
}

/* ================================================================
 *  매개변수 파싱
 *  param → [type] IDENT ['=' expr] | '...'
 * ================================================================ */
static Node *parse_params(Parser *p, Node *func_node) {
    consume(p, TOK_LPAREN, "'(' 가 필요합니다");

    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        /* 가변 인수 ... */
        if (match(p, TOK_DOTS)) {
            Node *vp = node_new(NODE_PARAM, p->previous.line, p->previous.col);
            node_set_sval_str(vp, "...");
            node_add_child(func_node, vp);
            break;
        }

        Node *param = node_new(NODE_PARAM, p->current.line, p->current.col);

        /* 자료형 (선택) — 타입 키워드 또는 타입-as-식별자 처리 */
        if (is_type_kw(p->current.type)) {
            param->dtype = p->current.type;
            parser_advance(p);
        } else if (check(p, TOK_IDENT)) {
            /* 현재 IDENT 다음에도 IDENT가 오면 첫 번째가 타입 이름 */
            Token peek = lexer_peek(p->lexer);
            if (peek.type == TOK_IDENT) {
                /* 타입 이름 소비 (문자열 타입은 sval에 저장 불필요 — dtype TOK_EOF 유지) */
                parser_advance(p);
            }
        }

        /* 이름 — IDENT, 자신, 부모, 생성 등 모두 허용 */
        if (is_name_token(p->current.type)) {
            node_set_sval_tok(param, &p->current);
            parser_advance(p);
        } else {
            parser_error(p, "매개변수 이름이 필요합니다");
            node_free(param);
            break;
        }

        /* 기본값 */
        if (match(p, TOK_EQ)) {
            Node *def = parse_expr(p);
            node_add_child(param, def);
        }

        node_add_child(func_node, param);

        if (!match(p, TOK_COMMA)) break;
    }

    consume(p, TOK_RPAREN, "')' 가 필요합니다");
    return func_node;
}

/* ================================================================
 *  구문 파싱
 * ================================================================ */

/* 가져오기: 가짐 모듈 [로부터 이름 {, 이름}] */
static Node *parse_import(Parser *p) {
    Node *n = node_new(NODE_IMPORT, p->previous.line, p->previous.col);

    if (!check(p, TOK_IDENT)) {
        parser_error(p, "모듈 이름이 필요합니다");
        return n;
    }
    node_set_sval_tok(n, &p->current);
    parser_advance(p);

    /* 로부터 이름, 이름, ... */
    if (match(p, TOK_KW_ROBUTEO)) {
        do {
            if (!check(p, TOK_IDENT)) {
                parser_error(p, "가져올 이름이 필요합니다");
                break;
            }
            Node *name = node_new(NODE_IDENT, p->current.line, p->current.col);
            node_set_sval_tok(name, &p->current);
            parser_advance(p);
            node_add_child(n, name);
        } while (match(p, TOK_COMMA));
    }

    consume_stmt_end(p);
    return n;
}

/* 함수/정의 선언 */
static Node *parse_func_decl(Parser *p, NodeType ntype) {
    Node *n = node_new(ntype, p->previous.line, p->previous.col);

    if (!is_name_token(p->current.type)) {
        parser_error(p, "함수 이름이 필요합니다");
        return n;
    }
    node_set_sval_tok(n, &p->current);
    parser_advance(p);

    parse_params(p, n);
    consume(p, TOK_COLON, "함수 선언 뒤에 ':' 가 필요합니다");
    Node *body = parse_block(p);
    node_add_child(n, body);
    return n;
}

/* 객체(클래스) 선언 */
static Node *parse_class_decl(Parser *p) {
    Node *n = node_new(NODE_CLASS_DECL, p->previous.line, p->previous.col);

    if (!check(p, TOK_IDENT)) {
        parser_error(p, "객체 이름이 필요합니다");
        return n;
    }
    node_set_sval_tok(n, &p->current);
    parser_advance(p);

    /* 이어받기 부모 */
    if (match(p, TOK_KW_IEOBATGI)) {
        if (!check(p, TOK_IDENT)) {
            parser_error(p, "부모 클래스 이름이 필요합니다");
        } else {
            Node *parent = node_new(NODE_IDENT, p->current.line, p->current.col);
            node_set_sval_tok(parent, &p->current);
            parser_advance(p);
            node_add_child(n, parent);
        }
    }

    consume(p, TOK_COLON, "객체 선언 뒤에 ':' 가 필요합니다");
    Node *body = parse_block(p);
    node_add_child(n, body);
    return n;
}

/* 만약 구문 */
static Node *parse_if(Parser *p) {
    Node *n = node_new(NODE_IF, p->previous.line, p->previous.col);

    /* 조건 */
    Node *cond = parse_expr(p);
    node_add_child(n, cond);
    consume(p, TOK_COLON, "만약 조건 뒤에 ':' 가 필요합니다");

    /* then 블록 */
    Node *then_blk = parse_block(p);
    node_add_child(n, then_blk);

    /* 아니면 만약 (elif) / 아니면 (else) */
    skip_newlines(p);
    while (check(p, TOK_KW_ANIMYEON)) {
        parser_advance(p);  /* 아니면 소비 */

        if (match(p, TOK_KW_MANYAK)) {
            /* 아니면 만약 → elif */
            Node *elif = node_new(NODE_ELIF, p->previous.line, p->previous.col);
            Node *elif_cond = parse_expr(p);
            node_add_child(elif, elif_cond);
            consume(p, TOK_COLON, "아니면 만약 조건 뒤에 ':' 가 필요합니다");
            Node *elif_blk = parse_block(p);
            node_add_child(elif, elif_blk);
            node_add_child(n, elif);
            skip_newlines(p);
        } else {
            /* 아니면 → else */
            Node *els = node_new(NODE_ELSE, p->previous.line, p->previous.col);
            consume(p, TOK_COLON, "아니면 뒤에 ':' 가 필요합니다");
            Node *else_blk = parse_block(p);
            node_add_child(els, else_blk);
            node_add_child(n, els);
            break;
        }
    }

    return n;
}

/* 동안 구문 */
static Node *parse_while(Parser *p) {
    Node *n = node_new(NODE_WHILE, p->previous.line, p->previous.col);
    Node *cond = parse_expr(p);
    node_add_child(n, cond);
    consume(p, TOK_COLON, "동안 조건 뒤에 ':' 가 필요합니다");
    Node *body = parse_block(p);
    node_add_child(n, body);
    return n;
}

/* 반복 변수 부터 시작 까지 끝: 블록 */
static Node *parse_for_range(Parser *p) {
    Node *n = node_new(NODE_FOR_RANGE, p->previous.line, p->previous.col);

    if (!check(p, TOK_IDENT)) {
        parser_error(p, "반복 변수 이름이 필요합니다");
        return n;
    }
    node_set_sval_tok(n, &p->current);
    parser_advance(p);

    consume(p, TOK_KW_BUTEO, "'부터' 가 필요합니다");
    Node *start = parse_expr(p);
    node_add_child(n, start);

    consume(p, TOK_KW_KKAJI, "'까지' 가 필요합니다");
    Node *end = parse_expr(p);
    node_add_child(n, end);

    /* 선택적 step */
    if (match(p, TOK_COMMA)) {
        Node *step = parse_expr(p);
        node_add_child(n, step);
    }

    consume(p, TOK_COLON, "반복 선언 뒤에 ':' 가 필요합니다");
    Node *body = parse_block(p);
    node_add_child(n, body);
    return n;
}

/* 각각 변수 안에 목록: 블록 */
static Node *parse_for_each(Parser *p) {
    Node *n = node_new(NODE_FOR_EACH, p->previous.line, p->previous.col);

    if (!check(p, TOK_IDENT)) {
        parser_error(p, "각각 변수 이름이 필요합니다");
        return n;
    }
    node_set_sval_tok(n, &p->current);
    parser_advance(p);

    consume(p, TOK_KW_ANE, "'안에' 가 필요합니다");
    Node *iterable = parse_expr(p);
    node_add_child(n, iterable);

    consume(p, TOK_COLON, "각각 선언 뒤에 ':' 가 필요합니다");
    Node *body = parse_block(p);
    node_add_child(n, body);
    return n;
}

/* 선택 구문 */
static Node *parse_switch(Parser *p) {
    Node *n = node_new(NODE_SWITCH, p->previous.line, p->previous.col);
    Node *expr = parse_expr(p);
    node_add_child(n, expr);

    consume(p, TOK_COLON, "선택 뒤에 ':' 가 필요합니다");
    consume(p, TOK_NEWLINE, "선택 뒤에 줄바꿈이 필요합니다");
    skip_newlines(p);
    consume(p, TOK_INDENT, "선택 블록이 들여쓰기로 시작해야 합니다");
    skip_newlines(p);

    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF)) {
        if (match(p, TOK_KW_GYEONGWOO)) {
            /* 경우 값: 블록 */
            Node *case_n = node_new(NODE_CASE, p->previous.line, p->previous.col);
            Node *val = parse_expr(p);
            node_add_child(case_n, val);
            consume(p, TOK_COLON, "경우 뒤에 ':' 가 필요합니다");
            Node *body = parse_block(p);
            node_add_child(case_n, body);
            node_add_child(n, case_n);
        } else if (match(p, TOK_KW_GEUWOI)) {
            /* 그외: 블록 */
            Node *def = node_new(NODE_DEFAULT, p->previous.line, p->previous.col);
            consume(p, TOK_COLON, "그외 뒤에 ':' 가 필요합니다");
            Node *body = parse_block(p);
            node_add_child(def, body);
            node_add_child(n, def);
            break;
        } else {
            parser_error(p, "경우 또는 그외 가 필요합니다");
            break;
        }
        skip_newlines(p);
    }

    consume(p, TOK_DEDENT, "선택 블록이 올바르게 닫히지 않았습니다");
    return n;
}

/* 시도/실패시/항상 */
static Node *parse_try(Parser *p) {
    Node *n = node_new(NODE_TRY, p->previous.line, p->previous.col);

    consume(p, TOK_COLON, "시도 뒤에 ':' 가 필요합니다");
    Node *try_blk = parse_block(p);
    node_add_child(n, try_blk);

    skip_newlines(p);
    consume(p, TOK_KW_SILPAESI, "'실패시' 가 필요합니다");
    consume(p, TOK_COLON, "실패시 뒤에 ':' 가 필요합니다");
    Node *catch_blk = parse_block(p);
    node_add_child(n, catch_blk);

    /* 항상 (선택) */
    skip_newlines(p);
    if (match(p, TOK_KW_HANGSANG)) {
        consume(p, TOK_COLON, "항상 뒤에 ':' 가 필요합니다");
        Node *fin_blk = parse_block(p);
        node_add_child(n, fin_blk);
    }

    return n;
}

/* 변수 선언: [type] IDENT [= expr] */
static Node *parse_var_decl(Parser *p, int is_const) {
    NodeType ntype = is_const ? NODE_CONST_DECL : NODE_VAR_DECL;
    Node *n = node_new(ntype, p->current.line, p->current.col);

    /* 자료형 키워드 */
    if (is_type_kw(p->current.type)) {
        n->dtype = p->current.type;
        parser_advance(p);
    }

    /* 이름 */
    if (!check(p, TOK_IDENT)) {
        parser_error(p, "변수 이름이 필요합니다");
        return n;
    }
    node_set_sval_tok(n, &p->current);
    parser_advance(p);

    /* 초기값 */
    if (match(p, TOK_EQ)) {
        Node *val = parse_expr(p);
        node_add_child(n, val);
    } else if (is_const) {
        parser_error(p, "고정(상수)은 초기값이 필요합니다");
    }

    consume_stmt_end(p);
    return n;
}

/* 전처리기 구문 */
static Node *parse_pp_stmt(Parser *p, TokenType pp_type) {
    Node *n = node_new(NODE_PP_STMT, p->previous.line, p->previous.col);
    n->op = pp_type;

    /* #정의 이름 [값] */
    if (pp_type == TOK_PP_DEFINE) {
        if (check(p, TOK_IDENT)) {
            node_set_sval_tok(n, &p->current);
            parser_advance(p);
            if (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF)) {
                Node *val = parse_expr(p);
                node_add_child(n, val);
            }
        }
    }

    /* #포함 "파일명" 또는 #포함 <파일명>
     * 지원 확장자: .han .hg .c .h .cpp .hpp .py .js .ts .java */
    if (pp_type == TOK_PP_INCLUDE) {
        /* 따옴표 문자열 "파일명" */
        if (check(p, TOK_STRING)) {
            node_set_sval_tok(n, &p->current);
            parser_advance(p);
        }
        /* 꺽쇠 <파일명> — 토큰이 연산자로 분리될 수 있으므로 원문 재조합 */
        else if (check(p, TOK_LT)) {
            parser_advance(p); /* < 소비 */
            char fname[256] = "";
            /* > 가 나올 때까지 토큰을 문자열로 합침 */
            while (!check(p, TOK_GT) && !check(p, TOK_NEWLINE) &&
                   !check(p, TOK_EOF)) {
                char part[128];
                token_to_str(&p->current, part, sizeof(part));
                strncat(fname, part, sizeof(fname) - strlen(fname) - 1);
                parser_advance(p);
            }
            if (check(p, TOK_GT)) parser_advance(p); /* > 소비 */
            /* sval 에 파일명 저장 */
            if (n->sval) free(n->sval);
            n->sval = strdup(fname);
        }
        /* 식별자만 온 경우 (확장자 없이 모듈명) */
        else if (check(p, TOK_IDENT)) {
            node_set_sval_tok(n, &p->current);
            parser_advance(p);
        }
    }

    consume_stmt_end(p);
    return n;
}

/* GPU/NPU 블록: 가속기: ... 가속기끝 */
/* ================================================================
 *  계약 시스템 — 헌법 / 법률 / 규정 / 법령 / 법위반 / 복원지점 파싱
 *  v4.2.0 계층 구조 블록 방식
 * ================================================================ */

/*
 * parse_sanction_inline() — 쉼표 뒤 제재 파싱 (헌법/법률 단일 라인용)
 *
 * 형식: [제재] → 경고|보고|중단|회귀 [지점]|대체 [값|함수명]
 */
static Node *parse_sanction_inline(Parser *p) {
    Node *sanction = node_new(NODE_SANCTION, p->current.line, p->current.col);

    if (check(p, TOK_KW_GYEONGGO) || check(p, TOK_KW_BOGO)   ||
        check(p, TOK_KW_JUNGDAN)  || check(p, TOK_KW_HOEGWI)  ||
        check(p, TOK_KW_DAECHE)) {

        sanction->op = p->current.type;
        parser_advance(p);

        if (sanction->op == TOK_KW_HOEGWI) {
            if (check(p, TOK_IDENT) || is_name_token(p->current.type)) {
                char buf[256] = "";
                token_to_str(&p->current, buf, sizeof(buf));
                sanction->sval = strdup(buf);
                parser_advance(p);
            }
        } else if (sanction->op == TOK_KW_DAECHE) {
            if (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF) &&
                !check(p, TOK_DEDENT)) {
                if (check(p, TOK_IDENT)) {
                    Lexer  saved_lx  = *p->lexer;
                    Token  saved_cur = p->current;
                    Token  saved_prv = p->previous;
                    Token  ident_tok = p->current;
                    parser_advance(p);
                    if (check(p, TOK_NEWLINE) || check(p, TOK_EOF) ||
                        check(p, TOK_DEDENT)  || check(p, TOK_COMMA)) {
                        char buf[256] = "";
                        token_to_str(&ident_tok, buf, sizeof(buf));
                        sanction->sval = strdup(buf);
                    } else {
                        *p->lexer  = saved_lx;
                        p->current = saved_cur;
                        p->previous= saved_prv;
                        node_add_child(sanction, parse_expr(p));
                    }
                } else {
                    node_add_child(sanction, parse_expr(p));
                }
            }
        }
    } else {
        parser_error(p, "제재(경고|보고|중단|회귀|대체) 중 하나가 필요합니다");
        sanction->op = TOK_KW_GYEONGGO;
    }
    return sanction;
}

/*
 * parse_sanction_block() — 블록 내 제재 파싱 (규정/법령/법위반 블록용)
 *
 * 형식 (들여쓰기 블록 안에서):
 *   조항 [조건식]
 *   [제재]
 */
static void parse_contract_block_body(Parser *p, Node *contract_node) {
    /* NEWLINE + INDENT 소비 */
    consume(p, TOK_NEWLINE, "':' 뒤에 줄바꿈이 필요합니다");
    consume(p, TOK_INDENT,  "계약 블록에 들여쓰기가 필요합니다");

    skip_newlines(p);

    /* 조항 [조건식] */
    if (!match(p, TOK_KW_JOHANG)) {
        parser_error(p, "계약 블록 안에 '조항 [조건]'이 필요합니다");
    }
    Node *cond = parse_expr(p);
    node_add_child(contract_node, cond); /* child[0] = 조건 */
    consume_stmt_end(p);
    skip_newlines(p);

    /* [제재] — 줄 */
    Node *sanction = parse_sanction_inline(p);
    node_add_child(contract_node, sanction); /* child[1] = 제재 */
    consume_stmt_end(p);
    skip_newlines(p);

    /* DEDENT */
    consume(p, TOK_DEDENT, "계약 블록 끝에 내어쓰기가 필요합니다");
    skip_newlines(p);
}

/*
 * parse_constitution() — 헌법 파싱 (단일 라인)
 *
 * 문법: 헌법 [조건], [제재]
 *
 * NODE_CONSTITUTION:
 *   child[0] = 조건 표현식
 *   child[1] = NODE_SANCTION
 */
static Node *parse_constitution(Parser *p) {
    Node *n = node_new(NODE_CONSTITUTION, p->previous.line, p->previous.col);

    Node *cond = parse_expr(p);
    node_add_child(n, cond);

    if (!match(p, TOK_COMMA)) {
        parser_error(p, "헌법: 조건 뒤에 ',' 가 필요합니다");
    }

    Node *sanction = parse_sanction_inline(p);
    node_add_child(n, sanction);

    consume_stmt_end(p);
    return n;
}

/*
 * parse_statute() — 법률 파싱 (단일 라인)
 *
 * 문법: 법률 [조건], [제재]
 *
 * NODE_STATUTE:
 *   child[0] = 조건 표현식
 *   child[1] = NODE_SANCTION
 */
static Node *parse_statute(Parser *p) {
    Node *n = node_new(NODE_STATUTE, p->previous.line, p->previous.col);

    Node *cond = parse_expr(p);
    node_add_child(n, cond);

    if (!match(p, TOK_COMMA)) {
        parser_error(p, "법률: 조건 뒤에 ',' 가 필요합니다");
    }

    Node *sanction = parse_sanction_inline(p);
    node_add_child(n, sanction);

    consume_stmt_end(p);
    return n;
}

/*
 * parse_regulation() — 규정 파싱 (블록)
 *
 * 문법:
 *   규정 [객체명]:
 *       조항 [조건]
 *       [제재]
 *   규정끝
 *
 * NODE_REGULATION:
 *   sval    = 객체명
 *   child[0]= 조건 표현식
 *   child[1]= NODE_SANCTION
 */
static Node *parse_regulation(Parser *p) {
    Node *n = node_new(NODE_REGULATION, p->previous.line, p->previous.col);

    /* 객체명 */
    if (check(p, TOK_IDENT) || is_name_token(p->current.type)) {
        char buf[256] = "";
        token_to_str(&p->current, buf, sizeof(buf));
        n->sval = strdup(buf);
        parser_advance(p);
    } else {
        parser_error(p, "규정: 객체 이름이 필요합니다");
        n->sval = strdup("?");
    }

    consume(p, TOK_COLON, "규정 객체명 뒤에 ':' 가 필요합니다");
    parse_contract_block_body(p, n);

    /* 규정끝 */
    if (!match(p, TOK_KW_KKUT_GYUJEONG)) {
        parser_error(p, "'규정끝' 이 필요합니다");
    }
    consume_stmt_end(p);
    return n;
}

/*
 * parse_contract() — 법령 / 법위반 파싱 (블록 방식 v4.2.0)
 *
 * 문법:
 *   법령 [함수명]:
 *       조항 [조건]
 *       [제재]
 *   법령끝
 *
 *   법위반 [함수명]:
 *       조항 [조건]
 *       [제재]
 *   법위반끝
 *
 * NODE_CONTRACT:
 *   op      = TOK_KW_BEOPRYEONG | TOK_KW_BEOPWIBAN
 *   sval    = 대상 함수명
 *   child[0]= 조건 표현식
 *   child[1]= NODE_SANCTION
 */
static Node *parse_contract(Parser *p) {
    TokenType kind = p->previous.type;
    Node *n = node_new(NODE_CONTRACT, p->previous.line, p->previous.col);
    n->op = kind;

    /* 대상 함수명 */
    if (check(p, TOK_IDENT) || is_name_token(p->current.type)) {
        char buf[256] = "";
        token_to_str(&p->current, buf, sizeof(buf));
        n->sval = strdup(buf);
        parser_advance(p);
    } else {
        parser_error(p, "법령/법위반: 대상 함수 이름이 필요합니다");
        n->sval = strdup("?");
    }

    consume(p, TOK_COLON, "법령/법위반 함수명 뒤에 ':' 가 필요합니다");
    parse_contract_block_body(p, n);

    /* 법령끝 / 법위반끝 */
    if (kind == TOK_KW_BEOPRYEONG) {
        if (!match(p, TOK_KW_KKUT_BEOPRYEONG)) {
            parser_error(p, "'법령끝' 이 필요합니다");
        }
    } else {
        if (!match(p, TOK_KW_KKUT_BEOPWIBAN)) {
            parser_error(p, "'법위반끝' 이 필요합니다");
        }
    }
    consume_stmt_end(p);
    return n;
}

/*
 * parse_checkpoint() — 복원지점 파싱
 *
 * 문법:
 *   복원지점 [이름]
 *
 * NODE_CHECKPOINT 구조:
 *   sval = 지점 이름 문자열
 */
static Node *parse_checkpoint(Parser *p) {
    Node *n = node_new(NODE_CHECKPOINT, p->previous.line, p->previous.col);

    if (check(p, TOK_IDENT) || is_name_token(p->current.type)) {
        char buf[256] = "";
        token_to_str(&p->current, buf, sizeof(buf));
        n->sval = strdup(buf);
        parser_advance(p);
    } else {
        parser_error(p, "복원지점: 지점 이름이 필요합니다");
        n->sval = strdup("?");
    }

    consume_stmt_end(p);
    return n;
}

/* ================================================================
 *  가속기 내장 연산 이름 목록 (v9.0.0)
 *  블록 본문에서 이 이름으로 시작하는 호출을 NODE_GPU_OP 로 파싱
 * ================================================================ */
static const char *kc_gpu_ops[] = {
    "\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1",           /* 행렬곱  matmul     */
    "\xED\x96\x89\xEB\xA0\xAC\xED\x95\xA9",           /* 행렬합  matadd     */
    "\xED\x95\xA9\xEC\x84\xB1\xEA\xB3\xB1",           /* 합성곱  conv2d     */
    "\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94",           /* 활성화  activate   */
    "\xEC\xA0\x84\xEC\xB9\x98",                        /* 전치    transpose  */
    NULL
};

static int is_gpu_op_name(const char *s) {
    if (!s) return 0;
    for (int i = 0; kc_gpu_ops[i]; i++)
        if (strcmp(s, kc_gpu_ops[i]) == 0) return 1;
    return 0;
}

/*
 * is_accel_op_tok — v23.0.0
 * TOK_KW_ACCEL_* 5종 토큰 판별 (토큰 기반 우선 인식)
 * 렉서가 정식 토큰으로 등록된 경우 IDENT strcmp 전에 먼저 확인.
 */
static int is_accel_op_tok(TokenType t) {
    switch (t) {
        case TOK_KW_ACCEL_MATMUL:
        case TOK_KW_ACCEL_MATADD:
        case TOK_KW_ACCEL_CONV:
        case TOK_KW_ACCEL_ACTIVATE:
        case TOK_KW_ACCEL_TRANSPOSE:
            return 1;
        default:
            return 0;
    }
}

/*
 * accel_op_tok_to_name — v23.0.0
 * 가속기 연산 토큰 → kc_accel_exec() 에 전달할 한글 이름 문자열 반환
 */
static const char *accel_op_tok_to_name(TokenType t) {
    switch (t) {
        case TOK_KW_ACCEL_MATMUL:    return "\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1"; /* 행렬곱 */
        case TOK_KW_ACCEL_MATADD:    return "\xED\x96\x89\xEB\xA0\xAC\xED\x95\xA9"; /* 행렬합 */
        case TOK_KW_ACCEL_CONV:      return "\xED\x95\xA9\xEC\x84\xB1\xEA\xB3\xB1"; /* 합성곱 */
        case TOK_KW_ACCEL_ACTIVATE:  return "\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94"; /* 활성화 */
        case TOK_KW_ACCEL_TRANSPOSE: return "\xEC\xA0\x84\xEC\xB9\x98";             /* 전치   */
        default:                     return NULL;
    }
}

/* ================================================================
 *  parse_gpu_op() — 가속기 내장 연산 파싱
 *
 *  형식: 행렬곱(A, B) => 결과
 *        활성화(A, "relu") => 결과
 *
 *  NODE_GPU_OP
 *    sval     = 연산 이름
 *    val.ival = 반환 child 인덱스 (-1=없음)
 *    child[0..k-1] = 입력 인수 (NODE_IDENT 또는 NODE_STRING_LIT)
 *    child[k]      = 반환 변수 (NODE_IDENT, => 뒤)
 * ================================================================ */
static Node *parse_gpu_op(Parser *p, const char *op_name) {
    Node *n = node_new(NODE_GPU_OP, p->previous.line, p->previous.col);
    node_set_sval_str(n, op_name);
    n->val.ival = -1;

    /* '(' 인수 목록 ')' */
    consume(p, TOK_LPAREN, "가속기 연산 뒤에 '(' 가 필요합니다");
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        Node *arg = NULL;
        if (check(p, TOK_IDENT)) {
            arg = node_new(NODE_IDENT, p->current.line, p->current.col);
            char tbuf[256] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            arg->sval = strdup(tbuf);
            parser_advance(p);
        } else {
            /* 문자열 리터럴 인수 ("relu" 등) */
            arg = parse_expr(p);
        }
        if (arg) node_add_child(n, arg);
        if (!match(p, TOK_COMMA)) break;
    }
    consume(p, TOK_RPAREN, "가속기 연산 인수 목록에 ')' 가 필요합니다");

    /* 선택적 반환: => 변수명 */
    if (match(p, TOK_ARROW)) {
        if (check(p, TOK_IDENT)) {
            Node *ret = node_new(NODE_IDENT, p->current.line, p->current.col);
            char tbuf[256] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            ret->sval = strdup(tbuf);
            n->val.ival = (int64_t)n->child_count;
            node_add_child(n, ret);
            parser_advance(p);
        } else {
            parser_error(p, "가속기 연산 '=>' 뒤에 반환 변수 이름이 필요합니다");
        }
    }

    consume_stmt_end(p);
    return n;
}

/* ================================================================
 *  MCP 시스템 파싱 (v14.0.0)
 *
 *  문법:
 *    헌법 MCP서버 "이름":
 *        MCP버전 = "1.0.0"
 *        MCP설명 = "설명"
 *        법률 MCP도구 "이름":
 *            MCP설명 = "..."
 *            법령: 조건
 *            법위반: MCP오류(코드, 메시지)
 *            실행: 블록
 *        MCP도구끝
 *        법률 MCP자원 "이름":
 *            MCP주소 = "uri"
 *            MCP형식 = "mime"
 *            실행: 블록
 *        MCP자원끝
 *        법률 MCP프롬프트 "이름":
 *            MCP설명 = "..."
 *            실행: 블록
 *        MCP프롬프트끝
 *    MCP서버끝
 *
 *  NODE_MCP_TOOL / NODE_MCP_RESOURCE / NODE_MCP_PROMPT
 *    sval        = 핸들러 이름
 *    children[0] = NODE_BLOCK (핸들러 내부 구문 전체)
 *
 *  NODE_MCP_SERVER
 *    sval        = 서버 이름
 *    children[0] = NODE_BLOCK (핸들러 노드 목록)
 * ================================================================ */

/* MCP 핸들러 블록 파싱 (도구/자원/프롬프트 공통) */
static Node *parse_mcp_handler(Parser *p, NodeType ntype, TokenType end_tok) {
    int line = p->previous.line, col = p->previous.col;

    /* "이름" */
    if (!check(p, TOK_STRING)) {
        parser_error(p, "MCP 핸들러 이름(문자열)이 필요합니다");
        return node_new(ntype, line, col);
    }
    Node *n = node_new(ntype, line, col);
    char tbuf[512] = "";
    token_to_str(&p->current, tbuf, sizeof(tbuf));
    /* 따옴표 제거 */
    size_t tlen = strlen(tbuf);
    if (tlen >= 2 && tbuf[0] == '"') {
        tbuf[tlen - 1] = '\0';
        n->sval = strdup(tbuf + 1);
    } else {
        n->sval = strdup(tbuf);
    }
    parser_advance(p);

    /* ':' + NEWLINE + INDENT */
    consume(p, TOK_COLON, "MCP 핸들러 이름 뒤에 ':' 가 필요합니다");
    match(p, TOK_NEWLINE);
    consume(p, TOK_INDENT, "MCP 핸들러 블록에 들여쓰기가 필요합니다");

    /* 내부 블록 파싱 — end_tok 까지 */
    Node *blk = node_new(NODE_BLOCK, line, col);
    while (!check(p, end_tok) && !check(p, TOK_EOF)) {
        match(p, TOK_NEWLINE);
        if (check(p, end_tok) || check(p, TOK_EOF)) break;
        Node *stmt = parse_stmt(p);
        if (stmt) node_add_child(blk, stmt);
    }
    consume(p, end_tok, "MCP 핸들러 블록 종료 키워드가 필요합니다");
    match(p, TOK_NEWLINE);
    match(p, TOK_DEDENT);

    node_add_child(n, blk);
    return n;
}

/* MCP서버 블록 파싱 */
static Node *parse_mcp_server(Parser *p) {
    int line = p->previous.line, col = p->previous.col;

    /* "이름" */
    if (!check(p, TOK_STRING)) {
        parser_error(p, "MCP서버 이름(문자열)이 필요합니다");
        return node_new(NODE_MCP_SERVER, line, col);
    }
    Node *n = node_new(NODE_MCP_SERVER, line, col);
    char tbuf[512] = "";
    token_to_str(&p->current, tbuf, sizeof(tbuf));
    size_t tlen = strlen(tbuf);
    if (tlen >= 2 && tbuf[0] == '"') {
        tbuf[tlen - 1] = '\0';
        n->sval = strdup(tbuf + 1);
    } else {
        n->sval = strdup(tbuf);
    }
    parser_advance(p);

    /* ':' + NEWLINE + INDENT */
    consume(p, TOK_COLON, "MCP서버 이름 뒤에 ':' 가 필요합니다");
    match(p, TOK_NEWLINE);
    consume(p, TOK_INDENT, "MCP서버 블록에 들여쓰기가 필요합니다");

    Node *blk = node_new(NODE_BLOCK, line, col);
    while (!check(p, TOK_KW_KKUT_MCP_SERVER) && !check(p, TOK_EOF)) {
        match(p, TOK_NEWLINE);
        if (check(p, TOK_KW_KKUT_MCP_SERVER) || check(p, TOK_EOF)) break;

        /* 핸들러 블록 */
        if (match(p, TOK_KW_MCP_TOOL)) {
            node_add_child(blk, parse_mcp_handler(p, NODE_MCP_TOOL, TOK_KW_KKUT_MCP_TOOL));
        } else if (match(p, TOK_KW_MCP_RESOURCE)) {
            node_add_child(blk, parse_mcp_handler(p, NODE_MCP_RESOURCE, TOK_KW_KKUT_MCP_RESOURCE));
        } else if (match(p, TOK_KW_MCP_PROMPT)) {
            node_add_child(blk, parse_mcp_handler(p, NODE_MCP_PROMPT, TOK_KW_KKUT_MCP_PROMPT));
        } else {
            /* 버전/설명 등 일반 구문 */
            Node *stmt = parse_stmt(p);
            if (stmt) node_add_child(blk, stmt);
        }
    }
    consume(p, TOK_KW_KKUT_MCP_SERVER, "MCP서버 블록 종료에 'MCP서버끝' 이 필요합니다");
    match(p, TOK_NEWLINE);
    match(p, TOK_DEDENT);

    node_add_child(n, blk);
    return n;
}

/* ================================================================
 *  parse_gpu_block() — 가속기 블록 파싱  (v9.0.0 전면 재작성)
 *
 *  형식:
 *    가속기 [GPU|NPU|CPU]:
 *        행렬곱(A, B) => 결과
 *        ...일반 Kcode 코드...
 *    가속기끝
 *
 *  NODE_GPU_BLOCK
 *    sval     = "GPU" | "NPU" | "CPU" | NULL (생략 시 자동)
 *    val.ival = -1 (미사용, 예약)
 *    child[0..n-1] = NODE_GPU_OP (내장 연산, 0개 이상)
 *    child[last]   = NODE_BLOCK  (블록 본문)
 * ================================================================ */
static Node *parse_gpu_block(Parser *p) {
    Node *n = node_new(NODE_GPU_BLOCK, p->previous.line, p->previous.col);
    n->val.ival = -1;

    /* 선택적 종류: GPU | NPU | CPU
     * 이 토큰들은 ASCII 대문자 → 렉서가 TOK_IDENT 로 반환
     * 현재 토큰이 IDENT 이고 값이 GPU/NPU/CPU 이면 종류로 읽음 */
    if (check(p, TOK_IDENT)) {
        char tbuf[32] = "";
        token_to_str(&p->current, tbuf, sizeof(tbuf));
        if (strcmp(tbuf, "GPU") == 0 ||
            strcmp(tbuf, "NPU") == 0 ||
            strcmp(tbuf, "CPU") == 0) {
            node_set_sval_str(n, tbuf);  /* "GPU" | "NPU" | "CPU" */
            parser_advance(p);
        }
        /* 다른 IDENT 이면 종류 아님 — 그대로 둠 */
    }
    /* sval == NULL → 자동 선택 (GPU→NPU→CPU) */

    consume(p, TOK_COLON, "가속기 뒤에 ':' 가 필요합니다");
    skip_newlines(p);

    /* 블록 본문 — 내장 연산(NODE_GPU_OP)과 일반 코드 분리 파싱
     * INDENT 진입 후 각 줄을 확인:
     *   - 줄 시작이 가속기 내장 연산 이름 → parse_gpu_op()
     *   - 그 외 → 일반 블록으로 수집
     * 가장 단순한 방법: parse_block()으로 전체 파싱 후
     * 블록 내에서 NODE_CALL 이름이 gpu_op이면 NODE_GPU_OP 로 변환.
     * 단, 여기서는 INDENT/DEDENT 구조를 직접 처리하는 parse_block()에
     * 위임하고, NODE_GPU_OP는 블록 앞에 별도 파싱한다. */

    /* INDENT 진입 */
    if (!check(p, TOK_INDENT)) {
        parser_error(p, "가속기 블록: 들여쓰기가 필요합니다");
        node_add_child(n, node_new(NODE_BLOCK, p->current.line, p->current.col));
        return n;
    }
    parser_advance(p); /* INDENT 소비 */
    skip_newlines(p);

    /* 내장 연산 파싱 — DEDENT 또는 가속기끝 전까지
     * v23.0.0: TOK_KW_ACCEL_* 토큰 우선 인식 -> IDENT strcmp 폴백 */
    while (!check(p, TOK_DEDENT) &&
           !check(p, TOK_KW_KKUT_GPU) &&
           !check(p, TOK_EOF)) {

        /* 1순위: 토큰 기반 인식 (v23.0.0 정식 등록 토큰) */
        if (is_accel_op_tok(p->current.type)) {
            const char *op_name = accel_op_tok_to_name(p->current.type);
            parser_advance(p); /* 연산 토큰 소비 */
            Node *op = parse_gpu_op(p, op_name);
            node_add_child(n, op);
            skip_newlines(p);
            continue;
        }

        /* 2순위: IDENT 폴백 (이전 버전 소스 호환) */
        if (check(p, TOK_IDENT)) {
            char tbuf[256] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            if (is_gpu_op_name(tbuf)) {
                parser_advance(p); /* 연산 이름 소비 */
                Node *op = parse_gpu_op(p, tbuf);
                node_add_child(n, op);
                skip_newlines(p);
                continue;
            }
        }

        /* 그 외 — 일반 문장: 블록 노드로 수집 */
        /* 일반 문장들을 하나의 NODE_BLOCK으로 묶기 위해
         * 남은 내용을 parse_block_body()로 읽는다.
         * parse_block_body가 없으므로 parse_stmt()를 반복 사용 */
        Node *body = node_new(NODE_BLOCK, p->current.line, p->current.col);
        while (!check(p, TOK_DEDENT) &&
               !check(p, TOK_KW_KKUT_GPU) &&
               !check(p, TOK_EOF)) {
            Node *stmt = parse_stmt(p);
            if (stmt) node_add_child(body, stmt);
            skip_newlines(p);
        }
        node_add_child(n, body);
        break;
    }

    /* DEDENT 소비 */
    if (check(p, TOK_DEDENT)) parser_advance(p);

    skip_newlines(p);
    consume(p, TOK_KW_KKUT_GPU, "'가속기끝' 이 필요합니다");
    consume_stmt_end(p);
    return n;
}

/* ================================================================
 *  인터럽트 시스템 파싱 (v6.0.0)
 * ================================================================ */

/* 현재 토큰이 신호 이름 상수인지 확인 */
static int is_signal_name(TokenType t) {
    return (t == TOK_KW_SIG_INT  || t == TOK_KW_SIG_TERM ||
            t == TOK_KW_SIG_KILL || t == TOK_KW_SIG_CHLD ||
            t == TOK_KW_SIG_USR1 || t == TOK_KW_SIG_USR2 ||
            t == TOK_KW_SIG_PIPE || t == TOK_KW_SIG_ALRM ||
            t == TOK_KW_SIG_STOP || t == TOK_KW_SIG_CONT);
}

/* 현재 토큰이 ISR 벡터 이름인지 확인 */
static int is_irq_name(TokenType t) {
    return (t == TOK_KW_IRQ_TIMER0    || t == TOK_KW_IRQ_TIMER1 ||
            t == TOK_KW_IRQ_EXT0_RISE || t == TOK_KW_IRQ_EXT0_FALL ||
            t == TOK_KW_IRQ_UART_RX);
}

/*
 * parse_signal_handler() — 신호받기 블록
 *
 * 문법:
 *   신호받기 중단신호:
 *       핸들러 코드
 *   신호받기끝
 *
 * NODE_SIGNAL_HANDLER:
 *   op      = 신호 TokenType
 *   sval    = 신호 이름 문자열
 *   child[0]= 핸들러 블록
 */
static Node *parse_signal_handler(Parser *p) {
    Node *n = node_new(NODE_SIGNAL_HANDLER, p->previous.line, p->previous.col);

    /* 신호 이름 */
    if (is_signal_name(p->current.type) || is_name_token(p->current.type)
        || check(p, TOK_IDENT)) {
        char buf[256] = "";
        token_to_str(&p->current, buf, sizeof(buf));
        n->op   = p->current.type;
        n->sval = strdup(buf);
        parser_advance(p);
    } else {
        parser_error(p, "신호받기: 신호 이름이 필요합니다 (예: 중단신호)");
        n->sval = strdup("?");
    }

    consume(p, TOK_COLON, "신호받기 신호명 뒤에 ':' 가 필요합니다");
    node_add_child(n, parse_block(p));
    skip_newlines(p);
    consume(p, TOK_KW_KKUT_SINHOBATGI, "'신호받기끝' 이 필요합니다");
    consume_stmt_end(p);
    return n;
}

/*
 * parse_signal_ctrl() — 신호무시 / 신호기본 / 신호보내기
 *
 * 문법:
 *   신호무시   중단신호
 *   신호기본   중단신호
 *   신호보내기 현재프로세스 중단신호   ← pid 표현식 + 신호명
 *
 * NODE_SIGNAL_CTRL:
 *   op      = TOK_KW_SINHOMUSI | TOK_KW_SINHOGIBON | TOK_KW_SINHOBONEGI
 *   sval    = 신호 이름 문자열
 *   child[0]= PID 표현식 (신호보내기만, 선택)
 */
static Node *parse_signal_ctrl(Parser *p) {
    TokenType kind = p->previous.type;
    Node *n = node_new(NODE_SIGNAL_CTRL, p->previous.line, p->previous.col);
    n->op = kind;

    if (kind == TOK_KW_SINHOBONEGI) {
        /* 신호보내기: PID 표현식 먼저 */
        node_add_child(n, parse_expr(p));
    }

    /* 신호 이름 */
    if (is_signal_name(p->current.type) || is_name_token(p->current.type)
        || check(p, TOK_IDENT)) {
        char buf[256] = "";
        token_to_str(&p->current, buf, sizeof(buf));
        n->sval = strdup(buf);
        parser_advance(p);
    } else {
        parser_error(p, "신호 제어: 신호 이름이 필요합니다");
        n->sval = strdup("?");
    }

    consume_stmt_end(p);
    return n;
}

/*
 * parse_isr_handler() — 간섭 블록 (하드웨어 ISR)
 *
 * 문법:
 *   간섭 시간0넘침:
 *       ISR 코드
 *   간섭끝
 *
 * NODE_ISR_HANDLER:
 *   op      = 벡터 TokenType
 *   sval    = 벡터 이름 문자열
 *   child[0]= ISR 블록
 */
static Node *parse_isr_handler(Parser *p) {
    Node *n = node_new(NODE_ISR_HANDLER, p->previous.line, p->previous.col);

    /* 벡터 이름 */
    if (is_irq_name(p->current.type) || is_name_token(p->current.type)
        || check(p, TOK_IDENT)) {
        char buf[256] = "";
        token_to_str(&p->current, buf, sizeof(buf));
        n->op   = p->current.type;
        n->sval = strdup(buf);
        parser_advance(p);
    } else {
        parser_error(p, "간섭: 벡터 이름이 필요합니다 (예: 시간0넘침)");
        n->sval = strdup("?");
    }

    consume(p, TOK_COLON, "간섭 벡터명 뒤에 ':' 가 필요합니다");
    node_add_child(n, parse_block(p));
    skip_newlines(p);
    consume(p, TOK_KW_KKUT_GANSEOB, "'간섭끝' 이 필요합니다");
    consume_stmt_end(p);
    return n;
}

/*
 * parse_isr_ctrl() — 간섭잠금 / 간섭허용
 *
 * 문법:
 *   간섭잠금
 *   간섭허용
 *
 * NODE_ISR_CTRL:
 *   op = TOK_KW_GANSEOB_JAMGEUM | TOK_KW_GANSEOB_HEOYONG
 */
static Node *parse_isr_ctrl(Parser *p) {
    Node *n = node_new(NODE_ISR_CTRL, p->previous.line, p->previous.col);
    n->op = p->previous.type;
    consume_stmt_end(p);
    return n;
}

/*
 * parse_event_handler() — 행사등록 블록
 *
 * 문법:
 *   행사등록 "이벤트명" 처리:
 *       핸들러 코드
 *   행사등록끝
 *
 *   행사등록 "이벤트명" 처리(매개변수1, 매개변수2):
 *       핸들러 코드
 *   행사등록끝
 *
 * NODE_EVENT_HANDLER:
 *   sval           = 이벤트 이름
 *   child[0..n-2]  = NODE_PARAM (선택)
 *   child[last]    = 핸들러 블록
 */
static Node *parse_event_handler(Parser *p) {
    Node *n = node_new(NODE_EVENT_HANDLER, p->previous.line, p->previous.col);

    /* 이벤트 이름 (문자열 리터럴 또는 식별자) */
    if (check(p, TOK_STRING)) {
        char buf[256] = "";
        /* 따옴표 제거 */
        size_t len = p->current.length;
        if (len >= 2) {
            size_t copy = len - 2 < sizeof(buf) - 1 ? len - 2 : sizeof(buf) - 1;
            memcpy(buf, p->current.start + 1, copy);
            buf[copy] = '\0';
        }
        n->sval = strdup(buf);
        parser_advance(p);
    } else if (check(p, TOK_IDENT) || is_name_token(p->current.type)) {
        char buf[256] = "";
        token_to_str(&p->current, buf, sizeof(buf));
        n->sval = strdup(buf);
        parser_advance(p);
    } else {
        parser_error(p, "행사등록: 이벤트 이름이 필요합니다");
        n->sval = strdup("?");
    }

    /* '처리' 키워드 (선택 — 없어도 허용) */
    if (check(p, TOK_IDENT)) {
        char buf[64] = "";
        token_to_str(&p->current, buf, sizeof(buf));
        if (strcmp(buf, "처리") == 0) parser_advance(p);
    }

    /* 선택적 매개변수: 처리(매개변수, ...) */
    if (match(p, TOK_LPAREN)) {
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            if (check(p, TOK_IDENT) || is_name_token(p->current.type)) {
                Node *param = node_new(NODE_PARAM, p->current.line, p->current.col);
                char pbuf[256] = "";
                token_to_str(&p->current, pbuf, sizeof(pbuf));
                param->sval  = strdup(pbuf);
                param->dtype = TOK_EOF; /* 타입 미지정 = 동적 */
                parser_advance(p);
                node_add_child(n, param);
            }
            if (!match(p, TOK_COMMA)) break;
        }
        consume(p, TOK_RPAREN, "행사등록 매개변수 목록에 ')' 가 필요합니다");
    }

    consume(p, TOK_COLON, "행사등록 뒤에 ':' 가 필요합니다");
    node_add_child(n, parse_block(p));
    skip_newlines(p);
    consume(p, TOK_KW_KKUT_HAENGSA, "'행사등록끝' 이 필요합니다");
    consume_stmt_end(p);
    return n;
}

/*
 * parse_event_ctrl() — 행사시작 / 행사중단 / 행사발생 / 행사해제
 *
 * 문법:
 *   행사시작
 *   행사중단
 *   행사발생 "이벤트명"
 *   행사해제 "이벤트명"
 *
 * NODE_EVENT_CTRL:
 *   op      = 해당 TokenType
 *   child[0]= 이벤트 이름 표현식 (발생/해제만)
 */
static Node *parse_event_ctrl(Parser *p) {
    TokenType kind = p->previous.type;
    Node *n = node_new(NODE_EVENT_CTRL, p->previous.line, p->previous.col);
    n->op = kind;

    /* 이름 인수가 필요한 제어 구문 */
    if (kind == TOK_KW_HAENGSA_EMIT || kind == TOK_KW_HAENGSA_OFF) {
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF))
            node_add_child(n, parse_expr(p));
        else
            parser_error(p, "행사발생/행사해제: 이벤트 이름이 필요합니다");
    }

    consume_stmt_end(p);
    return n;
}

/* ================================================================
 *  스크립트 블록 파싱
 *
 *  형식:
 *    파이썬[(변수, ...)] [-> 반환변수]:
 *        ...Python 코드 원문...
 *    파이썬끝
 *
 *  sval   = 스크립트 원문 코드 (들여쓰기 제거 후)
 *  child[0..n-1] = 전달 변수 (NODE_IDENT)
 *  child[n]      = 반환 변수 (NODE_IDENT, '->' 뒤, 선택)
 * ================================================================ */
static Node *parse_script_block(Parser *p, NodeType ntype,
                                TokenType end_tok, const char *end_name) {
    Node *n = node_new(ntype, p->previous.line, p->previous.col);

    /* 선택적 인수: 파이썬(변수1, 변수2) */
    int arg_count = 0;
    if (match(p, TOK_LPAREN)) {
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            if (check(p, TOK_IDENT)) {
                Node *arg = node_new(NODE_IDENT, p->current.line, p->current.col);
                char tbuf[256] = "";
                token_to_str(&p->current, tbuf, sizeof(tbuf));
                arg->sval = strdup(tbuf);
                node_add_child(n, arg);
                arg_count++;
                parser_advance(p);
            }
            if (!match(p, TOK_COMMA)) break;
        }
        consume(p, TOK_RPAREN, "스크립트 블록 인수 목록에 ')' 가 필요합니다");
    }

    /* 선택적 반환 변수: => 변수명
       n->val.ival 에 반환 child 인덱스 저장 (-1 = 없음) */
    n->val.ival = -1;
    if (match(p, TOK_ARROW)) {
        if (check(p, TOK_IDENT)) {
            Node *ret = node_new(NODE_IDENT, p->current.line, p->current.col);
            char tbuf[256] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            ret->sval = strdup(tbuf);
            n->val.ival = (int64_t)n->child_count; /* 반환 child 인덱스 */
            node_add_child(n, ret);
            parser_advance(p);
        }
    }

    /* ':' 소비 */
    consume(p, TOK_COLON, "스크립트 블록 뒤에 ':' 가 필요합니다");

    /* consume(COLON) 후 p->current = NEWLINE 토큰
       NEWLINE의 start 포인터 다음 줄부터 raw read 시작 */
    {
        /* p->current 가 NEWLINE 이면 그 뒤부터 시작 */
        const char *s   = p->lexer->src;
        size_t      len = p->lexer->src_len;
        size_t      new_pos;

        if (p->current.start && p->current.type == TOK_NEWLINE) {
            /* NEWLINE 토큰 바로 다음 줄 */
            new_pos = (size_t)(p->current.start - s) + p->current.length;
        } else {
            /* 현재 렉서 pos 에서 줄 끝까지 건너뜀 */
            new_pos = p->lexer->pos;
            while (new_pos < len && s[new_pos] != '\n') new_pos++;
            if (new_pos < len) new_pos++;
        }
        p->lexer->pos = new_pos;
        /* 들여쓰기 스택 초기화 방지 — at_line_start 리셋 */
        p->lexer->at_line_start = 1;
    }

    /* 끝키워드까지 원문 raw read */
    n->sval = lexer_read_raw_script(p->lexer, end_name);

    /* 렉서가 끝키워드 앞으로 이동 — 다음 토큰 갱신 */
    p->current = lexer_next(p->lexer);

    /* DEDENT/NEWLINE 소비 */
    while (check(p, TOK_DEDENT) || check(p, TOK_NEWLINE))
        parser_advance(p);

    /* 끝키워드 소비 */
    if (!match(p, end_tok)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' 가 필요합니다", end_name);
        parser_error(p, msg);
    }
    consume_stmt_end(p);

    return n;
}

/* ================================================================
 *  표현식 파싱
 * ================================================================ */

static Node *parse_assignment(Parser *p);
static Node *parse_logic_or(Parser *p);
static Node *parse_logic_and(Parser *p);
static Node *parse_logic_not(Parser *p);
static Node *parse_comparison(Parser *p);
static Node *parse_bitwise(Parser *p);
static Node *parse_shift(Parser *p);
static Node *parse_addition(Parser *p);
static Node *parse_mult(Parser *p);
static Node *parse_power(Parser *p);
static Node *parse_unary(Parser *p);
static Node *parse_postfix(Parser *p);
static Node *parse_primary(Parser *p);

static Node *parse_expr(Parser *p) {
    return parse_assignment(p);
}

/* 대입 표현식 */
static Node *parse_assignment(Parser *p) {
    Node *left = parse_logic_or(p);

    if (is_assign_op(p->current.type)) {
        TokenType op = p->current.type;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_assignment(p); /* 우결합 */

        Node *n = node_new(NODE_ASSIGN, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        return n;
    }

    return left;
}

/* 논리 OR: 또는 */
static Node *parse_logic_or(Parser *p) {
    Node *left = parse_logic_and(p);

    while (check(p, TOK_KW_OR)) {
        TokenType op = p->current.type;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_logic_and(p);
        Node *n = node_new(NODE_BINARY, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        left = n;
    }
    return left;
}

/* 논리 AND: 그리고 */
static Node *parse_logic_and(Parser *p) {
    Node *left = parse_logic_not(p);

    while (check(p, TOK_KW_AND)) {
        TokenType op = p->current.type;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_logic_not(p);
        Node *n = node_new(NODE_BINARY, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        left = n;
    }
    return left;
}

/* 논리 NOT: 아니다 */
static Node *parse_logic_not(Parser *p) {
    if (check(p, TOK_KW_NOT)) {
        int line = p->current.line, col = p->current.col;
        TokenType op = p->current.type;
        parser_advance(p);
        Node *operand = parse_logic_not(p);
        Node *n = node_new(NODE_UNARY, line, col);
        n->op = op;
        node_add_child(n, operand);
        return n;
    }
    return parse_comparison(p);
}

/* 비교: == != > < >= <= */
static Node *parse_comparison(Parser *p) {
    Node *left = parse_bitwise(p);

    while (1) {
        TokenType op = p->current.type;
        if (op != TOK_EQEQ  && op != TOK_BANGEQ &&
            op != TOK_GT    && op != TOK_LT     &&
            op != TOK_GTEQ  && op != TOK_LTEQ)
            break;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_bitwise(p);
        Node *n = node_new(NODE_BINARY, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        left = n;
    }
    return left;
}

/* 비트 연산: & | ^ */
static Node *parse_bitwise(Parser *p) {
    Node *left = parse_shift(p);

    while (1) {
        TokenType op = p->current.type;
        if (op != TOK_AMP && op != TOK_PIPE && op != TOK_CARET) break;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_shift(p);
        Node *n = node_new(NODE_BINARY, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        left = n;
    }
    return left;
}

/* 시프트: << >> */
static Node *parse_shift(Parser *p) {
    Node *left = parse_addition(p);

    while (check(p, TOK_LTLT) || check(p, TOK_GTGT)) {
        TokenType op = p->current.type;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_addition(p);
        Node *n = node_new(NODE_BINARY, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        left = n;
    }
    return left;
}

/* 덧셈/뺄셈: + - */
static Node *parse_addition(Parser *p) {
    Node *left = parse_mult(p);

    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        TokenType op = p->current.type;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_mult(p);
        Node *n = node_new(NODE_BINARY, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        left = n;
    }
    return left;
}

/* 곱셈/나눗셈: * / % */
static Node *parse_mult(Parser *p) {
    Node *left = parse_power(p);

    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT)) {
        TokenType op = p->current.type;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_power(p);
        Node *n = node_new(NODE_BINARY, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        left = n;
    }
    return left;
}

/* 거듭제곱: ** (우결합) */
static Node *parse_power(Parser *p) {
    Node *left = parse_unary(p);

    if (check(p, TOK_STARSTAR)) {
        TokenType op = p->current.type;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *right = parse_power(p);  /* 우결합 */
        Node *n = node_new(NODE_BINARY, line, col);
        n->op = op;
        node_add_child(n, left);
        node_add_child(n, right);
        return n;
    }
    return left;
}

/* 단항 연산: - ~ */
static Node *parse_unary(Parser *p) {
    if (check(p, TOK_MINUS) || check(p, TOK_TILDE)) {
        TokenType op = p->current.type;
        int line = p->current.line, col = p->current.col;
        parser_advance(p);
        Node *operand = parse_unary(p);
        Node *n = node_new(NODE_UNARY, line, col);
        n->op = op;
        node_add_child(n, operand);
        return n;
    }
    return parse_postfix(p);
}

/* 후위 연산: 호출(), 인덱스[], 멤버. */
static Node *parse_postfix(Parser *p) {
    Node *left = parse_primary(p);

    while (1) {
        if (match(p, TOK_DOT)) {
            /* 멤버 접근: 객체.이름 */
            if (!check(p, TOK_IDENT)) {
                parser_error(p, "'.' 뒤에 멤버 이름이 필요합니다");
                break;
            }
            Node *n = node_new(NODE_MEMBER, p->current.line, p->current.col);
            node_set_sval_tok(n, &p->current);
            parser_advance(p);
            node_add_child(n, left);
            left = n;
        } else if (match(p, TOK_LBRACKET)) {
            /* 인덱스 접근: 배열[인덱스] */
            Node *n = node_new(NODE_INDEX, p->previous.line, p->previous.col);
            node_add_child(n, left);
            Node *idx = parse_expr(p);
            node_add_child(n, idx);
            consume(p, TOK_RBRACKET, "']' 가 필요합니다");
            left = n;
        } else if (match(p, TOK_LPAREN)) {
            /* 함수 호출: 함수(인수*) */
            Node *n = node_new(NODE_CALL, p->previous.line, p->previous.col);
            node_add_child(n, left);

            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                Node *arg = parse_expr(p);
                node_add_child(n, arg);
                if (!match(p, TOK_COMMA)) break;
            }
            consume(p, TOK_RPAREN, "')' 가 필요합니다");
            left = n;
        } else {
            break;
        }
    }
    return left;
}

/* 람다 파싱: (params) => expr */
static Node *parse_lambda(Parser *p, int line, int col) {
    Node *n = node_new(NODE_LAMBDA, line, col);

    /* 매개변수 목록 */
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        Node *param = node_new(NODE_PARAM, p->current.line, p->current.col);

        if (is_type_kw(p->current.type)) {
            param->dtype = p->current.type;
            parser_advance(p);
        }

        if (check(p, TOK_IDENT)) {
            node_set_sval_tok(param, &p->current);
            parser_advance(p);
        } else {
            parser_error(p, "매개변수 이름이 필요합니다");
            node_free(param);
            break;
        }
        node_add_child(n, param);
        if (!match(p, TOK_COMMA)) break;
    }

    consume(p, TOK_RPAREN, "람다 ')' 가 필요합니다");
    consume(p, TOK_ARROW,  "람다 '=>' 가 필요합니다");

    Node *body = parse_expr(p);
    node_add_child(n, body);
    return n;
}

/* 기본 표현식 */
static Node *parse_primary(Parser *p) {
    int line = p->current.line, col = p->current.col;

    /* 정수 리터럴 */
    if (check(p, TOK_INT) || check(p, TOK_BIN) ||
        check(p, TOK_OCT) || check(p, TOK_HEX)) {
        Node *n = node_new(NODE_INT_LIT, line, col);
        n->val.ival = p->current.val.ival;
        parser_advance(p);
        return n;
    }

    /* 실수 리터럴 */
    if (check(p, TOK_FLOAT)) {
        Node *n = node_new(NODE_FLOAT_LIT, line, col);
        n->val.fval = p->current.val.fval;
        parser_advance(p);
        return n;
    }

    /* 문자열 리터럴 */
    if (check(p, TOK_STRING)) {
        Node *n = node_new(NODE_STRING_LIT, line, col);
        /* 따옴표 제거해서 내용만 저장 */
        size_t raw_len = p->current.length;
        const char *raw = p->current.start;
        if (raw_len >= 2) {
            n->sval = (char *)malloc(raw_len - 1);
            memcpy(n->sval, raw + 1, raw_len - 2);
            n->sval[raw_len - 2] = '\0';
        } else {
            node_set_sval_str(n, "");
        }
        parser_advance(p);
        return n;
    }

    /* 글자 리터럴 */
    if (check(p, TOK_CHAR_LIT)) {
        Node *n = node_new(NODE_CHAR_LIT, line, col);
        n->val.ival = (int64_t)p->current.val.cval;
        parser_advance(p);
        return n;
    }

    /* 참 / 거짓 */
    if (check(p, TOK_TRUE)) {
        Node *n = node_new(NODE_BOOL_LIT, line, col);
        n->val.bval = 1;
        parser_advance(p);
        return n;
    }
    if (check(p, TOK_FALSE)) {
        Node *n = node_new(NODE_BOOL_LIT, line, col);
        n->val.bval = 0;
        parser_advance(p);
        return n;
    }

    /* 없음 */
    if (check(p, TOK_NULL) || check(p, TOK_KW_EOPSEUM)) {
        Node *n = node_new(NODE_NULL_LIT, line, col);
        parser_advance(p);
        return n;
    }

    /* 식별자 */
    if (check(p, TOK_IDENT)) {
        Node *n = node_new(NODE_IDENT, line, col);
        node_set_sval_tok(n, &p->current);
        parser_advance(p);
        return n;
    }

    /* 내장 호출 가능 키워드: 출력, 입력, 오류 등 */
    if (is_callable_kw(p->current.type)) {
        Node *n = node_new(NODE_IDENT, line, col);
        node_set_sval_tok(n, &p->current);
        parser_advance(p);
        return n;
    }

    /* 자신 / 부모 */
    if (check(p, TOK_KW_JASIN) || check(p, TOK_KW_BUMO)) {
        Node *n = node_new(NODE_IDENT, line, col);
        node_set_sval_tok(n, &p->current);
        parser_advance(p);
        return n;
    }

    /* '(' ... ')' — 그룹 또는 람다 */
    if (match(p, TOK_LPAREN)) {
        /* 람다인지 판단: '(' 다음이 ')' 이거나, '(자료형? IDENT,' 또는 '(자료형? IDENT)' 뒤에 '=>' */
        /* 간단한 휴리스틱: 일단 표현식으로 파싱 시도, 
           ')' 다음에 '=>' 가 오면 람다 */

        /* 빈 매개변수 람다: () => */
        if (check(p, TOK_RPAREN)) {
            parser_advance(p);
            if (check(p, TOK_ARROW)) {
                /* () => expr */
                parser_advance(p);
                Node *n = node_new(NODE_LAMBDA, line, col);
                Node *body = parse_expr(p);
                node_add_child(n, body);
                return n;
            }
            /* () — 그냥 빈 괄호? 오류 */
            parser_error(p, "빈 괄호 표현식은 허용되지 않습니다");
            return node_new(NODE_NULL_LIT, line, col);
        }

        /* 매개변수가 있는 람다: (자료형? IDENT, ...) => expr */
        /* 저장해 두고 => 여부 확인 */
        Lexer  saved_lx  = *p->lexer;
        Token  saved_cur = p->current;
        Token  saved_prev= p->previous;

        /* 람다 시도 파싱 */
        int might_be_lambda = 0;
        if (is_type_kw(p->current.type) || check(p, TOK_IDENT)) {
            /* 식별자/자료형 소비 */
            if (is_type_kw(p->current.type)) parser_advance(p);
            if (check(p, TOK_IDENT)) {
                parser_advance(p);
                if (check(p, TOK_COMMA) || check(p, TOK_RPAREN)) {
                    /* (자료형? IDENT, 또는 (자료형? IDENT) */
                    /* ')' 까지 스킵하고 => 확인 */
                    int depth = 1;
                    while (!check(p, TOK_EOF) && depth > 0) {
                        if (check(p, TOK_LPAREN)) depth++;
                        else if (check(p, TOK_RPAREN)) depth--;
                        if (depth > 0) parser_advance(p);
                    }
                    if (check(p, TOK_RPAREN)) {
                        parser_advance(p);
                        if (check(p, TOK_ARROW)) might_be_lambda = 1;
                    }
                }
            }
        }

        /* 상태 복원 */
        *p->lexer  = saved_lx;
        p->current = saved_cur;
        p->previous= saved_prev;

        if (might_be_lambda) {
            return parse_lambda(p, line, col);
        }

        /* 그룹 표현식 */
        Node *inner = parse_expr(p);
        consume(p, TOK_RPAREN, "')' 가 필요합니다");
        return inner;
    }

    /* 배열 리터럴: [값, 값, ...] */
    if (match(p, TOK_LBRACKET)) {
        Node *n = node_new(NODE_ARRAY_LIT, line, col);
        while (!check(p, TOK_RBRACKET) && !check(p, TOK_EOF)) {
            node_add_child(n, parse_expr(p));
            if (!match(p, TOK_COMMA)) break;
        }
        consume(p, TOK_RBRACKET, "']' 가 필요합니다");
        return n;
    }

    /* 사전 리터럴: {키: 값, ...} */
    if (match(p, TOK_LBRACE)) {
        Node *n = node_new(NODE_DICT_LIT, line, col);
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            Node *entry = node_new(NODE_DICT_ENTRY, p->current.line, p->current.col);
            node_add_child(entry, parse_expr(p));
            consume(p, TOK_COLON, "사전 키 뒤에 ':' 가 필요합니다");
            node_add_child(entry, parse_expr(p));
            node_add_child(n, entry);
            if (!match(p, TOK_COMMA)) break;
        }
        consume(p, TOK_RBRACE, "'}' 가 필요합니다");
        return n;
    }

    parser_error(p, "표현식을 파싱할 수 없습니다");
    parser_advance(p);
    return node_new(NODE_NULL_LIT, line, col);
}

/* ================================================================
 *  구문 디스패처
 * ================================================================ */
static Node *parse_stmt(Parser *p) {
    skip_newlines(p);

    int line = p->current.line, col = p->current.col;

    /* 가져오기 */
    if (match(p, TOK_KW_GAJIM))
        return parse_import(p);

    /* 함수 선언 */
    if (match(p, TOK_KW_HAMSU))
        return parse_func_decl(p, NODE_FUNC_DECL);

    /* 정의 (반환값 없는 함수) */
    if (match(p, TOK_KW_JEONGUI))
        return parse_func_decl(p, NODE_VOID_DECL);

    /* 객체(클래스) */
    if (match(p, TOK_KW_GAEGCHE))
        return parse_class_decl(p);

    /* 만약 */
    if (match(p, TOK_KW_MANYAK))
        return parse_if(p);

    /* 동안 */
    if (match(p, TOK_KW_DONGAN))
        return parse_while(p);

    /* 반복 */
    if (match(p, TOK_KW_BANBOG))
        return parse_for_range(p);

    /* 각각 */
    if (match(p, TOK_KW_GAKGAK))
        return parse_for_each(p);

    /* 선택 */
    if (match(p, TOK_KW_SEONTAEK))
        return parse_switch(p);

    /* 시도 */
    if (match(p, TOK_KW_SIDO))
        return parse_try(p);

    /* 반환 */
    if (match(p, TOK_KW_BANHWAN)) {
        Node *n = node_new(NODE_RETURN, line, col);
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF)) {
            node_add_child(n, parse_expr(p));
        }
        consume_stmt_end(p);
        return n;
    }

    /* 끝냄 */
    if (match(p, TOK_KW_KKEUTNAEM)) {
        Node *n = node_new(NODE_RETURN, line, col);
        consume_stmt_end(p);
        return n;
    }

    /* 멈춤 */
    if (match(p, TOK_KW_MEOMCHUM)) {
        Node *n = node_new(NODE_BREAK, line, col);
        consume_stmt_end(p);
        return n;
    }

    /* 건너뜀 */
    if (match(p, TOK_KW_GEONNEO)) {
        Node *n = node_new(NODE_CONTINUE, line, col);
        consume_stmt_end(p);
        return n;
    }

    /* 이동 (goto) */
    if (match(p, TOK_KW_IDONG)) {
        Node *n = node_new(NODE_GOTO, line, col);
        if (check(p, TOK_IDENT)) {
            node_set_sval_tok(n, &p->current);
            parser_advance(p);
        } else {
            parser_error(p, "이동 대상 이름이 필요합니다");
        }
        consume_stmt_end(p);
        return n;
    }

    /* 오류(raise) — 키워드로 시작하는 경우 */
    if (match(p, TOK_KW_ORU)) {
        Node *n = node_new(NODE_RAISE, line, col);
        if (match(p, TOK_LPAREN)) {
            node_add_child(n, parse_expr(p));
            consume(p, TOK_RPAREN, "오류() 닫는 ')' 가 필요합니다");
        }
        consume_stmt_end(p);
        return n;
    }

    /* 고정 (상수 선언) */
    if (match(p, TOK_KW_GOJUNG)) {
        return parse_var_decl(p, 1);
    }

    /* 법령 / 법위반 (블록 방식 v4.2.0) */
    if (match(p, TOK_KW_BEOPRYEONG) || match(p, TOK_KW_BEOPWIBAN)) {
        return parse_contract(p);
    }

    /* 헌법 — 전역 최상위 계약 (단일 라인) 또는 MCP서버 블록 */
    if (match(p, TOK_KW_HEONBEOB)) {
        /* 헌법 MCP서버 "이름": 블록 형태 감지 */
        if (check(p, TOK_KW_MCP_SERVER)) {
            parser_advance(p); /* MCP서버 소비 */
            return parse_mcp_server(p);
        }
        return parse_constitution(p);
    }

    /* 법률 — 현재 파일 계약 (단일 라인) 또는 MCP 핸들러 블록 */
    if (match(p, TOK_KW_BEOMNYUL)) {
        if (check(p, TOK_KW_MCP_TOOL)) {
            parser_advance(p);
            return parse_mcp_handler(p, NODE_MCP_TOOL, TOK_KW_KKUT_MCP_TOOL);
        }
        if (check(p, TOK_KW_MCP_RESOURCE)) {
            parser_advance(p);
            return parse_mcp_handler(p, NODE_MCP_RESOURCE, TOK_KW_KKUT_MCP_RESOURCE);
        }
        if (check(p, TOK_KW_MCP_PROMPT)) {
            parser_advance(p);
            return parse_mcp_handler(p, NODE_MCP_PROMPT, TOK_KW_KKUT_MCP_PROMPT);
        }
        return parse_statute(p);
    }

    /* 규정 — 객체 범위 계약 (블록) */
    if (match(p, TOK_KW_GYUJEONG)) {
        return parse_regulation(p);
    }

    /* 복원지점 (회귀 제재의 기준점) */
    if (match(p, TOK_KW_BOKWON)) {
        return parse_checkpoint(p);
    }

    /* 가속기: ... 가속기끝 */
    if (match(p, TOK_KW_GPU_RO)) {
        return parse_gpu_block(p);
    }

    /* ── 인터럽트 시스템 (v6.0.0) ──────────────────────────── */

    /* A: OS 시그널 */
    if (match(p, TOK_KW_SINHOBATGI))
        return parse_signal_handler(p);
    if (match(p, TOK_KW_SINHOMUSI)  ||
        match(p, TOK_KW_SINHOGIBON) ||
        match(p, TOK_KW_SINHOBONEGI))
        return parse_signal_ctrl(p);

    /* B: 하드웨어 간섭 */
    if (match(p, TOK_KW_GANSEOB))
        return parse_isr_handler(p);
    if (match(p, TOK_KW_GANSEOB_JAMGEUM) ||
        match(p, TOK_KW_GANSEOB_HEOYONG))
        return parse_isr_ctrl(p);

    /* C: 행사(이벤트 루프) */
    if (match(p, TOK_KW_HAENGSA_REG))
        return parse_event_handler(p);
    if (match(p, TOK_KW_HAENGSA_START) ||
        match(p, TOK_KW_HAENGSA_STOP)  ||
        match(p, TOK_KW_HAENGSA_EMIT)  ||
        match(p, TOK_KW_HAENGSA_OFF))
        return parse_event_ctrl(p);

    /* ── 스크립트 블록 ─────────────────────────────────── */
    if (match(p, TOK_KW_PYTHON)) {
        return parse_script_block(p, NODE_SCRIPT_PYTHON,
                                  TOK_KW_END_PYTHON, "파이썬끝");
    }
    if (match(p, TOK_KW_JAVA)) {
        return parse_script_block(p, NODE_SCRIPT_JAVA,
                                  TOK_KW_END_JAVA, "자바끝");
    }
    if (match(p, TOK_KW_JS)) {
        return parse_script_block(p, NODE_SCRIPT_JS,
                                  TOK_KW_END_JS, "자바스크립트끝");
    }

    /* ── 산업/임베디드 블록 (v16.0.0) ─────────────────── */
    if (match(p, TOK_KW_TIMER_BLOCK)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *n = node_new(NODE_TIMER_BLOCK, ln, cl);
        char period[64] = "";
        if (check(p, TOK_INT) || check(p, TOK_FLOAT) || check(p, TOK_IDENT)) {
            char tbuf[64] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            strncpy(period, tbuf, sizeof(period) - 1);
            parser_advance(p);
            if (check(p, TOK_IDENT)) {
                char unit[16] = "";
                token_to_str(&p->current, unit, sizeof(unit));
                strncat(period, unit, sizeof(period) - strlen(period) - 1);
                parser_advance(p);
            }
        }
        n->sval = strdup(period[0] ? period : "100ms");
        consume(p, TOK_COLON, "타이머 주기 뒤에 ':' 가 필요합니다");
        node_add_child(n, parse_block(p));
        if (check(p, TOK_KW_TIMER_KKUT)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return n;
    }
    if (match(p, TOK_KW_ROS2_NODE)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *n = node_new(NODE_ROS2_BLOCK, ln, cl);
        if (check(p, TOK_STRING)) {
            char tbuf[256] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            size_t tlen = strlen(tbuf);
            if (tlen >= 2 && tbuf[0] == '"') { tbuf[tlen-1] = '\0'; n->sval = strdup(tbuf+1); }
            else n->sval = strdup(tbuf);
            parser_advance(p);
        } else { n->sval = strdup("ros2_node"); }
        consume(p, TOK_COLON, "ROS2노드 이름 뒤에 ':' 가 필요합니다");
        node_add_child(n, parse_block(p));
        if (check(p, TOK_KW_ROS2_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return n;
    }

    /* ── 안전 규격 블록 (v17.0.0) ──────────────────────── */
    if (match(p, TOK_KW_WATCHDOG)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *wn = node_new(NODE_WATCHDOG_BLOCK, ln, cl);
        char period[64] = "";
        if (check(p, TOK_INT) || check(p, TOK_FLOAT) || check(p, TOK_IDENT)) {
            char tbuf[64] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            strncpy(period, tbuf, sizeof(period) - 1);
            parser_advance(p);
            if (check(p, TOK_IDENT)) {
                char unit[16] = "";
                token_to_str(&p->current, unit, sizeof(unit));
                strncat(period, unit, sizeof(period) - strlen(period) - 1);
                parser_advance(p);
            }
        }
        wn->sval = strdup(period[0] ? period : "1000ms");
        wn->val.ival = (int64_t)atoi(period);
        if (wn->val.ival <= 0) wn->val.ival = 1000;
        consume(p, TOK_COLON, "워치독 타임아웃 뒤에 ':' 가 필요합니다");
        node_add_child(wn, parse_block(p));
        if (check(p, TOK_KW_WATCHDOG_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return wn;
    }
    if (match(p, TOK_KW_FAULT_TOL)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *fn2 = node_new(NODE_FAULT_TOL_BLOCK, ln, cl);
        int64_t n_copies = 2;
        if (check(p, TOK_INT)) {
            char tbuf[32] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            n_copies = (int64_t)atoi(tbuf);
            parser_advance(p);
        }
        if (check(p, TOK_IDENT)) parser_advance(p); /* '중' 소비 */
        fn2->val.ival = n_copies;
        consume(p, TOK_COLON, "결함허용 N중 뒤에 ':' 가 필요합니다");
        for (int64_t ci = 0; ci < n_copies; ci++) {
            node_add_child(fn2, parse_block(p));
            if (ci < n_copies - 1) match(p, TOK_NEWLINE);
        }
        if (check(p, TOK_KW_FAULT_TOL_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return fn2;
    }

    /* ── 온디바이스 AI 블록 (v18.0.0) ─────────────────── */
    if (match(p, TOK_KW_AI_MODEL)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *an = node_new(NODE_AI_MODEL_BLOCK, ln, cl);
        an->val.ival = 0; /* 0=ONNX 기본 */
        if (check(p, TOK_STRING)) {
            char tbuf[256] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            size_t tlen = strlen(tbuf);
            if (tlen >= 2 && tbuf[0] == '"') { tbuf[tlen-1] = '\0'; an->sval = strdup(tbuf+1); }
            else an->sval = strdup(tbuf);
            parser_advance(p);
        } else { an->sval = strdup("ai_model"); }
        consume(p, TOK_COLON, "AI모델 이름 뒤에 ':' 가 필요합니다");
        node_add_child(an, parse_block(p));
        if (check(p, TOK_KW_AI_MODEL_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return an;
    }
    if (match(p, TOK_KW_TINYML)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *tn = node_new(NODE_TINYML_BLOCK, ln, cl);
        if (check(p, TOK_STRING)) {
            char tbuf[256] = "";
            token_to_str(&p->current, tbuf, sizeof(tbuf));
            size_t tlen = strlen(tbuf);
            if (tlen >= 2 && tbuf[0] == '"') { tbuf[tlen-1] = '\0'; tn->sval = strdup(tbuf+1); }
            else tn->sval = strdup(tbuf);
            parser_advance(p);
        } else { tn->sval = strdup("tinyml_model"); }
        consume(p, TOK_COLON, "TinyML 이름 뒤에 ':' 가 필요합니다");
        node_add_child(tn, parse_block(p));
        if (check(p, TOK_KW_TINYML_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return tn;
    }
    if (match(p, TOK_KW_FEDERATED)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *fn3 = node_new(NODE_FEDERATED_BLOCK, ln, cl);
        if (check(p, TOK_COLON)) parser_advance(p);
        else match(p, TOK_NEWLINE);
        node_add_child(fn3, parse_block(p));
        if (check(p, TOK_KW_FEDERATED_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return fn3;
    }

    /* ── 온톨로지 블록 (v18.5.0 4회차) ─────────────────────────────── */
    if (match(p, TOK_KW_ONTOLOGY)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *on = node_new(NODE_ONT_BLOCK, ln, cl);
        on->val.ival = 0; /* 기본: 내장 */

        /* 모드 문자열 파싱: "내장" | "대여" | "접속" */
        if (check(p, TOK_STRING)) {
            char mbuf[256] = "";
            token_to_str(&p->current, mbuf, sizeof(mbuf));
            /* 따옴표 제거 */
            char *ms = mbuf;
            if (*ms == '"') { ms++; size_t ml = strlen(ms); if (ml > 0 && ms[ml-1]=='"') ms[ml-1]=0; }
            if (strncmp(ms, "ë´ì¥", 6) == 0)       on->val.ival = 0; /* 내장 */
            else if (strncmp(ms, "ëì¬", 6) == 0)  on->val.ival = 1; /* 대여 */
            else if (strncmp(ms, "ì ì", 6) == 0)  on->val.ival = 2; /* 접속 */
            on->sval = strdup(ms);
            parser_advance(p);
        } else {
            on->sval = strdup("\xEB\x82\xB4\xEC\x9E\xA5"); /* 내장 */
        }

        /* 모드 3: URL 문자열 파싱 */
        if (on->val.ival == 2 && check(p, TOK_STRING)) {
            char ubuf[256] = "";
            token_to_str(&p->current, ubuf, sizeof(ubuf));
            char *us = ubuf;
            if (*us == '"') { us++; size_t ul = strlen(us); if (ul > 0 && us[ul-1]=='"') us[ul-1]=0; }
            /* URL을 첫 번째 child로 저장 (NODE_IDENT 재활용) */
            Node *url_n = node_new(NODE_IDENT, p->current.line, p->current.col);
            url_n->sval = strdup(us);
            node_add_child(on, url_n);
            parser_advance(p);
        }

        consume(p, TOK_COLON, "온톨로지 선언 뒤에 ':' 가 필요합니다");
        node_add_child(on, parse_block(p));
        if (check(p, TOK_KW_ONTOLOGY_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return on;
    }
    /* 개념 정의 블록 */
    if (match(p, TOK_KW_ONT_CONCEPT)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *cn = node_new(NODE_ONT_CONCEPT, ln, cl);

        /* 개념 이름 */
        if (check(p, TOK_STRING) || check(p, TOK_IDENT)) {
            char nbuf[256] = "";
            token_to_str(&p->current, nbuf, sizeof(nbuf));
            char *ns = nbuf;
            if (*ns == '"') { ns++; size_t nl=strlen(ns); if(nl>0&&ns[nl-1]=='"') ns[nl-1]=0; }
            cn->sval = strdup(ns);
            parser_advance(p);
        } else { cn->sval = strdup("미명"); }

        /* 이어받기 부모 개념 (선택) */
        if (match(p, TOK_KW_IEOBATGI)) { /* 이어받기 */
            if (check(p, TOK_STRING) || check(p, TOK_IDENT)) {
                Node *par = node_new(NODE_IDENT, p->current.line, p->current.col);
                char pb[256]="";
                token_to_str(&p->current, pb, sizeof(pb));
                char *ps=pb; if(*ps=='"'){ps++;size_t pl=strlen(ps);if(pl>0&&ps[pl-1]=='"')ps[pl-1]=0;}
                par->sval = strdup(ps);
                node_add_child(cn, par);
                parser_advance(p);
            }
        } else {
            node_add_child(cn, NULL); /* 부모 없음 슬롯 */
        }

        consume(p, TOK_COLON, "개념 이름 뒤에 ':' 가 필요합니다");
        node_add_child(cn, parse_block(p));
        if (check(p, TOK_KW_ONT_CONCEPT_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return cn;
    }
    /* 속성 선언 */
    if (match(p, TOK_KW_ONT_PROP)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *pn = node_new(NODE_ONT_PROP, ln, cl);

        /* 속성 이름 */
        if (check(p, TOK_STRING) || check(p, TOK_IDENT)) {
            char abuf[256]="";
            token_to_str(&p->current, abuf, sizeof(abuf));
            char *as=abuf; if(*as=='"'){as++;size_t al=strlen(as);if(al>0&&as[al-1]=='"')as[al-1]=0;}
            pn->sval = strdup(as);
            parser_advance(p);
        } else { pn->sval = strdup("속성"); }

        /* 타입 키워드 */
        if (is_type_kw(p->current.type)) {
            pn->val.ival = (int64_t)p->current.type;
            parser_advance(p);
        } else { pn->val.ival = (int64_t)TOK_KW_MUNJA; } /* 글자 기본 */

        /* 민감 / 익명화 수식어 */
        if (check(p, TOK_KW_ONT_SENSITIVE)) { pn->op = TOK_KW_ONT_SENSITIVE; parser_advance(p); }
        else if (check(p, TOK_KW_ONT_ANON)) { pn->op = TOK_KW_ONT_ANON; parser_advance(p); }
        consume_stmt_end(p);
        return pn;
    }
    /* 관계 정의 */
    if (match(p, TOK_KW_ONT_RELATE)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *rn = node_new(NODE_ONT_RELATE, ln, cl);

        /* 관계 이름 */
        if (check(p, TOK_STRING) || check(p, TOK_IDENT)) {
            char rbuf[256]="";
            token_to_str(&p->current, rbuf, sizeof(rbuf));
            char *rs=rbuf; if(*rs=='"'){rs++;size_t rl=strlen(rs);if(rl>0&&rs[rl-1]=='"')rs[rl-1]=0;}
            rn->sval = strdup(rs);
            parser_advance(p);
        } else { rn->sval = strdup("관계"); }

        /* 에서(from) 개념 */
        /* 선택적 "에서" 키워드 건너뜀 */
        if (check(p, TOK_STRING) || check(p, TOK_IDENT)) {
            Node *from_n = node_new(NODE_IDENT, p->current.line, p->current.col);
            char fb[256]="";
            token_to_str(&p->current, fb, sizeof(fb));
            char *fs=fb; if(*fs=='"'){fs++;size_t fl=strlen(fs);if(fl>0&&fs[fl-1]=='"')fs[fl-1]=0;}
            from_n->sval = strdup(fs);
            node_add_child(rn, from_n);
            parser_advance(p);
        }
        /* 으로(to) 개념 */
        if (check(p, TOK_STRING) || check(p, TOK_IDENT)) {
            Node *to_n = node_new(NODE_IDENT, p->current.line, p->current.col);
            char tb2[256]="";
            token_to_str(&p->current, tb2, sizeof(tb2));
            char *ts=tb2; if(*ts=='"'){ts++;size_t tl=strlen(ts);if(tl>0&&ts[tl-1]=='"')ts[tl-1]=0;}
            to_n->sval = strdup(ts);
            node_add_child(rn, to_n);
            parser_advance(p);
        }
        consume_stmt_end(p);
        return rn;
    }
    /* 질의 실행 */
    if (match(p, TOK_KW_ONT_QUERY_BLOCK)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *qn = node_new(NODE_ONT_QUERY, ln, cl);

        if (check(p, TOK_STRING)) {
            char qbuf[512]="";
            token_to_str(&p->current, qbuf, sizeof(qbuf));
            char *qs=qbuf; if(*qs=='"'){qs++;size_t ql=strlen(qs);if(ql>0&&qs[ql-1]=='"')qs[ql-1]=0;}
            qn->sval = strdup(qs);
            parser_advance(p);
        } else { qn->sval = strdup(""); }

        /* => 결과변수 */
        if (match(p, TOK_ARROW)) {
            if (check(p, TOK_IDENT)) {
                Node *res_n = node_new(NODE_IDENT, p->current.line, p->current.col);
                node_set_sval_tok(res_n, &p->current);
                node_add_child(qn, res_n);
                parser_advance(p);
            }
        }
        consume_stmt_end(p);
        return qn;
    }
    /* 추론 실행 */
    if (match(p, TOK_KW_ONT_INFER_BLOCK)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *in2 = node_new(NODE_ONT_INFER, ln, cl);
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(in2, parse_block(p));
        } else { consume_stmt_end(p); }
        return in2;
    }

    /* ── Concept Identity / Vector Space (v22.0.0) ────────────────────── */

    /* 의미추론 블록 */
    if (match(p, TOK_KW_SEMANTIC_INFER)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *sn = node_new(NODE_SEMANTIC_INFER, ln, cl);
        /* 선택적 이름 문자열 */
        if (check(p, TOK_STRING)) {
            node_set_sval_tok(sn, &p->current);
            parser_advance(p);
        }
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(sn, parse_block(p));
        } else { consume_stmt_end(p); }
        if (check(p, TOK_KW_SEMANTIC_INFER_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return sn;
    }

    /* 벡터화 블록 */
    if (match(p, TOK_KW_VECTORIZE)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *vn = node_new(NODE_VECTORIZE, ln, cl);
        if (check(p, TOK_STRING)) {
            node_set_sval_tok(vn, &p->current);
            parser_advance(p);
        }
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(vn, parse_block(p));
        } else { consume_stmt_end(p); }
        if (check(p, TOK_KW_VECTORIZE_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return vn;
    }

    /* 의미복원 블록 */
    if (match(p, TOK_KW_SEM_RECON)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *rn2 = node_new(NODE_SEM_RECON, ln, cl);
        if (check(p, TOK_STRING)) {
            node_set_sval_tok(rn2, &p->current);
            parser_advance(p);
        }
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(rn2, parse_block(p));
        } else { consume_stmt_end(p); }
        if (check(p, TOK_KW_SEM_RECON_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return rn2;
    }

    /* 재생산라벨 블록 */
    if (match(p, TOK_KW_REPRO_LABEL)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *rl = node_new(NODE_REPRO_LABEL, ln, cl);
        if (check(p, TOK_STRING)) {
            node_set_sval_tok(rl, &p->current);
            parser_advance(p);
        }
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(rl, parse_block(p));
        } else { consume_stmt_end(p); }
        if (check(p, TOK_KW_REPRO_LABEL_END)) { parser_advance(p); match(p, TOK_NEWLINE); }
        return rl;
    }

    /* ── 지식 뱅크 파싱 (Stage 24) ── */

    /* 지식뱅크 블록 */
    if (match(p, TOK_KW_KBANK)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *kb = node_new(NODE_KBANK, ln, cl);
        if (check(p, TOK_STRING) || check(p, TOK_IDENT)) {
            node_set_sval_tok(kb, &p->current);
            parser_advance(p);
        }
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(kb, parse_block(p));
        } else { consume_stmt_end(p); }
        return kb;
    }

    /* 지식불러오기 블록 */
    if (match(p, TOK_KW_KBANK_LOAD)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *kl = node_new(NODE_KBANK_LOAD, ln, cl);
        if (check(p, TOK_STRING)) {
            node_set_sval_tok(kl, &p->current);
            parser_advance(p);
        }
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(kl, parse_block(p));
        } else { consume_stmt_end(p); }
        return kl;
    }

    /* 지식비교 블록 */
    if (match(p, TOK_KW_KBANK_COMPARE)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *kc = node_new(NODE_KBANK_COMPARE, ln, cl);
        if (check(p, TOK_STRING) || check(p, TOK_IDENT)) {
            node_add_child(kc, parse_expr(p));
        }
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(kc, parse_block(p));
        } else { consume_stmt_end(p); }
        return kc;
    }

    /* 재생산라벨선언 (인라인) */
    if (match(p, TOK_KW_REPRO_LABEL_DECL)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *rld = node_new(NODE_REPRO_LABEL_DECL, ln, cl);
        if (check(p, TOK_STRING)) {
            node_set_sval_tok(rld, &p->current);
            parser_advance(p);
        }
        consume_stmt_end(p);
        return rld;
    }

    /* 지식증거출력 블록 */
    if (match(p, TOK_KW_KBANK_PROOF)) {
        int ln = p->previous.line, cl = p->previous.col;
        Node *kp = node_new(NODE_KBANK_PROOF, ln, cl);
        if (check(p, TOK_STRING)) {
            node_set_sval_tok(kp, &p->current);
            parser_advance(p);
        }
        if (check(p, TOK_COLON)) {
            parser_advance(p);
            node_add_child(kp, parse_block(p));
        } else { consume_stmt_end(p); }
        return kp;
    }

    if (check(p, TOK_PP_DEFINE) || check(p, TOK_PP_IF)    ||
        check(p, TOK_PP_ELSE)   || check(p, TOK_PP_ENDIF) ||
        check(p, TOK_PP_IFDEF)  || check(p, TOK_PP_IFNDEF)||
        check(p, TOK_PP_INCLUDE)|| check(p, TOK_PP_GPU)) {
        TokenType pp = p->current.type;
        parser_advance(p);
        return parse_pp_stmt(p, pp);
    }

    /* 자료형 키워드로 시작 → 변수 선언 */
    if (is_type_kw(p->current.type)) {
        /* IDENT 다음에 IDENT가 오면 변수 선언 */
        Lexer saved_lx   = *p->lexer;
        Token saved_cur  = p->current;
        Token saved_prev = p->previous;

        TokenType dtype = p->current.type;
        parser_advance(p);

        if (check(p, TOK_IDENT)) {
            /* 변수 선언: 자료형 이름 [= 값] */
            Node *n = node_new(NODE_VAR_DECL, line, col);
            n->dtype = dtype;
            node_set_sval_tok(n, &p->current);
            parser_advance(p);
            if (match(p, TOK_EQ)) {
                node_add_child(n, parse_expr(p));
            }
            consume_stmt_end(p);
            return n;
        }

        /* 아니면 복원 후 표현식 처리 */
        *p->lexer  = saved_lx;
        p->current = saved_cur;
        p->previous= saved_prev;
    }

    /* IDENT ':' NEWLINE → 레이블 선언 */
    if (check(p, TOK_IDENT)) {
        Lexer saved_lx   = *p->lexer;
        Token saved_cur  = p->current;
        Token saved_prev = p->previous;

        Token ident = p->current;
        parser_advance(p);

        if (check(p, TOK_COLON)) {
            /* 다음 다음이 NEWLINE 이면 레이블 */
            Lexer save2_lx  = *p->lexer;
            Token save2_cur = p->current;
            parser_advance(p);
            if (check(p, TOK_NEWLINE) || check(p, TOK_EOF)) {
                Node *n = node_new(NODE_LABEL, ident.line, ident.col);
                node_set_sval_tok(n, &ident);
                consume_stmt_end(p);
                return n;
            }
            /* 콜론이 있지만 레이블이 아님 — 복원 */
            *p->lexer  = save2_lx;
            p->current = save2_cur;
        }

        /* 복원 후 표현식 */
        *p->lexer  = saved_lx;
        p->current = saved_cur;
        p->previous= saved_prev;
    }

    /* 표현식 구문 (함수 호출, 대입 등) */
    {
        Node *expr = parse_expr(p);
        Node *stmt = node_new(NODE_EXPR_STMT, line, col);
        node_add_child(stmt, expr);
        consume_stmt_end(p);
        return stmt;
    }
}

/* ================================================================
 *  공개 API
 * ================================================================ */

void parser_init(Parser *p, Lexer *lx) {
    memset(p, 0, sizeof(Parser));
    p->lexer = lx;
    /* 첫 토큰 로드 */
    p->current  = lexer_next(lx);
    p->had_error = 0;
    p->panic_mode = 0;
    p->error_count = 0;
}

Node *parser_parse(Parser *p) {
    Node *program = node_new(NODE_PROGRAM, 1, 1);

    skip_newlines(p);

    while (!check(p, TOK_EOF)) {
        Node *s = parse_stmt(p);
        if (s) node_add_child(program, s);
        if (p->panic_mode) synchronize(p);
        skip_newlines(p);
    }

    return program;
}

/* ================================================================
 *  AST 출력 (디버깅)
 * ================================================================ */
static const char *NODE_NAMES[NODE_COUNT] = {
    "PROGRAM",
    "BLOCK", "VAR_DECL", "CONST_DECL", "EXPR_STMT",
    "IF", "ELIF", "ELSE",
    "WHILE", "FOR_RANGE", "FOR_EACH",
    "SWITCH", "CASE", "DEFAULT",
    "FUNC_DECL", "VOID_DECL", "PARAM",
    "RETURN", "BREAK", "CONTINUE", "GOTO", "LABEL",
    "TRY", "IMPORT", "CLASS_DECL", "RAISE", "PP_STMT", "GPU_BLOCK", "GPU_OP",
    "SCRIPT_PYTHON", "SCRIPT_JAVA", "SCRIPT_JS",
    /* 계약 시스템 */
    "CONTRACT", "CONSTITUTION", "STATUTE", "REGULATION",
    "SANCTION", "CHECKPOINT",
    /* 표현식 */
    "INT_LIT", "FLOAT_LIT", "STRING_LIT", "CHAR_LIT",
    "BOOL_LIT", "NULL_LIT",
    "ARRAY_LIT", "DICT_LIT", "DICT_ENTRY",
    "IDENT", "BINARY", "UNARY", "ASSIGN",
    "CALL", "INDEX", "MEMBER", "LAMBDA",
    /* 산업/임베디드 v16.0.0 */
    "TIMER_BLOCK", "ROS2_BLOCK",
    /* 안전 규격 v17.0.0 */
    "WATCHDOG_BLOCK", "FAULT_TOL_BLOCK",
    /* 온디바이스 AI v18.0.0 */
    "AI_MODEL_BLOCK", "TINYML_BLOCK", "FEDERATED_BLOCK",
    /* 온톨로지 시스템 v18.5.0 4회차 */
    "ONT_BLOCK", "ONT_CONCEPT", "ONT_PROP", "ONT_RELATE", "ONT_QUERY", "ONT_INFER",
    /* Concept Identity v22.0.0 */
    "SEMANTIC_INFER", "VECTORIZE", "SEM_RECON", "REPRO_LABEL",
    /* 지식 뱅크 v22.6.0 */
    "KBANK", "KBANK_LOAD", "KBANK_COMPARE", "REPRO_LABEL_DECL", "KBANK_PROOF"
};

const char *node_type_name(NodeType t) {
    if (t < 0 || t >= NODE_COUNT) return "???";
    return NODE_NAMES[t];
}

void ast_print(const Node *node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth; i++) printf("  ");

    printf("[%s]", node_type_name(node->type));

    /* 추가 정보 출력 */
    if (node->sval)
        printf("  \"%s\"", node->sval);
    if (node->dtype != TOK_EOF)
        printf("  dtype=%s", token_type_name(node->dtype));
    if (node->op != TOK_EOF)
        printf("  op=%s", token_type_name(node->op));

    switch (node->type) {
        case NODE_INT_LIT:
            printf("  %lld", (long long)node->val.ival);
            break;
        case NODE_FLOAT_LIT:
            printf("  %g", node->val.fval);
            break;
        case NODE_BOOL_LIT:
            printf("  %s", node->val.bval ? "참" : "거짓");
            break;
        case NODE_CHAR_LIT:
            printf("  U+%04X", (unsigned)node->val.ival);
            break;
        default:
            break;
    }

    printf("  <%d:%d>\n", node->line, node->col);

    for (int i = 0; i < node->child_count; i++)
        ast_print(node->children[i], depth + 1);
}
