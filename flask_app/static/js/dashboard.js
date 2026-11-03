/* dashboard.js — AHLC Dashboard Interactivity & UX Polish */

// ================================================================
// Theme: light / dark, persisted in localStorage
// ================================================================
(function () {
  let saved = localStorage.getItem('ahlc-theme');
  if (saved === 'light' && !localStorage.getItem('ahlc-gradient-v2')) {
    saved = 'dark';
    localStorage.setItem('ahlc-theme', 'dark');
    localStorage.setItem('ahlc-gradient-v2', '1');
  }
  document.documentElement.setAttribute('data-theme', saved || 'dark');
})();

function initThemeToggle() {
  const btn = document.getElementById('theme-toggle');
  if (!btn) return;

  function applyTheme(theme) {
    document.documentElement.setAttribute('data-theme', theme);
    localStorage.setItem('ahlc-theme', theme);
    localStorage.setItem('ahlc-gradient-v2', '1');
    const icon = btn.querySelector('.theme-toggle-icon');
    const label = btn.querySelector('.theme-toggle-label');
    if (theme === 'dark') {
      if (icon)  icon.textContent = '◐';
      if (label) label.textContent = 'Soft Theme';
    } else {
      if (icon)  icon.textContent = '◑';
      if (label) label.textContent = 'Deep Theme';
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
  const textColor = isDark ? '#708e9e' : '#476675';
  const gridColor = isDark ? 'rgba(48, 104, 128, 0.35)' : 'rgba(35, 88, 112, 0.22)';
  Chart.defaults.color = textColor;
  Chart.defaults.scale = Chart.defaults.scale || {};

  // Update all registered chart instances
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
function setupDropZone(zoneId, inputId, nameId, selectedId, submitBtnId) {
  const zone  = document.getElementById(zoneId);
  const input = document.getElementById(inputId);
  const nameEl = document.getElementById(nameId);
  const selEl  = document.getElementById(selectedId);
  const submitBtn = submitBtnId ? document.getElementById(submitBtnId) : document.getElementById('compress-btn');
  if (!zone || !input) return;

  function showFile(file) {
    if (!file) return;
    if (nameEl) nameEl.textContent = `${file.name} (${formatBytes(file.size)})`;
    if (selEl) {
      selEl.style.display = 'flex';
    }
    zone.classList.remove('drag-over');
    if (submitBtn) {
      submitBtn.disabled = false;
    }
  }

  input.addEventListener('change', () => {
    if (input.files && input.files[0]) {
      showFile(input.files[0]);
    }
  });

  zone.addEventListener('click', (e) => {
    if (e.target.tagName === 'BUTTON' || e.target.classList.contains('upload-browse')) return;
    input.click();
  });

  let dragCounter = 0;

  zone.addEventListener('dragenter', (e) => {
    e.preventDefault();
    dragCounter++;
    zone.classList.add('drag-over');
  });

  zone.addEventListener('dragover', (e) => {
    e.preventDefault();
  });

  zone.addEventListener('dragleave', () => {
    dragCounter--;
    if (dragCounter <= 0) {
      dragCounter = 0;
      zone.classList.remove('drag-over');
    }
  });

  zone.addEventListener('drop', (e) => {
    e.preventDefault();
    dragCounter = 0;
    zone.classList.remove('drag-over');

    const file = e.dataTransfer.files[0];
    if (!file) return;

    const ext = file.name.split('.').pop().toLowerCase();
    if (!['bmp','png','wav','flac','avi','mp4'].includes(ext)) {
      alert('Unsupported format. Please upload a .bmp, .png, .wav, .flac, .avi, or .mp4 file.');
      return;
    }
    if (['avi', 'mp4'].includes(ext) && file.size > 50 * 1024 * 1024) {
      alert('Video file exceeds the 50 MB demo limit (' + (file.size / (1024*1024)).toFixed(1) + ' MB). Please choose a video under 50 MB.');
      return;
    }

    const dt = new DataTransfer();
    dt.items.add(file);
    input.files = dt.files;
    showFile(file);
  });
}

// ================================================================
// Dashboard upload form: progress animation & smooth submission
// ================================================================
function setupCompressForm() {
  const form    = document.getElementById('compress-form');
  const progBar = document.getElementById('progress-bar');
  const fill    = document.getElementById('progress-fill');
  if (!form) return;

  form.addEventListener('submit', (e) => {
    const input = document.getElementById('file-input');
    if (!input || !input.files[0]) {
      e.preventDefault();
      alert('Please select a file to compress.');
      return;
    }

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
    if (btn) {
      btn.disabled = true;
      btn.innerHTML = '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="animation:spin 1s linear infinite"><circle cx="12" cy="12" r="10" stroke-dasharray="32" stroke-dashoffset="16"/></svg> Processing…';
    }
    if (progBar) progBar.style.display = 'block';

    const steps = ['step-upload', 'step-analyze', 'step-process', 'step-verify', 'step-complete'];
    const durations = [220, 380, 750, 480, 320];
    let idx = 0;
    function nextStep() {
      if (idx < steps.length) {
        if (idx > 0) {
          const prevEl = document.getElementById(steps[idx - 1]);
          if (prevEl) {
            prevEl.classList.remove('active');
            prevEl.classList.add('done');
          }
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
// Analyze form smooth submission
// ================================================================
function setupAnalyzeForm() {
  const form = document.querySelector('form[action*="analyze"]');
  if (!form) return;

  form.addEventListener('submit', (e) => {
    const input = document.getElementById('file-input-analyze');
    if (!input || !input.files[0]) {
      e.preventDefault();
      alert('Please select a file to analyze.');
      return;
    }
    const btn = form.querySelector('button[type="submit"]');
    if (btn) {
      btn.disabled = true;
      btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="animation:spin 1s linear infinite"><circle cx="12" cy="12" r="10" stroke-dasharray="32" stroke-dashoffset="16"/></svg> Analyzing…';
    }
  });
}

// Keyframe spin injection for buttons
(function injectKeyframes() {
  const style = document.createElement('style');
  style.textContent = '@keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }';
  document.head.appendChild(style);
})();

// ================================================================
// Auto-dismiss flash messages smoothly
// ================================================================
function autoDismissFlash() {
  document.querySelectorAll('.flash').forEach(el => {
    setTimeout(() => {
      el.style.transition = 'opacity 0.35s ease, transform 0.35s ease, max-height 0.3s ease, margin 0.3s ease, padding 0.3s ease';
      el.style.opacity = '0';
      el.style.transform = 'translateY(-6px)';
      setTimeout(() => {
        el.style.maxHeight = '0px';
        el.style.margin = '0px';
        el.style.padding = '0px';
        setTimeout(() => el.remove(), 300);
      }, 350);
    }, 6000);
  });
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
  // Setup dashboard dropzone
  setupDropZone('drop-zone', 'file-input', 'file-name', 'file-selected', 'compress-btn');
  // Setup analyze page dropzone
  setupDropZone('drop-zone-analyze', 'file-input-analyze', 'file-name-analyze', 'file-selected-analyze', 'analyze-btn');
  setupCompressForm();
  setupAnalyzeForm();
  autoDismissFlash();

  // Apply chart theme after load
  const current = localStorage.getItem('ahlc-theme') || 'dark';
  updateChartTheme(current);
});
