// Drives the real web UI in headless Chrome through the DevTools protocol: signs up, makes
// topics/posts/replies, likes, edits, searches, checks that hostile text cannot run, that phones
// get no horizontal scroll, and that no console error or CSP violation appears. Screenshots go
// to <outDir>. Needs Node 22+ (global WebSocket); run it through tests/ui_check.sh.
// usage: node ui_check.mjs <baseUrl> <outDir> <chromeDebugPort>
import fs from 'node:fs';

const [BASE, OUT, CP] = process.argv.slice(2);
fs.mkdirSync(OUT, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const version = await (await fetch(`http://127.0.0.1:${CP}/json/version`)).json();
const ws = new WebSocket(version.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));

let nextId = 0;
const pending = new Map();
const events = [];
ws.addEventListener('message', (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) {
    const { res, rej } = pending.get(msg.id);
    pending.delete(msg.id);
    msg.error ? rej(new Error(JSON.stringify(msg.error))) : res(msg.result);
  } else if (msg.method) {
    events.push(msg);
  }
});
const send = (method, params = {}, sessionId) => new Promise((res, rej) => {
  const id = ++nextId;
  pending.set(id, { res, rej });
  ws.send(JSON.stringify({ id, method, params, sessionId }));
});

const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
const cmd = (m, p) => send(m, p, sessionId);
for (const d of ['Page', 'Runtime', 'Log', 'Network']) await cmd(`${d}.enable`);

const problems = [];
let consumed = 0;
function drainEvents() {
  for (; consumed < events.length; consumed++) {
    const e = events[consumed];
    if (e.method === 'Runtime.exceptionThrown')
      problems.push('exception: ' + (e.params.exceptionDetails.exception?.description || e.params.exceptionDetails.text));
    if (e.method === 'Runtime.consoleAPICalled' && ['error', 'warning'].includes(e.params.type))
      problems.push(`console.${e.params.type}: ` + e.params.args.map((a) => a.value ?? a.description).join(' '));
    // The browser logs every non-2xx fetch; 400/401/409 are the API's normal answers to the negative tests
    const expectedApiError = (t) => /Failed to load resource: the server responded with a status of (400|401|409)\b/.test(t || '');
    if (e.method === 'Log.entryAdded' && ['error', 'warning'].includes(e.params.entry.level) && !expectedApiError(e.params.entry.text))
      problems.push(`log.${e.params.entry.level}: ${e.params.entry.text} ${e.params.entry.url || ''}`);
  }
}

const js = async (expression) => {
  const r = await cmd('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error('eval failed: ' + (r.exceptionDetails.exception?.description || r.exceptionDetails.text) + '\n  in: ' + expression);
  return r.result.value;
};
const waitFor = async (expression, what, ms = 6000) => {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    if (await js(expression)) return;
    await sleep(40);
  }
  drainEvents();
  throw new Error(`timed out waiting for: ${what}\n  page: ${await js('location.hash + " | " + document.body.innerText.slice(0, 300)')}\n  problems: ${problems.join(' || ')}`);
};
const q = (sel) => JSON.stringify(sel);
const fill = (sel, text) => js(`(() => { const e = document.querySelector(${q(sel)}); if (!e) throw new Error('missing ${sel}'); e.value = ${JSON.stringify(text)}; e.dispatchEvent(new Event('input', { bubbles: true })); })()`);
const submit = (sel) => js(`document.querySelector(${q(sel)}).requestSubmit()`);
const clickText = (tag, text) => js(`(() => { const e = [...document.querySelectorAll(${q(tag)})].find((x) => x.textContent.trim().startsWith(${JSON.stringify(text)})); if (!e) throw new Error('no ${tag} starting with ${text}'); e.click(); })()`);
const shot = async (name) => {
  await js(`document.getElementById('toast').className = ''`);  // keep leftover toasts out of the pictures
  await sleep(300);
  const { data } = await cmd('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync(`${OUT}/${name}.png`, Buffer.from(data, 'base64'));
};
const viewport = (width, height, mobile = false) => cmd('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 2, mobile });
const scheme = (value) => cmd('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-color-scheme', value }] });
const check = (cond, what) => { if (!cond) { problems.push('CHECK FAILED: ' + what); console.log('  FAIL', what); } else console.log('  ok  ', what); };
const apiPost = (path, params) => js(`fetch(${q(path)}, { method: 'POST', headers: { 'X-Forum': '1', 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(${JSON.stringify(params)}) }).then((r) => r.json())`);

await viewport(1000, 820);
await scheme('light');

// ---- sign-in screen ----
console.log('sign-in');
await cmd('Page.navigate', { url: BASE + '/' });
await waitFor(`location.hash === '#/login' && !!document.querySelector('form')`, 'redirect to the sign-in page');
check(await js(`!!document.querySelector('.ascii')`), 'the C++ logo is shown');
await shot('01-login');

// wrong password first
await fill('input[name=username]', 'nobody');
await fill('input[name=password]', 'wrongpass');
await submit('#app form');
await waitFor(`!!document.querySelector('.error:not([hidden])')`, 'an error message for a bad login');
check((await js(`document.querySelector('.error').textContent`)).includes('Wrong username or password'), 'bad login shows an error');

// register alice through the UI
await clickText('.seg button', 'Create account');
await fill('input[name=username]', 'alice');
await fill('input[name=password]', 'secret1');
await submit('#app form');
await waitFor(`document.querySelector('h1')?.textContent === 'Topics'`, 'topics page after registering');
check((await js(`document.getElementById('nav').textContent`)).includes('Signed in as alice'), 'nav shows the signed-in user');
await shot('02-topics-empty');

// ---- seed some content quickly through the API, then use the UI for the main flow ----
await apiPost('/api/topics', { title: 'Pointers vs references', description: 'When do you reach for one over the other?' });
await apiPost('/api/topics', { title: 'Best C++ build system?', description: 'CMake, Meson, Bazel... or just a Makefile.' });
await apiPost('/api/topics', { title: 'Why does my vector crash on pop?', description: 'Classic off-by-one in a hand-written container.' });

await js(`render()`);
await waitFor(`document.querySelectorAll('article.card').length === 3`, 'three topic cards');

// ---- create a topic through the form ----
console.log('create topic');
await js(`document.querySelector('details').open = true`);
await fill('details input[name=title]', 'Move semantics');
await fill('details textarea[name=description]', 'std::move, rvalue references and when copies disappear.');
await shot('03-new-topic-form');
await submit('details form');
await waitFor(`document.querySelector('h1')?.textContent === 'Move semantics'`, 'the new topic page');
check(await js(`location.hash.startsWith('#/topic/')`), 'navigated to the topic');

// ---- posts, including hostile text ----
console.log('posts');
await fill('textarea[name=text]', 'Moving avoids a deep copy: the source is left valid but unspecified.');
await submit('#app form');
await waitFor(`document.querySelectorAll('article.card .body').length >= 2`, 'first post appears');

await fill('textarea[name=text]', '<img src=x onerror="window.__xss=1"><script>window.__xss=2</script> & "quotes"');
await submit('#app form');
await waitFor(`document.body.innerText.includes('onerror')`, 'hostile post shown as plain text');
await sleep(200);
check(await js(`window.__xss === undefined`), 'hostile post did not execute');
check(await js(`document.querySelectorAll('img[src="x"]').length === 0`), 'hostile post created no <img> element');

// duplicate text is rejected with a visible message
await fill('textarea[name=text]', 'Moving avoids a deep copy: the source is left valid but unspecified.');
await submit('#app form');
await waitFor(`!!document.querySelector('.error:not([hidden])')`, 'duplicate post error');
check((await js(`document.querySelector('.error:not([hidden])').textContent`)).includes('already has a post'), 'duplicate post shows an error');

// like the topic and toggle it back
const topicLike = `document.querySelector('article.card .card-foot .like')`;
await js(`${topicLike}.click()`);
await waitFor(`${topicLike}.getAttribute('aria-pressed') === 'true' && ${topicLike}.textContent.includes('1')`, 'topic liked');
await shot('04-topic');
await js(`${topicLike}.click()`);
await waitFor(`${topicLike}.getAttribute('aria-pressed') === 'false' && ${topicLike}.textContent.includes('0')`, 'topic unliked');
check(true, 'topic like toggles on and off');

// ---- post page: reply, like, edit ----
console.log('post page');
await clickText('a', '0 replies');
await waitFor(`location.hash.includes('/post/')`, 'open the post');
await waitFor(`document.querySelector('h2')?.textContent.includes('repl')`, 'post page rendered');
await fill('textarea[name=text]', 'Great explanation, thanks!');
await submit('#app form');
await waitFor(`document.body.innerText.includes('Great explanation')`, 'reply appears');
const replyLike = `document.querySelector('.replies .like')`;
await js(`${replyLike}.click()`);
await waitFor(`${replyLike}.getAttribute('aria-pressed') === 'true'`, 'reply liked');
await shot('05-post');

await clickText('button', 'Edit');
await waitFor(`!!document.querySelector('textarea[name=text]')`, 'edit form opens');
await fill('textarea[name=text]', 'Moving avoids a deep copy; the source stays valid but unspecified.');
await shot('06-edit');
await clickText('button', 'Save');
await waitFor(`document.body.innerText.includes('(edited)')`, 'edited marker');
check((await js(`document.querySelectorAll('.replies .card').length`)) === 1, 'the reply survived the edit');

// ---- search ----
console.log('search');
await fill('#nav input[type=search]', 'move');
await submit('#nav form');
await waitFor(`document.querySelector('h1')?.textContent.startsWith('Results for')`, 'search results page');
check((await js(`[...document.querySelectorAll('a.hit .tag')].map((t) => t.textContent)`)).join() === 'Topic', "search 'move' finds exactly the topic");
await shot('07-search');
await fill('#nav input[type=search]', 'DEEP COPY');
await submit('#nav form');
await waitFor(`document.querySelector('h1')?.textContent.includes('DEEP COPY')`, 'second search');
check((await js(`[...document.querySelectorAll('a.hit .tag')].map((t) => t.textContent)`)).join() === 'Post', "search 'deep copy' finds the post, ignoring case");

// ---- mobile + dark ----
console.log('responsive / dark');
await viewport(390, 844, true);
await js(`location.hash = '#/'`);
await waitFor(`document.querySelector('h1')?.textContent === 'Topics'`, 'topics on mobile');
await sleep(150);
check(await js(`document.documentElement.scrollWidth <= window.innerWidth`), 'no horizontal scroll on a phone (topics)');
await shot('08-mobile-topics');
await clickText('a', 'Move semantics');
await waitFor(`document.querySelector('h1')?.textContent === 'Move semantics'`, 'topic on mobile');
await sleep(150);
check(await js(`document.documentElement.scrollWidth <= window.innerWidth`), 'no horizontal scroll on a phone (topic)');
await scheme('dark');
await sleep(200);
await shot('09-mobile-dark-topic');
await viewport(1000, 820);
await js(`location.hash = '#/'`);
await waitFor(`document.querySelector('h1')?.textContent === 'Topics'`, 'topics again');
await shot('10-dark-topics');
await scheme('light');

// ---- another user sees the content, but cannot edit it ----
console.log('second user');
await clickText('#nav button', 'Sign out');
await waitFor(`location.hash === '#/login'`, 'signed out');
await clickText('.seg button', 'Create account');
await fill('input[name=username]', 'bob');
await fill('input[name=password]', 'hunter22');
await submit('#app form');
await waitFor(`document.querySelector('h1')?.textContent === 'Topics'`, 'bob signed in');
await clickText('a', 'Move semantics');
await waitFor(`document.querySelector('h1')?.textContent === 'Move semantics'`, 'bob sees the topic');
await clickText('a', '1 reply');
await waitFor(`location.hash.includes('/post/')`, 'bob opens the post');
await waitFor(`document.querySelector('h2')?.textContent.includes('repl')`, 'post page rendered for bob');
check(!(await js(`[...document.querySelectorAll('button')].some((b) => ['Edit', 'Delete'].includes(b.textContent.trim()))`)), "bob gets no Edit/Delete buttons on alice's post");
await shot('11-bob-view');

// the session-expired path: kill the cookie, then act
await js(`fetch('/api/logout', { method: 'POST', headers: { 'X-Forum': '1' } }).then(() => 0)`);
await js(`document.querySelector('.like').click()`);
await waitFor(`location.hash === '#/login'`, 'expired session sends the user back to sign in');
check(true, 'expired session returns to the sign-in page');

await sleep(200);
drainEvents();
console.log(problems.length ? `\nPROBLEMS (${problems.length}):\n - ` + problems.join('\n - ') : '\nno console errors, CSP violations or failed checks');
ws.close();
process.exit(problems.length ? 1 : 0);
