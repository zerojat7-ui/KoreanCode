/* ================================================================
   KCODE IDE v1.3.0 — ide.js
   ================================================================ */

/* ── 블록 정의 ────────────────────────────────────────────── */
const BD = {
  var_int:    {label:'정수',       sub:'int',      cat:'자료형',  clr:'#0a4a30',bdr:'#00e890',params:['이름','값'],    body:false},
  var_float:  {label:'실수',       sub:'float',    cat:'자료형',  clr:'#0a4a30',bdr:'#00e890',params:['이름','값'],    body:false},
  var_str:    {label:'문자',       sub:'string',   cat:'자료형',  clr:'#0a4a30',bdr:'#00e890',params:['이름','값'],    body:false},
  var_chr:    {label:'글자',       sub:'char',     cat:'자료형',  clr:'#0a4a30',bdr:'#00e890',params:['이름','값'],    body:false},
  var_bool:   {label:'논리',       sub:'bool',     cat:'자료형',  clr:'#0a4a30',bdr:'#00e890',params:['이름','값'],    body:false},
  var_arr:    {label:'배열',       sub:'array',    cat:'자료형',  clr:'#0a4a30',bdr:'#00e890',params:['이름'],         body:false},
  var_dict:   {label:'사전',       sub:'dict',     cat:'자료형',  clr:'#0a4a30',bdr:'#00e890',params:['이름'],         body:false},
  var_const:  {label:'고정',       sub:'const',    cat:'자료형',  clr:'#0a3a28',bdr:'#00e890',params:['이름','값'],    body:false},
  var_tensor: {label:'텐서',       sub:'tensor',   cat:'자료형',  clr:'#0a4a30',bdr:'#00e890',params:['이름','모양'],  body:false},
  if:         {label:'만약',       sub:'if',       cat:'제어문',  clr:'#0a5466',bdr:'#00d4ff',params:['조건'],         body:true},
  elif:       {label:'아니면만약', sub:'elif',     cat:'제어문',  clr:'#0a5466',bdr:'#00d4ff',params:['조건'],         body:true},
  else:       {label:'아니면',     sub:'else',     cat:'제어문',  clr:'#0a5466',bdr:'#00d4ff',params:[],               body:true},
  switch:     {label:'선택',       sub:'switch',   cat:'제어문',  clr:'#0a5466',bdr:'#00d4ff',params:['값'],           body:false},
  loop:       {label:'반복',       sub:'for',      cat:'제어문',  clr:'#0a3670',bdr:'#4a90ff',params:['횟수'],         body:true},
  while:      {label:'동안',       sub:'while',    cat:'제어문',  clr:'#0a3670',bdr:'#4a90ff',params:['조건'],         body:true},
  foreach:    {label:'각각',       sub:'foreach',  cat:'제어문',  clr:'#0a3670',bdr:'#4a90ff',params:['변수','목록'],  body:true},
  break:      {label:'멈춤',       sub:'break',    cat:'제어문',  clr:'#3a2a08',bdr:'#ff9900',params:[],               body:false},
  cont:       {label:'건너뜀',     sub:'continue', cat:'제어문',  clr:'#3a2a08',bdr:'#ff9900',params:[],               body:false},
  func:       {label:'함수',       sub:'function', cat:'함수',    clr:'#0a3670',bdr:'#4a90ff',params:['이름','인수'],  body:true},
  void:       {label:'정의',       sub:'void',     cat:'함수',    clr:'#0a3670',bdr:'#4a90ff',params:['이름','인수'],  body:true},
  ret:        {label:'반환',       sub:'return',   cat:'함수',    clr:'#561e08',bdr:'#ff6b35',params:['값'],           body:false},
  print:      {label:'출력',       sub:'print',    cat:'함수',    clr:'#561e08',bdr:'#ff6b35',params:['값'],           body:false},
  input:      {label:'입력',       sub:'input',    cat:'함수',    clr:'#561e08',bdr:'#ff6b35',params:['변수'],         body:false},
  class:      {label:'객체',       sub:'class',    cat:'객체',    clr:'#3a1650',bdr:'#aa55ff',params:['이름'],         body:true},
  extends:    {label:'이어받기',   sub:'extends',  cat:'객체',    clr:'#3a1650',bdr:'#aa55ff',params:['부모'],         body:false},
  ctor:       {label:'생성',       sub:'new',      cat:'객체',    clr:'#3a1650',bdr:'#aa55ff',params:[],               body:true},
  try:        {label:'시도',       sub:'try',      cat:'예외',    clr:'#3a2a08',bdr:'#ff9900',params:[],               body:true},
  catch:      {label:'실패시',     sub:'catch',    cat:'예외',    clr:'#3a2a08',bdr:'#ff9900',params:['오류'],         body:true},
  finally:    {label:'항상',       sub:'finally',  cat:'예외',    clr:'#3a2a08',bdr:'#ff9900',params:[],               body:true},
  raise:      {label:'오류',       sub:'throw',    cat:'예외',    clr:'#5a1a08',bdr:'#ff3322',params:['메시지'],       body:false},
  python:     {label:'파이썬',     sub:'.py',      cat:'스크립트',clr:'#1a3a10',bdr:'#6aa84f',params:['변수'],         body:true},
  java:       {label:'자바',       sub:'.java',    cat:'스크립트',clr:'#3a1010',bdr:'#ff6b35',params:['변수'],         body:true},
  js:         {label:'자바스크립트',sub:'.js',     cat:'스크립트',clr:'#3a3010',bdr:'#f0d000',params:['변수'],         body:true},
  ts:         {label:'타입스크립트',sub:'.ts',     cat:'스크립트',clr:'#102040',bdr:'#3178c6',params:['변수'],         body:true},
  gpu:        {label:'가속기',     sub:'GPU',      cat:'가속기',  clr:'#0a2050',bdr:'#00d4ff',params:['종류'],         body:true},
  matmul:     {label:'행렬곱',     sub:'GEMM',     cat:'가속기',  clr:'#0a2050',bdr:'#00d4ff',params:['A','B','결과'], body:false},
  matadd:     {label:'행렬합',     sub:'EWAdd',    cat:'가속기',  clr:'#0a2050',bdr:'#00d4ff',params:['A','B','결과'], body:false},
  conv:       {label:'합성곱',     sub:'Conv2D',   cat:'가속기',  clr:'#0a2050',bdr:'#00d4ff',params:['입력','결과'],  body:false},
  activate:   {label:'활성화',     sub:'ReLU',     cat:'가속기',  clr:'#0a2050',bdr:'#00d4ff',params:['입력','결과'],  body:false},
  ontology:   {label:'온톨로지',   sub:'onto',     cat:'온톨로지',clr:'#1a0a50',bdr:'#7755ff',params:['모드'],         body:true},
  concept:    {label:'개념',       sub:'class',    cat:'온톨로지',clr:'#1a0a50',bdr:'#7755ff',params:['이름'],         body:true},
  prop:       {label:'속성',       sub:'prop',     cat:'온톨로지',clr:'#1a0a50',bdr:'#7755ff',params:['이름','타입'],  body:false},
  relate:     {label:'관계',       sub:'rel',      cat:'온톨로지',clr:'#1a0a50',bdr:'#7755ff',params:['이름','A','B'], body:false},
  query:      {label:'질의',       sub:'query',    cat:'온톨로지',clr:'#1a0a50',bdr:'#7755ff',params:['질의','결과'],  body:false},
  infer:      {label:'추론',       sub:'infer',    cat:'온톨로지',clr:'#1a0a50',bdr:'#7755ff',params:[],               body:true},
  mcp_srv:    {label:'MCP서버',    sub:'server',   cat:'MCP',     clr:'#1a3030',bdr:'#00aaaa',params:['이름'],         body:true},
  mcp_tool:   {label:'MCP도구',    sub:'tool',     cat:'MCP',     clr:'#1a3030',bdr:'#00aaaa',params:['이름'],         body:true},
  mcp_res:    {label:'MCP자원',    sub:'resource', cat:'MCP',     clr:'#1a3030',bdr:'#00aaaa',params:['이름'],         body:true},
  heonbeob:   {label:'헌법',       sub:'global',   cat:'계약',    clr:'#3a1a08',bdr:'#ff9900',params:['조건','제재'],  body:false},
  beomnyul:   {label:'법률',       sub:'file',     cat:'계약',    clr:'#3a1a08',bdr:'#ff9900',params:['조건','제재'],  body:false},
  beomryeong: {label:'법령',       sub:'pre',      cat:'계약',    clr:'#3a1a08',bdr:'#ff9900',params:['함수명'],       body:true},
  beopwiban:  {label:'법위반',     sub:'post',     cat:'계약',    clr:'#3a1a08',bdr:'#ff9900',params:['함수명'],       body:true},
  signal:     {label:'신호받기',   sub:'signal',   cat:'인터럽트',clr:'#2a1a3a',bdr:'#ff44aa',params:['신호'],         body:true},
  isr:        {label:'간섭',       sub:'ISR',      cat:'인터럽트',clr:'#2a1a3a',bdr:'#ff44aa',params:['벡터'],         body:true},
  event:      {label:'행사등록',   sub:'event',    cat:'인터럽트',clr:'#2a1a3a',bdr:'#ff44aa',params:['이름'],         body:true},
  aiconn:     {label:'AI연결',     sub:'LLM',      cat:'AI',      clr:'#3a0832',bdr:'#ff44aa',params:['모델','키'],    body:false},
  aimodel:    {label:'AI모델',     sub:'ONNX',     cat:'AI',      clr:'#3a0832',bdr:'#ff44aa',params:['이름'],         body:true},
  tinyml:     {label:'TinyML',     sub:'lite',     cat:'AI',      clr:'#3a0832',bdr:'#ff44aa',params:['이름'],         body:true},
  img:        {label:'이미지',     sub:'image',    cat:'AI',      clr:'#320a52',bdr:'#aa55ff',params:['경로'],         body:false},
  fileopen:   {label:'파일열기',   sub:'fopen',    cat:'파일',    clr:'#0a2a40',bdr:'#4a90ff',params:['경로','모드'],  body:false},
  fileread:   {label:'파일읽기',   sub:'fread',    cat:'파일',    clr:'#0a2a40',bdr:'#4a90ff',params:['파일','변수'],  body:false},
  filewrite:  {label:'파일쓰기',   sub:'fwrite',   cat:'파일',    clr:'#0a2a40',bdr:'#4a90ff',params:['파일','내용'],  body:false},
  fileall:    {label:'파일전체읽기',sub:'readall', cat:'파일',    clr:'#0a2a40',bdr:'#4a90ff',params:['경로','변수'],  body:false},
  timer:      {label:'타이머',     sub:'timer',    cat:'임베디드',clr:'#1a2a10',bdr:'#88cc44',params:['주기'],         body:true},
  gpio_w:     {label:'GPIO쓰기',   sub:'gpio',     cat:'임베디드',clr:'#1a2a10',bdr:'#88cc44',params:['핀','값'],      body:false},
  gpio_r:     {label:'GPIO읽기',   sub:'gpio',     cat:'임베디드',clr:'#1a2a10',bdr:'#88cc44',params:['핀','결과'],    body:false},
  i2c:        {label:'I2C연결',    sub:'i2c',      cat:'임베디드',clr:'#1a2a10',bdr:'#88cc44',params:['주소'],         body:false},
  mqtt:       {label:'MQTT연결',   sub:'mqtt',     cat:'임베디드',clr:'#1a2a10',bdr:'#88cc44',params:['브로커'],       body:false},
  ros2:       {label:'ROS2노드',   sub:'ros2',     cat:'임베디드',clr:'#1a2a10',bdr:'#88cc44',params:['이름'],         body:true},
  watchdog:   {label:'워치독',     sub:'wdog',     cat:'안전',    clr:'#2a0a08',bdr:'#ff3322',params:['타임아웃'],     body:true},
  faultol:    {label:'결함허용',   sub:'FT',       cat:'안전',    clr:'#2a0a08',bdr:'#ff3322',params:['N중'],          body:true},
  failsafe:   {label:'페일세이프', sub:'safe',     cat:'안전',    clr:'#2a0a08',bdr:'#ff3322',params:[],               body:false},
  estop:      {label:'긴급정지',   sub:'estop',    cat:'안전',    clr:'#2a0a08',bdr:'#ff3322',params:[],               body:false},
  kbank:      {label:'지식뱅크',   sub:'kbank',    cat:'지식뱅크',clr:'#0a2a30',bdr:'#00aaaa',params:['이름'],         body:true},
  kbankload:  {label:'지식불러오기',sub:'load',    cat:'지식뱅크',clr:'#0a2a30',bdr:'#00aaaa',params:['경로'],         body:false},
  kbankcmp:   {label:'지식비교',   sub:'compare',  cat:'지식뱅크',clr:'#0a2a30',bdr:'#00aaaa',params:['뱅크1','뱅크2'],body:false},
};

const CATS = ['자료형','제어문','함수','객체','예외','스크립트','가속기','온톨로지','MCP','계약','인터럽트','AI','파일','임베디드','안전','지식뱅크'];
const TABS = ['콘솔','CLI','오류','AST','IR','이미지','AI 로그'];
const MENUS_LIST = ['파일','편집','실행','빌드','도움말'];
const AI_QUICK = ['문법 설명','블록 추가','오류 수정','예제 보기','최적화 팁'];

/* ── 상태 ─────────────────────────────────────────────────── */
let uidC=10;
let ws=[{uid:1,id:'if',params:{조건:'x > 10'},children:[
  {uid:2,id:'loop',params:{횟수:'5'},children:[
    {uid:3,id:'print',params:{값:"'안녕'"},children:[]}
  ]}
]}];
let selUid=1, catOpen={}, activeMenu='실행', activeTab='콘솔', running=false, editorMode='block';
CATS.forEach(c => catOpen[c] = ['자료형','제어문','함수'].includes(c));

let consoleLog=[{t:'out',s:'안녕'},{t:'out',s:'안녕'},{t:'out',s:'안녕'},{t:'info',s:'실행 완료 (0.002초)'}];
const CLI_INIT=[
  {t:'sys',s:'Kcode CLI v1.3.0'},
  {t:'sys',s:'──────────────────────────────────────────'},
  {t:'dim',s:'  run / build / lex / parse / version / clear'},
  {t:'sys',s:'──────────────────────────────────────────'},
];
let cliHist=[...CLI_INIT], cmdHist=[], hIdx=-1;
let aiMessages=[{role:'bot',text:'안녕하세요! Kcode 개발을 도와드리는 K-AI입니다.\n블록 코딩이나 .han 문법에 대해 무엇이든 물어보세요 😊',time:''}];

/* ── 유틸 ─────────────────────────────────────────────────── */
function findBlock(uid,bls){for(const b of bls){if(b.uid===uid)return b;const f=b.children?.length?findBlock(uid,b.children):null;if(f)return f;}return null;}
function newUid(){return++uidC;}
function nowTime(){return new Date().toLocaleTimeString('ko-KR',{hour:'2-digit',minute:'2-digit'});}

function blockToHan(b,ind){
  const d=BD[b.id];if(!d)return'';
  const pad='    '.repeat(ind);
  const pv=Object.values(b.params||{});
  const ch=()=>(b.children||[]).map(c=>blockToHan(c,ind+1)).join('');
  switch(b.id){
    case'if':       return`${pad}만약 ${pv[0]||'조건'}:\n${ch()}`;
    case'elif':     return`${pad}아니면 만약 ${pv[0]||'조건'}:\n${ch()}`;
    case'else':     return`${pad}아니면:\n${ch()}`;
    case'loop':     return`${pad}반복 i 부터 0 까지 ${pv[0]||'횟수'}:\n${ch()}`;
    case'while':    return`${pad}동안 ${pv[0]||'조건'}:\n${ch()}`;
    case'foreach':  return`${pad}각각 ${pv[0]||'변수'} 안에 ${pv[1]||'목록'}:\n${ch()}`;
    case'func':     return`${pad}함수 ${pv[0]||'이름'}(${pv[1]||''}):\n${ch()}`;
    case'void':     return`${pad}정의 ${pv[0]||'이름'}(${pv[1]||''}):\n${ch()}`;
    case'print':    return`${pad}출력(${pv[0]||'값'})\n`;
    case'ret':      return`${pad}반환 ${pv[0]||'값'}\n`;
    case'var_int':  return`${pad}정수 ${pv[0]||'변수'} = ${pv[1]||'0'}\n`;
    case'var_str':  return`${pad}문자 ${pv[0]||'변수'} = ${pv[1]||'""'}\n`;
    case'var_const':return`${pad}고정 ${pv[0]||'상수'} = ${pv[1]||'0'}\n`;
    case'gpu':      return`${pad}가속기 "${pv[0]||'GPU'}":\n${ch()}가속기끝\n`;
    case'ontology': return`${pad}온톨로지 "${pv[0]||'내장'}":\n${ch()}온톨로지끝\n`;
    case'python':   return`${pad}파이썬(${pv[0]||''}):\n${pad}    # Python\n${pad}파이썬끝\n`;
    case'ts':       return`${pad}타입스크립트(${pv[0]||''}):\n${pad}    // TypeScript\n${pad}타입스크립트끝\n`;
    default:return`${pad}${d.label}${pv.filter(Boolean).length?`(${pv.filter(Boolean).join(',')})`:''}\n`;
  }
}

/* ── 모드 전환 ───────────────────────────────────────────── */
function setMode(mode){
  editorMode=mode;
  document.getElementById('mb').classList.toggle('active',mode==='block');
  document.getElementById('mt').classList.toggle('active',mode==='text');

  const cv=document.getElementById('canvas');
  const te=document.getElementById('text-editor');
  const outline=document.getElementById('outline');
  const rightPanel=document.getElementById('right');
  const leftPanel=document.getElementById('left');
  const outlineResize=document.getElementById('outline-resize');

  if(mode==='block'){
    cv.style.display='block';
    te.style.display='none';
    // 블록 모드: 아웃라인 + 속성 패널 표시
    outline.classList.remove('hidden');
    rightPanel.classList.remove('hidden');
    if(outlineResize)outlineResize.style.display='block';
    leftPanel.style.display='flex';
    renderCanvas();
  } else {
    cv.style.display='none';
    te.style.display='flex';
    // 텍스트 모드: 아웃라인 + 속성 패널 숨김
    outline.classList.add('hidden');
    rightPanel.classList.add('hidden');
    if(outlineResize)outlineResize.style.display='none';
    // 텍스트 에디터 내용 초기화
    const ta=document.getElementById('text-area');
    if(!ta.value)ta.value=ws.map(b=>blockToHan(b,0)).join('');
  }
}

/* ── 렌더 함수들 ──────────────────────────────────────────── */
function renderMenus(){
  const el=document.getElementById('menus');
  el.style.cssText='display:flex;align-items:center;';el.innerHTML='';
  MENUS_LIST.forEach(m=>{
    const d=document.createElement('div');
    d.className='menu-item'+(m===activeMenu?' active':'');d.textContent=m;
    d.onclick=()=>{activeMenu=m;renderMenus();};el.appendChild(d);
  });
}

function renderTabs(){
  const el=document.getElementById('tabs');
  el.style.cssText='display:flex;align-items:center;';el.innerHTML='';
  TABS.forEach(t=>{
    const d=document.createElement('div');d.className='tab'+(t===activeTab?' active':'');
    if(t==='CLI'){const i=document.createElement('span');i.style.cssText='opacity:.6;font-size:8.5px;font-family:monospace;';i.textContent='>_';d.appendChild(i);}
    d.appendChild(document.createTextNode(t));
    d.onclick=()=>{activeTab=t;renderTabs();renderTabContent();};el.appendChild(d);
  });
}

function renderBlockList(){
  const el=document.getElementById('block-list');el.innerHTML='';
  CATS.forEach(cat=>{
    const items=Object.entries(BD).filter(([,d])=>d.cat===cat);if(!items.length)return;
    const hdr=document.createElement('div');hdr.className='cat-hdr';
    const arr=document.createElement('span');arr.textContent=catOpen[cat]?'▼':'▶';
    arr.style.cssText='font-size:7px;margin-right:1px;';
    hdr.appendChild(arr);hdr.appendChild(document.createTextNode(cat));
    hdr.onclick=()=>{catOpen[cat]=!catOpen[cat];renderBlockList();};el.appendChild(hdr);
    if(catOpen[cat]){
      items.forEach(([id,d])=>{
        const item=document.createElement('div');item.className='block-item';
        item.style.cssText=`background:linear-gradient(135deg,${d.clr}cc,${d.clr}77);border:1px solid ${d.bdr}38;`;
        item.draggable=true;
        item.onmouseenter=()=>{item.style.boxShadow=`0 0 10px ${d.bdr}38`;item.style.transform='translateX(2px)';};
        item.onmouseleave=()=>{item.style.boxShadow='none';item.style.transform='none';};
        item.ondragstart=e=>e.dataTransfer.setData('blockId',id);
        const dot=document.createElement('div');dot.className='block-dot';dot.style.cssText=`background:${d.bdr};box-shadow:0 0 5px ${d.bdr};`;
        const wrap=document.createElement('div');wrap.style.cssText='display:flex;flex-direction:column;';
        const lb=document.createElement('span');lb.className='block-label';lb.textContent=d.label;
        const sb=document.createElement('span');sb.className='block-sub';sb.textContent=d.sub;
        wrap.appendChild(lb);wrap.appendChild(sb);item.appendChild(dot);item.appendChild(wrap);el.appendChild(item);
      });
    }
  });
}

function renderWsBlock(block){
  const d=BD[block.id];if(!d)return null;
  const isAr=['if','elif','loop','func','void','while','foreach','else'].includes(block.id);
  const sel=block.uid===selUid;
  const wrap=document.createElement('div');wrap.className='ws-wrap';
  const blk=document.createElement('div');blk.className='ws-blk'+(isAr?'':' noarrow');
  blk.style.cssText=`background-image:linear-gradient(135deg,${d.clr}${sel?'ff':'dd'},${d.clr}${sel?'bb':'88'}),repeating-linear-gradient(0deg,transparent,transparent 3px,rgba(0,0,0,.06) 3px,rgba(0,0,0,.06) 4px);border:1.5px solid ${sel?d.bdr:d.bdr+'48'};box-shadow:${sel?`0 0 16px ${d.bdr}65,0 0 5px ${d.bdr}40`:`0 0 5px ${d.bdr}18`};${isAr?`clip-path:polygon(0 0,calc(100% - 11px) 0,100% 50%,calc(100% - 11px) 100%,0 100%);`:''}`;
  if(sel){const bar=document.createElement('div');bar.className='ws-blk-bar';bar.style.cssText=`background:linear-gradient(180deg,${d.bdr},${d.bdr}40);`;blk.appendChild(bar);}
  const dot=document.createElement('div');dot.className='ws-blk-dot';dot.style.cssText=`background:${d.bdr};box-shadow:0 0 7px ${d.bdr};`;
  const lbl=document.createElement('span');lbl.className='ws-blk-label';lbl.style.cssText=`text-shadow:0 0 9px ${d.bdr}88;`;lbl.textContent=d.label;
  blk.appendChild(dot);blk.appendChild(lbl);
  Object.entries(block.params||{}).forEach(([k,v])=>{
    const p=document.createElement('div');p.className='ws-blk-param';p.style.cssText=`border:1px solid ${d.bdr}50;color:${d.bdr};`;p.textContent=v||k;blk.appendChild(p);
  });
  blk.onclick=e=>{e.stopPropagation();selUid=block.uid;renderAll();};
  wrap.appendChild(blk);
  if(d.body&&block.children?.length>0){
    const cw=document.createElement('div');cw.className='ws-ch';cw.style.cssText=`border-left-color:${d.bdr}30;`;
    const dt=document.createElement('div');dt.className='ws-ch-dt';dt.style.cssText=`background:${d.bdr}70;box-shadow:0 0 6px ${d.bdr}44;`;
    cw.appendChild(dt);
    block.children.forEach((ch,i)=>{const ce=renderWsBlock(ch);if(ce){ce.style.marginBottom=i<block.children.length-1?'5px':'0';cw.appendChild(ce);}});
    const db=document.createElement('div');db.className='ws-ch-db';db.style.cssText=`background:${d.bdr}30;`;cw.appendChild(db);
    wrap.appendChild(cw);
  }
  return wrap;
}

function renderCanvas(){
  const el=document.getElementById('canvas');el.innerHTML='';
  ws.forEach(b=>{const w=document.createElement('div');w.style.cssText='margin-bottom:12px;display:inline-block;';const be=renderWsBlock(b);if(be){w.appendChild(be);el.appendChild(w);}});
}

function renderOutlineItem(b,ind){
  const d=BD[b.id];if(!d)return;
  const el=document.getElementById('outline-list');
  const item=document.createElement('div');item.className='outline-item'+(b.uid===selUid?' sel':'');
  item.style.cssText=`padding-left:${8+ind*10}px;border-left-color:${b.uid===selUid?d.bdr:'transparent'};`;
  const ic=document.createElement('span');ic.style.cssText=`color:${d.bdr};font-size:7.5px;opacity:.8;`;ic.textContent=d.body?'▶':'◆';
  const lb=document.createElement('span');lb.className='o-lbl';lb.style.color=d.bdr;lb.textContent=d.label;
  item.appendChild(ic);item.appendChild(lb);
  const ps=Object.values(b.params||{}).filter(Boolean).join(', ');
  if(ps){const p=document.createElement('span');p.className='o-par';p.textContent=`(${ps})`;item.appendChild(p);}
  item.onclick=()=>{selUid=b.uid;renderAll();};el.appendChild(item);
  if(d.body&&b.children)b.children.forEach(c=>renderOutlineItem(c,ind+1));
}

function renderOutline(){document.getElementById('outline-list').innerHTML='';ws.forEach(b=>renderOutlineItem(b,0));}

function renderProps(){
  const el=document.getElementById('props');el.innerHTML='';
  const sb=findBlock(selUid,ws);const sd=sb?BD[sb.id]:null;
  if(!sd){el.innerHTML='<div class="prop-empty">블록을 선택하세요</div>';return;}
  const badge=document.createElement('div');badge.className='prop-badge';
  badge.style.cssText=`background:linear-gradient(135deg,${sd.clr}cc,${sd.clr}77);border:1.5px solid ${sd.bdr}50;`;
  const dot=document.createElement('div');dot.className='prop-badge-dot';dot.style.cssText=`background:${sd.bdr};box-shadow:0 0 7px ${sd.bdr};`;
  const lb=document.createElement('span');lb.className='prop-badge-label';lb.textContent=sd.label;
  badge.appendChild(dot);badge.appendChild(lb);el.appendChild(badge);
  Object.entries(sb.params||{}).forEach(([key,val])=>{
    const field=document.createElement('div');field.className='prop-field';
    const flbl=document.createElement('div');flbl.className='prop-fl';
    const dia=document.createElement('span');dia.style.cssText=`color:${sd.bdr};font-size:7.5px;`;dia.textContent='◆';
    flbl.appendChild(dia);flbl.appendChild(document.createTextNode(key));
    const inp=document.createElement('input');inp.className='prop-input';
    inp.style.cssText=`border-color:${sd.bdr}38;color:${sd.bdr};`;inp.value=val;
    inp.onfocus=()=>inp.style.borderColor=sd.bdr+'99';
    inp.onblur=()=>inp.style.borderColor=sd.bdr+'38';
    inp.oninput=()=>{
      function up(bl){return bl.map(b=>{if(b.uid===selUid)return{...b,params:{...b.params,[key]:inp.value}};return{...b,children:up(b.children||[])};});}
      ws=up(ws);renderCanvas();renderOutline();
    };
    field.appendChild(flbl);field.appendChild(inp);el.appendChild(field);
  });
  const dv=document.createElement('div');dv.className='prop-div';el.appendChild(dv);
  const uid=document.createElement('div');uid.className='prop-uid';
  uid.innerHTML=`<span>uid</span><span style="color:var(--text-dim);">#${sb.uid}</span>`;el.appendChild(uid);
}

function renderAI(){
  const el=document.getElementById('ai-msgs');el.innerHTML='';
  aiMessages.forEach(m=>{
    const wrap=document.createElement('div');wrap.className=`ai-msg ${m.role==='user'?'user':'bot'}`;
    const bubble=document.createElement('div');bubble.className='ai-bubble';bubble.textContent=m.text;
    const time=document.createElement('div');time.className='ai-time';time.textContent=m.time||'';
    wrap.appendChild(bubble);wrap.appendChild(time);el.appendChild(wrap);
  });
  el.scrollTop=el.scrollHeight;
  const qel=document.getElementById('ai-quick');qel.innerHTML='';
  AI_QUICK.forEach(q=>{
    const btn=document.createElement('button');btn.className='ai-q-btn';btn.textContent=q;
    btn.onclick=()=>{document.getElementById('ai-input').value=q;aiSend();};qel.appendChild(btn);
  });
}

function aiSend(){
  const inp=document.getElementById('ai-input');const txt=inp.value.trim();if(!txt)return;
  aiMessages.push({role:'user',text:txt,time:nowTime()});inp.value='';renderAI();
  const el=document.getElementById('ai-msgs');
  const typing=document.createElement('div');typing.className='ai-msg bot';typing.id='ai-typing';
  typing.innerHTML='<div class="ai-bubble" style="padding:7px 10px;"><span class="ai-typing-dot"></span><span class="ai-typing-dot"></span><span class="ai-typing-dot"></span></div>';
  el.appendChild(typing);el.scrollTop=el.scrollHeight;
  setTimeout(()=>{
    document.getElementById('ai-typing')?.remove();
    aiMessages.push({role:'bot',text:getAIResp(txt),time:nowTime()});renderAI();
  },600+Math.random()*700);
}

function getAIResp(q){
  const lq=q.toLowerCase();
  if(lq.includes('만약')||lq.includes('if'))return'만약 블록 사용법:\n\n만약 조건:\n    // 참일 때\n아니면:\n    // 거짓일 때';
  if(lq.includes('함수'))return'함수 선언:\n\n함수 더하기(정수 가, 정수 나):\n    반환 가 + 나\n\n정의 인사(문자 이름):\n    출력("안녕, " + 이름)';
  if(lq.includes('온톨로지'))return'온톨로지 블록:\n\n온톨로지 "내장":\n    개념 "제품":\n        속성 "이름" 문자\n    개념끝\n    관계 "구매함" 사람 제품\n온톨로지끝';
  if(lq.includes('예제')||lq.includes('sample'))return'기본 예제:\n\n정수 합 = 0\n반복 i 부터 1 까지 10:\n    합 = 합 + i\n출력("합: " + 합)';
  if(lq.includes('오류')||lq.includes('에러'))return'오류 수정 팁:\n1. 들여쓰기 확인 (공백 4칸)\n2. 블록 끝에 ":" 확인\n3. 변수 자료형 명시\n4. 헌법/법령으로 사전조건 추가';
  if(lq.includes('문법')||lq.includes('syntax'))return'Kcode 기본 문법:\n• 들여쓰기 기반 (Python 방식)\n• 한글 키워드\n• 자료형: 정수/실수/문자/글자/논리\n• 함수(반환있음) vs 정의(반환없음)\n• 계약/가속기/온톨로지/MCP 블록';
  if(lq.includes('블록 추가'))return'블록 추가 방법:\n1. 왼쪽 팔레트에서 드래그\n2. 워크스페이스에 드롭\n3. 속성 패널에서 파라미터 수정\n\n텍스트 모드에서 직접 코드 작성도 가능합니다!';
  return`"${q}"에 대해 답변드리겠습니다.\n\n더 구체적인 질문을 해주시면 더 정확히 도움드릴 수 있어요.\n예: "만약 블록 사용법", "함수 예제" 등`;
}

function renderTabContent(){
  const el=document.getElementById('tab-content');el.innerHTML='';
  if(activeTab==='콘솔'){
    const log=document.createElement('div');log.id='con-log';
    consoleLog.forEach(line=>{
      const d=document.createElement('div');d.className='log-line';
      const clr=line.t==='out'?'#00e890':line.t==='err'?'#ff6b35':'#00d4ff';
      const ac=line.t==='out'?'#005f38':line.t==='err'?'#883318':'#006888';
      const arr=document.createElement('span');arr.className='log-arr';arr.style.color=ac;arr.textContent='▶';
      const txt=document.createElement('span');txt.style.color=clr;txt.textContent=line.s;
      d.appendChild(arr);d.appendChild(txt);log.appendChild(d);
    });
    el.appendChild(log);return;
  }
  if(activeTab==='CLI'){renderCLI(el);return;}
  const empty=document.createElement('div');empty.className='tab-empty';empty.textContent=activeTab+' — 데이터 없음';el.appendChild(empty);
}

function renderCLI(container){
  const out=document.createElement('div');out.id='cli-out';
  out.onclick=()=>document.getElementById('cli-inp')?.focus();
  const clrMap={cmd:'#00d4ff',ok:'#00e890',err:'#ff6b35',sys:'#3a6a8a',dim:'#182a3a'};
  cliHist.forEach(line=>{
    const d=document.createElement('div');d.className='cli-ln';d.style.color=clrMap[line.t]||'#3a6a8a';d.textContent=line.s;out.appendChild(d);
  });
  const CLI_CMDS={
    help:()=>[{t:'sys',s:'명령: run / build / lex / parse / version / clear'}],
    version:()=>[{t:'ok',s:'Kcode v1.3.0 | kserver: localhost:8080'}],
    run:(a)=>[{t:'sys',s:`▶ kinterp ${a||'main.han'}`},{t:'ok',s:'안녕'},{t:'ok',s:'안녕'},{t:'sys',s:`✓ 완료 (0.00${Math.floor(Math.random()*8)+1}초)`}],
    build:(a)=>[{t:'sys',s:`⚙ 컴파일: ${a||'main.han'}`},{t:'ok',s:'main.kbc (1.2 KB)'},{t:'sys',s:'✓ 빌드 완료'}],
    lex:(a)=>[{t:'sys',s:`어휘: ${a||'main.han'}`},{t:'ok',s:'[만약][x][>][10][:][반복]...'},{t:'ok',s:'토큰 13개'}],
    parse:(a)=>[{t:'sys',s:`파싱: ${a||'main.han'}`},{t:'ok',s:'AST: PROGRAM→IF→FOR_RANGE→EXPR_STMT'},{t:'ok',s:'오류 0'}],
    clear:()=>'CLEAR',
  };
  const ir=document.createElement('div');ir.id='cli-inp-row';
  const pr=document.createElement('span');pr.className='cli-prom';pr.textContent='$';
  const inp=document.createElement('input');inp.id='cli-inp';inp.placeholder='명령어 입력... (help)';
  const sub=()=>{
    const cmd=inp.value.trim();if(!cmd){inp.value='';return;}
    const[c,...args]=cmd.split(' ');cmdHist.unshift(cmd);hIdx=-1;
    const fn=CLI_CMDS[c];
    if(!fn)cliHist.push({t:'cmd',s:`$ ${cmd}`},{t:'err',s:`알 수 없는 명령어: ${c}`});
    else{const res=fn(args.join(' '));if(res==='CLEAR')cliHist=[...CLI_INIT];else cliHist.push({t:'cmd',s:`$ ${cmd}`},...res);}
    inp.value='';renderTabContent();setTimeout(()=>document.getElementById('cli-inp')?.focus(),10);
  };
  inp.onkeydown=e=>{
    if(e.key==='Enter')sub();
    else if(e.key==='ArrowUp'){e.preventDefault();const ni=Math.min(hIdx+1,cmdHist.length-1);hIdx=ni;inp.value=cmdHist[ni]||'';}
    else if(e.key==='ArrowDown'){e.preventDefault();const ni=Math.max(hIdx-1,-1);hIdx=ni;inp.value=ni===-1?'':cmdHist[ni]||'';}
  };
  const rb=document.createElement('button');rb.className='cli-run';rb.textContent='실행';rb.onclick=sub;
  ir.appendChild(pr);ir.appendChild(inp);ir.appendChild(rb);
  container.appendChild(out);container.appendChild(ir);
  setTimeout(()=>inp.focus(),50);
}

function clearSel(e){if(e.target.id==='canvas'){selUid=null;renderAll();}}

async function handleRun(){
  if(running)return;running=true;
  const btn=document.getElementById('run-btn');btn.textContent='● 실행 중';btn.classList.add('running');
  activeTab='콘솔';renderTabs();
  await new Promise(r=>setTimeout(r,350));
  consoleLog.push({t:'info',s:'▶ 실행 시작...'},{t:'out',s:'안녕'},{t:'out',s:'안녕'},{t:'out',s:'안녕'},{t:'info',s:`실행 완료 (0.00${Math.floor(Math.random()*8)+1}초)`});
  renderTabContent();running=false;btn.textContent='▶ 실행';btn.classList.remove('running');
}

function renderAll(){
  renderMenus();renderTabs();renderBlockList();
  if(editorMode==='block')renderCanvas();
  renderOutline();renderProps();renderTabContent();
}

/* ── 리사이즈 핸들러 ─────────────────────────────────────── */
function initResize(){
  // 수직 핸들 (좌우 패널 크기 조절)
  document.querySelectorAll('.resize-handle-v').forEach(handle=>{
    let startX,startW,targetEl;
    handle.addEventListener('mousedown',e=>{
      startX=e.clientX;
      targetEl=document.getElementById(handle.dataset.target);
      startW=targetEl.getBoundingClientRect().width;
      handle.classList.add('dragging');
      document.body.style.cursor='col-resize';
      document.body.style.userSelect='none';
      const onMove=e=>{
        const delta=e.clientX-startX;
        const dir=handle.dataset.dir==='right'?1:-1;
        const newW=Math.max(parseInt(targetEl.style.minWidth||targetEl.style.getPropertyValue('min-width')||120),
                   Math.min(parseInt(targetEl.style.maxWidth||targetEl.style.getPropertyValue('max-width')||600),
                   startW+delta*dir));
        targetEl.style.width=newW+'px';
      };
      const onUp=()=>{
        handle.classList.remove('dragging');
        document.body.style.cursor='';document.body.style.userSelect='';
        document.removeEventListener('mousemove',onMove);
        document.removeEventListener('mouseup',onUp);
      };
      document.addEventListener('mousemove',onMove);
      document.addEventListener('mouseup',onUp);
    });
  });

  // 수평 핸들 (하단 패널 높이 조절)
  const hHandle=document.getElementById('bottom-resize');
  if(hHandle){
    let startY,startH;
    hHandle.addEventListener('mousedown',e=>{
      startY=e.clientY;
      startH=document.getElementById('bottom').getBoundingClientRect().height;
      hHandle.classList.add('dragging');
      document.body.style.cursor='row-resize';
      document.body.style.userSelect='none';
      const onMove=e=>{
        const delta=startY-e.clientY;
        const newH=Math.max(80,Math.min(window.innerHeight*0.6,startH+delta));
        document.getElementById('bottom').style.height=newH+'px';
      };
      const onUp=()=>{
        hHandle.classList.remove('dragging');
        document.body.style.cursor='';document.body.style.userSelect='';
        document.removeEventListener('mousemove',onMove);
        document.removeEventListener('mouseup',onUp);
      };
      document.addEventListener('mousemove',onMove);
      document.addEventListener('mouseup',onUp);
    });
  }
}

/* ── 초기화 ─────────────────────────────────────────────── */
document.addEventListener('DOMContentLoaded',()=>{
  document.getElementById('canvas').addEventListener('dragover',e=>e.preventDefault());
  document.getElementById('canvas').addEventListener('drop',e=>{
    e.preventDefault();const id=e.dataTransfer.getData('blockId');
    if(!id||!BD[id])return;const d=BD[id];const params={};
    d.params.forEach(k=>params[k]='');
    ws.push({uid:newUid(),id,params,children:[]});renderAll();
  });
  document.getElementById('ai-input').addEventListener('keydown',e=>{
    if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();aiSend();}
  });
  initResize();
});

renderAll();
renderAI();
