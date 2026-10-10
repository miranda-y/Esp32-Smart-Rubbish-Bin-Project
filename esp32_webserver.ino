#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "img_converters.h"

WebServer server(80);

//Camera pins
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   15
#define SIOD_GPIO_NUM   4
#define SIOC_GPIO_NUM   5
#define Y2_GPIO_NUM     11
#define Y3_GPIO_NUM     9
#define Y4_GPIO_NUM     8
#define Y5_GPIO_NUM     10
#define Y6_GPIO_NUM     12
#define Y7_GPIO_NUM     17
#define Y8_GPIO_NUM     16
#define Y9_GPIO_NUM     18
#define VSYNC_GPIO_NUM  6
#define HREF_GPIO_NUM   7
#define PCLK_GPIO_NUM   13

static uint8_t *rgb888 = nullptr;

void handleJpg() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { server.send(500, "text/plain", "Camera capture failed"); return; }

  int total = fb->width * fb->height;
  uint16_t *pixels = (uint16_t *)fb->buf;
  for (int i = 0; i < total; i++) {
    uint16_t p = pixels[i];
    p = (p >> 8) | (p << 8);
    rgb888[i*3]   = ((p >> 11) & 0x1F) << 2;
    rgb888[i*3+1] = ((p >> 5) & 0x3F) << 2;
    rgb888[i*3+2] = (p & 0x1F) << 3;
  }

  uint8_t *jpg_buf = nullptr;
  size_t jpg_len = 0;
  bool ok = fmt2jpg(rgb888, total * 3, fb->width, fb->height, PIXFORMAT_RGB888, 80, &jpg_buf, &jpg_len);
  esp_camera_fb_return(fb);
  if (!ok) { server.send(500, "text/plain", "JPEG conversion failed"); return; }

  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "image/jpeg", (const char *)jpg_buf, jpg_len);
  free(jpg_buf);
}

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body{font-family:sans-serif;text-align:center;padding:16px;background:#fff;color:#000}
  img{max-width:100%;border:1px solid #ccc}
  select,button,input{padding:12px;font-size:17px;margin:4px}
  button{background:#ff99c4;color:#fff;border:none;border-radius:4px;cursor:pointer}
  button.stop{background:#c0392b}
  #counts{margin-top:10px;font-size:16px}
</style>
</head>
<body>
  <h2>Rubbish Bin Training Capture</h2>
  <img id="view" alt="live view">
  <br>
  <select id="label">
    <option>background</option>
    <option>green_bin</option>
    <option>yellow_bin</option>
    <option>red_bin</option>
  </select>
  <button onclick="saveOne()">Capture 1</button>
  <br>
  Burst: every <input id="ms" type="number" value="800" style="width:80px"> ms
  <button id="burstBtn" onclick="toggleBurst()">Start burst</button>
  <div id="counts"></div>

<script>
  let lastBlob = null, counts = {}, burst = null;

  async function refresh() {
    try {
      const r = await fetch('/jpg?t=' + Date.now());
      if (r.ok) {
        lastBlob = await r.blob();
        const img = document.getElementById('view');
        const old = img.src;
        img.src = URL.createObjectURL(lastBlob);
        if (old.startsWith('blob:')) URL.revokeObjectURL(old);
      }
    } catch (e) {}
    setTimeout(refresh, 100);   // next frame only after this one finished
  }

  function saveOne() {
    if (!lastBlob) return;
    const label = document.getElementById('label').value;
    const a = document.createElement('a');
    a.href = URL.createObjectURL(lastBlob);
    // "label.timestamp.jpg" -> Edge Impulse reads the label from before the first dot
    a.download = label + '.' + Date.now() + '.jpg';
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 2000);
    counts[label] = (counts[label] || 0) + 1;
    document.getElementById('counts').innerText =
      Object.entries(counts).map(([k, v]) => k + ': ' + v).join('   |   ');
  }

  function toggleBurst() {
    const btn = document.getElementById('burstBtn');
    if (burst) {
      clearInterval(burst); burst = null;
      btn.innerText = 'Start burst'; btn.className = '';
    } else {
      const ms = Math.max(300, parseInt(document.getElementById('ms').value) || 800);
      burst = setInterval(saveOne, ms);
      btn.innerText = 'Stop burst'; btn.className = 'stop';
    }
  }

  refresh();
</script>
</body>
</html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.softAP("RubbishBin", "12345678");
  Serial.print("Access point: http://");
  Serial.println(WiFi.softAPIP());

  camera_config_t config = {};
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) { Serial.printf("Cam init failed: 0x%x\n", err); return; }

  rgb888 = (uint8_t *)ps_malloc(320 * 240 * 3);
  if (!rgb888) { Serial.println("Buffer alloc failed (enable PSRAM in Tools menu)"); return; }

  server.on("/", handleRoot);
  server.on("/jpg", handleJpg);
  server.begin();
  Serial.println("Ready.");
}

void loop() {
  server.handleClient();
}