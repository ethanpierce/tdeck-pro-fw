// Native Web Serial only. No network requests, credential getters or storage.
const byId = (id) => document.getElementById(id);
const connectButton = byId("connect");
const disconnectButton = byId("disconnect");
const fields = byId("setup-fields");
const form = byId("setup-form");
const keyInput = byId("api-key");
const status = byId("setup-status");
let session = null;
let connecting = false;
let saving = false;
const supported = window.isSecureContext && "serial" in navigator;
const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const say = (message) => { status.textContent = message; };

function updateControls() {
  connectButton.disabled = !supported || connecting || !!session;
  disconnectButton.disabled = !session || saving || connecting;
  fields.disabled = !session?.ready || saving;
}

function cancelWait(s) {
  if (!s.pending) return;
  const pending = s.pending;
  s.pending = null;
  clearTimeout(pending.timer);
  pending.reject(new Error("Device operation did not complete"));
}

function expect(s, marker, timeout = 8000) {
  if (s.pending) throw new Error("Device operation already pending");
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      if (s.pending?.marker !== marker) return;
      s.pending = null;
      reject(new Error("Device confirmation timed out"));
    }, timeout);
    s.pending = { marker, timer, resolve, reject };
  });
}

function onLine(s, line) {
  // Accept exact, known firmware markers. Never display raw device output: a
  // different firmware could echo a command, including its bearer credential.
  const value = line.trim();
  if (value.startsWith("ai | ai setup | ai clear |") && value.includes("ai endpoint <https-base>")) s.ready = true;
  const pending = s.pending;
  if (!pending) return;
  if ((pending.marker === "ready" && s.ready) || value === pending.marker) {
    s.pending = null;
    clearTimeout(pending.timer);
    pending.resolve();
  } else if (value.startsWith("AI setup not saved;") || value.startsWith("AI setup busy;")) cancelWait(s);
}

async function readDevice(s) {
  const decoder = new TextDecoder();
  let buffer = "";
  try {
    while (!s.closed) {
      const { value, done } = await s.reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      let newline;
      while ((newline = buffer.indexOf("\n")) !== -1) {
        onLine(s, buffer.slice(0, newline));
        buffer = buffer.slice(newline + 1);
      }
      if (buffer.length > 4096) buffer = "";
    }
  } catch {
    // USB exceptions can include device data; deliberately keep them private.
  } finally {
    s.reader.releaseLock();
    cancelWait(s);
    if (!s.closed) setTimeout(() => {
      closeDevice(s).then(() => say("Device disconnected. Reconnect to try again."));
    }, 0);
  }
}

async function write(s, command) {
  const bytes = new TextEncoder().encode(command + "\n");
  try { await s.writer.write(bytes); } finally { bytes.fill(0); }
}

async function saveField(s, command, marker) {
  const confirmation = expect(s, marker);
  void confirmation.catch(() => {}); // A USB write can outlast the marker timeout.
  try {
    await write(s, command);
    await confirmation;
  } catch {
    cancelWait(s);
    // Consume the pending rejection even when the write itself failed.
    await confirmation.catch(() => {});
    throw new Error("Setup did not complete");
  }
}

async function closeDevice(s = session) {
  keyInput.value = "";
  if (!s || s.closed) return;
  s.closed = true;
  s.ready = false;
  cancelWait(s);
  try { await s.reader.cancel(); } catch { /* disconnected already */ }
  await s.readTask;
  try { s.writer.releaseLock(); } catch { /* lock released already */ }
  try { await s.port.close(); } catch { /* disconnected already */ }
  if (session === s) session = null;
  updateControls();
}

connectButton.addEventListener("click", async () => {
  connecting = true;
  updateControls();
  say("Choose the Espressif USB device. Close other connected serial tabs first.");
  let port;
  try {
    port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] });
    await port.open({ baudRate: 115200 });
    const s = { port, reader: port.readable.getReader(), writer: port.writable.getWriter(), ready: false, closed: false, pending: null };
    session = s;
    s.readTask = readDevice(s);
    say("Checking Handheld OS. Wait for the device's Home screen…");
    // Opening USB can restart some boards. No BOOT, reset or DTR/RTS signals
    // are asserted here, and no secret is sent before the help signature.
    await pause(3000);
    if (s.closed) throw new Error("Device disconnected");
    if (!s.ready) await saveField(s, "help", "ready");
    if (s.closed) throw new Error("Device disconnected");
    say("Connected. Paste your key and choose Save to T-Deck Pro.");
  } catch {
    await closeDevice();
    if (port && !session) { try { await port.close(); } catch { /* not open */ } }
    say("Could not connect to AI setup. Close other serial tabs, wait for Home, and reconnect. Install the current Handheld OS release if needed.");
  } finally {
    connecting = false;
    updateControls();
  }
});

disconnectButton.addEventListener("click", async () => {
  await closeDevice();
  say("Disconnected. The key field has been cleared.");
});

function values() {
  const endpoint = byId("endpoint").value.trim();
  const model = byId("model").value.trim();
  const key = keyInput.value;
  if (!endpoint.startsWith("https://") || endpoint.length > 191 || /[^\x21-\x7e]|[?#@\\]/.test(endpoint)) return null;
  let url;
  try { url = new URL(endpoint); } catch { return null; }
  if (url.protocol !== "https:" || !url.hostname || !/^[a-zA-Z0-9.-]+$/.test(url.hostname)) return null;
  if (!model || model.length > 95 || /[^\x21-\x7e]/.test(model)) return null;
  if (!key || key.length > 255 || /[^\x21-\x7e]/.test(key)) return null;
  return { endpoint, model, key };
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (saving || !session?.ready) return;
  const input = values();
  if (!input) { say("Check the HTTPS URL, model and key. Use printable characters without spaces; the key can have up to 255 characters."); return; }
  const s = session;
  saving = true;
  updateControls();
  say("Saving directly to your T-Deck Pro over USB…");
  try {
    await saveField(s, "ai endpoint " + input.endpoint, "AI endpoint saved");
    await saveField(s, "ai model " + input.model, "AI model saved");
    await saveField(s, "ai key " + input.key, "AI key saved");
    await closeDevice(s);
    say("Saved on your T-Deck Pro. USB is disconnected and the key field is cleared. On the device, press A, type a message, and press Enter. Chat uses Wi-Fi when connected, otherwise LTE.");
  } catch {
    await closeDevice(s);
    say("Setup did not finish. Some fields may have saved. Reconnect and try again; no API request was made.");
  } finally {
    input.key = "";
    keyInput.value = "";
    saving = false;
    updateControls();
  }
});

window.addEventListener("pageshow", () => { keyInput.value = ""; });
window.addEventListener("pagehide", () => { keyInput.value = ""; void closeDevice(); });
if (!supported) say("Open the HTTPS version of this page in desktop Chrome or Edge on macOS or Windows to use USB setup.");
updateControls();
