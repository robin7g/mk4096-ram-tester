// MK4096N-16 DRAM tester (4Kx1) for Arduino Uno R4 Minima
// Multi-run + stress-soak: repeats the full 4-pattern test RUNS times and
// gradually increases an inter-column pause as the test progresses.
//
// Targets total time < ~30s by default. Tune MAX_SOAK_US if you want more/less.
//
// MK4096 specifics:
//   - 6 address bits (A0..A5): 64 rows x 64 cols = 4096 bits
//   - Socket pin 13 is /CS (active LOW). We hold it LOW.
//
// Serial: 9600 baud
//
// LED wiring assumption (active-low):
//   LED anode -> +5V through resistor, cathode -> Arduino pin
//   => LED turns ON when Arduino pin is LOW (active-low)
//
// NOTE: On Uno R4, D9 supports PWM, D8 does not. We dim red with PWM and
// "dim" green by using short pulses (low duty cycle).

// ---------------- Pins ----------------
#define DI          7     // Arduino D7  -> socket pin 2  (DIN)
#define DO          15    // Arduino A1  -> socket pin 14 (DOUT)  (numeric pin ID)
#define CAS         14    // Arduino A0  -> socket pin 15 (/CAS)
#define RAS         5     // Arduino D5  -> socket pin 4  (/RAS)
#define WE          6     // Arduino D6  -> socket pin 3  (/WE)

// Address pins (A0..A5)
#define XA0         4     // Arduino D4  -> socket pin 5  (A0)
#define XA1         2     // Arduino D2  -> socket pin 7  (A1)
#define XA2         3     // Arduino D3  -> socket pin 6  (A2)
#define XA3         17    // Arduino A3  -> socket pin 12 (A3)
#define XA4         18    // Arduino A4  -> socket pin 11 (A4)
#define XA5         19    // Arduino A5  -> socket pin 10 (A5)

// MK4096 /CS on socket pin 13 (active LOW)
#define CS          16    // Arduino A2  -> socket pin 13 (/CS)

// LEDs (active-low)
#define R_LED       9     // Arduino D9  -> red LED cathode (PWM-capable)
#define G_LED       8     // Arduino D8  -> green LED cathode (NOT PWM)

// ---------------- Config ----------------
#define BUS_SIZE     6    // MK4096 has 6 address inputs
#define RUNS        10    // repeat the full test this many times

// Brightness controls (0..255 where 0=off, 255=full ON)
// Because LEDs are active-low, PWM duty is inverted internally.
#define RED_RUN_LEVEL        18   // very dim "running" glow
#define RED_FLASH_LEVEL     170   // fail flash intensity
#define GREEN_PULSE_ON_MS     2   // short green pulses during progress

// Progress printing control
#define DOT_EVERY_COLS        8   // print one dot every N columns (64/N dots per pass)

// Stress-soak: per-column pause increases gradually from 0..MAX_SOAK_US
// across the *entire* test (all runs, all passes, all columns).
// With RUNS=10, BUS_SIZE=6 => total columns = 10 * 4 * 64 = 2560.
// Average pause ~ MAX_SOAK_US/2, so extra time ≈ 2560 * (MAX/2) microseconds.
// MAX_SOAK_US=8000 -> extra ~ 10.24s. Typical total remains under ~30s.
#define MAX_SOAK_US        8000u  // increase if you want more stress, decrease for faster

const unsigned int a_bus[BUS_SIZE] = { XA0, XA1, XA2, XA3, XA4, XA5 };

// Test bookkeeping
static const char* g_pass_name = "init";
static unsigned long g_pass_start_ms = 0;

// Global progress for soak
static const uint32_t COLS_PER_PASS = (1u << BUS_SIZE); // 64
static const uint32_t PASSES_PER_RUN = 4u;
static const uint32_t TOTAL_COLUMNS = (uint32_t)RUNS * PASSES_PER_RUN * COLS_PER_PASS; // 2560
static uint32_t g_global_col = 0;

// --------------- LED helpers (active-low) ---------------
static inline void red_set(uint8_t level) {
  // level: 0(off) .. 255(full)
  analogWrite(R_LED, (uint8_t)(255 - level)); // invert for active-low
}
static inline void red_off()  { red_set(0); }
static inline void red_dim()  { red_set(RED_RUN_LEVEL); }
static inline void green_off(){ digitalWrite(G_LED, HIGH); }
static inline void green_on() { digitalWrite(G_LED, LOW);  }
static inline void green_pulse(uint16_t on_ms = GREEN_PULSE_ON_MS) {
  digitalWrite(G_LED, LOW);
  delay(on_ms);
  digitalWrite(G_LED, HIGH);
}

static void breathe_red(uint8_t peak = 70, uint8_t step = 6, uint8_t ms = 6) {
  for (uint16_t i = 0; i <= peak; i += step) { red_set((uint8_t)i); delay(ms); }
  for (int16_t  i = peak; i >= 0; i -= step) { red_set((uint8_t)i); delay(ms); }
  red_off();
}

// --------------- DRAM helpers ---------------
void setBus(unsigned int a) {
  for (int i = 0; i < BUS_SIZE; i++) {
    digitalWrite(a_bus[i], (a & 1) ? HIGH : LOW);
    a >>= 1;
  }
}

void writeAddress(unsigned int r, unsigned int c, int v) {
  digitalWrite(CS, LOW);
  setBus(r);
  digitalWrite(RAS, LOW);
  digitalWrite(WE, LOW);
  digitalWrite(DI, (v & 1) ? HIGH : LOW);
  setBus(c);
  digitalWrite(CAS, LOW);
  digitalWrite(WE, HIGH);
  digitalWrite(CAS, HIGH);
  digitalWrite(RAS, HIGH);
}

int readAddress(unsigned int r, unsigned int c) {
  digitalWrite(CS, LOW);
  setBus(r);
  digitalWrite(RAS, LOW);
  setBus(c);
  digitalWrite(CAS, LOW);
  int ret = digitalRead(DO);
  digitalWrite(CAS, HIGH);
  digitalWrite(RAS, HIGH);
  return ret;
}

static void print_addr(unsigned int r, unsigned int c) {
  unsigned long a = ((unsigned long)c << BUS_SIZE) + r;
  Serial.print(" addr=$");
  Serial.print(a, HEX);
  Serial.print(" (row=");
  Serial.print(r);
  Serial.print(", col=");
  Serial.print(c);
  Serial.print(")");
}

// --------------- Soak pause computation ---------------
static inline uint16_t current_soak_us() {
  // progress in [0..TOTAL_COLUMNS-1]
  // Use a gentle curve (quadratic) so early tests are fast and later tests stress more.
  // pause = MAX * (p^2), where p in [0..1]
  const uint32_t denom = (TOTAL_COLUMNS > 1) ? (TOTAL_COLUMNS - 1) : 1;
  uint32_t p_num = g_global_col;            // 0..denom
  // quadratic: (p_num^2)/(denom^2)
  uint64_t num2 = (uint64_t)p_num * (uint64_t)p_num;
  uint64_t den2 = (uint64_t)denom * (uint64_t)denom;
  uint64_t pause = (uint64_t)MAX_SOAK_US * num2 / den2;
  if (pause > 65535u) pause = 65535u;
  return (uint16_t)pause;
}

static inline void soak_pause_tick() {
  uint16_t us = current_soak_us();
  if (us) delayMicroseconds(us);
  if (g_global_col < TOTAL_COLUMNS - 1) g_global_col++;
}

// --------------- Fail/Pass status ---------------
static void fail_halt(unsigned int r, unsigned int c, int expected, int got) {
  Serial.println();
  Serial.print("FAIL in ");
  Serial.print(g_pass_name);
  print_addr(r, c);
  Serial.print(" expected=");
  Serial.print(expected & 1);
  Serial.print(" got=");
  Serial.print(got & 1);
  Serial.print(" elapsed=");
  Serial.print(millis() - g_pass_start_ms);
  Serial.println("ms");

  Serial.print("Soak pause at failure: ");
  Serial.print(current_soak_us());
  Serial.println("us/col (approx)");
  Serial.println("Red = FAIL (double-flash).");
  Serial.flush();

  green_off();
  red_off();
  while (1) {
    red_set(RED_FLASH_LEVEL); delay(70);
    red_off();                delay(80);
    red_set(RED_FLASH_LEVEL); delay(70);
    red_off();                delay(800);
  }
}

static inline void error(unsigned int r, unsigned int c, int expected, int got) {
  interrupts();
  fail_halt(r, c, expected, got);
}

// --------------- Progress + passes ---------------
static void progress_column(int c) {
  const int cols = (1 << BUS_SIZE); // 64

  if ((c % DOT_EVERY_COLS) == 0) {
    Serial.print('.');
    green_pulse(); // dim indication
  }

  if (c == (cols/4)-1)   Serial.print(" 25% ");
  if (c == (cols/2)-1)   Serial.print(" 50% ");
  if (c == (3*cols/4)-1) Serial.print(" 75% ");
  if (c == cols-1)       Serial.print(" 100% ");
}

void fill_constant(int v) {
  v &= 1;
  const int cols = (1 << BUS_SIZE);
  const int rows = (1 << BUS_SIZE);

  for (int c = 0; c < cols; c++) {
    progress_column(c);
    soak_pause_tick();
    for (int r = 0; r < rows; r++) {
      writeAddress(r, c, v);
      int got = readAddress(r, c);
      if (v != got) error(r, c, v, got);
    }
  }
}

void fill_checkerboard(int start_v) {
  int v = start_v & 1;
  const int cols = (1 << BUS_SIZE);
  const int rows = (1 << BUS_SIZE);

  for (int c = 0; c < cols; c++) {
    progress_column(c);
    soak_pause_tick();
    for (int r = 0; r < rows; r++) {
      writeAddress(r, c, v);
      int got = readAddress(r, c);
      if (v != got) error(r, c, v, got);
      v ^= 1;
    }
  }
}

static void begin_pass(const char* name) {
  g_pass_name = name;
  g_pass_start_ms = millis();
  Serial.println();
  Serial.print(name);
  Serial.print(" ");
  Serial.flush();

  // Fun but not bright
  breathe_red(60, 6, 5);

  red_dim();
  green_off();
}

static void end_pass() {
  Serial.print(" done (");
  Serial.print(millis() - g_pass_start_ms);
  Serial.println("ms)");
  Serial.flush();
}

// Simple, dim "run complete" animation
static void between_runs_anim() {
  green_pulse(2); delay(120);
  green_pulse(2); delay(300);
}

// ---------------- Setup / Main ----------------
void setup() {
  Serial.begin(9600);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 1500) { /* wait up to 1.5s */ }

  Serial.println();
  Serial.println("MK4096 DRAM TESTER (4K x 1) for Uno R4 Minima");
  Serial.print("Runs: "); Serial.println(RUNS);
  Serial.println("Patterns per run: checkerboard(0), checkerboard(1), all-0, all-1");
  Serial.println("Soak: per-column pause ramps from 0 to MAX_SOAK_US (quadratic).");
  Serial.print("MAX_SOAK_US = "); Serial.print(MAX_SOAK_US); Serial.println(" us");
  Serial.println("Note: /CS held LOW on socket pin 13. Address bits used: A0..A5.");
  Serial.println();

  for (int i = 0; i < BUS_SIZE; i++) pinMode(a_bus[i], OUTPUT);

  pinMode(CAS, OUTPUT);
  pinMode(RAS, OUTPUT);
  pinMode(WE, OUTPUT);
  pinMode(DI, OUTPUT);
  pinMode(DO, INPUT);

  pinMode(CS, OUTPUT);
  digitalWrite(CS, LOW);

  pinMode(R_LED, OUTPUT);
  pinMode(G_LED, OUTPUT);
  red_off();
  green_off();

  digitalWrite(WE, HIGH);
  digitalWrite(RAS, HIGH);
  digitalWrite(CAS, HIGH);

  // Alive indicator
  breathe_red(60, 6, 6);

  // Prime rows once
  Serial.print("Priming rows (RAS-only refresh sweep) ... ");
  noInterrupts();
  for (int r = 0; r < (1 << BUS_SIZE); r++) {
    setBus(r);
    digitalWrite(RAS, LOW);
    digitalWrite(RAS, HIGH);
  }
  interrupts();
  Serial.println("done.");
}

void loop() {
  unsigned long run_times[RUNS];
  unsigned long all_start = millis();
  g_global_col = 0;

  for (int run = 1; run <= RUNS; run++) {
    Serial.println();
    Serial.print("=== RUN ");
    Serial.print(run);
    Serial.print("/");
    Serial.print(RUNS);
    Serial.println(" ===");

    // Show the current ramp level at start of run
    Serial.print("Soak pause now ~");
    Serial.print(current_soak_us());
    Serial.println(" us/col (increases as test progresses)");
    Serial.flush();

    unsigned long total_start = millis();

    begin_pass("Pass 1/4: checkerboard start=0");
    fill_checkerboard(0);
    end_pass();

    begin_pass("Pass 2/4: checkerboard start=1");
    fill_checkerboard(1);
    end_pass();

    begin_pass("Pass 3/4: solid 0");
    fill_constant(0);
    end_pass();

    begin_pass("Pass 4/4: solid 1");
    fill_constant(1);
    end_pass();

    unsigned long total_ms = millis() - total_start;
    run_times[run - 1] = total_ms;

    Serial.print("RUN ");
    Serial.print(run);
    Serial.print(" time: ");
    Serial.print(total_ms);
    Serial.println("ms");
    Serial.flush();

    red_off();
    between_runs_anim();
  }

  // Summary
  unsigned long total_all_ms = millis() - all_start;
  unsigned long min_ms = run_times[0], max_ms = run_times[0], sum_ms = 0;
  for (int i = 0; i < RUNS; i++) {
    if (run_times[i] < min_ms) min_ms = run_times[i];
    if (run_times[i] > max_ms) max_ms = run_times[i];
    sum_ms += run_times[i];
  }
  unsigned long avg_ms = sum_ms / RUNS;

  Serial.println();
  Serial.println("=== SUMMARY ===");
  Serial.print("Runs: "); Serial.println(RUNS);
  Serial.print("Min: "); Serial.print(min_ms); Serial.println("ms");
  Serial.print("Avg: "); Serial.print(avg_ms); Serial.println("ms");
  Serial.print("Max: "); Serial.print(max_ms); Serial.println("ms");
  Serial.print("Total elapsed: "); Serial.print(total_all_ms); Serial.println("ms");
  Serial.print("Final soak pause ~"); Serial.print(current_soak_us()); Serial.println(" us/col");
  Serial.println("Result: PASS");
  Serial.println("Green = PASS (slow pulse).");
  Serial.flush();

  // PASS indicator loop (dim)
  red_off();
  green_off();
  while (1) {
    green_on();   delay(60);
    green_off();  delay(940);
  }
}
