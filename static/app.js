/**
 * MAX30102 + ESP32-S3 High-Precision PPG Acquisition Dashboard
 * Integrated with IBM Plex Design System & Live Hardware Telemetry
 */

// DOM Elements
const measureBtn = document.getElementById('measureBtn');
const measureBtnText = document.getElementById('measureBtnText');
const measureBtnIcon = document.getElementById('measureBtnIcon');
const recordBtn = document.getElementById('recordBtn');
const recordBtnText = document.getElementById('recordBtnText');
const clearPlotBtn = document.getElementById('clearPlotBtn');
const portSelect = document.getElementById('portSelect');
const refreshPortsBtn = document.getElementById('refreshPortsBtn');
const statusPill = document.getElementById('statusPill');
const statusText = document.getElementById('statusText');

// Vitals & Metrics Elements
const bpmValue = document.getElementById('bpmValue');
const bpmStatus = document.getElementById('bpmStatus');
const fingerBadge = document.getElementById('fingerBadge');
const fingerBadgeText = document.getElementById('fingerBadgeText');
const sampleRateVal = document.getElementById('sampleRateVal');
const sampleCountVal = document.getElementById('sampleCountVal');
const rawIrVal = document.getElementById('rawIrVal');
const rawRedVal = document.getElementById('rawRedVal');
const recordingIndicator = document.getElementById('recordingIndicator');
const recordingCountdown = document.getElementById('recordingCountdown');
const recordingSessionLabel = document.getElementById('recordingSessionLabel');
const recordingProgressTrack = document.getElementById('recordingProgressTrack');
const recordingProgressFill = document.getElementById('recordingProgressFill');
const timerButtons = document.querySelectorAll('.timer-btn');
const timeScaleLabel = document.getElementById('timeScaleLabel');
const liveDot = document.getElementById('liveDot');
const liveStatusText = document.getElementById('liveStatusText');
const modeDescription = document.getElementById('modeDescription');

// Canvas Elements
const canvas = document.getElementById('ppgCanvas');
const ctx = canvas.getContext('2d');
const canvasOverlay = document.getElementById('canvasOverlay');

// Modal Elements
const hardwareAlertModal = document.getElementById('hardwareAlertModal');
const alertModalTitle = document.getElementById('alertModalTitle');
const alertModalMessage = document.getElementById('alertModalMessage');
const modalDismissBtn = document.getElementById('modalDismissBtn');
const modalCloseBtn = document.getElementById('modalCloseBtn');
const openHelpBtn = document.getElementById('openHelpBtn');
const toastContainer = document.getElementById('toastContainer');

// State Variables
let isMeasuring = false;
let isRecording = false;
let selectedDurationSec = 0; // 0 = manual, 180 = 3m, 300 = 5m, 480 = 8m, 600 = 10m
let recordingTimerInterval = null;
let recordingStartTime = 0;
let recordingTargetDuration = 0;
let ws = null;
let viewMode = 'ac'; // 'ac' or 'raw'
let timeWindowSec = 8; // 4s, 8s, 12s
const NOMINAL_HZ = 100;
let maxBufferSize = NOMINAL_HZ * timeWindowSec;

// Data Buffers
let irSamples = [];
let redSamples = [];
let timestamps = [];
let totalSamplesReceived = 0;

// High-pass / AC filter moving averages
let irMovingAvg = 0;
let redMovingAvg = 0;
const ALPHA = 0.95; // DC tracking filter coefficient

// ==============================================================================
// CANVAS INITIALIZATION & RESIZING
// ==============================================================================
function resizeCanvas() {
  if (!canvas) return;
  const bounds = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  if (canvas.width !== bounds.width * dpr || canvas.height !== bounds.height * dpr) {
    canvas.width = bounds.width * dpr;
    canvas.height = bounds.height * dpr;
  }
}

window.addEventListener('resize', resizeCanvas);
resizeCanvas();

// ==============================================================================
// TOAST NOTIFICATIONS
// ==============================================================================
function showToast(message, type = 'info') {
  if (!toastContainer) return;
  const toast = document.createElement('div');
  toast.className = `toast ${type === 'error' ? 'toast-error' : type === 'success' ? 'toast-success' : ''}`;
  
  let icon = 'ℹ️';
  if (type === 'error') icon = '⚠️';
  if (type === 'success') icon = '✅';
  
  toast.innerHTML = `<span>${icon}</span><span>${message}</span>`;
  toastContainer.appendChild(toast);
  
  setTimeout(() => {
    toast.style.transition = 'opacity 0.4s ease, transform 0.4s ease';
    toast.style.opacity = '0';
    toast.style.transform = 'translateY(10px)';
    setTimeout(() => toast.remove(), 400);
  }, 4000);
}

// ==============================================================================
// HARDWARE CHECKLIST MODAL
// ==============================================================================
function showHardwareAlertModal(message, errorCode = 'HARDWARE_NOT_FOUND') {
  if (errorCode === 'SENSOR_COMMUNICATION_FAILED') {
    alertModalTitle.innerText = 'MAX30102 Sensor Error';
  } else if (errorCode === 'PORT_OPEN_FAILED') {
    alertModalTitle.innerText = 'Serial Port In Use';
  } else {
    alertModalTitle.innerText = 'Check your sensor connection';
  }
  
  alertModalMessage.innerText = message || 'Connect the MAX30102 to the ESP32-S3 using the pins below. Verify the supply voltage supported by your sensor breakout board.';
  hardwareAlertModal.classList.add('active');
  
  statusPill.className = 'status-pill';
  statusText.innerText = 'Hardware Missing';
}

function hideHardwareAlertModal() {
  hardwareAlertModal.classList.remove('active');
}

if (modalDismissBtn) modalDismissBtn.addEventListener('click', hideHardwareAlertModal);
if (modalCloseBtn) modalCloseBtn.addEventListener('click', hideHardwareAlertModal);
if (openHelpBtn) openHelpBtn.addEventListener('click', () => {
  alertModalTitle.innerText = 'Check your sensor connection';
  alertModalMessage.innerText = 'Connect the MAX30102 to the ESP32-S3 using the pins below. Verify the supply voltage supported by your sensor breakout board.';
  hardwareAlertModal.classList.add('active');
});

hardwareAlertModal.addEventListener('click', (e) => {
  if (e.target === hardwareAlertModal) {
    hideHardwareAlertModal();
  }
});

// ==============================================================================
// PORT MANAGEMENT
// ==============================================================================
async function loadAvailablePorts() {
  try {
    const res = await fetch('/api/ports');
    const data = await res.json();
    
    const currentVal = portSelect.value;
    portSelect.innerHTML = '<option value="auto">Auto-Detect ESP32</option>';
    
    if (data.ports && data.ports.length > 0) {
      data.ports.forEach((p) => {
        const opt = document.createElement('option');
        opt.value = p.device;
        let label = `${p.device}`;
        if (p.is_esp32_candidate) {
          label = `USB · ${p.device} (ESP32)`;
        } else if (p.is_bluetooth) {
          label = `BT · ${p.device}`;
        } else {
          label = `USB · ${p.device}`;
        }
        opt.textContent = label;
        portSelect.appendChild(opt);
      });
    }
    
    if (currentVal && Array.from(portSelect.options).some(o => o.value === currentVal)) {
      portSelect.value = currentVal;
    } else if (Array.from(portSelect.options).some(o => o.value === 'COM9')) {
      portSelect.value = 'COM9';
    }
  } catch (err) {
    console.error('Failed to load ports:', err);
  }
}

refreshPortsBtn.addEventListener('click', async () => {
  showToast('Refreshing available serial ports...', 'info');
  await loadAvailablePorts();
});

// ==============================================================================
// MEASUREMENT CONTROLS
// ==============================================================================
async function startMeasurement() {
  measureBtn.disabled = true;
  measureBtnText.innerText = 'Connecting...';
  statusPill.className = 'status-pill';
  statusText.innerText = 'Connecting...';
  
  const selectedPort = portSelect.value === 'auto' ? null : portSelect.value;
  
  try {
    const response = await fetch('/api/measure/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ port: selectedPort })
    });
    
    const result = await response.json();
    
    if (!response.ok || !result.success) {
      console.warn('Measurement start failed:', result);
      showHardwareAlertModal(result.message, result.error_code);
      showToast(result.message || 'Hardware connection failed!', 'error');
      
      measureBtn.disabled = false;
      measureBtn.classList.remove('active');
      measureBtnText.innerText = 'Measure PPG';
      measureBtnIcon.innerHTML = '<path d="M12 2v10" /><path d="M18.4 5.6a9 9 0 1 1-12.8 0" />';
      return;
    }
    
    // SUCCESSFUL CONNECTION
    isMeasuring = true;
    measureBtn.disabled = false;
    measureBtn.classList.add('active');
    measureBtnText.innerText = 'Stop measurement';
    measureBtnIcon.innerHTML = '<rect x="6" y="6" width="12" height="12"/>';
    
    statusPill.className = 'status-pill connected';
    statusText.innerText = `Measuring (${result.port || 'ESP32'})`;
    
    liveDot.classList.add('on');
    liveStatusText.innerText = 'Live telemetry stream (100 Hz)';
    
    recordBtn.disabled = false;
    canvasOverlay.style.display = 'none';
    
    showToast(result.message, 'success');
    connectWebSocket();
    
  } catch (err) {
    console.error('Error starting measurement:', err);
    showHardwareAlertModal('Connection error: Could not reach the backend server.', 'SERVER_ERROR');
    measureBtn.disabled = false;
    measureBtnText.innerText = 'Measure PPG';
  }
}

async function stopMeasurement() {
  measureBtn.disabled = true;
  measureBtnText.innerText = 'Stopping...';
  
  try {
    await fetch('/api/measure/stop', { method: 'POST' });
  } catch (err) {
    console.error('Error stopping measurement:', err);
  }
  
  if (ws) {
    ws.close();
    ws = null;
  }
  
  isMeasuring = false;
  measureBtn.disabled = false;
  measureBtn.classList.remove('active');
  measureBtnText.innerText = 'Measure PPG';
  measureBtnIcon.innerHTML = '<path d="M12 2v10" /><path d="M18.4 5.6a9 9 0 1 1-12.8 0" />';
  
  statusPill.className = 'status-pill';
  statusText.innerText = 'Disconnected';
  
  liveDot.classList.remove('on');
  liveStatusText.innerText = 'No active stream';
  
  recordBtn.disabled = true;
  if (isRecording) {
    stopRecording();
  }
  if (recordingTimerInterval) {
    clearInterval(recordingTimerInterval);
    recordingTimerInterval = null;
  }
  timerButtons.forEach(b => b.disabled = false);
  
  canvasOverlay.style.display = 'grid';
  showToast('Acquisition stopped.', 'info');
}

measureBtn.addEventListener('click', () => {
  if (isMeasuring) {
    stopMeasurement();
  } else {
    startMeasurement();
  }
});

// ==============================================================================
// WEBSOCKET TELEMETRY STREAM
// ==============================================================================
function connectWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${protocol}//${window.location.host}/ws`;
  
  ws = new WebSocket(wsUrl);
  
  ws.onopen = () => {
    console.log('[WS] Telemetry stream connected.');
  };
  
  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);
      if (msg.type === 'recording_stopped') {
        if (isRecording) {
          playTimerDoneChime();
          stopRecording(true);
        }
        return;
      }
      handleNewSample(msg);
    } catch (e) {
      console.error('WS Parse Error:', e);
    }
  };
  
  ws.onclose = () => {
    console.log('[WS] Telemetry stream disconnected.');
    if (isMeasuring) {
      statusPill.className = 'status-pill';
      statusText.innerText = 'Disconnected';
      showToast('Hardware stream disconnected.', 'error');
      stopMeasurement();
    }
  };
  
  ws.onerror = (err) => {
    console.error('[WS] Telemetry stream error:', err);
  };
}

// ==============================================================================
// SAMPLE PROCESSING
// ==============================================================================
function handleNewSample(sample) {
  totalSamplesReceived++;
  
  const ir = sample.ir;
  const red = sample.red;
  const finger = sample.finger;
  
  // Moving Average for AC filtering (Detrending)
  if (irMovingAvg === 0) {
    irMovingAvg = ir;
    redMovingAvg = red;
  } else {
    irMovingAvg = ALPHA * irMovingAvg + (1 - ALPHA) * ir;
    redMovingAvg = ALPHA * redMovingAvg + (1 - ALPHA) * red;
  }
  
  const irAc = -(ir - irMovingAvg);
  const redAc = -(red - redMovingAvg);
  
  irSamples.push({ raw: ir, ac: irAc });
  redSamples.push({ raw: red, ac: redAc });
  timestamps.push(sample.timestamp_us);
  
  while (irSamples.length > maxBufferSize) {
    irSamples.shift();
    redSamples.shift();
    timestamps.shift();
  }
  
  // Update Numeric Labels
  rawIrVal.innerText = ir.toLocaleString();
  rawRedVal.innerText = red.toLocaleString();
  sampleRateVal.innerText = (sample.rate_hz || 100.0).toFixed(1);
  sampleCountVal.innerText = `${totalSamplesReceived.toLocaleString()} samples`;
  
  // Finger Presence
  if (finger === 1) {
    fingerBadge.className = 'contact-badge detected';
    fingerBadgeText.innerText = 'Finger detected';
  } else {
    fingerBadge.className = 'contact-badge';
    fingerBadgeText.innerText = 'No contact';
  }
  
  // BPM & Heartbeat Animation
  if (finger === 1 && sample.bpm > 0) {
    bpmValue.innerText = Math.round(sample.bpm);
    bpmStatus.innerText = 'Pulse detected';
    bpmValue.classList.add('beating');
  } else {
    bpmValue.innerText = '— —';
    bpmStatus.innerText = finger === 1 ? 'Detecting pulse...' : 'No data';
    bpmValue.classList.remove('beating');
  }
}

// ==============================================================================
// RECORDING CONTROLS & TIMERS
// ==============================================================================
function formatTime(seconds) {
  const s = Math.max(0, Math.floor(seconds));
  const mins = Math.floor(s / 60);
  const secs = s % 60;
  return `${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
}

function playTimerDoneChime() {
  try {
    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!AudioCtx) return;
    const audioCtx = new AudioCtx();
    const now = audioCtx.currentTime;
    
    const playTone = (freq, start, duration) => {
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(freq, start);
      gain.gain.setValueAtTime(0.0001, start);
      gain.gain.exponentialRampToValueAtTime(0.2, start + 0.03);
      gain.gain.exponentialRampToValueAtTime(0.0001, start + duration);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start(start);
      osc.stop(start + duration);
    };

    playTone(523.25, now, 0.22); // C5
    playTone(659.25, now + 0.16, 0.35); // E5
  } catch (e) {
    // AudioContext blocked or unavailable
  }
}

// Timer preset selection buttons (Manual, 3 min, 5 min, 8 min, 10 min)
timerButtons.forEach((btn) => {
  btn.addEventListener('click', () => {
    if (isRecording) return;
    timerButtons.forEach(b => b.classList.remove('selected'));
    btn.classList.add('selected');
    selectedDurationSec = parseInt(btn.getAttribute('data-sec'), 10) || 0;
    
    if (selectedDurationSec > 0) {
      const m = selectedDurationSec / 60;
      showToast(`Record timer set to ${m} min`, 'info');
    } else {
      showToast('Record mode set to manual continuous', 'info');
    }
  });
});

function updateRecordingCountdown() {
  if (!isRecording) return;
  const elapsedSec = Math.floor((Date.now() - recordingStartTime) / 1000);
  
  if (recordingTargetDuration > 0) {
    const remainingSec = Math.max(0, recordingTargetDuration - elapsedSec);
    const formattedRemaining = formatTime(remainingSec);
    
    recordBtnText.innerText = `Stop (${formattedRemaining})`;
    if (recordingCountdown) recordingCountdown.innerText = formattedRemaining;
    if (recordingSessionLabel) {
      const mins = recordingTargetDuration / 60;
      recordingSessionLabel.innerText = `REC · ${mins} MIN`;
    }
    
    if (recordingProgressTrack) recordingProgressTrack.style.display = 'block';
    if (recordingProgressFill) {
      const pct = Math.min(100, Math.max(0, (elapsedSec / recordingTargetDuration) * 100));
      recordingProgressFill.style.width = `${pct}%`;
    }
    
    // Auto-stop when timer hits 0
    if (remainingSec <= 0) {
      if (recordingTimerInterval) {
        clearInterval(recordingTimerInterval);
        recordingTimerInterval = null;
      }
      playTimerDoneChime();
      stopRecording(true);
    }
  } else {
    // Manual mode
    const formattedElapsed = formatTime(elapsedSec);
    recordBtnText.innerText = `Stop (${formattedElapsed})`;
    if (recordingCountdown) recordingCountdown.innerText = formattedElapsed;
    if (recordingSessionLabel) recordingSessionLabel.innerText = 'REC · MANUAL';
    if (recordingProgressTrack) recordingProgressTrack.style.display = 'none';
  }
}

async function startRecording() {
  try {
    const res = await fetch('/api/recording/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ duration: selectedDurationSec })
    });
    const data = await res.json();
    if (res.ok && data.success) {
      isRecording = true;
      recordBtn.classList.add('recording');
      recordingStartTime = Date.now();
      recordingTargetDuration = selectedDurationSec;
      
      // Lock timer buttons during active session
      timerButtons.forEach(b => b.disabled = true);
      
      recordingIndicator.style.display = 'flex';
      updateRecordingCountdown();
      
      if (recordingTimerInterval) clearInterval(recordingTimerInterval);
      recordingTimerInterval = setInterval(updateRecordingCountdown, 500);
      
      const durationMsg = selectedDurationSec > 0 
        ? ` (${selectedDurationSec / 60} min timer)` 
        : ' (continuous)';
      showToast(`Recording to ${data.filename}${durationMsg}`, 'success');
    } else {
      showToast(data.message || 'Failed to start recording', 'error');
    }
  } catch (err) {
    showToast('Recording request failed', 'error');
  }
}

async function stopRecording(fromTimer = false) {
  if (recordingTimerInterval) {
    clearInterval(recordingTimerInterval);
    recordingTimerInterval = null;
  }
  
  try {
    const res = await fetch('/api/recording/stop', { method: 'POST' });
    const data = await res.json();
    isRecording = false;
    recordBtn.classList.remove('recording');
    recordBtnText.innerText = 'Record CSV';
    recordingIndicator.style.display = 'none';
    
    // Unlock timer buttons
    timerButtons.forEach(b => b.disabled = false);
    
    if (data.filename) {
      if (fromTimer) {
        showToast(`⏱️ ${recordingTargetDuration / 60} min recording complete! Saved: ${data.filename}`, 'success');
      } else {
        showToast(`Saved: ${data.filename}`, 'success');
      }
      const a = document.createElement('a');
      a.href = `/api/recordings/${data.filename}`;
      a.download = data.filename;
      a.click();
    }
  } catch (err) {
    showToast('Failed to stop recording', 'error');
    timerButtons.forEach(b => b.disabled = false);
  }
}

recordBtn.addEventListener('click', () => {
  if (isRecording) {
    stopRecording();
  } else {
    startRecording();
  }
});

// Clear Plot
clearPlotBtn.addEventListener('click', () => {
  irSamples = [];
  redSamples = [];
  timestamps = [];
  totalSamplesReceived = 0;
  sampleCountVal.innerText = '0 samples';
  showToast('Waveform display buffer cleared.', 'info');
});

// View mode toggles
document.getElementById('viewAcBtn').addEventListener('click', function() {
  viewMode = 'ac';
  document.getElementById('viewAcBtn').classList.add('selected');
  document.getElementById('viewRawBtn').classList.remove('selected');
  modeDescription.innerText = 'Dynamic baseline removal enabled';
});

document.getElementById('viewRawBtn').addEventListener('click', function() {
  viewMode = 'raw';
  document.getElementById('viewRawBtn').classList.add('selected');
  document.getElementById('viewAcBtn').classList.remove('selected');
  modeDescription.innerText = 'Absolute ADC counts (raw)';
});

// Time window buttons
document.querySelectorAll('.window-btn').forEach(btn => {
  btn.addEventListener('click', function() {
    document.querySelectorAll('.window-btn').forEach(b => b.classList.remove('selected'));
    this.classList.add('selected');
    timeWindowSec = parseInt(this.dataset.sec, 10);
    maxBufferSize = NOMINAL_HZ * timeWindowSec;
    timeScaleLabel.innerText = `${timeWindowSec}.0 sec window`;
  });
});

// ==============================================================================
// 60 FPS CANVAS OSCILLOSCOPE
// ==============================================================================
function renderWaveforms() {
  requestAnimationFrame(renderWaveforms);
  
  if (!canvas || !ctx) return;
  
  const bounds = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  if (canvas.width !== bounds.width * dpr || canvas.height !== bounds.height * dpr) {
    canvas.width = bounds.width * dpr;
    canvas.height = bounds.height * dpr;
  }
  
  const w = bounds.width;
  const h = bounds.height;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  
  // Oscilloscope Grid Lines (from IBM Plex design)
  ctx.strokeStyle = "rgba(129, 160, 194, .09)";
  ctx.lineWidth = 1;
  
  for (let x = 0; x <= w; x += w / 12) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, h);
    ctx.stroke();
  }
  for (let y = 0; y <= h; y += h / 6) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }
  
  if (!isMeasuring || irSamples.length < 2) return;
  
  const count = irSamples.length;
  const irVals = irSamples.map(s => viewMode === 'ac' ? s.ac : s.raw);
  const redVals = redSamples.map(s => viewMode === 'ac' ? s.ac : s.raw);
  
  // Compute Min & Max for auto-scaling
  let minVal = Infinity;
  let maxVal = -Infinity;
  for (let i = 0; i < count; i++) {
    if (irVals[i] < minVal) minVal = irVals[i];
    if (irVals[i] > maxVal) maxVal = irVals[i];
    if (redVals[i] < minVal) minVal = redVals[i];
    if (redVals[i] > maxVal) maxVal = redVals[i];
  }
  
  const span = Math.max(maxVal - minVal, 10);
  const margin = span * 0.15;
  const plotMin = minVal - margin;
  const plotMax = maxVal + margin;
  const plotSpan = plotMax - plotMin;
  
  const valToY = (v) => {
    return h - ((v - plotMin) / plotSpan) * h;
  };
  
  const stepX = w / (maxBufferSize - 1);
  const startX = w - (count - 1) * stepX;
  
  // 1. Draw IR Channel Trace (#31e6d1 Teal)
  ctx.save();
  ctx.beginPath();
  for (let i = 0; i < count; i++) {
    const x = startX + i * stepX;
    const y = valToY(irVals[i]);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = '#31e6d1';
  ctx.lineWidth = 2.0;
  ctx.shadowColor = '#31e6d1';
  ctx.shadowBlur = 6;
  ctx.stroke();
  ctx.restore();
  
  // 2. Draw Red Channel Trace (#ff4d7d Pink)
  ctx.save();
  ctx.beginPath();
  for (let i = 0; i < count; i++) {
    const x = startX + i * stepX;
    const y = valToY(redVals[i]);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = '#ff4d7d';
  ctx.lineWidth = 1.8;
  ctx.shadowColor = '#ff4d7d';
  ctx.shadowBlur = 5;
  ctx.stroke();
  ctx.restore();
}

// Check initial status on load
async function checkInitialStatus() {
  try {
    const res = await fetch('/api/status');
    const data = await res.json();
    if (data.is_measuring) {
      isMeasuring = true;
      measureBtn.classList.add('active');
      measureBtnText.innerText = 'Stop measurement';
      measureBtnIcon.innerHTML = '<rect x="6" y="6" width="12" height="12"/>';
      statusPill.className = 'status-pill connected';
      statusText.innerText = `Measuring (${data.connected_port || 'ESP32'})`;
      liveDot.classList.add('on');
      liveStatusText.innerText = 'Live telemetry stream (100 Hz)';
      recordBtn.disabled = false;
      canvasOverlay.style.display = 'none';
      connectWebSocket();
    }
  } catch (err) {
    console.error('Failed to sync initial status:', err);
  }
}

// Initialize
renderWaveforms();
loadAvailablePorts();
checkInitialStatus();
