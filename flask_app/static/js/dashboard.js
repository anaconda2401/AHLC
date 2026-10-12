/* dashboard.js — AHLC Dashboard interactivity */

// ================================================================
// Theme: light / dark, persisted in localStorage
// ================================================================
(function () {
  const saved = localStorage.getItem('ahlc-theme') || 'dark';
  document.documentElement.setAttribute('data-theme', saved);
})();

function initThemeToggle() {
  const btn = document.getElementById('theme-toggle');
  if (!btn) return;

  function applyTheme(theme) {
    document.documentElement.setAttribute('data-theme', theme);
    localStorage.setItem('ahlc-theme', theme);
    const icon = btn.querySelector('.theme-toggle-icon');
    const label = btn.querySelector('.theme-toggle-label');
    if (theme === 'dark') {
      if (icon)  icon.textContent = '☀';
      if (label) label.textContent = 'Light mode';
    } else {
      if (icon)  icon.textContent = '☾';
      if (label) label.textContent = 'Dark mode';
    }
    // Update chart colors if charts exist
    updateChartTheme(theme);
  }

  btn.addEventListener('click', () => {
    const current = document.documentElement.getAttribute('data-theme') || 'dark';
    applyTheme(current === 'dark' ? 'light' : 'dark');
  });

  // Set initial icon state
  const current = localStorage.getItem('ahlc-theme') || 'dark';
  applyTheme(current);
}

// Update Chart.js defaults to match current theme
function updateChartTheme(theme) {
  if (typeof Chart === 'undefined') return;
  const isDark = theme === 'dark';
  const textColor  = isDark ? '#7a8394' : '#6b7385';
  const gridColor  = isDark ? 'rgba(45,50,64,0.6)' : 'rgba(200,205,215,0.5)';
  Chart.defaults.color = textColor;
  Chart.defaults.scale = Chart.defaults.scale || {};
  // Update all registered charts
  if (Chart.instances) {
    Object.values(Chart.instances).forEach(chart => {
      if (chart.options && chart.options.scales) {
        Object.values(chart.options.scales).forEach(scale => {
          if (scale.grid) scale.grid.color = gridColor;
          if (scale.ticks) scale.ticks.color = textColor;
        });
      }
      chart.update('none');
    });
  }
}

// ================================================================
// Drop-zone helper (reused on dashboard and analyze pages)
// ================================================================
function setupDropZone(zoneId, inputId, nameId, selectedId) {
  const zone  = document.getElementById(zoneId);
  const input = document.getElementById(inputId);
  const nameEl = document.getElementById(nameId);
  const selEl  = document.getElementById(selectedId);
  if (!zone || !input) return;

  function showFile(file) {
    if (!file) return;
    if (nameEl) nameEl.textContent = `${file.name} (${formatBytes(file.size)})`;
    if (selEl)  selEl.style.display = 'flex';
    zone.classList.add('drag-over');
    const btn = document.getElementById('compress-btn');
    if (btn) btn.disabled = false;
  }

  input.addEventListener('change', () => { if (input.files[0]) showFile(input.files[0]); });

  zone.addEventListener('click', (e) => {
    if (e.target.tagName === 'BUTTON') return;
    input.click();
  });

  zone.addEventListener('dragover', (e) => {
    e.preventDefault();
    zone.classList.add('drag-over');
  });
  zone.addEventListener('dragleave', () => zone.classList.remove('drag-over'));
  zone.addEventListener('drop', (e) => {
    e.preventDefault();
    const file = e.dataTransfer.files[0];
    if (!file) return;
    const ext = file.name.split('.').pop().toLowerCase();
    if (!['bmp','png','wav','flac','avi','mp4'].includes(ext)) {
      alert('Unsupported format. Please upload a .bmp, .png, .wav, .flac, .avi, or .mp4 file.');
      zone.classList.remove('drag-over');
      return;
    }
    if (['avi', 'mp4'].includes(ext) && file.size > 50 * 1024 * 1024) {
      alert('Video file exceeds the 50 MB demo limit (' + (file.size / (1024*1024)).toFixed(1) + ' MB). Please choose a video under 50 MB.');
      zone.classList.remove('drag-over');
      return;
    }
    const dt = new DataTransfer();
    dt.items.add(file);
    input.files = dt.files;
    showFile(file);
  });
}

// ================================================================
// Dashboard upload form: progress animation
// ================================================================
function setupCompressForm() {
  const form    = document.getElementById('compress-form');
  const progBar = document.getElementById('progress-bar');
  const fill    = document.getElementById('progress-fill');
  if (!form) return;

  form.addEventListener('submit', (e) => {
    const input = document.getElementById('file-input');
    if (!input || !input.files[0]) { e.preventDefault(); return; }

    const ext = input.files[0].name.split('.').pop().toLowerCase();
    if (!['bmp','png','wav','flac','avi','mp4'].includes(ext)) {
      e.preventDefault();
      alert('Only .bmp, .png, .wav, .flac, .avi, and .mp4 files are supported.');
      return;
    }
    if (['avi', 'mp4'].includes(ext) && input.files[0].size > 50 * 1024 * 1024) {
      e.preventDefault();
      alert('Video file exceeds the 50 MB demo limit (' + (input.files[0].size / (1024*1024)).toFixed(1) + ' MB). Please choose a video under 50 MB.');
      return;
    }

    const btn = document.getElementById('compress-btn');
    if (btn) { btn.disabled = true; btn.textContent = 'Processing…'; }
    if (progBar) progBar.style.display = 'block';

    const steps = ['step-upload', 'step-analyze', 'step-process', 'step-verify', 'step-complete'];
    const durations = [200, 350, 700, 450, 300];
    let idx = 0;
    function nextStep() {
      if (idx < steps.length) {
        if (idx > 0) {
          const prevEl = document.getElementById(steps[idx - 1]);
          if (prevEl) { prevEl.classList.remove('active'); prevEl.classList.add('done'); }
        }
        const el = document.getElementById(steps[idx]);
        if (el) el.classList.add('active');
        const pct = ((idx + 1) / steps.length) * 100;
        if (fill) fill.style.width = pct + '%';
        idx++;
        setTimeout(nextStep, durations[idx - 1] || 400);
      }
    }
    nextStep();
  });
}

// ================================================================
// Auto-dismiss flash messages after 6 s
// ================================================================
function autoDismissFlash() {
  document.querySelectorAll('.flash').forEach(el => setTimeout(() => {
    el.style.transition = 'opacity 0.4s';
    el.style.opacity = '0';
    setTimeout(() => el.remove(), 400);
  }, 6000));
}

// ================================================================
// Utility: format bytes
// ================================================================
function formatBytes(n) {
  if (n === null || n === undefined) return 'N/A';
  const units = ['B','KB','MB','GB'];
  let i = 0;
  while (n >= 1024 && i < units.length - 1) { n /= 1024; i++; }
  return (i === 0 ? n.toFixed(0) : n.toFixed(1)) + ' ' + units[i];
}

// ================================================================
// Init
// ================================================================
document.addEventListener('DOMContentLoaded', () => {
  initThemeToggle();
  setupDropZone('drop-zone', 'file-input', 'file-name', 'file-selected');
  setupCompressForm();
  autoDismissFlash();
  // Apply chart theme after load
  const current = localStorage.getItem('ahlc-theme') || 'dark';
  updateChartTheme(current);
});
