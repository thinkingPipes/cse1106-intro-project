// Visual foundation: representative sample data for the first design review.
const sampleHistory = {
  labels: ['09:00', '09:10', '09:20', '09:30', '09:40', '09:50', '10:00', '10:10', '10:20', '10:30', '10:40'],
  cpu: [24, 31, 28, 42, 36, 54, 39, 35, 47, 32, 38.4],
  memory: [59, 60, 61, 60, 62, 61, 63, 62, 64, 63, 64.2],
  storage: [47.8, 47.9, 48, 48.1, 48.2, 48.3, 48.3, 48.4, 48.5, 48.6, 48.7]
};

const canvas = document.getElementById('trend-chart');
if (window.Chart && canvas) {
  const context = canvas.getContext('2d');
  const gradient = context.createLinearGradient(0, 0, 0, 230);
  gradient.addColorStop(0, 'rgba(66,120,232,.13)');
  gradient.addColorStop(1, 'rgba(66,120,232,0)');
  new Chart(context, {
    type: 'line',
    data: {
      labels: sampleHistory.labels,
      datasets: [
        { label: 'CPU', data: sampleHistory.cpu, borderColor: '#4278e8', backgroundColor: gradient, fill: true, tension: .36, borderWidth: 2, pointRadius: 0, pointHoverRadius: 4 },
        { label: 'Memory', data: sampleHistory.memory, borderColor: '#8063df', backgroundColor: 'transparent', tension: .36, borderWidth: 2, pointRadius: 0, pointHoverRadius: 4 },
        { label: 'Storage', data: sampleHistory.storage, borderColor: '#18a889', backgroundColor: 'transparent', tension: .36, borderWidth: 2, pointRadius: 0, pointHoverRadius: 4 }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { intersect: false, mode: 'index' },
      plugins: { legend: { display: false }, tooltip: { backgroundColor: '#172334', padding: 10, titleFont: { family: 'DM Sans', size: 10 }, bodyFont: { family: 'DM Mono', size: 10 }, callbacks: { label: item => ` ${item.dataset.label}: ${item.formattedValue}%` } } },
      scales: {
        x: { grid: { display: false }, border: { display: false }, ticks: { color: '#9aa4b2', maxTicksLimit: 6, font: { family: 'DM Mono', size: 9 }, maxRotation: 0 } },
        y: { min: 0, max: 100, border: { display: false, dash: [3, 4] }, grid: { color: '#edf0f4', drawTicks: false }, ticks: { stepSize: 25, padding: 10, color: '#9aa4b2', font: { family: 'DM Mono', size: 9 }, callback: value => `${value}%` } }
      }
    }
  });
}

document.getElementById('refresh-button')?.addEventListener('click', event => {
  const button = event.currentTarget;
  const symbol = button.querySelector('.refresh-symbol');
  symbol.style.transform = 'rotate(180deg)';
  window.setTimeout(() => { symbol.style.transform = ''; }, 280);
});
