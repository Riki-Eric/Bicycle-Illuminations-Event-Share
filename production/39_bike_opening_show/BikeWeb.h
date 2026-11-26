#pragma once

const char BIKE_WEB[] PROGMEM = R"html(
<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>点灯式 本番運営</title>
<style>
:root{color-scheme:dark;--bg:#10181f;--panel:#1a2732;--line:#405361;--muted:#b9cad6;--mint:#9fe5c5;--yellow:#ffe29b;--red:#ffbaad}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:#f7fafc;font:16px/1.5 system-ui,sans-serif}
header,main{width:min(100% - 2rem,62rem);margin:auto}
header{display:flex;align-items:center;justify-content:space-between;gap:.75rem;padding:1rem 0}
h1,h2,p{margin:0}
h1{font-size:1.22rem}
h2{font-size:1rem}
small,.muted{color:var(--muted)}
a{color:var(--mint)}
main{display:grid;gap:.8rem;padding-bottom:1.5rem;grid-template-areas:"stage" "action" "flow" "progress" "peers"}
section{min-width:0;background:var(--panel);border:1px solid var(--line);border-radius:.65rem;padding:1rem}
.stage{grid-area:stage}
.actions{grid-area:action;border-color:#678493}
.flow{grid-area:flow}
.progress{grid-area:progress}
.peers{grid-area:peers}
@media(min-width:48rem){main{grid-template-columns:minmax(0,1.22fr) minmax(0,1fr);grid-template-areas:"stage action" "flow action" "progress peers"}}
.status-line{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:.3rem}
#stage{font-size:clamp(1.5rem,5vw,2.4rem);font-weight:750;line-height:1.25;margin:.35rem 0}
#connection{font-weight:650;color:var(--yellow)}
#connection[data-live=true],.good{color:var(--mint)}
[data-phone-sync]{border-top:1px solid var(--line);padding-top:.55rem;margin-top:.55rem}
#sync-state{color:var(--yellow)}
#sync-state[data-sent]{color:var(--mint)}
.warn{color:var(--yellow)}
.bad{color:var(--red)}
#mode,#output,#wing-note{font-size:.88rem}
button{font:inherit;font-weight:700;min-height:44px;padding:.55rem .8rem;border-radius:.4rem;border:1px solid #738b9b;background:#283e4c;color:#f7fafc;cursor:pointer}
button:disabled{opacity:.48;cursor:default}
button:focus-visible,a:focus-visible{outline:3px solid #fff;outline-offset:3px}
.primary{background:var(--mint);color:#102c21;border-color:var(--mint)}
#hold{background:var(--yellow);color:#3d2810;border-color:var(--yellow);touch-action:none;user-select:none;-webkit-user-select:none}
.full{width:100%;min-height:52px}
.action-list{display:grid;gap:.45rem;margin-top:.75rem}
#instruction,#note{font-size:.88rem;margin-top:.5rem}
#note{color:var(--yellow)}
#note:empty{display:none}
.details{margin-top:.35rem;font-size:.88rem}
details button{width:100%;margin-top:.5rem}
.light-row{display:grid;grid-template-columns:1fr auto;gap:.35rem;align-items:center;border-top:1px solid var(--line);margin-top:.45rem;padding-top:.25rem}
.light-row button{margin:0;font-size:.83rem}
.light-row small{display:block}
.steps{display:flex;gap:.35rem .55rem;align-items:center;flex-wrap:wrap;margin:.65rem 0 0;padding:0;list-style:none}
.steps li{display:inline-flex;align-items:center;gap:.55rem;font-size:.84rem}
.steps li:not(:last-child)::after{content:"→";color:#92a6b4}
.steps span{display:inline-block;border:1px solid var(--line);border-radius:.35rem;padding:.23rem .45rem;color:var(--muted)}
.steps [aria-current] span{border-color:var(--mint);color:#112c22;background:var(--mint);font-weight:750}
.ride{display:grid;grid-template-columns:1fr auto;align-items:center;gap:.15rem .5rem;margin:.65rem 0}
progress{grid-column:1 / -1;display:block;width:100%;height:.6rem;accent-color:var(--mint)}
output{font-variant-numeric:tabular-nums}
table{width:100%;border-collapse:collapse;font-size:.86rem;text-align:left;margin:.45rem 0}
td,th{padding:.33rem .22rem;vertical-align:top;border-bottom:1px solid var(--line)}
tbody th{font-weight:600}
td:last-child{width:55%}
nav{margin-top:.75rem;font-size:.86rem}
[hidden]{display:none!important}
body[data-view=true] main{grid-template-areas:"stage" "flow" "progress" "peers"}
@media(min-width:48rem){body[data-view=true] main{grid-template-areas:"stage peers" "flow peers" "progress peers"}}
</style>
</head>
<body>
<header>
  <div><h1>点灯式 本番運営</h1><small id="view-label">進行コントローラー（自転車側）</small></div>
  <a href="/send">Sitesの送信状況</a>
</header>
<main>
  <section class="stage" aria-label="現在の進行">
    <div class="status-line"><h2>現在の進行</h2><small id="connection" role="status">進行コントローラーに接続中</small></div>
    <p id="stage" role="status">状態を取得中</p>
    <p id="mode" class="muted"></p><p id="output" class="muted"></p>
    <div class="status-line" data-phone-sync hidden>
      <strong id="sync-state" role="status">Sitesの同期状況を確認中</strong>
      <small>Sitesの最終受信 <time id="sync-received">—</time></small>
    </div>
    <nav><a id="view-link" href="/view">関係者用の閲覧画面</a></nav>
  </section>
  <section class="actions" id="actions" aria-labelledby="actions-title">
    <h2 id="actions-title">開演操作</h2>
    <p id="instruction" class="muted">進行コントローラーの応答を待っています。</p>
    <div class="action-list" id="arm-controls" hidden>
      <button class="primary full" data-action="arm" disabled>開演待機に入る</button>
      <details class="details"><summary>短縮・市長ボタンなしで実施</summary>
        <button data-action="short" disabled>短縮版の開演待機</button>
        <button data-action="local" disabled>市長ボタンなしで開演待機</button>
      </details>
    </div>
    <div class="action-list" id="start-controls" hidden>
      <button class="primary full" data-action="confirm" disabled>市長の押下を確認</button>
      <button id="hold" class="full" hidden disabled>予備開始を1秒押して離す</button>
      <button data-action="cancel" hidden disabled>押下確認を取り消す</button>
    </div>
    <div id="cue-controls" class="action-list" hidden>
      <button class="primary full" data-action="advance" disabled>次の演出を予備開始</button>
      <details class="details">
        <summary>照明系統ごとの手動停止・再開</summary>
        <div id="lights"></div>
      </details>
    </div>
    <p id="note" role="status"></p>
  </section>
  <section class="flow" aria-labelledby="flow-title"><h2 id="flow-title">本番フロー</h2><ol class="steps" id="flow"></ol></section>
  <section class="progress" aria-labelledby="ride-title">
    <h2 id="ride-title">自転車の到達率</h2>
    <div class="ride"><label for="a">A アップライト</label><output id="a-value" for="a">—</output><progress id="a" max="100"></progress></div>
    <div class="ride"><label for="b">B アップライト</label><output id="b-value" for="b">—</output><progress id="b" max="100"></progress></div>
    <p id="wing-note" class="muted">翼Aの応答を待っています。</p>
  </section>
  <section class="peers" aria-labelledby="peers-title">
    <h2 id="peers-title">各ESP32の応答</h2>
    <table aria-labelledby="peers-title">
      <thead><tr><th scope="col">担当</th><th scope="col">最新報告</th></tr></thead>
      <tbody id="boxes"></tbody>
    </table>
    <small>点灯・点滅は現地で目視。回転表示器は状態報告がないため現地で確認。</small>
  </section>
</main>
<script>
const get = id => document.getElementById(id);
const stage = get('stage'), note = get('note'), hold = get('hold');
const monitor = location.pathname === '/view';
const actions = [...document.querySelectorAll('[data-action]')];
const byAction = Object.fromEntries(actions.map(button => [button.dataset.action, button]));
const page = crypto.getRandomValues(new Uint32Array(1))[0] || 1;
const labels = {
  OFF:'停止・待機', ARMED:'開演待ち', TRAIL:'軌跡点灯', RIDE:'両翼の蓄積',
  WHITE:'白点灯', SPARKLE:'きらめき', DRAPE:'ドレープ点灯', HOLD:'点灯保持',
  'SHORT HOLD':'短縮版の点灯保持', FADE:'消灯へフェード', 'TREE CHARGE':'ツリーの蓄積', STEADY:'定常運転'
};
const phaseNames = Object.keys(labels);
const fullFlow = ['OFF','ARMED','TRAIL','RIDE','TREE CHARGE','WHITE','SPARKLE','DRAPE','HOLD','STEADY'];
const shortFlow = ['OFF','ARMED','TRAIL','RIDE','SHORT HOLD','FADE'];
const peerNames = ['', '42 市長ボタン台', '43 翼A', '40 ツリー下', '40 ツリー上',
  '38 ドレープA', '38 ドレープB', '41 外周1', '41 外周2', '41 外周3'];
const sources = ['', '市長ボタン', 'Web予備開始', '進行コントローラーの現地ボタン', 'シリアル操作'];
const lightNames = ['', '', '翼A', 'ツリー下', 'ツリー上', 'ドレープA', 'ドレープB',
  '外周1', '外周2', '外周3', '軌跡', 'アップライトAの車体', 'アップライトBの車体', '人物照明'];
const lightRows = [];
let state, lastState, press, attempt = 0, busy = false, flowKey = '';

if (monitor) {
  document.body.dataset.view = 'true';
  get('view-label').textContent = '閲覧用 ・ 操作は登録済みの会場PCから';
  get('view-link').hidden = true;
  get('actions').hidden = true;
} else {
  for (let id = 2; id < lightNames.length; id++) {
    const row = document.createElement('div');
    row.className = 'light-row';
    const label = document.createElement('div');
    label.textContent = lightNames[id];
    const status = document.createElement('small');
    label.append(status);
    const button = document.createElement('button');
    button.type = 'button';
    button.onclick = () => void runAction(button.dataset.command, id);
    row.append(label, button);
    get('lights').append(row);
    lightRows.push({id, row, status, button});
  }
}

function showFlow(snapshot) {
  const key = snapshot ? snapshot.phase + ':' + snapshot.short : 'offline';
  if (key === flowKey) return;
  flowKey = key;
  const steps = snapshot?.short && snapshot.phase !== 'OFF' ? shortFlow : fullFlow;
  get('flow').replaceChildren(...steps.map(phase => {
    const item = document.createElement('li');
    const span = document.createElement('span');
    span.textContent = labels[phase];
    if (snapshot?.phase === phase) item.setAttribute('aria-current', 'step');
    item.append(span);
    return item;
  }));
}

function showPeers(snapshot) {
  const boxes = get('boxes');
  const peers = snapshot?.peers ?? [];
  if (!boxes.children.length) {
    for (const name of ['進行コントローラー（自転車側）', ...peerNames.slice(1)]) {
      const row = boxes.insertRow();
      const label = document.createElement('th');
      label.scope = 'row';
      label.textContent = name;
      row.append(label);
      row.insertCell();
    }
  }
  for (const [id, row] of [...boxes.rows].entries()) {
    const cell = row.cells[1];
    const peer = peers.find(item => item.id === id);
    let [status, tone] = ['確認不可', 'muted'];
    if (snapshot && id === 0) {
      [status, tone] = snapshot.fault ? ['出力系統異常', 'bad'] : ['応答あり', 'good'];
    } else if (snapshot && peer) {
      const unused = (id === 9 && snapshot.plazas === 2) ||
        (snapshot.phase !== 'OFF' && snapshot.short && id >= 3);
      if (unused) [status, tone] = ['今回の演出対象外', 'muted'];
      else if (peer.conflict) [status, tone] = ['同じ役割のIDが重複', 'bad'];
      else if (!peer.link) [status, tone] = ['応答なし', 'warn'];
      else if (peer.fault) [status, tone] = ['機器から異常報告', 'bad'];
      else if (snapshot.phase !== 'OFF' && !peer.following) [status, tone] = ['今回の進行への応答待ち', 'warn'];
      else if (peer.muted) [status, tone] = ['個別停止を受信', 'warn'];
      else if (peer.done) [status, tone] = ['最終フレーム送信済', 'good'];
      else if (peer.pressed) [status, tone] = ['ボタン信号を受信', 'good'];
      else if (peer.ready) [status, tone] = ['応答あり・準備可', 'good'];
      else [status, tone] = ['応答あり・準備待ち', 'warn'];
    }
    if (cell.textContent !== status) cell.textContent = status;
    cell.className = tone;
  }
}

function showRide(snapshot) {
  const wing = snapshot?.peers.find(peer => peer.id === 2);
  const live = wing?.link && wing.following && wing.ready;
  const show = snapshot && !['OFF','ARMED','TRAIL'].includes(snapshot.phase);
  for (const rider of ['a','b']) {
    const bar = get(rider), value = get(rider + '-value');
    bar.value = show ? snapshot[rider] : 0;
    value.textContent = show ? snapshot[rider] + '%' : '—';
  }
  get('wing-note').textContent = !snapshot ? '通信の復帰を待っています。' : !show ? '走行開始後に翼Aの報告を表示します。' :
    live ? '翼Aからの到達率' : '翼Aの有効な応答待ち。数値は最後に受理した到達率。';
}

function showActions() {
  if (monitor) return;
  const armed = state?.phase === 'ARMED';
  const off = state?.phase === 'OFF';
  const active = state && !armed && !off;
  const next = active && state.next !== state.phaseId && labels[phaseNames[state.next]];
  const fallback = state?.fallback;
  const waiting = armed && !state.accepted && !state.fault;
  const confirmable = waiting && fallback === 0;
  const canHold = waiting && fallback === 6;
  const canCancel = waiting && [1,6,7].includes(fallback);

  get('arm-controls').hidden = !off;
  get('start-controls').hidden = !waiting;
  byAction.confirm.hidden = !confirmable;
  byAction.cancel.hidden = !canCancel;
  hold.hidden = !canHold;
  get('cue-controls').hidden = !active;
  byAction.advance.hidden = !next;
  byAction.advance.disabled = busy || !next || !!state?.fault;
  if (next) byAction.advance.textContent = state.next === 11 ? '定常運転に切り替える' :
    state.next === 1 ? '点灯式の開演待機に戻す' : next + 'を予備開始';
  for (const {id, row, status, button} of lightRows) {
    const peer = state?.peers.find(item => item.id === id);
    const requested = !!(state?.muted & (1 << id));
    const connected = id >= 10 || (peer?.link && peer.following && !peer.fault);
    row.hidden = id === 9 && state?.plazas !== 3;
    button.disabled = !active || busy || !connected || !!state?.fault;
    button.dataset.command = requested ? 'light-resume' : 'light-stop';
    button.textContent = requested ? '再開' : '手動停止';
    button.setAttribute('aria-label', lightNames[id] + 'を' + button.textContent);
    status.textContent = !connected ? '応答待ち' :
      id < 10 && !!peer.muted !== requested ? '反映待ち' : requested ? '個別停止' : '演出に連動';
  }
  for (const mode of ['arm','short','local']) byAction[mode].disabled = !off || busy || !!state.fault || !!state.test;
  byAction.confirm.disabled = busy || !confirmable;
  byAction.cancel.disabled = busy || !canCancel;
  hold.disabled = busy || !canHold;

  const text = !state ? '通信が戻るまで操作は無効です。' :
    state.fault ? '進行コントローラーが出力異常を報告しています。' :
    state.test ? '進行コントローラーで単体試験中です。' :
    off ? '開演の合図を受ける前に、開演待機に入ります。' :
    !armed ? '前の演出が終わり、次が始まっていないときに予備開始を押してください。' :
    fallback === 1 ? '市長ボタンからの開始を監視中。あと約 ' + Math.ceil(state.remaining / 1000) + ' 秒。' :
    fallback === 6 ? '軌跡が始まっていないことを目視し、予備開始を長押ししてください。' :
    fallback === 7 ? '予備操作の受付時間が終了しました。押下確認を取り消して再確認できます。' :
    '市長がボタンを押すのに合わせて、担当者が一度押してください。';
  if (get('instruction').textContent !== text) get('instruction').textContent = text;
}

function render() {
  const connection = get('connection');
  const connected = state ? '進行コントローラーと通信中' : '進行コントローラーと通信切断';
  connection.dataset.live = !!state;
  if (connection.textContent !== connected) connection.textContent = connected;
  const name = state ? labels[state.phase] : lastState ? '通信切断 · 最終受信 ' + labels[lastState.phase] : '状態を取得できません';
  if (stage.textContent !== name) stage.textContent = name;
  get('mode').textContent = state ? (state.short && state.phase !== 'OFF' ? '短縮版' : '外周 ' + state.plazas + ' 台') +
    (state.test ? ' ・ 単体試験中' : '') + (state.fault ? ' ・ 出力異常' : '') : '';
  get('output').textContent = state?.accepted ? (state.phase === 'OFF' ? '直前の開演 ' : '開演経路 ') + sources[state.source] +
    (state.output === 1 ? ' ／ 軌跡データ送信済' : state.output === 3 ? ' ／ 軌跡出力異常' : ' ／ 軌跡データ送信待ち') : '';
  showActions();
  showFlow(state);
  showRide(state);
  showPeers(state);
}

async function action(command, mark = state, seq = 0, id = 0, light = 0) {
  if (!mark || monitor) return false;
  const body = new URLSearchParams({action:command, token:mark.token, run:mark.run, rev:mark.rev,
    phase:mark.phaseId, page, attempt:id, step:seq, light});
  try {
    const response = await fetch('/action', {method:'POST', body, signal:AbortSignal.timeout(900)});
    if (!response.ok && command !== 'abort') note.textContent = '操作は受理されませんでした。最新の進行と各ESP32の応答を確認してください。';
    return response.ok;
  } catch {
    if (command !== 'abort') note.textContent = '操作の応答がありません。画面の進行を確認してください。';
    return false;
  }
}

async function refresh() {
  try {
    const response = await fetch(monitor ? '/status' : '/operator-status', {cache:'no-store', signal:AbortSignal.timeout(900)});
    if (!response.ok) throw new Error('status');
    const snapshot = await response.json();
    if (!Object.hasOwn(labels, snapshot.phase) || !Array.isArray(snapshot.peers)) throw new Error('status');
    if (lastState?.phase !== snapshot.phase) note.textContent = '';
    state = lastState = snapshot;
  } catch {
    state = null;
  }
  if (press && (!state || state.run !== press.mark.run || state.rev !== press.mark.rev ||
      state.phase !== 'ARMED' || state.fallback !== 6)) void finish(true);
  render();
  setTimeout(refresh, 500);
}

async function runAction(command, light = 0) {
  if (!state || monitor || busy) return;
  const mark = state;
  if (command === 'local' && !confirm('市長ボタンなしで開演待機に入りますか？')) return;
  if (command === 'advance' && [1, 11].includes(state?.next) && !confirm(byAction.advance.textContent + '操作を実行しますか？')) return;
  busy = true;
  showActions();
  note.textContent = '';
  if (await action(command, mark, 0, 0, light)) note.textContent = '受け付けました。会場の点灯を確認してください。';
  busy = false;
  showActions();
}
for (const button of actions) button.onclick = () => void runAction(button.dataset.action);

async function finish(cancelled) {
  const current = press;
  if (!current) return;
  press = null;
  clearInterval(current.timer);
  hold.textContent = '予備開始を1秒押して離す';
  busy = true;
  showActions();
  if (current.pending) await current.pending;
  const held = !cancelled && current.ok && state?.run === current.mark.run &&
    state.rev === current.mark.rev && state.phase === 'ARMED' && state.fallback === 6 &&
    performance.now() - current.started >= 1100;
  if (await action(held ? 'release' : 'abort', current.mark, current.seq + 1, current.id) && held) {
    note.textContent = '予備開始を受け付けました。軌跡を目視してください。';
  }
  busy = false;
  showActions();
}

async function begin(input) {
  if (press || !state || hold.disabled) return;
  const current = {mark:state, input, id:++attempt, seq:0, started:Infinity, ok:true};
  press = current;
  note.textContent = '';
  hold.textContent = '押したまま待つ';
  current.pending = action('hold', current.mark, 0, current.id).then(ok => current.ok = ok);
  await current.pending;
  current.pending = null;
  if (press !== current) return;
  if (!current.ok) { void finish(true); return; }
  current.started = performance.now();
  current.timer = setInterval(() => {
    if (current.pending) return;
    if (performance.now() - current.started >= 3900) { void finish(true); return; }
    if (performance.now() - current.started >= 1100) hold.textContent = '指を離して予備開始';
    current.pending = action('step', current.mark, ++current.seq, current.id).then(ok => {
      current.ok = ok;
      current.pending = null;
      if (!ok && press === current) void finish(true);
    });
  }, 160);
}

hold.onpointerdown = event => {
  if (!event.isPrimary || event.button !== 0 || press || hold.disabled) return;
  hold.setPointerCapture(event.pointerId);
  void begin(event.pointerId);
};
hold.onpointerup = event => { if (press?.input === event.pointerId) void finish(false); };
hold.onpointercancel = hold.onlostpointercapture = event => {
  if (press?.input === event.pointerId) void finish(true);
};
hold.onkeydown = event => {
  if (event.key !== ' ' && event.key !== 'Enter') return;
  event.preventDefault();
  if (!event.repeat) void begin(event.key);
};
hold.onkeyup = event => {
  if (press?.input === event.key) { event.preventDefault(); void finish(false); }
};
hold.onblur = () => { if (typeof press?.input === 'string') void finish(true); };
window.addEventListener('blur', () => void finish(true));
window.addEventListener('pagehide', () => void finish(true));
document.addEventListener('visibilitychange', () => { if (document.hidden) void finish(true); });
showFlow();
showPeers();
refresh();
</script>
<script src="/phone-sync.js" defer></script>
</body>
</html>
)html";
