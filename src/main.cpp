#include <Arduino.h>
#include <M5StickCPlus2.h>

// ── Display (portrait) ────────────────────────────────────────
#define W 135   // rotation 0 → 135 wide
#define H 240   //              240 tall

// ── Grid ──────────────────────────────────────────────────────
#define CELL 2
#define COLS 67    // 67*2 = 134 px wide
#define ROWS 114   // 114*2 = 228 px; grid y 0..227
                   // separator y 229; HUD y 230..239

static uint8_t grid[ROWS][COLS];
static uint8_t next[ROWS][COLS];
static uint8_t age[ROWS][COLS];

// ── State ─────────────────────────────────────────────────────
static uint32_t turingCycle = 0;
static uint8_t  extCount     = 0;   // consecutive gens with < 8 cells
static int      patternIndex = 0;   // BtnA pattern cycle 0..3

static const uint16_t SPEEDS[] = {80, 120, 200};
static uint8_t  speedIdx     = 0;
static uint16_t currentSpeed = 80;

// ── Toast overlay ─────────────────────────────────────────────
static char     toastText[32] = "";
static uint32_t toastUntil    = 0;

// ── Patterns (no PROGMEM) ─────────────────────────────────────
static const int8_t GLIDER[5][2]      = {{0,1},{1,2},{2,0},{2,1},{2,2}};
static const int8_t R_PENTOMINO[5][2] = {{0,1},{0,2},{1,0},{1,1},{2,1}};
static const int8_t ACORN[7][2]       = {{0,1},{1,3},{2,0},{2,1},{2,4},{2,5},{2,6}};
// Classic period-3 pulsar, 48 cells, centered offsets (-6..+6)
static const int8_t PULSAR[48][2] = {
    {-6,-4},{-6,-3},{-6,-2},{-6,2},{-6,3},{-6,4},
    {-4,-6},{-4,-1},{-4,1},{-4,6},
    {-3,-6},{-3,-1},{-3,1},{-3,6},
    {-2,-6},{-2,-1},{-2,1},{-2,6},
    {-1,-4},{-1,-3},{-1,-2},{-1,2},{-1,3},{-1,4},
    {1,-4},{1,-3},{1,-2},{1,2},{1,3},{1,4},
    {2,-6},{2,-1},{2,1},{2,6},
    {3,-6},{3,-1},{3,1},{3,6},
    {4,-6},{4,-1},{4,1},{4,6},
    {6,-4},{6,-3},{6,-2},{6,2},{6,3},{6,4},
};

// ── Color ─────────────────────────────────────────────────────
inline uint16_t ageToColor(uint8_t a) {
    if (a == 0)  return 0x07FF;   // cyan
    if (a <= 4)  return 0x07E0;   // bright green
    if (a <= 15) return 0x0640;   // mid green
    if (a <= 34) return 0x0300;   // dark green
    return             0xFD40;   // amber (a >= 35)
}

// ── Toast ─────────────────────────────────────────────────────
void showToast(const char* text, uint32_t durationMs) {
    strncpy(toastText, text, 31);
    toastText[31] = '\0';
    toastUntil = millis() + durationMs;
}

// Portrait toast: narrow (135 px) so long strings wrap to two lines,
// split at the last space that fits. Self-contained write block;
// grid pixels behind it recover on the next drawDiff() pass.
void drawToast() {
    if (millis() >= toastUntil) return;
    auto& D = StickCP2.Display;

    const int boxX = 4, boxW = 127;   // inner width ~123 px → ~20 chars/line
    const int maxChars = 20;
    const int len = (int)strlen(toastText);

    char l1[24] = "", l2[24] = "";
    if (len <= maxChars) {
        strncpy(l1, toastText, 23); l1[23] = '\0';
    } else {
        int sp = -1;
        for (int i = 0; i < len && i <= maxChars; i++)
            if (toastText[i] == ' ') sp = i;
        if (sp < 0) sp = maxChars;          // no space → hard split
        int n1 = (sp > 23) ? 23 : sp;
        strncpy(l1, toastText, n1); l1[n1] = '\0';
        int start = (toastText[sp] == ' ') ? sp + 1 : sp;
        strncpy(l2, toastText + start, 23); l2[23] = '\0';
    }

    const bool twoLines = (l2[0] != '\0');
    const int  boxH = twoLines ? 24 : 14;
    const int  boxY = H/2 - boxH/2;

    D.startWrite();
    D.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
    D.drawRect(boxX, boxY, boxW, boxH, 0x4208);
    D.setTextSize(1);
    D.setTextColor(TFT_WHITE, TFT_BLACK);
    if (twoLines) {
        D.setCursor(boxX + (boxW - (int)strlen(l1)*6)/2, boxY + 4);  D.print(l1);
        D.setCursor(boxX + (boxW - (int)strlen(l2)*6)/2, boxY + 14); D.print(l2);
    } else {
        D.setCursor(boxX + (boxW - (int)strlen(l1)*6)/2, boxY + 4);  D.print(l1);
    }
    D.endWrite();
}

// ── Core functions ────────────────────────────────────────────
int measureMass() {
    int n = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            n += grid[r][c];
    return n;
}

void bigBang() {
    randomSeed(esp_random());
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            grid[r][c] = (random(100) < 32) ? 1 : 0;
            age[r][c]  = 0;
        }
    turingCycle = 0;
    extCount    = 0;
}

void drawFull() {
    StickCP2.Display.startWrite();
    StickCP2.Display.fillRect(0, 0, COLS*CELL, ROWS*CELL, TFT_BLACK);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (grid[r][c])
                StickCP2.Display.fillRect(
                    c*CELL, r*CELL, CELL, CELL, ageToColor(age[r][c]));
    StickCP2.Display.endWrite();
}

void cosmicReset() {
    bigBang();
    drawFull();
}

void advanceTuringMachine() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    n += grid[(r+dr+ROWS)%ROWS][(c+dc+COLS)%COLS];
                }
            next[r][c] = grid[r][c]
                ? (n == 2 || n == 3 ? 1 : 0)
                : (n == 3           ? 1 : 0);
            // age updated here so drawDiff can compare old vs new bracket
            if (next[r][c] && grid[r][c])
                age[r][c] = (age[r][c] < 255) ? age[r][c] + 1 : 255;
            else
                age[r][c] = 0;
        }
    }
}

// Called AFTER advanceTuringMachine(), BEFORE memcpy().
// age[] is already updated; for survivors old age = age[r][c]-1.
void drawDiff() {
    StickCP2.Display.startWrite();
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const bool alive    = next[r][c];
            const bool wasAlive = grid[r][c];
            if (alive != wasAlive) {
                StickCP2.Display.fillRect(c*CELL, r*CELL, CELL, CELL,
                    alive ? ageToColor(age[r][c]) : TFT_BLACK);
            } else if (alive && age[r][c] > 0) {
                if (ageToColor(age[r][c] - 1) != ageToColor(age[r][c]))
                    StickCP2.Display.fillRect(c*CELL, r*CELL, CELL, CELL,
                        ageToColor(age[r][c]));
            }
        }
    }
    StickCP2.Display.endWrite();
}

// HUD strip — clean, two values. D.printf crashes on nano-newlib;
// use snprintf+print everywhere.
void drawHUD() {
    auto& D = StickCP2.Display;
    D.startWrite();
    D.drawFastHLine(0, 229, W, 0x2104);       // dark separator above HUD
    D.fillRect(0, 230, W, 10, TFT_BLACK);     // clear strip (y 230..239)
    D.setTextSize(1);
    D.setTextColor(0x4208, TFT_BLACK);

    // LEFT: generation counter
    char buf[16];
    snprintf(buf, sizeof(buf), "T:%05lu", (unsigned long)turingCycle);
    D.setCursor(2, 231);
    D.print(buf);

    // RIGHT: density F:X.XX (integer arithmetic, no float printf)
    const int total = ROWS * COLS;
    const int pct   = (measureMass() * 100) / total;
    buf[0]='F'; buf[1]=':';
    buf[2]='0' + pct / 100;
    buf[3]='.';
    buf[4]='0' + (pct % 100) / 10;
    buf[5]='0' + pct % 10;
    buf[6]='\0';
    D.setCursor(W - 36 - 3, 231);   // right-aligned 6-char field
    D.print(buf);

    D.endWrite();
}

// ── Pattern injection ─────────────────────────────────────────
void injectPattern(int idx) {
    const int cr = ROWS / 2, cc = COLS / 2;
    const int8_t (*pat)[2];
    int len;
    const char* msg;
    switch (idx) {
        case 0:  pat = GLIDER;      len = 5;  msg = "GLIDER - the traveler";         break;
        case 1:  pat = R_PENTOMINO; len = 5;  msg = "R-PENTOMINO - 1103 generations";break;
        case 2:  pat = ACORN;       len = 7;  msg = "ACORN - grows for 5206 gen";    break;
        default: pat = PULSAR;      len = 48; msg = "PULSAR - period 3 oscillator";  break;
    }
    for (int i = 0; i < len; i++) {
        int r = (cr + pat[i][0] + ROWS) % ROWS;
        int c = (cc + pat[i][1] + COLS) % COLS;
        grid[r][c] = 1;
    }
    drawFull();
    showToast(msg, 1800);
}

// ── Milestones ────────────────────────────────────────────────
void checkMilestone() {
    char buf[32];
    if      (turingCycle == 500)  showToast("500 generations survived", 1500);
    else if (turingCycle == 1000) showToast("a thousand cycles of life", 1500);
    else if (turingCycle == 2000) showToast("Conway would be proud", 1500);
    else if (turingCycle == 5000) showToast("still running. remarkable.", 1500);
    else if (turingCycle > 5000 && turingCycle % 5000 == 0) {
        snprintf(buf, sizeof(buf), "cycle %lu - entropy resists",
                 (unsigned long)turingCycle);
        showToast(buf, 1500);
    }
}

// ── Buttons ───────────────────────────────────────────────────
void handleButtons() {
    if (StickCP2.BtnA.wasReleasedAfterHold()) {
        showToast("COSMIC RESET", 1500);   // shown before reset executes
        cosmicReset();
    } else if (StickCP2.BtnA.wasPressed()) {
        injectPattern(patternIndex);
        patternIndex = (patternIndex + 1) % 4;
    }

    if (StickCP2.BtnB.wasPressed()) {
        speedIdx     = (speedIdx + 1) % 3;
        currentSpeed = SPEEDS[speedIdx];
        char buf[16];
        snprintf(buf, sizeof(buf), "SPEED: %ums", (unsigned)currentSpeed);
        showToast(buf, 1000);
    }
}

// ── Splash screen (portrait) ──────────────────────────────────
void showSplash() {
    auto& D = StickCP2.Display;
    D.fillScreen(TFT_BLACK);
    D.startWrite();

    D.setTextSize(1); D.setTextColor(0x4208, TFT_BLACK);
    D.setCursor((W - 21*6)/2, 40); D.print("Conway's Game of Life");

    // Title stacked (TextSize 2 won't fit on one portrait line)
    D.setTextSize(2); D.setTextColor(TFT_WHITE, TFT_BLACK);
    D.setCursor((W - 8*12)/2, 70); D.print("AUTOMATA");
    D.setCursor((W - 5*12)/2, 90); D.print("VITAE");

    D.drawFastHLine(10, 120, W - 20, 0x4208);

    // Quote, wrapped for narrow width
    D.setTextSize(1); D.setTextColor(0xAD55, TFT_BLACK);
    D.setCursor((W - 17*6)/2, 135); D.print("\"Any sufficiently");
    D.setCursor((W - 14*6)/2, 147); D.print("complex system");
    D.setCursor((W - 15*6)/2, 159); D.print("becomes alive.\"");

    D.setTextColor(0x4208, TFT_BLACK);
    D.setCursor((W - 19*6)/2, 210); D.print("- J.H. Conway, 1970");

    D.endWrite();
    delay(2500);
}

// ── Boot countdown ────────────────────────────────────────────
void showBootSequence() {
    auto& D = StickCP2.Display;
    D.setTextSize(1);
    D.setTextColor(TFT_WHITE, TFT_BLACK);

    D.startWrite();
    D.fillScreen(TFT_BLACK);
    D.setCursor((W - 15*6)/2, H/2 - 4);   // "initializing..." = 15 chars
    D.print("initializing...");
    D.endWrite();
    delay(500);

    D.startWrite();
    D.fillScreen(TFT_BLACK);
    D.setCursor((W - 19*6)/2, H/2 - 4);   // "let there be light." = 19 chars
    D.print("let there be light.");
    D.endWrite();
    delay(500);

    D.startWrite();
    D.fillScreen(TFT_BLACK);
    D.endWrite();
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    StickCP2.begin(M5.config());
    StickCP2.Display.setRotation(0);   // portrait
    StickCP2.Display.setBrightness(180);

    showSplash();
    showBootSequence();

    bigBang();
    drawFull();
    drawHUD();
    showToast("BIG BANG", 1500);
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
    StickCP2.update();
    handleButtons();
    advanceTuringMachine();
    drawDiff();
    memcpy(grid, next, sizeof(grid));
    turingCycle++;

    checkMilestone();

    // Auto-reset: < 8 cells for 40 consecutive generations
    if (measureMass() < 8) {
        if (++extCount >= 40) {
            showToast("EXTINCTION - restarting", 1200);
            drawToast();          // render now, before the blocking hold
            delay(1200);
            cosmicReset();
            return;
        }
    } else {
        extCount = 0;
    }

    drawHUD();
    drawToast();
    delay(currentSpeed);
}
