/* app.js — logique de l'application Les Théorèmes (aucune dépendance) */
"use strict";

/* ---------------- état ---------------- */
const state = {
  token: localStorage.getItem("lt_token") || "",
  me: null,
  config: {},
  tab: "home",
  route: { name: "home" },
  children: [],
  childIndex: 0,
  unread: 0
};

const $ = (sel, root) => (root || document).querySelector(sel);
const $$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));

/* ---------------- appels réseau ---------------- */
async function api(path, opts) {
  opts = opts || {};
  const headers = { "Content-Type": "application/json" };
  if (state.token) headers["Authorization"] = "Bearer " + state.token;
  let res, data;
  try {
    res = await fetch("/api" + path, {
      method: opts.method || (opts.body ? "POST" : "GET"),
      headers,
      body: opts.body ? JSON.stringify(opts.body) : undefined
    });
  } catch (e) {
    throw { code: "network" };
  }
  try { data = await res.json(); } catch (e) { data = { ok: false, error: "internal" }; }
  if (res.status === 401 && state.token) {
    logoutLocal();
    throw { code: "session_expired" };
  }
  if (!data.ok) throw { code: data.error || "internal", message: data.message };
  return data;
}

/* ---------------- utilitaires ---------------- */
function esc(s) {
  return String(s === null || s === undefined ? "" : s)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
}

function pick(obj, base) {
  if (!obj) return "";
  const v = obj[base + "_" + LANG];
  if (v && String(v).trim()) return String(v);
  const fr = obj[base + "_fr"];
  return fr ? String(fr) : "";
}

function money(n) {
  const v = Number(n || 0);
  return v.toLocaleString(LANG === "ar" ? "ar-DZ" : LANG === "en" ? "en-GB" : "fr-FR") + " DA";
}

function shortDate(s) {
  if (!s) return "";
  return String(s).slice(0, 10).split("-").reverse().join("/");
}

function toast(msg) {
  const el = $("#toast");
  el.textContent = msg;
  el.hidden = false;
  clearTimeout(toast._t);
  toast._t = setTimeout(() => { el.hidden = true; }, 2600);
}

function fail(err) {
  toast(err && err.message ? err.message : errText(err && err.code));
}

function dayName(i) { return t("day_full_" + (i + 1)); }
function dayShort(i) {
  return [t("monday_short"), t("tuesday_short"), t("wednesday_short"), t("thursday_short"), t("friday_short")][i] || "";
}

function isStaff() { return state.me && (state.me.role === "admin" || state.me.role === "moderator"); }
function isAdmin() { return state.me && state.me.role === "admin"; }

function activeChild() {
  return state.children[state.childIndex] || null;
}

function roleLabel(role) {
  if (role === "admin") return t("role_admin");
  if (role === "moderator") return t("role_moderator");
  return t("role_parent");
}

/* ---------------- icônes (SVG) ---------------- */
const ICONS = {
  money: '<rect x="2.5" y="6" width="19" height="12" rx="2"/><circle cx="12" cy="12" r="2.6"/><path d="M6 9.5v5M18 9.5v5"/>',
  book: '<path d="M4 5.5A2.5 2.5 0 0 1 6.5 3H19v15H6.5A2.5 2.5 0 0 0 4 20.5z"/><path d="M8 7.5h7M8 11h5"/>',
  check: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7.5 10.5h6M7.5 14.5h4"/><path d="M15.5 14.5l1.8 1.8 3-3.4"/>',
  chart: '<rect x="3.5" y="4" width="17" height="16" rx="2"/><path d="M8 15.5v-3.2M12 15.5V9M16 15.5v-2"/>',
  food: '<path d="M7 3v8a2 2 0 0 0 2 2v8"/><path d="M4 3v5a3 3 0 0 0 3 3"/><path d="M10 3v5"/><path d="M16 3c-1.7 1.2-2.5 3-2.5 5.5S15 13 16 13v8"/>',
  bus: '<rect x="3" y="4.5" width="18" height="12" rx="2"/><path d="M3 9.5h18M7 20v-2.5M17 20v-2.5"/><circle cx="7.5" cy="13.5" r="1"/><circle cx="16.5" cy="13.5" r="1"/>',
  help: '<circle cx="12" cy="12" r="9"/><path d="M9.5 9.5a2.6 2.6 0 0 1 5 1c0 1.7-2.5 2-2.5 3.6"/><circle cx="12" cy="17.2" r=".9" fill="currentColor"/>',
  photo: '<rect x="3" y="4.5" width="18" height="15" rx="2"/><circle cx="8.5" cy="9.5" r="1.5"/><path d="M4 17l5-4.5 4 3.5 3-2.5 4 3.5"/>',
  logout: '<path d="M14 4H6.5A1.5 1.5 0 0 0 5 5.5v13A1.5 1.5 0 0 0 6.5 20H14"/><path d="M17 8.5 20.5 12 17 15.5M10 12h10"/>',
  users: '<circle cx="9" cy="8" r="3"/><path d="M3 20c0-3.3 2.7-6 6-6s6 2.7 6 6"/><circle cx="17.5" cy="9" r="2.3"/><path d="M15.5 20c0-2.5 1.2-4.4 3.5-4.4S22 17.5 22 20"/>',
  bell: '<path d="M6.5 10a5.5 5.5 0 0 1 11 0c0 4 1.5 5.5 1.5 5.5H5S6.5 14 6.5 10"/><path d="M10 19a2.2 2.2 0 0 0 4 0"/>',
  clock: '<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>',
  school: '<path d="M12 3 3 7.5l9 4.5 9-4.5z"/><path d="M6.5 10v5.5c0 1.7 2.5 3 5.5 3s5.5-1.3 5.5-3V10"/>',
  shield: '<path d="M12 3.5 5 6v6c0 4 3 7 7 8.5 4-1.5 7-4.5 7-8.5V6z"/><path d="M9.2 12.2l2 2 3.6-3.8"/>',
  note: '<path d="M6 3.5h9l4 4V20a.5.5 0 0 1-.5.5h-12A.5.5 0 0 1 6 20z"/><path d="M15 3.5v4h4M9 12h6M9 15.5h6"/>'
};

function ico(name, cls) {
  return '<svg viewBox="0 0 24 24" class="' + (cls || "tile-ico") + '" aria-hidden="true">' + (ICONS[name] || ICONS.book) + "</svg>";
}

/* ---------------- connexion ---------------- */
function logoutLocal() {
  state.token = "";
  state.me = null;
  localStorage.removeItem("lt_token");
  $("#screen-app").hidden = true;
  $("#screen-login").hidden = false;
  $("#password").value = "";
}

function showLoginError(msg) {
  $("#login-error").textContent = msg || "";
}

async function afterLogin(data) {
  state.token = data.token;
  localStorage.setItem("lt_token", data.token);
  await boot();
}

async function boot() {
  if (!state.token) { logoutLocal(); return; }
  let me;
  try {
    me = await api("/me");
  } catch (e) {
    logoutLocal();
    showLoginError(errText("session_expired"));
    return;
  }
  state.me = me.user;
  state.children = me.children || [];
  $("#screen-login").hidden = true;
  $("#screen-app").hidden = false;
  $("#user-name").textContent = (state.me.first_name + " " + state.me.last_name).trim();
  $("#user-role").textContent = roleLabel(state.me.role);
  applyLang(LANG);
  refreshUnread();
  go(me.user.must_change && me.user.role !== "parent"
     ? { name: "account", mustChange: true }
     : { name: "home" });
  setTab("home");
}

async function refreshUnread() {
  try {
    const d = await api("/messages");
    state.unread = d.unread || 0;
    const b = $("#msg-badge");
    b.textContent = state.unread;
    b.hidden = !state.unread;
  } catch (e) { /* silencieux */ }
}

/* ---------------- navigation ---------------- */
function setTab(tab) {
  state.tab = tab;
  $$(".tab").forEach(el => el.classList.toggle("active", el.dataset.tab === tab));
}

const TABS = ["home", "messages", "children", "account"];

function go(route) {
  state.route = route;
  if (TABS.indexOf(route.name) >= 0) setTab(route.name);
  render();
  $("#view").scrollTop = 0;
  window.scrollTo(0, 0);
}

function back() {
  go({ name: "home" });
}

function pageTitle(title, subtitle) {
  return '<div class="page-title">' +
    '<button class="back" onclick="back()" aria-label="Retour">' +
    '<svg viewBox="0 0 24 24" class="ico-sm" style="width:18px;height:18px"><path d="M15 5l-7 7 7 7"/></svg>' +
    "</button><h1>" + esc(title) + "</h1>" +
    (subtitle ? '<span class="chip">' + esc(subtitle) + "</span>" : "") +
    "</div>";
}

/* ---------------- rendu principal ---------------- */
function render() {
  const v = $("#view");
  const r = state.route;
  const map = {
    home: renderHome,
    messages: renderMessages,
    children: renderChildren,
    account: renderAccount,
    announcements: renderAnnouncements,
    homework: renderHomework,
    schedule: renderSchedule,
    absences: renderAbsences,
    grades: renderGrades,
    fees: renderFees,
    menu: renderMenu,
    bus: renderBus,
    photos: renderPhotos,
    contact: renderContact,
    users: renderUsers,
    students: renderStudentsAdmin,
    stats: renderStats,
    audit: renderAudit,
    carnet: renderCarnet
  };
  const fn = map[r.name] || renderHome;
  if (!render._warm[r.name]) {
    /* premiere visite : on montre tout de suite l'en-tete (l'ecran ne
       reste plus vide pendant le chargement des donnees) */
    v.innerHTML = '<div class="page-title"><h1>' + esc(routeTitle(r)) + "</h1></div>" +
                  '<div class="empty">' + t("loading") + "</div>";
  }
  const out = fn(r);
  if (out && typeof out.then === "function") {
    out.then(html => {
      render._warm[r.name] = 1;
      if (state.route !== r || typeof html !== "string") return;
      v.innerHTML = html;
      afterRender(r);
    }).catch(() => {
      v.innerHTML = '<div class="empty">' + t("empty") + "</div>";
    });
  } else {
    v.innerHTML = out;
    afterRender(r);
  }
}

render._warm = {};

/* titre affiche pendant le chargement, avant que les donnees arrivent */
function routeTitle(r) {
  const keys = {
    home: "nav_home", messages: "nav_messages", children: "nav_children",
    account: "nav_account", announcements: "t_announcements", homework: "t_homework",
    schedule: "t_schedule", absences: "t_absences", grades: "t_grades",
    fees: "t_fees", menu: "t_menu", bus: "t_bus", photos: "t_photos",
    contact: "t_contact", users: "t_users", students: "t_students",
    stats: "t_stats", audit: "t_audit", carnet: "t_carnet"
  };
  return keys[r.name] ? t(keys[r.name]) : "";
}

function afterRender(r) {
  if (r.name === "users") updateChildBlock();
  if (r.name === "messages") markRead();
  if (r.name === "carnet") loadCarnet();
}

/* ---------------- accueil ---------------- */
function renderHome() {
  const staff = isStaff();
  const tiles = [];

  if (!staff) {
    tiles.push({ icon: "money", label: t("t_fees"), route: "fees" });
    tiles.push({ icon: "book", label: t("t_carnet"), route: "carnet" });
    tiles.push({ icon: "clock", label: t("t_schedule"), route: "schedule" });
    tiles.push({ icon: "check", label: t("t_homework"), route: "homework" });
    tiles.push({ icon: "note", label: t("t_absences"), route: "absences" });
    tiles.push({ icon: "chart", label: t("t_grades"), route: "grades" });
    tiles.push({ icon: "food", label: t("t_menu"), route: "menu" });
    tiles.push({ icon: "bus", label: t("t_bus"), route: "bus" });
    tiles.push({ icon: "bell", label: t("t_announcements"), route: "announcements" });
    tiles.push({ icon: "photo", label: t("t_photos"), route: "photos" });
    tiles.push({ icon: "help", label: t("t_contact"), route: "contact" });
  } else {
    tiles.push({ icon: "bell", label: t("t_announcements"), route: "announcements" });
    tiles.push({ icon: "users", label: t("t_users"), route: "users" });
    tiles.push({ icon: "school", label: t("t_students"), route: "students" });
    tiles.push({ icon: "chart", label: t("t_stats"), route: "stats" });
    tiles.push({ icon: "check", label: t("t_homework"), route: "homework" });
    tiles.push({ icon: "clock", label: t("t_schedule"), route: "schedule" });
    tiles.push({ icon: "note", label: t("t_absences"), route: "absences" });
    tiles.push({ icon: "money", label: t("t_fees"), route: "fees" });
    tiles.push({ icon: "photo", label: t("t_photos"), route: "photos" });
    tiles.push({ icon: "help", label: t("t_contact"), route: "contact" });
    if (isAdmin()) tiles.push({ icon: "shield", label: t("t_audit"), route: "audit" });
    tiles.push({ icon: "logout", label: t("t_logout"), route: "logout" });
  }

  const child = activeChild();
  let header = "";
  if (!staff && child) {
    header = '<div class="panel" style="display:flex;gap:12px;align-items:center">' +
      '<div style="width:44px;height:44px;border-radius:50%;background:var(--orange-soft);display:grid;place-items:center;color:var(--orange);font-weight:700">' +
      esc((child.first_name || "?").slice(0, 1)) + "</div>" +
      '<div style="flex:1;min-width:0">' +
      "<strong>" + esc(child.first_name + " " + child.last_name) + "</strong>" +
      '<div class="muted">' + esc(child.level) + (child.class_name ? " · " + esc(child.class_name) : "") + "</div>" +
      "</div>" +
      (state.children.length > 1
        ? '<button class="btn secondary small" onclick="switchChild()">' + esc(t("pick_child")) + "</button>"
        : "") +
      "</div>";
  }

  return header +
    '<div class="grid">' +
    tiles.map(x => {
      const onclick = x.route === "logout" ? "doLogout()" : "go({name:'" + x.route + "'})";
      return '<button class="tile" onclick="' + onclick + '">' + ico(x.icon) +
        "<span>" + esc(x.label) + "</span></button>";
    }).join("") +
    "</div>";
}

function switchChild() {
  state.childIndex = (state.childIndex + 1) % state.children.length;
  toast(state.children[state.childIndex].first_name);
  render();
}

/* ---------------- messages ---------------- */
let messagesCache = null;

async function renderMessages() {
  if (!messagesCache) return '<div class="empty">' + t("loading") + "</div>";
  const d = messagesCache;
  const staff = isStaff();
  const myId = String(state.me.id);
  const list = d.messages || [];
  messagesCache = null;

  const items = list.map(m => {
    const mine = String(m.from_id) === myId;
    const who = mine ? m.to_name : m.from_name;
    return '<div class="item' + (mine ? " neutral" : "") + '">' +
      '<div class="row"><strong>' + esc(m.subject || t("message")) + "</strong>" +
      '<span class="chip ' + (mine ? "grey" : "green") + '">' + esc(mine ? "→ " + who : "← " + who) + "</span></div>" +
      "<p>" + esc(m.body) + "</p>" +
      '<div class="meta"><span>' + esc(m.created_at) + "</span></div></div>";
  }).join("");

  const targets = staff
    ? (d.parents || []).map(p => '<option value="' + p.id + '">' + esc(p.name + " (" + p.email + ")") + "</option>").join("")
    : (d.staff || []).map(p => '<option value="' + p.id + '">' + esc(p.name + " — " + roleLabel(p.role)) + "</option>").join("");

  return pageTitle(t("nav_messages")) +
    '<div class="panel"><h2>' + t("send") + '</h2><div class="form-grid">' +
    "<select id=" + '"msg-to"' + ">" + targets + "</select>" +
    '<input id="msg-subject" placeholder="' + esc(t("title")) + '">' +
    '<textarea id="msg-body" placeholder="' + esc(t("message")) + '"></textarea>' +
    '<button class="btn primary" onclick="sendMessage()">' + t("send") + "</button>" +
    "</div></div>" +
    '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

async function loadMessages() { return api("/messages"); }

function markRead() { api("/messages/read", { body: {} }).then(refreshUnread).catch(() => {}); }

async function sendMessage() {
  const to = $("#msg-to").value;
  const subject = $("#msg-subject").value.trim();
  const body = $("#msg-body").value.trim();
  if (!body) { toast(t("required")); return; }
  try {
    await api("/messages", { body: { to_id: Number(to), subject, body } });
    toast(t("saved"));
    go({ name: "messages" });
  } catch (e) { fail(e); }
}

/* ---------------- enfants ---------------- */
function renderChildren() {
  if (!state.children.length) {
    return pageTitle(t("nav_children")) + '<div class="panel"><p class="muted">' + t("no_child") + "</p></div>";
  }
  return pageTitle(t("nav_children")) + '<div class="list">' + state.children.map((s, i) =>
    '<div class="item" onclick="state.childIndex=' + i + ';render();toast(\'' + esc(s.first_name) + '\')">' +
    "<strong>" + esc(s.first_name + " " + s.last_name) + "</strong>" +
    '<div class="meta"><span class="chip">' + esc(s.level || "—") + "</span>" +
    (s.class_name ? '<span class="chip grey">' + esc(s.class_name) + "</span>" : "") +
    (s.bus_route ? "<span>" + esc(s.bus_route) + "</span>" : "") +
    "</div></div>").join("") + "</div>";
}

/* ---------------- compte ---------------- */
function renderAccount(r) {
  const u = state.me;
  return pageTitle(t("nav_account")) +
    (r.mustChange && u.role !== "parent" ? '<div class="panel" style="border-inline-start:4px solid var(--orange)"><p class="muted" style="margin:0">' + t("must_change") + "</p></div>" : "") +
    '<div class="panel"><h2>' + t("nav_account") + "</h2>" +
    '<div class="kv"><span>' + t("name") + "</span><span>" + esc(u.first_name) + "</span></div>" +
    '<div class="kv"><span>' + t("last_name") + "</span><span>" + esc(u.last_name) + "</span></div>" +
    '<div class="kv"><span>' + t("email") + "</span><span>" + esc(u.email) + "</span></div>" +
    '<div class="kv"><span>' + t("role") + "</span><span>" + esc(roleLabel(u.role)) + "</span></div>" +
    '<div class="kv"><span>' + t("hours") + "</span><span>" + esc(u.last_login || "—") + "</span></div>" +
    "</div>" +
    (u.role === "parent"
      ? '<div class="panel"><h2>' + t("password_locked_title") + "</h2>" +
        '<p class="muted" style="margin:0">' + t("password_locked") + "</p></div>"
      : '<div class="panel"><h2>' + t("change_password") + '</h2><div class="form-grid">' +
        '<input id="old-pw" type="password" placeholder="' + esc(t("old_password")) + '">' +
        '<input id="new-pw" type="password" placeholder="' + esc(t("new_password")) + '">' +
        '<button class="btn primary" onclick="changePassword()">' + t("save") + "</button>" +
        "</div></div>") +
    '<button class="btn danger block" onclick="doLogout()">' + t("logout") + "</button>";
}

async function changePassword() {
  const oldp = $("#old-pw").value;
  const newp = $("#new-pw").value;
  if (!oldp || !newp) { toast(t("required")); return; }
  try {
    await api("/password", { body: { old: oldp, new: newp } });
    toast(t("saved"));
    state.me.must_change = 0;
    go({ name: "home" });
  } catch (e) { fail(e); }
}

function doLogout() {
  const tok = state.token;
  state.token = tok;
  api("/logout", { body: {} }).catch(() => {}).finally(() => {
    logoutLocal();
    go({ name: "home" });
    showLoginError("");
  });
}

/* ---------------- annonces ---------------- */
let annCache = null;

async function renderAnnouncements() {
  const staff = isStaff();
  let data = annCache;
  annCache = null;
  if (!data) {
    try { data = await api("/announcements"); } catch (e) { fail(e); data = { announcements: [] }; }
  }
  const list = data.announcements || [];
  const items = list.map(a =>
    '<div class="item"><div class="row"><strong>' + esc(pick(a, "title")) + "</strong>" +
    (a.pinned ? '<span class="chip">★</span>' : "") + "</div>" +
    "<p>" + esc(pick(a, "body")) + "</p>" +
    '<div class="meta"><span>' + esc(shortDate(a.created_at)) + "</span>" +
    (a.author ? "<span>" + esc(a.author) + "</span>" : "") +
    (staff ? '<a href="#" onclick="editAnnouncement(' + a.id + ');return false">' + t("edit") + "</a>" +
      '<a href="#" onclick="removeAnnouncement(' + a.id + ');return false" style="color:var(--red)">' + t("delete") + "</a>" : "") +
    "</div></div>").join("");

  const form = staff ? '<div class="panel"><h2>' + t("new_announcement") + "</h2>" +
    '<div class="form-grid">' +
    '<input id="an-fr" placeholder="Titre (FR)">' +
    '<input id="an-ar" placeholder="العنوان (AR)">' +
    '<input id="an-en" placeholder="Title (EN)">' +
    '<textarea id="an-body-fr" placeholder="Texte (FR)"></textarea>' +
    '<textarea id="an-body-ar" placeholder="النص (AR)"></textarea>' +
    '<textarea id="an-body-en" placeholder="Text (EN)"></textarea>' +
    '<label class="check"><input type="checkbox" id="an-pinned"> ' + t("confirm") + " (★)</label>" +
    '<button class="btn primary" onclick="saveAnnouncement()">' + t("save") + "</button>" +
    "</div></div>" : "";

  setTimeout(() => {
    if (staff && state._editAnn) fillAnnouncementForm(state._editAnn);
  }, 0);

  return pageTitle(t("t_announcements")) + form +
    '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

function fillAnnouncementForm(a) {
  const set = (id, v) => { const el = $("#" + id); if (el) el.value = v || ""; };
  set("an-fr", a.title_fr); set("an-ar", a.title_ar); set("an-en", a.title_en);
  set("an-body-fr", a.body_fr); set("an-body-ar", a.body_ar); set("an-body-en", a.body_en);
  const p = $("#an-pinned"); if (p) p.checked = !!a.pinned;
}

async function editAnnouncement(id) {
  try {
    const d = await api("/announcements");
    const a = (d.announcements || []).find(x => x.id === id);
    state._editAnn = a || null;
    if (!a) return;
    fillAnnouncementForm(a);
    toast(t("edit"));
  } catch (e) { fail(e); }
}

async function saveAnnouncement() {
  const body = {
    title_fr: $("#an-fr").value.trim(),
    title_ar: $("#an-ar").value.trim(),
    title_en: $("#an-en").value.trim(),
    body_fr: $("#an-body-fr").value.trim(),
    body_ar: $("#an-body-ar").value.trim(),
    body_en: $("#an-body-en").value.trim(),
    pinned: $("#an-pinned").checked
  };
  if (state._editAnn) body.id = state._editAnn.id;
  if (!body.title_fr) { toast(t("required")); return; }
  try {
    await api("/announcements/save", { body });
    state._editAnn = null;
    toast(t("saved"));
    annCache = null;
    render();
  } catch (e) { fail(e); }
}

async function removeAnnouncement(id) {
  if (!confirm(t("confirm_delete"))) return;
  try {
    await api("/announcements/delete", { body: { id } });
    toast(t("deleted"));
    render();
  } catch (e) { fail(e); }
}

/* ---------------- devoirs ---------------- */
async function renderHomework() {
  const staff = isStaff();
  const cls = staff ? "" : (activeChild() ? activeChild().class_name : "");
  let data;
  try { data = await api("/homework" + (cls ? "?class=" + encodeURIComponent(cls) : "")); }
  catch (e) { fail(e); data = { homework: [] }; }
  const items = (data.homework || []).map(h =>
    '<div class="item"><div class="row"><strong>' + esc(pick(h, "title")) + "</strong>" +
    '<span class="chip">' + esc(h.subject) + "</span></div>" +
    (pick(h, "body") ? "<p>" + esc(pick(h, "body")) + "</p>" : "") +
    '<div class="meta"><span>' + esc(h.class_name) + "</span>" +
    '<span>' + t("due") + " : " + esc(shortDate(h.due)) + "</span></div></div>").join("");

  const form = staff ? '<div class="panel"><h2>' + t("new_homework") + "</h2><div class=\"form-grid\">" +
    '<div class="two"><input id="hw-class" placeholder="' + esc(t("class")) + ' (3AP)"><input id="hw-subject" placeholder="' + esc(t("subject")) + '"></div>' +
    '<input id="hw-title" placeholder="' + esc(t("title")) + ' (FR)">' +
    '<input id="hw-title-ar" placeholder="العنوان (AR)">' +
    '<input id="hw-title-en" placeholder="Title (EN)">' +
    '<input id="hw-due" type="date">' +
    '<button class="btn primary" onclick="saveHomework()">' + t("save") + "</button></div></div>" : "";

  return pageTitle(t("t_homework"), cls || null) + form +
    '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

async function saveHomework() {
  const body = {
    class_name: $("#hw-class").value.trim(),
    subject: $("#hw-subject").value.trim(),
    title_fr: $("#hw-title").value.trim(),
    title_ar: $("#hw-title-ar").value.trim(),
    title_en: $("#hw-title-en").value.trim(),
    due: $("#hw-due").value
  };
  if (!body.class_name || !body.title_fr) { toast(t("required")); return; }
  try { await api("/homework", { body }); toast(t("saved")); render(); } catch (e) { fail(e); }
}

/* ---------------- emploi du temps ---------------- */
async function renderSchedule() {
  const cls = isStaff() ? "" : (activeChild() ? activeChild().class_name : "");
  let data;
  try { data = await api("/schedule" + (cls ? "?class=" + encodeURIComponent(cls) : "")); }
  catch (e) { fail(e); data = { schedule: [] }; }
  const rows = data.schedule || [];
  const classes = Array.from(new Set(rows.map(r => r.class_name)));
  const shown = cls ? [cls] : classes.slice(0, 1);
  const c = shown[0] || "";

  let html = pageTitle(t("t_schedule"), c || null);
  if (!rows.length) return html + '<div class="panel"><p class="muted">' + t("empty") + "</p></div>";

  const slots = Array.from(new Set(rows.map(r => r.slot))).sort((a, b) => a - b);
  const days = [0, 1, 2, 3, 4];
  let table = '<table class="tt"><thead><tr><th></th>' + days.map(d => "<th>" + dayShort(d) + "</th>").join("") + "</tr></thead><tbody>";
  slots.forEach((s, idx) => {
    table += "<tr><th>" + (idx + 1) + "</th>";
    days.forEach(d => {
      const cell = rows.find(r => r.class_name === c && Number(r.day) === d && Number(r.slot) === s);
      table += cell && cell.subject
        ? "<td>" + esc(cell.subject) + (cell.room ? '<br><small style="color:#9a938c">' + esc(cell.room) + "</small>" : "") + "</td>"
        : '<td class="empty-cell">—</td>';
    });
    table += "</tr>";
  });
  table += "</tbody></table>";

  return html + '<div class="panel"><div class="table-wrap">' + table + "</div>" +
    (rows.find(r => r.teacher) ? '<p class="muted" style="margin:10px 0 0">' + t("teacher") + " : " + esc(rows.find(r => r.teacher).teacher) + "</p>" : "") +
    "</div>";
}

/* ---------------- absences ---------------- */
async function renderAbsences() {
  let data;
  try { data = await api("/absences"); } catch (e) { fail(e); data = { absences: [] }; }
  const items = (data.absences || []).map(a =>
    '<div class="item' + (a.justified ? "" : " neutral") + '">' +
    "<strong>" + esc(a.kind === "retard" ? t("lateness") : t("absence")) +
    (isStaff() ? " — " + esc(a.student_name) : "") + "</strong>" +
    "<p>" + esc(a.reason || "—") + "</p>" +
    '<div class="meta"><span>' + esc(shortDate(a.date)) + "</span>" +
    '<span class="chip ' + (a.justified ? "green" : "red") + '">' + esc(a.justified ? t("justified") : t("unjustified")) + "</span></div></div>").join("");
  const form = isStaff() ? '<div class="panel"><h2>' + t("new_absence") + "</h2>" +
    '<div class="form-grid"><input id="ab-student" type="number" placeholder="ID ' + esc(t("student")) + '">' +
    '<input id="ab-date" type="date"><select id="ab-kind"><option value="absence">' + t("absence") + '</option><option value="retard">' + t("lateness") + "</option></select>" +
    '<input id="ab-reason" placeholder="' + esc(t("reason")) + '">' +
    '<button class="btn primary" onclick="saveAbsence()">' + t("save") + "</button></div></div>" : "";
  return pageTitle(t("t_absences")) + form +
    '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

async function saveAbsence() {
  const body = {
    student_id: Number($("#ab-student").value || 0),
    date: $("#ab-date").value,
    kind: $("#ab-kind").value,
    reason: $("#ab-reason").value.trim()
  };
  if (!body.student_id) { toast(t("required")); return; }
  try { await api("/absences", { body }); toast(t("saved")); render(); } catch (e) { fail(e); }
}

/* ---------------- notes ---------------- */
async function renderGrades() {
  let data;
  try { data = await api("/grades"); } catch (e) { fail(e); data = { results: [] }; }
  const results = data.results || [];
  if (!results.length) return pageTitle(t("t_grades")) + '<div class="panel"><p class="muted">' + t("empty") + "</p></div>";

  let html = pageTitle(t("t_grades"));
  if (isStaff()) {
    const items = results.map(g =>
      '<div class="item neutral"><div class="row"><strong>' + esc(g.subject) + "</strong>" +
      '<span class="chip ' + (Number(g.note) >= 10 ? "green" : "red") + '">' + esc(g.note) + "/" + esc(g.max) + "</span></div>" +
      '<div class="meta"><span>' + esc(g.student_name) + "</span><span>" + esc(g.evaluation) + "</span><span>" + esc(shortDate(g.date)) + "</span></div></div>").join("");
    return html + '<div class="list">' + items + "</div>";
  }

  results.forEach(entry => {
    const st = entry.student || {};
    const grades = entry.grades || [];
    const bySubject = {};
    grades.forEach(g => {
      const k = g.subject;
      if (!bySubject[k]) bySubject[k] = [];
      bySubject[k].push(g);
    });
    let sum = 0, cnt = 0;
    const rowsHtml = Object.keys(bySubject).map(subj => {
      const arr = bySubject[subj];
      const vals = arr.map(g => Number(g.note) / Number(g.max) * 20);
      const avg = vals.reduce((a, b) => a + b, 0) / (vals.length || 1);
      sum += avg; cnt++;
      return '<div class="kv"><span>' + esc(subj) + "</span><span>" +
        '<span class="chip ' + (avg >= 10 ? "green" : "red") + '">' + avg.toFixed(2) + "/20</span></span></div>";
    }).join("");
    const total = cnt ? (sum / cnt).toFixed(2) : "—";
    html += '<div class="panel"><div class="panel-head"><h2>' + esc(st.first_name + " " + st.last_name) + "</h2>" +
      '<span class="chip">' + esc(st.class_name || "") + "</span></div>" +
      (rowsHtml || '<p class="muted">' + t("empty") + "</p>") +
      '<div class="kv" style="border-top:2px solid var(--orange);margin-top:6px"><span><strong>' + t("average") + "</strong></span><span><strong>" + total + "/20</strong></span></div>" +
      "</div>";
  });
  return html;
}

/* ---------------- frais ---------------- */
async function renderFees() {
  let data;
  try { data = await api("/fees"); } catch (e) { fail(e); data = { fees: [] }; }
  const fees = data.fees || [];
  let totalDue = 0;
  fees.forEach(f => { if (!f.paid) totalDue += Number(f.amount); });
  const items = fees.map(f =>
    '<div class="item' + (f.paid ? " neutral" : "") + '"><div class="row"><strong>' + esc(pick(f, "label")) + "</strong>" +
    '<span class="chip ' + (f.paid ? "green" : "red") + '">' + esc(f.paid ? t("paid") : t("unpaid")) + "</span></div>" +
    '<div class="meta"><span>' + esc(money(f.amount)) + "</span>" +
    (isStaff() ? "<span>" + esc(f.student_name) + "</span>" : "") +
    '<span>' + esc(shortDate(f.due_date)) + "</span>" +
    (isStaff() ? '<a href="#" onclick="toggleFee(' + f.id + "," + (f.paid ? 0 : 1) + ');return false">' +
      (f.paid ? t("unpaid") : t("paid")) + "</a>" : "") +
    "</div></div>").join("");
  return pageTitle(t("t_fees")) +
    '<div class="panel" style="display:flex;justify-content:space-between;align-items:center">' +
    "<span>" + t("total") + " " + t("unpaid") + '</span><strong style="color:var(--red);font-size:18px">' + esc(money(totalDue)) + "</strong></div>" +
    '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

async function toggleFee(id, paid) {
  try { await api("/fees/toggle", { body: { id, paid } }); toast(t("saved")); render(); } catch (e) { fail(e); }
}

/* ---------------- cantine ---------------- */
async function renderMenu() {
  let data;
  try { data = await api("/menu"); } catch (e) { fail(e); data = { menu: [] }; }
  const items = (data.menu || []).map(m =>
    '<div class="item"><div class="row"><strong>' + esc(dayName(Number(m.day))) + "</strong></div>" +
    "<p>" + esc(pick(m, "dish")) + "</p></div>").join("");
  return pageTitle(t("t_menu")) + '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

/* ---------------- transport ---------------- */
async function renderBus() {
  let data;
  try { data = await api("/bus"); } catch (e) { fail(e); data = { bus: [] }; }
  const items = (data.bus || []).map(b =>
    '<div class="item"><strong>' + esc(b.route) + "</strong>" +
    "<p>" + esc(b.driver) + " · " + esc(b.phone) + "</p>" +
    '<div class="meta"><span>' + esc(b.morning) + " → " + esc(b.evening) + "</span></div></div>").join("");
  return pageTitle(t("t_bus")) + '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

/* ---------------- photos ---------------- */
async function renderPhotos() {
  let data;
  try { data = await api("/photos"); } catch (e) { fail(e); data = { photos: [] }; }
  const items = (data.photos || []).map(p =>
    '<div class="tile light" style="min-height:96px">' + ico(p.icon, "tile-ico") +
    "<span>" + esc(p.title) + "</span></div>").join("");
  return pageTitle(t("t_photos")) + '<div class="grid">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

/* ---------------- contact ---------------- */
function renderContact() {
  const c = state.config;
  const rows = [
    [t("address"), c.school_address || ""],
    [t("phone"), (c.school_phone || "") + " / " + (c.school_phone2 || "")],
    [t("email"), c.school_email || ""],
    [t("hours"), "Dim → Jeu : 7h30 - 17h30"]
  ];
  return pageTitle(t("t_contact")) +
    '<div class="panel"><h2>' + esc(c.school_name || "Les Théorèmes") + "</h2>" +
    rows.map(r => '<div class="kv"><span>' + esc(r[0]) + "</span><span>" + esc(r[1]) + "</span></div>").join("") +
    '<a class="btn primary block" style="margin-top:14px;text-decoration:none" href="tel:' + esc((c.school_phone || "").replace(/[^+\d]/g, "")) + '">' + t("call") + "</a>" +
    '<a class="btn secondary block" style="margin-top:8px;text-decoration:none" href="mailto:' + esc(c.school_email || "") + '">' + t("write") + "</a>" +
    '<a class="btn secondary block" style="margin-top:8px;text-decoration:none" href="' + esc(c.school_maps || "#") + '" target="_blank">' + t("itinerary") + "</a>" +
    "</div>";
}

/* ---------------- carnet de correspondance (messages) ---------------- */
async function renderCarnet() {
  messagesCache = null;
  return pageTitle(t("t_carnet")) +
    '<div class="panel"><p class="muted">' + t("loading") + "</p></div>";
}

async function loadCarnet() {
  try {
    const d = await api("/messages");
    const myId = String(state.me.id);
    const items = (d.messages || []).map(m => {
      const mine = String(m.from_id) === myId;
      const who = mine ? m.to_name : m.from_name;
      return '<div class="item' + (mine ? " neutral" : "") + '"><div class="row">' +
        "<strong>" + esc(m.subject || t("message")) + "</strong>" +
        '<span class="chip ' + (mine ? "grey" : "green") + '">' + esc(mine ? "→ " + who : "← " + who) + "</span></div>" +
        "<p>" + esc(m.body) + "</p>" +
        '<div class="meta"><span>' + esc(m.created_at) + "</span></div></div>";
    }).join("");
    $("#view").innerHTML = pageTitle(t("t_carnet")) +
      '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>" +
      '<div class="panel" style="margin-top:12px"><button class="btn primary block" onclick="go({name:\'messages\'})">' + t("send") + "</button></div>";
    markRead();
  } catch (e) { fail(e); }
}

/* ---------------- gestion des comptes (admin) ---------------- */
async function renderUsers() {
  let data;
  try { data = await api("/users"); } catch (e) { fail(e); return pageTitle(t("t_users")); }
  const users = data.users || [];
  const items = users.map(u =>
    '<div class="item' + (u.active ? "" : " neutral") + '"><div class="row"><strong>' + esc(u.first_name + " " + u.last_name) + "</strong>" +
    '<span class="chip ' + (u.role === "admin" ? "" : u.role === "moderator" ? "grey" : "green") + '">' + esc(roleLabel(u.role)) + "</span></div>" +
    '<div class="meta"><span>' + esc(u.email) + "</span></div>" +
    '<div class="meta">' + (u.active ? '<span class="chip green">' + t("active") + "</span>" : '<span class="chip red">' + t("no_account_yet") + "</span>") +
    (u.role === "parent" && u.child_name ? "<span>👦 " + esc(u.child_name) + "</span>" : "") +
    (String(u.id) !== String(state.me.id) && u.role !== "admin"
      ? '<a href="#" onclick="toggleUser(' + u.id + "," + (u.active ? 0 : 1) + ');return false">' + (u.active ? t("delete") : t("active")) + "</a>" +
        '<a href="#" onclick="resetUserPassword(' + u.id + ');return false">' + t("new_password") + "</a>" +
        (isAdmin() ? '<a href="#" onclick="deleteUser(' + u.id + ');return false" style="color:var(--red)">' + t("delete") + "</a>" : "")
      : "") +
    "</div></div>").join("");

  const roleOptions = isAdmin()
    ? '<option value="parent">' + t("role_parent") + '</option><option value="moderator">' + t("role_moderator") + "</option>"
    : '<option value="parent">' + t("role_parent") + "</option>";

  return pageTitle(t("t_users")) +
    '<div class="panel"><h2>' + t("new_user") + '</h2><div class="form-grid">' +
    '<div class="two"><input id="nu-first" placeholder="' + esc(t("name")) + '"><input id="nu-last" placeholder="' + esc(t("last_name")) + '"></div>' +
    '<input id="nu-email" type="email" inputmode="email" placeholder="' + esc(t("email")) + '">' +
    '<input id="nu-phone" inputmode="tel" placeholder="' + esc(t("phone")) + '">' +
    "<select id=\"nu-role\" onchange=\"updateChildBlock()\">" + roleOptions + "</select>" +
    '<input id="nu-pw" type="text" placeholder="' + esc(t("password")) + ' (Maj, min, chiffre)">' +
    '<div id="nu-child"' + (isAdmin() ? "" : "") + '>' +
      '<h3 style="margin:14px 0 6px">' + t("child_required_title") + '</h3>' +
      '<p class="muted" style="margin:0 0 8px">' + t("child_required") + "</p>" +
      '<div class="two"><input id="nu-child-first" placeholder="' + esc(t("name")) + '"><input id="nu-child-last" placeholder="' + esc(t("last_name")) + '"></div>' +
      '<div class="two"><input id="nu-child-level" placeholder="' + esc(t("level")) + ' (Préscolaire, Primaire, Moyen)"><input id="nu-child-class" placeholder="3AP"></div>' +
    "</div>" +
    '<button class="btn primary" onclick="createUser()">' + t("add") + "</button>" +
    '<p class="muted" style="margin:0">' + t("login_hint") + "</p>" +
    "</div></div>" +
    '<div class="list">' + items + "</div>";
}

/* le bloc enfant n'a de sens que pour un parent */
function updateChildBlock() {
  const role = $("#nu-role").value;
  const box = $("#nu-child");
  if (box) box.hidden = role !== "parent";
}

async function createUser() {
  const role = $("#nu-role").value;
  const body = {
    first_name: $("#nu-first").value.trim(),
    last_name: $("#nu-last").value.trim(),
    email: $("#nu-email").value.trim(),
    phone: $("#nu-phone").value.trim(),
    role: role,
    password: $("#nu-pw").value
  };
  if (!body.first_name || !body.last_name || !body.email || !body.password) { toast(t("required")); return; }
  if (role === "parent") {
    body.child_first_name = $("#nu-child-first").value.trim();
    body.child_last_name = $("#nu-child-last").value.trim();
    body.child_level = $("#nu-child-level").value.trim();
    body.child_class = $("#nu-child-class").value.trim();
    if (!body.child_first_name || !body.child_last_name ||
        (!body.child_level && !body.child_class)) { toast(t("child_required")); return; }
  }
  try {
    const d = await api("/users", { body });
    toast(d.child_name ? t("created_with_child").replace("{child}", d.child_name) : t("created"));
    render();
  } catch (e) { fail(e); }
}

async function toggleUser(id, active) {
  try { await api("/users/update", { body: { id, active } }); toast(t("saved")); render(); } catch (e) { fail(e); }
}

async function resetUserPassword(id) {
  const pw = prompt(t("new_password") + " (8+, Maj, min, chiffre)");
  if (!pw) return;
  try { await api("/users/update", { body: { id, password: pw } }); toast(t("saved")); } catch (e) { fail(e); }
}

async function deleteUser(id) {
  if (!confirm(t("confirm_delete"))) return;
  try { await api("/users/delete", { body: { id } }); toast(t("deleted")); render(); } catch (e) { fail(e); }
}

/* ---------------- eleves (admin) ---------------- */
async function renderStudentsAdmin() {
  let data, parents;
  try {
    data = await api("/students");
    parents = await api("/users");
  } catch (e) { fail(e); return pageTitle(t("t_students")); }
  const list = data.students || [];
  const parentOpts = (parents.users || []).filter(u => u.role === "parent")
    .map(u => '<option value="' + u.id + '">' + esc(u.first_name + " " + u.last_name) + " — " + esc(u.email) + "</option>").join("");
  const items = list.map(s =>
    '<div class="item neutral"><div class="row"><strong>' + esc(s.first_name + " " + s.last_name) + "</strong>" +
    '<span class="chip">' + esc(s.class_name || s.level) + "</span></div>" +
    '<div class="meta"><span>' + esc(s.parent_name || "") + "</span>" +
    (s.bus_route ? "<span>" + esc(s.bus_route) + "</span>" : "") + "</div></div>").join("");
  return pageTitle(t("t_students")) +
    '<div class="panel"><h2>' + t("new_student") + "</h2><div class=\"form-grid\">" +
    "<select id=\"st-parent\">" + parentOpts + "</select>" +
    '<div class="two"><input id="st-first" placeholder="' + esc(t("name")) + '"><input id="st-last" placeholder="' + esc(t("last_name")) + '"></div>' +
    '<div class="two"><input id="st-level" placeholder="' + esc(t("class")) + ' (Primaire)"><input id="st-class" placeholder="3AP"></div>' +
    '<input id="st-bus" placeholder="' + esc(t("t_bus")) + '">' +
    '<button class="btn primary" onclick="createStudent()">' + t("add") + "</button></div></div>" +
    '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

async function createStudent() {
  const body = {
    parent_id: Number($("#st-parent").value || 0),
    first_name: $("#st-first").value.trim(),
    last_name: $("#st-last").value.trim(),
    level: $("#st-level").value.trim(),
    class_name: $("#st-class").value.trim(),
    bus_route: $("#st-bus").value.trim()
  };
  if (!body.parent_id || !body.first_name || !body.last_name) { toast(t("required")); return; }
  try { await api("/students", { body }); toast(t("created")); render(); } catch (e) { fail(e); }
}

/* ---------------- tableau de bord ---------------- */
async function renderStats() {
  let d;
  try { d = await api("/stats"); } catch (e) { fail(e); return pageTitle(t("t_stats")); }
  const cards = [
    [t("parent"), d.parents],
    [t("role_moderator"), d.moderators],
    [t("student"), d.students],
    [t("t_announcements"), d.announcements],
    [t("t_fees"), d.unpaid]
  ];
  return pageTitle(t("t_stats")) + '<div class="grid">' + cards.map(c =>
    '<div class="tile light" style="min-height:92px"><span style="font-size:26px;font-weight:700;color:var(--orange)">' +
    esc(c[1]) + "</span><span>" + esc(c[0]) + "</span></div>").join("") + "</div>";
}

/* ---------------- journal ---------------- */
async function renderAudit() {
  let d;
  try { d = await api("/audit"); } catch (e) { fail(e); return pageTitle(t("t_audit")); }
  const items = (d.events || []).map(e =>
    '<div class="item neutral"><div class="row"><strong>' + esc(e.action) + "</strong></div>" +
    '<div class="meta"><span>' + esc(e.detail) + "</span><span>" + esc(e.created_at) + "</span></div></div>").join("");
  return pageTitle(t("t_audit")) + '<div class="list">' + (items || '<div class="empty">' + t("empty") + "</div>") + "</div>";
}

/* ---------------- démarrage ---------------- */
function bindEvents() {
  $("#login-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    showLoginError("");
    const btn = e.target.querySelector("button[type=submit]");
    btn.disabled = true;
    try {
      const data = await api("/login", {
        body: { email: $("#email").value.trim(), password: $("#password").value }
      });
      await afterLogin(data);
    } catch (err) {
      showLoginError(err.message || errText(err.code));
    } finally {
      btn.disabled = false;
    }
  });

  $("#pw-toggle").addEventListener("click", () => {
    const inp = $("#password");
    inp.type = inp.type === "password" ? "text" : "password";
  });

  $$(".lang-btn").forEach(b => b.addEventListener("click", () => {
    applyLang(b.dataset.lang);
    if (state.me) { render(); } 
  }));

  $("#btn-lang").addEventListener("click", () => {
    const order = ["fr", "ar", "en"];
    applyLang(order[(order.indexOf(LANG) + 1) % order.length]);
    render();
  });

  $$(".tab").forEach(b => b.addEventListener("click", async () => {
    const name = b.dataset.tab;
    if (name === "messages") {
      try { messagesCache = await loadMessages(); } catch (e) { messagesCache = { messages: [] }; }
    }
    go({ name });
  }));

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") document.hidden = false;
  });
}

async function init() {
  applyLang(LANG);
  bindEvents();
  try { state.config = await api("/config"); } catch (e) { state.config = {}; }
  if (state.token) await boot();
  else { $("#screen-login").hidden = false; }
  setInterval(() => { if (state.token) refreshUnread(); }, 45000);
}

init();
