'use strict';

/* The forum web app: a single page with hash routing, no build step, no dependencies.
   All user-written text goes into the page with textContent / text nodes, never innerHTML,
   so nothing a user types can run as markup or script. */

const $app = document.getElementById('app');
const $nav = document.getElementById('nav');
const $toast = document.getElementById('toast');

let me = null;
let limits = { username: 32, passwordMin: 4, passwordMax: 128, title: 100, description: 500, text: 2000 };
const SESSION_EXPIRED = 'Sign in required.';

/* ---------- small helpers ---------- */

/* h('div', {class: 'x', onclick: fn}, child, 'text', [more children]) */
function h(tag, attrs, ...kids) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (value === false || value == null) continue;
    if (key === 'class') node.className = value;
    else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value === true ? '' : String(value));
  }
  for (const kid of kids.flat()) {
    if (kid == null || kid === false) continue;
    node.append(kid instanceof Node ? kid : String(kid));
  }
  return node;
}

const plural = (n, word, many = word + 's') => `${n} ${n === 1 ? word : many}`;

function when(epochSeconds) {
  return h('time', { datetime: new Date(epochSeconds * 1000).toISOString() },
    new Date(epochSeconds * 1000).toLocaleString([], { dateStyle: 'medium', timeStyle: 'short' }));
}

let toastTimer = 0;
function toast(message, bad) {
  $toast.textContent = message;
  $toast.className = 'show' + (bad ? ' bad' : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { $toast.className = ''; }, 3200);
}

class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

async function api(method, path, params) {
  const init = { method, headers: { 'X-Forum': '1' }, credentials: 'same-origin' };
  const body = new URLSearchParams(params || {});
  let url = path;
  if (method === 'GET') {
    const query = body.toString();
    if (query) url += '?' + query;
  } else {
    init.headers['Content-Type'] = 'application/x-www-form-urlencoded';
    init.body = body;
  }

  let res;
  try {
    res = await fetch(url, init);
  } catch {
    throw new ApiError(0, 'Cannot reach the server.');
  }
  let data = null;
  try { data = await res.json(); } catch { /* an error page, not JSON */ }

  if (!res.ok) {
    const message = (data && data.error) || `Request failed (${res.status})`;
    if (res.status === 401 && message === SESSION_EXPIRED && me) {
      me = null;
      renderNav();
      go('#/login');
    }
    throw new ApiError(res.status, message);
  }
  return data;
}

function getSort() {
  try {
    const saved = localStorage.getItem('forum.sort');
    if (saved === 'time' || saved === 'alpha' || saved === 'none') return saved;
  } catch { /* storage unavailable */ }
  return 'time';
}

function setSort(sort) {
  try { localStorage.setItem('forum.sort', sort); } catch { /* storage unavailable */ }
}

/* ---------- reusable pieces ---------- */

let uid = 0;

/* fields: [{name, label, type, maxlength, minlength, autocomplete, rows, value}] */
function makeForm({ fields, submit, onSubmit, onCancel }) {
  const error = h('p', { class: 'error', role: 'alert', hidden: true });
  const inputs = {};

  const rows = fields.map((f) => {
    const id = `field-${++uid}`;
    const common = { id, name: f.name, maxlength: f.maxlength, required: f.required !== false };
    const input = f.type === 'textarea'
      ? h('textarea', { ...common, rows: f.rows || 3 })
      : h('input', { ...common, type: f.type || 'text', minlength: f.minlength, autocomplete: f.autocomplete });
    if (f.value) input.value = f.value;
    inputs[f.name] = input;
    return h('div', { class: 'field' }, f.label && h('label', { for: id }, f.label), input);
  });

  const button = h('button', { class: 'btn primary', type: 'submit' }, submit);
  const form = h('form', { class: 'form' }, rows, error,
    h('div', { class: 'actions' }, button,
      onCancel && h('button', { class: 'btn ghost', type: 'button', onclick: onCancel }, 'Cancel')));

  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    error.hidden = true;
    button.disabled = true;
    try {
      const values = {};
      for (const [name, input] of Object.entries(inputs)) values[name] = input.value;
      await onSubmit(values);
    } catch (e) {
      error.textContent = e.message;
      error.hidden = false;
    } finally {
      button.disabled = false;
    }
  });
  return form;
}

/* A like toggle that updates itself from the server's answer */
function likeButton(count, liked, url) {
  const button = h('button', { class: 'like', type: 'button' });
  const paint = (n, on) => {
    button.setAttribute('aria-pressed', String(on));
    button.setAttribute('aria-label', `${plural(n, 'like')}${on ? ', liked by you' : ''}`);
    button.title = on ? 'Remove your like' : 'Like';
    button.textContent = `${on ? '♥' : '♡'} ${n}`;
  };
  paint(count, liked);
  button.addEventListener('click', async () => {
    button.disabled = true;
    try {
      const result = await api('POST', url);
      paint(result.likes, result.liked);
    } catch (e) {
      toast(e.message, true);
    } finally {
      button.disabled = false;
    }
  });
  return button;
}

function crumbs(...parts) {
  const out = [];
  parts.forEach((part, i) => {
    if (i > 0) out.push(' › ');
    out.push(part);
  });
  return h('nav', { class: 'crumbs', 'aria-label': 'Breadcrumb' }, out);
}

/* ---------- views ---------- */

function viewAuth() {
  document.title = 'Sign in · C++ Forum';
  let mode = 'login';
  const holder = h('div');
  const tabs = h('div', { class: 'seg', role: 'group', 'aria-label': 'Sign in or create an account' });

  const paint = () => {
    tabs.replaceChildren(
      h('button', { type: 'button', 'aria-pressed': String(mode === 'login'), onclick: () => { mode = 'login'; paint(); } }, 'Sign in'),
      h('button', { type: 'button', 'aria-pressed': String(mode === 'register'), onclick: () => { mode = 'register'; paint(); } }, 'Create account'));

    const registering = mode === 'register';
    holder.replaceChildren(makeForm({
      fields: [
        { name: 'username', label: 'Username', maxlength: limits.username, autocomplete: 'username' },
        { name: 'password', label: registering ? `Password (at least ${limits.passwordMin} characters)` : 'Password',
          type: 'password', maxlength: limits.passwordMax, minlength: registering ? limits.passwordMin : null,
          autocomplete: registering ? 'new-password' : 'current-password' },
      ],
      submit: registering ? 'Create account' : 'Sign in',
      onSubmit: async (values) => {
        const result = await api('POST', registering ? '/api/register' : '/api/login', values);
        me = result.user;
        renderNav();
        go('#/');
      },
    }));
  };
  paint();

  return h('section', { class: 'auth' },
    document.getElementById('logo').content.cloneNode(true),
    h('div', { class: 'card' }, tabs, holder));
}

async function viewTopics() {
  document.title = 'Topics · C++ Forum';
  const sort = getSort();
  const { topics } = await api('GET', '/api/topics', { sort });

  const choices = [['time', 'Newest'], ['alpha', 'A–Z'], ['none', 'Oldest']];
  const seg = h('div', { class: 'seg', role: 'group', 'aria-label': 'Sort topics' },
    choices.map(([value, label]) => h('button', {
      type: 'button',
      'aria-pressed': String(sort === value),
      onclick: () => { setSort(value); render(); },
    }, label)));

  const newTopic = h('details', { class: 'card' },
    h('summary', {}, 'New topic'),
    makeForm({
      fields: [
        { name: 'title', label: 'Title', maxlength: limits.title },
        { name: 'description', label: 'Description (optional)', type: 'textarea', maxlength: limits.description, required: false },
      ],
      submit: 'Create topic',
      onSubmit: async (values) => {
        const { id } = await api('POST', '/api/topics', values);
        go(`#/topic/${id}`);
      },
    }));

  const list = topics.length
    ? topics.map((t) => h('article', { class: 'card' },
      h('h3', {}, h('a', { href: `#/topic/${t.id}` }, t.title)),
      t.description && h('p', { class: 'body muted' }, t.description),
      h('div', { class: 'meta' }, h('span', {}, `by ${t.author}`), when(t.created), h('span', {}, plural(t.postCount, 'post'))),
      h('div', { class: 'card-foot' }, likeButton(t.likes, t.likedByMe, `/api/topics/${t.id}/like`))))
    : h('p', { class: 'empty' }, 'No topics yet. Start the first one!');

  return h('div', {},
    h('div', { class: 'toolbar' }, h('h1', {}, 'Topics'), seg),
    newTopic,
    h('div', {}, list));
}

async function viewTopic(topicId) {
  const { topic, posts } = await api('GET', `/api/topics/${topicId}`);
  document.title = `${topic.title} · C++ Forum`;

  const postForm = makeForm({
    fields: [{ name: 'text', label: 'Write a post', type: 'textarea', maxlength: limits.text, rows: 4 }],
    submit: 'Post',
    onSubmit: async (values) => {
      await api('POST', `/api/topics/${topic.id}/posts`, values);
      toast('Posted');
      render();
    },
  });

  const list = posts.length
    ? posts.map((p) => h('article', { class: 'card' },
      h('p', { class: 'body' }, p.text),
      h('div', { class: 'meta' },
        h('span', {}, `by ${p.author}`), when(p.created), p.edited && h('span', {}, '(edited)')),
      h('div', { class: 'card-foot' },
        likeButton(p.likes, p.likedByMe, `/api/topics/${topic.id}/posts/${p.id}/like`),
        h('a', { href: `#/topic/${topic.id}/post/${p.id}` }, plural(p.replyCount, 'reply', 'replies') + ' · open'))))
    : h('p', { class: 'empty' }, 'No posts yet. Be the first to write one.');

  return h('div', {},
    crumbs(h('a', { href: '#/' }, 'Topics'), topic.title),
    h('article', { class: 'card' },
      h('h1', {}, topic.title),
      topic.description && h('p', { class: 'body' }, topic.description),
      h('div', { class: 'meta' }, h('span', {}, `by ${topic.author}`), when(topic.created)),
      h('div', { class: 'card-foot' }, likeButton(topic.likes, topic.likedByMe, `/api/topics/${topic.id}/like`))),
    h('h2', {}, plural(posts.length, 'post')),
    h('div', {}, list),
    h('div', { class: 'card' }, postForm));
}

async function viewPost(topicId, postId) {
  const { topic, post, replies } = await api('GET', `/api/topics/${topicId}/posts/${postId}`);
  document.title = `Post in ${topic.title} · C++ Forum`;
  const base = `/api/topics/${topic.id}/posts/${post.id}`;

  const body = h('p', { class: 'body' }, post.text);
  const actions = h('div', { class: 'card-foot' }, likeButton(post.likes, post.likedByMe, `${base}/like`));

  if (post.mine) {
    const edit = () => {
      const form = makeForm({
        fields: [{ name: 'text', label: 'Edit post', type: 'textarea', maxlength: limits.text, rows: 4, value: post.text }],
        submit: 'Save',
        onCancel: () => render(),
        onSubmit: async (values) => {
          await api('POST', `${base}/edit`, values);
          toast('Saved');
          render();
        },
      });
      body.replaceWith(form);
      actions.hidden = true;
      form.querySelector('textarea').focus();
    };
    const remove = async () => {
      if (!window.confirm('Delete this post and all of its replies?')) return;
      try {
        await api('POST', `${base}/delete`);
        toast('Post deleted');
        go(`#/topic/${topic.id}`);
      } catch (e) {
        toast(e.message, true);
      }
    };
    actions.append(
      h('button', { class: 'btn', type: 'button', onclick: edit }, 'Edit'),
      h('button', { class: 'btn danger', type: 'button', onclick: remove }, 'Delete'));
  }

  const replyForm = makeForm({
    fields: [{ name: 'text', label: 'Write a reply', type: 'textarea', maxlength: limits.text, rows: 3 }],
    submit: 'Reply',
    onSubmit: async (values) => {
      await api('POST', `${base}/replies`, values);
      toast('Reply added');
      render();
    },
  });

  const replyCards = replies.map((r) => h('article', { class: 'card' },
    h('p', { class: 'body' }, r.text),
    h('div', { class: 'meta' }, h('span', {}, `by ${r.author}`), when(r.created)),
    h('div', { class: 'card-foot' }, likeButton(r.likes, r.likedByMe, `${base}/replies/${r.id}/like`))));

  return h('div', {},
    crumbs(h('a', { href: '#/' }, 'Topics'), h('a', { href: `#/topic/${topic.id}` }, topic.title), 'Post'),
    h('article', { class: 'card' },
      body,
      h('div', { class: 'meta' }, h('span', {}, `by ${post.author}`), when(post.created), post.edited && h('span', {}, '(edited)')),
      actions),
    h('h2', {}, plural(replies.length, 'reply', 'replies')),
    replyCards.length ? h('div', { class: 'replies' }, replyCards) : h('p', { class: 'empty' }, 'No replies yet.'),
    h('div', { class: 'card' }, replyForm));
}

async function viewSearch(rawQuery) {
  let query = rawQuery;
  try { query = decodeURIComponent(rawQuery); } catch { /* keep it as typed */ }
  document.title = `Search: ${query} · C++ Forum`;
  const { hits } = await api('GET', '/api/search', { q: query });

  const cards = hits.map((hit) => {
    const isPost = hit.postId !== 0;
    const href = isPost ? `#/topic/${hit.topicId}/post/${hit.postId}` : `#/topic/${hit.topicId}`;
    return h('a', { class: 'card hit', href },
      h('h3', {}, h('span', { class: 'tag' }, isPost ? 'Post' : 'Topic'), hit.topicTitle),
      hit.text && h('p', { class: 'body muted' }, hit.text));
  });

  return h('div', {},
    crumbs(h('a', { href: '#/' }, 'Topics'), 'Search'),
    h('h1', {}, `Results for “${query}”`),
    h('p', { class: 'muted' }, `${hits.length} ${hits.length === 1 ? 'match' : 'matches'}`),
    cards.length ? cards : h('p', { class: 'empty' }, 'Nothing matched. Try a different word.'));
}

function viewPassword() {
  document.title = 'Change password · C++ Forum';
  return h('section', { class: 'auth' },
    crumbs(h('a', { href: '#/' }, 'Topics'), 'Change password'),
    h('div', { class: 'card' },
      makeForm({
        fields: [
          { name: 'old', label: 'Current password', type: 'password', maxlength: limits.passwordMax, autocomplete: 'current-password' },
          { name: 'new', label: `New password (at least ${limits.passwordMin} characters)`, type: 'password',
            minlength: limits.passwordMin, maxlength: limits.passwordMax, autocomplete: 'new-password' },
        ],
        submit: 'Change password',
        onSubmit: async (values) => {
          await api('POST', '/api/password', values);
          toast('Password changed');
          go('#/');
        },
      })));
}

function viewNotFound() {
  document.title = 'Not found · C++ Forum';
  return h('p', { class: 'empty' }, 'That page does not exist. ', h('a', { href: '#/' }, 'Back to the topics'));
}

/* ---------- navigation ---------- */

const routes = [
  [/^#\/?$/, viewTopics],
  [/^#\/login$/, viewAuth],
  [/^#\/topic\/(\d+)$/, viewTopic],
  [/^#\/topic\/(\d+)\/post\/(\d+)$/, viewPost],
  [/^#\/search\/(.*)$/, viewSearch],
  [/^#\/password$/, viewPassword],
];

function renderNav() {
  if (!me) {
    $nav.replaceChildren();
    return;
  }
  const search = h('form', { role: 'search' },
    h('input', { type: 'search', name: 'q', placeholder: 'Search topics and posts', 'aria-label': 'Search topics and posts', maxlength: 100, required: true }));
  search.addEventListener('submit', (event) => {
    event.preventDefault();
    const q = search.q.value.trim();
    if (q) go(`#/search/${encodeURIComponent(q)}`);
  });

  $nav.replaceChildren(
    search,
    h('span', { class: 'who' }, h('span', { class: 'sr' }, 'Signed in as '), h('strong', {}, me)),
    h('a', { href: '#/password' }, 'Password'),
    h('button', {
      class: 'btn ghost',
      type: 'button',
      onclick: async () => {
        try { await api('POST', '/api/logout'); } catch { /* signing out locally anyway */ }
        me = null;
        renderNav();
        go('#/login');
      },
    }, 'Sign out'));
}

function go(hash) {
  if (location.hash === hash) render();
  else location.hash = hash;
}

let renderSeq = 0;

async function render() {
  const seq = ++renderSeq;
  const hash = location.hash || '#/';

  let view = viewNotFound;
  let args = [];
  for (const [pattern, candidate] of routes) {
    const match = pattern.exec(hash);
    if (match) {
      view = candidate;
      args = match.slice(1);
      break;
    }
  }

  if (!me && view !== viewAuth) { go('#/login'); return; }
  if (me && view === viewAuth) { go('#/'); return; }

  try {
    const content = await view(...args);
    if (seq !== renderSeq) return;  /* the user navigated on while this was loading */
    $app.replaceChildren(content);
  } catch (e) {
    if (seq !== renderSeq) return;
    $app.replaceChildren(
      h('div', { class: 'card' },
        h('p', { class: 'error', role: 'alert' }, e.status === 404 ? 'That page was not found.' : e.message),
        h('a', { href: '#/' }, 'Back to the topics')));
  }
  if (seq === renderSeq) $app.focus({ preventScroll: true });
}

window.addEventListener('hashchange', render);

(async function start() {
  try {
    const status = await api('GET', '/api/me');
    me = status.user;
    if (status.limits) limits = status.limits;
  } catch (e) {
    $app.replaceChildren(h('p', { class: 'error', role: 'alert' }, e.message));
    return;
  }
  renderNav();
  render();
})();
