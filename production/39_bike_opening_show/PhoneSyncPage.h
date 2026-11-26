#pragma once

const char PHONE_SYNC_PAGE[] PROGMEM = R"html(
<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>点灯式 進行送信</title>
<style>
:root{color-scheme:dark}
body{margin:0;background:#10181f;color:#f7fafc;font:16px/1.55 system-ui,sans-serif}
main{width:min(calc(100% - 2rem),36rem);margin:1rem auto}
section{background:#1a2732;border:1px solid #405361;border-radius:.65rem;padding:1rem}
h1{font-size:1.4rem;margin:.1rem 0 .75rem}
p{margin:.5rem 0}
#sync-state{font-weight:700;color:#ffe29b}
#sync-state[data-sent]{color:#9fe5c5}
small{color:#b9cad6}
a{color:#9fe5c5}
</style>
</head>
<body>
<main>
  <section data-phone-sync>
    <h1>点灯式 進行送信</h1>
    <p id="sync-state" role="status">ESPとの接続を確認中</p>
    <p>Sitesの最終受信 <time id="sync-received">—</time></p>
    <p><small>モバイルデータ通信をオンにして、ESPの画面を前面に表示します。「会場の進行を見る」に切り替えても自動で送信します。</small></p>
    <p><a href="/view">会場の進行を見る</a></p>
    <noscript>自動送信にはブラウザーのJavaScriptを有効にしてください。</noscript>
  </section>
</main>
<script src="/phone-sync.js" defer></script>
</body>
</html>
)html";

const char PHONE_SYNC_SCRIPT[] PROGMEM = R"js(
(() => {
const state = document.getElementById('sync-state');
const receivedAt = document.getElementById('sync-received');
if (!state || !receivedAt || !['/view', '/send'].includes(location.pathname)) return;
state.closest('[data-phone-sync]').hidden = false;
const endpoint = 'https://bicycle-illuminations-1128.feyhawthorn16.chatgpt.site/sync';
const freshnessMs = 6500;
const format = new Intl.DateTimeFormat('ja-JP', {timeZone: 'Asia/Tokyo', hour: '2-digit', minute: '2-digit', second: '2-digit'});
let lastSignature = '';
let freshUntil = 0;
let pollTimer;
let expiryTimer;
let pending;

const espUnavailable = {
  unconfigured: 'ESPの送信設定が未登録です。機材担当に設定を依頼してください',
  unavailable: 'ESPが送信を準備できません。機材担当はシリアルの site status を確認してください',
};

function showStatus(message, live = false) {
  if (state.textContent !== message) state.textContent = message;
  state.toggleAttribute('data-sent', live);
}

async function sync() {
  if (document.hidden || pending) return;
  const controller = new AbortController();
  pending = controller;
  const deadline = setTimeout(() => controller.abort(), 4500);
  let hasSnapshot = false;
  try {
    const snapshot = await fetch('/phone-sync', {cache: 'no-store', signal: controller.signal});
    if (!snapshot.ok) {
      const reason = snapshot.status === 503 ? await snapshot.text() : '';
      if (!controller.signal.aborted) {
        if (snapshot.status === 503) showStatus(espUnavailable[reason] ?? 'ESPに接続済み・進行データ待ち');
        else showStatus('ESPの進行データを取得できません。機材担当にファームウェアの版を確認してください');
      }
      return;
    }
    const signature = snapshot.headers.get('X-Snapshot-Signature');
    const body = await snapshot.text();
    if (document.hidden || controller.signal.aborted) return;
    if (!signature || !body) {
      showStatus('ESPの進行データを読み取れません');
      return;
    }
    if (lastSignature === signature && state.hasAttribute('data-sent') && performance.now() < freshUntil) return;
    hasSnapshot = true;
    const requestStarted = performance.now();
    const result = await fetch(endpoint, {
      method: 'POST', credentials: 'omit', redirect: 'error', signal: controller.signal,
      headers: {'Content-Type': 'application/json', 'X-Snapshot-Signature': signature}, body,
    });
    if (document.hidden || controller.signal.aborted) return;
    if (result.status !== 204) {
      if (result.status === 403) {
        showStatus('Sitesが送信を拒否しました。機材担当に送信設定を確認してください');
      } else {
        showStatus('Sitesが受信できません。自動で再試行します');
      }
      return;
    }
    const start = result.headers.get('X-TBT-Staff-Start');
    if (start) {
      await fetch('/staff-start', {method: 'POST', headers: {'Content-Type': 'text/plain'},
        body: start, cache: 'no-store', signal: controller.signal}).catch(() => {});
    }
    const rate = result.headers.get('X-TBT-Ride-Rate');
    if (rate) {
      await fetch('/ride-rate', {method: 'POST', headers: {'Content-Type': 'text/plain'},
        body: rate, cache: 'no-store', signal: controller.signal}).catch(() => {});
    }
    const timestamp = Number(result.headers.get('X-TBT-Received-At'));
    const ageHeader = result.headers.get('X-TBT-Received-Age');
    const age = Number(ageHeader);
    if (!Number.isSafeInteger(timestamp) || timestamp <= 0 || !ageHeader || !Number.isSafeInteger(age) || age < 0) {
      showStatus('Sitesの受信確認待ち');
      return;
    }
    lastSignature = signature;
    const time = new Date(timestamp);
    receivedAt.dateTime = time.toISOString();
    receivedAt.textContent = format.format(time);
    freshUntil = requestStarted + freshnessMs - age;
    const remaining = Math.max(0, freshUntil - performance.now());
    clearTimeout(expiryTimer);
    if (remaining > 0) {
      showStatus('Sitesに同期中', true);
      expiryTimer = setTimeout(() => {
        if (state.hasAttribute('data-sent')) showStatus('Sitesへの同期が途切れました');
      }, remaining);
    } else showStatus('Sitesへの同期が途切れました');
  } catch {
    if (!document.hidden && pending === controller) showStatus(hasSnapshot ? 'Sitesに届きません・モバイルデータ通信を確認' :
      'ESPにつながりません・Wi-Fiを確認');
  } finally {
    clearTimeout(deadline);
    if (pending === controller) {
      pending = null;
      if (!document.hidden) pollTimer = setTimeout(sync, 1000);
    }
  }
}

document.addEventListener('visibilitychange', () => {
  clearTimeout(pollTimer);
  clearTimeout(expiryTimer);
  freshUntil = 0;
  if (document.hidden) {
    const controller = pending;
    pending = null;
    controller?.abort();
    showStatus('画面を開くと同期を再開します');
    return;
  }
  showStatus('Sitesの同期状況を確認中');
  void sync();
});
void sync();
})();
)js";
