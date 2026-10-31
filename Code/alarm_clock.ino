#include <Adafruit_GFX.h> // graphics library
#include <Adafruit_ST7789.h> // driver for the ST7789 screen
#include <SPI.h> 

// Defining pins for the display, change according to your setup!!! Uses the white numbers on the ESP
#define TFT_SCLK D9
#define TFT_MOSI D10
#define TFT_RST D8
#define TFT_DC D4
#define TFT_CS D5
#define TFT_BL D6

// Fix setColRowStart() by exposing it via a subclass
class MyST7789 : public Adafruit_ST7789 {
public:
  MyST7789(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst)
    : Adafruit_ST7789(cs, dc, mosi, sclk, rst) {}
  void setOffsets(uint8_t col, uint8_t row) {
    _colstart = _colstart2 = col;
    _rowstart = _rowstart2 = row;
  }
};

MyST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// setup() runs ONCE when the board powers on
void setup() {
  Serial.begin(115200); // lets the board talk to your computer

  pinMode(TFT_BL, OUTPUT); // Set the backlight pin mode, or just wire it to 3.3V
  digitalWrite(TFT_BL, LOW); // Turns the backlight ON, for some reason this screen is active Low, so setting it to LOW is really HIGH

  tft.init(76, 284); // Our panel size (portrait)
  tft.setOffsets(82, 18); // Offsets for the weird resolution
  tft.invertDisplay(false); // Invert the colors (This display is flipped from normal)
  tft.setRotation(1); // Landscape, if it's upside down use 3!
  Serial.println("TFT Initialized!");

  tft.fillScreen(ST77XX_BLACK); // clear the screen

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(6);
  tft.setCursor(0,0); // Where the text is drawn, 0,0 is top left
  tft.print(42); // Show whatever you want! Draws from the top left of the text/number/shape 
}

// loop() runs OVER and OVER, forever
void loop() {
  // SW1 = mode, SW2 = up, SW3 = down, SW4 = alarm on/off (and stops the alarm)
  // buttons go to GND so they read LOW when pressed
  const int buttonPins[4] = {D3, D2, D1, D0};
  const int buzzerPin = D7;

  // static so they keep their values every time loop() runs
  static bool firstRun = true;
  static int hrs = 12, mins = 0, secs = 0;
  static int alarmHrs = 7, alarmMins = 30;
  static bool alarmOn = false;
  static bool ringing = false;
  static bool beepOn = false;
  static unsigned long ringStart = 0;
  static unsigned long lastTick = 0;
  static unsigned long lastPressTime = 0;
  static bool wasDown[4] = {false, false, false, false};
  static int mode = 0;  // 0 = clock, 1 = set hour, 2 = set min, 3 = alarm hour, 4 = alarm min
  static bool needRedraw = true;

  // runs once, like a mini setup
  if (firstRun) {
    for (int i = 0; i < 4; i++) {
      pinMode(buttonPins[i], INPUT_PULLUP);
    }
    pinMode(buzzerPin, OUTPUT);
    tft.fillScreen(ST77XX_BLACK);  // gets rid of the 42 from before
    lastTick = millis();
    firstRun = false;
  }

  // ---------- buttons ----------
  bool pressed[4] = {false, false, false, false};
  for (int i = 0; i < 4; i++) {
    bool down = (digitalRead(buttonPins[i]) == LOW);
    // only counts the moment it goes down, and ignores bouncing
    if (down && !wasDown[i] && millis() - lastPressTime > 150) {
      pressed[i] = true;
      lastPressTime = millis();
    }
    wasDown[i] = down;
  }

  // if the alarm is going off, any button shuts it up
  if (ringing) {
    if (pressed[0] || pressed[1] || pressed[2] || pressed[3]) {
      ringing = false;
      beepOn = false;
      noTone(buzzerPin);
      needRedraw = true;
      for (int i = 0; i < 4; i++) pressed[i] = false;  // dont let it do anything else
    }
  }

  // mode button, goes through the 5 modes then back to the clock
  if (pressed[0]) {
    mode++;
    if (mode > 4) mode = 0;
    needRedraw = true;
  }

  // up and down buttons
  int change = 0;
  if (pressed[1]) change = 1;
  if (pressed[2]) change = -1;

  if (change != 0 && mode != 0) {
    if (mode == 1) hrs = (hrs + change + 24) % 24;
    if (mode == 2) mins = (mins + change + 60) % 60;
    if (mode == 3) alarmHrs = (alarmHrs + change + 24) % 24;
    if (mode == 4) alarmMins = (alarmMins + change + 60) % 60;

    // if we changed the actual time, start the seconds over
    if (mode == 1 || mode == 2) {
      secs = 0;
      lastTick = millis();
    }
    needRedraw = true;
  }

  // alarm on/off
  if (pressed[3]) {
    alarmOn = !alarmOn;
    needRedraw = true;
  }

  // ---------- keeping time ----------
  // adds 1000 to lastTick instead of using millis() again so it doesnt drift
  while (millis() - lastTick >= 1000) {
    lastTick += 1000;
    secs++;
    if (secs >= 60) {
      secs = 0;
      mins++;
      if (mins >= 60) {
        mins = 0;
        hrs++;
        if (hrs >= 24) hrs = 0;
      }
    }
    needRedraw = true;

    // check the alarm
    if (alarmOn && hrs == alarmHrs && mins == alarmMins && secs == 0) {
      ringing = true;
      ringStart = millis();
    }
  }

  // ---------- buzzer ----------
  if (ringing) {
    if (millis() - ringStart > 60000) {
      // gives up after a minute
      ringing = false;
      beepOn = false;
      noTone(buzzerPin);
      needRedraw = true;
    } else {
      // beeps on and off every 250ms
      bool shouldBeep = ((millis() / 250) % 2 == 0);
      if (shouldBeep != beepOn) {
        beepOn = shouldBeep;
        if (beepOn) tone(buzzerPin, 2000);
        else noTone(buzzerPin);
      }
    }
  }

  // ---------- drawing ----------
  // only redraws when something changed, and the text has a black background
  // so it writes over the old numbers without needing fillScreen (no flicker)
  if (needRedraw) {
    needRedraw = false;

    // the big HH:MM
    tft.setTextSize(5);
    tft.setCursor(0, 0);
    tft.setTextColor(mode == 1 ? ST77XX_YELLOW : ST77XX_WHITE, ST77XX_BLACK);
    tft.printf("%02d", hrs);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(":");
    tft.setTextColor(mode == 2 ? ST77XX_YELLOW : ST77XX_WHITE, ST77XX_BLACK);
    tft.printf("%02d", mins);

    // seconds, smaller
    tft.setTextSize(3);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(165, 14);
    tft.printf("%02d", secs);

    // bottom line, alarm info (or wake up message)
    tft.setTextSize(2);
    tft.setCursor(0, 55);
    if (ringing) {
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
      tft.print("WAKE UP!         ");
    } else {
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print("Alarm ");
      tft.setTextColor(mode == 3 ? ST77XX_YELLOW : ST77XX_WHITE, ST77XX_BLACK);
      tft.printf("%02d", alarmHrs);
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print(":");
      tft.setTextColor(mode == 4 ? ST77XX_YELLOW : ST77XX_WHITE, ST77XX_BLACK);
      tft.printf("%02d", alarmMins);
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print(alarmOn ? " ON   " : " OFF  ");
    }
  }

  delay(10);  // small break so the loop isnt going crazy
}
