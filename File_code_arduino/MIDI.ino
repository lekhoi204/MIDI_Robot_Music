/******************  ESP32 MIDI → MULTI-SOLENOIDS (LittleFS + WebUI + POWER)  ******************
 * - Phát tất cả track/kênh; NOTE ON (vel>0) kể cả running status
 * - Drum: CH10 (map trong MAP[])
 * - Flute: CH1; nốt C mặc định khi boot (đè 6 lỗ); giữ hơi 450ms (tự đóng bằng millis)
 * - Bitmask sáo: 1 = ĐÈ (giữ yên, KHÔNG kích), 0 = NHẢ (kích 1 nhịp)
 * - Chỉ kích relay những lỗ cần đổi trạng thái giữa 2 nốt (tránh “tạch” dư)
 * - Web UI: / (UI) /upload /play /stop /scan /kick?v= /kick_all
 *           /power?x= /getpower
 *           /flute_note?n=72|74|76|79|81
 *           /flute_map /flute_fingers
 ************************************************************************************************/

/* ====== TrackBuf (đặt trước để tránh lỗi prototype của Arduino) ====== */
#include <stdint.h>
struct TrackBuf {
  uint8_t* data = nullptr;
  uint32_t len  = 0;
  uint32_t idx  = 0;
  uint8_t  run  = 0;       // running status
  bool     ended   = false;
  bool     pending = false;
  uint32_t next_dt = 0;    // delta ticks của event đang chờ
  uint32_t abs_tick= 0;    // tổng tick tính từ đầu track
};

/* ================== PHẦN CÒN LẠI ================== */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <vector>

/* ================== CẤU HÌNH ================== */
const char* AP_SSID = "ESP32-MIDI";
const char* AP_PASS = "12345678";

#define MAX_TRACKS        24
#define TRACE_NOTES       1
#define DEBUG_RAW_EVENTS  0
#define UI_FIXED_FILE     "/song.mid"  // Play/Scan đọc file này

/* ================== WEB & TRẠNG THÁI ================== */
WebServer server(80);
volatile bool isPlaying = false;         // chỉ để UI
volatile bool stopRequested = false;     // cờ dừng thực sự
volatile uint32_t g_noteon_count = 0;    // đếm NOTE ON để quyết định fallback

/* ====== POWER (hệ số lực đập cho solenoid dạng "hit") ====== */
volatile float g_power_boost = 1.0f;     // 0.5x..3.0x, mặc định 1.0x

/* ================== MULTI-SOLENOIDS ================== */
struct Solenoid {
  uint8_t  pin;
  bool     activeLow;
  volatile bool     pulseActive;
  volatile uint32_t offAt;
};

/* 
 * SỬA GPIO/activeLow CHO ĐÚNG PHẦN CỨNG CỦA BẠN:
 * - 0..4: Drum
 * - 5..10: Flute Hole1..Hole6 (1 = lỗ TRÊN, 6 = lỗ DƯỚI)
 * - 11:   Flute Air Valve (van hơi)
 */
Solenoid SOL[] = {
  {14, false, false, 0}, // 0: Drum Kick
  {27, false, false, 0}, // 1: Drum Snare
  {26, false, false, 0}, // 2: Drum Hi-hat
  {25, false, false, 0}, // 3: Drum Crash
  {12, false, false, 0}, // 4: Drum extra

  {33, false, false, 0}, // 5: Flute Hole1 (trên cùng)
  {32, false, false, 0}, // 6: Flute Hole2
  {23, false, false, 0}, // 7: Flute Hole3 (GPIO23)
  {22, false, false, 0}, // 8: Flute Hole4 (GPIO22)
  {21, false, false, 0}, // 9: Flute Hole5 (GPIO21)
  {19, false, false, 0}, // 10: Flute Hole6 (dưới cùng)
  {18, true,  false, 0}, // 11: Flute Air Valve (van hơi) - activeLow=true to keep closed at power
};
const uint8_t NUM_SOL = sizeof(SOL)/sizeof(SOL[0]);

/* ====== Helpers điều khiển solenoid ====== */
static inline void solDrive(uint8_t v, bool on){
  if (v >= NUM_SOL) return;
  digitalWrite(SOL[v].pin, SOL[v].activeLow ? !on : on);
}
static inline uint16_t velToMs(uint8_t v){
  v = constrain(v, 1, 127);
  return map(v, 1, 127, 30, 120); // ms
}
static inline void solHit(uint8_t v, uint8_t vel){  // non-blocking pulse (dùng cho Drum)
  if (v >= NUM_SOL) return;
  uint16_t base = velToMs(vel);
  uint16_t ms = (uint16_t)constrain((uint32_t)(base * g_power_boost), 5u, 400u);
#if TRACE_NOTES
  Serial.printf("SOL %u HIT vel=%u base=%ums power=%.2f -> %ums (pin=%u)\n",
                v, vel, base, g_power_boost, ms, SOL[v].pin);
#endif
  solDrive(v, true);
  SOL[v].pulseActive = true;
  SOL[v].offAt = millis() + ms;
}
static inline void solServiceAll(){
  uint32_t now = millis();
  for (uint8_t v=0; v<NUM_SOL; ++v){
    if (SOL[v].pulseActive && (int32_t)(now - SOL[v].offAt) >= 0){
      solDrive(v, false);
      SOL[v].pulseActive = false;
    }
  }
}
static inline void solInitAll(){
  for (uint8_t v=0; v<NUM_SOL; ++v){
    pinMode(SOL[v].pin, OUTPUT);
    solDrive(v, false);
    SOL[v].pulseActive = false;
  }
}

/* Xung blocking: dùng cho /scan, /kick */
static inline void solBangBlocking(uint8_t v, uint8_t vel){
  if (v >= NUM_SOL) return;
  uint16_t base = velToMs(vel);
  uint16_t ms = (uint16_t)constrain((uint32_t)(base * g_power_boost), 5u, 400u);
  solDrive(v, true);
  delay(ms);
  solDrive(v, false);
  delay(40);
}

/* ================== DRUM MAP (CH10) ================== */
/* ch: 0..15 (kênh 1..16), 255 = mọi kênh; note: 0..127, 255 = mọi nốt; voice: index SOL[] */
struct MapEntry { uint8_t ch; uint8_t note; int8_t voice; };

// Map trống phổ biến (Channel 10 = ch=9):
MapEntry MAP[] = {
  { 9, 36, 0 },   // Trống  -> sol0 (GPIO14)
  { 9, 56, 1 },   // Mỏ     -> sol1 (GPIO27)
  { 9, 42, 2 },   // Chũm chọe -> sol2 (GPIO26)
  { 9, 49, 3 },   // Chiêng -> sol3 (GPIO25)
  { 9, 37, 4 },   // Trống 2 -> sol4 (GPIO12)
};
const uint8_t MAP_N = sizeof(MAP)/sizeof(MAP[0]);

static inline int8_t mapVoice(uint8_t ch, uint8_t note){
  for (uint8_t i=0; i<MAP_N; ++i){
    bool okCh   = (MAP[i].ch   == 255) || (MAP[i].ch   == ch);
    bool okNote = (MAP[i].note == 255) || (MAP[i].note == note);
    if (okCh && okNote) return MAP[i].voice;
  }
  return -1;
}

/* ================== FLUTE (CH1) ================== */
#define FLUTE_BASE 5       // SOL index bắt đầu của lỗ 1
#define FLUTE_N    6       // 6 lỗ
#define FLUTE_AIR  (FLUTE_BASE + FLUTE_N)  // index van hơi
const bool FLUTE_AIR_BLOCK_ON = true;     // true = van đóng khi coil ON (tuỳ van)

// Thời gian
#define FLUTE_HOLD_MS  450   // giữ solenoid ON khi chơi nốt (tự đóng bằng millis)
// Nếu bật nhiều solenoid cùng lúc, stagger time (ms) giữa các kích để giảm inrush current
#define FLUTE_STAGGER_MS 12

// Bitmask sáo: bit = 1 → kích solenoid lỗ đó ON. bit0=Hole1 … bit5=Hole6
// Ví dụ D5: kích lỗ 6 (bit 5 = 1) → 0b00100000
struct Fingering { uint8_t note; uint8_t mask; };
Fingering FINGER[] = {
  {72, 0b00000000}, // C5 (Do):   không kích lỗ nào
  {74, 0b00100000}, // D5 (Re):   kích lỗ 6 (bit 5)
  {76, 0b00110000}, // E5 (Mi):   kích lỗ 5-6 (bit 5-4)
  {77, 0b00111000}, // F5 (Fa):   kích lỗ 4-5-6 (bit 5-3)
  {79, 0b00111100}, // G5 (Sol):  kích lỗ 3-4-5-6 (bit 5-2)
  {81, 0b00111110}, // A5 (La):   kích lỗ 2-3-4-5-6 (bit 5-1)
  {83, 0b00111111}, // B5 (Si):   kích lỗ 1-2-3-4-5-6 (tất cả)
};
const uint8_t FINGER_N = sizeof(FINGER)/sizeof(FINGER[0]);

// Trạng thái hơi
volatile bool     g_flute_air_open     = false;
volatile uint32_t g_flute_air_close_at = 0;

static inline void fluteAirSet(bool open){
  bool driveOn = FLUTE_AIR_BLOCK_ON ? !open : open;  // nếu van "đóng khi ON" thì đảo
  solDrive(FLUTE_AIR, driveOn);
  g_flute_air_open = open;
  if (open) g_flute_air_close_at = millis() + FLUTE_HOLD_MS;
}

static inline void fluteAirDefaultClosed(){ fluteAirSet(false); }

// Trạng thái giữ solenoid (kích ON và giữ đến hết time)
struct FluteHold {
  uint8_t  holeIdx;     // 0..5 (hole index)
  uint32_t onAt;        // millis khi sẽ bật ON (stagger scheduling)
  uint32_t releaseAt;   // millis khi hết thời gian giữ
  bool     scheduled;   // đã được đặt lịch bật chưa
  bool     active;      // đang ON hay không
};
static FluteHold g_flute_holds[FLUTE_N];

// Khởi tạo hold system sáo
static inline void fluteHoldInit(){
  for (uint8_t i = 0; i < FLUTE_N; ++i){
    g_flute_holds[i].active = false;
    g_flute_holds[i].scheduled = false;
    g_flute_holds[i].holeIdx = i;
    g_flute_holds[i].onAt = 0;
    g_flute_holds[i].releaseAt = 0;
  }
}

// Service giữ solenoid: bật khi đến onAt, tắt khi đến releaseAt
static inline void fluteHoldService(){
  uint32_t now = millis();
  for (uint8_t i = 0; i < FLUTE_N; ++i){
    FluteHold &h = g_flute_holds[i];
    if (h.scheduled && (int32_t)(now - h.onAt) >= 0){
      // time to turn ON
      solDrive(FLUTE_BASE + i, true);
      h.active = true;
      h.scheduled = false;
      h.releaseAt = now + FLUTE_HOLD_MS;
#if TRACE_NOTES
      Serial.printf("FLUTE L%u ON (scheduled)\n", i+1);
#endif
    }
    if (h.active && (int32_t)(now - h.releaseAt) >= 0){
      solDrive(FLUTE_BASE + i, false);  // tắt solenoid
      h.active = false;
#if TRACE_NOTES
      Serial.printf("FLUTE L%u release\n", i+1);
#endif
    }
  }
}

// Áp dụng mask: kích ON những lỗ trong mask và giữ FLUTE_HOLD_MS
static inline void fluteApplyMask(uint8_t mask){
  uint32_t now = millis();
  // schedule staggered ON for each set bit to reduce simultaneous current
  uint8_t k = 0; // index of scheduled activation
  for (uint8_t i = 0; i < FLUTE_N; ++i){
    if ((mask >> i) & 1){  // bit = 1 → schedule this hole
      g_flute_holds[i].onAt = now + (uint32_t)k * FLUTE_STAGGER_MS;
      g_flute_holds[i].scheduled = true;
      g_flute_holds[i].active = false; // will be set when turned on
#if TRACE_NOTES
      Serial.printf("FLUTE L%u scheduled ON at +%ums\n", i+1, (unsigned)((uint32_t)k * FLUTE_STAGGER_MS));
#endif
      k++;
    }
  }
}

// Tìm mask theo note rồi áp dụng
static inline bool fluteApplyNote(uint8_t note){
  for (uint8_t i=0;i<FINGER_N;i++){
    if (FINGER[i].note == note){
      fluteApplyMask(FINGER[i].mask);
      return true;
    }
  }
  return false; // nốt ngoài ngũ cung → bỏ qua
}

// Service đóng hơi đúng hạn (không block) + hold solenoid
static inline void fluteService(){
  fluteHoldService();  // service hold solenoid trước
  
  if (g_flute_air_open && (int32_t)(millis() - g_flute_air_close_at) >= 0){
    fluteAirSet(false);
#if TRACE_NOTES
    Serial.println("FLUTE air auto-close");
#endif
  }
}

/* ================== ROUTE NOTE ================== */
static inline void hitRoute(uint8_t ch, uint8_t note, uint8_t vel){
  if (vel == 0) return;

  // --- Flute trên CH1 (ch == 0) ---
  if (ch == 0){
    bool ok = fluteApplyNote(note);
    if (ok){
      fluteAirSet(true);                 // mở hơi và đặt lịch đóng
      g_noteon_count++;
#if TRACE_NOTES
      Serial.printf("FLUTE ch=%u note=%u -> mask applied, HOLD=%ums\n", ch+1, note, (unsigned)FLUTE_HOLD_MS);
#endif
    } else {
#if TRACE_NOTES
      Serial.printf("FLUTE ch=%u note=%u -> SKIP (outside pentatonic)\n", ch+1, note);
#endif
    }
    return;
  }

  // --- Drum (map CH10) ---
  int8_t v = mapVoice(ch, note);
  if (v >= 0) {
#if TRACE_NOTES
    Serial.printf("DRUM ch=%u note=%u vel=%u -> sol=%d\n", ch+1, note, vel, (int)v);
#endif
    solHit((uint8_t)v, vel);
    g_noteon_count++;
  }
}

/* ================== HỖ TRỢ ĐỌC FILE ================== */
uint32_t readBE32(File &f){
  uint8_t b[4]; if (f.read(b,4)!=4) return 0;
  return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];
}
uint16_t readBE16(File &f){
  uint8_t b[2]; if (f.read(b,2)!=2) return 0;
  return ((uint16_t)b[0]<<8)|b[1];
}
uint32_t readVarLen(File &f){
  uint32_t value=0; uint8_t c;
  do{ if (f.read(&c,1)!=1) return value; value=(value<<7)|(c&0x7F);}while(c&0x80);
  return value;
}
bool skipN(File &f, uint32_t n){
  while(n>0){ uint8_t buf[64]; uint32_t k=min<uint32_t>(64,n); int r=f.read(buf,k); if(r<=0)return false; n-=r; }
  return true;
}
void delayByTicks(uint32_t dticks, uint32_t tempo_us_per_qn, uint16_t ppqn){
  if (!dticks) return;
  uint64_t us_total = (uint64_t)dticks * (uint64_t)tempo_us_per_qn / (uint64_t)ppqn;
  if (us_total >= 1000){
    uint32_t ms = us_total/1000;
    uint32_t start = millis();
    while((millis()-start) < ms){ solServiceAll(); fluteService(); delay(1); }
    us_total -= (uint64_t)ms*1000ULL;
  }
  if (us_total>0) delayMicroseconds((uint32_t)us_total);
}

/* ================== BỘ TRỘN (MIXER) ================== */
static bool bufReadVarLen(TrackBuf& tb, uint32_t& out) {
  out = 0;
  while (tb.idx < tb.len) {
    uint8_t c = tb.data[tb.idx++];
    out = (out << 7) | (c & 0x7F);
    if ((c & 0x80) == 0) return true;
  }
  return false;
}
static inline bool bufReadByte(TrackBuf& tb, uint8_t& out) {
  if (tb.idx >= tb.len) return false;
  out = tb.data[tb.idx++];
  return true;
}
static inline bool bufSkipN(TrackBuf& tb, uint32_t n) {
  if (tb.idx + n > tb.len) { tb.idx = tb.len; return false; }
  tb.idx += n; return true;
}
static bool scheduleNextEvent(TrackBuf& tb) {
  if (tb.ended || tb.pending) return tb.pending;
  if (tb.idx >= tb.len) { tb.ended = true; return false; }
  uint32_t dt=0;
  if (!bufReadVarLen(tb, dt)) { tb.ended = true; return false; }
  tb.next_dt = dt;
  tb.abs_tick += dt;
  tb.pending = true;
  return true;
}

// Xử lý 1 event (NOTE ON vel>0 → route)
static uint8_t processOneEvent(TrackBuf& tb, uint32_t& tempo_us_per_qn) {
  if (tb.ended) return 1;
  if (!tb.pending) { if (!scheduleNextEvent(tb)) return tb.ended ? 1 : 2; }
  if (tb.idx >= tb.len) { tb.ended = true; return 1; }

  uint8_t b = tb.data[tb.idx++];

  if (b < 0x80) {
    if (tb.run == 0) { return 2; }
    uint8_t hi = tb.run & 0xF0;
    uint8_t ch = tb.run & 0x0F;
    uint8_t d1 = b;
    if (hi == 0xC0 || hi == 0xD0) {
      // 1 data byte
    } else {
      if (tb.idx >= tb.len) { tb.ended = true; return 1; }
      uint8_t d2 = tb.data[tb.idx++];
      if (hi == 0x90 && d2 > 0) {
#if TRACE_NOTES
        Serial.printf("MIX NOTEON(rs) ch=%u note=%u vel=%u\n", ch+1, d1, d2);
#endif
        hitRoute(ch, d1, d2);
      }
    }
  } else if (b == 0xFF) {
    if (tb.idx >= tb.len) { tb.ended=true; return 1; }
    uint8_t type = tb.data[tb.idx++];
    uint32_t len=0; if (!bufReadVarLen(tb,len)) { tb.ended=true; return 1; }
    if (type==0x2F) { tb.ended = true; return 1; }              // End of Track
    if (type==0x51 && len==3 && tb.idx+3 <= tb.len) {           // SetTempo
      tempo_us_per_qn = ((uint32_t)tb.data[tb.idx]<<16) | ((uint32_t)tb.data[tb.idx+1]<<8) | tb.data[tb.idx+2];
#if TRACE_NOTES
      Serial.printf("META SetTempo %lu us/qn\n", (unsigned long)tempo_us_per_qn);
#endif
    }
    bufSkipN(tb, len);
    tb.run = 0;
  } else if (b == 0xF0 || b == 0xF7) {
    uint32_t len=0; if (!bufReadVarLen(tb,len)) { tb.ended=true; return 1; }
    bufSkipN(tb, len);
    tb.run = 0;
  } else {
    tb.run = b;
    uint8_t hi = b & 0xF0;
    uint8_t ch = b & 0x0F;
    if (hi == 0xC0 || hi == 0xD0) {               // Program/Channel Pressure
      if (tb.idx >= tb.len) { tb.ended=true; return 1; }
      (void)tb.data[tb.idx++];                    // ✅ đã sửa: không còn tb.idx sai phạm vi
    } else {
      if (tb.idx+1 >= tb.len) { tb.ended=true; return 1; }
      uint8_t d1 = tb.data[tb.idx++];
      uint8_t d2 = tb.data[tb.idx++];
      if (hi == 0x90 && d2 > 0) {
#if TRACE_NOTES
        Serial.printf("MIX NOTEON(st) ch=%u note=%u vel=%u\n", ch+1, d1, d2);
#endif
        hitRoute(ch, d1, d2);
      }
    }
  }
  tb.pending = false;
  return 0;
}

/* ======= Fallback player (tuần tự) ======= */
static bool playSimpleSequential(const char* path){
  Serial.println("🔁 Fallback: sequential player");
  File f = LittleFS.open(path, "r");
  if (!f){ Serial.println("❌ Fallback open fail"); return false; }

  char sig[4];
  if (f.read((uint8_t*)sig,4)!=4 || memcmp(sig,"MThd",4)!=0){ f.close(); return false; }
  uint32_t hdLen = readBE32(f);
  (void)readBE16(f);                   // fmt
  uint16_t ntr = readBE16(f);
  uint16_t div = readBE16(f);
  if (hdLen > 6) skipN(f, hdLen - 6);
  if (div & 0x8000){ f.close(); return false; } // SMPTE không hỗ trợ
  uint16_t ppqn = div;
  uint32_t tempo_us_per_qn = 500000;

  for (uint16_t t=0; t<ntr && !stopRequested; ++t){
    if (f.read((uint8_t*)sig,4)!=4 || memcmp(sig,"MTrk",4)!=0) { f.close(); return false; }
    uint32_t len = readBE32(f);
    uint32_t end = f.position() + len;
    uint8_t run = 0;

    while ((uint32_t)f.position() < end && !stopRequested){
      uint32_t dt = readVarLen(f);
      delayByTicks(dt, tempo_us_per_qn, ppqn);
      int ib = f.read(); if (ib<0) break;
      uint8_t b = (uint8_t)ib;

      if (b < 0x80){
        if (!run) continue;
        uint8_t hi = run & 0xF0;
        uint8_t ch = run & 0x0F;
        uint8_t d1 = b;
        if (hi==0xC0 || hi==0xD0) { /*done*/ }
        else {
          int d2i=f.read(); if (d2i<0) break;
          uint8_t d2=(uint8_t)d2i;
          if (hi==0x90 && d2>0) hitRoute(ch, d1, d2);
        }
      } else if (b==0xFF){
        int type=f.read(); if (type<0) break;
        uint32_t l=readVarLen(f);
        if (type==0x51 && l==3){
          uint8_t buf[3]; if (f.read(buf,3)==3){
            tempo_us_per_qn = ((uint32_t)buf[0]<<16)|((uint32_t)buf[1]<<8)|buf[2];
#if TRACE_NOTES
            Serial.printf("META SetTempo %lu us/qn\n", (unsigned long)tempo_us_per_qn);
#endif
          } else break;
        } else { if (!skipN(f,l)) break; }
        run=0;
      } else if (b==0xF0 || b==0xF7){
        uint32_t l=readVarLen(f); if (!skipN(f,l)) break; run=0;
      } else {
        run = b;
        uint8_t hi = b & 0xF0;
        uint8_t ch = b & 0x0F;
        if (hi==0xC0 || hi==0xD0){ (void)f.read(); }
        else {
          int d1i=f.read(); int d2i=f.read(); if (d1i<0||d2i<0) break;
          uint8_t d1=(uint8_t)d1i, d2=(uint8_t)d2i;
          if (hi==0x90 && d2>0) hitRoute(ch, d1, d2);
        }
      }
    }
    if ((uint32_t)f.position() < end) skipN(f, end - f.position());
  }
  f.close();
  return true;
}

/* ======== Mixer chính: phát tất cả track/kênh (route theo MAP/FLUTE) ======== */
static bool playMIDI_AllNotes(const char* path){
  File f = LittleFS.open(path, "r");
  if (!f){ Serial.println("❌ Cannot open MIDI"); return false; }
  size_t fsz = f.size();
  Serial.printf("ℹ️ File size = %u bytes\n", (unsigned)fsz);

  char sig[4];
  if (f.read((uint8_t*)sig,4)!=4 || memcmp(sig,"MThd",4)!=0){ Serial.println("❌ Not MThd"); f.close(); return false; }
  uint32_t hdLen = readBE32(f);
  uint16_t fmt = readBE16(f);
  uint16_t ntr = readBE16(f);
  uint16_t div = readBE16(f);
  Serial.printf("ℹ️ MThd len=%lu fmt=%u tracks=%u div=0x%04X\n",
                (unsigned long)hdLen, fmt, ntr, div);
  if (hdLen>6) skipN(f, hdLen-6);
  if (div & 0x8000){ Serial.println("❌ SMPTE timebase not supported"); f.close(); return false; }
  uint16_t ppqn = div;
  if (ntr == 0 || ntr > MAX_TRACKS){ Serial.println("❌ Unsupported track count"); f.close(); return false; }

  TrackBuf tracks[MAX_TRACKS];
  for (uint16_t t=0; t<ntr; ++t){
    if (f.read((uint8_t*)sig,4)!=4 || memcmp(sig,"MTrk",4)!=0){
      Serial.printf("❌ Track %u header missing\n", t); f.close(); return false;
    }
    uint32_t len = readBE32(f);
    Serial.printf("ℹ️ Track %u len=%lu\n", t, (unsigned long)len);
    tracks[t].data = (uint8_t*)malloc(len);
    if (!tracks[t].data){ Serial.println("❌ OOM while malloc track"); f.close(); return false; }
    tracks[t].len = len;
    int readed = f.read(tracks[t].data, len);
    if (readed != (int)len){ Serial.println("❌ Read track failed"); f.close(); return false; }
    tracks[t].idx = 0; tracks[t].run = 0; tracks[t].ended = false;
    tracks[t].pending = false; tracks[t].next_dt = 0; tracks[t].abs_tick = 0;
  }
  f.close();

  uint32_t tempo_us_per_qn = 500000;
  for (uint16_t t=0; t<ntr; ++t) scheduleNextEvent(tracks[t]);
  uint32_t last_tick = 0;

  // Mixer loop
  while (!stopRequested) {
    bool anyPending = false;
    uint32_t minTick = 0xFFFFFFFF;
    for (uint16_t t=0; t<ntr; ++t){
      if (!tracks[t].ended && tracks[t].pending){
        anyPending = true;
        if (tracks[t].abs_tick < minTick) minTick = tracks[t].abs_tick;
      }
    }
    if (!anyPending) {
      for (uint16_t t=0; t<ntr; ++t)
        if (!tracks[t].ended && !tracks[t].pending) scheduleNextEvent(tracks[t]);
      anyPending = false; minTick = 0xFFFFFFFF;
      for (uint16_t t=0; t<ntr; ++t){
        if (!tracks[t].ended && tracks[t].pending){
          anyPending = true;
          if (tracks[t].abs_tick < minTick) minTick = tracks[t].abs_tick;
        }
      }
      if (!anyPending) break;  // hết bài
    }

    if (minTick > last_tick) {
      uint32_t dt = minTick - last_tick;
      delayByTicks(dt, tempo_us_per_qn, ppqn);  // có gọi solServiceAll() + fluteService() bên trong
      if (stopRequested) break;
      last_tick = minTick;
    }

    for (uint16_t t=0; t<ntr; ++t){
      if (!tracks[t].ended && tracks[t].pending && tracks[t].abs_tick == minTick){
        uint8_t b = tracks[t].data[tracks[t].idx++];

        if (b == 0xFF) {  // Meta
          if (tracks[t].idx >= tracks[t].len) { tracks[t].ended=true; continue; }
          uint8_t type = tracks[t].data[tracks[t].idx++];
          uint32_t len=0; bufReadVarLen(tracks[t], len);
          if (type==0x2F) { tracks[t].ended = true; }
          else if (type==0x51 && len==3 && tracks[t].idx+3<=tracks[t].len) {
            tempo_us_per_qn = ((uint32_t)tracks[t].data[tracks[t].idx]<<16) | ((uint32_t)tracks[t].data[tracks[t].idx+1]<<8) | tracks[t].data[tracks[t].idx+2];
          }
          tracks[t].idx += len;
          tracks[t].pending = false;

        } else if (b == 0xF0 || b == 0xF7) { // SysEx skip
          uint32_t len=0; bufReadVarLen(tracks[t], len);
          tracks[t].idx += len;
          tracks[t].pending = false;

        } else { // MIDI channel event
          if (b < 0x80) {  // running status
            if (tracks[t].run == 0) { tracks[t].pending=false; continue; }
            uint8_t hi = tracks[t].run & 0xF0;
            uint8_t ch = tracks[t].run & 0x0F;
            uint8_t d1 = b;
            if (hi != 0xC0 && hi != 0xD0) {
              if (tracks[t].idx >= tracks[t].len) { tracks[t].ended=true; continue; }
              uint8_t d2 = tracks[t].data[tracks[t].idx++];
              if (hi == 0x90 && d2 > 0) hitRoute(ch, d1, d2);
            }
          } else {         // new status
            tracks[t].run = b;
            uint8_t hi = b & 0xF0;
            uint8_t ch = b & 0x0F;
            if (hi == 0xC0 || hi == 0xD0) {
              if (tracks[t].idx >= tracks[t].len) { tracks[t].ended=true; continue; }
              (void)tracks[t].data[tracks[t].idx++]; // ✅ fixed
            } else {
              if (tracks[t].idx+1 >= tracks[t].len) { tracks[t].ended=true; continue; }
              uint8_t d1 = tracks[t].data[tracks[t].idx++];
              uint8_t d2 = tracks[t].data[tracks[t].idx++];
              if (hi == 0x90 && d2 > 0) hitRoute(ch, d1, d2);
            }
          }
          tracks[t].pending = false;
        }
        scheduleNextEvent(tracks[t]);
      }
    }
  }

  for (uint16_t t=0; t<ntr; ++t){ if (tracks[t].data) free(tracks[t].data); }

  // CHỈ fallback nếu KHÔNG có NOTE ON nào
  if (!stopRequested && g_noteon_count == 0) {
    Serial.println("⚠️ No NOTE ON detected in mixer — trying fallback...");
    return playSimpleSequential(path);
  }
  return true;
}

/* ================== SCAN (không timing): test parser + đập blocking) ================== */
static bool scanMIDI_JustNotes(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f){ Serial.println("❌ Cannot open MIDI"); return false; }

  char sig[4];
  if (f.read((uint8_t*)sig,4)!=4 || memcmp(sig,"MThd",4)!=0){ Serial.println("❌ Not MThd"); f.close(); return false; }
  uint32_t hdLen = readBE32(f);
  (void)readBE16(f); // fmt
  uint16_t ntr = readBE16(f);
  (void)readBE16(f); // div
  if (hdLen>6) skipN(f, hdLen-6);

  uint32_t totalOn = 0;

  for (uint16_t t=0; t<ntr; ++t){
    if (f.read((uint8_t*)sig,4)!=4 || memcmp(sig,"MTrk",4)!=0){ Serial.println("❌ MTrk missing"); break; }
    uint32_t len = readBE32(f);
    uint32_t end = f.position() + len;

    uint8_t run = 0;
    while ((uint32_t)f.position() < end){
      (void)readVarLen(f); // bỏ delta
      int ib = f.read(); if (ib<0) break;
      uint8_t b = (uint8_t)ib;

      if (b < 0x80){
        if (!run) continue;
        uint8_t hi = run & 0xF0;
        uint8_t ch = run & 0x0F;
        uint8_t d1 = b;
        if (hi!=0xC0 && hi!=0xD0){
          int d2i=f.read(); if (d2i<0) break;
          uint8_t d2=(uint8_t)d2i;
          if (hi==0x90 && d2>0) {
            totalOn++;
            int8_t v = mapVoice(ch, d1);  // chỉ scan Drum để tiết kiệm coil sáo
            if (v>=0){
              Serial.printf("SCAN NOTEON ch=%u note=%u vel=%u -> sol=%d\n", ch+1, d1, d2, (int)v);
              solBangBlocking((uint8_t)v, d2);
            }
          }
        }
      } else if (b == 0xFF){
        int type=f.read(); if (type<0) break;
        uint32_t l=readVarLen(f); if (!skipN(f,l)) break;
        run = 0;
      } else if (b == 0xF0 || b == 0xF7){
        uint32_t l=readVarLen(f); if (!skipN(f,l)) break;
        run = 0;
      } else {
        run = b;
        uint8_t hi = b & 0xF0;
        uint8_t ch = b & 0x0F;
        if (hi==0xC0 || hi==0xD0){ (void)f.read(); }
        else {
          int d1i=f.read(); int d2i=f.read(); if (d1i<0||d2i<0) break;
          uint8_t d1=(uint8_t)d1i, d2=(uint8_t)d2i;
          if (hi==0x90 && d2>0) {
            totalOn++;
            int8_t v = mapVoice(ch, d1);
            if (v>=0){
              Serial.printf("SCAN NOTEON ch=%u note=%u vel=%u -> sol=%d\n", ch+1, d1, d2, (int)v);
              solBangBlocking((uint8_t)v, d2);
            }
          }
        }
      }
    }
    if ((uint32_t)f.position() < end) skipN(f, end - f.position());
  }
  f.close();
  Serial.printf("SCAN DONE: total NOTEON=%lu\n", (unsigned long)totalOn);
  return totalOn>0;
}

/* ================== WEB UI ================== */
static inline const char* noteName(uint8_t n){
  switch(n){
    case 72: return "C"; case 74: return "D"; case 76: return "E";
    case 79: return "G"; case 81: return "A";
    default: return "?";
  }
}

// Endpoints UI JSON
void handleFluteMap(){
  String s = "{\"holes\":[";
  for (int j=0; j<FLUTE_N; ++j){
    if (j) s += ",";
    s += String(SOL[FLUTE_BASE + j].pin);
  }
  s += "],\"air\":";
  s += String(SOL[FLUTE_AIR].pin);
  s += "}";
  server.send(200, "application/json", s);
}

void handleFluteFingers(){
  String s = "[";
  for (int i=0;i<FINGER_N;i++){
    if (i) s += ",";
    uint8_t mask = FINGER[i].mask;
    s += "{";
    s += "\"note\":" + String(FINGER[i].note);
    s += ",\"name\":\""; s += noteName(FINGER[i].note); s += "\"";
    s += ",\"mask\":" + String(mask);
    s += ",\"pins\":[";
    bool first=true;
    for (int j=0;j<FLUTE_N;j++){
      if ((mask >> j) & 1){
        if (!first) s += ",";
        first=false;
        s += String(SOL[FLUTE_BASE + j].pin);
      }
    }
    s += "]}";
  }
  s += "]";
  server.send(200, "application/json", s);
}

// Nút test sáo từ UI
void handleFluteNote() {
  if (!server.hasArg("n")) { server.send(400,"text/plain","missing n"); return; }
  uint8_t note = server.arg("n").toInt();
  Serial.printf("[FLUTE-TEST] note=%u\n", note);
  bool ok = fluteApplyNote(note);
  if (ok){ fluteAirSet(true); }  // tự đóng sau FLUTE_HOLD_MS
  server.send(200,"text/plain", ok ? "OK" : "SKIP");
}

/* ================== UI HTML ================== */
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<title>ESP32 MIDI → Multi Solenoids</title>
<style>
body{font-family:Arial;max-width:900px;margin:24px auto;background:#fafafa}
.card{background:#fff;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,.06);padding:16px;margin:16px 0}
button{padding:10px 16px;border:0;border-radius:8px;margin:6px;cursor:pointer}
.play{background:#4CAF50;color:#fff}.stop{background:#f44336;color:#fff}
.upload{background:#2196F3;color:#fff}.tool{background:#795548;color:#fff}
.test{background:#9C27B0;color:#fff}
code{background:#f3f3f3;padding:2px 6px;border-radius:6px}
.small{font-size:12px;color:#666}
input[type=range]{width:260px}
.value{display:inline-block;min-width:52px;text-align:center}
.btn{min-width:56px}
table{border-collapse:collapse;width:100%}
th,td{border:1px solid #eee;padding:8px;text-align:left}
th{background:#fafafa}
.tag{display:inline-block;padding:2px 8px;border-radius:999px;background:#eee;margin:2px 4px}
</style></head><body>
<h2>🥁 ESP32 MIDI → Multi-Solenoids</h2>

<div class="card">
  <h3>Upload .mid</h3>
  <form method="POST" action="/upload" enctype="multipart/form-data">
    <input type="file" name="midi" accept=".mid,.midi" required>
    <button class="upload" type="submit">Upload to <code>/song.mid</code></button>
  </form>
  <p class="small">Play/Scan đọc cố định file <code>/song.mid</code> trong LittleFS.</p>
</div>

<div class="card">
  <h3>Playback</h3>
  <button class="play" onclick="fetch('/play')">Play (Mixer)</button>
  <button class="tool" onclick="fetch('/scan')">Scan Notes (no timing)</button>
  <button class="stop" onclick="fetch('/stop')">Stop</button>
  <p>Status: <span id="st">...</span></p>
</div>

<div class="card">
  <h3>Power (lực đập Drum)</h3>
  <div>
    <input id="pwr" type="range" min="0.5" max="3.0" step="0.1" value="1.0" oninput="pwrval.textContent=this.value" onchange="setPower(this.value)">
    <span class="value"><b id="pwrval">1.0</b>×</span>
    <button class="tool" onclick="setPower(document.getElementById('pwr').value)">Apply</button>
  </div>
</div>

<div class="card">
  <h3>Solenoid Test</h3>
  <div>
    <button class="test" onclick="fetch('/kick?v=0')">Kick sol0</button>
    <button class="test" onclick="fetch('/kick?v=1')">Kick sol1</button>
    <button class="test" onclick="fetch('/kick?v=2')">Kick sol2</button>
    <button class="test" onclick="fetch('/kick?v=3')">Kick sol3</button>
    <button class="test" onclick="fetch('/kick?v=4')">Kick sol4</button>
    <button class="test" onclick="fetch('/kick_all')">Kick ALL</button>
  </div>
</div>

<div class="card">
  <h3>Flute Test (bảy nốt: Do Re Mi Fa Sol La Si + giữ hơi 450ms)</h3>
  <div>
    <button class="test btn" id="btnC" onclick="fl(72)" title="C">Do</button>
    <button class="test btn" id="btnD" onclick="fl(74)" title="D">Re</button>
    <button class="test btn" id="btnE" onclick="fl(76)" title="E">Mi</button>
    <button class="test btn" id="btnF" onclick="fl(77)" title="F">Fa</button>
    <button class="test btn" id="btnG" onclick="fl(79)" title="G">Sol</button>
    <button class="test btn" id="btnA" onclick="fl(81)" title="A">La</button>
    <button class="test btn" id="btnB" onclick="fl(83)" title="B">Si</button>
  </div>
  <p class="small">Tooltip nút hiển thị GPIO đang đè cho mỗi nốt.</p>
</div>

<div class="card">
  <h3>Flute GPIO Map</h3>
  <table>
    <thead><tr><th>Hole</th><th>GPIO</th></tr></thead>
    <tbody id="flmap"></tbody>
  </table>
  <p class="small">Đọc trực tiếp từ firmware (SOL[]) nên luôn đúng với phần cứng.</p>
</div>

<div class="card">
  <h3>Flute Fingers (Note → Holes → GPIO)</h3>
  <table>
    <thead><tr><th>Note</th><th>Mask (b5..b0, 1=đè)</th><th>GPIO đang đè</th></tr></thead>
    <tbody id="flfingers"></tbody>
  </table>
</div>

<script>
async function tick(){
  try{
    const r=await fetch('/status'); 
    document.getElementById('st').innerText=await r.text();
  }catch(e){}
}
async function loadPower(){
  try{
    const r=await fetch('/getpower');
    const x=parseFloat(await r.text());
    const el=document.getElementById('pwr');
    el.value=isNaN(x)?1.0:x;
    document.getElementById('pwrval').textContent=el.value;
  }catch(e){}
}
async function setPower(x){ try{ await fetch('/power?x='+x); }catch(e){} }
async function fl(n){ try{ await fetch('/flute_note?n='+n); }catch(e){} }

async function loadFluteMap(){
  try{
    const r = await fetch('/flute_map'); const j = await r.json();
    const tb = document.getElementById('flmap'); tb.innerHTML='';
    for(let i=0;i<j.holes.length;i++){
      const tr=document.createElement('tr');
      tr.innerHTML = `<td>Hole ${i+1}</td><td><code>GPIO${j.holes[i]}</code></td>`;
      tb.appendChild(tr);
    }
    const tr=document.createElement('tr');
    tr.innerHTML = `<td><b>Air Valve</b></td><td><code>GPIO${j.air}</code></td>`;
    tb.appendChild(tr);
  }catch(e){}
}

async function loadFluteFingers(){
  try{
    const r = await fetch('/flute_fingers'); const arr = await r.json();
    const tb = document.getElementById('flfingers'); tb.innerHTML='';
    const btnMap = {72:'btnC',74:'btnD',76:'btnE',77:'btnF',79:'btnG',81:'btnA',83:'btnB'};
    for(const it of arr){
      const tags = it.pins.map(p=>`<span class="tag">GPIO${p}</span>`).join(' ');
      const mask = it.mask.toString(2).padStart(6,'0'); // b5..b0
      const tr = document.createElement('tr');
      tr.innerHTML = `<td><b>${it.name}</b> (${it.note})</td><td><code>${mask}</code></td><td>${tags}</td>`;
      tb.appendChild(tr);
      const btnId = btnMap[it.note];
      const btn = document.getElementById(btnId);
      if (btn) btn.title = `${it.name}: ${it.pins.map(p=>'GPIO'+p).join(', ')} (đè)`;
    }
  }catch(e){}
}

setInterval(tick,600);
tick(); loadPower(); loadFluteMap(); loadFluteFingers();
</script>
</body></html>
)HTML";

/* ================== HANDLERS ================== */
void handleUpload(){
  HTTPUpload& up = server.upload();
  static File f;
  if (up.status == UPLOAD_FILE_START){
    if (LittleFS.exists(UI_FIXED_FILE)) LittleFS.remove(UI_FIXED_FILE);
    f = LittleFS.open(UI_FIXED_FILE,"w");
    if (!f){ server.send(500,"text/plain","Open LittleFS failed"); return; }
    Serial.println("[Upload] start " UI_FIXED_FILE);
  } else if (up.status == UPLOAD_FILE_WRITE){
    if (f) f.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END){
    if (f) f.close();
    Serial.printf("[Upload] done %u bytes\n", up.totalSize);
    server.send(200,"text/plain","Upload OK (go back)");
  } else if (up.status == UPLOAD_FILE_ABORTED){
    if (f) f.close(); LittleFS.remove(UI_FIXED_FILE);
    server.send(500,"text/plain","Upload aborted");
  }
}

void handleKick(){     // /kick?v=idx
  if (!server.hasArg("v")){ server.send(400,"text/plain","missing v"); return; }
  int v = server.arg("v").toInt();
  if (v<0 || v>=NUM_SOL){ server.send(400,"text/plain","bad v"); return; }
  Serial.printf("KICK sol%d\n", v);
  solBangBlocking((uint8_t)v, 100);
  server.send(200,"text/plain","kicked");
}
void handleKickAll(){
  Serial.println("KICK ALL");
  for (uint8_t v=0; v<NUM_SOL; ++v){ solBangBlocking(v, 100); delay(80); }
  server.send(200,"text/plain","kicked all");
}

/* ===== POWER handlers ===== */
void handlePower(){
  if (!server.hasArg("x")) { server.send(400,"text/plain","use /power?x=0.5..3.0"); return; }
  float x = server.arg("x").toFloat();
  if (x < 0.5f) x = 0.5f;
  if (x > 3.0f) x = 3.0f;
  g_power_boost = x;
  char buf[64]; dtostrf(x, 1, 2, buf);
  String s = "power="; s += buf; s += "x";
  server.send(200,"text/plain", s);
}
void handleGetPower(){
  char buf[32]; dtostrf(g_power_boost, 1, 2, buf);
  server.send(200,"text/plain", buf);
}

/* ============ TASK PHÁT MIDI ============ */
TaskHandle_t playTaskHandle = NULL;
void playTask(void*){
  Serial.println("▶ Play start");
  g_noteon_count = 0;           // đếm NOTE ON
  bool ok = playMIDI_AllNotes(UI_FIXED_FILE);
  isPlaying = false;
  stopRequested = false;
  for (uint8_t v=0; v<NUM_SOL; ++v){ solDrive(v, false); SOL[v].pulseActive=false; }
  fluteAirDefaultClosed();      // đảm bảo đóng hơi
  Serial.printf("⏹ Play end (ok=%d, NOTEON=%lu)\n", ok ? 1 : 0, (unsigned long)g_noteon_count);
  playTaskHandle = NULL;
  vTaskDelete(NULL);
}

void handlePlay(){
  if (!LittleFS.exists(UI_FIXED_FILE)){ server.send(404,"text/plain","No /song.mid"); return; }
  if (!isPlaying){
    isPlaying = true;
    stopRequested = false;
    xTaskCreate(playTask, "MIDIPlay", 16384, NULL, 1, &playTaskHandle);
    server.send(200,"text/plain","Playing (mixer)...");
  } else server.send(200,"text/plain","Already playing");
}
void handleScan(){
  if (!LittleFS.exists(UI_FIXED_FILE)){ server.send(404,"text/plain","No /song.mid"); return; }
  bool ok = scanMIDI_JustNotes(UI_FIXED_FILE);
  server.send(200,"text/plain", ok ? "Scan: NOTE ON found (check Serial)" : "Scan: NO NOTE ON");
}
void handleStop(){
  if (isPlaying){
    stopRequested = true;
    Serial.println("🛑 Stop requested");
  }
  for (uint8_t v=0; v<NUM_SOL; ++v){ solDrive(v, false); SOL[v].pulseActive=false; }
  fluteAirDefaultClosed();
  server.send(200,"text/plain","Stopped");
}
void handleStatus(){ server.send(200,"text/plain", isPlaying ? "Playing..." : "Idle"); }

/* ================== SETUP / LOOP ================== */
void setup(){
  Serial.begin(115200);
  Serial.printf("BUILD: %s %s\n", __DATE__, __TIME__);

  solInitAll();
  fluteHoldInit();  // khởi tạo hold system

  if (!LittleFS.begin(true)) Serial.println("❌ LittleFS mount failed");
  else                      Serial.println("✅ LittleFS ready");

  WiFi.softAP(AP_SSID, AP_PASS);
  delay(300);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  // Web routes
  server.on("/", [](){ server.send(200,"text/html", INDEX_HTML); });
  server.on("/upload", HTTP_POST, [](){}, handleUpload);
  server.on("/play", handlePlay);
  server.on("/scan", handleScan);
  server.on("/stop", handleStop);
  server.on("/status", handleStatus);
  server.on("/kick", handleKick);
  server.on("/kick_all", handleKickAll);

  // POWER endpoints
  server.on("/power", handlePower);
  server.on("/getpower", handleGetPower);

  // Flute endpoints
  server.on("/flute_note", handleFluteNote);
  server.on("/flute_map", handleFluteMap);
  server.on("/flute_fingers", handleFluteFingers);

  server.begin();
  Serial.println("🌐 Web server ready");

  // --- Sáo mặc định: tất cả lỗ đã OFF khi boot ---
  fluteAirDefaultClosed();
  Serial.println("✅ Flute ready (all holes OFF)");
}

void loop(){
  server.handleClient();
  solServiceAll();   // tắt xung Drum đúng hạn
  fluteService();    // auto-close hơi khi đủ 450ms
}
