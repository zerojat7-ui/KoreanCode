# 개념지식뉴런 (NCkR) 설계 문서
> Concept Knowledge Neuron
> 온톨로지 + K-Neuron 결합 설계
> v3.0.0 — 2026-03-28
> 상태: 설계 완료 / 구현 대기

### 변경 이력
| 버전 | 날짜 | 내용 |
|------|------|------|
| v1.5.0 | 2026-03-27 | 최초 설계 완료 — KcNeuronChunk 32B / link_id 16B / KcNeuronPool 별도 / KcChunkDict 별도 |
| v2.0.0 | 2026-03-28 | 청크 사전 / DRAM 배치 / VRAM 재정의 / 3계층 스토리지 / KACP ID 전송 |
| v3.0.0 | 2026-03-28 | **온톨로지 == NCkR 통합 설계** — 구조 전면 재정의 |

---

## 개념 정의

```
개념지식뉴런 (Concept Knowledge Neuron, NCkR)

  = 온톨로지 (개념 지식 그래프)
  + K-Neuron (시냅스 가소성)
  의 결합

표현 규칙:
  개념 표현:  "개념지식뉴런"  ← 이 문서 및 설계 용어
  코드 표현:  kc_ontology / kc_ckr / kc_neuron  ← 기존 코드 그대로 유지
  파일명:     기존 k 접두어 코드 변경 없음

핵심 정의:
  청크 1개  = 개념지식뉴런 1개
            = 온톨로지 관계 엣지 1개
            = 시냅스 1개
            = 32B 바이너리

  NCkR 전체 = 살아있는 온톨로지
            = 개념들이 시냅스로 연결된 뇌
```

---

## 목차

0. [개념 정의](#개념-정의)
1. [배경 및 목적](#1-배경-및-목적)
2. [핵심 구조체 — KcNeuronChunk](#2-핵심-구조체--kcneuronchunk)
3. [저장 방식 — 컬럼 청크](#3-저장-방식--컬럼-청크)
4. [메모리 구조 — DRAM과 VRAM](#4-메모리-구조--dram과-vram)
5. [KACP 전송 — 방법 B + ColHeader](#5-kacp-전송--방법-b--colheader)
6. [전송 오류 처리](#6-전송-오류-처리)
7. [온톨로지와 LLM 관계](#7-온톨로지와-llm-관계)
8. [BF16 적용 검토](#8-bf16-적용-검토)
9. [속도 비교 수치](#9-속도-비교-수치)
10. [구현 계획](#10-구현-계획)
11. [VRAM 컨텍스트 압축/리셋](#11-vram-컨텍스트-압축--리셋-메커니즘)
12. [판단 구조](#12-nckr-판단-구조)
13. [망각 시스템 — 에빙하우스 곡선](#13-망각-시스템--에빙하우스-곡선)
14. [가변 시냅스](#14-가변-시냅스)
15. [목적 달성 요약](#15-목적-달성-요약)
16. [청크 사전 — KcChunkDict](#16-청크-사전--kcchunkdict)
17. [메모리 배치 확정 — DRAM / VRAM / SSD](#17-메모리-배치-확정--dram--vram--ssd)
18. [KACP 전송 개선 — uint32 ID](#18-kacp-전송-개선--uint32-id)
19. [저장 용량 — 규모별 수치](#19-저장-용량--규모별-수치)
20. [DNA 연동 변경 4가지](#20-dna-연동-변경-4가지)
21. [v3.0.0 — 온톨로지 == NCkR 통합 설계](#21-v300--온톨로지--nckr-통합-설계)
22. [최초 설계 vs v3.0.0 비교](#22-최초-설계-vs-v300-비교)
23. [1000억 시냅스 용량](#23-1000억-시냅스-용량)
24. [이름 제거 + OHT 사전 통합 구조](#24-이름-제거--oht-사전-통합-구조)
25. [시냅스 포함 전체 용량](#25-시냅스-포함-전체-용량)
26. [시냅스 경량화 확정 — 8B + 양방향](#26-시냅스-경량화-확정--8b--양방향)
27. [망각 후 시냅스 추가 — Slack + Compaction](#27-망각-후-시냅스-추가--slack--compaction)
28. [같은 청크를 가리키는 시냅스 구분](#28-같은-청크를-가리키는-시냅스-구분)
29. [순환 시냅스 처리](#29-순환-시냅스-처리)
30. [저장/로드 재설계](#30-저장로드-재설계)
31. [KACP 프로토콜 — NCkR 적용 확정](#31-kacp-프로토콜--nckr-적용-확정)

---

## 1. 배경 및 목적

### 1-0. 개념지식뉴런이란

```
개념지식뉴런 = 온톨로지 × K-Neuron

  온톨로지:
    개념(KcOntClass) + 관계(KcOntRelation) + 규칙(KcOntRule)
    정적 지식 그래프 — 쿼리 → 응답 (수동적)

  K-Neuron:
    시냅스 강도(weight) + 가소성(K-Hebbian)
    동적 신호 전달 — 자극 → 발화 → 연쇄 (능동적)

  결합:
    온톨로지 관계 엣지 1개 = 개념지식뉴런 1개
    관계가 시냅스가 됨 → 살아있는 온톨로지

코드는 기존 그대로:
  kc_ontology.c / kc_ckr.c / kc_neuron.c
  → 변경 없음
  → 개념지식뉴런은 설계/표현 용어
```

### 1-1. 문제 정의

`kont_server.md` 설계 문서는 **32B 바이너리 정렬 + Zero-Copy** 원칙을 요구한다.
현재 `kc_ckr.c` 구현은 아래 세 가지 핵심 문제를 가진다.

```
문제 1 — JSON 직렬화:
  C 구조체 → serialize_class() → JSON 텍스트 → KcCKRChunk 페이로드
  → 파싱 계층 상존, Zero-Copy 불가

문제 2 — AoS → SoA 변환 병목:
  KcNeuronPool (SoA 10개 배열 분산)
  가속기 푸쉬 전 scatter 변환 → O(N) 캐시 미스
  N=10만 시냅스 → 변환 50~200ms 소요

문제 3 — L 게이트 미구현:
  현재 K-Hebbian:  Δw = η × M(m) × K(d)     ← 승인 조건 없음
  설계 문서 요구:  Δw = η × M(m) × K(d) × L ← L=0 이면 차단
  → 미승인 ΔW가 즉시 weights[]에 반영 → 무결성 위반
```

### 1-2. 해결 방향

```
NCkR (Neuron + CKR 결합):

  저장  : AoS 개별 청크 → 컬럼 청크 (SoA와 동일 구조)
  연산  : SoA 유지 (SIMD 최적화 보존)
  전송  : KACP 헤더 1개 + NCkR 컬럼 스트림
  L게이트: KcNeuronChunk.flags[5] 비트 내장
  LLM   : kc_vec_recon 경유 (청크에 텍스트 불필요)
```

---

## 2. 핵심 구조체 — KcNeuronChunk

### 2-1. 구조체 정의

```c
typedef struct __attribute__((aligned(32))) {
    /* [0~7]   출발 노드 link_id 앞 8B */
    uint8_t  from_id[8];    //  8B

    /* [8~15]  도착 노드 link_id 앞 8B */
    uint8_t  to_id[8];      //  8B

    /* [16~19] 현재 연결 강도 */
    float    weight;        //  4B   0.0 ~ 1.0

    /* [20~23] 미승인 ΔW — L 게이트 격리 버퍼 */
    float    delta_w;       //  4B   L=0 대기 / L=1 합산

    /* [24]    극성 */
    uint8_t  polarity;      //  1B   0x01=흥분 / 0xFF=억제

    /* [25]    계보 깊이 */
    uint8_t  lineage_depth; //  1B   감쇠 룩업 테이블 인덱스

    /* [26]    제어 플래그 */
    uint8_t  flags;         //  1B   (비트 레이아웃 하단 참조)

    /* [27]    불응기 활성 */
    uint8_t  refractory_on; //  1B

    /* [28~29] 불응기 지속 */
    uint16_t refractory_ms; //  2B

    /* [30~31] 쿨다운 */
    uint16_t cooldown_ms;   //  2B   L1=50 / L2=100 / L3=200
} KcNeuronChunk;             // 정확히 32B

typedef char kc_neuron_chunk_sz[(sizeof(KcNeuronChunk)==32)?1:-1];
```

### 2-2. flags 비트 레이아웃

```
비트 [7]   : fired        현재 발화 여부
비트 [6]   : refractory   불응기 중 여부
비트 [5]   : L_approved   재생산 라벨 승인 (0=미승인/대기, 1=승인)
비트 [4:3] : owner        KcPoolOwner (L1=01 / L2=10 / L3=11)
비트 [2:0] : reserved     향후 확장

예)
  0b10100010 = fired=1, L_approved=1, owner=L1
  0b00000010 = fired=0, L_approved=0, owner=L1 (ΔW 대기 중)
```

### 2-3. L 게이트 동작

```c
// K-Hebbian 연산 → delta_w 필드 기록
chunk->delta_w = eta * maturity_w * lineage_kd;

// L 게이트 판정
if (chunk->flags & (1 << 5)) {   // L_approved = 1
    chunk->weight  += chunk->delta_w;  // 승인: 합산
    chunk->delta_w  = 0.0f;
} else {
    // 미승인: delta_w 필드에서 격리 대기
    // 별도 버퍼 할당 없음 — delta_w 자체가 격리 버퍼
}
```

---

## 3. 저장 방식 — 컬럼 청크

### 3-1. 왜 컬럼 청크인가

```
AoS 저장 (개별 청크):
  chunk[i] = {weight, delta_w, polarity, lineage, flags, ...}
  → 가속기 푸쉬 시 weight만 추출: 32B 간격 비연속 접근
  → AVX2 gather 명령 필요 (일반 load보다 3~5배 느림)

컬럼 청크 (필드별 연속):
  col_weight[0..N]   연속 float 배열
  col_delta[0..N]    연속 float 배열
  col_flags[0..N]    연속 uint8 배열
  → 가속기 푸쉬: 포인터 직접 전달 (변환 없음)
  → AVX2 일반 load 가능
```

### 3-2. .ckr 파일 구조

```
┌──────────────────────────────────────────┐
│  HEADER   (64B)                          │
│    magic="CKRN" + 버전 + 컬럼 수         │
├──────────────────────────────────────────┤
│  PASSPORT (32B)                          │
│    link_id(16B) + 계보ID + 소유자        │
├──────────────────────────────────────────┤
│  NCkRColHeader (32B) — weight 컬럼       │
│    type="weig" + byte_len + crc32        │
│    + critical=1 + col_index=0            │
│  float weight[N]                         │
├──────────────────────────────────────────┤
│  NCkRColHeader (32B) — delta 컬럼        │
│  float delta_w[N]                        │
├──────────────────────────────────────────┤
│  NCkRColHeader (32B) — flags 컬럼        │
│    critical=1 (L게이트 핵심)             │
│  uint8_t flags[N]                        │
├──────────────────────────────────────────┤
│  NCkRColHeader (32B) — lineage 컬럼      │
│  uint8_t lineage[N]                      │
├──────────────────────────────────────────┤
│  NCkRColHeader (32B) — polarity 컬럼     │
│  uint8_t polarity[N]                     │
├──────────────────────────────────────────┤
│  SEAL (64B)                              │
│    SHA-256 + HMAC                        │
└──────────────────────────────────────────┘
```

### 3-3. NCkRColHeader 구조체

```c
typedef struct __attribute__((aligned(32))) {
    uint8_t  col_type[4];    // "weig"/"delw"/"polr"/"linG"/"flag"
    uint32_t col_index;      // 0,1,2,3,4
    uint32_t byte_len;       // 이 컬럼의 바이트 크기
    uint32_t crc32;          // 이 컬럼의 CRC32
    uint8_t  is_last_col;    // 마지막 컬럼 여부 (0/1)
    uint8_t  flags;          // [7]=critical [6]=retry_ok [5]=use_prev
    uint8_t  pad[10];
} NCkRColHeader;             // 32B ✅
```

### 3-4. 로드 방식

```c
// 파일 → mmap → 포인터 직접 연결 (파싱 없음)
uint8_t *base = mmap(path);

NCkRColHeader *col_hdr = (NCkRColHeader *)(base + offset);
float *pool->weights   = (float *)(col_hdr + 1);   // 포인터 대입만
float *pool->delta_w   = ...;
uint8_t *pool->flags   = ...;

// 가속기 푸쉬: 연속 배열 직접 전달
kc_accel_begin(KC_ACCEL_AUTO, pool->weights, N, NULL, 0);
```

### 3-5. 저장 방식 (SoA → 컬럼 청크)

```c
// SoA → 컬럼 청크: memcpy 1회
memcpy(ckr_col_weight, pool->weights, N * sizeof(float));
memcpy(ckr_col_delta,  pool->delta_w, N * sizeof(float));
memcpy(ckr_col_flags,  pool->flags,   N * sizeof(uint8_t));
// JSON 직렬화 없음
```

---

## 4. 메모리 구조 — DRAM과 VRAM

### 4-1. 확정 배치 원칙

```
VRAM = GPU가 직접 연산하는 숫자만
DRAM = 이름/의미/메타 (GPU 연산 불필요)
```

### 4-2. 계층별 역할

```
DRAM (CPU 메모리)
┌─────────────────────────────────────────────────┐
│  KcChunkDict  (§16 청크 사전)                   │
│    entries[chunk_id]  이름/CRC/lang/decay  32B  │
│    lsh_table[chunk_id] LSH 256bit          32B  │
│    hash_slots          CRC → ID 버킷        8B  │
│    reverse[chunk_id]   이름 포인터          8B  │
│                                                 │
│  역할: 이름↔ID 변환, LSH 의미 검색             │
│  크기: 88B/개념 × 100만 = 88MB                 │
│  KACP 인터페이스: uint32 ID 발급/역조회        │
└─────────────────────────────────────────────────┘
           │  chunk_id (uint32, 4B)  1:1 인덱싱
           ▼
VRAM (GPU 메모리)
┌─────────────────────────────────────────────────┐
│  노드 상태 SoA  [chunk_id 인덱스]               │
│    node_threshold[]  float   발화 임계값         │
│    node_input_sum[]  float   수신 신호 합산       │
│    node_fired[]      uint8   발화 여부            │
│    node_maturity[]   float   성숙도               │
│    node_lineage[]    uint8   계보 깊이             │
│    소계: 14B/개념                                │
├─────────────────────────────────────────────────┤
│  엣지(시냅스) SoA  [시냅스 인덱스]              │
│    from_id[]   uint32  4B  ← 현재 16B 교체      │
│    to_id[]     uint32  4B  ← 현재 16B 교체      │
│    weight[]    BF16    2B                        │
│    delta_w[]   FP32    4B                        │
│    flags[]     uint8   1B                        │
│    polarity[]  int8    1B                        │
│    lineage[]   uint8   1B                        │
│    decay[]     uint8   1B                        │
│    소계: 18B/시냅스  (기존 48B 대비 62% 절감)   │
└─────────────────────────────────────────────────┘
```

### 4-3. 노드(청크)와 엣지(시냅스) 관계

```
KcChunkDict[4576] = "고양이"  ← DRAM 노드
KcNeuronState[4576]           ← VRAM 노드 상태  (chunk_id 동일 인덱스)

KcNeuronChunk[i]  from=4576 → to=3201  weight=0.92  ← VRAM 엣지
KcNeuronChunk[j]  from=4576 → to=1024  weight=0.85  ← VRAM 엣지
KcNeuronChunk[k]  from=4576 → to=7823  weight=0.71  ← VRAM 엣지

핵심:
  chunk_id = 뉴런 ID = 사전 인덱스 = VRAM 노드 인덱스
  포인터 추적 없음 — 배열 직접 접근
```

### 4-4. 발화 파이프라인

```
[DRAM] "고양이" → dict 조회 → chunk_id=4576     0.00005ms
[VRAM] node_fired[4576] = 1                      0.001ms
[VRAM GPU 병렬] 시냅스 신호 전파 (숫자만)
[VRAM GPU] K-Hebbian weight 갱신
[DRAM] 출력 시만: dict_reverse[id] → "동물"...  0.00005ms

→ 이름 변환: 입력/출력 경계에서 딱 2번
→ 중간 모든 연산: uint32 + float 숫자만
```

### 4-5. LSH 배치

```
LSH = DRAM (KcChunkDict 내)
  의미 유사 검색 = CPU 담당
  해밍거리 POPCNT = CPU SIMD

예외: 창의적 확산 발화 시
  GPU 해밍거리 필요 → LSH 컬럼 VRAM 선택 추가
  (평상시 불필요)
```

### 4-6. 환경별 동작

| 환경 | DRAM/VRAM | DMA 필요 | Zero-Copy |
|------|----------|---------|----------|
| CPU only | DRAM만 | 없음 | ✅ |
| 일반 GPU (CUDA) | 분리 | **필수** | ❌ |
| Apple M시리즈 | 통합 | 없음 | ✅ |
| Qualcomm NPU | 분리 | **필수** | ❌ |



## 5. KACP 전송 — 방법 B + ColHeader

### 5-1. 기존 방식의 문제

```
현재 (컬럼당 KACP 헤더 1개):
  헤더(64B) + 컬럼데이터  ← 5개 반복
  → KACP 헤더 5개
  → TCP 세션 5회 왕복 가능
  → KACP 우선순위 큐 부하 5배

패킷 N개 방식 (원래 AoS):
  헤더(64B) + 청크(32B) + JSON(49B) = 145B × 10,000개
  → 패킷 10,000개
  → KACP 큐 포화
```

### 5-2. 방법 B — 헤더 1개 + 컬럼 스트림

```
KACPHeader (64B)              ← 헤더 1개
NCkRDirectory (32B)           ← 컬럼 목차 (전체 CRC32)
NCkRColHeader (32B) + weight[N]   ← 컬럼 0 (critical)
NCkRColHeader (32B) + flags[N]    ← 컬럼 1 (critical)
NCkRColHeader (32B) + delta_w[N]  ← 컬럼 2 (non-critical)
NCkRColHeader (32B) + lineage[N]  ← 컬럼 3 (non-critical)
NCkRColHeader (32B) + polarity[N] ← 컬럼 4 (non-critical)
```

### 5-3. NCkRDirectory 구조체

```c
typedef struct __attribute__((aligned(32))) {
    uint8_t  col_count;       // 1B  전체 컬럼 수
    uint8_t  flags;           // 1B  [retry_ok|partial_ok|...]
    uint16_t version;         // 2B  NCkR 버전
    uint32_t total_byte_len;  // 4B  전체 데이터 바이트
    uint32_t global_crc32;    // 4B  전체 데이터 CRC32
    uint32_t synapse_count;   // 4B  시냅스 수 N
    uint8_t  reserved[16];    // 16B 향후 확장
} NCkRDirectory;              // 32B ✅
```

### 5-4. KACP flags 활용

```
KACPHeader.flags:
  KACP_FLAG_TRANSFER_CHUNK (0x02): 스트림 전송 중
  KACP_FLAG_LAST_CHUNK     (0x04): 마지막 청크

KACPHeader.data_blocks:
  uint8_t → 최대 255 × 32B = 8,160B 직접 기술
  초과 시: NCkRDirectory.total_byte_len으로 실제 크기 참조
```

---

## 6. 전송 오류 처리

### 6-1. 오류 발생 지점별 위험도

```
Case 1 — 헤더(64B) 손상:
  위험도: 높음
  감지: magic 불일치 → Early Reject
  처리: 전체 재전송 요청

Case 2 — Directory(32B) 손상:
  위험도: 높음
  감지: global_crc32 불일치
  처리: 전체 재전송 요청

Case 3 — critical 컬럼 중간 손상:
  대상: weight, flags (L게이트 핵심)
  위험도: 매우 높음
  감지: NCkRColHeader.crc32 불일치
  처리: KACP_ERR_BAD_BLOCK 반환 + 전체 재전송

Case 4 — non-critical 컬럼 손상:
  대상: delta_w, lineage, polarity
  위험도: 낮음
  감지: NCkRColHeader.crc32 불일치
  처리: 기존값 유지 + 다음 sync 대기
        flags[5]=use_prev → 이전 컬럼값 사용
```

### 6-2. NCkRColHeader flags 비트

```
비트 [7] : critical   0=재전송 필수 / 1=손실 허용
비트 [6] : retry_ok   재전송 요청 가능 여부
비트 [5] : use_prev   오류 시 이전값 유지
비트 [4:0]: reserved

컬럼별 설정:
  weight   → flags = 0b00000000  (critical, 재전송 필수)
  flags    → flags = 0b00000000  (critical, L게이트 핵심)
  delta_w  → flags = 0b10100000  (non-critical, use_prev)
  lineage  → flags = 0b10100000  (non-critical, use_prev)
  polarity → flags = 0b10100000  (non-critical, use_prev)
```

### 6-3. 오류 처리 흐름

```
송신                              수신
  │                                 │
  ├─ Header(64B) + Dir(32B) ──────▶ │ global_crc 검증
  │                                 ├─ ACK ──────────▶
  │                                 │
  ├─ ColHdr(32B) + weight[N] ──────▶│ col_crc 검증
  │                                 ├─ ACK ──────────▶ (정상)
  │                                 ├─ NAK(critical) ─▶ (손상 → 전체 재전송)
  │                                 │
  ├─ ColHdr(32B) + delta_w[N] ─────▶│ col_crc 검증
  │                                 ├─ ACK ──────────▶ (정상)
  │                                 ├─ NAK(non-crit) ─▶ (손상 → 기존값 유지)
  │                                 │
  └─ ... (마지막 컬럼 is_last=1) ──▶│
                                    ├─ ACK(COMPLETE) ──▶
```

### 6-4. sequence 불일치 처리

```
KACPHeader.sequence:
  재전송 시 동일 sequence 유지
  수신측: sequence_last 와 비교
  중복 수신 → KACP_ERR_REPLAY + 폐기
  순서 역전 → 버퍼링 후 재조립
```

---

## 7. 온톨로지와 LLM 관계

### 7-1. 온톨로지의 본질

```
온톨로지 = 시스템이 "참"으로 인정하는 지식의 원본

  인간이 정의:  개념/관계/제약 (텍스트)
  LLM이 접근:  vec_cache 벡터 경유 (직접 읽기 ❌)
  엔진이 연산:  link_id 바이너리 경유
  무결성 보장:  Gate 검증 + SHA-256 봉인
```

### 7-2. NCkR과 LLM의 관계

```
NCkR 청크 32B (바이너리)
        │
        │ kc_vec_recon_from_vec()  ← 이미 구현됨
        ▼
KcReconResult {
    matches[].inst_id    → "기계A001"   ← LLM이 읽을 수 있음
    matches[].class_name → "기계"
    matches[].score      → 0.94
    summary              → "기계 개념과 0.94 유사"
}

→ 청크에 텍스트/좌표 불필요
→ vec_cache + kc_vec_recon이 이미 번역기 역할
→ 현재 레이어 분리 설계 그대로 유지
```

### 7-3. 레이어 분리 원칙

| 용도 | 포맷 | 담당 |
|------|------|------|
| 엔진 저장/전송 | NCkR 32B 컬럼 청크 | kc_ckr.c |
| LLM 이해/추론 | vec_cache → kc_vec_recon | kc_vec_recon.c |
| 인간 읽기 | JSON-LD / Turtle | kc_ont_sdk.py/js |

---

## 8. BF16 적용 검토

### 8-1. 포맷별 정밀도 특성

```
포맷  | 지수 | 가수  | 유효자릿수 | 범위          | 크기
──────────────────────────────────────────────────────
FP64  |  11  |  52b  |   ~15자리  | ±1.8×10^308  |  8B  ← 현재
FP32  |   8  |  23b  |    ~7자리  | ±3.4×10^38   |  4B  ← NCkR 기본
BF16  |   8  |   7b  |    ~3자리  | ±3.4×10^38   |  2B  ← 검토 대상
FP16  |   5  |  10b  |    ~3자리  | ±65504        |  2B  ← 범위 제한으로 부적합
INT8  |   -  |   -   |   0~255    | 정수만         |  1B  ← 양자화 전용
```

**BF16 핵심 특성**: 지수부가 FP32와 동일(8비트) → 범위 동일, 정밀도만 낮음

---

### 8-2. 컬럼별 BF16 적용 판정

| 컬럼 | 범위 | 권장 포맷 | 크기 | 판정 | 이유 |
|------|------|---------|------|------|------|
| weight | 0.0~1.0 | **BF16** | 2B | ✅ | L 게이트로 누적오차 방지 |
| delta_w | 0.0001~0.01 | **FP32 유지** | 4B | ⚠️ | 소량 ΔW 정밀도 필요 |
| flags | 비트 필드 | **INT8** | 1B | ✅ | 변환 불필요 |
| lineage | 0~15 정수 | **INT8** | 1B | ✅ | 변환 불필요 |
| polarity | +1/-1 | **INT8** | 1B | ✅ | 변환 불필요 |
| from_id | 바이너리 | **INT8** | 8B | ✅ | 변환 불필요 |
| to_id | 바이너리 | **INT8** | 8B | ✅ | 변환 불필요 |

---

### 8-3. BF16 정밀도 검증 결과

```
weight 컬럼 (0.0~1.0):
  BF16 최대 오차: 0.003750  (0.375%)
  BF16 평균 오차: 0.001236

  Hebbian Δw 범위:
    η(0.01) × M(0.1~1.0) × K(0.1~1.0) = 0.0001~0.01
    BF16 정밀도(~0.004) > Δw 최솟값(0.0001)
    → 소량 갱신 시 BF16으로 직접 반영 불가

  1000회 누적 시뮬레이션:
    FP32 최종: 1.000000
    BF16 최종: 1.000000
    누적 오차: 0.000000%  ← 클램핑(0~1) 덕분에 수렴

delta_w 컬럼 오차:
  Δw=0.0001 → BF16 오차 0.3%  ✅
  Δw=0.001  → BF16 오차 0.1%  ✅
  Δw=0.01   → BF16 오차 0.5%  ✅
```

---

### 8-4. 누적 오차 해결 — L 게이트 연동

```
문제:
  Δw = 0.0001 (소량)
  BF16 최소 표현 단위 ≈ 0.004
  → BF16 weight에 직접 더하면 소량 갱신 소멸

해결 — Hybrid 방식:
  delta_w (FP32): 소량 ΔW 정밀 누적
  weight  (BF16): L 게이트 승인 시에만 갱신

코드:
  // FP32 delta에 정밀 누적
  delta_w_fp32[i] += eta * Mm * Kd;

  // L 게이트 승인 시 BF16 weight에 반영
  if (flags[i] & (1<<5)) {   // L_approved
      float cur = bf16_to_fp32(weight_bf16[i]);
      weight_bf16[i] = fp32_to_bf16(cur + delta_w_fp32[i]);
      delta_w_fp32[i] = 0.0f;
  }
  // 미승인: delta_w에 계속 누적 → L 게이트가 누적 오차 방지

효과:
  BF16 weight → VRAM 절반 + Tensor Core 2배
  FP32 delta  → 정밀도 완전 유지
  L 게이트    → 누적 오차 자동 방지 (격리 후 한 번에 반영)
```

---

### 8-5. VRAM 및 GPU 성능 비교 (N=100,000 시냅스)

| 설정 | VRAM | GPU 연산 | Tensor Core | 현재 대비 |
|------|------|---------|------------|---------|
| **현재 FP64** | 6,934 KB | 0.0154 μs | ❌ 미사용 | 기준 |
| **NCkR FP32** | 2,637 KB | 0.0010 μs | ✅ FP32 | VRAM 2.6x, 연산 16x |
| **NCkR BF16 hybrid** | 2,246 KB | 0.0005 μs | ✅ BF16 | VRAM 3.1x, 연산 32x |

```
GPU Tensor Core TFLOPS:
  FP64:  19.5 TFLOPS  (A100) ← 현재
  FP32: 312.0 TFLOPS  (A100) ← NCkR FP32
  BF16: 624.0 TFLOPS  (A100) ← NCkR BF16 hybrid
```

---

### 8-6. VRAM 기억 용량 비교

| GPU | 현재 FP64 | NCkR FP32 | NCkR BF16 | 최대 배율 |
|-----|---------|---------|---------|---------|
| RTX 3080 10GB | 121M 시냅스 | 318M | 373M | **3.1배** |
| RTX 4090 24GB | 290M 시냅스 | 764M | 895M | **3.1배** |
| A100 80GB | 968M 시냅스 | 2,545M | 2,984M | **3.1배** |

---

### 8-7. 구현 단계

```
1단계 — NCkR FP32 (즉시):
  모든 컬럼 FP32/INT8
  현재 대비: VRAM 2.6배, 연산 16배
  위험도: 없음

2단계 — weight BF16 전환 (검증 후):
  weight 컬럼만 BF16으로 전환
  delta_w는 FP32 유지
  L 게이트 연동 필수
  추가 효과: VRAM 3.1배, 연산 32배
  위험도: 낮음 (L 게이트가 오차 방지)

3단계 — INT8 양자화 (추론 전용, 장기):
  weight를 INT8로 양자화 (0~255 스케일링)
  학습 시에는 FP32/BF16 유지
  추론 시에만 INT8 적용
  위험도: 중간 (역양자화 오차 관리 필요)
```

---

## 9. 속도 비교 수치

### 8-1. 저장 용량 (N=10,000 시냅스)

| 방식 | 크기 | 비고 |
|------|------|------|
| 현재 (JSON 직렬화) | ~1,414 KB | 청크헤더 + JSON |
| NCkR (컬럼 청크) | ~108 KB | 순수 바이너리 |
| **절감** | **13.1배** | |

### 8-2. KACP 로드/언로드 (N=10,000, 포트 7070)

| 단계 | 현재 | NCkR | 배율 |
|------|------|------|------|
| 직렬화+역직렬화 | 15.92ms | 0.02ms | **796x** |
| 패킷 조립 | 8.23ms | 0.34ms | **24x** |
| SHA-256 봉인 | 0.50ms | 0.08ms | 6x |
| **CPU 합계** | **38.48ms** | **0.46ms** | **83x** |
| 전송 크기 | 1,414KB | 108KB | 13x |
| 1Gbps 전송 | 11.59ms | 0.88ms | 13x |
| **전체 파이프라인** | **50.07ms** | **1.35ms** | **37x** |
| KACP 패킷 수 | 10,000개 | **5개** | 2,000x |

### 8-3. 메모리 (N=1,000 시냅스)

| 항목 | 현재 SoA | NCkR | 변화 |
|------|---------|------|------|
| RAM | ~55KB (10회 malloc) | **32KB** (1회) | ▼ 42% |
| 파일 | ~182KB (JSON) | **32KB** | ▼ 82% |
| malloc 횟수 | 10회 | 1회 | ▼ 90% |

### 8-4. AoS→SoA 변환 제거 효과

```
현재 (AoS scatter):
  N=100,000 시냅스 × 32B 간격 비연속 접근
  변환 시간 ≈ 50~200ms
  AVX2 gather 명령 필요 (3~5배 느림)

NCkR (컬럼 직접):
  weight 컬럼 400KB → memcpy 1회
  변환 시간 ≈ 0.3ms
  AVX2 일반 load 가능
  → 100~600배 빠름
```

---

## 10. 구현 계획

### 9-1. 신규 구조체

| 구조체 | 크기 | 파일 |
|--------|------|------|
| `KcNeuronChunk` | 32B | `kc_ckr.h` 추가 |
| `NCkRColHeader` | 32B | `kc_ckr.h` 추가 |
| `NCkRDirectory` | 32B | `kc_ckr.h` 추가 |

### 10-2. 단계별 작업

**1단계 — NCkR FP32 (기본)**

| 단계 | 내용 | 파일 | 상태 |
|------|------|------|------|
| 1 | `KcNeuronChunk` / `NCkRColHeader` / `NCkRDirectory` 구조체 정의 | `kc_ckr.h` | 🔲 |
| 2 | `.ckr` 파일 섹션 재설계 (HEADER/PASSPORT/ColHeader×5/SEAL) | `kc_ckr.h` | 🔲 |
| 3 | `kc_ckr_save()` — JSON 직렬화 제거 → 컬럼 memcpy | `kc_ckr.c` | 🔲 |
| 4 | `kc_ckr_load()` — JSON 파싱 제거 → mmap Zero-Copy | `kc_ckr.c` | 🔲 |
| 5 | L 게이트 — `flags[5]` + `delta_w` FP32 Hebbian 연동 | `kc_neuron.c` | 🔲 |
| 6 | SoA ↔ 컬럼 변환 함수 | `kc_neuron.h/.c` | 🔲 |
| 7 | KACP 방법 B — 헤더 1개 + NCkRDirectory + 컬럼 스트림 | `kacp.h` | 🔲 |
| 8 | 컬럼별 CRC32 + critical/non-critical 오류 처리 | `kc_ckr.c` | 🔲 |
| 9 | 기존 JSON 직렬화 코드 제거 (`serialize_class` 등) | `kc_ckr.c` | 🔲 |
| 10 | 테스트 (`kc_ckr_test.c` 갱신) | `kc_ckr_test.c` | 🔲 |

**2단계 — BF16 Hybrid (FP32 검증 후)**

| 단계 | 내용 | 파일 | 상태 |
|------|------|------|------|
| 11 | `weight` 컬럼 FP32→BF16 전환 | `kc_ckr.h/.c` | 🔲 |
| 12 | `fp32_to_bf16` / `bf16_to_fp32` 변환 함수 | `kc_ckr.h` | 🔲 |
| 13 | L 게이트 연동 — FP32 delta 누적 후 BF16 weight 반영 | `kc_neuron.c` | 🔲 |
| 14 | BF16 누적 오차 검증 테스트 (1000회 Hebbian) | `kc_ckr_test.c` | 🔲 |

**3단계 — INT8 양자화 (추론 전용, 장기)**

| 단계 | 내용 | 파일 | 상태 |
|------|------|------|------|
| 15 | weight INT8 양자화 (학습=FP32, 추론=INT8) | `kc_neuron.c` | 🔲 |
| 16 | 역양자화 오차 관리 | `kc_neuron.c` | 🔲 |

### 10-3. 수정 대상 파일

| 파일 | 수정 내용 |
|------|---------|
| `kc_ckr.h` | 신규 구조체 3종 추가, 파일 섹션 상수 |
| `kc_ckr.c` | save/load 전면 재작성 (JSON→컬럼 바이너리) |
| `kc_ckr_cache.h/.c` | 컬럼 캐시 슬롯 구조 업데이트 |
| `kc_neuron.h` | SoA↔컬럼 변환 API 선언 |
| `kc_neuron.c` | `kc_neuron_hebbian()` L 게이트 조건 추가 |
| `kacp.h` | NCkRDirectory / 방법 B 패킷 구조 추가 |

### 10-4. 구현하지 않는 것

| 항목 | 이유 |
|------|------|
| 청크에 텍스트/좌표 추가 | vec_cache + kc_vec_recon이 담당 |
| kcode_server.md NCkR 적용 | HTTP = CPU 문자열 연산, 대상 아님 |
| GPU Zero-Copy | 분리 메모리 환경에서 DMA 불가피 |
| ONNX 완전 제거 | 텐서 연산 백엔드는 ONNX 폴백 유지 |

---

---

## 11. VRAM 컨텍스트 압축 / 리셋 메커니즘

### 11-1. 문제 정의

```
VRAM 한계 도달 시:

  단순 삭제 → 대화/작업 맥락 소멸
  전부 유지 → VRAM 부족

  필요한 것:
    리셋 전 중요도 기반 압축
    압축본 VRAM 일부 보존
    새 데이터와 연결하여 맥락 유지
```

### 11-2. VRAM 한계 수치 (NCkR BF16 hybrid 기준)

```
시냅스당 메모리: weight(2B)+delta(4B)+flags(1B)+lineage(1B)+polarity(1B)+from(8B)+to(8B) = 25B

GPU            VRAM 사용가능(80%)   최대 시냅스 수
─────────────────────────────────────────────────
RTX 3080 10GB  8.0 GB              344M개
RTX 4090 24GB  19.2 GB             825M개
A100    80GB   64.0 GB             2,749M개
```

### 11-3. 압축 트리거 및 3단계 흐름

```
[트리거] VRAM 80% 도달
         ↓
[1단계] 중요도 score 계산 (GPU 병렬)

  score[i] = weight[i]
           × recency(last_fired_ms[i])
           × (1.0 / (lineage_depth[i] + 1))
           × L_approved_bit(flags[i])

  weight 높을수록       → 강한 연결 = 중요
  last_fired 최근일수록 → 최근 사용 = 중요
  lineage_depth 얕을수록→ 핵심 개념 = 중요
  L_approved = 1        → 검증된 지식 = 중요

         ↓
[2단계] 상위/하위 분리

  상위 10% (핵심)
    → NCkRCompressed 변환 (100개 → 1개)
    → VRAM 보존

  하위 90% (오래된/약한 연결)
    → CKR 파일로 오프로드
    → DRAM mmap 보관

         ↓
[3단계] VRAM 리셋 후 재시작

  NCkRCompressed[] + 새 입력으로 재시작
  맥락 필요 시 ckr_offset → DRAM 페이징
```

### 11-4. NCkRCompressed 구조체

```c
// 시냅스 100개 → 대표 1개로 압축 (32B 유지)
typedef struct __attribute__((aligned(32))) {
    uint8_t  centroid_from[8]; //  8B  대표 from_id (중심점)
    uint8_t  centroid_to[8];   //  8B  대표 to_id
    float    avg_weight;       //  4B  평균 weight
    float    importance;       //  4B  중요도 점수
    uint16_t syn_count;        //  2B  압축된 시냅스 수
    uint8_t  depth_min;        //  1B  최소 계보 깊이
    uint8_t  flags;            //  1B  압축 플래그
    uint32_t ckr_offset;       //  4B  원본 CKR 파일 오프셋
} NCkRCompressed;              // 32B ✅

typedef char nckr_compressed_sz[(sizeof(NCkRCompressed)==32)?1:-1];
```

### 11-5. VRAM 레이아웃 (리셋 후)

```
VRAM (RTX 4090 24GB 예시)
┌──────────────────────────────────────┐  0%
│  LLM 가중치 / KV Cache               │  고정 (건드리지 않음)
├──────────────────────────────────────┤ 60%
│  NCkRCompressed[]                    │  이전 맥락 압축본
│  = 상위 10% 시냅스 → 100:1 압축      │  ~1.2MB / 50,000 그룹
├──────────────────────────────────────┤ 65%
│  현재 작업 시냅스                     │  새 입력 데이터
│  (리셋 후 새로 채워짐)                │
└──────────────────────────────────────┘ 80% (여유 20% 유지)

DRAM (mmap)
├── 전체 .ckr 파일                     오프로드된 90% 시냅스
└── NCkRCompressed.ckr_offset 경유     필요 시 페이징
```

### 11-6. 압축 비율 및 보존량

| 보존 비율 | 시냅스 수 (원본 100만개 기준) | VRAM 점유 |
|---------|--------------------------|---------|
| 상위 5% | 50,000개 그룹 | ~1.2 MB |
| 상위 10% | 100,000개 그룹 | ~2.4 MB |
| 상위 20% | 200,000개 그룹 | ~4.8 MB |

```
압축 비율: 100:1 (시냅스 100개 → NCkRCompressed 1개)
맥락 손실: 90% 오프로드 but CKR에서 복원 가능
페이징 비용: DRAM mmap → μs 단위
맥락 복원: ckr_offset → CKR 파일 직접 접근
```

### 11-7. 맥락 복원 흐름

```c
// 리셋 후 맥락 필요 시
NCkRCompressed *comp = &compressed_pool[i];

if (need_detail) {
    // CKR 파일에서 원본 시냅스 페이징
    KcNeuronChunk *original =
        (KcNeuronChunk *)(ckr_mmap_base + comp->ckr_offset);

    // 필요한 시냅스만 VRAM으로 로드
    kc_accel_begin(KC_ACCEL_AUTO,
        original->weight, comp->syn_count, NULL, 0);
} else {
    // 압축본의 avg_weight / centroid로 근사 처리
    use_compressed_approximation(comp);
}
```

### 11-8. 구현 단계

| 단계 | 내용 | 파일 | 상태 |
|------|------|------|------|
| 17 | `NCkRCompressed` 구조체 정의 | `kc_ckr.h` | 🔲 |
| 18 | 중요도 `score[]` 계산 함수 | `kc_neuron.c` | 🔲 |
| 19 | 상위 N% 선택 + NCkRCompressed 변환 | `kc_ckr.c` | 🔲 |
| 20 | 하위 90% CKR 파일 오프로드 | `kc_ckr.c` | 🔲 |
| 21 | VRAM 리셋 + 압축본 재배치 | `kc_accel.h` | 🔲 |
| 22 | ckr_offset 기반 DRAM 페이징 | `kc_ckr.c` | 🔲 |
| 23 | 압축 트리거 (VRAM 80% 감지) | `kc_neuron.c` | 🔲 |

---

---

## 12. NCkR 판단 구조

### 12-1. 전체 판단 계층

```
입력 신호
    ↓
[L1] 시냅스 물리 판단   — NCkR 청크 단위
[L2] 발화 패턴 판단    — K-Neuron 네트워크 단위
[L3] 의미 검증 판단    — 온톨로지 Gate 1~4
[L4] 창의성/암 판단    — NCkRVerdict (신규)
[L5] 승인 판단         — L 게이트 + 중앙 서버
    ↓
반영 or 격리
```

---

### 12-2. L1 — 시냅스 물리 판단

```c
// 발화 여부 결정
signal_sum = weight[i] × polarity[i] × input[i] × decay_lut[lineage[i]]

if (refractory_on[i])       → 강제 침묵 (불응기)
else if (signal_sum > 0.5f) → 발화: flags[i] |= (1<<7)
else                        → 침묵: flags[i] &= ~(1<<7)
```

**판단 기준**: `weight × polarity × decay > threshold`

---

### 12-3. L2 — 발화 패턴 판단

```
정상 패턴:
  A → B → C  (선형 연쇄, 예측 가능)

창의 패턴:
  A ──약한 연결──▶ D ──강한──▶ G
  평소 비활성 경로가 낮은 임계값에서 개통

암세포 패턴:
  A → B → C → A  (순환, 자기강화)
  끝없이 에너지 소비 → energy_budget 급감

판단 방법:
  fired_indices 궤적 추적
  동일 청크 n회 이상 반복 발화 → 순환 감지
  energy_budget 감소율 급변 → 암 의심
```

---

### 12-4. L4 — 창의성 vs 암세포 판단

**구별 기준 3가지**

```
[1] 수렴성 (Convergence)
  창의 경로: 새 연결 → 안정화 → 수렴  (weight 변화율 감소)
  암   경로: 연결 → 더 확산 → 더 확산 (weight 변화율 증가)

[2] 에너지 효율 (Energy Efficiency)
  창의 경로: energy 소비 → 새 연결 생산
  암   경로: energy 소비 → 반복만 (새 연결 없음)
  판단: novelty_yield / energy_spent 비율

[3] 다양성 (Novelty)
  창의 경로: 새로운 from-to 조합 발견
  반복 경로: 기존 강한 연결만 재강화
  판단: 신규 링크 수 / 전체 발화 수 = novelty_ratio
```

---

### 12-5. NCkRVerdict 구조체

```c
typedef struct __attribute__((aligned(32))) {
    /* [0~7] 수렴성 판단 */
    float    weight_change_rate;  //  4B  가중치 변화율 (감소=창의)
    float    convergence_score;   //  4B  수렴 점수 0.0~1.0

    /* [8~15] 에너지 효율 판단 */
    float    energy_spent;        //  4B  소비 에너지
    float    novelty_yield;       //  4B  신규 연결 생산량

    /* [16~19] 다양성 판단 */
    uint16_t new_links;           //  2B  새 from-to 쌍 수
    uint16_t total_fires;         //  2B  총 발화 수
    float    novelty_ratio;       //  4B  신규 비율 (new_links/total_fires)

    /* [20~23] 종합 판정 결과 */
    uint8_t  verdict;             //  1B  판정 코드 (하단 참조)
    uint8_t  cancer_score;        //  1B  암세포 위험도 0~255
    uint8_t  creative_score;      //  1B  창의성 점수 0~255
    uint8_t  action;              //  1B  조치 코드 (하단 참조)

    /* [24~27] 사이클 카운터 */
    uint32_t cycle_count;         //  4B  판단 누적 사이클

    /* [28~31] 예약 */
    uint8_t  reserved[4];
} NCkRVerdict;                    // 32B ✅

typedef char nckr_verdict_sz[(sizeof(NCkRVerdict)==32)?1:-1];

/* verdict 코드 */
#define NCKR_VERDICT_NORMAL     0x00  // 정상 학습
#define NCKR_VERDICT_CREATIVE   0x01  // 창의적 발화 → 강화
#define NCKR_VERDICT_NOISE      0x02  // 잡음 → 무시
#define NCKR_VERDICT_SUSPECT    0x03  // 암 의심 → Shadow Mirror 감시
#define NCKR_VERDICT_CANCER     0x04  // 암세포 확정 → 격리
#define NCKR_VERDICT_DEAD       0x05  // 아포토시스 → 제거

/* action 코드 */
#define NCKR_ACTION_REINFORCE   0x01  // K-Hebbian 강화
#define NCKR_ACTION_WEAKEN      0x02  // 가중치 약화
#define NCKR_ACTION_ISOLATE     0x03  // Shadow Node 격리
#define NCKR_ACTION_PRUNE       0x04  // 시냅스 제거
#define NCKR_ACTION_PROMOTE     0x05  // Gate 통과 후 승격
#define NCKR_ACTION_KILL        0x06  // 아포토시스 실행
```

---

### 12-6. 전체 판단 흐름

```
NCkR 청크 발화
      ↓
[L1] signal_sum > threshold?
      NO  → 침묵
      YES ↓
[L2] 패턴 분류
      순환 감지 → cancer_score ++
      에너지 낭비 → cancer_score ++
      신규 링크 생성 → creative_score ++
      ↓
[판정]
  creative_score 높음 + cancer_score 낮음
    → NCKR_VERDICT_CREATIVE
    → NCKR_ACTION_REINFORCE  (K-Hebbian 강화)
    → Shadow Node 등록        (새 개념 조합)
    → [L3] Gate 1~4 검증 대기

  cancer_score 높음
    → NCKR_VERDICT_CANCER
    → NCKR_ACTION_ISOLATE → PRUNE → KILL
    → 아포토시스 실행
    → energy_budget = 0 → 경로 영구 소멸

  cancer_score 중간 (의심)
    → NCKR_VERDICT_SUSPECT
    → Shadow Mirror 병렬 감시 시작
    → cycle_count 증가하며 재관찰

  정상 범위
    → NCKR_VERDICT_NORMAL
    → NCKR_ACTION_REINFORCE (일반 K-Hebbian)
      ↓
[L3] Gate 1~4 의미 검증
      통과 → MATURITY_VERIFIED
      실패 → Shadow Node 영구 격리
      ↓
[L5] L 게이트 승인 (flags[5] = 1)
      ΔW → weight에 반영
      온톨로지에 영구 누적
```

---

### 12-7. 창의성과 암세포의 수치 경계

| 항목 | 창의 | 정상 | 의심 | 암세포 |
|------|------|------|------|--------|
| weight_change_rate | 감소 | 안정 | 증가 | 급증 |
| novelty_ratio | > 0.3 | 0.05~0.3 | < 0.05 | ≈ 0 |
| novelty_yield / energy_spent | > 0.5 | 0.1~0.5 | < 0.1 | ≈ 0 |
| cancer_score | 0~30 | 0~60 | 60~150 | > 150 |
| creative_score | > 150 | 30~150 | < 30 | — |

---

### 12-8. 구현 단계

| 단계 | 내용 | 파일 | 상태 |
|------|------|------|------|
| 24 | `NCkRVerdict` 구조체 정의 | `kc_ckr.h` | 🔲 |
| 25 | L2 패턴 판단 — 순환 감지 + 에너지 효율 계산 | `kc_neuron.c` | 🔲 |
| 26 | L4 창의성/암 점수 계산 함수 | `kc_ckr.c` | 🔲 |
| 27 | verdict 기반 action 실행 분기 | `kc_ckr.c` | 🔲 |
| 28 | Shadow Mirror 연동 — SUSPECT 시 병렬 감시 | `kc_ontology.c` | 🔲 |
| 29 | 아포토시스 실행 — CANCER 시 energy_budget = 0 | `kc_neuron.c` | 🔲 |
| 30 | NCkRSpreadConfig — 창의 모드 임계값 조절 | `kc_ckr.h` | 🔲 |

---

## 13. 망각 시스템 — 에빙하우스 곡선

### 13-1. 왜 망각이 필요한가

```
망각 없는 시스템:
  모든 시냅스가 동일한 강도로 유지
  → 중요한 것과 안 중요한 것 구별 불가
  → VRAM 압축 기준이 없음
  → 창의성 불가 (새 연결이 기존에 묻힘)

망각 있는 시스템 (인간 뇌):
  안 쓰는 연결 → 약화 → 공간 확보
  자주 쓰는 연결 → 강화 → 고착
  → 중요한 것이 자연스럽게 부상
  → VRAM 압축 기준 = 망각 곡선
```

---

### 13-2. 에빙하우스 망각 곡선

```
w(t) = w_0 × e^(-t / τ)

  w_0  : 초기 weight (마지막 활성화 시점)
  t    : 마지막 활성화 후 경과 시간
  τ    : 시정수 (개념의 중요도에 따라 다름)

τ 기준:
  Core Zone 개념  τ = ∞   (불변 공리 — 망각 없음)
  L2 UNIVERSAL    τ = 매우 큼 (보편 지식 — 매우 천천히 망각)
  L1 VERIFIED     τ = 크게  (검증된 지식)
  L0 SHADOW       τ = 작게  (미검증 — 빠르게 망각)
  단기 시냅스      τ = 매우 작게 (잠깐 쓰인 것)
```

---

### 13-3. NCkR 통합 — 필드 추가

```c
// KcNeuronChunk에 망각 필드 추가
// 현재 32B → 64B 확장 or delta_w 재사용

// 방법 A: 32B 유지 — cooldown_ms 를 decay_tau로 전환
typedef struct __attribute__((aligned(32))) {
    uint8_t  from_id[8];     //  8B
    uint8_t  to_id[8];       //  8B
    float    weight;         //  4B  현재 강도
    float    delta_w;        //  4B  미승인 ΔW
    uint8_t  polarity;       //  1B
    uint8_t  lineage_depth;  //  1B
    uint8_t  flags;          //  1B
    uint8_t  refractory_on;  //  1B
    uint16_t refractory_ms;  //  2B
    uint8_t  decay_class;    //  1B  망각 등급 (τ 인덱스)
    uint8_t  pad;            //  1B
} KcNeuronChunk;             // 32B ✅ (cooldown_ms 제거 → decay_class로 교체)

// decay_class → τ 룩업 테이블 (엔진 초기화 시 1회)
// decay_lut[16] 과 동일 원리
static float g_tau_lut[8] = {
    0.0f,          // [0] Core Zone — 망각 없음 (∞)
    999999.0f,     // [1] UNIVERSAL — 극히 천천히
    86400.0f * 365,// [2] VERIFIED  — 1년
    86400.0f * 30, // [3] 검증됨    — 1달
    86400.0f * 7,  // [4] 일반      — 1주
    86400.0f,      // [5] 단기      — 1일
    3600.0f,       // [6] 임시      — 1시간
    300.0f,        // [7] 순간      — 5분
};
```

---

### 13-4. 망각 적용 함수

```c
// 발화 시 망각 감쇠 적용
// 마지막 발화 이후 경과 시간으로 weight 갱신
void nckr_apply_decay(
    float    *weight,         // weight 컬럼
    uint32_t *last_fired_ms,  // 마지막 발화 타임스탬프 컬럼
    uint8_t  *decay_class,    // 망각 등급 컬럼
    uint32_t  now_ms,         // 현재 시간
    uint32_t  N               // 시냅스 수
) {
    for (uint32_t i = 0; i < N; i++) {
        if (decay_class[i] == 0) continue;  // Core — 망각 없음

        float tau = g_tau_lut[decay_class[i]];
        float t   = (now_ms - last_fired_ms[i]) / 1000.0f; // 초 단위
        float decay_factor = expf(-t / tau);

        weight[i] *= decay_factor;

        // 임계값 이하 → 아포토시스 후보
        if (weight[i] < 0.01f) {
            flags[i] |= NCKR_FLAG_APOPTOSIS;  // 제거 대상 마킹
        }
    }
}
```

---

### 13-5. 망각 + K-Hebbian 통합 흐름

```
[시냅스 발화 시]
  1. 망각 감쇠: weight *= e^(-t/τ)    ← 에빙하우스
  2. Hebbian 강화: weight += ΔW       ← K-Hebbian
  3. L 게이트: L=0이면 delta_w에 대기 ← 무결성
  4. 클램핑: weight = clamp(0.0, 1.0) ← 안정성
  5. last_fired_ms 갱신

[미발화 시냅스 배치 처리 (주기적)]
  VRAM 80% 도달 → 전체 decay 배치 적용
  weight < 0.01 → 아포토시스 마킹
  → §11 압축 기준으로 활용

[복습 효과 (Spaced Repetition)]
  재발화 시 weight 급등 (기억 복원)
  → 망각 후 재학습 = 더 강하게 고착
  → 인간의 간격 반복 학습과 동일
```

---

### 13-6. VRAM 압축 기준 강화

```
기존 §11 압축 기준:
  score = weight × recency × (1/lineage) × L_approved

망각 적용 후 새 기준:
  score = weight_decayed × recency × (1/lineage) × L_approved
        = (w_0 × e^(-t/τ)) × recency × ...

  → 오래 미사용 시냅스는 weight_decayed 자동 감소
  → 별도 score 계산 없이 weight 자체가 중요도 표현
  → VRAM 압축 = weight < 임계값 → 자동 오프로드
```

---

### 13-7. 구현 단계

| 단계 | 내용 | 파일 | 상태 |
|------|------|------|------|
| 31 | `KcNeuronChunk` — `cooldown_ms` → `decay_class + pad` 교체 | `kc_ckr.h` | 🔲 |
| 32 | `g_tau_lut[8]` 초기화 (8등급 τ 값) | `kc_ckr.c` | 🔲 |
| 33 | `nckr_apply_decay()` — 배치 감쇠 함수 | `kc_ckr.c` | 🔲 |
| 34 | K-Hebbian 연동 — 발화 시 감쇠 후 강화 | `kc_neuron.c` | 🔲 |
| 35 | 아포토시스 트리거 — weight < 0.01 마킹 | `kc_neuron.c` | 🔲 |
| 36 | §11 압축 기준 업데이트 — weight_decayed 활용 | `kc_ckr.c` | 🔲 |

---

## 14. 가변 시냅스

### 14-1. 개요

```
개념지식뉴런의 시냅스 수는 고정이 아니다.

생성 조건:
  1. 창의 발화  — A→B→C 반복 발화 → A→C 신규 생성
  2. Shadow Node 승격 — Gate 통과 후 기존 개념과 연결
  3. 학습 루프 — kc_learn 새 규칙 → 시냅스 생성

소멸 조건:
  1. 망각  — weight < 0.01 → 아포토시스 마킹 → 제거
  2. 에너지 고갈 — energy_budget = 0
  3. 암세포 판정 — NCkRVerdict CANCER → PRUNE

인간 뇌와 동일:
  유아기:   과잉 생성 (Base_Ont 로드)
  학습기:   생성 > 소멸 (지식 확장)
  안정기:   생성 ≈ 소멸 (균형)
  망각기:   소멸 > 생성 (오래된 지식 정리)
```

### 14-2. 슬롯 방식 구현

```c
typedef struct {
    // 컬럼 배열 (사전 확보)
    float    *col_weight;       // [MAX_N]
    float    *col_delta;        // [MAX_N]
    uint8_t  *col_flags;        // [MAX_N]
    uint8_t  *col_from[4];      // [MAX_N]
    uint8_t  *col_to[4];        // [MAX_N]
    uint8_t  *col_decay;        // [MAX_N]

    uint32_t  capacity;         // 사전 확보 최대치 (변경 없음)
    uint32_t  active_count;     // 실제 살아있는 시냅스 수 (가변)

    // 재활용 스택
    uint32_t *free_slots;       // 아포토시스된 슬롯 인덱스
    uint32_t  free_count;
} NCkRPool;
```

### 14-3. Swap-and-Pop 삭제

```c
// O(1) 삭제 — 배열 연속성 유지
void nckr_synapse_remove(NCkRPool *pool, uint32_t idx) {
    uint32_t last = --pool->active_count;

    // 마지막 슬롯을 삭제 위치로 이동
    pool->col_weight[idx] = pool->col_weight[last];
    pool->col_delta[idx]  = pool->col_delta[last];
    pool->col_flags[idx]  = pool->col_flags[last];
    memcpy(&pool->col_from[idx*4], &pool->col_from[last*4], 4);
    memcpy(&pool->col_to[idx*4],   &pool->col_to[last*4],   4);
    pool->col_decay[idx]  = pool->col_decay[last];
}

// 생성
uint32_t nckr_synapse_add(NCkRPool *pool, float w, uint8_t decay) {
    if (pool->active_count >= pool->capacity)
        nckr_pool_grow(pool);   // DRAM realloc
    uint32_t idx = pool->active_count++;
    pool->col_weight[idx] = w;
    pool->col_flags[idx]  = 0;
    pool->col_decay[idx]  = decay;
    return idx;
}
```

### 14-4. KACP 전송 — active_count 기준

```
NCkRDirectory.synapse_count = pool->active_count
  → 수신측이 실제 크기 파악
  → 죽은 슬롯 전송 안 함

GPU 푸쉬:
  col_weight[0..active_count] 만 전송
  → 가변 크기 패킷 자동 처리
```

### 14-5. 구현 단계

| 단계 | 내용 | 파일 | 상태 |
|------|------|------|------|
| 37 | `NCkRPool` — capacity/active_count/free_slots 구조 | `kc_ckr.h` | 🔲 |
| 38 | `nckr_synapse_add()` — 생성 함수 | `kc_ckr.c` | 🔲 |
| 39 | `nckr_synapse_remove()` — Swap-and-Pop 삭제 | `kc_ckr.c` | 🔲 |
| 40 | `nckr_pool_grow()` — DRAM realloc | `kc_ckr.c` | 🔲 |
| 41 | NCkRDirectory `active_count` 필드 추가 | `kc_ckr.h` | 🔲 |
| 42 | KACP 전송 — active_count 기준 패킷 크기 | `kacp.h` | 🔲 |

---

## 15. 목적 달성 요약

| # | 목적 | 개념지식뉴런 담당 | 비고 |
|---|------|-----------------|------|
| 1 | CKR 성능 향상 | ✅ 직접 | JSON→바이너리, 83배 CPU |
| 2 | 청크 1개 = 뉴런 1개 | ✅ 직접 | 개념지식뉴런 32B |
| 3 | 저장공간 향상 | ✅ 직접 | VRAM 2.6~3.1배, 파일 82% |
| 4 | 처리속도 향상 | ✅ 직접 | GPU FP32 16배, BF16 32배 |
| 5 | 뉴런 본래 목적 | ✅ 직접 | K-Hebbian + L게이트 + 망각 |
| 6 | LLM 추론 성능 | 🔶 간접 | 풍부한 컨텍스트 공급 |
| 7 | 한도내 자율 사고 | 🔶 보조 | L게이트 + Shell Zone |
| 8 | 암세포 판별/조치 | ✅ 직접 | NCkRVerdict + 아포토시스 |
| 9 | VRAM 효율 운영 | ✅ 직접 | §11 압축/리셋 + BF16 |
| 10 | LLM 창의적 사고 | ✅ 간접 | 확산 활성화 → 새 개념 누적 |
| + | 가변 시냅스 | ✅ 직접 | 뇌 가소성 동일 원리 |
| + | 망각 시스템 | ✅ 직접 | 에빙하우스 + VRAM 압축 연동 |
| + | 청크 사전 | ✅ 직접 | embed 영구 캐시 + DRAM 배치 |
| + | KACP ID 전송 | ✅ 직접 | uint32 4B / 이름 불필요 |

---

## 16. 청크 사전 — KcChunkDict

### 16-1. 설계 배경 및 통찰

```
통찰 1: 사전 = 임베딩 영구 캐시
  "고양이" 등록 시 embed() 1회 실행
  이후 발화마다 dict 조회만 (embed 없음)

통찰 2: 임베딩 → LSH ID화
  vec(1536D) → SimHash → 32B 이진코드
  HNSW 인덱스 불필요
  의미 유사 검색 = 해밍거리 POPCNT

통찰 3: 시냅스 순회 = 디코딩 불필요
  from_id/to_id = uint32 정수
  발화 전파 = 정수 비교만
  이름 변환 = 출력 시만 dict_reverse[id]

세 통찰 결합:
  등록 1회  → embed() + LSH 계산 + dict 저장
  발화 매번 → dict 조회 (0.00005ms) + 시냅스 순회
  출력 시만 → dict_reverse[id] → 이름
```

### 16-2. 구조체 (32B × 2)

```c
/* 사전 항목 (32B) */
typedef struct {
    uint32_t chunk_id;     /* 4B 정수 ID (배열 인덱스) */
    uint32_t name_crc32;   /* 4B CRC32 (해시 버킷 키) */
    uint32_t lsh_offset;   /* 4B lsh_table 인덱스 */
    uint8_t  decay_class;  /* 1B 망각 등급 */
    uint8_t  lang;         /* 1B 언어 KC_LANG_KO/EN/JP */
    uint8_t  flags;        /* 1B Core/Shell/Shadow/LSH유효 */
    uint8_t  name_len;     /* 1B 이름 길이 */
    uint8_t  name[16];     /* 16B 이름 인라인 */
} KcChunkEntry;            /* = 32B ✅ */

/* LSH 이진코드 (32B) */
typedef struct {
    uint8_t code[32];      /* 256bit SimHash */
} KcLshEntry;              /* = 32B ✅ */

/* 사전 전체 */
typedef struct {
    KcChunkEntry  *entries;    /* [capacity] */
    KcLshEntry    *lsh_table;  /* [capacity] */
    char         **ext_names;  /* [capacity] 긴 이름 */
    KcCDictSlot   *hash_slots; /* [131072] CRC 해시 버킷 */
    const char   **reverse;    /* [capacity] ID→이름 */
    uint32_t       count;
    uint32_t       capacity;
    uint32_t       next_id;
} KcChunkDict;
```

### 16-3. 3계층 ID 구조

```
계층 0: 이름 → 사전 조회        0.00005ms  embed 캐시
  "고양이" → CRC → hash_slots → chunk_id=4576

계층 1: ID → LSH               0.00005ms  배열 직접
  lsh_table[4576] → LSH_32B

계층 2: 시냅스                  uint32 4B
  from_id=4576, to_id=3201
```

### 16-4. 등록 흐름 (최초 1회)

```
입력 "고양이"
  → CRC32 충돌 검사
  → chunk_id 발급 (next_id++)
  → KcChunkEntry 저장 (인라인 이름)
  → embed(name) → vec[1536D]       ← 1회만
  → kc_lsh_from_vec(vec) → LSH 32B
  → lsh_table[chunk_id] 저장
  → hash_slots 삽입
  → .kcd 비동기 동기화
```

### 16-5. 발화 흐름 (매번, embed 없음)

```
입력 "고양이"
  → dict["고양이"] → 4576         embed 없음
  → VRAM node_fired[4576] = 1
  → 시냅스 순회 (uint32+float)
  → 출력 시: dict_reverse[id]
```

### 16-6. 의미 유사 검색 (HNSW 없음)

```
"고냥이" (오타)
  → embed("고냥이") → vec
  → kc_lsh_from_vec(vec) → LSH_쿼리
  → kc_cdict_search_similar(LSH_쿼리, threshold=64)
  → 해밍거리 ≤ 64인 chunk_id 목록
  → 0.002ms  (HNSW 0.09ms 대비 45배 빠름)
```

### 16-7. 메모리 (100만 개념)

```
KcChunkEntry × 100만:  32MB
KcLshEntry   × 100만:  32MB
hash_slots            :   1MB
reverse ptrs          :   8MB
합계                  :  73MB  ← VRAM 상주 불필요, DRAM으로 충분
```

### 16-8. 구현 파일

| 파일 | 상태 | 비고 |
|------|------|------|
| `kc_chunk_dict.h` | ✅ v31.0.0 구현 완료 | 구조체 + API 12종 |
| `kc_chunk_dict.c` | ✅ v31.0.0 구현 완료 | TC-01~TC-11 통과 |

---

## 17. 메모리 배치 확정 — DRAM / VRAM / SSD

### 17-1. 배치 결정 원칙

```
VRAM: GPU 연산 필요 데이터만
  → 숫자 (uint32, float, BF16)
  → 노드 상태 (threshold/fired/maturity)
  → 시냅스 (from_id/to_id/weight/delta)

DRAM: 텍스트/메타/의미 데이터
  → 이름 문자열
  → LSH 이진코드
  → CRC 해시 버킷
  → 역조회 포인터

SSD: 장기 미발화 cold 시냅스
  → .ckr 컬럼 파일
  → .kcd 사전 스냅샷
```

### 17-2. 3계층 스토리지 정책

| 계층 | 위치 | 비율 | 기준 |
|------|------|------|------|
| HOT | VRAM | 10% | 최근 72시간 내 발화 |
| WARM | DRAM | 20% | 최근 30일 내 발화 |
| COLD | SSD | 70% | 30일 이상 미발화 |

```
페이지 교체:
  발화 감지   → COLD→WARM 승격   0.1ms
  빈도 초과   → WARM→HOT 승격    0.01ms
  VRAM 80%   → HOT→WARM 강등 + NCkRCompressed 압축

.kcd 파일:
  서버 재시작 → DRAM 사전 즉시 복원
  embed() 재실행 불필요 ✅
```

### 17-3. 노드/엣지 분리 구조

```
DRAM                          VRAM
KcChunkDict[chunk_id]  ──▶  node_threshold[chunk_id]
  이름/LSH/메타               node_input_sum[chunk_id]
                              node_fired[chunk_id]
                              node_maturity[chunk_id]
                              node_lineage[chunk_id]

                    ──▶  NeuronChunk[i]
                              from_id  uint32
                              to_id    uint32
                              weight   BF16
                              delta_w  FP32
                              flags/polarity/lineage/decay
```

---

## 18. KACP 전송 개선 — uint32 ID

### 18-1. 변경 전/후

```
현재 KACP:
  이름 "고양이" (9B UTF-8) 전송
  수신측에서 hash 재계산
  비효율 반복

개선 KACP:
  송신: dict["고양이"] → uint32 4576 → KACP 전송
  수신: uint32 처리 → 출력 시만 dict_reverse[4576]

패킷 크기:
  현재: 9B + 헤더 64B = 73B
  개선: 4B + 헤더 64B = 68B  (7% 절감)
  실질: 이름 문자열 처리 비용 제거
```

### 18-2. 시냅스 KACP 전송

```
현재: from_id 16B + to_id 16B = 32B/시냅스
개선: from_id  4B + to_id  4B =  8B/시냅스  (75% 절감)

10M 시냅스 KACP 전송:
  현재: 320MB 패킷
  개선:  80MB 패킷  (4배 절감)
```

### 18-3. magic 코드

```
KACP 기존 magic 유지:
  KONT = 온톨로지 (개념 조회)
  KCKR = CKR DB (시냅스 저장)
  KDNA = DNA 엔진

신규 패킷 필드:
  KACPHeader.chunk_id  uint32  이름 대신 ID 전송
  KACPHeader.flags 비트: ID_MODE=0x01 (ID 전송 여부)
```

---

## 19. 저장 용량 — 규모별 수치

### 19-1. 단위 크기

| 항목 | 크기 | 변경 | 위치 |
|------|------|------|------|
| DRAM KcChunkEntry | 32B/개념 | 신규 | DRAM |
| DRAM KcLshEntry | 32B/개념 | 신규 | DRAM |
| DRAM hash+ptr | 24B/개념 | 신규 | DRAM |
| VRAM 노드 상태 | 14B/개념 | 신규 분리 | VRAM |
| VRAM 시냅스 from/to | 4B+4B | **16B+16B → 4B+4B** | VRAM |
| VRAM 시냅스 전체 | 18B | **48B → 18B (-62%)** | VRAM |

### 19-2. 규모별 전체 용량

| 규모 | 개념 | 시냅스 | VRAM | DRAM | SSD | 합계 | 기존 대비 |
|------|------|--------|------|------|-----|------|-----------|
| 소형(개인) | 10만 | 100만 | 3MB | 12MB | 13MB | **28MB** | 48MB (-60%) |
| 중형(서비스) | 100만 | 1000만 | 32MB | 124MB | 126MB | **282MB** | 480MB (-60%) |
| 대형(기업) | 1000만 | 1억 | 320MB | 1.2GB | 1.3GB | **2.8GB** | 4.8GB (-60%) |
| **뇌 규모** | 10억 | **100억** | **32GB** | **124GB** | **126GB** | **282GB** | 480GB (-60%) |

### 19-3. 뇌 규모 달성 조건

```
VRAM:  32GB  → A100 80GB 1대 (여유 48GB 확보)
DRAM: 124GB  → 서버 표준 256GB 메모리
SSD:  126GB  → NVMe SSD 1TB 1개
합계: 282GB  → 서버 1~2대로 뇌 규모 달성

기존 설계: 480GB → 서버 3~4대 필요
새 구조:   282GB → 서버 1~2대  (41% 추가 절감)
```

### 19-4. 핵심 절감 요인

```
1. from_id/to_id: 16B+16B → 4B+4B  (-75%)
2. DRAM 사전 분리: embed 재실행 0
3. 3계층 스토리지: hot 10%만 VRAM 상주
4. 사전 .kcd 파일: 재시작 비용 0
```

---

## 20. DNA 연동 변경 4가지

### 20-1. 변경 목록

| # | 파일 | 문제 | 심각도 |
|---|------|------|--------|
| 1 | `kc_dna_neuron.h` + `kc_ontology.h` | KC_ZONE_CORE 상수 중복 | 🔴 컴파일 오류 |
| 2 | `kc_dna_ont.h` | NCkR 연동 API 없음 | 🔴 기능 누락 |
| 3 | `kc_dna_ont.c` | v_ont 벡터 공급 → KcChunkDict 연동 필요 | 🟡 설계 미완 |
| 4 | `KcDNADORS` | 망각 시간축 불일치 (ms vs day) | 🟡 동작 오류 |

### 20-2. 변경 상세

**변경 1 — KC_ZONE_CORE 중복**
```c
// kc_dna_neuron.h 수정
// 기존:
typedef enum {
    KC_ZONE_CORE     = 0,   // 충돌!
    KC_ZONE_VERIFIED = 1,
    ...
} KcZoneClass;

// 변경:
typedef enum {
    KC_DNA_ZONE_CORE     = 0,
    KC_DNA_ZONE_VERIFIED = 1,
    KC_DNA_ZONE_PROPOSED = 2,
    KC_DNA_ZONE_GENERAL  = 3,
} KcZoneClass;
```

**변경 2 — NCkR 연동 API 추가**
```c
// kc_dna_ont.h 추가
int kc_dna_ont_nckr_fire(uint32_t chunk_id, float resonance);
int kc_dna_ont_verdict(uint32_t chunk_id, const NCkRVerdict *v);
```

**변경 3 — v_ont → KcChunkDict vec_cache**
```c
// 기존: kc_vec_from_class() 매번 호출
// 변경: KcChunkDict 사전의 vec_cache 포인터 직접
//   dict_get_vec(chunk_id) → float* (Zero-Copy)
```

**변경 4 — tau_sec 추가**
```c
// KcDNADORS에 tau_sec 추가
typedef struct {
    float    alpha_A;
    float    gamma_G;
    float    theta_R;
    uint32_t access_count;
    double   last_access_t;
    uint32_t bootstrap_n;
    float    tau_sec;      // ← 신규: NCkR decay_class와 동기화
} KcDNADORS;              // 28B → 32B (+4B)
```

### 20-3. 변경 효과

| 항목 | 변경 전 | 변경 후 |
|------|---------|---------|
| 컴파일 | 오류 | ✅ 정상 |
| D-ORS 쿼리 속도 | 0.25ms | 0.001ms (250배) |
| KACP 패킷 | 128B | 96B (-25%) |
| 망각 동기화 | 불일치 | ✅ tau_sec 연동 |
| DNA 뉴런 크기 | +4B/뉴런 | +3.8MB/100만 뉴런 (미미) |

---

*문서 끝 — v3.0.0 / 2026-03-28*
*v3.0.0: 온톨로지==NCkR 통합 / KcNeuronSynapse 24B / KcNCkRVram / 1000억 용량 / 구조 단순화*
*v2.0.0: 청크 사전 / DRAM 배치 / 3계층 스토리지 / KACP ID 전송*
*v1.5.0: 최초 설계 완료*
*다음 단계: kc_ontology.h 수정 + KcNeuronSynapse + KcNCkRVram 신규 구현*
*미해결: KcNodeMeta 800B/노드 최적화 (100억 노드 기준 8TB)*

---

## 21. v3.0.0 — 온톨로지 == NCkR 통합 설계

### 21-1. 핵심 통찰

```
온톨로지 == NCkR

KcOntClass     = 청크 노드  (chunk_id = class_idx, 동일)
관계/속성/계층 = 시냅스     (KcNeuronSynapse)
KcOHTSlot      = 이름→ID   (현행 그대로)
class_idx      = chunk_id  (동일, 별도 사전 불필요)
```

### 21-2. 최종 구조체 3개

```c
/* [1] KcOntClass — 청크 노드 */
typedef struct KcOntClass {
    char       *name;        /* 개념 이름 */
    char       *uri;
    char       *description;
    uint32_t    chunk_id;    /* = class_idx */
    uint8_t     lsh[32];     /* SimHash 256bit */
    uint8_t     decay_class; /* 망각 등급 0~7 */
    uint8_t     nckr_flags;  /* fired/refrac/L승인 */
    uint8_t     lang;        /* 언어 코드 */
    uint8_t     pad[1];
    float      *vec_cache;   /* CI 임베딩 */
    int         vec_dim;
    KcNodeLabel label;
    KcNodeMeta  meta;
    int         ref_count;
    /* 삭제: parent*//*children[64]/props*//*child_count/prop_count */
} KcOntClass;

/* [2] KcNeuronSynapse — 시냅스 24B */
typedef struct {
    uint32_t from_id;    /* 출발 chunk_id */
    uint32_t to_id;      /* 도착 chunk_id */
    uint16_t weight;     /* BF16 */
    float    delta_w;    /* FP32 */
    uint8_t  rel_type;   /* PARENT/CHILD/PROP/RELATE/INSTANCE/LEARNED */
    int8_t   polarity;
    uint8_t  lineage;
    uint8_t  decay;
    uint16_t refractory;
    uint8_t  flags;
    uint8_t  pad;
} KcNeuronSynapse;       /* = 24B */

/* [3] KcNCkRVram — VRAM SoA */
typedef struct {
    float    *node_threshold;
    float    *node_input_sum;
    uint8_t  *node_fired;
    float    *node_maturity;
    uint8_t  *node_lineage;
    uint32_t *syn_from;
    uint32_t *syn_to;
    uint16_t *syn_weight;   /* BF16 */
    float    *syn_delta;
    uint8_t  *syn_flags;
    uint8_t  *syn_decay;
    uint32_t *fired_ids;
    uint32_t  fired_count;
    uint32_t  node_count;
    uint32_t  syn_count;
    uint32_t  node_capacity;
    uint32_t  syn_capacity;
} KcNCkRVram;
```

### 21-3. 제거 목록

| 제거 | 대체 |
|------|------|
| `KcNeuronPool` | `KcNCkRVram` |
| `KcNeuronChunk 32B` | `KcNeuronSynapse 24B` |
| `KcNeuronState` | `KcNCkRVram.node_*` |
| `KcChunkDict` | `KcOHTSlot` 그대로 |
| `children[64]` 512B | `rel_type=CHILD` 시냅스 |
| `parent*` | `rel_type=PARENT` 시냅스 |
| `props*` | `rel_type=PROP` 시냅스 |

### 21-4. 발화 흐름

```
"고양이" → KcOHTSlot → chunk_id=1      0.0001ms
         → VRAM node_fired[1] = 1
         → syn_from==1 시냅스 순회 (GPU)
         → K-Hebbian weight 갱신
         → 출력 시만: KcOntClass[id].name

embed(): 등록 시 1회만
디코딩(): 출력 시만 name 직접
```

### 21-5. KACP 통신

```
저장용: KcNeuronSynapse 24B
통신용: KcSynBlock 32B (reserved 8B)
컬럼 전송: KACPHeader + NCkRDirectory + 컬럼 스트리밍
```

---

## 22. 최초 설계 vs v3.0.0 비교

| 항목 | v1.5.0 최초 | v3.0.0 |
|------|-------------|---------|
| 철학 | 온톨로지 + K-Neuron 결합 | 온톨로지 == NCkR |
| 청크 크기 | 32B | 24B (-25%) |
| from/to | 16B hash | 4B uint32 (-75%) |
| 이름→ID | KcChunkDict 별도 | KcOHTSlot 그대로 |
| 별도 구조체 | 7개 | 3개 |
| embed() | 매 발화 | 등록 1회 |
| children[] | 고정 512B | 시냅스 대체 |

---

## 23. 1000억 시냅스 용량

### 23-1. 3계층 분배

| 계층 | 비율 | 용량 | 하드웨어 |
|------|------|------|---------|
| VRAM hot | 1% | **164GB** | A100 2장 |
| DRAM warm | 9% | **1TB** | 서버 1대 |
| SSD cold | 90% | **10TB** | NVMe 4TB 3개 |
| **전체** | | **11.3TB** | 서버 1~2대 |

### 23-2. 항목별

```
시냅스 24B × 1000억  = 2.4TB
노드 핵심 80B × 100억 = 800GB
노드 메타 800B × 100억 = 8TB  ← ⚠️ 최대 항목 (다음 최적화 대상)
노드 VRAM 14B × 100억 = 140GB
```

### 23-3. 뇌 규모

```
인간 뇌 = 100조 시냅스 (1000억의 1000배)
1000억  = 뇌의 0.1%
뇌 전체: 시냅스 240TB / VRAM 16.4TB → 분산 클러스터
```


---

## 24. 이름 제거 + OHT 사전 통합 구조

### 24-1. 핵심 통찰

```
이름(문자열) = OHT 사전에만 존재
청크 노드   = chunk_id만 (이름 없음)
다국어/동의어 = 각자 chunk_id + ALIAS 시냅스

"자동차"(100) / "Car"(101) / "차"(102)
  → 각자 OHT 슬롯에 이름 저장
  → ALIAS 시냅스로 연결: Car→자동차, 차→자동차
  → 노드 자체에 name 문자열 불필요
```

### 24-2. KcOntClass 최종 (이름 없음, ~91B)

```c
typedef struct KcOntClass {
    uint32_t chunk_id;        //  4B  유일 식별자
    uint8_t  lsh[32];         // 32B  SimHash 256bit
    uint8_t  decay_class;     //  1B  망각 등급
    uint8_t  nckr_flags;      //  1B  zone/maturity/보안
    uint8_t  fingerprint[32]; // 32B  위조방지 SHA-256
    uint32_t registered_at;   //  4B  타임스탬프
    uint8_t  immune_flags;    //  1B  immune/poisoned/gate
    uint32_t gate_fail_mask;  //  4B  검증 결과
    float   *vec_cache;       //  8B  CI 임베딩 포인터
    int      vec_dim;         //  4B
    int      ref_count;       //  4B
} KcOntClass;                 // = ~91B (현재 933B → -90%)

/* 삭제:
   name* / uri* / description*  → OHT 사전에만
   KcNodeLabel 649B             → OHT + ALIAS 시냅스
   KcNodeMeta 문자열 필드        → 해시/비트플래그로 압축
*/
```

### 24-3. KcOHTSlot 확장 (32B) — 사전 역할 통합

```c
typedef struct {
    uint32_t hash_key;   //  4B  djb2/CRC32
    uint32_t chunk_id;   //  4B  = class_idx
    uint8_t  used;       //  1B
    uint8_t  tomb;       //  1B
    uint8_t  lang;       //  1B  KO/EN/JP/ZH
    uint8_t  name_len;   //  1B  이름 길이
    uint8_t  name[16];   // 16B  인라인 이름 (≤15자+NULL)
} KcOHTSlot;             // = 32B ✅

/* 긴 이름(>15자): SSD ext_names 참조 */
```

### 24-4. KC_SYN_ALIAS 추가

```c
#define KC_SYN_PARENT    0x01
#define KC_SYN_CHILD     0x02
#define KC_SYN_PROP      0x03
#define KC_SYN_RELATE    0x04
#define KC_SYN_INSTANCE  0x05
#define KC_SYN_LEARNED   0x06
#define KC_SYN_ALIAS     0x07  /* 다국어/동의어 — 신규 */
```

### 24-5. 다국어 처리 흐름

```
OHT:
  "자동차" → chunk_id=100 (KO, lang=0x01)
  "Car"    → chunk_id=101 (EN, lang=0x02)
  "차"     → chunk_id=102 (KO, lang=0x01)

ALIAS 시냅스:
  {from=101, to=100, rel_type=ALIAS, weight=1.0}  Car→자동차
  {from=102, to=100, rel_type=ALIAS, weight=1.0}  차→자동차

검색:
  "Car" → OHT → 101 → ALIAS 시냅스 → 100(자동차) 발화
  "차"  → OHT → 102 → ALIAS 시냅스 → 100(자동차) 발화

이름 역조회 (출력 시만):
  chunk_id=100 → OHT reverse[] → "자동차"
```

---

## 25. 시냅스 포함 전체 용량

### 25-1. 시냅스 수에 따른 전체 용량 (100억 노드)

| 시나리오 | 시냅스 수 | VRAM | DRAM | SSD | **전체** |
|----------|-----------|------|------|-----|---------|
| 노드당 10개 | 1000억 | 164GB | 2.2TB | 2.2TB | **4.5TB** |
| 노드당 100개 (의미망) | 1조 | 380GB | 4.1TB | 21.6TB | **26TB** |
| 노드당 1000개 (뇌 근사) | 10조 | 2.5TB | 23.5TB | 216TB | **242TB** |

### 25-2. 이전 설계 vs 현재 (1000억 시냅스)

```
               이전 v2.0.0    현재 v3.0.0    절감
KcOntClass:    933B           91B            -90%
DRAM:          9.5TB          2.2TB          -77%
전체:          11.9TB         4.5TB          -62%
```

### 25-3. 하드웨어 (1000억 시냅스 기준)

```
VRAM  164GB  → A100 80GB 2장
DRAM  2.2TB  → DDR5 서버 2~3대
SSD   2.2TB  → NVMe 4TB 1개
─────────────────────────────
전체  4.5TB  → 서버 2~3대
```

### 25-4. 핵심 포인트

```
시냅스가 지배적:
  노드당 100개 이상이면 시냅스가 전체 용량의 90%
  노드 최적화보다 시냅스 수 관리가 더 중요

시냅스 수 관리 전략:
  망각 시스템 (decay_class) → cold/dead 시냅스 SSD 이동
  K-Hebbian 임계값 → 약한 시냅스 자동 제거
  NCkRVerdict 아포토시스 → 암세포/노이즈 시냅스 삭제
```


---

## 26. 시냅스 경량화 확정 — 8B + 양방향

### 26-1. 진화 과정

```
최초 KcNeuronChunk  32B  from[8]+to[8]+weight+delta+flags...
v3.0 KcNeuronSynapse 24B  from(4)+to(4)+weight+delta+rel+ld+refrac+flags
경량화 1             16B  +next(연결리스트), -delta/polarity/flags/pad
경량화 2             12B  -from_id (syn_head로 소유자 알수 있음)
경량화 3              8B  -next (직접 배열 + offset 방식)
양방향 포함          16B  출력8B + 역방향인덱스8B
```

### 26-2. 최종 확정 구조

```c
/* 출력 SoA (per시냅스 8B) */
// KcOntology 전역:
uint32_t *syn_to;      // 4B to_id
uint16_t *syn_weight;  // 2B BF16 (음수=억제, polarity 통합)
uint8_t  *syn_rel;     // 1B rel_type
uint8_t  *syn_ld;      // 1B lineage[3:0]+decay[6:4]

/* 역방향 인덱스 (per시냅스 8B) — K-Hebbian 용 */
uint32_t *in_from;     // 4B from_id
uint32_t *in_syn_idx;  // 4B 출력배열 내 인덱스

/* KcOntClass 추가 필드 */
uint32_t syn_offset;    // 출력 시냅스 시작
uint32_t syn_count;     // 실제 시냅스 수
uint32_t syn_capacity;  // 예약 슬롯 (Slack)
uint32_t in_offset;     // 역방향 시작
uint32_t in_count;      // 역방향 수
```

### 26-3. rel_type 확정

```c
#define KC_SYN_PARENT    0x01
#define KC_SYN_CHILD     0x02
#define KC_SYN_PROP      0x03
#define KC_SYN_RELATE    0x04
#define KC_SYN_INSTANCE  0x05
#define KC_SYN_LEARNED   0x06
#define KC_SYN_ALIAS     0x07  /* 다국어/동의어 */
```

### 26-4. 용량 비교

| 방식 | 크기 | 1000억 | 절감 |
|------|------|--------|------|
| 최초 AoS | 32B | 3.2TB | 기준 |
| v3.0 | 24B | 2.4TB | -25% |
| 출력만 | 8B | 0.8TB | -75% |
| **양방향** | **16B** | **1.6TB** | **-50%** |

---

## 27. 망각 후 시냅스 추가 — Slack + Compaction

### 27-1. 문제

```
연속 배열에서 노드 B에 시냅스 추가 시
인접 노드 C 영역 침범 위험
```

### 27-2. 해결 — Slack + 비동기 Compaction

```c
KcOntClass:
  syn_offset    // 시작 위치
  syn_count     // 실제 시냅스 수
  syn_capacity  // 예약 슬롯 (count + slack)

추가:
  if (count < capacity) → 슬랙에 O(1)
  else → 오버플로 큐 보관 + 백그라운드 Compaction

Compaction (비동기 더블버퍼링):
  새 배열 할당 → 노드별 복사 → offset 갱신
  → 발화 중단 없음
```

### 27-3. 시냅스 생명주기

```
등록    → count++ (슬랙 소비)
강화    → weight 증가
망각    → weight < 0.01 → Swap-and-Pop → count--
소멸    → 슬랙 반환 → 재사용

망각≈추가 균형 → Compaction 불필요
```

### 27-4. 슬랙 20% 기준

```
유효 7000억 × 8B = 5.6TB
슬랙 20% 예약   = 6.7TB (+1.1TB)
```

---

## 28. 같은 청크를 가리키는 시냅스 구분

### 28-1. 순방향 — 이미 해결

```
고양이 slice: syn_to[0..2] = [동물, 포유류, 발톱]
강아지 slice: syn_to[3..4] = [동물, 포유류]

같은 to_id=동물이지만 다른 슬롯 → 구분됨 ✅
```

### 28-2. 역방향 — 역방향 인덱스

```
"동물을 가리키는 모든 시냅스 찾기"
→ in_from[] + in_syn_idx[] 역방향 인덱스
→ K-Hebbian 시 동물.in_range 순회
```

### 28-3. 고유 키 — (from, to, rel_type) 3튜플

```
고양이→동물 PARENT   ← 유효
고양이→동물 RELATE   ← 별개 시냅스 (허용)
고양이→동물 PARENT   ← 중복 ❌ 차단

중복 검사:
  outDegree ≤ 100:  선형 탐색 O(outDegree)
  outDegree > 1000: 해시셋 O(1) 권고
```

---

## 29. 순환 시냅스 처리

### 29-1. 예시

```
고양이(1) → 동물(0) → 색상(5) → 파랭이(6) → 고양이(1)

등록:  허용 ✅ ("파랭이색 고양이" = 의미있는 순환)
발화:  무한루프 차단 필요
```

### 29-2. 해결 — fired_set 비트맵

```c
// KcNCkRVram 추가
uint8_t *node_visited;   // 비트맵 N/8 바이트

#define VISITED(id) (node_visited[(id)>>3] >> ((id)&7) & 1)
#define MARK(id)    (node_visited[(id)>>3] |= 1<<((id)&7))

// 발화 루프 +2줄만 추가
for (int i = 0; i < node.syn_count; i++) {
    uint32_t to = syn_to[offset + i];
    if (VISITED(to)) continue;  // ← +1줄
    MARK(to);                    // ← +1줄
    propagate(to, weight);
}
// 사이클 종료: fired_ids 기반 클리어
```

### 29-3. 기존 검증 로직 — 변경 불필요

```
기존 Gate 1~4:     등록 보안 (DRAM)  → 변경 없음 ✅
kc_kbank_cycle_check: 계보 순환 (별도) → 변경 없음 ✅
fired_set 비트맵:  발화 엔진 (VRAM)  → 추가만 ✅

Gate 코드 변경:  0줄
발화 코드 변경: +2줄
비트맵 용량:    1.25GB (100억 노드)
```

### 29-4. 발화 결과

```
고양이→동물→색상→파랭이→고양이(SKIP)
K-Hebbian: 전체 경로 정상 강화 ✅
```


---

## 30. 저장/로드 재설계

### 30-1. 현재 구현 현황

```
함수                구현 상태    방식
────────────────    ──────────   ──────────────────────
kc_ckr_save()       ✅ 구현됨    JSON 청크 → .ckr 바이너리
kc_ckr_load()       ✅ 구현됨    JSON 청크 파싱 → KcOntClass 복원
kc_kbank_save()     ✅ 구현됨    kbank → kc_ont_save() 호출
kc_kbank_load()     ✅ 구현됨    JSON → kc_kbank_from_json()
kc_ont_save()       ✅ 구현됨    온톨로지 → JSON/TTL
kc_ont_load()       ❌ 미구현    TODO (return ERR_NOTFOUND)
```

### 30-2. 호출 체계

```
.kw 실행 / HTTP /kbank/save
    ↓
kc_kbank_save/load()     ← 진입점 (유지)
    ↓
kc_ont_save/load()       ← JSON 텍스트 방식
    ↓ (추가 필요)
kc_ckr_save/load()       ← 바이너리 컬럼 방식 (독립, kbank 미연결)
```

### 30-3. kc_ckr_load 복원 현황

```
✅ KC_CKR_CT_CLASS    → kc_ont_add_class()
✅ KC_CKR_CT_INSTANCE → kc_ont_add_instance()
✅ KC_CKR_CT_RELATION → kc_ont_add_relation()
✅ KC_CKR_CT_RULE     → kc_ont_add_rule()

❌ 시냅스 SoA         (syn_to/weight/rel/ld)
❌ 역방향 인덱스      (in_from/in_syn_idx)
❌ OHT 사전           (이름↔chunk_id)
❌ LSH 코드           (lsh[32]/노드)
❌ syn_offset/count/capacity
❌ chunk_id
❌ VRAM weight 스냅샷
```

### 30-4. 새 파일 포맷

```
.ckr  노드 + 시냅스 바이너리 컬럼
  [KcCKRFileHeader 64B]
  ── 노드 섹션 ──
  chunk_id[]        uint32 × N
  lsh[]             uint8[32] × N
  decay_class[]     uint8 × N
  nckr_flags[]      uint8 × N
  fingerprint[]     uint8[32] × N
  registered_at[]   uint32 × N
  immune_flags[]    uint8 × N
  gate_fail_mask[]  uint32 × N
  syn_offset[]      uint32 × N
  syn_count[]       uint32 × N
  syn_capacity[]    uint32 × N
  in_offset[]       uint32 × N
  in_count[]        uint32 × N
  ── 시냅스 섹션 ──
  syn_to[]          uint32 × S
  syn_weight[]      uint16 × S  BF16
  syn_rel[]         uint8  × S
  syn_ld[]          uint8  × S
  in_from[]         uint32 × S  역방향
  in_syn_idx[]      uint32 × S  역방향
  [KcCKRSeal 32B]

.oht  OHT 사전
  KcOHTSlot[] 32B × N  (이름 인라인 16B 포함)

.vram  VRAM weight 스냅샷 (선택)
  syn_weight[] 현재값
```

### 30-5. 로드 방식 — mmap Zero-Copy

```c
// 새 kc_ckr_load():
int fd = open(path, O_RDONLY);
void *base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);

// 포인터 직접 참조 (파싱 0번, 복사 0번)
onto->syn_to     = base + syn_to_offset;
onto->syn_weight = base + syn_weight_offset;
onto->all_oht    = base + oht_offset;
```

### 30-6. 속도 비교

```
              현재 JSON        바이너리 mmap
저장 방식     텍스트 직렬화    바이너리 컬럼
로드 방식     파싱 + malloc    mmap Zero-Copy
로드 시간     100ms ~ 1s       ~1ms
파싱 횟수     N회              0회
```

### 30-7. 변경 우선순위

```
1. kc_ckr.c 재작성
     serialize_class() → 바이너리 컬럼
     시냅스/OHT/LSH 섹션 추가
     mmap 기반 로드

2. kc_ont_load() 구현
     TODO 해결
     kc_ckr_load() 호출로 위임

3. kc_kbank → kc_ckr 연결
     kc_kbank_save/load() 진입점 유지
     내부에서 kc_ckr_save/load() 호출

외부 API:  kc_kbank_save/load() → 변경 없음 ✅
```


---

## 31. KACP 프로토콜 — NCkR 적용 확정

### 31-1. 결정 원칙

```
외부 공개 KACP 설계:  변경 없음 ✅ (이미 배포)
내부 구현:            자유롭게 선택
외부에서 보이는 것:   KACP TCP 7070 그대로
```

### 31-2. 5개 결정사항

```
1. chunk_id + magic KONT 유지
   concept_id[16] 앞 4B = chunk_id (암묵적 재해석)
   크기 32B 유지, 하위 호환 ✅

2. 시냅스 스트리밍
   KONT cmd=0x10 SYN_STREAM_REQ
   NCkRDirectory(32B) + 컬럼 페이로드
   col_flags 비트로 필요한 컬럼만 선택

3. 발화 신호 — 내부 UDP
   외부 노출 없음
   내부에서 단순 UDP 처리
   손실 허용 (재발화 가능)

4. 서버간 연결 기존 KFED
   데이터 다운로드는 .ckr 파일
   차분 전송 (변경분만)
   cmd 0x30 CKR_DOWNLOAD_REQ

5. TCP/UDP 분리 — 내부 구현
   KACP / TCP 7070: 등록/조회/파일/서버간 (보안 강화)
   UDP / 내부:      발화 신호 (단순/빠름)
   외부: KACP 그대로
```

### 31-3. KcONTBlock1 내부 재매핑 (32B 유지)

```c
typedef struct {
    /* concept_id[16] 재해석 */
    uint32_t chunk_id;        // [0..3]  ← 신규
    uint8_t  lsh_prefix[8];   // [4..11] ← LSH 앞 8B
    uint8_t  reserved[4];     // [12..15]
    uint8_t  class_hash[8];   // 8B
    uint8_t  ont_status;      // 1B
    uint8_t  constraint_flag; // 1B
    uint8_t  access_level;    // 1B
    uint8_t  reserved2[5];    // 5B
} KcONTBlock1;                // = 32B ✅

// chunk_id == 0 → concept_name OHT 조회 (기존 동작)
// chunk_id != 0 → 직접 사용 (고속 경로)
```

### 31-4. 시냅스 스트리밍 헤더 (32B)

```c
// NCkRDirectory — KONT cmd=0x10
typedef struct {
    uint8_t  cmd;             // 0x10 SYN_STREAM_REQ
    uint8_t  col_flags;       // [7]to [6]weight [5]rel [4]ld
                              // [3]in_from [2]in_syn_idx
    uint32_t node_count;
    uint32_t syn_count;
    uint32_t from_chunk_id;   // 0=전체
    uint32_t checksum;        // CRC32
    uint8_t  reserved[18];
} KcNCkRDirectory;            // = 32B ✅

// 선택적 컬럼:
// 발화만: col_flags=0b11000000 (to+weight)
// 전체:   col_flags=0b11111100
```

### 31-5. 신규 cmd 정의

```c
#define KC_ONT_CMD_SYN_STREAM_REQ   0x10
#define KC_ONT_CMD_SYN_STREAM_ACK   0x11
#define KC_ONT_CMD_SYN_STREAM_ERR   0x12
#define KC_ONT_CMD_CKR_DOWNLOAD_REQ 0x30
#define KC_ONT_CMD_CKR_DOWNLOAD_ACK 0x31
```

### 31-6. 발화 신호 내부 UDP 패킷 (16B)

```c
// 외부 노출 없음 — 내부 전용
typedef struct {
    uint8_t  magic[4];    // "KLBV"
    uint8_t  flags;       // 1B
    uint8_t  access_level;// 1B
    uint16_t sequence;    // 2B
    uint32_t chunk_id;    // 4B 발화 노드
    uint16_t weight;      // 2B BF16
    uint16_t checksum;    // 2B CRC16
} KcFirePacket;           // = 16B

// 배치 발화 N개: 8B + 6B × N
// 5개 발화: 38B
```

### 31-7. 무거운 부분 — 향후 KCP 교체 (조용히)

```
현재:
  발화 신호:    내부 UDP (단순)
  .ckr 전송:    TCP

성능 이슈 발생 시:
  .ckr 파일 전송    → KCP (무겁고 대용량)
  시냅스 스트리밍   → KCP (1000억 시냅스)

교체 방식:
  외부: KACP TCP 7070 그대로
  내부: 전송 레이어만 교체
  외부 클라이언트: 변경 없음 ✅

참고: KCP = UDP 위에서 신뢰성 ARQ 구현
      TCP 대비 평균 지연 30~40% 감소
      HTTP와 같은 등급의 범용 프로토콜
```

### 31-8. 전체 프로토콜 구조

```
외부 공개:
  KACP / TCP 7070   개념 등록/조회/파일/서버간   보안 강화

내부 구현:
  UDP / 내부        발화 신호                     16B 단순
  TCP               .ckr 파일 전송                스트리밍

향후 (조용히):
  KCP over UDP      무거운 대용량 전송             성능 필요 시
```


---

## 32. 우선순위 발화 시스템 — 동적 threshold + 치매/증식 예방

> 결정일: 2026-03-30
> 상태: 확정 / v31.2.2 구현 완료

### 32-1. 핵심 설계 원칙

```
연결된 시냅스:
  weight 高 (hot)  → 매 틱 즉시 처리
  weight 低 (warm) → 200틱마다 배치 처리

연결 없는 시냅스:
  → decay → Swap-and-Pop 삭제 (망각)

치매 예방:
  모든 노드 최소 시냅스 1개 보장
  syn_count == 0 → 유예 기간 후 삭제

비정상 증식 예방:
  hot tier 비율 5~15% 유지 → 동적 T_hot 조정
```

### 32-2. weight tier 정의

```
흥분 시냅스 (weight > 0):
  HOT  : weight >= T_hot   (기본 0.75) → 매 틱
  WARM : weight >= T_warm  (T_hot×0.35) → 200틱마다
  COLD : weight >= T_dead  (0.01)       → decay만
  DEAD : weight <  T_dead  (0.01)       → Swap-and-Pop

억제 시냅스 (weight < 0, 대칭):
  HOT_INH  : weight <= -0.75 → 매 틱
  WARM_INH : weight <= -0.25 → 200틱마다
  COLD_INH : weight >  -0.25 → decay만
```

### 32-3. 동적 T_hot 조정

```
목표: hot tier = 전체 시냅스의 10%

hot_ratio = hot_count / syn_count

hot_ratio > 15%:  T_hot += 0.01  (증식 억제)
hot_ratio <  5%:  T_hot -= 0.01  (치매 예방)
hot_ratio 5~15%:  T_hot 유지

조정 범위:  0.50 ≤ T_hot ≤ 0.95
T_warm    = T_hot × 0.35  (연동 자동 갱신)
```

### 32-4. 확정 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| KC_SYN_WEIGHT_INIT | 0.5 | 신규 시냅스 초기값 |
| KC_SYN_T_DEAD | 0.01 | 삭제 임계값 (고정) |
| KC_SYN_T_HOT_MIN | 0.50 | T_hot 하한 (치매 방지) |
| KC_SYN_T_HOT_MAX | 0.95 | T_hot 상한 (증식 방지) |
| KC_SYN_T_HOT_DEFAULT | 0.75 | 초기 T_hot |
| KC_SYN_T_WARM_RATIO | 0.35 | T_warm = T_hot × ratio |
| KC_SYN_T_HOT_INH | -0.75 | 강한 억제 임계값 |
| KC_SYN_T_WARM_INH | -0.25 | 약한 억제 임계값 |
| KC_HOT_TARGET_RATIO | 0.10 | hot tier 목표 비율 |
| KC_HOT_UPPER_BOUND | 0.15 | 증식 경계 |
| KC_HOT_LOWER_BOUND | 0.05 | 치매 경계 |
| KC_T_HOT_ADJUST_STEP | 0.01 | 틱당 조정 단위 |
| KC_WARM_TICK_CYCLE | 200 | warm 처리 주기 (틱) |
| KC_SYN_MIN_PER_NODE | 1 | 최소 시냅스 수 |
| KC_FORGETTING_GRACE | 100 | 망각 유예 (decay×N틱) |

### 32-5. KcNCkRVram 추가 필드

```c
/* 동적 threshold 상태 (런타임 변수) */
float    t_hot;          /* 현재 T_hot (동적 조정)    */
float    t_warm;         /* 현재 T_warm = t_hot×0.35 */
uint32_t hot_count;      /* 현재 hot 시냅스 수        */
uint32_t tick_count;     /* 누적 틱 카운터            */
uint32_t warm_cycle;     /* warm 처리 주기 (기본 200) */
```

### 32-6. 틱 처리 흐름

```
매 틱:
  [1] hot/hot_inh 시냅스 발화 전파
  [2] node_input_sum >= threshold → 발화 판정
  [3] K-Hebbian 갱신 (발화 쌍 강화 / 미발화 약화)
  [4] T_dead 미만 → Swap-and-Pop
  [5] 치매 예방 — syn_count==0 유예 카운터 갱신
  [6] 동적 T_hot 조정 (hot_ratio 기반)

200틱마다 (tick_count % warm_cycle == 0):
  [7] warm/warm_inh 시냅스 배치 처리
```

### 32-7. 규모별 예상 성능 (GX10 단독)

```
hot tier 10% 기준:

뉴런 1억   / 시냅스 10억  → hot 1억   × 8B = 0.8GB  ✅
뉴런 10억  / 시냅스 100억 → hot 10억  × 8B = 8GB   ✅
뉴런 100억 / 시냅스 1000억→ hot 100억 × 8B = 80GB  ⚠️ GX10×2 필요

GX10 단독: ~50억 시냅스 hot tier 처리 가능
GX10 × 2 : ~100억 시냅스 hot tier 처리 가능
```

### 32-8. 생물학적 대응

```
hot  tier  = 장기기억 (LTP, Long-Term Potentiation)
warm tier  = 단기기억 (STP, Short-Term Potentiation)
dead       = 시냅스 가지치기 (Synaptic Pruning)
syn_count==0 + 유예 = 뉴런 휴면 (Quiescence)
치매 예방   = 최소 LTP 보장 (use-it-or-lose-it 방지)
증식 억제   = 호메오스타시스 (Homeostatic Plasticity)
```
