const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];

const glyphs = {
  Food: '🍲', Transport: '🚗', Shopping: '🛍', Bills: '⚡', Entertainment: '🎬',
  Health: '✚', Home: '⌂', Travel: '✈', Education: '✎', Salary: '↗', Other: '✳',
  default: '◌'
};
const chartColors = ['#78a77b', '#e8a27c', '#8ca5c4', '#e1c16e', '#9a8fc0', '#82b7a8', '#df8a83', '#7795a2', '#b8a174', '#a9b779', '#d18ea5'];
const defaultCategories = ['Food', 'Transport', 'Shopping', 'Bills', 'Entertainment', 'Health', 'Home', 'Travel', 'Education', 'Salary', 'Other'];
const defaultState = { transactions: [], categories: defaultCategories, budgets: {}, monthBudget: 0, currency: 'INR' };
let state = structuredClone(defaultState);
let selectedMonth = localMonth(new Date());
let page = 0;
let toastTimer;
const demoMode = new URLSearchParams(location.search).get('demo') === '1';
let offlineMode = demoMode;
let offlineState = null;
const previewStorageKey = 'milo-expense-tracker-preview';

function localMonth(date) { return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}`; }
function todayString() { const d = new Date(); return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`; }
function esc(value) { return String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])); }
function currencyFormatter() { try { return new Intl.NumberFormat(undefined, { style: 'currency', currency: state.currency || 'INR', maximumFractionDigits: 2 }); } catch { return new Intl.NumberFormat(undefined, { style: 'currency', currency: 'INR', maximumFractionDigits: 2 }); } }
function money(value) { return currencyFormatter().format(Number(value || 0)); }
function symbol() { try { return currencyFormatter().formatToParts(0).find(x => x.type === 'currency')?.value || '₹'; } catch { return '₹'; } }
function fmtDate(value) { if (!value) return ''; const [y, m, d] = value.split('-').map(Number); return new Intl.DateTimeFormat(undefined, { month: 'short', day: 'numeric' }).format(new Date(y, m - 1, d)); }
function monthName(value, year = true) { const [y, m] = value.split('-').map(Number); return new Intl.DateTimeFormat(undefined, { month: 'long', ...(year ? { year: 'numeric' } : {}) }).format(new Date(y, m - 1, 1)); }
function monthShift(value, delta) { const [y, m] = value.split('-').map(Number); const d = new Date(y, m - 1 + delta, 1); return localMonth(d); }
function icon(name) {
  const p = {
    grid: '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
    receipt: '<path d="M5 3h14v18l-2.6-1.7L14 21l-2-1.7L9.5 21 7 19.3 5 21z"/><path d="M8 8h8M8 12h8M8 16h4"/>',
    pie: '<path d="M12 3v9h9A9 9 0 1 1 12 3Z"/><path d="M16 3.7A9 9 0 0 1 20.3 8H16z"/>',
    settings: '<circle cx="12" cy="12" r="3"/><path d="m19.4 15 .1.1a1.7 1.7 0 0 1-2.4 2.4l-.1-.1a1.7 1.7 0 0 0-2.9 1.2v.2a1.7 1.7 0 0 1-3.4 0v-.2a1.7 1.7 0 0 0-2.9-1.2l-.1.1a1.7 1.7 0 0 1-2.4-2.4l.1-.1a1.7 1.7 0 0 0-1.2-2.9H4a1.7 1.7 0 0 1 0-3.4h.2a1.7 1.7 0 0 0 1.2-2.9l-.1-.1a1.7 1.7 0 0 1 2.4-2.4l.1.1a1.7 1.7 0 0 0 2.9-1.2V4a1.7 1.7 0 0 1 3.4 0v.2a1.7 1.7 0 0 0 2.9 1.2l.1-.1a1.7 1.7 0 0 1 2.4 2.4l-.1.1a1.7 1.7 0 0 0 1.2 2.9h.2a1.7 1.7 0 0 1 0 3.4h-.2a1.7 1.7 0 0 0-1.2.9Z"/>',
    calendar: '<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M16 3v4M8 3v4M3 10h18"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    'arrow-down': '<path d="M12 4v15m-6-6 6 6 6-6"/>',
    'arrow-up': '<path d="M12 20V5m-6 6 6-6 6 6"/>',
    'arrow-right': '<path d="M5 12h14m-6-6 6 6-6 6"/>',
    'trending-up': '<path d="m3 17 6-6 4 4 8-8M15 7h6v6"/>',
    'trending-down': '<path d="m3 7 6 6 4-4 8 8M15 17h6v-6"/>',
    leaf: '<path d="M20 4c-8 0-14 3-14 10a6 6 0 0 0 6 6c7 0 10-7 8-16Z"/><path d="M4 21c3-5 7-8 12-11"/>',
    spark: '<path d="m12 3 1.6 6.4L20 11l-6.4 1.6L12 19l-1.6-6.4L4 11l6.4-1.6L12 3Z"/><path d="m19 15 .8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8L19 15Z"/>',
    sliders: '<path d="M4 21v-7m0-4V3m8 18v-9m0-4V3m8 18v-5m0-4V3M2 14h4m4-6h4m4 6h4"/>',
    search: '<circle cx="10.8" cy="10.8" r="6.8"/><path d="m16 16 4 4"/>',
    download: '<path d="M12 3v12m-5-5 5 5 5-5M4 20h16"/>',
    menu: '<path d="M4 6h16M4 12h16M4 18h16"/>'
  };
  return `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${p[name] || ''}</svg>`;
}

async function api(path, options = {}) {
  if (offlineMode) return localApi(path, options);
  const headers = { ...(options.body ? { 'Content-Type': 'application/json' } : {}), ...options.headers };
  const response = await fetch(path, { ...options, headers, credentials: 'same-origin', body: options.body ? JSON.stringify(options.body) : undefined });
  if (!response.ok) {
    let message = `Request failed (${response.status})`;
    try { message = (await response.json()).error || message; } catch {}
    throw new Error(message);
  }
  if (response.status === 204) return null;
  const type = response.headers.get('content-type') || '';
  return type.includes('application/json') ? response.json() : response.text();
}
function getPreviewState() {
  if (offlineState) return offlineState;
  try {
    const saved = localStorage.getItem(previewStorageKey);
    if (saved) { offlineState = JSON.parse(saved); return offlineState; }
  } catch {}
  const today = todayString();
  offlineState = {
    ...structuredClone(defaultState), monthBudget: 30000,
    budgets: { Food: 8000, Transport: 2500, Bills: 5000, Entertainment: 1500 },
    transactions: [
      { id: 'preview-salary', type: 'income', amount: 82000, category: 'Salary', date: today, note: 'Monthly salary' },
      { id: 'preview-rent', type: 'expense', amount: 18000, category: 'Home', date: today, note: 'Monthly rent' },
      { id: 'preview-food', type: 'expense', amount: 1240, category: 'Food', date: today, note: 'Groceries & coffee' },
      { id: 'preview-trip', type: 'expense', amount: 380, category: 'Transport', date: today, note: 'Cab ride' },
      { id: 'preview-bill', type: 'expense', amount: 899, category: 'Bills', date: today, note: 'Internet bill' }
    ]
  };
  try { localStorage.setItem(previewStorageKey, JSON.stringify(offlineState)); } catch {}
  return offlineState;
}
function savePreviewState() {
  try { localStorage.setItem(previewStorageKey, JSON.stringify(offlineState)); } catch {}
}
function localApi(path, options = {}) {
  const method = options.method || 'GET';
  const body = options.body || {};
  const txPrefix = '/api/transactions/';
  const catPrefix = '/api/categories/';
  const rows = getPreviewState().transactions;
  if (path === '/api/state' && method === 'GET') return structuredClone(offlineState);
  if (path === '/api/transactions' && method === 'POST') {
    const row = { ...body, id: `preview-${Date.now()}-${Math.random().toString(36).slice(2, 7)}` };
    offlineState.transactions.push(row); savePreviewState(); return row;
  }
  if (path.startsWith(txPrefix)) {
    const id = decodeURIComponent(path.slice(txPrefix.length));
    const index = rows.findIndex(row => row.id === id);
    if (method === 'DELETE') {
      if (index < 0) throw new Error('Transaction not found');
      rows.splice(index, 1); savePreviewState(); return { ok: true };
    }
    if (method === 'PUT') {
      if (index < 0) throw new Error('Transaction not found');
      rows[index] = { ...body, id }; savePreviewState(); return rows[index];
    }
  }
  if (path === '/api/budgets' && method === 'PUT') {
    offlineState.monthBudget = body.monthBudget; offlineState.budgets = body.budgets; offlineState.currency = body.currency;
    savePreviewState(); return { ok: true };
  }
  if (path === '/api/categories' && method === 'POST') {
    const name = String(body.name || '').trim();
    if (!name) throw new Error('Category name is required');
    if (offlineState.categories.some(item => item.toLowerCase() === name.toLowerCase())) throw new Error('That category already exists');
    offlineState.categories.push(name); savePreviewState(); return { ok: true };
  }
  if (path.startsWith(catPrefix) && method === 'DELETE') {
    const name = decodeURIComponent(path.slice(catPrefix.length));
    if (defaultCategories.includes(name)) throw new Error('Default categories cannot be removed');
    if (rows.some(row => row.category === name)) throw new Error('This category has transactions. Reassign or remove them first.');
    offlineState.categories = offlineState.categories.filter(item => item !== name); delete offlineState.budgets[name];
    savePreviewState(); return { ok: true };
  }
  if (path === '/api/export.csv' && method === 'GET') {
    const cell = value => `"${String(value ?? '').replace(/"/g, '""')}"`;
    return ['Date,Type,Category,Note,Amount', ...rows.map(row => [row.date, row.type, row.category, row.note, Number(row.amount).toFixed(2)].map(cell).join(','))].join('\r\n');
  }
  throw new Error('This action is not available in preview mode.');
}
function toast(message) {
  const node = $('#toast'); node.textContent = message; node.classList.add('show');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => node.classList.remove('show'), 2600);
}
function monthTransactions() { return state.transactions.filter(t => t.date?.slice(0, 7) === selectedMonth); }
function totals(rows) {
  return rows.reduce((sum, t) => { if (t.type === 'income') sum.income += Number(t.amount); else sum.expenses += Number(t.amount); return sum; }, { income: 0, expenses: 0 });
}
function categoryColor(category) {
  const index = Math.max(0, state.categories.indexOf(category)); return chartColors[index % chartColors.length];
}
function render() {
  $('#monthPicker').value = selectedMonth;
  $('#todayLabel').textContent = monthName(selectedMonth).toUpperCase() + ' · YOUR PERSONAL FINANCES';
  const rows = monthTransactions(); const { income, expenses } = totals(rows); const balance = income - expenses;
  $('#balanceAmount').textContent = money(balance);
  $('#balanceFoot').textContent = `${money(income)} in · ${money(expenses)} out ${monthName(selectedMonth, false)}`;
  $('#incomeAmount').textContent = money(income); $('#expenseAmount').textContent = money(expenses);
  $('#incomeCount').textContent = `${rows.filter(t => t.type === 'income').length} ${rows.filter(t => t.type === 'income').length === 1 ? 'deposit' : 'deposits'}`;
  $('#expenseCount').textContent = `${rows.filter(t => t.type === 'expense').length} ${rows.filter(t => t.type === 'expense').length === 1 ? 'purchase' : 'purchases'}`;
  const rate = income ? (balance / income) * 100 : null;
  $('#savingsRate').textContent = rate === null ? '—' : `${Math.round(rate)}%`;
  $('#savingsBar').style.width = `${Math.max(0, Math.min(100, rate || 0))}%`;
  $('#savingsCaption').textContent = rate === null ? 'Add income to get started' : rate >= 0 ? `${money(Math.max(0, balance))} kept this month` : 'Spending is above income';
  $('#navCount').textContent = String(rows.length);
  renderDonut(rows); renderFlow(); renderBudgets(rows, expenses); renderCategoryOptions(); renderTransactions(); renderManageCategories();
  $('#amountCurrency').textContent = symbol(); $('.currency-prefix').textContent = symbol();
}
function renderDonut(rows) {
  const sums = new Map();
  for (const t of rows) if (t.type === 'expense') sums.set(t.category, (sums.get(t.category) || 0) + Number(t.amount));
  const entries = [...sums].sort((a, b) => b[1] - a[1]); const total = entries.reduce((n, x) => n + x[1], 0);
  $('#donutTotal').textContent = money(total);
  $('#chartEmpty').classList.toggle('hidden', total > 0);
  $('#donutLayout').classList.toggle('hidden', total === 0);
  let degrees = 0; const stops = entries.map(([name, amount]) => { const start = degrees; degrees += amount / total * 360; return `${categoryColor(name)} ${start.toFixed(2)}deg ${degrees.toFixed(2)}deg`; });
  $('#donutChart').style.background = total ? `conic-gradient(${stops.join(',')})` : 'conic-gradient(#eaf0e9 0deg 360deg)';
  $('#categoryLegend').innerHTML = entries.slice(0, 5).map(([name, amount]) => `<div class="legend-row"><span class="legend-dot" style="background:${categoryColor(name)}"></span><span class="legend-name">${esc(name)}</span><span class="legend-amount">${money(amount)}</span></div>`).join('');
}
function renderFlow() {
  const months = Array.from({ length: 6 }, (_, i) => monthShift(selectedMonth, i - 5));
  const data = months.map(month => ({ month, ...totals(state.transactions.filter(t => t.date?.slice(0, 7) === month)) }));
  const highest = Math.max(1, ...data.flatMap(d => [d.income, d.expenses])); const width = 450, height = 142, padTop = 13, baseY = 112, plotH = 91, groupW = width / 6;
  let svg = `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="Income and spending over the last six months">`;
  for (let i = 0; i < 3; i++) { const y = padTop + i * (plotH / 2); svg += `<line class="grid-line" x1="0" y1="${y}" x2="${width}" y2="${y}"/>`; }
  data.forEach((d, i) => {
    const x = groupW * i + groupW / 2; const ih = d.income / highest * plotH; const eh = d.expenses / highest * plotH;
    svg += `<rect x="${x - 13}" y="${baseY - ih}" width="10" height="${Math.max(1, ih)}" rx="3" fill="#9cbd9a"><title>${esc(monthName(d.month))}: income ${esc(money(d.income))}</title></rect>`;
    svg += `<rect x="${x + 2}" y="${baseY - eh}" width="10" height="${Math.max(1, eh)}" rx="3" fill="#e7a27d"><title>${esc(monthName(d.month))}: spending ${esc(money(d.expenses))}</title></rect>`;
    svg += `<text class="axis-text" x="${x}" y="132" text-anchor="middle">${esc(monthName(d.month, false).slice(0, 3))}</text>`;
  });
  svg += '</svg>'; $('#flowChart').innerHTML = svg;
  const latest = data[data.length - 1];
  $('#flowInsight').textContent = latest.income || latest.expenses ? `${money(latest.income - latest.expenses)} net in ${monthName(latest.month, false)}` : 'A little progress, month by month.';
}
function progressClass(value, limit) { const ratio = limit ? value / limit : 0; return ratio >= 1 ? 'over' : ratio >= .8 ? 'warn' : ''; }
function renderBudgets(rows, expenses) {
  const limit = Number(state.monthBudget || 0); const { income } = totals(rows);
  let overview = '';
  if (limit > 0) {
    const pct = Math.min(100, expenses / limit * 100); const tone = progressClass(expenses, limit);
    overview = `<div class="budget-overall"><div class="budget-overall-top"><strong>${money(expenses)} <span style="font:400 10px 'DM Sans';color:#9aa19a">of ${money(limit)}</span></strong><span>${Math.round(pct)}% used</span></div><div class="progress-track"><div class="progress-fill ${tone}" style="width:${pct}%"></div></div><div class="budget-caption"><span>${expenses > limit ? `${money(expenses - limit)} over your limit` : `${money(Math.max(0, limit - expenses))} left to spend`}</span><span>${esc(monthName(selectedMonth, false))}</span></div></div>`;
  } else {
    overview = `<div class="budget-overall"><div class="budget-overall-top"><strong>${money(expenses)} <span style="font:400 10px 'DM Sans';color:#9aa19a">spent so far</span></strong><span>${income ? `${money(income)} income` : 'No limit set'}</span></div><div class="progress-track"><div class="progress-fill" style="width:${expenses ? '18%' : '0%'}"></div></div><div class="budget-caption"><span>Set a monthly budget to stay on track</span><span>${esc(monthName(selectedMonth, false))}</span></div></div>`;
  }
  $('#budgetOverview').innerHTML = overview;
  const amounts = new Map(); for (const t of rows) if (t.type === 'expense') amounts.set(t.category, (amounts.get(t.category) || 0) + Number(t.amount));
  const categoryRows = Object.entries(state.budgets || {}).filter(([, amount]) => Number(amount) > 0).sort((a, b) => (amounts.get(b[0]) || 0) / Number(b[1]) - (amounts.get(a[0]) || 0) / Number(a[1]));
  $('#categoryBudgets').innerHTML = categoryRows.slice(0, 6).map(([name, cap]) => {
    const spent = amounts.get(name) || 0; const pct = Math.min(100, spent / Number(cap) * 100); const tone = progressClass(spent, Number(cap));
    return `<div class="budget-row"><span class="category-glyph">${glyphs[name] || glyphs.default}</span><div class="budget-row-main"><div class="budget-row-top"><strong>${esc(name)}</strong><span>${money(spent)} / ${money(cap)}</span></div><div class="budget-mini-track"><div class="budget-mini-fill ${tone}" style="width:${pct}%"></div></div></div></div>`;
  }).join('') || `<div class="empty-note" style="padding:14px 0">Your category limits will show up here.</div>`;
}
function renderCategoryOptions() {
  const options = state.categories.map(c => `<option value="${esc(c)}">${esc(c)}</option>`).join('');
  $('#transactionCategory').innerHTML = options;
  const current = $('#categoryFilter').value;
  $('#categoryFilter').innerHTML = '<option value="">All categories</option>' + options;
  if (state.categories.includes(current)) $('#categoryFilter').value = current;
}
function filteredTransactions() {
  const search = $('#searchInput').value.trim().toLowerCase(), category = $('#categoryFilter').value, type = $('#typeFilter').value;
  return monthTransactions().filter(t => (!category || t.category === category) && (!type || t.type === type) && (!search || `${t.category} ${t.note} ${t.type} ${t.amount}`.toLowerCase().includes(search))).sort((a, b) => b.date.localeCompare(a.date) || b.id.localeCompare(a.id));
}
function renderTransactions() {
  const items = filteredTransactions(); const pageSize = 5; const pages = Math.max(1, Math.ceil(items.length / pageSize)); page = Math.min(page, pages - 1);
  const shown = items.slice(page * pageSize, (page + 1) * pageSize);
  $('#transactionRows').innerHTML = shown.map(t => {
    const note = t.note?.trim() || t.category;
    return `<tr class="transaction-row"><td><div class="transaction-cell"><span class="transaction-mark">${glyphs[t.category] || glyphs.default}</span><span><span class="transaction-name">${esc(note)}</span><span class="transaction-sub">${t.type === 'income' ? 'Money in' : 'Money out'}</span></span></div></td><td><span class="category-pill"><i style="background:${categoryColor(t.category)}"></i>${esc(t.category)}</span></td><td>${esc(fmtDate(t.date))}</td><td class="amount-value ${t.type === 'income' ? 'income-text' : ''}">${t.type === 'income' ? '+' : '−'}${esc(money(t.amount))}</td><td class="row-actions"><button class="row-action" data-edit="${esc(t.id)}" title="Edit transaction" aria-label="Edit transaction">✎</button><button class="row-action" data-delete="${esc(t.id)}" title="Delete transaction" aria-label="Delete transaction">×</button></td></tr>`;
  }).join('');
  $('#tableEmpty').classList.toggle('hidden', shown.length > 0);
  $('#pageLabel').textContent = items.length ? `Showing ${page * pageSize + 1}–${Math.min(items.length, (page + 1) * pageSize)} of ${items.length}` : 'Showing 0 transactions';
  $('#prevPage').disabled = page <= 0; $('#nextPage').disabled = page >= pages - 1;
}
function renderManageCategories() {
  $('#categoryManageList').innerHTML = state.categories.map(name => `<div class="manage-category"><span class="category-glyph">${glyphs[name] || glyphs.default}</span><span>${esc(name)}</span>${defaultCategories.includes(name) ? '<span class="default-tag">DEFAULT</span>' : `<button type="button" class="remove-category" data-remove-category="${esc(name)}" title="Remove category" aria-label="Remove ${esc(name)}">×</button>`}</div>`).join('');
}
function showError(selector, message) { const el = $(selector); el.textContent = message; el.classList.remove('hidden'); }
function hideError(selector) { $(selector).textContent = ''; $(selector).classList.add('hidden'); }
function openTransaction(transaction = null) {
  const form = $('#transactionForm'); form.reset(); hideError('#transactionError');
  $('#transactionTitle').textContent = transaction ? 'Edit transaction' : 'Add transaction';
  $('#transactionId').value = transaction?.id || '';
  const type = transaction?.type || 'expense'; $('#transactionType').value = type;
  $$('.type-option').forEach(button => button.classList.toggle('active', button.dataset.type === type));
  $('#amountInput').value = transaction?.amount ?? '';
  $('#transactionDate').value = transaction?.date || todayString();
  $('#transactionNote').value = transaction?.note || '';
  $('#transactionCategory').value = transaction?.category || (type === 'income' && state.categories.includes('Salary') ? 'Salary' : state.categories[0]);
  $('#transactionDialog').showModal(); setTimeout(() => $('#amountInput').focus(), 40);
}
function openBudgetDialog() {
  hideError('#budgetError'); $('#monthBudgetInput').value = state.monthBudget || '';
  $('#currencySelect').value = state.currency || 'INR';
  $('#budgetInputs').innerHTML = state.categories.filter(c => c !== 'Salary').map(name => `<label class="budget-edit-row"><span class="category-glyph">${glyphs[name] || glyphs.default}</span><span>${esc(name)}</span><input type="number" min="0" step="0.01" data-budget-category="${esc(name)}" value="${Number(state.budgets?.[name] || 0) || ''}" placeholder="No limit" /></label>`).join('');
  $('#budgetDialog').showModal();
}
function openCategoryDialog() { hideError('#categoryError'); $('#categoryName').value = ''; renderManageCategories(); $('#categoryDialog').showModal(); }

document.addEventListener('click', async event => {
  const close = event.target.closest('[data-close]'); if (close) { $('#' + close.dataset.close).close(); return; }
  const type = event.target.closest('[data-type]');
  if (type) { $('#transactionType').value = type.dataset.type; $$('.type-option').forEach(b => b.classList.toggle('active', b === type)); if (type.dataset.type === 'income' && state.categories.includes('Salary')) $('#transactionCategory').value = 'Salary'; return; }
  const edit = event.target.closest('[data-edit]');
  if (edit) { const t = state.transactions.find(x => x.id === edit.dataset.edit); if (t) openTransaction(t); return; }
  const remove = event.target.closest('[data-delete]');
  if (remove) {
    if (!window.confirm('Delete this transaction? This cannot be undone.')) return;
    try { await api(`/api/transactions/${encodeURIComponent(remove.dataset.delete)}`, { method: 'DELETE' }); state.transactions = state.transactions.filter(t => t.id !== remove.dataset.delete); render(); toast('Transaction deleted.'); }
    catch (error) { toast(error.message); }
    return;
  }
  const removeCategory = event.target.closest('[data-remove-category]');
  if (removeCategory) {
    try { await api(`/api/categories/${encodeURIComponent(removeCategory.dataset.removeCategory)}`, { method: 'DELETE' }); state.categories = state.categories.filter(c => c !== removeCategory.dataset.removeCategory); delete state.budgets[removeCategory.dataset.removeCategory]; render(); renderManageCategories(); toast('Category removed.'); }
    catch (error) { showError('#categoryError', error.message); }
    return;
  }
  const go = event.target.closest('[data-go]');
  if (go) { $('#transactions').scrollIntoView({ behavior: 'smooth', block: 'start' }); $('.sidebar').classList.remove('open'); }
});

$('#addTransaction').addEventListener('click', () => openTransaction());
$('#emptyAdd').addEventListener('click', () => openTransaction());
$('#editBudgets').addEventListener('click', openBudgetDialog);
$('#currencyButton').addEventListener('click', openBudgetDialog);
$('#manageCategories').addEventListener('click', openCategoryDialog);
$('#mobileMenu').addEventListener('click', () => $('.sidebar').classList.toggle('open'));
$('#monthPicker').addEventListener('change', event => { if (event.target.value) { selectedMonth = event.target.value; page = 0; render(); } });
for (const selector of ['#searchInput', '#categoryFilter', '#typeFilter']) $(selector).addEventListener('input', () => { page = 0; renderTransactions(); });
$('#prevPage').addEventListener('click', () => { page = Math.max(0, page - 1); renderTransactions(); });
$('#nextPage').addEventListener('click', () => { page += 1; renderTransactions(); });
$('#exportCsv').addEventListener('click', async () => {
  try {
    const csv = await api('/api/export.csv'); const blob = new Blob(['\ufeff', csv], { type: 'text/csv;charset=utf-8' });
    const link = document.createElement('a'); link.href = URL.createObjectURL(blob); link.download = 'expense-tracker.csv'; link.click(); URL.revokeObjectURL(link.href); toast('Your CSV is ready.');
  } catch (error) { toast(error.message); }
});

$('#transactionForm').addEventListener('submit', async event => {
  event.preventDefault(); hideError('#transactionError');
  const body = { type: $('#transactionType').value, amount: Number($('#amountInput').value), category: $('#transactionCategory').value, date: $('#transactionDate').value, note: $('#transactionNote').value.trim() };
  if (!Number.isFinite(body.amount) || body.amount <= 0) { showError('#transactionError', 'Enter an amount greater than zero.'); return; }
  const id = $('#transactionId').value;
  try {
    const saved = await api(id ? `/api/transactions/${encodeURIComponent(id)}` : '/api/transactions', { method: id ? 'PUT' : 'POST', body });
    if (id) state.transactions = state.transactions.map(t => t.id === id ? saved : t); else state.transactions.push(saved);
    $('#transactionDialog').close(); render(); toast(id ? 'Transaction updated.' : 'Transaction added.');
  } catch (error) { showError('#transactionError', error.message); }
});

$('#budgetForm').addEventListener('submit', async event => {
  event.preventDefault(); hideError('#budgetError');
  const budgets = {};
  $$('[data-budget-category]').forEach(input => { const value = Number(input.value || 0); if (value > 0) budgets[input.dataset.budgetCategory] = value; });
  const body = { monthBudget: Number($('#monthBudgetInput').value || 0), budgets, currency: $('#currencySelect').value };
  if (body.monthBudget < 0 || Object.values(budgets).some(x => x < 0)) { showError('#budgetError', 'Budget amounts cannot be negative.'); return; }
  try { await api('/api/budgets', { method: 'PUT', body }); state.monthBudget = body.monthBudget; state.budgets = budgets; state.currency = body.currency; $('#budgetDialog').close(); render(); toast('Your budget is saved.'); }
  catch (error) { showError('#budgetError', error.message); }
});

$('#categoryForm').addEventListener('submit', async event => {
  event.preventDefault(); hideError('#categoryError'); const name = $('#categoryName').value.trim();
  if (!name) { showError('#categoryError', 'Give your category a name first.'); return; }
  try { await api('/api/categories', { method: 'POST', body: { name } }); state.categories.push(name); $('#categoryName').value = ''; render(); renderManageCategories(); toast('Category added.'); }
  catch (error) { showError('#categoryError', error.message); }
});

$$('.nav-link').forEach(link => link.addEventListener('click', () => {
  $$('.nav-link').forEach(item => item.classList.toggle('active', item === link)); $('.sidebar').classList.remove('open');
}));
$$('[data-icon]').forEach(node => { node.innerHTML = icon(node.dataset.icon); });

async function start() {
  if (demoMode) {
    state = { ...defaultState, ...getPreviewState() };
    $('#appShell').hidden = false;
    render();
    const banner = document.createElement('div'); banner.className = 'backend-banner';
    banner.innerHTML = '<span class="backend-banner-dot"></span><span><strong>Demo mode</strong> · Changes are stored only in this browser and are not synced to the cloud.</span>';
    $('.page-wrap').prepend(banner);
    $('.sidebar-foot').innerHTML = '<span class="status-dot"></span>Demo · Saved in this browser';
    return;
  }

  try {
    const loaded = await api('/api/state');
    state = { ...defaultState, ...loaded, budgets: loaded.budgets || {}, transactions: loaded.transactions || [], categories: loaded.categories || defaultCategories };
    if (!state.categories.includes('Salary')) state.categories.push('Salary');
    render();
    $('#appShell').hidden = false;
    hideError('#appError');
    $('.sidebar-foot').innerHTML = '<span class="status-dot"></span>Public cloud data · Anyone can access';
  } catch (error) {
    $('#appShell').hidden = false;
    render();
    showError('#appError', error.message || 'Could not connect to the tracker. Check your Vercel setup and try again.');
    $('.sidebar-foot').innerHTML = '<span class="status-dot"></span>Cloud connection unavailable';
  }
}

start();
