# Kcode 웹 IDE
> version : v0.1.0  
> repository : zerojat7-ui/KoreanCode/web

Kcode 언어를 브라우저에서 바로 실행할 수 있는 웹 IDE입니다.  
설치 없이 접속만으로 사용 가능합니다.

---

## 온라인 접속

https://zerojat7-ui.github.io/KoreanCode/

---

## 실행 모드

| 모드 | 조건 | 가능한 기능 |
|------|------|------------|
| **WASM 모드** | 기본 (kserver 없음) | 실행 / 렉싱 / 파싱 / 바이트코드 VM |
| **kserver 모드** | kserver 로컬 실행 중 | WASM 전체 + LLVM IR + C코드 생성 + 최적화 |

---

## 폴더 구조

```
web/
├── index.html           ← IDE 메인 페이지
├── readme.md            ← 이 파일
└── wasm/
    ├── kcode_vm.wasm    ← 브라우저 실행 엔진 (~200KB)
    ├── kcode_vm.js      ← WASM 로더 (Emscripten)
    ├── kcode-wasm.js    ← 웹 IDE 엔진 v16.5.0
    └── kc_ont_sdk.js    ← 온톨로지 Node.js SDK v20.3.0
```

---

## kserver 연동 (선택)

LLVM IR, C코드 생성 등 고급 기능이 필요할 때 로컬에서 실행합니다.

```bash
# 기본 실행 (포트 8080)
./kserver

# 포트 지정
./kserver 3000

# LLVM 컴파일러 사용
./kserver 8080 -llvm
```

kserver 실행 시 웹 IDE가 자동으로 감지하여 연결됩니다.

---

## HTML에서 직접 사용

```html
<script src="/wasm/kcode_vm.js"></script>
<script src="/wasm/kcode-wasm.js"></script>
<script>
  const engine = new KcodeEngine();
  await engine.init();
  const result = await engine.run('출력("안녕, 세계!")');
  console.log(result.output); // 안녕, 세계!
</script>
```

---

## GitHub Pages 설정

```
Settings → Pages
브랜치 : main
폴더   : /web
```

---

*Kcode 웹 IDE v0.1.0*  
*kserver 없이, 브라우저만으로 Kcode가 실행됩니다.*
