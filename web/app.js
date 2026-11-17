const POLL_INTERVAL_MS = 5000;
const STALE_AFTER_MS = 15000;
const MAX_SESSION_POINTS = 360;
const observedSamples = [];
let latestMetrics = null;
let latestMetricsReceivedAt = null;
let latestHealth = null;
let lastSampleKey = null;
let requestInProgress = false;
let trendChart = null;

const $ = id => document.getElementById(id);
const formatTimestamp = value => {
  if (!value) return 'Timestamp unavailable';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return 'Timestamp unavailable';
  return new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'medium' }).format(date);
};
const formatCapacity = (bytes, unit) => {
  if (!Number.isFinite(bytes) || bytes < 0) return '—';
  const divisor = unit === 'GB' ? 1e9 : 1024 ** 3;
  const value = bytes / divisor;
  return `${value >= 100 ? value.toFixed(0) : value.toFixed(1)} ${unit}`;
};
const setText = (id, value) => { const node = $(id); if (node) node.textContent = value; };

function setConnectionState(state, message) {
  const indicator = $('api-status');
  const dot = $('connection-dot');
  const chartText = $('connection-text');
  if (indicator) {
    indicator.classList.remove('connected', 'partial', 'disconnected');
    indicator.classList.add(state);
    indicator.innerHTML = `<i></i> ${message}`;
  }
  if (dot) {
    dot.classList.remove('connected', 'partial', 'disconnected');
    dot.classList.add(state);
  }
  if (chartText) chartText.textContent = message === 'Connected' ? 'Connected to monitor API' : message;
}

function setStatus(status, details) {
  const normalized = ['HEALTHY', 'WARNING', 'UNKNOWN'].includes(status) ? status : 'UNKNOWN';
  const badge = $('overall-status');
  badge.textContent = normalized;
  badge.classList.remove('healthy', 'warning', 'unknown');
  badge.classList.add(normalized.toLowerCase());
  const icon = $('status-icon');
  icon.classList.remove('healthy', 'warning', 'unknown');
  icon.classList.add(normalized.toLowerCase());
  icon.firstElementChild.textContent = normalized === 'HEALTHY' ? '✓' : normalized === 'WARNING' ? '!' : '?';
  const title = normalized === 'HEALTHY' ? 'All systems operational' : normalized === 'WARNING' ? 'Attention required' : 'System status unknown';
  setText('status-title', title);
  setText('status-description', details || (normalized === 'HEALTHY'
    ? 'No threshold warnings reported by the monitor.'
    : normalized === 'WARNING' ? 'One or more monitored resources exceeded a warning threshold.'
      : 'The monitor has not reported a complete system status.'));
}

function renderMetric({ key, source, pct, threshold, detail, capacity }) {
  const available = source?.available === true && Number.isFinite(pct);
  const gauge = $(`${key}-gauge`);
  gauge.classList.toggle('unavailable', !available);
  gauge.classList.toggle('over-threshold', available && pct >= threshold);
  gauge.style.setProperty('--value', available ? Math.max(0, Math.min(100, pct)) : 0);
  setText(`${key}-value`, available ? `${pct.toFixed(1)}%` : '—');
  setText(`${key}-gauge-label`, available ? (pct >= threshold ? 'WARNING' : 'IN USE') : 'UNAVAILABLE');
  setText(`${key}-threshold`, Number.isFinite(threshold) ? `${threshold}%` : '—');
  setText(`${key}-detail`, available ? detail : 'Measurement unavailable');
  if (capacity !== undefined) setText(`${key}-capacity`, available ? capacity : '—');
}

function renderMetrics(data) {
  const cpu = data.cpu || {};
  const memory = data.memory || {};
  const filesystem = data.root_filesystem || {};
  const memUsedKb = Number.isFinite(memory.total_kb) && Number.isFinite(memory.available_kb)
    ? Math.max(0, memory.total_kb - memory.available_kb) : null;
  const fsUsedBytes = Number.isFinite(filesystem.total_bytes) && Number.isFinite(filesystem.available_bytes)
    ? Math.max(0, filesystem.total_bytes - filesystem.available_bytes) : null;
  renderMetric({ key: 'cpu', source: cpu, pct: cpu.used_pct, threshold: cpu.warning_threshold_pct,
    detail: Number.isFinite(cpu.used_pct) ? `${cpu.used_pct.toFixed(1)}%` : 'Measurement unavailable' });
  renderMetric({ key: 'memory', source: memory, pct: memory.used_pct, threshold: memory.warning_threshold_pct,
    detail: memUsedKb === null ? 'Measurement unavailable' : `${formatCapacity(memUsedKb * 1024, 'GiB')} used`,
    capacity: Number.isFinite(memory.total_kb) ? `of ${formatCapacity(memory.total_kb * 1024, 'GiB')}` : '—' });
  renderMetric({ key: 'storage', source: filesystem, pct: filesystem.used_pct, threshold: filesystem.warning_threshold_pct,
    detail: fsUsedBytes === null ? 'Measurement unavailable' : `${formatCapacity(fsUsedBytes, 'GB')} used`,
    capacity: Number.isFinite(filesystem.total_bytes) ? `of ${formatCapacity(filesystem.total_bytes, 'GB')}` : '—' });

  const sampleTime = data.sampled_at;
  setText('last-sample', formatTimestamp(sampleTime));
  const sampleDate = sampleTime ? new Date(sampleTime) : null;
  const isStale = sampleDate && !Number.isNaN(sampleDate.getTime()) && Date.now() - sampleDate.getTime() > STALE_AFTER_MS;
  document.querySelectorAll('.metric-card').forEach(card => card.classList.toggle('stale', Boolean(isStale)));
  $('sample-dot').classList.toggle('stale', Boolean(isStale));
  const statusDetail = isStale ? 'The latest reported sample is older than 15 seconds.' : undefined;
  setStatus(data.status, statusDetail);
  if (isStale) {
    setText('status-title', 'Stale monitor sample');
    setText('status-description', 'Latest monitor sample is older than 15 seconds. Waiting for a newer reading.');
  }

  // A timestamp identifies a completed backend sample. Repeated polls of that
  // same report do not create extra historical points; null timestamps cannot
  // safely identify distinct samples, so they are omitted from the trend.
  if (sampleTime && !Number.isNaN(new Date(sampleTime).getTime()) && sampleTime !== lastSampleKey) {
    lastSampleKey = sampleTime;
    observedSamples.push({
      timestamp: sampleTime,
      cpu: cpu.available && Number.isFinite(cpu.used_pct) ? cpu.used_pct : null,
      memory: memory.available && Number.isFinite(memory.used_pct) ? memory.used_pct : null,
      storage: filesystem.available && Number.isFinite(filesystem.used_pct) ? filesystem.used_pct : null
    });
    if (observedSamples.length > MAX_SESSION_POINTS) observedSamples.shift();
    updateTrend();
  }
}

function updateTrend() {
  const labels = observedSamples.map(sample => {
    const date = new Date(sample.timestamp);
    return new Intl.DateTimeFormat(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' }).format(date);
  });
  setText('trend-description', `Samples observed during this browser session · ${observedSamples.length} ${observedSamples.length === 1 ? 'reading' : 'readings'}`);
  if (!trendChart) return;
  trendChart.data.labels = labels;
  trendChart.data.datasets[0].data = observedSamples.map(point => point.cpu);
  trendChart.data.datasets[1].data = observedSamples.map(point => point.memory);
  trendChart.data.datasets[2].data = observedSamples.map(point => point.storage);
  trendChart.update('none');
}

async function getJson(path) {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), 4000);
  try {
    const response = await fetch(path, { method: 'GET', headers: { Accept: 'application/json' }, cache: 'no-store', signal: controller.signal });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return await response.json();
  } finally {
    window.clearTimeout(timeout);
  }
}

async function refreshData() {
  if (requestInProgress) return;
  requestInProgress = true;
  const refresh = $('refresh-button');
  refresh?.classList.add('loading');
  const [healthResult, metricsResult] = await Promise.allSettled([getJson('/api/health'), getJson('/api/metrics')]);
  requestInProgress = false;
  refresh?.classList.remove('loading');

  latestHealth = healthResult.status === 'fulfilled' ? healthResult.value : null;
  const healthOk = Boolean(latestHealth && latestHealth.ok === true);
  if (metricsResult.status === 'fulfilled') {
    latestMetrics = metricsResult.value;
    latestMetricsReceivedAt = Date.now();
    renderMetrics(latestMetrics);
  }

  const metricsOk = metricsResult.status === 'fulfilled';
  if (healthOk && metricsOk) {
    setConnectionState('connected', 'Connected');
  } else if (healthOk || metricsOk) {
    setConnectionState('partial', 'Partial connection');
    if (!metricsOk && latestHealth && !latestMetrics) {
      setStatus(latestHealth.status, 'Health endpoint responded, but current metric readings could not be loaded.');
      setText('last-sample', formatTimestamp(latestHealth.sampled_at));
    }
    setText('status-description', `Monitor API partially available. ${metricsOk ? 'Health check failed.' : 'Metrics request failed; showing last successful readings.'}`);
  } else {
    setConnectionState('disconnected', 'Disconnected');
    if (!latestMetrics) {
      setStatus('UNKNOWN', 'Cannot reach the monitor API. No readings are available.');
      setText('last-sample', 'No sample received');
    } else {
      const elapsed = Date.now() - latestMetricsReceivedAt;
      setStatus(latestMetrics.status, `API disconnected. Showing the last successful sample from ${formatTimestamp(latestMetrics.sampled_at)} (${Math.floor(elapsed / 1000)} seconds ago).`);
      setText('status-title', 'Monitor disconnected · last known status');
      document.querySelectorAll('.metric-card').forEach(card => card.classList.add('stale'));
      $('sample-dot').classList.add('stale');
    }
  }
}

function initializeChart() {
  const canvas = $('trend-chart');
  if (!window.Chart || !canvas) {
    setText('trend-description', 'Chart library unavailable · session readings cannot be graphed');
    return;
  }
  const context = canvas.getContext('2d');
  const gradient = context.createLinearGradient(0, 0, 0, 230);
  gradient.addColorStop(0, 'rgba(66,120,232,.13)');
  gradient.addColorStop(1, 'rgba(66,120,232,0)');
  trendChart = new Chart(context, {
    type: 'line',
    data: { labels: [], datasets: [
      { label: 'CPU', data: [], borderColor: '#4278e8', backgroundColor: gradient, fill: true, tension: .36, borderWidth: 2, pointRadius: 0, pointHoverRadius: 4, spanGaps: false },
      { label: 'Memory', data: [], borderColor: '#8063df', backgroundColor: 'transparent', tension: .36, borderWidth: 2, pointRadius: 0, pointHoverRadius: 4, spanGaps: false },
      { label: 'Storage', data: [], borderColor: '#18a889', backgroundColor: 'transparent', tension: .36, borderWidth: 2, pointRadius: 0, pointHoverRadius: 4, spanGaps: false }
    ] },
    options: {
      responsive: true, maintainAspectRatio: false, interaction: { intersect: false, mode: 'index' },
      plugins: { legend: { display: false }, tooltip: { backgroundColor: '#172334', padding: 10, titleFont: { family: 'DM Sans', size: 10 }, bodyFont: { family: 'DM Mono', size: 10 }, callbacks: { label: item => ` ${item.dataset.label}: ${item.formattedValue}%` } } },
      scales: {
        x: { grid: { display: false }, border: { display: false }, ticks: { color: '#9aa4b2', maxTicksLimit: 6, font: { family: 'DM Mono', size: 9 }, maxRotation: 0 } },
        y: { min: 0, max: 100, border: { display: false, dash: [3, 4] }, grid: { color: '#edf0f4', drawTicks: false }, ticks: { stepSize: 25, padding: 10, color: '#9aa4b2', font: { family: 'DM Mono', size: 9 }, callback: value => `${value}%` } }
      }
    }
  });
}

initializeChart();
$('refresh-button')?.addEventListener('click', refreshData);
refreshData();
window.setInterval(() => {
  // Re-evaluate freshness even when the API is returning the same report.
  if (latestMetrics && latestMetrics.sampled_at) {
    const sampleAge = Date.now() - new Date(latestMetrics.sampled_at).getTime();
    const stale = Number.isFinite(sampleAge) && sampleAge > STALE_AFTER_MS;
    document.querySelectorAll('.metric-card').forEach(card => card.classList.toggle('stale', stale));
    $('sample-dot').classList.toggle('stale', stale);
    if (stale && latestHealth) {
      setText('status-title', 'Stale monitor sample');
      setText('status-description', 'Latest monitor sample is older than 15 seconds. Waiting for a newer reading.');
    }
  }
  refreshData();
}, POLL_INTERVAL_MS);
