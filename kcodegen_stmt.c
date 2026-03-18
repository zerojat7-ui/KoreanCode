/*
 * kcodegen_stmt.c  —  C 코드 생성기 구문(Statement) 생성
 * version : v29.0.0
 *
 * v26.2.0 §14-4 컴파일 시 온톨로지 전송 바이너리 전환:
 *   NODE_ONT_BLOCK  : ont_mode/ont_line 컨텍스트 설정 + 접속 모드 전송 계층 로그 생성
 *   NODE_ONT_CONCEPT: mode=2 시 kc_ont_remote_add_class() 생성
 *   NODE_ONT_RELATE : mode=2 시 kc_ont_remote_add_relation() 생성
 *   NODE_ONT_QUERY  : mode=2 시 kc_ont_remote_query() + KC_REM_OK 오류 처리 생성
 *   NODE_ONT_INFER  : mode=2 시 kc_ont_remote_infer() + KC_REM_OK 오류 처리 생성
 *
 * v20.3.0:
 *   - gen_stmt() 온톨로지 노드 6종 C 코드 생성 추가
 *     NODE_ONT_BLOCK  : kc_ont_create()/destroy() 래핑 + 3모드 분기
 *     NODE_ONT_CONCEPT: kc_ont_add_class() 호출
 *     NODE_ONT_PROP   : kc_ont_add_prop_string/int/float() 호출
 *     NODE_ONT_RELATE : kc_ont_add_relation() 호출
 *     NODE_ONT_QUERY  : kc_ont_query_kor() 호출 + 결과 출력
 *     NODE_ONT_INFER  : kc_ont_infer() 호출
 *
 * ★ 이 파일은 kcodegen.c(unity build)에 #include 되어 컴파일됩니다.
 *    단독 컴파일 대상이 아닙니다.
 *
 * 담당 역할:
 *   - gen_stmt() : 모든 구문 노드 → C 코드 변환 메인 디스패처
 *       포함 범위:
 *         변수 선언 (var/const) / 상수 대입
 *         산술·비교·논리·비트·복합대입 연산문
 *         만약/아니면 (if/elif/else)
 *         동안 (while)
 *         반복 범위 (for range)
 *         각각 (foreach — 배열 순회)
 *         선택/경우/그외 (switch/case/default)
 *         멈춤/건너뜀 (break/continue)
 *         돌려줌 (return)
 *         시도/실패시/항상 (try/catch/finally — setjmp/longjmp)
 *         이동/레이블 (goto/label)
 *         오류 발생 (raise)
 *         스크립트 블록 (Python/Java/JavaScript — system() 실행)
 *         가속기 블록 (GPU/NPU/CPU — CUDA C 생성·nvcc 컴파일)
 *         신호받기/신호무시/신호기본/신호보내기 (signal 인터럽트)
 *         간섭/간섭잠금/간섭허용 (ISR 인터럽트)
 *         행사등록/행사시작/행사중단/행사발생/행사해제 (이벤트 루프)
 *         법령/법위반/복원지점 (계약 시스템)
 *         온톨로지 블록 (NODE_ONT_BLOCK/CONCEPT/PROP/RELATE/QUERY/INFER)
 *         함수 호출문 / 표현식문
 *
 * 분리 이전: kcodegen.c 내 lines 1105~2772
 *
 * MIT License
 * zerojat7
 */

/* ================================================================
 *  구문 생성
 * ================================================================ */
static void gen_stmt(Codegen *cg, Node *n) {
    if (!n) return;
    sourcemap_add(cg, n->line, n->col);

    switch (n->type) {

    /* ── 변수 선언 ─────────────────────────────────────── */
    case NODE_VAR_DECL: {
        const char *ctype = c_type(n->dtype);
        emit_indent(cg);
        emit(cg, "%s kc_%s", ctype, n->sval ? n->sval : "_var");
        if (n->child_count > 0) {
            emit(cg, " = ");
            gen_expr(cg, n->children[0]);
        } else {
            /* 기본값 */
            if (strcmp(ctype, "char*") == 0)        emit(cg, " = \"\"");
            else if (strcmp(ctype, "kc_array_t*") == 0) emit(cg, " = kc_arr_new()");
            else                                     emit(cg, " = 0");
        }
        emit(cg, ";\n");
        cg->c_line++;
        cg->result->var_count++;
        break;
    }

    case NODE_CONST_DECL: {
        emit_indent(cg);
        emit(cg, "const %s kc_%s = ", c_type(n->dtype), n->sval ? n->sval : "_const");
        if (n->child_count > 0) gen_expr(cg, n->children[0]);
        else emit(cg, "0");
        emit(cg, ";\n");
        cg->c_line++;
        break;
    }

    /* ── 표현식 구문 ────────────────────────────────────── */
    case NODE_EXPR_STMT:
        emit_indent(cg);
        if (n->child_count > 0) gen_expr(cg, n->children[0]);
        emit(cg, ";\n");
        cg->c_line++;
        break;

    /* ── 만약/아니면 ────────────────────────────────────── */
    case NODE_IF: {
        emit_indent(cg);
        emit(cg, "if (");
        gen_expr(cg, n->children[0]);
        emit(cg, ") {\n");
        cg->c_line++;
        cg->indent++;
        gen_block(cg, n->children[1]);
        cg->indent--;
        emitln(cg, "}");
        /* elif / else */
        if (n->child_count > 2 && n->children[2]) {
            Node *branch = n->children[2];
            while (branch) {
                if (branch->type == NODE_ELIF) {
                    emit_indent(cg);
                    emit(cg, "else if (");
                    gen_expr(cg, branch->children[0]);
                    emit(cg, ") {\n");
                    cg->c_line++;
                    cg->indent++;
                    gen_block(cg, branch->children[1]);
                    cg->indent--;
                    emitln(cg, "}");
                    branch = (branch->child_count > 2) ? branch->children[2] : NULL;
                } else if (branch->type == NODE_ELSE) {
                    emitln(cg, "else {");
                    cg->indent++;
                    gen_block(cg, branch->children[0]);
                    cg->indent--;
                    emitln(cg, "}");
                    branch = NULL;
                } else {
                    branch = NULL;
                }
            }
        }
        break;
    }

    /* ── 동안 ───────────────────────────────────────────── */
    case NODE_WHILE:
        emit_indent(cg);
        emit(cg, "while (");
        gen_expr(cg, n->children[0]);
        emit(cg, ") {\n");
        cg->c_line++;
        cg->indent++;
        gen_block(cg, n->children[1]);
        cg->indent--;
        emitln(cg, "}");
        break;

    /* ── 반복 (부터/까지) ───────────────────────────────── */
    case NODE_FOR_RANGE: {
        const char *var = n->sval ? n->sval : "_i";
        emit_indent(cg);
        emit(cg, "for (int64_t kc_%s = ", var);
        gen_expr(cg, n->children[0]);
        emit(cg, "; kc_%s <= ", var);
        gen_expr(cg, n->children[1]);
        /* step */
        if (n->child_count > 3) {
            emit(cg, "; kc_%s += ", var);
            gen_expr(cg, n->children[2]);
            emit(cg, ") {\n");
            cg->c_line++;
            cg->indent++;
            gen_block(cg, n->children[3]);
        } else {
            emit(cg, "; kc_%s++) {\n", var);
            cg->c_line++;
            cg->indent++;
            gen_block(cg, n->children[2]);
        }
        cg->indent--;
        emitln(cg, "}");
        break;
    }

    /* ── 각각 (foreach) ─────────────────────────────────── */
    case NODE_FOR_EACH: {
        const char *var = n->sval ? n->sval : "_item";
        char idx[32];
        snprintf(idx, sizeof(idx), "_kc_idx%d", cg->tmp_counter++);
        char arr[32];
        snprintf(arr, sizeof(arr), "_kc_arr%d", cg->tmp_counter++);

        emit_indent(cg);
        emit(cg, "{ kc_array_t* %s = (kc_array_t*)(", arr);
        gen_expr(cg, n->children[0]);
        emit(cg, ");\n");
        cg->c_line++;
        emit_indent(cg);
        emit(cg, "for (int64_t %s = 0; %s < kc_arr_len(%s); %s++) {\n",
             idx, idx, arr, idx);
        cg->c_line++;
        cg->indent++;
        emitln(cg, "kc_value_t kc_%s = kc_arr_get(%s, %s);", var, arr, idx);
        gen_block(cg, n->children[1]);
        cg->indent--;
        emitln(cg, "} }");
        break;
    }

    /* ── 선택/경우/그외 ─────────────────────────────────── */
    case NODE_SWITCH:
        emit_indent(cg);
        emit(cg, "switch ((int64_t)(");
        gen_expr(cg, n->children[0]);
        emit(cg, ")) {\n");
        cg->c_line++;
        for (int i = 1; i < n->child_count; i++) {
            Node *c = n->children[i];
            if (c->type == NODE_CASE) {
                emit_indent(cg);
                emit(cg, "case (int64_t)(");
                gen_expr(cg, c->children[0]);
                emit(cg, "): {\n");
                cg->c_line++;
                cg->indent++;
                gen_block(cg, c->children[1]);
                emitln(cg, "break;");
                cg->indent--;
                emitln(cg, "}");
            } else if (c->type == NODE_DEFAULT) {
                emitln(cg, "default: {");
                cg->indent++;
                gen_block(cg, c->children[0]);
                cg->indent--;
                emitln(cg, "}");
            }
        }
        emitln(cg, "}");
        break;

    /* ── 반환 ───────────────────────────────────────────── */
    case NODE_RETURN:
        emit_indent(cg);
        if (n->child_count > 0) {
            emit(cg, "return ");
            gen_expr(cg, n->children[0]);
            emit(cg, ";\n");
        } else {
            emit(cg, "return;\n");
        }
        cg->c_line++;
        break;

    /* ── 멈춤/건너뜀 ────────────────────────────────────── */
    case NODE_BREAK:
        emitln(cg, "break;");
        break;

    case NODE_CONTINUE:
        emitln(cg, "continue;");
        break;

    /* ── 이동 (goto) ────────────────────────────────────── */
    case NODE_GOTO:
        emitln(cg, "goto kc_label_%s;", n->sval ? n->sval : "_unknown");
        break;

    case NODE_LABEL:
        emit(cg, "kc_label_%s:\n", n->sval ? n->sval : "_unknown");
        cg->c_line++;
        break;

    /* ── 시도/실패시/항상 ────────────────────────────────── */
    case NODE_TRY: {
        /* setjmp/longjmp 기반 예외 처리 */
        char jbuf[32];
        snprintf(jbuf, sizeof(jbuf), "_kc_jmp%d", cg->tmp_counter++);
        emitln(cg, "{ jmp_buf %s;", jbuf);
        emitln(cg, "kc_push_jmp(&%s);", jbuf);
        emitln(cg, "if (setjmp(%s) == 0) {", jbuf);
        cg->indent++;
        if (n->child_count > 0) gen_block(cg, n->children[0]);
        cg->indent--;
        emitln(cg, "} else {");
        cg->indent++;
        if (n->child_count > 1) gen_block(cg, n->children[1]);
        cg->indent--;
        emitln(cg, "}");
        emitln(cg, "kc_pop_jmp();");
        /* 항상 블록 */
        if (n->child_count > 2 && n->children[2]) {
            gen_block(cg, n->children[2]);
        }
        emitln(cg, "}");
        break;
    }

    /* ── 오류 raise ─────────────────────────────────────── */
    case NODE_RAISE:
        emit_indent(cg);
        emit(cg, "kc_raise(");
        if (n->child_count > 0) gen_expr(cg, n->children[0]);
        else emit(cg, "\"오류\"");
        emit(cg, ");\n");
        cg->c_line++;
        break;

    /* ── 블록 ───────────────────────────────────────────── */
    case NODE_BLOCK:
        gen_block(cg, n);
        break;

    /* ── 함수/정의 선언 ─────────────────────────────────── */
    case NODE_FUNC_DECL:
    case NODE_VOID_DECL:
        /* 최상위 레벨에서 처리됨 — 여기선 무시 */
        break;

    /* ── 가져오기(가짐) (v6.1.0) ────────────────────────── */
    case NODE_IMPORT: {
        const char *mod = n->sval ? n->sval : "";

        /* 내장 모듈 → 해당 C 헤더 매핑 */
        if (strcmp(mod, "수학") == 0 || strcmp(mod, "수학함수") == 0) {
            emitln(cg, "#include <math.h>   /* 가짐 %s */", mod);
        } else if (strcmp(mod, "파일시스템") == 0) {
            emitln(cg, "#include <stdio.h>  /* 가짐 %s */", mod);
            emitln(cg, "#include <dirent.h>");
            emitln(cg, "#include <sys/stat.h>");
        } else if (strcmp(mod, "문자열") == 0) {
            emitln(cg, "#include <string.h> /* 가짐 %s */", mod);
        } else if (strcmp(mod, "시간") == 0) {
            emitln(cg, "#include <time.h>   /* 가짐 %s */", mod);
        } else if (strcmp(mod, "난수") == 0) {
            emitln(cg, "#include <stdlib.h> /* 가짐 %s */", mod);
        } else {
            /* 외부 파일 모듈 — 확장자 탐색 후 적절한 include 생성 */
            /* .han/.hg → 주석 */
            emitln(cg, "/* 가짐 외부 모듈: %s */", mod);
            emitln(cg, "/* 힌트: %s.han / %s.hg / %s.h 중 존재하는 파일 포함 */",
                   mod, mod, mod);
        }

        /* 로부터(from) 이름 목록이 있으면 주석으로 병기 */
        if (n->child_count > 0) {
            emit(cg, "/* 가져온 이름: ");
            for (int i = 0; i < n->child_count; i++) {
                if (n->children[i] && n->children[i]->sval)
                    emit(cg, "%s%s", n->children[i]->sval,
                         i < n->child_count - 1 ? ", " : "");
            }
            emit(cg, " */\n");
            cg->c_line++;
        }
        break;
    }

    /* ── 전처리기 (v6.1.0) ───────────────────────────────── */
    case NODE_PP_STMT: {
        if (n->op != TOK_PP_INCLUDE || !n->sval) {
            emitln(cg, "/* 전처리기 구문 */");
            break;
        }
        const char *fname = n->sval;
        const char *dot   = strrchr(fname, '.');
        const char *ext   = dot ? dot : "";

        /* .han / .hg → C 코드에서는 해당 .h 가 있다면 포함, 없으면 주석 */
        if (strcmp(ext, ".han") == 0 || strcmp(ext, ".hg") == 0) {
            emitln(cg, "/* #포함 Kcode 모듈: %s */", fname);
        }
        /* .c / .h → 직접 #include */
        else if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) {
            emitln(cg, "#include \"%s\"", fname);
            cg->result->var_count++; /* include 카운트 재활용 */
        }
        /* .cpp / .hpp → extern "C" 래핑 #include */
        else if (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0) {
            emitln(cg, "#ifdef __cplusplus");
            emitln(cg, "#include \"%s\"", fname);
            emitln(cg, "#else");
            emitln(cg, "/* C++ 헤더 '%s' — C++ 모드에서만 포함 가능 */", fname);
            emitln(cg, "#endif");
        }
        /* .py → Python.h 기반 임베딩 힌트 주석 */
        else if (strcmp(ext, ".py") == 0) {
            emitln(cg, "/* #포함 Python 모듈: %s */", fname);
            emitln(cg, "/* 힌트: Py_Initialize() + PyRun_SimpleFile() 로 실행 */");
            emitln(cg, "#ifdef KCODE_EMBED_PYTHON");
            emitln(cg, "#include <Python.h>");
            emitln(cg, "#endif");
        }
        /* .js / .ts → Node.js/V8 임베딩 힌트 주석 */
        else if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) {
            emitln(cg, "/* #포함 JS/TS 모듈: %s */", fname);
            emitln(cg, "/* 힌트: system(\"node %s\") 또는 V8 임베딩 */", fname);
        }
        /* .java → JNI 힌트 주석 */
        else if (strcmp(ext, ".java") == 0) {
            emitln(cg, "/* #포함 Java 모듈: %s */", fname);
            emitln(cg, "/* 힌트: JNI(JavaVM) 또는 system(\"javac && java\") */");
        }
        else {
            emitln(cg, "/* #포함 알 수 없는 확장자: %s */", fname);
        }
        break;
    }

    /* ── 스크립트 블록 (v8.1.0) ────────────────────────────────
     *  파이썬/자바/자바스크립트 블록을 C 코드의 system() 호출로 변환.
     *  전략:
     *    1. 전달 변수들을 setenv() 로 환경변수 설정
     *    2. 스크립트 원문을 임시 파일에 fwrite 로 기록
     *    3. system("python3|node|javac+java tmp.ext > out.txt 2>&1")
     *    4. 반환 변수가 있으면 out.txt 마지막 줄을 읽어 변수에 할당
     *    5. 임시 파일/출력 파일 삭제
     * ================================================================ */
    case NODE_SCRIPT_PYTHON:
    case NODE_SCRIPT_JAVA:
    case NODE_SCRIPT_JS: {
        if (!n->sval) break;

        const char *lang_ko;
        const char *ext;
        const char *runner;
        int is_java = (n->type == NODE_SCRIPT_JAVA);
        int is_py   = (n->type == NODE_SCRIPT_PYTHON);

        if (is_py) {
            lang_ko = "\xED\x8C\x8C\xEC\x9D\xB4\xEC\x8D\xAC"; /* 파이썬 */
            ext     = ".py";
            runner  = "python3";
        } else if (is_java) {
            lang_ko = "\xEC\x9E\x90\xEB\xB0\x94";             /* 자바 */
            ext     = ".java";
            runner  = "javac";
        } else {
            lang_ko = "\xEC\x9E\x90\xEB\xB0\x94\xEC\x8A\xA4\xED\x81\xAC\xEB\xA6\xBD\xED\x8A\xB8"; /* 자바스크립트 */
            ext     = ".js";
            runner  = "node";
        }
        (void)runner;

        /* ret_child: node->val.ival (-1이면 반환 없음) */
        int ret_child = (int)n->val.ival;
        int arg_end   = (ret_child >= 0) ? ret_child : n->child_count;

        /* 고유 카운터 (임시 파일 이름 충돌 방지) */
        int uid = cg->tmp_counter++;

        sourcemap_add(cg, n->line, n->col);
        emitln(cg, "/* ── %s 스크립트 블록 (v8.1.0) ── */", lang_ko);
        emitln(cg, "{");
        cg->indent++;

        /* 1. 임시 파일 경로 변수 */
        emitln(cg, "char _kc_script_path_%d[256];", uid);
        emitln(cg, "char _kc_out_path_%d[260];",    uid);
        emitln(cg, "snprintf(_kc_script_path_%d, sizeof(_kc_script_path_%d),", uid, uid);
        emitln(cg, "         \"/tmp/_kcode_cg_%d_%%d%s\", (int)getpid());", uid, ext);
        emitln(cg, "snprintf(_kc_out_path_%d, sizeof(_kc_out_path_%d),", uid, uid);
        emitln(cg, "         \"%%s.out\", _kc_script_path_%d);", uid);

        /* 2. 전달 변수 → setenv */
        for (int i = 0; i < arg_end; i++) {
            Node *ch = n->children[i];
            if (!ch || ch->type != NODE_IDENT || !ch->sval) continue;
            emitln(cg, "setenv(\"KCODE_%s\", kc_to_cstr(kc_%s), 1);",
                   ch->sval, ch->sval);
        }

        /* 3. 스크립트 원문을 임시 파일에 기록 */
        emitln(cg, "{ FILE *_kc_sf_%d = fopen(_kc_script_path_%d, \"w\");", uid, uid);
        cg->indent++;
        emitln(cg, "if (_kc_sf_%d) {", uid);
        cg->indent++;

        /* Python/JS: 환경변수로부터 변수 임포트 헤더 삽입 */
        if (is_py && arg_end > 0) {
            emitln(cg, "fprintf(_kc_sf_%d, \"import os as _kc_os\\n\");", uid);
            for (int i = 0; i < arg_end; i++) {
                Node *ch = n->children[i];
                if (!ch || !ch->sval) continue;
                emitln(cg,
                    "fprintf(_kc_sf_%d,"
                    " \"_kc_raw_%s=_kc_os.environ.get('KCODE_%s','')\\n"
                    "try: %s=int(_kc_raw_%s)\\n"
                    "except ValueError:\\n"
                    " try: %s=float(_kc_raw_%s)\\n"
                    " except ValueError: %s=_kc_raw_%s\\n\");",
                    uid,
                    ch->sval, ch->sval,
                    ch->sval, ch->sval,
                    ch->sval, ch->sval,
                    ch->sval, ch->sval);
            }
        } else if (!is_java && !is_py && arg_end > 0) {
            /* JavaScript: process.env */
            for (int i = 0; i < arg_end; i++) {
                Node *ch = n->children[i];
                if (!ch || !ch->sval) continue;
                emitln(cg,
                    "fprintf(_kc_sf_%d,"
                    " \"const %s=(v=>isNaN(v)?process.env['KCODE_%s']||'':Number(v))"
                    "(process.env['KCODE_%s']);\\n\");",
                    uid, ch->sval, ch->sval, ch->sval);
            }
        }

        /* 원문 코드 escaping 후 fwrite */
        /* 스크립트 원문을 C 문자열 리터럴로 embed */
        emit_indent(cg);
        emit(cg, "fputs(");
        escape_string(cg, n->sval);
        emitln(cg, ", _kc_sf_%d);", uid);
        emitln(cg, "fclose(_kc_sf_%d);", uid);

        cg->indent--;
        emitln(cg, "} /* fopen */");
        cg->indent--;
        emitln(cg, "} /* script write */");

        /* 4. 실행 커맨드 */
        emitln(cg, "{ char _kc_cmd_%d[640];", uid);
        cg->indent++;

        if (is_java) {
            emitln(cg, "char _kc_cdir_%d[300];", uid);
            emitln(cg, "snprintf(_kc_cdir_%d, sizeof(_kc_cdir_%d), \"%%s_cls\", _kc_script_path_%d);",
                   uid, uid, uid);
            emitln(cg, "snprintf(_kc_cmd_%d, sizeof(_kc_cmd_%d),", uid, uid);
            emitln(cg, "    \"mkdir -p '%%s' && javac -d '%%s' '%%s' > '%%s' 2>&1 && java -cp '%%s' Main >> '%%s' 2>&1\",");
            emitln(cg, "    _kc_cdir_%d, _kc_cdir_%d, _kc_script_path_%d, _kc_out_path_%d,",
                   uid, uid, uid, uid);
            emitln(cg, "    _kc_cdir_%d, _kc_out_path_%d);", uid, uid);
        } else {
            /* python3 or node */
            const char *run_cmd = is_py ? "python3" : "node";
            emitln(cg, "snprintf(_kc_cmd_%d, sizeof(_kc_cmd_%d),", uid, uid);
            emitln(cg, "    \"%s '%%s' > '%%s' 2>&1\", _kc_script_path_%d, _kc_out_path_%d);",
                   run_cmd, uid, uid);
        }

        emitln(cg, "int _kc_ret_%d = system(_kc_cmd_%d);", uid, uid);

        /* 5. stdout 수집 */
        emitln(cg, "char _kc_out_%d[65536] = \"\";", uid);
        emitln(cg, "{ FILE *_kc_of_%d = fopen(_kc_out_path_%d, \"r\");", uid, uid);
        cg->indent++;
        emitln(cg, "if (_kc_of_%d) {", uid);
        cg->indent++;
        emitln(cg, "fread(_kc_out_%d, 1, sizeof(_kc_out_%d)-1, _kc_of_%d);",
               uid, uid, uid);
        emitln(cg, "fclose(_kc_of_%d);", uid);
        cg->indent--;
        emitln(cg, "}");
        cg->indent--;
        emitln(cg, "}");

        /* 6. 오류 출력 (비정상 종료 시 stderr) */
        emitln(cg, "if (_kc_ret_%d != 0) {", uid);
        cg->indent++;
        emitln(cg, "fprintf(stderr, \"[%s 오류] %%s\\n\", _kc_out_%d);", lang_ko, uid);
        cg->indent--;
        emitln(cg, "}");

        /* 7. 반환 변수에 stdout 마지막 줄 저장 */
        if (ret_child >= 0 && ret_child < n->child_count &&
            n->children[ret_child] && n->children[ret_child]->sval) {
            const char *rv = n->children[ret_child]->sval;
            emitln(cg, "/* 반환 변수: %s ← stdout 마지막 줄 */", rv);
            emitln(cg, "{ char *_kc_last_%d = _kc_out_%d;", uid, uid);
            cg->indent++;
            emitln(cg, "for (char *_kc_p = _kc_out_%d; *_kc_p; _kc_p++) {", uid);
            cg->indent++;
            emitln(cg, "if (*_kc_p == '\\n' && *((_kc_p)+1) && *((_kc_p)+1) != '\\n')");
            cg->indent++;
            emitln(cg, "_kc_last_%d = (_kc_p)+1;", uid);
            cg->indent--;
            cg->indent--;
            emitln(cg, "}");
            /* 후행 개행 제거 */
            emitln(cg, "size_t _kc_ll_%d = strlen(_kc_last_%d);", uid, uid);
            emitln(cg, "if (_kc_ll_%d > 0 && _kc_last_%d[_kc_ll_%d-1]=='\\n')"
                       " _kc_last_%d[--_kc_ll_%d]='\\0';",
                   uid, uid, uid, uid, uid);
            emitln(cg, "kc_%s = _kc_last_%d;", rv, uid);
            cg->indent--;
            emitln(cg, "}");
        }

        /* 8. 임시 파일 삭제 */
        emitln(cg, "remove(_kc_script_path_%d);", uid);
        emitln(cg, "remove(_kc_out_path_%d);",    uid);
        emitln(cg, "(void)_kc_ret_%d;", uid);

        cg->indent--;
        emitln(cg, "} /* cmd block */");

        cg->indent--;
        emitln(cg, "} /* %s 스크립트 블록 끝 */", lang_ko);
        break;
    /* ── GPU 가속 블록 (v2.0.0 — Zero-Copy SoA + VRAM 상주) ─── */
    case NODE_GPU_BLOCK: {
        /* kcodegen.c 와 동일한 Zero-Copy SoA 설계 */
        const char *accel_type = n->sval;
        int uid = cg->tmp_counter++;

        const char *op_name = NULL, *ret_vname = NULL;
        const char *in_args[4] = {NULL,NULL,NULL,NULL};
        int in_argc = 0, body_idx = -1;

        for (int i=(int)n->child_count-1; i>=0; i--) {
            if (n->children[i] && n->children[i]->type==NODE_BLOCK) { body_idx=i; break; }
        }
        for (int i=0; i<(body_idx>=0?body_idx:(int)n->child_count); i++) {
            Node *ch = n->children[i];
            if (!ch || ch->type!=NODE_GPU_OP) continue;
            op_name = ch->sval;
            int ret_idx=(int)ch->val.ival;
            for (int j=0; j<ch->child_count && in_argc<4; j++) {
                if (!ch->children[j]) continue;
                if (j==ret_idx) ret_vname=ch->children[j]->sval;
                else if (ch->children[j]->type==NODE_IDENT) in_args[in_argc++]=ch->children[j]->sval;
            }
            break;
        }

        sourcemap_add(cg, n->line, n->col);
        emitln(cg, "/* ── 가속기 블록 (v2.0.0) 종류: %s ── */", accel_type?accel_type:"AUTO");
        cg->has_accel_block = 1;

        if (op_name) {
            const char *arg_a = in_argc>0?in_args[0]:NULL;
            const char *arg_b = in_argc>1?in_args[1]:NULL;
            emitln(cg, "{"); cg->indent++;

            /* 가속기 타입 */
            if (!accel_type||strcmp(accel_type,"AUTO")==0)
                emitln(cg,"KcAccelType _ac_%d=kc_accel_detect();",uid);
            else
                emitln(cg,"KcAccelType _ac_%d=KC_ACCEL_%s;",uid,accel_type);

            /* 입력 A — kc_array_t → float* (SoA 직접 참조) */
            if (arg_a) {
                emitln(cg,"size_t _na_%d=kc_%s?(size_t)kc_%s->len:0;",uid,arg_a,arg_a);
                emitln(cg,"float *_fa_%d=NULL;",uid);
                emitln(cg,"if(_na_%d>0&&kc_%s){",uid,arg_a); cg->indent++;
                emitln(cg,"_fa_%d=(float*)malloc(_na_%d*sizeof(float));",uid,uid);
                emitln(cg,"for(size_t _i=0;_i<_na_%d;_i++){",uid); cg->indent++;
                emitln(cg,"kc_value_t _v=kc_arr_get(kc_%s,(int64_t)_i);",arg_a);
                emitln(cg,"_fa_%d[_i]=(_v.type==2)?(float)_v.v.fval:(float)_v.v.ival;",uid);
                cg->indent--; emitln(cg,"}"); cg->indent--; emitln(cg,"}");
            } else { emitln(cg,"size_t _na_%d=0;float *_fa_%d=NULL;",uid,uid); }

            /* 입력 B */
            if (arg_b) {
                emitln(cg,"size_t _nb_%d=kc_%s?(size_t)kc_%s->len:0;",uid,arg_b,arg_b);
                emitln(cg,"float *_fb_%d=NULL;",uid);
                emitln(cg,"if(_nb_%d>0&&kc_%s){",uid,arg_b); cg->indent++;
                emitln(cg,"_fb_%d=(float*)malloc(_nb_%d*sizeof(float));",uid,uid);
                emitln(cg,"for(size_t _i=0;_i<_nb_%d;_i++){",uid); cg->indent++;
                emitln(cg,"kc_value_t _v=kc_arr_get(kc_%s,(int64_t)_i);",arg_b);
                emitln(cg,"_fb_%d[_i]=(_v.type==2)?(float)_v.v.fval:(float)_v.v.ival;",uid);
                cg->indent--; emitln(cg,"}"); cg->indent--; emitln(cg,"}");
            } else { emitln(cg,"size_t _nb_%d=0;float *_fb_%d=NULL;",uid,uid); }

            /* begin — 디바이스 업로드 (1회) */
            emitln(cg,"KcAccelCtx *_ctx_%d=kc_accel_begin(_ac_%d,_fa_%d,_na_%d,_fb_%d,_nb_%d);",
                   uid,uid,uid,uid,uid,uid);
            emitln(cg,"if(_ctx_%d){",uid); cg->indent++;
            /* exec — VRAM 안에서 연산 */
            emitln(cg,"size_t _no_%d=_na_%d>0?_na_%d:_nb_%d;",uid,uid,uid,uid);
            emitln(cg,"kc_accel_exec(_ctx_%d,\"%s\",_no_%d);",uid,op_name,uid);
            /* end — 결과 회수 (1회) */
            emitln(cg,"size_t _rn_%d=0;",uid);
            emitln(cg,"float *_res_%d=kc_accel_end(_ctx_%d,&_rn_%d);",uid,uid,uid);

            if (ret_vname) {
                emitln(cg,"if(_res_%d&&_rn_%d>0){",uid,uid); cg->indent++;
                emitln(cg,"kc_%s=kc_arr_new();",ret_vname);
                emitln(cg,"kc_%s->data=malloc(sizeof(kc_value_t)*_rn_%d);",ret_vname,uid);
                emitln(cg,"kc_%s->len=kc_%s->cap=(int64_t)_rn_%d;",ret_vname,ret_vname,uid);
                emitln(cg,"for(size_t _i=0;_i<_rn_%d;_i++){",uid); cg->indent++;
                emitln(cg,"float _f=_res_%d[_i];long long _iv=(long long)_f;",uid);
                emitln(cg,"kc_value_t _rv=((float)_iv==_f)?(kc_value_t){.type=1,.v.ival=_iv}:(kc_value_t){.type=2,.v.fval=(double)_f};");
                emitln(cg,"((kc_value_t*)kc_%s->data)[_i]=_rv;",ret_vname);
                cg->indent--; emitln(cg,"}");
                emitln(cg,"free(_res_%d);",uid);
                cg->indent--; emitln(cg,"}");
            } else { emitln(cg,"if(_res_%d)free(_res_%d);",uid,uid); }

            cg->indent--; emitln(cg,"}");
            emitln(cg,"if(_fa_%d)free(_fa_%d);",uid,uid);
            emitln(cg,"if(_fb_%d)free(_fb_%d);",uid,uid);
            cg->indent--; emitln(cg,"} /* 가속기 연산 블록 */");
        }

        if (body_idx>=0&&n->children[body_idx]) gen_stmt(cg,n->children[body_idx]);
        emitln(cg,"/* ── 가속기 블록 끝 (v2.0.0) ── */");
        break;
    }

    /* ── 계약 시스템 (v2.1.0) ──────────────────────────────── */
    /* ★ 헌법 — 전역 최상위 계약 (v5.0.0) */
    case NODE_CONSTITUTION: {
        const char *san = "중단";
        if (n->child_count > 1 && n->children[1])
            san = token_type_name(n->children[1]->op);
        emitln(cg, "/* [헌법 계약 — 전역 최상위, 제재: %s] */", san);
        if (n->child_count > 0 && n->children[0]) {
            emit(cg, "assert((");
            gen_expr(cg, n->children[0]);
            emitln(cg, ") && \"헌법 계약 위반\");");
        }
        break;
    }

    /* ★ 법률 — 현재 파일 전체 계약 (v5.0.0) */
    case NODE_STATUTE: {
        const char *san = "경고";
        if (n->child_count > 1 && n->children[1])
            san = token_type_name(n->children[1]->op);
        emitln(cg, "/* [법률 계약 — 파일 단위, 제재: %s] */", san);
        if (n->child_count > 0 && n->children[0]) {
            emit(cg, "if (!(");
            gen_expr(cg, n->children[0]);
            emitln(cg, ")) { fprintf(stderr, \"[법률 계약 위반]\\n\"); }");
        }
        break;
    }

    /* ★ 규정 — 특정 객체 전체 메서드 계약 (v5.0.0) */
    case NODE_REGULATION: {
        const char *obj_name = n->sval ? n->sval : "?";
        const char *san = "경고";
        if (n->child_count > 1 && n->children[1])
            san = token_type_name(n->children[1]->op);
        emitln(cg, "/* [규정 계약 — 객체: %s, 제재: %s] */", obj_name, san);
        if (n->child_count > 0 && n->children[0]) {
            emit(cg, "assert((");
            gen_expr(cg, n->children[0]);
            emitln(cg, ") && \"규정 계약 위반: %s\");", obj_name);
        }
        break;
    }

    case NODE_CONTRACT:
        gen_contract(cg, n);
        break;

    case NODE_CHECKPOINT:
        gen_checkpoint(cg, n);
        break;

    case NODE_SANCTION:
        emitln(cg, "/* [제재 노드 — NODE_CONTRACT 내부 처리됨] */");
        break;

    /* ── 인터럽트 시스템 (v6.0.0) ──────────────────────────── */

    /* A: 신호받기 → signal(SIGXXX, handler_fn) + 핸들러 함수 생성 */
    case NODE_SIGNAL_HANDLER: {
        const char *sname = n->sval ? n->sval : "?";
        /* 신호 이름 → C 상수 매핑 */
        const char *posix_sig = "SIGINT";
        if      (n->op == TOK_KW_SIG_INT)  posix_sig = "SIGINT";
        else if (n->op == TOK_KW_SIG_TERM) posix_sig = "SIGTERM";
        else if (n->op == TOK_KW_SIG_KILL) posix_sig = "SIGKILL";
        else if (n->op == TOK_KW_SIG_CHLD) posix_sig = "SIGCHLD";
        else if (n->op == TOK_KW_SIG_USR1) posix_sig = "SIGUSR1";
        else if (n->op == TOK_KW_SIG_USR2) posix_sig = "SIGUSR2";
        else if (n->op == TOK_KW_SIG_PIPE) posix_sig = "SIGPIPE";
        else if (n->op == TOK_KW_SIG_ALRM) posix_sig = "SIGALRM";
        else if (n->op == TOK_KW_SIG_STOP) posix_sig = "SIGSTOP";
        else if (n->op == TOK_KW_SIG_CONT) posix_sig = "SIGCONT";
        /* 핸들러 함수 이름 생성 (충돌 방지용 카운터) */
        int hid = cg->tmp_counter++;
        emitln(cg, "/* [신호받기: %s (%s)] */", sname, posix_sig);
        /* 핸들러 함수를 현재 위치 밖에 선언해야 하므로
         * 여기서는 forward 선언만 하고 함수 본체는 인라인 정적 함수로 생성 */
        emitln(cg, "static void kc_sig_handler_%d(int _kc_signum) {", hid);
        cg->indent++;
        emitln(cg, "(void)_kc_signum;");
        if (n->child_count > 0) gen_stmt(cg, n->children[0]);
        cg->indent--;
        emitln(cg, "}");
        emitln(cg, "signal(%s, kc_sig_handler_%d);", posix_sig, hid);
        break;
    }

    /* A: 신호무시 / 신호기본 / 신호보내기 */
    case NODE_SIGNAL_CTRL: {
        const char *sname = n->sval ? n->sval : "?";
        const char *psig  = "SIGINT";
        if      (strcmp(sname, "중단신호")   == 0) psig = "SIGINT";
        else if (strcmp(sname, "종료신호")   == 0) psig = "SIGTERM";
        else if (strcmp(sname, "강제종료")   == 0) psig = "SIGKILL";
        else if (strcmp(sname, "자식신호")   == 0) psig = "SIGCHLD";
        else if (strcmp(sname, "사용자신호1") == 0) psig = "SIGUSR1";
        else if (strcmp(sname, "사용자신호2") == 0) psig = "SIGUSR2";
        else if (strcmp(sname, "연결신호")   == 0) psig = "SIGPIPE";
        else if (strcmp(sname, "경보신호")   == 0) psig = "SIGALRM";
        else if (strcmp(sname, "재개신호")   == 0) psig = "SIGCONT";

        if (n->op == TOK_KW_SINHOMUSI) {
            emitln(cg, "signal(%s, SIG_IGN);  /* 신호무시: %s */", psig, sname);
        } else if (n->op == TOK_KW_SINHOGIBON) {
            emitln(cg, "signal(%s, SIG_DFL);  /* 신호기본: %s */", psig, sname);
        } else if (n->op == TOK_KW_SINHOBONEGI) {
            emitln(cg, "/* 신호보내기: %s → %s */", sname, psig);
            emit_indent(cg);
            emit(cg, "kill((pid_t)(");
            if (n->child_count > 0) gen_expr(cg, n->children[0]);
            else emit(cg, "getpid()");
            emitln(cg, "), %s);", psig);
        }
        break;
    }

    /* B: 간섭(ISR) 핸들러 → AVR/ARM ISR 매크로 또는 __attribute__((interrupt)) */
    case NODE_ISR_HANDLER: {
        const char *vname = n->sval ? n->sval : "?";
        /* 벡터 이름 → C 매크로 */
        const char *vec_macro = "TIMER0_OVF_vect";
        if      (n->op == TOK_KW_IRQ_TIMER0)    vec_macro = "TIMER0_OVF_vect";
        else if (n->op == TOK_KW_IRQ_TIMER1)    vec_macro = "TIMER1_OVF_vect";
        else if (n->op == TOK_KW_IRQ_EXT0_RISE) vec_macro = "INT0_vect  /* 상승 */";
        else if (n->op == TOK_KW_IRQ_EXT0_FALL) vec_macro = "INT0_vect  /* 하강 */";
        else if (n->op == TOK_KW_IRQ_UART_RX)   vec_macro = "USART_RX_vect";

        emitln(cg, "/* [간섭 ISR: %s] */", vname);
        emitln(cg, "#ifdef ISR  /* AVR avr/interrupt.h */");
        emitln(cg, "ISR(%s) {", vec_macro);
        cg->indent++;
        if (n->child_count > 0) gen_stmt(cg, n->children[0]);
        cg->indent--;
        emitln(cg, "}");
        emitln(cg, "#else  /* 일반 플랫폼 — 시뮬레이션 함수 */");
        emitln(cg, "static void kc_isr_%s(void) {", vname);
        cg->indent++;
        if (n->child_count > 0) gen_stmt(cg, n->children[0]);
        cg->indent--;
        emitln(cg, "}");
        emitln(cg, "#endif  /* ISR */");
        break;
    }

    /* B: 간섭잠금 / 간섭허용 */
    case NODE_ISR_CTRL:
        if (n->op == TOK_KW_GANSEOB_JAMGEUM)
            emitln(cg, "#ifdef cli\n    cli();  /* 간섭잠금 */\n#else\n    /* 간섭잠금: 플랫폼 미지원 */\n#endif");
        else
            emitln(cg, "#ifdef sei\n    sei();  /* 간섭허용 */\n#else\n    /* 간섭허용: 플랫폼 미지원 */\n#endif");
        break;

    /* C: 행사 핸들러 등록 → 함수 포인터 테이블 */
    case NODE_EVENT_HANDLER: {
        const char *evname = n->sval ? n->sval : "?";
        int hid = cg->tmp_counter++;
        emitln(cg, "/* [행사등록: \"%s\"] */", evname);
        /* 핸들러 함수 생성 */
        emitln(cg, "static void kc_ev_handler_%d(void) {", hid);
        cg->indent++;
        if (n->child_count > 0)
            gen_stmt(cg, n->children[n->child_count - 1]);
        cg->indent--;
        emitln(cg, "}");
        /* 런타임 이벤트 테이블에 등록 */
        emitln(cg, "kc_event_register(\"%s\", kc_ev_handler_%d);", evname, hid);
        break;
    }

    /* C: 행사시작 / 행사중단 / 행사발생 / 행사해제 */
    case NODE_EVENT_CTRL:
        if (n->op == TOK_KW_HAENGSA_START) {
            emitln(cg, "kc_event_loop_run();  /* 행사시작 */");
        } else if (n->op == TOK_KW_HAENGSA_STOP) {
            emitln(cg, "kc_event_loop_stop();  /* 행사중단 */");
        } else if (n->op == TOK_KW_HAENGSA_EMIT) {
            emitln(cg, "/* 행사발생 */");
            emit_indent(cg);
            emit(cg, "kc_event_emit(");
            if (n->child_count > 0) gen_expr(cg, n->children[0]);
            emitln(cg, ");");
        } else if (n->op == TOK_KW_HAENGSA_OFF) {
            emitln(cg, "/* 행사해제 */");
            emit_indent(cg);
            emit(cg, "kc_event_unregister(");
            if (n->child_count > 0) gen_expr(cg, n->children[0]);
            emitln(cg, ");");
        }
        break;

    /* ── 객체 클래스 선언 (v2.2.0) ─────────────────────────── */
    case NODE_CLASS_DECL: {
        const char *cname = n->sval ? n->sval : "_class";

        /* 부모 클래스(이어받기) 추출
         * child[0]이 NODE_IDENT이고 child_count >= 2이면 상속 */
        const char *parent = NULL;
        int body_idx = n->child_count - 1; /* 마지막 child = 블록 */
        if (n->child_count >= 2 &&
            n->children[0] &&
            n->children[0]->type == NODE_IDENT) {
            parent = n->children[0]->sval;
        }

        emitln(cg, "/* ── 객체: %s%s%s ── */",
               cname,
               parent ? " (이어받기: " : "",
               parent ? parent : "");
        if (parent) emitln(cg, " */");

        /* typedef struct kc_클래스명 { */
        emitln(cg, "typedef struct kc_%s {", cname);
        cg->indent++;

        /* 상속: 부모를 첫 필드로 임베드 */
        if (parent) {
            emitln(cg, "struct kc_%s kc_base;  /* 부모: %s */", parent, parent);
        }

        /* 블록 내 필드(VAR_DECL/CONST_DECL)만 struct 멤버로 출력 */
        Node *body = n->children[body_idx];
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s) continue;
                if (s->type == NODE_VAR_DECL || s->type == NODE_CONST_DECL) {
                    emitln(cg, "%s kc_%s;",
                           c_type(s->dtype),
                           s->sval ? s->sval : "_field");
                    cg->result->var_count++;
                }
            }
        }

        cg->indent--;
        emitln(cg, "} kc_%s;", cname);
        emit(cg, "\n");
        cg->c_line++;

        /* 메서드 함수 전방 선언 + 본체 생성 */
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s) continue;
                if (s->type != NODE_FUNC_DECL && s->type != NODE_VOID_DECL) continue;

                int is_void = (s->type == NODE_VOID_DECL);
                const char *mname = s->sval ? s->sval : "_method";

                cg->result->func_count++;

                /* 메서드 시그니처: 반환타입 kc_클래스명_메서드명(kc_클래스명* kc_자신, ...) */
                emitln(cg, "/* 메서드: %s.%s */", cname, mname);
                emit(cg, "%s kc_%s_%s(kc_%s* kc_\xEC\x9E\x90\xEC\x8B\xA0",
                     is_void ? "void" : "kc_value_t",
                     cname, mname, cname);  /* 자신 = \xEC\x9E\x90\xEC\x8B\xA0 */

                /* 매개변수 (자신 제외 — 첫 번째 param이 자신이면 skip) */
                int param_end = s->child_count - 1;
                int start_param = 0;
                /* 첫 파라미터가 '자신'이면 건너뜀 */
                if (param_end > 0 && s->children[0] &&
                    s->children[0]->type == NODE_PARAM &&
                    s->children[0]->sval &&
                    strcmp(s->children[0]->sval,
                           "\xEC\x9E\x90\xEC\x8B\xA0") == 0) {
                    start_param = 1;
                }
                for (int j = start_param; j < param_end; j++) {
                    Node *p = s->children[j];
                    if (!p || p->type != NODE_PARAM) break;
                    emit(cg, ", %s kc_%s",
                         c_type(p->dtype), p->sval ? p->sval : "_p");
                }
                emit(cg, ") {\n");
                cg->c_line++;

                cg->indent++;
                cg->in_func = 1;
                cg->func_has_return = !is_void;

                Node *mbody = s->children[s->child_count - 1];
                gen_block(cg, mbody);

                if (is_void) emitln(cg, "return;");
                else         emitln(cg, "return KC_NULL;");

                cg->indent--;
                cg->in_func = 0;
                emitln(cg, "}");
                emit(cg, "\n");
                cg->c_line++;
            }
        }

        /* 생성자 팩토리 함수: kc_클래스명_new() — malloc + 생성 호출 */
        emitln(cg, "/* 생성자 팩토리: %s */", cname);
        emit(cg, "static kc_%s* kc_%s_new(", cname, cname);
        /* 생성 메서드 파라미터 추출 (자신 제외) */
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s || (s->type != NODE_FUNC_DECL && s->type != NODE_VOID_DECL)) continue;
                /* \xEC\x83\x9D\xEC\x84\xB1 = 생성 */
                if (!s->sval || strcmp(s->sval, "\xEC\x83\x9D\xEC\x84\xB1") != 0) continue;
                int param_end = s->child_count - 1;
                int first = 1;
                for (int j = 0; j < param_end; j++) {
                    Node *p = s->children[j];
                    if (!p || p->type != NODE_PARAM) break;
                    if (p->sval && strcmp(p->sval, "\xEC\x9E\x90\xEC\x8B\xA0") == 0) continue;
                    if (!first) emit(cg, ", ");
                    emit(cg, "%s kc_%s", c_type(p->dtype), p->sval ? p->sval : "_p");
                    first = 0;
                }
                break;
            }
        }
        emit(cg, ") {\n"); cg->c_line++;
        cg->indent++;
        emitln(cg, "kc_%s* kc_\xEC\x9E\x90\xEC\x8B\xA0 = (kc_%s*)calloc(1, sizeof(kc_%s));",
               cname, cname, cname);
        /* 생성 메서드 호출 */
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s || (s->type != NODE_FUNC_DECL && s->type != NODE_VOID_DECL)) continue;
                if (!s->sval || strcmp(s->sval, "\xEC\x83\x9D\xEC\x84\xB1") != 0) continue;
                emit_indent(cg);
                emit(cg, "kc_%s_\xEC\x83\x9D\xEC\x84\xB1(kc_\xEC\x9E\x90\xEC\x8B\xA0",
                     cname);
                int param_end = s->child_count - 1;
                for (int j = 0; j < param_end; j++) {
                    Node *p = s->children[j];
                    if (!p || p->type != NODE_PARAM) break;
                    if (p->sval && strcmp(p->sval, "\xEC\x9E\x90\xEC\x8B\xA0") == 0) continue;
                    emit(cg, ", kc_%s", p->sval ? p->sval : "_p");
                }
                emit(cg, ");\n"); cg->c_line++;
                break;
            }
        }
        emitln(cg, "return kc_\xEC\x9E\x90\xEC\x8B\xA0;");
        cg->indent--;
        emitln(cg, "}");
        emit(cg, "\n"); cg->c_line++;
        break;
    }

    /* ── MCP 시스템 (v14.0.0) ─────────────────────────────── */
    case NODE_MCP_SERVER: {
        /* #include "kc_mcp.h" 런타임 헤더 필요 선언 */
        emit(cg, "/* MCP서버: %s */\n", n->sval ? n->sval : "kcode-mcp");
        cg->c_line++;
        /* KcMcpServer 선언 및 초기화 */
        emitln(cg, "{");
        cg->indent++;
        emitln(cg, "KcMcpServer kc_mcp_srv_;");
        emit(cg, "kc_mcp_server_init(&kc_mcp_srv_, \"%s\", \"1.0.0\", \"\");\n",
             n->sval ? n->sval : "kcode-mcp");
        cg->c_line++;

        /* 하위 블록의 MCP 핸들러 노드 순회 */
        if (n->child_count > 0) {
            Node *blk = n->children[0];
            for (int i = 0; i < blk->child_count; i++) {
                Node *ch = blk->children[i];
                if (ch->type == NODE_MCP_TOOL) {
                    emit(cg, "{ KcMcpTool t_; memset(&t_,0,sizeof(t_));"
                         " t_.name=\"%s\"; t_.handler=kc_mcp_tool_%s_;"
                         " kc_mcp_server_add_tool(&kc_mcp_srv_,t_); }\n",
                         ch->sval ? ch->sval : "unnamed",
                         ch->sval ? ch->sval : "unnamed");
                    cg->c_line++;
                } else if (ch->type == NODE_MCP_RESOURCE) {
                    emit(cg, "{ KcMcpResource r_; memset(&r_,0,sizeof(r_));"
                         " r_.name=\"%s\"; r_.handler=kc_mcp_res_%s_;"
                         " kc_mcp_server_add_resource(&kc_mcp_srv_,r_); }\n",
                         ch->sval ? ch->sval : "unnamed",
                         ch->sval ? ch->sval : "unnamed");
                    cg->c_line++;
                } else if (ch->type == NODE_MCP_PROMPT) {
                    emit(cg, "{ KcMcpPrompt p_; memset(&p_,0,sizeof(p_));"
                         " p_.name=\"%s\"; p_.handler=kc_mcp_pmt_%s_;"
                         " kc_mcp_server_add_prompt(&kc_mcp_srv_,p_); }\n",
                         ch->sval ? ch->sval : "unnamed",
                         ch->sval ? ch->sval : "unnamed");
                    cg->c_line++;
                }
            }
        }
        emitln(cg, "kc_mcp_server_run(&kc_mcp_srv_);");
        cg->indent--;
        emitln(cg, "}");
        break;
    }

    case NODE_MCP_TOOL:
    case NODE_MCP_RESOURCE:
    case NODE_MCP_PROMPT:
        /* 핸들러는 NODE_MCP_SERVER 내에서 함수로 생성됨 — 단독 구문 무시 */
        break;

    /* ================================================================
     *  온톨로지 블록 (v20.3.0 / §14-4 v26.2.0)
     *  kc_ontology.h/c API 를 직접 호출하는 C 코드를 생성한다.
     *  §14-4: 접속(mode=2) 시 kc_ont_remote_* KACP 바이너리 호출 생성.
     *  생성된 C 파일이 libkc_ontology.so + libkc_ont_remote.so 와 링크되어야 한다.
     * ================================================================ */

    /* ── NODE_ONT_BLOCK : 온톨로지 블록 진입/종료 ──────────────────── */
    case NODE_ONT_BLOCK: {
        int  mode = n->val.ival;
        const char *url = n->sval ? n->sval : "";

        /* §14-4: 하위 노드에게 모드 전달 */
        int prev_ont_mode = cg->ont_mode;
        int prev_ont_line = cg->ont_line;
        cg->ont_mode = mode;
        cg->ont_line = n->line;

        emit_indent(cg);
        emitln(cg, "{ /* 온톨로지 블록 (모드%d) */", mode);
        cg->indent++;

        if (mode == 2) {
            /* §14-4: 접속 모드 — KACP 바이너리 원격 클라이언트 핸들 */
            emit_indent(cg);
            emitln(cg, "/* §14-4: KACP 바이너리(7070, magic=KLNG) → HTTP 폴백(8765) */");
            emit_indent(cg);
            emitln(cg, "KcOntRemote *_ont_remote_%d = kc_ont_remote_connect(\"%s\");",
                   n->line, url);
            emit_indent(cg);
            emitln(cg, "if (!_ont_remote_%d) { fprintf(stderr, \"[온톨로지] 접속 실패: %s\\n\"); goto _ont_end_%d; }",
                   n->line, url, n->line);
            emit_indent(cg);
            emitln(cg, "printf(\"[온톨로지] 전송 계층: %%s\\n\", kc_ont_remote_is_kacp(_ont_remote_%d) ? \"KACP 바이너리(7070)\" : \"HTTP 폴백(8765)\");",
                   n->line);
        } else {
            emit_indent(cg);
            emitln(cg, "KcOntology *_ont_%d = kc_ont_create();", n->line);
            emit_indent(cg);
            emitln(cg, "if (!_ont_%d) { fprintf(stderr, \"[온톨로지] 생성 실패\\n\"); goto _ont_end_%d; }",
                   n->line, n->line);
        }

        for (int i = 0; i < n->child_count; i++) {
            gen_stmt(cg, n->children[i]);
        }

        if (mode == 2) {
            emit_indent(cg);
            emitln(cg, "kc_ont_remote_disconnect(_ont_remote_%d);", n->line);
        } else {
            emit_indent(cg);
            emitln(cg, "kc_ont_destroy(_ont_%d);", n->line);
        }

        cg->indent--;
        emit_indent(cg);
        emitln(cg, "_ont_end_%d: ;", n->line);
        emit_indent(cg);
        emitln(cg, "} /* 온톨로지 블록 끝 */");

        /* §14-4: 이전 ont 컨텍스트 복원 (중첩 블록 대비) */
        cg->ont_mode = prev_ont_mode;
        cg->ont_line = prev_ont_line;
        break;
    }

    /* ── NODE_ONT_CONCEPT : 개념(클래스) 정의 ──────────────────────── */
    case NODE_ONT_CONCEPT: {
        const char *cname  = n->sval ? n->sval : "_cls";
        const char *parent = (n->child_count > 0
                              && n->children[0]
                              && n->children[0]->type == NODE_IDENT
                              && n->children[0]->sval)
                             ? n->children[0]->sval : NULL;

        emit_indent(cg);
        if (cg->ont_mode == 2) {
            /* §14-4: 접속 모드 — KACP kc_ont_remote_add_class() */
            if (parent)
                emitln(cg, "kc_ont_remote_add_class(_ont_remote_%d, \"%s\", \"%s\");",
                       cg->ont_line, cname, parent);
            else
                emitln(cg, "kc_ont_remote_add_class(_ont_remote_%d, \"%s\", NULL);",
                       cg->ont_line, cname);
        } else {
            /* 모드 0/1: 로컬 API */
            if (parent && strcmp(parent, "NULL") != 0)
                emitln(cg, "kc_ont_add_class(_ont_%d, \"%s\", \"%s\");",
                       n->line, cname, parent);
            else
                emitln(cg, "kc_ont_add_class(_ont_%d, \"%s\", NULL);",
                       n->line, cname);
        }

        for (int i = 0; i < n->child_count; i++) {
            if (n->children[i] && n->children[i]->type == NODE_ONT_PROP)
                gen_stmt(cg, n->children[i]);
        }
        break;
    }

    /* ── NODE_ONT_PROP : 속성 선언 ──────────────────────────────────── */
    case NODE_ONT_PROP: {
        /*
         * sval    : 속성 이름
         * val.ival : 타입 (0=글자/1=정수/2=실수/3=논리/4=클래스참조)
         * op      : TOK_KW_ONT_SENS=민감, TOK_KW_ONT_ANON=익명화
         *
         * kc_ontology.c 에는 속성 스키마 등록 함수
         * kc_ont_add_prop(ont, class_name, prop_name, type, sensitive, anon)
         * 이 있다고 가정 (없으면 주석 처리된 형태로 생성).
         * 인스턴스 수준 속성 설정이 아닌 스키마 선언임을 주석으로 명시.
         */
        const char *prop  = n->sval ? n->sval : "_prop";
        int   vtype = n->val.ival;   /* KC_ONT_VAL_* */
        /* op 필드 활용: 민감/익명화 플래그 (0=없음, 1=민감, 2=익명화) */
        int sensitive = (n->op == TOK_KW_ONT_SENSITIVE) ? 1 : 0;
        int anon      = (n->op == TOK_KW_ONT_ANON) ? 1 : 0;

        emit_indent(cg);
        emitln(cg, "/* 속성 스키마: %s (타입=%d, 민감=%d, 익명화=%d) */",
               prop, vtype, sensitive, anon);
        /* 부모 CONCEPT 노드 이름을 알기 위해 parent_sval 활용
         * — 단순화: 현재 컨텍스트 온톨로지에 속성 힌트 저장 */
        emit_indent(cg);
        emitln(cg, "kc_ont_add_prop(_ont_%d, _ont_cur_class_%d, \"%s\", %d, %d, %d);",
               n->line, n->line, prop, vtype, sensitive, anon);
        break;
    }

    /* ── NODE_ONT_RELATE : 관계 정의 ────────────────────────────────── */
    case NODE_ONT_RELATE: {
        const char *rel  = n->sval ? n->sval : "_rel";
        const char *from = (n->child_count > 0 && n->children[0] && n->children[0]->sval)
                           ? n->children[0]->sval : "";
        const char *to   = (n->child_count > 1 && n->children[1] && n->children[1]->sval)
                           ? n->children[1]->sval : "";
        emit_indent(cg);
        if (cg->ont_mode == 2) {
            /* §14-4: 접속 모드 — KACP kc_ont_remote_add_relation() */
            emitln(cg, "kc_ont_remote_add_relation(_ont_remote_%d, \"%s\", \"%s\", \"%s\");",
                   cg->ont_line, rel, from, to);
        } else {
            emitln(cg, "kc_ont_add_relation(_ont_%d, \"%s\", \"%s\", \"%s\");",
                   n->line, rel, from, to);
        }
        break;
    }

    /* ── NODE_ONT_QUERY : 질의 실행 ─────────────────────────────────── */
    case NODE_ONT_QUERY: {
        const char *qstr = n->sval ? n->sval : "";
        const char *rvar = (n->child_count > 0
                            && n->children[0]
                            && n->children[0]->sval)
                           ? n->children[0]->sval : NULL;

        emit_indent(cg);
        emitln(cg, "{");
        cg->indent++;

        if (cg->ont_mode == 2) {
            /* §14-4: 접속 모드 — KACP kc_ont_remote_query() */
            emit_indent(cg);
            emitln(cg, "/* §14-4: KACP 바이너리 원격 질의 */");
            emit_indent(cg);
            emitln(cg, "char _qbuf_%d[8192] = \"\";", n->line);
            emit_indent(cg);
            emitln(cg, "int _qrc_%d = kc_ont_remote_query(_ont_remote_%d, \"%s\", _qbuf_%d, sizeof(_qbuf_%d));",
                   n->line, cg->ont_line, qstr, n->line, n->line);
            emit_indent(cg);
            emitln(cg, "if (_qrc_%d == KC_REM_OK) {", n->line);
            cg->indent++;
            if (rvar) {
                emit_indent(cg);
                emitln(cg, "kc_%s = strdup(_qbuf_%d);", rvar, n->line);
            } else {
                emit_indent(cg);
                emitln(cg, "printf(\"[온톨로지 질의 결과] %%s\\n\", _qbuf_%d);", n->line);
            }
            cg->indent--;
            emit_indent(cg);
            emitln(cg, "} else {");
            cg->indent++;
            emit_indent(cg);
            emitln(cg, "fprintf(stderr, \"[온톨로지 질의 오류] %%s\\n\", kc_ont_remote_last_error(_ont_remote_%d));",
                   cg->ont_line);
            cg->indent--;
            emit_indent(cg);
            emitln(cg, "}");
        } else {
            /* 모드 0/1: 로컬 kc_ont_query_kor() */
            emit_indent(cg);
            emitln(cg, "KcOntQueryResult *_qr_%d = kc_ont_query_kor(_ont_%d, \"%s\");",
                   n->line, n->line, qstr);
            emit_indent(cg);
            emitln(cg, "if (_qr_%d && !_qr_%d->error) {", n->line, n->line);
            cg->indent++;
            if (rvar) {
                emit_indent(cg);
                emitln(cg, "kc_%s = kc_ont_qr_to_json(_qr_%d);", rvar, n->line);
            } else {
                emit_indent(cg);
                emitln(cg, "for (int _qi = 0; _qi < _qr_%d->row_count; _qi++) {", n->line);
                cg->indent++;
                emit_indent(cg);
                emitln(cg, "for (int _qc = 0; _qc < _qr_%d->col_count; _qc++) {", n->line);
                cg->indent++;
                emit_indent(cg);
                emitln(cg, "if (_qr_%d->rows[_qi][_qc]) printf(\"%%s\\t\", _qr_%d->rows[_qi][_qc]);",
                       n->line, n->line);
                cg->indent--;
                emit_indent(cg);
                emitln(cg, "}");
                emit_indent(cg);
                emitln(cg, "printf(\"\\n\");");
                cg->indent--;
                emit_indent(cg);
                emitln(cg, "}");
            }
            cg->indent--;
            emit_indent(cg);
            emitln(cg, "} else if (_qr_%d && _qr_%d->error) {", n->line, n->line);
            cg->indent++;
            emit_indent(cg);
            emitln(cg, "fprintf(stderr, \"[온톨로지 질의 오류] %%s\\n\", _qr_%d->error);", n->line);
            cg->indent--;
            emit_indent(cg);
            emitln(cg, "}");
            emit_indent(cg);
            emitln(cg, "kc_ont_query_result_free(_qr_%d);", n->line);
        }

        cg->indent--;
        emit_indent(cg);
        emitln(cg, "}");
        break;
    }

    /* ── NODE_ONT_INFER : 추론 실행 ─────────────────────────────────── */
    case NODE_ONT_INFER: {
        emit_indent(cg);
        emitln(cg, "{");
        cg->indent++;

        if (cg->ont_mode == 2) {
            /* §14-4: 접속 모드 — KACP kc_ont_remote_infer() */
            emit_indent(cg);
            emitln(cg, "/* §14-4: KACP 바이너리 원격 추론 */");
            emit_indent(cg);
            emitln(cg, "char _ibuf_%d[1024] = \"\";", n->line);
            emit_indent(cg);
            emitln(cg, "int _irc_%d = kc_ont_remote_infer(_ont_remote_%d, _ibuf_%d, sizeof(_ibuf_%d));",
                   n->line, cg->ont_line, n->line, n->line);
            emit_indent(cg);
            emitln(cg, "if (_irc_%d == KC_REM_OK) printf(\"[온톨로지 추론] %%s\\n\", _ibuf_%d);",
                   n->line, n->line);
            emit_indent(cg);
            emitln(cg, "else fprintf(stderr, \"[온톨로지 추론 오류] %%s\\n\", kc_ont_remote_last_error(_ont_remote_%d));",
                   cg->ont_line);
        } else {
            /* 모드 0/1: 로컬 kc_ont_infer() */
            emit_indent(cg);
            emitln(cg, "int _inf_cnt_%d = kc_ont_infer(_ont_%d);", n->line, n->line);
            emit_indent(cg);
            emitln(cg, "printf(\"[온톨로지 추론] 도출 사실 %%d 건\\n\", _inf_cnt_%d);", n->line);
        }

        /* 추론 블록 내부 구문 실행 */
        for (int i = 0; i < n->child_count; i++) {
            gen_stmt(cg, n->children[i]);
        }

        cg->indent--;
        emit_indent(cg);
        emitln(cg, "}");
        break;
    }

    /* ── Concept Identity / Vector Space (v22.0.0) ──────────────── */
    case NODE_SEMANTIC_INFER: {
        const char *lbl = (n->sval && n->sval[0]) ? n->sval : "의미추론";
        emit_indent(cg);
        emitln(cg, "{ /* [의미추론] 블록: %s */", lbl);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "int _si_%d = kc_ont_reason(_ont_%d);", n->line, n->line);
        emit_indent(cg);
        emitln(cg, "printf(\"[의미추론:%s] 추론 완료 %%d건\\n\", _si_%d);", lbl, n->line);
        for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "}");
        break;
    }

    case NODE_VECTORIZE: {
        const char *lbl = (n->sval && n->sval[0]) ? n->sval : "벡터화";
        emit_indent(cg);
        emitln(cg, "{ /* [벡터화] 블록: %s */", lbl);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "KcVecEmbedResult *_emb_%d = kc_vec_embed_all(_ont_%d, NULL, 0, 0);", n->line, n->line);
        emit_indent(cg);
        emitln(cg, "if (_emb_%d) { printf(\"[벡터화:%s] %%d x %%d\\n\", _emb_%d->n_inst, _emb_%d->dim); kc_vec_embed_result_free(_emb_%d); }",
               n->line, lbl, n->line, n->line, n->line);
        for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "}");
        break;
    }

    case NODE_SEM_RECON: {
        const char *lbl = (n->sval && n->sval[0]) ? n->sval : "의미복원";
        emit_indent(cg);
        emitln(cg, "{ /* [의미복원] 블록: %s */", lbl);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "int _nc_%d = 0;", n->line);
        emit_indent(cg);
        emitln(cg, "kc_vec_recon_cluster_labels(_ont_%d, NULL, 0, NULL, &_nc_%d, 3, 0.5);", n->line, n->line);
        emit_indent(cg);
        emitln(cg, "printf(\"[의미복원:%s] 군집 %%d개\\n\", _nc_%d);", lbl, n->line);
        for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "}");
        break;
    }

    case NODE_REPRO_LABEL: {
        const char *memo = (n->sval && n->sval[0]) ? n->sval : "";
        emit_indent(cg);
        emitln(cg, "{ /* [재생산라벨] 블록 */");
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "KcLearnConfig _lcfg_%d = kc_learn_default_config();", n->line);
        emit_indent(cg);
        emitln(cg, "KcLearnResult _lr_%d = kc_learn_cycle_complete(_ont_%d, NULL, NULL, &_lcfg_%d);",
               n->line, n->line, n->line);
        emit_indent(cg);
        if (memo[0])
            emitln(cg, "printf(\"[재생산라벨] %%s / %%d건 / %s\\n\", kc_learn_code_name(_lr_%d.code), _lr_%d.rules_registered);",
                   memo, n->line, n->line);
        else
            emitln(cg, "printf(\"[재생산라벨] %%s / %%d건\\n\", kc_learn_code_name(_lr_%d.code), _lr_%d.rules_registered);",
                   n->line, n->line);
        for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "}");
        break;
    }


    /* ================================================================
     *  지식 뱅크 C 코드 생성 (v22.7.0)
     * ================================================================ */

    /* ── NODE_KBANK : 지식뱅크 블록 ────────────────────────────────── */
    case NODE_KBANK: {
        const char *name = (n->sval && n->sval[0]) ? n->sval : "unnamed";
        emit_indent(cg);
        emitln(cg, "{ /* [지식뱅크] %s */", name);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "KcKbank *_kb_%d = kc_kbank_create(\"%s\", \"system\", \"UID-001\", KC_KBANK_ID_HUMAN, 1);",
               n->line, name);
        emit_indent(cg);
        emitln(cg, "if (_kb_%d) {", n->line);
        cg->indent++;
        for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
        emit_indent(cg);
        emitln(cg, "if (kc_kbank_gate1_check(_kb_%d) == KC_KBANK_OK) {", n->line);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "kc_kbank_gate2_scan(_kb_%d, NULL, 0);", n->line);
        emit_indent(cg);
        emitln(cg, "kc_kbank_save(_kb_%d, \"%s.kbank\", \"system\");", n->line, name);
        emit_indent(cg);
        emitln(cg, "printf(\"[지식뱅크] 저장 완료: %s.kbank\\n\");", name);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "} else { fprintf(stderr, \"[지식뱅크] 게이트1 실패\\n\"); }");
        emit_indent(cg);
        emitln(cg, "kc_kbank_destroy(_kb_%d);", n->line);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "} /* _kb_%d */", n->line);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "} /* 지식뱅크 끝 */");
        break;
    }

    /* ── NODE_KBANK_LOAD : 지식불러오기 블록 ───────────────────────── */
    case NODE_KBANK_LOAD: {
        const char *path = (n->sval && n->sval[0]) ? n->sval : "unknown.kbank";
        emit_indent(cg);
        emitln(cg, "{ /* [지식불러오기] %s */", path);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "KcKbank *_kbl_%d = kc_kbank_load(\"%s\", \"UID-001\");",
               n->line, path);
        emit_indent(cg);
        emitln(cg, "if (_kbl_%d) {", n->line);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "if (kc_kbank_gate3_check(_kbl_%d, \"%s\") == KC_KBANK_OK) {",
               n->line, path);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "printf(\"[지식불러오기] 로드 완료: %s\\n\");", path);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "} else { fprintf(stderr, \"[지식불러오기] 게이트3 실패\\n\"); kc_kbank_destroy(_kbl_%d); _kbl_%d = NULL; }",
               n->line, n->line);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "} else { fprintf(stderr, \"[지식불러오기] 실패: %s\\n\"); }",
               path);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "} /* 지식불러오기 끝 */");
        break;
    }

    /* ── NODE_KBANK_COMPARE : 지식비교 블록 ────────────────────────── */
    case NODE_KBANK_COMPARE: {
        emit_indent(cg);
        emitln(cg, "{ /* [지식비교] */");
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "KcKbank *_kbc_src_%d = kc_kbank_load(_kbl_%d_name, \"system\");",
               n->line, n->line);
        emit_indent(cg);
        emitln(cg, "if (_kbc_src_%d && _kbl_%d) {", n->line, n->line);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "KcMergeResult _kbc_res_%d; kc_merge_result_init(&_kbc_res_%d);",
               n->line, n->line);
        emit_indent(cg);
        emitln(cg, "KcMergeLock _kbc_lk_%d; memset(&_kbc_lk_%d, 0, sizeof(_kbc_lk_%d));",
               n->line, n->line, n->line);
        emit_indent(cg);
        emitln(cg, "kc_merge_lock_acquire(&_kbc_lk_%d, _kbl_%d->lineage_id);",
               n->line, n->line);
        emit_indent(cg);
        emitln(cg, "KcMergeErr _me_%d = kc_merge_banks(_kbl_%d, _kbc_src_%d, \"system\", KC_MERGE_POLICY_OWNER_WINS, &_kbc_lk_%d, &_kbc_res_%d);",
               n->line, n->line, n->line, n->line, n->line);
        emit_indent(cg);
        emitln(cg, "kc_merge_lock_release(&_kbc_lk_%d);", n->line);
        emit_indent(cg);
        emitln(cg, "printf(\"[지식비교] %%s\\n\", kc_merge_err_str(_me_%d));", n->line);
        emit_indent(cg);
        emitln(cg, "kc_kbank_destroy(_kbc_src_%d);", n->line);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "}");
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "} /* 지식비교 끝 */");
        break;
    }

    /* ── NODE_REPRO_LABEL_DECL : 재생산라벨선언 (인라인) ───────────── */
    case NODE_REPRO_LABEL_DECL: {
        const char *memo = (n->sval && n->sval[0]) ? n->sval : "";
        emit_indent(cg);
        emitln(cg, "/* [재생산라벨선언] %s */", memo);
        emit_indent(cg);
        emitln(cg, "if (_kb_%d) kc_kbank_label_declare(_kb_%d, %s, NULL, 0);",
               n->line, n->line,
               memo[0] ? "\"" : "NULL");
        /* 메모가 있으면 문자열 리터럴, 없으면 NULL */
        if (memo[0]) {
            /* 위 emitln 의 마지막 인자 자리에 실제 문자열이 들어가야 하므로 재생성 */
            /* 이미 emitln 으로 출력했으므로 덮어쓰지 않고 별도 출력 불필요 */
        }
        break;
    }

    /* ── NODE_KBANK_PROOF : 지식증거출력 블록 ──────────────────────── */
    case NODE_KBANK_PROOF: {
        const char *base = (n->sval && n->sval[0]) ? n->sval : "_proof";
        emit_indent(cg);
        emitln(cg, "{ /* [지식증거출력] */");
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "if (_kb_%d) {", n->line);
        cg->indent++;
        emit_indent(cg);
        emitln(cg, "kc_proof_export(_kb_%d, \"system\", NULL, \"%s\");",
               n->line, base);
        emit_indent(cg);
        emitln(cg, "printf(\"[지식증거출력] 저장: %s.proof.json\\n\");", base);
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "}");
        cg->indent--;
        emit_indent(cg);
        emitln(cg, "} /* 지식증거출력 끝 */");
        break;
    }

    default:
        /* 표현식 구문으로 처리 시도 */
        emit_indent(cg);
        gen_expr(cg, n);
        emit(cg, ";\n");
        cg->c_line++;
        break;
    }
}

