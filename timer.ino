#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <FastLED.h>

// GPIO 4 for LEDs causes E (81) phy_comm: gpio[1] number: 25 is reserved

// ================== Hardware Config ==================
#define START_BUTTON 19  // done
#define STOP_SENSOR_LEFT 14
#define STOP_SENSOR_RIGHT 33
#define FOOT_SENSOR_LEFT 13
#define FOOT_SENSOR_RIGHT 26
#define AUDIO_PIN 22

// ================== LED Strip Config ==================
#define LED_PIN_LEFT 17
#define LED_PIN_RIGHT 18
#define NUM_LEDS_PER_STRIP 60

CRGB ledsLeft[NUM_LEDS_PER_STRIP];
CRGB ledsRight[NUM_LEDS_PER_STRIP];

// ================== WiFi Config ==================
// const char* ssid = "IPY6J60VPXF0";
// const char* password = "password133";

const char* ssid = "Nacho WiFi";
const char* password = "airforce11";

// Gravitry Worx Wifi
// const char* ssid = "Optus_53BE2F";
// const char* password = "kudzuzerdauRX5r";

// ================== Audio Config ==================
#define LEDC_CHANNEL 0
#define LEDC_RESOLUTION 8

// Audio sequence timing (all in milliseconds)
struct AudioStep {
  int frequency;  // 0 = silence
  unsigned long duration;
};

const AudioStep audioSequence[] = {
  { 0, 500 },    // Silence 500ms
  { 880, 250 },  // 880Hz for 250ms
  { 0, 750 },    // Silence 500ms
  { 880, 250 },  // 880Hz for 250ms
  { 0, 750 },    // Silence 500ms
  { 1760, 150 }  // 1760Hz for 150ms
};

const int AUDIO_SEQUENCE_LENGTH = sizeof(audioSequence) / sizeof(AudioStep);

// False start audio sequence
const AudioStep falseStartSequence[] = {
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 },     // Silence for 100ms
  { 1568, 100 },  // 1568Hz for 100ms
  { 0, 100 }      // Final silence for 100ms
};

const int FALSE_START_SEQUENCE_LENGTH = sizeof(falseStartSequence) / sizeof(AudioStep);

// ================== Global Variables ==================
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
bool isTimerRunningLeft = false;
bool isTimerRunningRight = false;
unsigned long timerStartTime = 0;
unsigned long currentElapsedTime = 0;
bool isPlayingAudio = false;
bool isPlayingFalseStart = false;
bool startButtonPressed = false;
bool stopLeftPressed = false;
bool stopRightPressed = false;
bool footLeftPressed = false;
bool footRightPressed = false;
bool falseStartOccurred = false;
bool leftFalseStart = false;
bool rightFalseStart = false;
bool leftFootValidDuringAudio = true;      // Track if left foot stayed pressed during audio
bool rightFootValidDuringAudio = true;     // Track if right foot stayed pressed during audio
bool falseStartAudioPlayed = false;        // Ensure false start audio only plays once
bool singlePlayerMode = true;              // New: Single player mode toggle
unsigned long audioSequenceStartTime = 0;  // Track when the entire audio sequence started

// STATE Manager variables
bool lastTimerRunning = false;
bool lastPlayingAudio = false;
bool lastPlayingFalseStart = false;
bool lastFalseStartOccurred = false;
bool lastLeftFalseStart = false;
bool lastRightFalseStart = false;
bool lastFootLeftPressed = false;
bool lastFootRightPressed = false;
bool lastSinglePlayerMode = false;
bool lastLeftFinished = false;
bool lastRightFinished = false;
long lastReactionTimeLeft = 0;
long lastReactionTimeRight = 0;
unsigned long lastCompletionTimeLeft = 0;
unsigned long lastCompletionTimeRight = 0;
bool lastTimerRunningLeft = false;
bool lastTimerRunningRight = false;

// Track when false starts occur for negative reaction time calculation
unsigned long leftFalseStartTime = 0;
unsigned long rightFalseStartTime = 0;

unsigned long lastEventTime = 0;           // Track when last significant event occurred
bool resetTimeoutActive = false;           // Flag to indicate if timeout is active
const unsigned long RESET_TIMEOUT = 1300;  // 1.3 seconds in milliseconds

// Tracking isngle player mode
bool leftLaneActive = false;
bool rightLaneActive = false;

// Left side timing - changed to signed long for negative reaction times
long reactionTimeLeft = 0;
unsigned long completionTimeLeft = 0;
bool leftFinished = false;

// Right side timing - changed to signed long for negative reaction times
long reactionTimeRight = 0;
unsigned long completionTimeRight = 0;
bool rightFinished = false;

unsigned long audioEndTime = 0;
unsigned long lastButtonCheck = 0;
unsigned long lastWebSocketUpdate = 0;
const unsigned long BUTTON_DEBOUNCE = 10;
const unsigned long WEBSOCKET_UPDATE_INTERVAL = 50;  // Update every 50ms
unsigned long footPressStartTime = 0;                // When foot sensor was first pressed
bool footHeldForAutoStart = false;                   // Track if we're counting down for auto-start
const unsigned long AUTO_START_DELAY = 3000;         // 3 seconds in milliseconds


// Non-blocking audio sequence state
int currentAudioStep = 0;
unsigned long audioStepStartTime = 0;


//WIFI TIMEOUT
unsigned long startTime = millis();
unsigned long timeout = 30000;  // 1 minute in milliseconds

// ================== LED Functions ==================
void initializeLEDs() {
  FastLED.addLeds<WS2812B, LED_PIN_LEFT, GRB>(ledsLeft, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_PIN_RIGHT, GRB>(ledsRight, NUM_LEDS_PER_STRIP);
  FastLED.setBrightness(128);  // Set brightness to 50%

  // Turn off all LEDs initially
  fill_solid(ledsLeft, NUM_LEDS_PER_STRIP, CRGB::Black);
  fill_solid(ledsRight, NUM_LEDS_PER_STRIP, CRGB::Black);
  FastLED.show();
}

void setLeftLEDs(CRGB color) {
  fill_solid(ledsLeft, NUM_LEDS_PER_STRIP, color);
  FastLED.show();
}

void setRightLEDs(CRGB color) {
  fill_solid(ledsRight, NUM_LEDS_PER_STRIP, color);
  FastLED.show();
}

void turnOffAllLEDs() {
  fill_solid(ledsLeft, NUM_LEDS_PER_STRIP, CRGB::Black);
  fill_solid(ledsRight, NUM_LEDS_PER_STRIP, CRGB::Black);
  FastLED.show();
}

void setup() {
  Serial.begin(115200);

  // Initialize pins
  pinMode(START_BUTTON, INPUT_PULLUP);
  pinMode(STOP_SENSOR_LEFT, INPUT_PULLUP);
  pinMode(STOP_SENSOR_RIGHT, INPUT_PULLUP);
  pinMode(FOOT_SENSOR_LEFT, INPUT_PULLUP);
  pinMode(FOOT_SENSOR_RIGHT, INPUT_PULLUP);

  // Initialize LED strips
  initializeLEDs();

  // Initialize LEDC for audio - using newer ESP32 Arduino Core functions
  ledcAttach(AUDIO_PIN, 1000, LEDC_RESOLUTION);  // Start with 1kHz base frequency
  ledcWrite(AUDIO_PIN, 0);                       // Start with no sound

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection timeout!");
    // Handle timeout - maybe restart, go to AP mode, etc.
  } else {
    Serial.println("\nWiFi connected!");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/start", HTTP_POST, handleApiStart);
  server.on("/api/stop", HTTP_POST, handleApiStop);
  server.on("/api/reset", HTTP_POST, handleApiReset);

  // Setup WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  server.begin();
  Serial.println("HTTP server started");
  Serial.println("WebSocket server started on port 81");
}

void loop() {
  server.handleClient();
  webSocket.loop();
  checkButtons();
  updateAudioSequence();
  updateTimer();
  updateWebSocket();
}

bool isAnyTimerRunning() {
  return isTimerRunningLeft || isTimerRunningRight;
}

void checkButtons() {
  if (millis() - lastButtonCheck < BUTTON_DEBOUNCE) return;

  bool startPressed = !digitalRead(START_BUTTON);
  bool stopLeftNow = !digitalRead(STOP_SENSOR_LEFT);
  bool stopRightNow = !digitalRead(STOP_SENSOR_RIGHT);
  bool footLeftNow = (digitalRead(FOOT_SENSOR_LEFT) == LOW);
  bool footRightNow = (digitalRead(FOOT_SENSOR_RIGHT) == LOW);

  // Check if we're in the "ready" state where foot sensor LED feedback should be active
  bool inReadyState = !isPlayingAudio && !isPlayingFalseStart && !isAnyTimerRunning();

  // During audio sequence, track if either foot sensor is released (false start)
  // MODIFIED: Only check for false starts BEFORE the 1760Hz tone starts (currentAudioStep < 5)
  // Only check for false starts on active sensors based on mode
  if (isPlayingAudio && !isPlayingFalseStart && currentAudioStep < 5) {
    if (singlePlayerMode) {
      // In single player mode, only check the sensor that was initially pressed
      if (footLeftPressed && !footLeftNow && leftFootValidDuringAudio) {
        leftFootValidDuringAudio = false;
        leftFalseStart = true;
        falseStartOccurred = true;
        leftFalseStartTime = millis();
        setLeftLEDs(CRGB::Red);  // Turn on red LEDs for left false start
        resetTimeoutActive = true;
        lastEventTime = millis();
      }
      if (footRightPressed && !footRightNow && rightFootValidDuringAudio) {
        rightFootValidDuringAudio = false;
        rightFalseStart = true;
        falseStartOccurred = true;
        rightFalseStartTime = millis();
        setRightLEDs(CRGB::Red);  // Turn on red LEDs for right false start
        resetTimeoutActive = true;
        lastEventTime = millis();
      }
    } else {
      // Competition mode: check both sensors
      if (!footLeftNow && leftFootValidDuringAudio) {
        leftFootValidDuringAudio = false;
        leftFalseStart = true;
        falseStartOccurred = true;
        leftFalseStartTime = millis();
        setLeftLEDs(CRGB::Red);  // Turn on red LEDs for left false start
      }
      if (!footRightNow && rightFootValidDuringAudio) {
        rightFootValidDuringAudio = false;
        rightFalseStart = true;
        falseStartOccurred = true;
        rightFalseStartTime = millis();
        setRightLEDs(CRGB::Red);  // Turn on red LEDs for right false start
      }
    }
  }

  // Handle foot sensor left state changes
  if (footLeftNow && !footLeftPressed) {
    footLeftPressed = true;

    // NEW: In single player mode, if timer is running and foot sensor is pressed again, reset
    if (singlePlayerMode && isAnyTimerRunning()) {
      if (resetTimeoutActive && (millis() - lastEventTime < RESET_TIMEOUT)) {
        return;  // Ignore foot sensor reset during timeout
      }
      handleApiReset();
      return;  // Exit early after reset
    }

    // In single player mode, start tracking time for auto-start
    if (singlePlayerMode && !isPlayingAudio && !isPlayingFalseStart && !isAnyTimerRunning()) {
      if (!footRightPressed) {  // Only if this is the only foot pressed
        footPressStartTime = millis();
        footHeldForAutoStart = true;
      }
    }

    // LED feedback only during ready state
    if (inReadyState) {
      setLeftLEDs(CRGB::Green);
    }
  } else if (!footLeftNow && footLeftPressed) {
    footLeftPressed = false;

    // Reset auto-start tracking if foot is released
    if (singlePlayerMode && footHeldForAutoStart && !footRightPressed) {
      footHeldForAutoStart = false;
      footPressStartTime = 0;
    }

    // LED feedback only during ready state
    if (inReadyState) {
      setLeftLEDs(CRGB::Black);  // Turn off LEDs
    }

    // MODIFIED: Calculate reaction time whenever foot is released after the 1760Hz tone starts
    // This means currentAudioStep >= 5 OR audio has completely ended
    if ((currentAudioStep >= 5 || audioEndTime > 0) && reactionTimeLeft == 0 && !leftFalseStart) {
      if (audioEndTime > 0) {
        // Audio completely finished - normal reaction time calculation
        reactionTimeLeft = millis() - audioEndTime;
      } else {
        // 1760Hz tone is currently playing - calculate from when it started
        unsigned long tone1760StartTime = audioStepStartTime;  // Time when current step (1760Hz) started
        reactionTimeLeft = millis() - tone1760StartTime;
      }
      sendWebSocketUpdate();
    }
  }

  // Handle foot sensor right state changes
  if (footRightNow && !footRightPressed) {
    footRightPressed = true;

    // NEW: In single player mode, if timer is running and foot sensor is pressed again, reset
    if (singlePlayerMode && isAnyTimerRunning()) {
      if (resetTimeoutActive && (millis() - lastEventTime < RESET_TIMEOUT)) {
        return;  // Ignore foot sensor reset during timeout
      }
      handleApiReset();
      return;  // Exit early after reset
    }

    // In single player mode, start tracking time for auto-start
    if (singlePlayerMode && !isPlayingAudio && !isPlayingFalseStart && !isAnyTimerRunning()) {
      if (!footLeftPressed) {  // Only if this is the only foot pressed
        footPressStartTime = millis();
        footHeldForAutoStart = true;
      }
    }

    // LED feedback only during ready state
    if (inReadyState) {
      setRightLEDs(CRGB::Green);
    }
  } else if (!footRightNow && footRightPressed) {
    footRightPressed = false;

    // Reset auto-start tracking if foot is released
    if (singlePlayerMode && footHeldForAutoStart && !footLeftPressed) {
      footHeldForAutoStart = false;
      footPressStartTime = 0;
    }

    // LED feedback only during ready state
    if (inReadyState) {
      setRightLEDs(CRGB::Black);  // Turn off LEDs
    }

    // MODIFIED: Calculate reaction time whenever foot is released after the 1760Hz tone starts
    // This means currentAudioStep >= 5 OR audio has completely ended
    if ((currentAudioStep >= 5 || audioEndTime > 0) && reactionTimeRight == 0 && !rightFalseStart) {
      if (audioEndTime > 0) {
        // Audio completely finished - normal reaction time calculation
        reactionTimeRight = millis() - audioEndTime;
      } else {
        // 1760Hz tone is currently playing - calculate from when it started
        unsigned long tone1760StartTime = audioStepStartTime;  // Time when current step (1760Hz) started
        reactionTimeRight = millis() - tone1760StartTime;
      }
      sendWebSocketUpdate();
    }
  }

  // Check for auto-start in single player mode
  if (singlePlayerMode && footHeldForAutoStart && !isPlayingAudio && !isPlayingFalseStart && !isAnyTimerRunning()) {
    if (millis() - footPressStartTime >= AUTO_START_DELAY) {
      // Auto-start the sequence
      footHeldForAutoStart = false;
      footPressStartTime = 0;

      // Set which lane is active based on current foot sensor state
      leftLaneActive = footLeftPressed;
      rightLaneActive = footRightPressed;

      // Start the competition automatically
      falseStartOccurred = false;
      leftFalseStart = false;
      rightFalseStart = false;
      leftFootValidDuringAudio = true;
      rightFootValidDuringAudio = true;
      falseStartAudioPlayed = false;
      leftFalseStartTime = 0;
      rightFalseStartTime = 0;
      reactionTimeLeft = 0;
      reactionTimeRight = 0;
      completionTimeLeft = 0;
      completionTimeRight = 0;
      leftFinished = false;
      rightFinished = false;
      startAudioSequence();
    }
  }

  // Handle foot sensor right state changes
  if (footRightNow && !footRightPressed) {
    footRightPressed = true;

    // LED feedback only during ready state
    if (inReadyState) {
      setRightLEDs(CRGB::Green);
    }
  } else if (!footRightNow && footRightPressed) {
    footRightPressed = false;

    // LED feedback only during ready state
    if (inReadyState) {
      setRightLEDs(CRGB::Black);  // Turn off LEDs
    }

    // MODIFIED: Calculate reaction time whenever foot is released after the 1760Hz tone starts
    // This means currentAudioStep >= 5 OR audio has completely ended
    if ((currentAudioStep >= 5 || audioEndTime > 0) && reactionTimeRight == 0 && !rightFalseStart) {
      if (audioEndTime > 0) {
        // Audio completely finished - normal reaction time calculation
        reactionTimeRight = millis() - audioEndTime;
      } else {
        // 1760Hz tone is currently playing - calculate from when it started
        unsigned long tone1760StartTime = audioStepStartTime;  // Time when current step (1760Hz) started
        reactionTimeRight = millis() - tone1760StartTime;
      }
      sendWebSocketUpdate();
    }
  }

  // Handle start button
  if (startPressed && !startButtonPressed) {
    startButtonPressed = true;
    handleStartButton();
  } else if (!startPressed) {
    startButtonPressed = false;
  }

  // Handle stop sensor left
  if (stopLeftNow && !stopLeftPressed) {
    stopLeftPressed = true;
    handleStopSensor(true);  // true for left side
  } else if (!stopLeftNow) {
    stopLeftPressed = false;
  }

  // Handle stop sensor right
  if (stopRightNow && !stopRightPressed) {
    stopRightPressed = true;
    handleStopSensor(false);  // false for right side
  } else if (!stopRightNow) {
    stopRightPressed = false;
  }

  lastButtonCheck = millis();
}

void handleStartButton() {
  // Check if we're in ready-to-start state
  bool canStart = false;

  if (singlePlayerMode) {
    // Single player: allow if at least one foot sensor is pressed
    canStart = (footLeftPressed || footRightPressed) && !isPlayingAudio && !isPlayingFalseStart && !isAnyTimerRunning();

    // Set which lane is active based on which foot sensor is currently pressed
    leftLaneActive = footLeftPressed;
    rightLaneActive = footRightPressed;
  } else {
    // Competition mode: require BOTH foot sensors
    canStart = footLeftPressed && footRightPressed && !isPlayingAudio && !isPlayingFalseStart && !isAnyTimerRunning();

    // Both lanes active in competition mode
    leftLaneActive = true;
    rightLaneActive = true;
  }

  if (canStart) {
    // Start the competition
    falseStartOccurred = false;
    leftFalseStart = false;
    rightFalseStart = false;
    leftFootValidDuringAudio = true;
    rightFootValidDuringAudio = true;
    falseStartAudioPlayed = false;
    leftFalseStartTime = 0;
    rightFalseStartTime = 0;
    reactionTimeLeft = 0;
    reactionTimeRight = 0;
    completionTimeLeft = 0;
    completionTimeRight = 0;
    leftFinished = false;
    rightFinished = false;

    // Clear reset timeout when starting new competition
    resetTimeoutActive = false;
    lastEventTime = 0;

    startAudioSequence();
  } else {
    // Act as reset button in all other states
    handleApiReset();
    if (isPlayingAudio) {
      isPlayingAudio = false;
      currentAudioStep = 0;
      stopTone();
    }
    if (isPlayingFalseStart) {
      isPlayingFalseStart = false;
      currentAudioStep = 0;
      stopTone();
    }
  }
}

void handleStopSensor(bool isLeft) {
  if (isAnyTimerRunning()) {
    // MODIFIED: Always record completion times, regardless of false start status
    unsigned long completionTime = millis() - timerStartTime;

    if (isLeft && !leftFinished) {
      completionTimeLeft = completionTime;
      leftFinished = true;
    } else if (!isLeft && !rightFinished) {
      completionTimeRight = completionTime;
      rightFinished = true;
    }

    if (isLeft) {
      isTimerRunningLeft = false;
    } else {
      isTimerRunningRight = false;
    }

    // In competition mode, keep timers running until both finish
    // In single player mode, stop when the active lane finishes
    if (singlePlayerMode) {
      // Single player: stop when the active participant finishes
      if ((leftLaneActive && leftFinished) || (rightLaneActive && rightFinished)) {
        // Timer for this lane is already stopped above
      }
    } else {
      // Competition mode: only fully stop when both finish
      if (leftFinished && rightFinished) {
        isTimerRunningLeft = false;
        isTimerRunningRight = false;
      }
    }

    // Update elapsed time if all active timers stopped
    if (!isAnyTimerRunning()) {
      currentElapsedTime = millis() - timerStartTime;

      // ADDED: Start reset timeout when race completes
      resetTimeoutActive = true;
      lastEventTime = millis();
    }

    // NEW: Determine winner immediately when first person finishes
    if ((leftFinished || rightFinished) && !falseStartOccurred) {
      // No false starts - first to finish wins
      if (leftFinished && !rightFinished) {
        // Left wins
        setLeftLEDs(CRGB::Green);
        setRightLEDs(CRGB::Red);
      } else if (rightFinished && !leftFinished) {
        // Right wins
        setRightLEDs(CRGB::Green);
        setLeftLEDs(CRGB::Red);
      } else if (leftFinished && rightFinished) {
        // Both finished at same time - compare times
        if (completionTimeLeft < completionTimeRight) {
          setLeftLEDs(CRGB::Green);
          setRightLEDs(CRGB::Red);
        } else if (completionTimeRight < completionTimeLeft) {
          setRightLEDs(CRGB::Green);
          setLeftLEDs(CRGB::Red);
        } else {
          // Tie - both stay green
          setLeftLEDs(CRGB::Green);
          setRightLEDs(CRGB::Green);
        }
      }
    } else if (falseStartOccurred) {
      // Handle winner determination with false starts
      if (!leftFalseStart && !rightFalseStart) {
        // Neither false started - normal winner logic (already handled above)
      } else if (leftFalseStart && !rightFalseStart) {
        // Left false started - right wins if they finish
        if (rightFinished) {
          setRightLEDs(CRGB::Green);
          // Left already red from false start
        }
      } else if (!leftFalseStart && rightFalseStart) {
        // Right false started - left wins if they finish
        if (leftFinished) {
          setLeftLEDs(CRGB::Green);
          // Right already red from false start
        }
      }
      // If both false started, no winner - both stay red
    }

    // Stop timer when both competitors finish (regardless of false starts)
    if (leftFinished && rightFinished) {
      stopTimer();
    }

    sendWebSocketUpdate();
  }
}

void startAudioSequence() {
  isPlayingAudio = true;
  currentAudioStep = 0;
  audioStepStartTime = millis();
  audioSequenceStartTime = millis();  // Record when the entire sequence started

  setLeftLEDs(CRGB::Black);
  setRightLEDs(CRGB::Black);

  // Start first step immediately
  if (audioSequence[0].frequency > 0) {
    playTone(audioSequence[0].frequency);
  } else {
    stopTone();
  }
}

void startFalseStartSequence() {
  isPlayingFalseStart = true;
  currentAudioStep = 0;
  audioStepStartTime = millis();

  // Start first step immediately
  if (falseStartSequence[0].frequency > 0) {
    playTone(falseStartSequence[0].frequency);
  } else {
    stopTone();
  }
}

void updateAudioSequence() {
  if (!isPlayingAudio && !isPlayingFalseStart) return;

  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - audioStepStartTime;

  // Choose the correct sequence based on what's playing
  const AudioStep* sequence = isPlayingAudio ? audioSequence : falseStartSequence;
  int sequenceLength = isPlayingAudio ? AUDIO_SEQUENCE_LENGTH : FALSE_START_SEQUENCE_LENGTH;

  // Check if current step is complete
  if (elapsed >= sequence[currentAudioStep].duration) {
    currentAudioStep++;

    // Check if sequence is complete
    if (currentAudioStep >= sequenceLength) {
      // Sequence complete
      stopTone();
      if (isPlayingAudio) {
        isPlayingAudio = false;
        audioEndTime = millis();  // Record when audio ended
        // Don't turn off LEDs here - let completeAudioSequence() handle LED state
        completeAudioSequence();
      } else if (isPlayingFalseStart) {
        isPlayingFalseStart = false;
        completeFalseStartSequence();
      }
      currentAudioStep = 0;
      return;
    }

    // Start next step
    audioStepStartTime = currentTime;
    if (sequence[currentAudioStep].frequency > 0) {
      playTone(sequence[currentAudioStep].frequency);

      // Handle LED control based on audio frequency
      if (isPlayingAudio) {
        // Main audio sequence: 880Hz and 1760Hz = green, 0Hz = blank
        // MODIFIED: Check for false starts before setting green LEDs
        if (sequence[currentAudioStep].frequency == 880 || sequence[currentAudioStep].frequency == 1760) {
          // Left LED: green if no false start, red if false start occurred
          if (leftFalseStart) {
            setLeftLEDs(CRGB::Red);
          } else {
            setLeftLEDs(CRGB::Green);
          }

          // Right LED: green if no false start, red if false start occurred
          if (rightFalseStart) {
            setRightLEDs(CRGB::Red);
          } else {
            setRightLEDs(CRGB::Green);
          }
        }
      } else if (isPlayingFalseStart) {
        // False start sequence: 1568Hz = flash red on false start lanes only
        if (sequence[currentAudioStep].frequency == 1568) {
          if (leftFalseStart) {
            setLeftLEDs(CRGB::Red);
          }
          if (rightFalseStart) {
            setRightLEDs(CRGB::Red);
          }
          // Keep non-false-start lanes off during flash
          if (!leftFalseStart) {
            setLeftLEDs(CRGB::Black);
          }
          if (!rightFalseStart) {
            setRightLEDs(CRGB::Black);
          }
        }
      }
    } else {
      stopTone();

      // Handle LED control for silence (0Hz)
      if (isPlayingAudio) {
        // MODIFIED: During silence, keep false start lanes red, turn off valid lanes
        if (leftFalseStart) {
          setLeftLEDs(CRGB::Red);  // Keep red for false start
        } else {
          setLeftLEDs(CRGB::Black);  // Turn off for valid competitor
        }

        if (rightFalseStart) {
          setRightLEDs(CRGB::Red);  // Keep red for false start
        } else {
          setRightLEDs(CRGB::Black);  // Turn off for valid competitor
        }
      } else if (isPlayingFalseStart) {
        // False start sequence: silence = turn OFF false start lanes for flashing effect
        if (leftFalseStart) {
          setLeftLEDs(CRGB::Black);  // Turn OFF for flashing
        } else {
          setLeftLEDs(CRGB::Black);
        }
        if (rightFalseStart) {
          setRightLEDs(CRGB::Black);  // Turn OFF for flashing
        } else {
          setRightLEDs(CRGB::Black);
        }
      }
    }
  }
}


void playTone(int frequency) {
  ledcChangeFrequency(AUDIO_PIN, frequency, LEDC_RESOLUTION);
  ledcWrite(AUDIO_PIN, 128);  // 50% duty cycle for square wave
}

void stopTone() {
  ledcWrite(AUDIO_PIN, 0);
}

void completeAudioSequence() {
  isPlayingAudio = false;
  currentAudioStep = 0;
  audioEndTime = millis();  // Record when audio ended

  // MODIFIED: Calculate negative reaction times for false starts
  // Now calculate from when the 1760Hz tone SHOULD have started, not when audio ended
  if (leftFalseStart && leftFalseStartTime > 0) {
    // Calculate the time when 1760Hz tone should have started
    unsigned long tone1760ShouldStart = audioSequenceStartTime + 500 + 250 + 750 + 250 + 750;  // Sum of first 5 steps
    // Calculate how early they started (negative reaction time)
    reactionTimeLeft = (long)leftFalseStartTime - (long)tone1760ShouldStart;  // This will be negative
  }
  if (rightFalseStart && rightFalseStartTime > 0) {
    // Calculate the time when 1760Hz tone should have started
    unsigned long tone1760ShouldStart = audioSequenceStartTime + 500 + 250 + 750 + 250 + 750;  // Sum of first 5 steps
    // Calculate how early they started (negative reaction time)
    reactionTimeRight = (long)rightFalseStartTime - (long)tone1760ShouldStart;  // This will be negative
  }

  // FIXED: Always start timer immediately when audio sequence completes
  startTimer();

  // Play false start beep AFTER starting timer (doesn't delay valid competitor)
  if (falseStartOccurred && !falseStartAudioPlayed) {
    falseStartAudioPlayed = true;
    startFalseStartSequence();
  }

  sendWebSocketUpdate();  // Immediate update when audio completes
}

void completeFalseStartSequence() {
  isPlayingFalseStart = false;
  currentAudioStep = 0;

  // Keep false start lanes red after sequence completes
  if (leftFalseStart) {
    setLeftLEDs(CRGB::Red);
  }
  if (rightFalseStart) {
    setRightLEDs(CRGB::Red);
  }
  // Turn off LEDs for non-false-start lanes
  if (!leftFalseStart) {
    setLeftLEDs(CRGB::Black);
  }
  if (!rightFalseStart) {
    setRightLEDs(CRGB::Black);
  }

  // Timer is already running from when main audio completed - no need to start it again

  // ADDED: Start reset timeout after false start sequence completes
  resetTimeoutActive = true;
  lastEventTime = millis();

  sendWebSocketUpdate();  // Immediate update when false start sequence completes
}

void startTimer() {
  // Set timer running for active lanes
  if (!singlePlayerMode || leftLaneActive) {
    isTimerRunningLeft = true;
  }
  if (!singlePlayerMode || rightLaneActive) {
    isTimerRunningRight = true;
  }

  timerStartTime = millis();
  sendWebSocketUpdate();
}

void stopTimer() {
  isTimerRunningLeft = false;
  isTimerRunningRight = false;
  currentElapsedTime = millis() - timerStartTime;
  sendWebSocketUpdate();
}

void resetTimer() {
  isTimerRunningLeft = false;
  isTimerRunningRight = false;
  currentElapsedTime = 0;
  timerStartTime = 0;
  falseStartOccurred = false;
  leftFalseStart = false;
  rightFalseStart = false;
  leftFootValidDuringAudio = true;
  rightFootValidDuringAudio = true;
  falseStartAudioPlayed = false;
  leftFalseStartTime = 0;
  rightFalseStartTime = 0;
  reactionTimeLeft = 0;
  reactionTimeRight = 0;
  completionTimeLeft = 0;
  completionTimeRight = 0;
  leftFinished = false;
  rightFinished = false;
  audioEndTime = 0;

  // IMPORTANT: Reset lane activity tracking
  leftLaneActive = false;
  rightLaneActive = false;

  // ADDED: Clear reset timeout when manually resetting
  resetTimeoutActive = false;
  lastEventTime = 0;

  // IMPORTANT: Reset all the last state tracking variables
  lastTimerRunningLeft = false;
  lastTimerRunningRight = false;
  lastPlayingAudio = false;
  lastPlayingFalseStart = false;
  lastFalseStartOccurred = false;
  lastLeftFalseStart = false;
  lastRightFalseStart = false;
  lastFootLeftPressed = footLeftPressed;    // Keep current foot state
  lastFootRightPressed = footRightPressed;  // Keep current foot state
  lastSinglePlayerMode = singlePlayerMode;  // Keep current mode
  lastLeftFinished = false;
  lastRightFinished = false;
  lastReactionTimeLeft = 0;
  lastReactionTimeRight = 0;
  lastCompletionTimeLeft = 0;
  lastCompletionTimeRight = 0;

  // Turn off all LEDs when reset
  turnOffAllLEDs();

  sendWebSocketUpdate();
}

void updateTimer() {
  if (isAnyTimerRunning()) {
    currentElapsedTime = millis() - timerStartTime;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client #%u disconnected\n", num);
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("WebSocket client #%u connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        // Send current status to new client
        sendWebSocketUpdate();
      }
      break;

    case WStype_TEXT:
      {
        String command = String((char*)payload);
        Serial.printf("WebSocket received: %s\n", command.c_str());

        if (command == "start") {
          bool canStart = false;

          if (singlePlayerMode) {
            // Single player: allow if at least one foot sensor is pressed
            canStart = (footLeftPressed || footRightPressed) && !isPlayingAudio && !isPlayingFalseStart;
          } else {
            // Competition mode: require BOTH foot sensors
            canStart = footLeftPressed && footRightPressed && !isPlayingAudio && !isPlayingFalseStart;
          }

          if (canStart) {
            falseStartOccurred = false;
            leftFalseStart = false;
            rightFalseStart = false;
            leftFootValidDuringAudio = true;
            rightFootValidDuringAudio = true;
            falseStartAudioPlayed = false;
            leftFalseStartTime = 0;   // Reset false start times
            rightFalseStartTime = 0;  // Reset false start times
            reactionTimeLeft = 0;
            reactionTimeRight = 0;
            completionTimeLeft = 0;
            completionTimeRight = 0;
            leftFinished = false;
            rightFinished = false;
            startAudioSequence();
          }
        } else if (command == "stop") {
          if (isAnyTimerRunning()) {
            stopTimer();
          }
        } else if (command == "reset") {
          resetTimer();
          if (isPlayingAudio) {
            isPlayingAudio = false;
            currentAudioStep = 0;
            stopTone();
          }
          if (isPlayingFalseStart) {
            isPlayingFalseStart = false;
            currentAudioStep = 0;
            stopTone();
          }
        } else if (command == "toggle_mode") {
          singlePlayerMode = !singlePlayerMode;
        }
        sendWebSocketUpdate();  // Send immediate update after command
      }
      break;

    default:
      break;
  }
}

void updateWebSocket() {
  unsigned long currentTime = millis();

  // Always check foot sensor changes for GUI feedback
  bool footSensorChanged = (footLeftPressed != lastFootLeftPressed || footRightPressed != lastFootRightPressed);

  // Check timer state changes per lane
  bool timerStateChanged = (isTimerRunningLeft != lastTimerRunningLeft || isTimerRunningRight != lastTimerRunningRight);

  // Check other relevant state changes per active lane
  bool leftLaneStateChanged = (!singlePlayerMode || leftLaneActive) && (leftFalseStart != lastLeftFalseStart || leftFinished != lastLeftFinished || reactionTimeLeft != lastReactionTimeLeft || completionTimeLeft != lastCompletionTimeLeft);

  bool rightLaneStateChanged = (!singlePlayerMode || rightLaneActive) && (rightFalseStart != lastRightFalseStart || rightFinished != lastRightFinished || reactionTimeRight != lastReactionTimeRight || completionTimeRight != lastCompletionTimeRight);

  bool otherStateChanged = (isPlayingAudio != lastPlayingAudio || isPlayingFalseStart != lastPlayingFalseStart || falseStartOccurred != lastFalseStartOccurred || singlePlayerMode != lastSinglePlayerMode);

  // Send updates based on what's actually running or changed
  bool shouldUpdate = false;

  if (singlePlayerMode) {
    shouldUpdate = (leftLaneActive && isTimerRunningLeft) || (rightLaneActive && isTimerRunningRight) || isPlayingAudio || isPlayingFalseStart || footSensorChanged || timerStateChanged || leftLaneStateChanged || rightLaneStateChanged || otherStateChanged;
  } else {
    shouldUpdate = isAnyTimerRunning() || isPlayingAudio || isPlayingFalseStart || footSensorChanged || timerStateChanged || leftLaneStateChanged || rightLaneStateChanged || otherStateChanged;
  }

  if (shouldUpdate) {
    sendWebSocketUpdate();

    // Update last known state
    lastTimerRunningLeft = isTimerRunningLeft;
    lastTimerRunningRight = isTimerRunningRight;
    lastPlayingAudio = isPlayingAudio;
    lastPlayingFalseStart = isPlayingFalseStart;
    lastFalseStartOccurred = falseStartOccurred;
    lastLeftFalseStart = leftFalseStart;
    lastRightFalseStart = rightFalseStart;
    lastFootLeftPressed = footLeftPressed;
    lastFootRightPressed = footRightPressed;
    lastSinglePlayerMode = singlePlayerMode;
    lastLeftFinished = leftFinished;
    lastRightFinished = rightFinished;
    lastReactionTimeLeft = reactionTimeLeft;
    lastReactionTimeRight = reactionTimeRight;
    lastCompletionTimeLeft = completionTimeLeft;
    lastCompletionTimeRight = completionTimeRight;

    lastWebSocketUpdate = currentTime;
  }
}

void sendWebSocketUpdate() {
  // Always get fresh timer value when sending update
  if (isAnyTimerRunning()) {
    currentElapsedTime = millis() - timerStartTime;
  }

  DynamicJsonDocument doc(1024);

  doc["is_timer_running"] = isAnyTimerRunning();
  doc["is_playing_audio"] = isPlayingAudio;
  doc["is_playing_false_start"] = isPlayingFalseStart;
  doc["false_start_occurred"] = falseStartOccurred;
  doc["left_false_start"] = leftFalseStart;
  doc["right_false_start"] = rightFalseStart;
  doc["single_player_mode"] = singlePlayerMode;

  // Foot sensor states
  doc["foot_left_pressed"] = footLeftPressed;
  doc["foot_right_pressed"] = footRightPressed;
  doc["both_feet_ready"] = footLeftPressed && footRightPressed;
  doc["ready_to_start"] = singlePlayerMode ? (footLeftPressed || footRightPressed) : (footLeftPressed && footRightPressed);

  // Timing data
  doc["elapsed_time"] = currentElapsedTime;
  doc["formatted_time"] = formatTime(currentElapsedTime);

  // Left side data (including negative reaction times)
  doc["reaction_time_left"] = reactionTimeLeft;
  doc["formatted_reaction_time_left"] = formatSignedTime(reactionTimeLeft);
  doc["completion_time_left"] = completionTimeLeft;
  doc["formatted_completion_time_left"] = formatTime(completionTimeLeft);
  doc["left_finished"] = leftFinished;

  // Right side data (including negative reaction times)
  doc["reaction_time_right"] = reactionTimeRight;
  doc["formatted_reaction_time_right"] = formatSignedTime(reactionTimeRight);
  doc["completion_time_right"] = completionTimeRight;
  doc["formatted_completion_time_right"] = formatTime(completionTimeRight);
  doc["right_finished"] = rightFinished;

  doc["uptime"] = millis();

  String message;
  serializeJson(doc, message);

  webSocket.broadcastTXT(message);
}

String formatTime(unsigned long milliseconds) {
  unsigned long totalSeconds = milliseconds / 1000;
  unsigned long ms = milliseconds % 1000;
  unsigned long seconds = totalSeconds % 60;
  unsigned long minutes = totalSeconds / 60;

  char timeStr[16];
  sprintf(timeStr, "%lu:%02lu.%03lu", minutes, seconds, ms);
  return String(timeStr);
}

// Format signed time for reaction times (can be negative for false starts)
String formatSignedTime(long milliseconds) {
  if (milliseconds == 0) {
    return "0:00.000";
  }

  bool isNegative = milliseconds < 0;
  unsigned long absMilliseconds = abs(milliseconds);

  unsigned long totalSeconds = absMilliseconds / 1000;
  unsigned long ms = absMilliseconds % 1000;
  unsigned long seconds = totalSeconds % 60;
  unsigned long minutes = totalSeconds / 60;

  char timeStr[17];  // Extra space for minus sign
  if (isNegative) {
    sprintf(timeStr, "-%lu:%02lu.%03lu", minutes, seconds, ms);
  } else {
    sprintf(timeStr, "%lu:%02lu.%03lu", minutes, seconds, ms);
  }
  return String(timeStr);
}

// ================== Web Server Handlers ==================

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Gravity Worx Speed Timer</title>
  <meta charset="UTF-8">
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    * {
      box-sizing: border-box;
    }
    
    body {
      font-family: Arial, sans-serif;
      max-width: 800px;
      margin: 0 auto;
      padding: 10px;
      background: linear-gradient(135deg, #43B75C 0%, #2E8B57 100%);
      color: white;
      min-height: 100vh;
    }
    
    .container {
      background: rgba(255,255,255,0.1);
      padding: 15px;
      border-radius: 15px;
      backdrop-filter: blur(10px);
      box-shadow: 0 8px 32px rgba(0,0,0,0.1);
    }
    
    .competition-layout {
      display: flex;
      flex-direction: column;
      gap: 15px;
      margin: 20px 0;
    }
    
    .climber-panel {
      background: linear-gradient(135deg, rgba(255,255,255,0.1), rgba(255,255,255,0.05));
      padding: 20px;
      border-radius: 15px;
      text-align: center;
      border: 1px solid rgba(255,255,255,0.2);
      box-shadow: 0 8px 25px rgba(0,0,0,0.15);
    }
    
    .timer-display {
      font-size: 2.5em;
      font-weight: bold;
      margin: 20px 0;
      text-shadow: 3px 3px 6px rgba(0,0,0,0.6);
      background: linear-gradient(135deg, rgba(255,255,255,0.15), rgba(255,255,255,0.08));
      padding: 20px;
      border-radius: 15px;
      border: 1px solid rgba(255,255,255,0.3);
      box-shadow: inset 0 2px 10px rgba(0,0,0,0.2);
      word-break: break-all;
    }
    
    .status {
      text-align: center;
      margin: 20px 0;
      font-size: 1.1em;
      padding: 15px;
      border-radius: 10px;
      background: rgba(0,100,0,0.7);
      border: 2px solid rgba(255,255,255,0.2);
      box-shadow: 0 4px 15px rgba(0,0,0,0.2);
      word-wrap: break-word;
    }
    
    .running { 
      background: linear-gradient(135deg, rgba(0,150,0,0.8), rgba(0,100,0,0.9));
      animation: pulse-green 2s ease-in-out infinite alternate;
    }
    .stopped { 
      background: rgba(0,100,0,0.7);
    }
    .playing { 
      background: linear-gradient(135deg, rgba(255,193,7,0.8), rgba(255,152,0,0.8));
      animation: pulse-yellow 1s ease-in-out infinite alternate;
    }
    .false-start { 
      background: linear-gradient(135deg, rgba(220,38,38,0.8), rgba(185,28,28,0.9));
      border: 2px solid #dc2626;
      animation: pulse-red 0.5s ease-in-out infinite alternate;
    }
    
    @keyframes pulse-green {
      from { box-shadow: 0 4px 15px rgba(0,150,0,0.4); }
      to { box-shadow: 0 6px 25px rgba(0,150,0,0.7); }
    }
    @keyframes pulse-yellow {
      from { box-shadow: 0 4px 15px rgba(255,193,7,0.4); }
      to { box-shadow: 0 6px 25px rgba(255,193,7,0.7); }
    }
    @keyframes pulse-red {
      from { box-shadow: 0 4px 15px rgba(220,38,38,0.6); }
      to { box-shadow: 0 8px 30px rgba(220,38,38,0.9); }
    }
    
    .disqualified { 
      opacity: 0.8; 
      background: rgba(220,38,38,0.2) !important;
      border: 2px solid #dc2626 !important;
    }
    
    .foot-sensor {
      padding: 12px;
      border-radius: 8px;
      font-weight: bold;
      margin: 12px 0;
      border: 1px solid rgba(255,255,255,0.2);
      transition: all 0.3s ease;
      font-size: 0.9em;
    }
    .foot-pressed { 
      background: linear-gradient(135deg, rgba(34,197,94,0.4), rgba(22,163,74,0.5)); 
      box-shadow: 0 4px 15px rgba(34,197,94,0.3);
    }
    .foot-released { 
      background: linear-gradient(135deg, rgba(239,68,68,0.4), rgba(220,38,38,0.5)); 
      box-shadow: 0 4px 15px rgba(239,68,68,0.3);
    }
    
    .reaction-time, .completion-time {
      padding: 10px;
      border-radius: 8px;
      margin: 8px 0;
      font-size: 0.85em;
      border: 1px solid rgba(255,255,255,0.2);
      transition: all 0.3s ease;
    }
    .reaction-time { 
      background: linear-gradient(135deg, rgba(251,191,36,0.3), rgba(245,158,11,0.4)); 
      box-shadow: 0 3px 12px rgba(251,191,36,0.2);
    }
    .reaction-time.negative { 
      background: linear-gradient(135deg, rgba(220,38,38,0.4), rgba(185,28,28,0.5)); 
      color: #ff8a80; 
      font-weight: bold; 
      box-shadow: 0 3px 12px rgba(220,38,38,0.4);
    }
    .completion-time { 
      background: linear-gradient(135deg, rgba(34,197,94,0.3), rgba(22,163,74,0.4)); 
      box-shadow: 0 3px 12px rgba(34,197,94,0.2);
    }
    
    .winner { 
      background: rgba(255,215,0,0.3) !important; 
      border: 2px solid gold;
      animation: winner-glow 1s ease-in-out infinite alternate;
    }
    @keyframes winner-glow {
      from { box-shadow: 0 0 10px rgba(255,215,0,0.5); }
      to { box-shadow: 0 0 20px rgba(255,215,0,0.8); }
    }
    
    button {
      background: linear-gradient(135deg, rgba(255,255,255,0.25), rgba(255,255,255,0.15));
      color: white;
      border: 2px solid rgba(255,255,255,0.4);
      padding: 12px 20px;
      margin: 6px;
      border-radius: 12px;
      cursor: pointer;
      font-size: 14px;
      font-weight: bold;
      transition: all 0.3s ease;
      text-shadow: 0 1px 2px rgba(0,0,0,0.3);
      box-shadow: 0 4px 15px rgba(0,0,0,0.2);
      min-height: 44px; /* Touch-friendly minimum */
      width: 100%;
      max-width: 200px;
    }
    button:hover { 
      background: linear-gradient(135deg, rgba(255,255,255,0.35), rgba(255,255,255,0.25)); 
      transform: translateY(-2px); 
      box-shadow: 0 6px 20px rgba(0,0,0,0.3);
    }
    button:disabled { 
      opacity: 0.4; 
      cursor: not-allowed; 
      transform: none; 
      box-shadow: 0 2px 8px rgba(0,0,0,0.1);
    }
    
    .button-group { 
      text-align: center; 
      margin: 20px 0;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 10px;
    }
    
    .mode-toggle {
      background: rgba(255,255,255,0.15);
      border: 2px solid rgba(255,255,255,0.4);
      padding: 12px 24px;
      border-radius: 30px;
      font-size: 14px;
      font-weight: bold;
      position: relative;
      overflow: hidden;
      transition: all 0.4s ease;
      text-shadow: 0 1px 2px rgba(0,0,0,0.4);
      width: 100%;
      max-width: 240px;
    }
    .mode-toggle.single-mode {
      background: linear-gradient(135deg, #ff6b6b, #ff8e8e);
      border-color: #ff4757;
      box-shadow: 0 6px 20px rgba(255, 107, 107, 0.5);
    }
    .mode-toggle.competition-mode {
      background: linear-gradient(135deg, #4CAF50, #66BB6A);
      border-color: #45A049;
      box-shadow: 0 6px 20px rgba(76, 175, 80, 0.5);
    }
    .mode-toggle:hover {
      transform: translateY(-2px) scale(1.02);
      box-shadow: 0 8px 25px rgba(255,255,255,0.3);
    }
    
    h1 { 
      text-align: center; 
      margin-bottom: 20px; 
      text-shadow: 2px 2px 4px rgba(0,0,0,0.5);
      font-size: 1.8em;
    }
    
    h2 { 
      margin-top: 0;
      font-size: 1.3em;
    }
    
    .instructions { 
      background: linear-gradient(135deg, rgba(255,255,255,0.15), rgba(255,255,255,0.08)); 
      padding: 15px; 
      border-radius: 12px; 
      margin: 20px 0; 
      font-size: 0.85em;
      border: 1px solid rgba(255,255,255,0.25);
      box-shadow: 0 6px 20px rgba(0,0,0,0.15);
      line-height: 1.4;
    }
    
    /* Desktop view - side by side layout */
    @media (min-width: 768px) {
      body {
        padding: 20px;
      }
      
      .container {
        padding: 30px;
      }
      
      .competition-layout {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 30px;
      }
      
      .status {
        grid-column: 1 / -1;
        font-size: 1.2em;
      }
      
      .button-group {
        grid-column: 1 / -1;
        flex-direction: row;
        justify-content: center;
      }
      
      .instructions {
        grid-column: 1 / -1;
        font-size: 0.9em;
      }
      
      .timer-display {
        font-size: 3.6em;
        padding: 25px;
      }
      
      .climber-panel {
        padding: 25px;
      }
      
      h1 {
        font-size: 2.2em;
        margin-bottom: 30px;
      }
      
      h2 {
        font-size: 1.5em;
      }
      
      button {
        width: auto;
        padding: 15px 30px;
        font-size: 16px;
      }
      
      .mode-toggle {
        width: auto;
      }
    }
    
    /* Large desktop view */
    @media (min-width: 1024px) {
      .timer-display {
        font-size: 3.6em;
      }
      
      h1 {
        font-size: 2.5em;
      }
    }
    
    /* Very small mobile devices */
    @media (max-width: 360px) {
      body {
        padding: 5px;
      }
      
      .container {
        padding: 10px;
      }
      
      .timer-display {
        font-size: 2em;
        padding: 15px;
        margin: 15px 0;
      }
      
      h1 {
        font-size: 1.5em;
      }
      
      h2 {
        font-size: 1.1em;
      }
      
      .instructions {
        font-size: 0.8em;
        padding: 12px;
      }
      
      button {
        font-size: 13px;
        padding: 10px 16px;
      }
      
      .climber-panel {
        padding: 15px;
      }
    }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Gravity Worx Speed Timer</h1>
    
    <div class='competition-layout'>
      <div id='status' class='status stopped'>System Ready - Both Climbers Place Feet</div>
      
      <div class='climber-panel left-panel'>
        <h2>LEFT CLIMBER</h2>
        <div id='timer-left' class='timer-display'>0:00.000</div>
        <div id='foot-status-left' class='foot-sensor foot-released'>Foot Sensor: None</div>
        <div id='reaction-time-left' class='reaction-time' style='display:none'>Reaction: 0:00.000</div>
        <div id='completion-time-left' class='completion-time' style='display:none'>Time: 0:00.000</div>
      </div>
      
      <div class='climber-panel right-panel'>
        <h2>RIGHT CLIMBER</h2>
        <div id='timer-right' class='timer-display'>0:00.000</div>
        <div id='foot-status-right' class='foot-sensor foot-released'>Foot Sensor: None</div>
        <div id='reaction-time-right' class='reaction-time' style='display:none'>Reaction: 0:00.000</div>
        <div id='completion-time-right' class='completion-time' style='display:none'>Time: 0:00.000</div>
      </div>
      
      <div class='button-group'>
        <button onclick='startSequence()' id='startBtn'>🚀 Start Competition</button>
        <button onclick='resetTimer()' id='resetBtn'>🔄 Reset</button>
        <button onclick='toggleMode()' id='modeBtn' class='mode-toggle competition-mode'>🏆 Competition Mode</button>
      </div>
      
      <div class='instructions'>
        <strong id='instructions-title'>Single Player Instructions:</strong><br>
        <span id='instructions-text'>
          1. Press and hold <b>ONE</b> foot sensor<br>
          2. Press Start to begin audio countdown or <b>stand on foot sensor for 3 seconds</b><br>
          3. Keep foot sensor pressed during entire audio countdown<br>
          4. Start climbing on the high picthed start tone<br>
          5. Hit your stop sensor when you reach the top<br>
        </span>
      </div>
    </div>
  </div>

  <script>
    let ws;
    function connectWebSocket() {
      const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
      ws = new WebSocket(protocol + '://' + location.hostname + ':81');
      ws.onopen = function() { console.log('WebSocket connected'); };
      ws.onmessage = function(event) {
        const data = JSON.parse(event.data);
        
        // Update individual timers - show completion time if finished, otherwise current timer
        const leftTimer = document.getElementById('timer-left');
        const rightTimer = document.getElementById('timer-right');
        
        // LEFT TIMER - only update if foot was pressed initially or in competition mode
        if(!data.single_player_mode || data.left_finished || data.left_false_start || data.reaction_time_left != 0 || (data.is_timer_running && data.foot_left_pressed)) {
          if(data.left_finished && data.completion_time_left > 0) {
            leftTimer.textContent = data.formatted_completion_time_left;
            leftTimer.style.background = 'rgba(34,197,94,0.2)'; // Green background when finished
          } else if(data.left_false_start) {
            leftTimer.textContent = 'FALSE START';
            leftTimer.style.background = 'rgba(220,38,38,0.3)'; // Red background for false start
          } else {
            leftTimer.textContent = data.formatted_time;
            leftTimer.style.background = 'rgba(255,255,255,0.1)'; // Default background
          }
        } else {
          // In single player mode and left foot not active - show inactive state
          leftTimer.textContent = '0:00.000';
          leftTimer.style.background = 'rgba(100,100,100,0.2)'; // Gray background for inactive
        }
        
        // RIGHT TIMER - only update if foot was pressed initially or in competition mode
        if(!data.single_player_mode || data.right_finished || data.right_false_start || data.reaction_time_right != 0 || (data.is_timer_running && data.foot_right_pressed)) {
          if(data.right_finished && data.completion_time_right > 0) {
            rightTimer.textContent = data.formatted_completion_time_right;
            rightTimer.style.background = 'rgba(34,197,94,0.2)'; // Green background when finished
          } else if(data.right_false_start) {
            rightTimer.textContent = 'FALSE START';
            rightTimer.style.background = 'rgba(220,38,38,0.3)'; // Red background for false start
          } else {
            rightTimer.textContent = data.formatted_time;
            rightTimer.style.background = 'rgba(255,255,255,0.1)'; // Default background
          }
        } else {
          // In single player mode and right foot not active - show inactive state
          rightTimer.textContent = '0:00.000';
          rightTimer.style.background = 'rgba(100,100,100,0.2)'; // Gray background for inactive
        }
        
        // Update foot sensor status and ready state based on mode
        const footLeftDiv = document.getElementById('foot-status-left');
        const footRightDiv = document.getElementById('foot-status-right');
        
        if(data.foot_left_pressed) {
          footLeftDiv.textContent = 'Foot Sensor: Pressed';
          footLeftDiv.className = 'foot-sensor foot-pressed';
        } else {
          footLeftDiv.textContent = 'Foot Sensor: None';
          footLeftDiv.className = 'foot-sensor foot-released';
        }
        
        if(data.foot_right_pressed) {
          footRightDiv.textContent = 'Foot Sensor: Pressed';
          footRightDiv.className = 'foot-sensor foot-pressed';
        } else {
          footRightDiv.textContent = 'Foot Sensor: None';
          footRightDiv.className = 'foot-sensor foot-released';
        }
        
        // Update mode button display
        const modeBtn = document.getElementById('modeBtn');
        if(data.single_player_mode) {
          modeBtn.textContent = '👤 Single Player Mode';
          modeBtn.className = 'mode-toggle single-mode';
        } else {
          modeBtn.textContent = '🏆 Competition Mode';
          modeBtn.className = 'mode-toggle competition-mode';
        }
        
        // Update instructions based on mode
        const instructionsTitle = document.getElementById('instructions-title');
        const instructionsText = document.getElementById('instructions-text');
        if(data.single_player_mode) {
          instructionsTitle.textContent = 'Single Player Instructions:';
          instructionsText.innerHTML = `
            1. Press and hold <b>ONE</b> foot sensor<br>
            2. Press Start to begin audio countdown or <b>stand on foot sensor for 3 seconds</b><br>
            3. Keep foot sensor pressed during entire audio countdown<br>
            4. Start climbing on the high picthed start tone<br>
            5. Hit your stop sensor when you reach the top<br>
          `;
        } else {
          instructionsTitle.textContent = 'Competition Instructions:';
          instructionsText.innerHTML = `
            1. Both climbers press and hold foot sensors<br>
            2. Press Start to begin audio countdown<br>
            3. Keep foot sensors pressed during entire audio countdown<br>
            4. Start climbing on the high picthed start tone<br>
            5. Hit your stop sensor when you reach the top<br>
            <strong>Both climbers can still finish even if one false starts</strong>
          `;
        }
        
        // Update reaction times (including negative ones for false starts)
        const reactionLeftDiv = document.getElementById('reaction-time-left');
        const reactionRightDiv = document.getElementById('reaction-time-right');
        
        // Show reaction time if it exists (not 0) OR if audio has ended (ready to show)
        // BUT only for active sensors in single player mode
        if((!data.single_player_mode || data.left_finished || data.left_false_start || data.reaction_time_left != 0) && 
           (data.reaction_time_left != 0 || (data.elapsed_time > 0 && !data.is_playing_audio))) {
          if(data.reaction_time_left != 0) {
            reactionLeftDiv.textContent = 'Reaction: ' + data.formatted_reaction_time_left;
            // Add special styling for negative reaction times (false starts)
            if(data.reaction_time_left < 0) {
              reactionLeftDiv.className = 'reaction-time negative';
            } else {
              reactionLeftDiv.className = 'reaction-time';
            }
          } else {
            reactionLeftDiv.textContent = 'Reaction: Waiting...';
            reactionLeftDiv.className = 'reaction-time';
          }
          reactionLeftDiv.style.display = 'block';
        } else {
          reactionLeftDiv.style.display = 'none';
        }
        
        if((!data.single_player_mode || data.right_finished || data.right_false_start || data.reaction_time_right != 0) && 
           (data.reaction_time_right != 0 || (data.elapsed_time > 0 && !data.is_playing_audio))) {
          if(data.reaction_time_right != 0) {
            reactionRightDiv.textContent = 'Reaction: ' + data.formatted_reaction_time_right;
            // Add special styling for negative reaction times (false starts)
            if(data.reaction_time_right < 0) {
              reactionRightDiv.className = 'reaction-time negative';
            } else {
              reactionRightDiv.className = 'reaction-time';
            }
          } else {
            reactionRightDiv.textContent = 'Reaction: Waiting...';
            reactionRightDiv.className = 'reaction-time';
          }
          reactionRightDiv.style.display = 'block';
        } else {
          reactionRightDiv.style.display = 'none';
        }
        
        // Update completion times and winner highlighting
        const completionLeftDiv = document.getElementById('completion-time-left');
        const completionRightDiv = document.getElementById('completion-time-right');
        const leftPanel = document.querySelector('.left-panel');
        const rightPanel = document.querySelector('.right-panel');
        
        // Remove winner and disqualified classes first
        leftPanel.classList.remove('winner', 'disqualified');
        rightPanel.classList.remove('winner', 'disqualified');
        
        // Mark false start competitors as disqualified (but still show their completion time)
        if(data.left_false_start) {
          leftPanel.classList.add('disqualified');
        }
        if(data.right_false_start) {
          rightPanel.classList.add('disqualified');
        }
        
        if(data.completion_time_left > 0) {
          if(data.left_false_start) {
            completionLeftDiv.textContent = 'FINISH: ' + data.formatted_completion_time_left + ' (DQ)';
            completionLeftDiv.style.background = 'rgba(220,38,38,0.3)';
          } else {
            completionLeftDiv.textContent = 'FINISH: ' + data.formatted_completion_time_left;
            completionLeftDiv.style.background = 'rgba(34,197,94,0.2)';
          }
          completionLeftDiv.style.display = 'block';
        } else {
          completionLeftDiv.style.display = 'none';
        }
        
        if(data.completion_time_right > 0) {
          if(data.right_false_start) {
            completionRightDiv.textContent = 'FINISH: ' + data.formatted_completion_time_right + ' (DQ)';
            completionRightDiv.style.background = 'rgba(220,38,38,0.3)';
          } else {
            completionRightDiv.textContent = 'FINISH: ' + data.formatted_completion_time_right;
            completionRightDiv.style.background = 'rgba(34,197,94,0.2)';
          }
          completionRightDiv.style.display = 'block';
        } else {
          completionRightDiv.style.display = 'none';
        }
        
        // Highlight winner only among non-disqualified competitors
        if(!data.left_false_start && !data.right_false_start) {
          // Both competitors valid
          if(data.completion_time_left > 0 && data.completion_time_right > 0) {
            if(data.completion_time_left < data.completion_time_right) {
              leftPanel.classList.add('winner');
            } else if(data.completion_time_right < data.completion_time_left) {
              rightPanel.classList.add('winner');
            }
          } else if(data.completion_time_left > 0) {
            leftPanel.classList.add('winner');
          } else if(data.completion_time_right > 0) {
            rightPanel.classList.add('winner');
          }
        } else if(data.left_false_start && !data.right_false_start) {
          // Left disqualified, right wins if finished
          if(data.completion_time_right > 0) {
            rightPanel.classList.add('winner');
          }
        } else if(!data.left_false_start && data.right_false_start) {
          // Right disqualified, left wins if finished
          if(data.completion_time_left > 0) {
            leftPanel.classList.add('winner');
          }
        }
        // If both false started, no winner highlighting
        
        // Update status
        const statusDiv = document.getElementById('status');
        const startBtn = document.getElementById('startBtn');

        if(data.is_playing_false_start) {
          let falseStartMsg = 'FALSE START!';
          if(data.left_false_start && data.right_false_start) {
            falseStartMsg = 'FALSE START - BOTH COMPETITORS!';
          } else if(data.left_false_start) {
            falseStartMsg = 'FALSE START - LEFT COMPETITOR!';
          } else if(data.right_false_start) {
            falseStartMsg = 'FALSE START - RIGHT COMPETITOR!';
          }
          statusDiv.textContent = falseStartMsg;
          statusDiv.className = 'status false-start';
          startBtn.disabled = true;
        } else if(data.is_playing_audio) {
          statusDiv.textContent = 'Audio Sequence Playing - Keep Feet Pressed!';
          statusDiv.className = 'status playing';
          startBtn.disabled = true; 
        } else if(data.is_timer_running) {
          let runningMsg = 'CLIMB! Timer Running';
          if(data.false_start_occurred) {
            if(data.left_false_start && data.right_false_start) {
              runningMsg = 'CLIMB! (Both False Started)';
            } else if(data.left_false_start) {
              runningMsg = 'CLIMB! (Left False Started)';
            } else if(data.right_false_start) {
              runningMsg = 'CLIMB! (Right False Started)';
            }
          }
          statusDiv.textContent = runningMsg;
          statusDiv.className = 'status running';
          startBtn.disabled = true;
        } else {
          if(data.false_start_occurred) {
            let falseStartMsg = 'False Start Occurred';
            if(data.left_false_start && data.right_false_start) {
              falseStartMsg = 'Both Competitors False Started';
            } else if(data.left_false_start) {
              falseStartMsg = 'Left Competitor False Started';
            } else if(data.right_false_start) {
              falseStartMsg = 'Right Competitor False Started';
            }
            statusDiv.textContent = falseStartMsg + ' - Reset to Try Again';
            statusDiv.className = 'status false-start';
          } else if(data.completion_time_left > 0 || data.completion_time_right > 0) {
            statusDiv.textContent = 'Competition Complete!';
            statusDiv.className = 'status stopped';
          } else {
            statusDiv.textContent = data.ready_to_start ? 'Ready to Start!' : (data.single_player_mode ? 'Waiting for One Foot Sensor' : 'Waiting for Both Foot Sensors');
            statusDiv.className = 'status stopped';
          }
          startBtn.disabled = !data.ready_to_start;
        }
      };
      ws.onclose = function() { console.log('WebSocket disconnected'); setTimeout(connectWebSocket, 3000); };
      ws.onerror = function(error) { console.log('WebSocket error:', error); };
    }

    function startSequence() { ws.send('start'); }
    function resetTimer() { ws.send('reset'); }
    function toggleMode() { ws.send('toggle_mode'); }

    connectWebSocket();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleApiStatus() {
  DynamicJsonDocument doc(1024);

  doc["is_timer_running"] = isAnyTimerRunning();
  doc["is_playing_audio"] = isPlayingAudio;
  doc["is_playing_false_start"] = isPlayingFalseStart;
  doc["false_start_occurred"] = falseStartOccurred;
  doc["left_false_start"] = leftFalseStart;
  doc["right_false_start"] = rightFalseStart;
  doc["single_player_mode"] = singlePlayerMode;

  // Foot sensor states
  doc["foot_left_pressed"] = footLeftPressed;
  doc["foot_right_pressed"] = footRightPressed;
  doc["both_feet_ready"] = footLeftPressed && footRightPressed;
  doc["ready_to_start"] = singlePlayerMode ? (footLeftPressed || footRightPressed) : (footLeftPressed && footRightPressed);

  // Timing data
  doc["elapsed_time"] = currentElapsedTime;
  doc["formatted_time"] = formatTime(currentElapsedTime);

  // Left side data (including negative reaction times)
  doc["reaction_time_left"] = reactionTimeLeft;
  doc["formatted_reaction_time_left"] = formatSignedTime(reactionTimeLeft);
  doc["completion_time_left"] = completionTimeLeft;
  doc["formatted_completion_time_left"] = formatTime(completionTimeLeft);
  doc["left_finished"] = leftFinished;

  // Right side data (including negative reaction times)
  doc["reaction_time_right"] = reactionTimeRight;
  doc["formatted_reaction_time_right"] = formatSignedTime(reactionTimeRight);
  doc["completion_time_right"] = completionTimeRight;
  doc["formatted_completion_time_right"] = formatTime(completionTimeRight);
  doc["right_finished"] = rightFinished;

  doc["uptime"] = millis();

  String response;
  serializeJson(doc, response);

  server.send(200, "application/json", response);
}

void handleApiStart() {
  bool canStart = false;

  if (singlePlayerMode) {
    // Single player: allow if at least one foot sensor is pressed
    canStart = (footLeftPressed || footRightPressed) && !isPlayingAudio && !isPlayingFalseStart;
  } else {
    // Competition mode: require BOTH foot sensors
    canStart = footLeftPressed && footRightPressed && !isPlayingAudio && !isPlayingFalseStart;
  }

  if (canStart) {
    falseStartOccurred = false;
    leftFalseStart = false;
    rightFalseStart = false;
    leftFootValidDuringAudio = true;
    rightFootValidDuringAudio = true;
    falseStartAudioPlayed = false;
    leftFalseStartTime = 0;   // Reset false start times
    rightFalseStartTime = 0;  // Reset false start times
    reactionTimeLeft = 0;
    reactionTimeRight = 0;
    completionTimeLeft = 0;
    completionTimeRight = 0;
    leftFinished = false;
    rightFinished = false;
    startAudioSequence();
    server.send(200, "application/json", "{\"status\":\"started\"}");
  } else if (singlePlayerMode && !footLeftPressed && !footRightPressed) {
    server.send(400, "application/json", "{\"error\":\"at least one foot sensor must be pressed in single player mode\"}");
  } else if (!singlePlayerMode && (!footLeftPressed || !footRightPressed)) {
    server.send(400, "application/json", "{\"error\":\"both foot sensors must be pressed in competition mode\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"audio already playing\"}");
  }
}

void handleApiStop() {
  if (isAnyTimerRunning()) {
    stopTimer();
    server.send(200, "application/json", "{\"status\":\"stopped\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"timer not running\"}");
  }
}

void handleApiReset() {
  resetTimer();
  if (isPlayingAudio) {
    isPlayingAudio = false;
    currentAudioStep = 0;
    stopTone();
  }
  if (isPlayingFalseStart) {
    isPlayingFalseStart = false;
    currentAudioStep = 0;
    stopTone();
  }
  sendWebSocketUpdate();
  server.send(200, "application/json", "{\"status\":\"reset\"}");
}
