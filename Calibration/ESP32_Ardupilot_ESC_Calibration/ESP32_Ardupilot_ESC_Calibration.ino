// GUIDED ESC CALIBRATION for 4 ESCs - no ArduPilot.
// Arduino-ESP32 core 3.x, board "ESP32 Dev Module".
// Serial Monitor: 115200 baud, line ending "Newline". Press Enter in the input box to answer.
// To start over at any time: unplug the battery, press the ESP32's EN/RST button.

const int ESC_PINS[4] = {25, 27, 33, 32};   // motor 1-4 pins from your hwdef

void setPulse(int us) {
  for (int i = 0; i < 4; i++) {
    ledcWrite(ESC_PINS[i], (uint32_t)us * 65535 / 20000);   // 50 Hz, pulse width in us
  }
  Serial.printf("   [signal to ESCs = %d us]\n", us);
}

void waitEnter() {
  while (Serial.available()) Serial.read();               // clear old input
  while (!Serial.available()) delay(10);                  // wait for Enter
  while (Serial.available()) Serial.read();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  for (int i = 0; i < 4; i++) ledcAttach(ESC_PINS[i], 50, 16);
  setPulse(1000);                                         // safe start

  Serial.println("\n=========== ESC CALIBRATION ===========");
  Serial.println("STEP 1: Check ALL of these:");
  Serial.println("  - PROPS OFF");
  Serial.println("  - Battery UNPLUGGED");
  Serial.println("  - ESC signal wires on IO25, IO27, IO33, IO32");
  Serial.println("  - ESC black/brown signal ground wires on ESP32 GND");
  Serial.println("  - ESC red wires NOT connected (ESP32 runs from USB)");
  Serial.println(">> Press Enter when ready.");
  waitEnter();

  setPulse(2000);                                         // full throttle BEFORE the ESCs get power
  Serial.println("\nSTEP 2: Sending FULL throttle now.");
  Serial.println(">> PLUG IN THE BATTERY NOW.");
  Serial.println("   You will hear: startup tune, then 'beep-beep'.");
  Serial.println(">> Press Enter AS SOON AS you hear 'beep-beep' (within 2-3 seconds!).");
  waitEnter();

  setPulse(1000);                                         // minimum throttle -> ESC stores the range
  Serial.println("\nSTEP 3: Sending MINIMUM throttle now.");
  Serial.println("   You should hear: 3 beeps (3S battery), then 1 long beep = CALIBRATED.");
  Serial.println("   If you hear a repeating series of DIFFERENT tones instead, you were too slow:");
  Serial.println("   unplug the battery, press EN/RST, and start again.");
  Serial.println(">> Press Enter after the long beep.");
  waitEnter();

  Serial.println("\nSTEP 4: Test. Motors WILL SPIN (props must be OFF).");
  Serial.println(">> Press Enter to start the test.");
  waitEnter();
  int steps[] = {1100, 1200, 1300, 1400, 1500};
  for (int i = 0; i < 5; i++) {
    setPulse(steps[i]);
    Serial.println("   all 4 motors should be spinning, and getting faster");
    delay(2000);
  }
  setPulse(1000);                                         // stop motors

  Serial.println("\nSTEP 5: DONE. Motors stopped.");
  Serial.println(">> UNPLUG THE BATTERY.");
  Serial.println("   The ESCs keep this calibration. You can flash ArduPilot back now.");
}

void loop() {
  // nothing - the signal stays at 1000 us (motors stopped)
}