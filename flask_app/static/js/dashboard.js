/* dashboard.js — AHLC Dashboard interactivity */

// ================================================================
// Drop-zone helper (reused on dashboard and analyze pages)
// ================================================================
function setupDropZone(zoneId, inputId, nameId, selectedId) {
  const zone     = document.getElementById(zoneId);
  const input    = document.getElementById(inputId);
  const nameEl   = document.getElementById(nameId);
  const selEl    = document.getElementById(selectedId);
  if (!zone || !input) return;

  function showFile(file) {
    if (!file) return;
    if (nameEl)  nameEl.textContent = file.name;
    if (selEl)   selEl.style.display = 'flex';
    zone.classList.add('drag-over');
    // Enable compress button if present
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
      alert('Video file exceeds 50 MB demo limit (' + (file.size / (1024 * 1024)).toFixed(1) + ' MB). Please choose a video under 50 MB.');
      zone.classList.remove('drag-over');
      return;
    }
    // Inject the dropped file into the input
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
  const form     = document.getElementById('compress-form');
  const progBar  = document.getElementById('progress-bar');
  const fill     = document.getElementById('progress-fill');
  const steps    = ['step-upload','step-analyze','step-compress','step-verify'];
  if (!form) return;

  form.addEventListener('submit', (e) => {
    const input = document.getElementById('file-input');
    if (!input || !input.files[0]) { e.preventDefault(); return; }

    // Validate extension
    const ext = input.files[0].name.split('.').pop().toLowerCase();
    if (!['bmp','png','wav','flac','avi','mp4'].includes(ext)) {
      e.preventDefault();
      alert('Only .bmp, .png, .wav, .flac, .avi, and .mp4 files are supported.');
      return;
    }
    if (['avi', 'mp4'].includes(ext) && input.files[0].size > 50 * 1024 * 1024) {
      e.preventDefault();
      alert('Video file exceeds 50 MB demo limit (' + (input.files[0].size / (1024 * 1024)).toFixed(1) + ' MB). Please choose a video under 50 MB.');
      return;
    }

    // Show progress UI
    const btn = document.getElementById('compress-btn');
    if (btn) { btn.disabled = true; btn.textContent = 'Processing…'; }
    if (progBar) progBar.style.display = 'block';

    // Animate progress steps: Analyzing -> Processing -> Verifying -> Complete
    const steps = ['step-upload', 'step-analyze', 'step-process', 'step-verify', 'step-complete'];
    let idx = 0;
    const durations = [200, 350, 600, 400, 300]; // ms per step
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
    // Form submits normally — server handles it
  });
}

// ================================================================
// Auto-dismiss flash messages after 6 s
// ================================================================
function autoDismissFlash() {
  const msgs = document.querySelectorAll('.flash');
  msgs.forEach(el => setTimeout(() => {
    el.style.transition = 'opacity 0.4s';
    el.style.opacity = '0';
    setTimeout(() => el.remove(), 400);
  }, 6000));
}

// ================================================================
// Animate stat numbers on load (count-up)
// ================================================================
function animateCounters() {
  document.querySelectorAll('.stat-value').forEach(el => {
    const text = el.textContent.trim();
    const num  = parseFloat(text.replace(/[^0-9.]/g, ''));
    if (isNaN(num) || num === 0) return;
    const suffix = text.replace(/[\d.]/g, '');
    let start = 0;
    const duration = 900;
    const step = 16;
    const steps = duration / step;
    let i = 0;
    const timer = setInterval(() => {
      i++;
      const progress = i / steps;
      const eased = 1 - Math.pow(1 - progress, 3); // ease-out cubic
      const val = num * eased;
      el.textContent = (Number.isInteger(num) ? Math.round(val) : val.toFixed(2)) + suffix;
      if (i >= steps) { clearInterval(timer); el.textContent = text; }
    }, step);
  });
}

// ================================================================
// Init
// ================================================================
document.addEventListener('DOMContentLoaded', () => {
  // Dashboard drop zone
  setupDropZone('drop-zone', 'file-input', 'file-name', 'file-selected');
  setupCompressForm();
  autoDismissFlash();
  animateCounters();
});
