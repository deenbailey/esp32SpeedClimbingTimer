#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <FastLED.h>
#include <DNSServer.h>

// Hardware Config
#define START_BUTTON 19

#define FOOT_SENSOR_LEFT 13
#define STOP_SENSOR_LEFT_KIDS 14
#define STOP_SENSOR_LEFT 27

#define FOOT_SENSOR_RIGHT 32
#define STOP_SENSOR_RIGHT_KIDS 26
#define STOP_SENSOR_RIGHT 33


#define AUDIO_PIN 22

// LED Strip Config
#define LED_PIN_LEFT 17
#define LED_PIN_RIGHT 18
#define NUM_LEDS_PER_STRIP 60

CRGB ledsLeft[NUM_LEDS_PER_STRIP];
CRGB ledsRight[NUM_LEDS_PER_STRIP];

// KID MODE STUFF
bool kidsModeSensorsEnabled = true;
bool stopLeftKidsPressed = false;
bool stopRightKidsPressed = false;
bool lastKidsModeSensorsEnabled = true;

bool friendlyFalseStartsEnabled = false;

// WiFi Config
const char* ap_ssid = "Gravity Worx Speed Timer";
const char* ap_password = "";  // Min 8 chars, or "" for open network
DNSServer dnsServer;
const byte DNS_PORT = 53;

//loop check
unsigned long loopCount = 0;
unsigned long lastTime = 0;
unsigned long lastLoopTime = 0;

// Audio Config
#define LEDC_CHANNEL 0
#define LEDC_RESOLUTION 8

struct AudioStep {
  int frequency;
  unsigned long duration;
};

const AudioStep audioSequence[] = {
  { 0, 500 }, { 880, 250 }, { 0, 750 }, { 880, 250 }, { 0, 750 }, { 1760, 150 }
};

const AudioStep falseStartSequence[] = {
  { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }
};

const int AUDIO_SEQUENCE_LENGTH = sizeof(audioSequence) / sizeof(AudioStep);
const int FALSE_START_SEQUENCE_LENGTH = sizeof(falseStartSequence) / sizeof(AudioStep);

// Global Variables
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Timer state
bool isTimerRunningLeft = false;
bool isTimerRunningRight = false;
unsigned long timerStartTime = 0;
unsigned long currentElapsedTime = 0;

// Audio state
bool isPlayingAudio = false;
bool isPlayingFalseStart = false;
int currentAudioStep = 0;
unsigned long audioStepStartTime = 0;
unsigned long audioSequenceStartTime = 0;
unsigned long audioEndTime = 0;

// Button/sensor state
bool startButtonPressed = false;
bool stopLeftPressed = false;
bool stopRightPressed = false;
bool footLeftPressed = false;
bool footRightPressed = false;

// False start tracking
unsigned long startSignalTime = 0;
long leftReactionTime = 0;
long rightReactionTime = 0;
bool leftReactionCalculated = false;
bool rightReactionCalculated = false;
bool falseStartOccurred = false;
bool leftFalseStart = false;
bool rightFalseStart = false;
bool leftFootValidDuringAudio = true;
bool rightFootValidDuringAudio = true;
bool falseStartAudioPlayed = false;
unsigned long leftFalseStartTime = 0;
unsigned long rightFalseStartTime = 0;
const unsigned long FALSE_START_REACTION_TIME_CUTOFF = 100;  //if reaction is under this time a false start is detected - 100ms

// Mode and lane tracking
bool singlePlayerMode = true;
bool leftLaneActive = false;
bool rightLaneActive = false;

// Timing results
long reactionTimeLeft = 0;
long reactionTimeRight = 0;
unsigned long completionTimeLeft = 0;
unsigned long completionTimeRight = 0;
bool leftFinished = false;
bool rightFinished = false;

// State tracking for updates
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

// Timing control
unsigned long lastEventTime = 0;
bool resetTimeoutActive = false;
const unsigned long RESET_TIMEOUT = 1300;
unsigned long lastButtonCheck = 0;
unsigned long lastWebSocketUpdate = 0;
const unsigned long BUTTON_DEBOUNCE = 10;
const unsigned long WEBSOCKET_UPDATE_INTERVAL = 111;

// Auto-start
unsigned long footPressStartTime = 0;
bool footHeldForAutoStart = false;
const unsigned long AUTO_START_DELAY = 3000;

// WiFi timeout
unsigned long startTime = millis();
unsigned long timeout = 30000;

void handleCaptivePortal() {
  // Redirect all requests to the main page for captive portal
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
  handleRoot();
}

// Utility Functions
bool isAnyTimerRunning() {
  return isTimerRunningLeft || isTimerRunningRight;
}

bool isReadyState() {
  return !isPlayingAudio && !isPlayingFalseStart && !isAnyTimerRunning();
}

bool canStartCompetition() {
  if (singlePlayerMode) {
    return (footLeftPressed || footRightPressed) && isReadyState();
  }
  return footLeftPressed && footRightPressed && isReadyState();
}

void setLaneActivity() {
  if (singlePlayerMode) {
    leftLaneActive = footLeftPressed;
    rightLaneActive = footRightPressed;
  } else {
    leftLaneActive = true;
    rightLaneActive = true;
  }
}

void resetCompetitionState() {
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
  resetTimeoutActive = false;
  lastEventTime = 0;
  startSignalTime = 0;
  leftReactionTime = 0;
  rightReactionTime = 0;
  leftReactionCalculated = false;
  rightReactionCalculated = false;
}

// LED Functions
void initializeLEDs() {
  FastLED.addLeds<WS2812B, LED_PIN_LEFT, GRB>(ledsLeft, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_PIN_RIGHT, GRB>(ledsRight, NUM_LEDS_PER_STRIP);
  FastLED.setBrightness(128);
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

void setLEDsBasedOnState(bool isLeft, bool falseStart, CRGB normalColor) {
  bool showRed = falseStart && !friendlyFalseStartsEnabled;
  if (isLeft) {
    setLeftLEDs(showRed ? CRGB::Red : normalColor);
  } else {
    setRightLEDs(showRed ? CRGB::Red : normalColor);
  }
}

// Audio Functions
void playTone(int frequency) {
  ledcChangeFrequency(AUDIO_PIN, frequency, LEDC_RESOLUTION);
  ledcWrite(AUDIO_PIN, 128);
}

void stopTone() {
  ledcWrite(AUDIO_PIN, 0);
}

void startAudioSequence() {
  isPlayingAudio = true;
  currentAudioStep = 0;
  audioStepStartTime = millis();
  audioSequenceStartTime = millis();
  setLeftLEDs(CRGB::Black);
  setRightLEDs(CRGB::Black);

  if (audioSequence[0].frequency > 0) {
    playTone(audioSequence[0].frequency);
  } else {
    stopTone();
  }
}

void startFalseStartSequence() {
  if (friendlyFalseStartsEnabled) {
      completeFalseStartSequence();
      return;
    }
  isPlayingFalseStart = true;
  currentAudioStep = 0;
  audioStepStartTime = millis();

  if (falseStartSequence[0].frequency > 0) {
    playTone(falseStartSequence[0].frequency);
  } else {
    stopTone();
  }
}

void handleAudioLEDs(const AudioStep* sequence, int frequency) {
  if (isPlayingAudio) {
    if (frequency == 880 || frequency == 1760) {
      setLEDsBasedOnState(true, !friendlyFalseStartsEnabled && leftFalseStart, CRGB::Green);
      setLEDsBasedOnState(false, !friendlyFalseStartsEnabled && rightFalseStart, CRGB::Green);
    } else {
      setLEDsBasedOnState(true, !friendlyFalseStartsEnabled && leftFalseStart, CRGB::Black);
      setLEDsBasedOnState(false, !friendlyFalseStartsEnabled && rightFalseStart, CRGB::Black);
    }
  } else if (isPlayingFalseStart && !friendlyFalseStartsEnabled) {
    if (frequency == 1568) {
      if (leftFalseStart) setLeftLEDs(CRGB::Red);
      if (rightFalseStart) setRightLEDs(CRGB::Red);
      if (!leftFalseStart) setLeftLEDs(CRGB::Black);
      if (!rightFalseStart) setRightLEDs(CRGB::Black);
    } else {
      if (leftFalseStart) setLeftLEDs(CRGB::Black);
      if (rightFalseStart) setRightLEDs(CRGB::Black);
    }
  }
}

void updateAudioSequence() {
  if (!isPlayingAudio && !isPlayingFalseStart) return;

  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - audioStepStartTime;

  const AudioStep* sequence = isPlayingAudio ? audioSequence : falseStartSequence;
  int sequenceLength = isPlayingAudio ? AUDIO_SEQUENCE_LENGTH : FALSE_START_SEQUENCE_LENGTH;

  if (elapsed >= sequence[currentAudioStep].duration) {
    currentAudioStep++;

    if (currentAudioStep >= sequenceLength) {
      stopTone();
      if (isPlayingAudio) {
        isPlayingAudio = false;
        audioEndTime = millis();
        completeAudioSequence();
      } else if (isPlayingFalseStart) {
        isPlayingFalseStart = false;
        completeFalseStartSequence();
      }
      currentAudioStep = 0;
      return;
    }

    audioStepStartTime = currentTime;

    // START TIMER WHEN 1760Hz TONE BEGINS (step 5 in the sequence)
    if (isPlayingAudio && currentAudioStep == 5 && sequence[currentAudioStep].frequency == 1760) {
      startTimer();
      audioEndTime = millis();  // Set this for reaction time calculations
    }

    if (sequence[currentAudioStep].frequency > 0) {
      playTone(sequence[currentAudioStep].frequency);
      handleAudioLEDs(sequence, sequence[currentAudioStep].frequency);
    } else {
      stopTone();
      handleAudioLEDs(sequence, 0);
    }
  }
}

void completeAudioSequence() {
  isPlayingAudio = false;
  currentAudioStep = 0;

  if (falseStartOccurred && !falseStartAudioPlayed) {
    falseStartAudioPlayed = true;
    if (!friendlyFalseStartsEnabled) {
      startFalseStartSequence();
    } else {
      completeFalseStartSequence();
    }
  }

  sendWebSocketUpdate();
}

void completeFalseStartSequence() {
  isPlayingFalseStart = false;
  currentAudioStep = 0;

  setLEDsBasedOnState(true, !friendlyFalseStartsEnabled && leftFalseStart, CRGB::Black);
  setLEDsBasedOnState(false, !friendlyFalseStartsEnabled && rightFalseStart, CRGB::Black);

  resetTimeoutActive = true;
  lastEventTime = millis();

  sendWebSocketUpdate();
}

// Timer Functions
void startTimer() {
  if (!singlePlayerMode || leftLaneActive) {
    isTimerRunningLeft = true;
  }
  if (!singlePlayerMode || rightLaneActive) {
    isTimerRunningRight = true;
  }

  timerStartTime = millis();
  startSignalTime = millis();  // Mark start signal time for reaction calculation
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
  audioEndTime = 0;
  leftLaneActive = false;
  rightLaneActive = false;

  resetCompetitionState();

  // Reset state tracking
  lastTimerRunningLeft = false;
  lastTimerRunningRight = false;
  lastPlayingAudio = false;
  lastPlayingFalseStart = false;
  lastFalseStartOccurred = false;
  lastLeftFalseStart = false;
  lastRightFalseStart = false;
  lastFootLeftPressed = footLeftPressed;
  lastFootRightPressed = footRightPressed;
  lastSinglePlayerMode = singlePlayerMode;
  lastLeftFinished = false;
  lastRightFinished = false;
  lastReactionTimeLeft = 0;
  lastReactionTimeRight = 0;
  lastCompletionTimeLeft = 0;
  lastCompletionTimeRight = 0;
  lastKidsModeSensorsEnabled = kidsModeSensorsEnabled;

  turnOffAllLEDs();
  sendWebSocketUpdate();
}

void updateTimer() {
  if (isAnyTimerRunning()) {
    currentElapsedTime = millis() - timerStartTime;
  }
}

// Button/Sensor Handling
void handleFootSensorPress(bool isLeft) {
  if (singlePlayerMode && isAnyTimerRunning()) {
    if (resetTimeoutActive && (millis() - lastEventTime < RESET_TIMEOUT)) {
      return;
    }
    handleApiReset();
    return;
  }

  if (singlePlayerMode && isReadyState()) {
    bool otherFootPressed = isLeft ? footRightPressed : footLeftPressed;
    if (!otherFootPressed) {
      footPressStartTime = millis();
      footHeldForAutoStart = true;
    }
  }

  if (isReadyState()) {
    if (isLeft) {
      setLeftLEDs(CRGB::Green);
    } else {
      setRightLEDs(CRGB::Green);
    }
  }
}

void handleFootSensorRelease(bool isLeft) {
  if (singlePlayerMode && footHeldForAutoStart) {
    bool otherFootPressed = isLeft ? footLeftPressed : footRightPressed;
    if (!otherFootPressed) {
      footHeldForAutoStart = false;
      footPressStartTime = 0;
    }
  }

  if (isReadyState()) {
    if (isLeft) {
      setLeftLEDs(CRGB::Black);
    } else {
      setRightLEDs(CRGB::Black);
    }
  }

  // Check for false start if audio is playing but timer hasn't started yet
  if (isPlayingAudio && startSignalTime == 0) {
    // Calculate negative reaction time for pre-start movement
    unsigned long audioStartTime = audioSequenceStartTime;
    unsigned long expectedStartTime = audioStartTime + 500 + 250 + 750 + 250 + 750;  // When 1760Hz should start
    long negativeReactionTime = (long)millis() - (long)expectedStartTime;

    if (isLeft) {
      leftReactionTime = negativeReactionTime;  // Store negative reaction time
      leftReactionCalculated = true;
      leftFalseStart = true;
      falseStartOccurred = true;
      if (!friendlyFalseStartsEnabled) setLeftLEDs(CRGB::Red);
    } else {
      rightReactionTime = negativeReactionTime;  // Store negative reaction time
      rightReactionCalculated = true;
      rightFalseStart = true;
      falseStartOccurred = true;
      if (!friendlyFalseStartsEnabled) setRightLEDs(CRGB::Red);
    }
    resetTimeoutActive = true;
    lastEventTime = millis();
    sendWebSocketUpdate();
    return;
  }

  // IFSC reaction time calculation (after start signal)
  if (startSignalTime > 0 && isAnyTimerRunning()) {
    unsigned long releaseTime = millis();
    long reactionTime = (long)releaseTime - (long)startSignalTime;

    if (isLeft && !leftReactionCalculated) {
      leftReactionTime = reactionTime;
      leftReactionCalculated = true;

      if (reactionTime < FALSE_START_REACTION_TIME_CUTOFF) {  // False start < 100ms
        leftFalseStart = true;
        falseStartOccurred = true;
        if (!friendlyFalseStartsEnabled) setLeftLEDs(CRGB::Red);
      }
    } else if (!isLeft && !rightReactionCalculated) {
      rightReactionTime = reactionTime;
      rightReactionCalculated = true;

      if (reactionTime < FALSE_START_REACTION_TIME_CUTOFF) {  // False start < 100ms
        rightFalseStart = true;
        falseStartOccurred = true;
        if (!friendlyFalseStartsEnabled) setRightLEDs(CRGB::Red);
      }
    }

    // Handle both competitors false starting
    if (leftFalseStart && rightFalseStart && leftReactionCalculated && rightReactionCalculated) {
      if (leftReactionTime == rightReactionTime) {
        // Equal reaction times - both false started
      } else if (leftReactionTime < rightReactionTime) {
        rightFalseStart = false;
        setRightLEDs(CRGB::Black);
      } else {
        leftFalseStart = false;
        setLeftLEDs(CRGB::Black);
      }
    }

    sendWebSocketUpdate();
  }
}

void checkAutoStart() {
  if (singlePlayerMode && footHeldForAutoStart && isReadyState()) {
    if (millis() - footPressStartTime >= AUTO_START_DELAY) {
      footHeldForAutoStart = false;
      footPressStartTime = 0;
      setLaneActivity();
      resetCompetitionState();
      startAudioSequence();
    }
  }
}

void checkButtons() {
  if (millis() - lastButtonCheck < BUTTON_DEBOUNCE) return;

  bool startPressed = !digitalRead(START_BUTTON);
  bool stopLeftNow = !digitalRead(STOP_SENSOR_LEFT);
  bool stopRightNow = !digitalRead(STOP_SENSOR_RIGHT);
  bool footLeftNow = (digitalRead(FOOT_SENSOR_LEFT) == LOW);
  bool footRightNow = (digitalRead(FOOT_SENSOR_RIGHT) == LOW);

  // Handle foot sensor changes
  if (footLeftNow && !footLeftPressed) {
    footLeftPressed = true;
    handleFootSensorPress(true);
  } else if (!footLeftNow && footLeftPressed) {
    footLeftPressed = false;
    handleFootSensorRelease(true);
  }

  if (footRightNow && !footRightPressed) {
    footRightPressed = true;
    handleFootSensorPress(false);
  } else if (!footRightNow && footRightPressed) {
    footRightPressed = false;
    handleFootSensorRelease(false);
  }

  checkAutoStart();

  // Handle start button
  if (startPressed && !startButtonPressed) {
    startButtonPressed = true;
    handleStartButton();
  } else if (!startPressed) {
    startButtonPressed = false;
  }

  // Handle stop sensors
  if (stopLeftNow && !stopLeftPressed) {
    stopLeftPressed = true;
    handleStopSensor(true);
  } else if (!stopLeftNow) {
    stopLeftPressed = false;
  }

  if (stopRightNow && !stopRightPressed) {
    stopRightPressed = true;
    handleStopSensor(false);
  } else if (!stopRightNow) {
    stopRightPressed = false;
  }

  // Handle kids mode stop sensors (only if enabled)
  if (kidsModeSensorsEnabled) {
    bool stopLeftKidsNow = !digitalRead(STOP_SENSOR_LEFT_KIDS);
    bool stopRightKidsNow = !digitalRead(STOP_SENSOR_RIGHT_KIDS);

    if (stopLeftKidsNow && !stopLeftKidsPressed) {
      stopLeftKidsPressed = true;
      handleStopSensor(true);  // Use same handler as regular stop sensor
    } else if (!stopLeftKidsNow) {
      stopLeftKidsPressed = false;
    }

    if (stopRightKidsNow && !stopRightKidsPressed) {
      stopRightKidsPressed = true;
      handleStopSensor(false);  // Use same handler as regular stop sensor
    } else if (!stopRightKidsNow) {
      stopRightKidsPressed = false;
    }
  }
  lastButtonCheck = millis();
}

void handleStartButton() {
  if (canStartCompetition()) {
    setLaneActivity();
    resetCompetitionState();
    startAudioSequence();
  } else {
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

void determineWinner() {
  bool leftDNF = !leftFinished && !leftFalseStart && completionTimeLeft == 0 && (currentElapsedTime > 0 || resetTimeoutActive);
  bool rightDNF = !rightFinished && !rightFalseStart && completionTimeRight == 0 && (currentElapsedTime > 0 || resetTimeoutActive);
  
  if ((leftFinished || rightFinished || leftDNF || rightDNF) && !falseStartOccurred) {
    if (leftFinished && !rightFinished && !rightDNF) {
      setLeftLEDs(CRGB::Green);
      setRightLEDs(CRGB::Red);
    } else if (rightFinished && !leftFinished && !leftDNF) {
      setRightLEDs(CRGB::Green);
      setLeftLEDs(CRGB::Red);
    } else if (leftFinished && rightDNF) {
      setLeftLEDs(CRGB::Green);
      setRightLEDs(CRGB::Orange); // Keep orange for DNF
    } else if (rightFinished && leftDNF) {
      setRightLEDs(CRGB::Green);
      setLeftLEDs(CRGB::Orange); // Keep orange for DNF
    } else if (leftFinished && rightFinished) {
      if (completionTimeLeft < completionTimeRight) {
        setLeftLEDs(CRGB::Green);
        setRightLEDs(CRGB::Red);
      } else if (completionTimeRight < completionTimeLeft) {
        setRightLEDs(CRGB::Green);
        setLeftLEDs(CRGB::Red);
      } else {
        setLeftLEDs(CRGB::Green);
        setRightLEDs(CRGB::Green);
      }
    }
    // Both DNF case - no winner
    else if (leftDNF && rightDNF) {
      setLeftLEDs(CRGB::Orange);
      setRightLEDs(CRGB::Orange);
    }
  } else if (falseStartOccurred) {
    if (!leftFalseStart && rightFalseStart && leftFinished) {
      setLeftLEDs(CRGB::Green);
    } else if (!rightFalseStart && leftFalseStart && rightFinished) {
      setRightLEDs(CRGB::Green);
    } else if (!leftFalseStart && rightFalseStart && leftDNF) {
      setLeftLEDs(CRGB::Orange);
    } else if (!rightFalseStart && leftFalseStart && rightDNF) {
      setRightLEDs(CRGB::Orange);
    }
  }
}

void handleStopSensor(bool isLeft) {
  if (isAnyTimerRunning()) {
    unsigned long completionTime = millis() - timerStartTime;

    if (isLeft && !leftFinished) {
      completionTimeLeft = completionTime;
      leftFinished = true;
      isTimerRunningLeft = false;
    } else if (!isLeft && !rightFinished) {
      completionTimeRight = completionTime;
      rightFinished = true;
      isTimerRunningRight = false;
    }

    if (!isAnyTimerRunning()) {
      currentElapsedTime = millis() - timerStartTime;
      resetTimeoutActive = true;
      lastEventTime = millis();
    }

    determineWinner();

    if (leftFinished && rightFinished) {
      stopTimer();
    }

    sendWebSocketUpdate();
  }
}

// WebSocket Functions
bool hasStateChanged() {
  bool footSensorChanged = (footLeftPressed != lastFootLeftPressed || footRightPressed != lastFootRightPressed);
  bool timerStateChanged = (isTimerRunningLeft != lastTimerRunningLeft || isTimerRunningRight != lastTimerRunningRight);
  bool leftLaneStateChanged = (!singlePlayerMode || leftLaneActive) && (leftFalseStart != lastLeftFalseStart || leftFinished != lastLeftFinished || reactionTimeLeft != lastReactionTimeLeft || completionTimeLeft != lastCompletionTimeLeft);
  bool rightLaneStateChanged = (!singlePlayerMode || rightLaneActive) && (rightFalseStart != lastRightFalseStart || rightFinished != lastRightFinished || reactionTimeRight != lastReactionTimeRight || completionTimeRight != lastCompletionTimeRight);
  bool otherStateChanged = (isPlayingAudio != lastPlayingAudio || isPlayingFalseStart != lastPlayingFalseStart || falseStartOccurred != lastFalseStartOccurred || singlePlayerMode != lastSinglePlayerMode);
  bool kidsModeSensorsChanged = (kidsModeSensorsEnabled != lastKidsModeSensorsEnabled);

  return footSensorChanged || timerStateChanged || leftLaneStateChanged || rightLaneStateChanged || otherStateChanged || kidsModeSensorsChanged;
}

void updateLastState() {
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
  lastKidsModeSensorsEnabled = kidsModeSensorsEnabled;
}

void updateWebSocket() {
  // Skip processing if no clients connected
  if (!hasConnectedClients()) {
    // Still update last state to prevent flooding when clients reconnect
    updateLastState();
    return;
  }

  unsigned long currentTime = millis();
  bool shouldUpdate = false;

  if (singlePlayerMode) {
    shouldUpdate = (leftLaneActive && isTimerRunningLeft) || (rightLaneActive && isTimerRunningRight) || isPlayingAudio || isPlayingFalseStart || hasStateChanged();
  } else {
    shouldUpdate = isAnyTimerRunning() || isPlayingAudio || isPlayingFalseStart || hasStateChanged();
  }

  // Only send update if shouldUpdate is true AND at least WEBSOCKET_UPDATE_INTERVAL ms have passed
  if (shouldUpdate && (currentTime - lastWebSocketUpdate >= WEBSOCKET_UPDATE_INTERVAL)) {
    sendWebSocketUpdate();
    updateLastState();
    lastWebSocketUpdate = currentTime;
  }
}

DynamicJsonDocument buildStatusJson() {
  if (isAnyTimerRunning()) {
    currentElapsedTime = millis() - timerStartTime;
  }

  DynamicJsonDocument doc(1024);

  doc["is_timer_running"] = isAnyTimerRunning();
  doc["is_timer_running_left"] = isTimerRunningLeft;  // Added for DNF button logic
  doc["is_timer_running_right"] = isTimerRunningRight; // Added for DNF button logic
  doc["is_playing_audio"] = isPlayingAudio;
  doc["is_playing_false_start"] = isPlayingFalseStart;
  doc["false_start_occurred"] = falseStartOccurred;
  doc["left_false_start"] = leftFalseStart;
  doc["right_false_start"] = rightFalseStart;
  doc["single_player_mode"] = singlePlayerMode;

  doc["foot_left_pressed"] = footLeftPressed;
  doc["foot_right_pressed"] = footRightPressed;
  doc["both_feet_ready"] = footLeftPressed && footRightPressed;
  doc["ready_to_start"] = singlePlayerMode ? (footLeftPressed || footRightPressed) : (footLeftPressed && footRightPressed);

  doc["elapsed_time"] = currentElapsedTime;
  doc["formatted_time"] = formatTime(currentElapsedTime);

  doc["completion_time_left"] = completionTimeLeft;
  doc["formatted_completion_time_left"] = formatTime(completionTimeLeft);
  doc["left_finished"] = leftFinished;

  doc["completion_time_right"] = completionTimeRight;
  doc["formatted_completion_time_right"] = formatTime(completionTimeRight);
  doc["right_finished"] = rightFinished;

  doc["reaction_time_left"] = leftReactionCalculated ? leftReactionTime : 0;
  doc["formatted_reaction_time_left"] = formatSignedTime(leftReactionCalculated ? leftReactionTime : 0);
  doc["reaction_time_right"] = rightReactionCalculated ? rightReactionTime : 0;
  doc["formatted_reaction_time_right"] = formatSignedTime(rightReactionCalculated ? rightReactionTime : 0);
  doc["kids_mode_sensors_enabled"] = kidsModeSensorsEnabled;
  doc["competition_complete"] = (leftFinished || rightFinished) && !isAnyTimerRunning();

  doc["left_dnf"] = !leftFinished && !leftFalseStart && completionTimeLeft == 0 && (currentElapsedTime > 0 || resetTimeoutActive);
  doc["right_dnf"] = !rightFinished && !rightFalseStart && completionTimeRight == 0 && (currentElapsedTime > 0 || resetTimeoutActive);

  doc["friendly_false_starts"] = friendlyFalseStartsEnabled;

  doc["uptime"] = millis();

  return doc;
}

bool hasConnectedClients() {
  uint8_t totalClients = webSocket.connectedClients();
  
  for (uint8_t i = 0; i < totalClients; i++) {
    if (webSocket.clientIsConnected(i)) {
      return true;
    }
  }
  return false;
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

  char timeStr[17];
  if (isNegative) {
    sprintf(timeStr, "-%lu:%02lu.%03lu", minutes, seconds, ms);
  } else {
    sprintf(timeStr, "%lu:%02lu.%03lu", minutes, seconds, ms);
  }
  return String(timeStr);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client #%u disconnected (Total clients: %u)\n", num, webSocket.connectedClients());
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("WebSocket client #%u connected from %d.%d.%d.%d (Total clients: %u)\n", num, ip[0], ip[1], ip[2], ip[3], webSocket.connectedClients());
        sendWebSocketUpdate();
      }
      break;

    case WStype_TEXT:
      {
        String command = String((char*)payload);
        Serial.printf("WebSocket received: %s\n", command.c_str());

        if (command == "start") {
          if (canStartCompetition()) {
            setLaneActivity();
            resetCompetitionState();
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
        } else if (command == "toggle_kids_mode") {
          kidsModeSensorsEnabled = !kidsModeSensorsEnabled;
        } else if (command == "dnf_left") {
          handleDNF(true);  // true for left climber
        } else if (command == "dnf_right") {
          handleDNF(false); // false for right climber
        } else if (command == "toggle_friendly_fs") {
          friendlyFalseStartsEnabled = !friendlyFalseStartsEnabled;
        }
        sendWebSocketUpdate();
      }
      break;

    default:
      break;
  }
}

void handleDNF(bool isLeft) {
  // Only allow DNF if timer is running and climber hasn't already finished
  if (isAnyTimerRunning()) {
    if (isLeft && !leftFinished && !leftFalseStart) {
      // Stop left timer but don't set completion time
      isTimerRunningLeft = false;
      leftFinished = false;  // Keep as false to indicate DNF
      completionTimeLeft = 0; // Ensure it stays 0 for DNF
      
      // Set LEDs to indicate DNF
      setLeftLEDs(CRGB::Orange);
      
    } else if (!isLeft && !rightFinished && !rightFalseStart) {
      // Stop right timer but don't set completion time  
      isTimerRunningRight = false;
      rightFinished = false;  // Keep as false to indicate DNF
      completionTimeRight = 0; // Ensure it stays 0 for DNF
      
      // Set LEDs to indicate DNF
      setRightLEDs(CRGB::Orange);
    }
    
    // If both timers stopped, end the competition
    if (!isAnyTimerRunning()) {
      currentElapsedTime = millis() - timerStartTime;
      resetTimeoutActive = true;
      lastEventTime = millis();
      
      // Determine winner (the one who didn't DNF)
      determineWinner();
    }
    
    sendWebSocketUpdate();
  }
}

// Web Server Handlers
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
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      min-height: 1.2em;
    }


    .timer-display.false-start-text {
      font-size: 1.8em;
    }

    @media (max-width: 768px) {
      .timer-display.false-start-text {
        font-size: 1.4em;
      }
    }

    @media (max-width: 360px) {
      .timer-display.false-start-text {
        font-size: 1.2em;
      }
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

    .winner-time {
      background: rgba(255,215,0,0.3) !important; 
      border: 2px solid gold;
      animation: winner-glow 1s ease-in-out infinite alternate;
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
      min-height: 44px;
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
      display: grid;
      grid-template-columns: 1fr 1fr;
      grid-column: 1 / -1;
      gap: 10px;
      justify-items: center;
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
      display: none; /* Hidden by default */
      grid-column: 1 / -1;
    }
        
    .help-button {
      background: linear-gradient(135deg, rgba(59, 130, 246, 0.3), rgba(37, 99, 235, 0.4));
      border: 2px solid rgba(59, 130, 246, 0.6);
      padding: 8px 16px;
      border-radius: 20px;
      font-size: 12px;
      font-weight: bold;
      transition: all 0.3s ease;
      text-shadow: 0 1px 2px rgba(0,0,0,0.4);
      width: 100%;
      max-width: 120px;
      min-height: 36px;
    }
    .help-button:hover {
      background: linear-gradient(135deg, rgba(59, 130, 246, 0.4), rgba(37, 99, 235, 0.5));
      transform: translateY(-1px);
      box-shadow: 0 4px 15px rgba(59, 130, 246, 0.4);
    }
    
    /* Times Log Styles */
    .times-log {
      background: linear-gradient(135deg, rgba(255,255,255,0.12), rgba(255,255,255,0.06));
      padding: 20px;
      border-radius: 15px;
      margin: 30px 0 0 0;
      border: 1px solid rgba(255,255,255,0.25);
      box-shadow: 0 8px 25px rgba(0,0,0,0.15);
    }
    
    .log-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 15px;
    }
    
    .log-controls button {
      font-size: 12px;
      padding: 8px 16px;
      margin-left: 10px;
      max-width: none;
      width: auto;
    }
    
    .log-entry {
      background: rgba(255,255,255,0.1);
      padding: 12px;
      margin: 8px 0;
      border-radius: 8px;
      border: 1px solid rgba(255,255,255,0.2);
      font-size: 0.9em;
    }

    /* Only apply winner styling for competition mode or multi-person single player */
    .log-entry.winner-left {
      border-left: 4px solid #FFD700;
      background: linear-gradient(135deg, rgba(255,215,0,0.15), rgba(255,215,0,0.08));
      box-shadow: 0 0 10px rgba(255,215,0,0.3);
    }

    .log-entry.winner-right {
      border-left: 4px solid #FFD700;
      background: linear-gradient(135deg, rgba(255,215,0,0.15), rgba(255,215,0,0.08));
      box-shadow: 0 0 10px rgba(255,215,0,0.3);
    }

    .log-entry.single-player {
      border-left: 4px solid #FF9800;
    }

    .log-entry-header {
      font-weight: bold;
      margin-bottom: 5px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    .winner-badge {
      color: #FFD700;
      font-weight: bold;
      text-shadow: 0 0 5px rgba(255,215,0,0.5);
    }

    /* Flexible grid layout for log times */
    .log-times {
      margin-top: 8px;
    }

    .log-times.two-column {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }

    .log-times.single-column {
      display: flex;
      justify-content: center;
    }

    .log-times.single-column .log-time {
      max-width: 300px;
      width: 100%;
    }

    .log-time {
      background: rgba(0,0,0,0.2);
      padding: 8px;
      border-radius: 6px;
      text-align: center;
      border: 1px solid rgba(255,255,255,0.1);
    }

    /* Only apply false-start styling in competition mode */
    .log-time.false-start {
      background: rgba(220,38,38,0.3);
      color: #ff8a80;
      border: 1px solid rgba(220,38,38,0.5);
    }
    
    .empty-log {
      text-align: center;
      color: rgba(255,255,255,0.7);
      font-style: italic;
      padding: 20px;
    }
    
    .kids-mode-toggle {
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
      .kids-mode-toggle.enabled {
        background: linear-gradient(135deg, #FF6B35, #FF8E53);
        border-color: #FF5722;
        box-shadow: 0 6px 20px rgba(255, 107, 53, 0.5);
      }
      .kids-mode-toggle.disabled {
        background: linear-gradient(135deg, #666, #888);
        border-color: #555;
        box-shadow: 0 6px 20px rgba(102, 102, 102, 0.3);
      }
      .kids-mode-toggle:hover {
        transform: translateY(-2px) scale(1.02);
        box-shadow: 0 8px 25px rgba(255,255,255,0.3);
      }

      .friendly-fs-toggle {
        background: rgba(255,255,255,0.15);
        border: 2px solid rgba(255,255,255,0.4);
        padding: 12px 24px;
        border-radius: 30px;
        font-size: 14px;
        font-weight: bold;
        transition: all 0.4s ease;
        text-shadow: 0 1px 2px rgba(0,0,0,0.4);
        width: 100%;
        max-width: 240px;
      }
      .friendly-fs-toggle.enabled {
        background: linear-gradient(135deg, #9C27B0, #CE93D8);
        border-color: #7B1FA2;
        box-shadow: 0 6px 20px rgba(156, 39, 176, 0.5);
      }
      .friendly-fs-toggle.disabled {
        background: linear-gradient(135deg, #666, #888);
        border-color: #555;
        box-shadow: 0 6px 20px rgba(102, 102, 102, 0.3);
      }
      .friendly-fs-toggle:hover {
        transform: translateY(-2px) scale(1.02);
        box-shadow: 0 8px 25px rgba(255,255,255,0.3);
      }

      .dnf-button {
        background: linear-gradient(135deg, rgba(220,38,38,0.3), rgba(185,28,28,0.4));
        border: 2px solid rgba(220,38,38,0.6);
        color: #ff8a80;
        font-weight: bold;
        padding: 8px 16px;
        margin: 8px auto;  /* This auto margin works with display: block */
        border-radius: 8px;
        font-size: 0.9em;
        width: 150px;      /* Fixed width instead of max-width */
        display: block;    /* Add this to make the button a block element */
      }
      .dnf-button:hover {
        background: linear-gradient(135deg, rgba(220,38,38,0.4), rgba(185,28,28,0.5));
        transform: translateY(-1px);
      }
    
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
      
      .times-log {
        grid-column: 1 / -1;
      }
      
      .timer-display {
        font-size: 2.8em;
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
      
      .help-button {
        max-width: 150px;
        font-size: 14px;
      }
    }
    
    @media (min-width: 1024px) {
      .timer-display {
        font-size: 3em;
      }
      
      h1 {
        font-size: 2.5em;
      }
    }
    
    /* Mobile responsive adjustments */
    @media (max-width: 768px) {
      .log-times.two-column {
        grid-template-columns: 1fr;
        gap: 8px;
      }
      
      .log-entry-header {
        flex-direction: column;
        align-items: flex-start;
        gap: 5px;
      }
      
      .winner-badge {
        align-self: flex-end;
      }
    }

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
      
      .log-times {
        grid-template-columns: 1fr;
      }
      
      .help-button {
        font-size: 11px;
        padding: 6px 12px;
        max-width: 100px;
        min-height: 32px;
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
        <button onclick='markDNF("left")' id='dnf-left-btn' class='dnf-button' style='display:none'>❌ Mark DNF</button>
      </div>
      
      <div class='climber-panel right-panel'>
        <h2>RIGHT CLIMBER</h2>
        <div id='timer-right' class='timer-display'>0:00.000</div>
        <div id='foot-status-right' class='foot-sensor foot-released'>Foot Sensor: None</div>
        <div id='reaction-time-right' class='reaction-time' style='display:none'>Reaction: 0:00.000</div>
        <div id='completion-time-right' class='completion-time' style='display:none'>Time: 0:00.000</div>
        <button onclick='markDNF("right")' id='dnf-right-btn' class='dnf-button' style='display:none'>❌ Mark DNF</button>
      </div>
      
      <div class='button-group'>
        <button onclick='startSequence()' id='startBtn'>🚀 Start Competition</button>
        <button onclick='resetTimer()' id='resetBtn'>🔄 Reset</button>
        <button onclick='toggleMode()' id='modeBtn' class='mode-toggle competition-mode'>🏆 Competition Mode</button>
        <button onclick='toggleKidsMode()' id='kidsModeBtn' class='kids-mode-toggle disabled'>🤸 Blue Sensors: OFF</button>
        <button onclick='toggleFriendlyFS()' id='friendlyFSBtn' class='friendly-fs-toggle disabled'>😊 Friendly False Starts: OFF</button>
        <button onclick='toggleHelp()' id='helpBtn' class='help-button'>❓ Help</button>
      </div>
      
      <div class='instructions' id='instructions'>
        <strong id='instructions-title'>Single Player Instructions:</strong><br>
        <span id='instructions-text'>
          1. Press and hold <b>ONE</b> foot sensor<br>
          2. Press Start to begin audio countdown or <b>stand on foot sensor for 3 seconds</b><br>
          3. Keep foot sensor pressed during entire audio countdown<br>
          4. Start climbing on the high picthed start tone<br>
          5. Hit your stop sensor when you reach the top<br>
          False starts as per IFSC are calucated at 0.1 seconds after the start tone sounds
        </span>
      </div>
      
      <!-- Times Log Section -->
      <div class='times-log'>
        <div class='log-header'>
          <h2>Times Log</h2>
          <div class='log-controls'>
            <button onclick='clearLog()'>Clear Log</button>
            <button onclick='exportLog()'>Export CSV</button>
          </div>
        </div>
        <div id='log-entries'>
          <div class='empty-log'>No times recorded yet. Complete a competition to see results here.</div>
        </div>
      </div>
    </div>
  </div>

  <script>
    let ws;
    let timesLog = [];
    let lastCompetitionComplete = false;
    let helpVisible = false;

    // Toggle help instructions
    function toggleHelp() {
      const instructionsDiv = document.getElementById('instructions');
      const helpBtn = document.getElementById('helpBtn');
      
      helpVisible = !helpVisible;
      
      if (helpVisible) {
        instructionsDiv.style.display = 'block';
        helpBtn.textContent = '❌ Hide Help';
      } else {
        instructionsDiv.style.display = 'none';
        helpBtn.textContent = '❓ Help';
      }
    }

    function toggleFriendlyFS() { ws.send('toggle_friendly_fs'); }

    function markDNF(side) {
      if (confirm(`Mark ${side} climber as Did Not Finish?`)) {
        ws.send(`dnf_${side}`);
      }
    }

    // Load saved log from localStorage
    function loadLog() {
      const saved = localStorage.getItem('gravityWorxTimesLog');
      if (saved) {
        timesLog = JSON.parse(saved);
        renderLog();
      }
    }

    // Save log to localStorage
    function saveLog() {
      localStorage.setItem('gravityWorxTimesLog', JSON.stringify(timesLog));
    }

    // Add entry to log
    function addLogEntry(data) {
      const timestamp = new Date();
      const entry = {
        timestamp: timestamp.toISOString(),
        date: timestamp.toLocaleDateString(),
        time: timestamp.toLocaleTimeString(),
        singlePlayerMode: data.single_player_mode,
        leftTime: data.completion_time_left || 0,
        rightTime: data.completion_time_right || 0,
        leftReaction: data.reaction_time_left || 0,
        rightReaction: data.reaction_time_right || 0,
        leftFalseStart: data.left_false_start || false,
        rightFalseStart: data.right_false_start || false,
        leftFinished: data.left_finished || false,
        rightFinished: data.right_finished || false,
        formattedLeftTime: data.formatted_completion_time_left || '0:00.000',
        formattedRightTime: data.formatted_completion_time_right || '0:00.000',
        formattedLeftReaction: data.formatted_reaction_time_left || '0:00.000',
        formattedRightReaction: data.formatted_reaction_time_right || '0:00.000'
      };

      timesLog.unshift(entry); // Add to beginning
      if (timesLog.length > 100) { // Keep only last 100 entries
        timesLog = timesLog.slice(0, 100);
      }
      
      saveLog();
      renderLog();
    }

    // Render log entries
    function renderLog() {
      const logContainer = document.getElementById('log-entries');
      
      if (timesLog.length === 0) {
        logContainer.innerHTML = '<div class="empty-log">No times recorded yet. Complete a competition to see results here.</div>';
        return;
      }

      let html = '';
      timesLog.forEach((entry, index) => {
        let entryClass = 'log-entry';
        let winner = '';
        let showWinner = false;
        let leftIsWinner = false;
        let rightIsWinner = false;
        
        if (entry.singlePlayerMode) {
          entryClass += ' single-player';
          // For single player, only show winner styling if both lanes have valid times (indicating a race between two single players)
          const leftHasTime = entry.leftFinished && !entry.leftFalseStart && entry.leftTime > 0;
          const rightHasTime = entry.rightFinished && !entry.rightFalseStart && entry.rightTime > 0;
          
          if (leftHasTime && rightHasTime) {
            showWinner = true;
            if (entry.leftTime < entry.rightTime) {
              winner = 'Left Climber';
              leftIsWinner = true;
            } else if (entry.rightTime < entry.leftTime) {
              winner = 'Right Climber';
              rightIsWinner = true;
            } else {
              winner = 'Tie';
              leftIsWinner = true;
              rightIsWinner = true;
            }
          } else if (leftHasTime) {
            winner = 'Left Climber';
            leftIsWinner = true;
          } else if (rightHasTime) {
            winner = 'Right Climber';
            rightIsWinner = true;
          }
        } else {
          // Competition mode - keep existing logic but only show winner for valid completions
          const leftValid = entry.leftFinished && !entry.leftFalseStart;
          const rightValid = entry.rightFinished && !entry.rightFalseStart;
          
          if (leftValid || rightValid) {
            showWinner = true;
            if (entry.leftFalseStart && !entry.rightFalseStart && entry.rightFinished) {
              winner = 'Right Climber';
              rightIsWinner = true;
            } else if (entry.rightFalseStart && !entry.leftFalseStart && entry.leftFinished) {
              winner = 'Left Climber';
              leftIsWinner = true;
            } else if (leftValid && rightValid) {
              if (entry.leftTime < entry.rightTime) {
                winner = 'Left Climber';
                leftIsWinner = true;
              } else if (entry.rightTime < entry.leftTime) {
                winner = 'Right Climber';
                rightIsWinner = true;
              } else {
                winner = 'Tie';
                leftIsWinner = true;
                rightIsWinner = true;
              }
            } else if (leftValid) {
              winner = 'Left Climber';
              leftIsWinner = true;
            } else if (rightValid) {
              winner = 'Right Climber';
              rightIsWinner = true;
            }
          }
        }

        // Determine which times to show based on mode and actual data
        let leftTimeDisplay = '';
        let rightTimeDisplay = '';
        let showLeftTime = false;
        let showRightTime = false;

        if (entry.singlePlayerMode) {
          // In single player, only show times that actually have data
          if (entry.leftFinished || entry.leftFalseStart || entry.leftTime > 0) {
            showLeftTime = true;
            if (entry.leftFinished && entry.leftTime > 0) {
              leftTimeDisplay = `
                <div class="log-time ${leftIsWinner ? 'winner-time' : ''}">
                  <strong>Left:</strong><br>
                  Time: ${entry.formattedLeftTime}<br>
                  Reaction: ${entry.formattedLeftReaction}
                </div>
              `;
            } else if (entry.leftFalseStart) {
              leftTimeDisplay = `
                <div class="log-time">
                  <strong>Left:</strong><br>
                  Time: DQ (False Start)<br>
                  Reaction: ${entry.formattedLeftReaction}
                </div>
              `;
            } else {
              leftTimeDisplay = `
                <div class="log-time">
                  <strong>Left:</strong><br>
                  Time: DNF<br>
                  Reaction: ${entry.formattedLeftReaction}
                </div>
              `;
            }
          }

          if (entry.rightFinished || entry.rightFalseStart || entry.rightTime > 0) {
            showRightTime = true;
            if (entry.rightFinished && entry.rightTime > 0) {
              rightTimeDisplay = `
                <div class="log-time ${rightIsWinner ? 'winner-time' : ''}">
                  <strong>Right:</strong><br>
                  Time: ${entry.formattedRightTime}<br>
                  Reaction: ${entry.formattedRightReaction}
                </div>
              `;
            } else if (entry.rightFalseStart) {
              rightTimeDisplay = `
                <div class="log-time">
                  <strong>Right:</strong><br>
                  Time: DQ (False Start)<br>
                  Reaction: ${entry.formattedRightReaction}
                </div>
              `;
            } else {
              rightTimeDisplay = `
                <div class="log-time">
                  <strong>Right:</strong><br>
                  Time: DNF<br>
                  Reaction: ${entry.formattedRightReaction}
                </div>
              `;
            }
          }
        } else {
          // Competition mode - show both times
          showLeftTime = true;
          showRightTime = true;
          
          leftTimeDisplay = `
            <div class="log-time ${entry.leftFalseStart ? 'false-start' : ''} ${leftIsWinner ? 'winner-time' : ''}">
              <strong>Left:</strong><br>
              Time: ${entry.leftFinished ? entry.formattedLeftTime : 'DNF'}${entry.leftFalseStart ? ' (DQ)' : ''}<br>
              Reaction: ${entry.formattedLeftReaction}
            </div>
          `;
          
          rightTimeDisplay = `
            <div class="log-time ${entry.rightFalseStart ? 'false-start' : ''} ${rightIsWinner ? 'winner-time' : ''}">
              <strong>Right:</strong><br>
              Time: ${entry.rightFinished ? entry.formattedRightTime : 'DNF'}${entry.rightFalseStart ? ' (DQ)' : ''}<br>
              Reaction: ${entry.formattedRightReaction}
            </div>
          `;
        }

        // Determine grid layout based on what's being shown
        let gridClass = 'log-times';
        if (showLeftTime && showRightTime) {
          gridClass = 'log-times two-column';
        } else {
          gridClass = 'log-times single-column';
        }

        html += `
          <div class="${entryClass}">
            <div class="log-entry-header">
              <span>${entry.date} ${entry.time} - ${entry.singlePlayerMode ? 'Single Player' : 'Competition'}</span>
            </div>
            <div class="${gridClass}">
              ${showLeftTime ? leftTimeDisplay : ''}
              ${showRightTime ? rightTimeDisplay : ''}
            </div>
          </div>
        `;
      });
      
      logContainer.innerHTML = html;
    }

    // Clear log
    function clearLog() {
      if (confirm('Are you sure you want to clear all logged times? This cannot be undone.')) {
        timesLog = [];
        saveLog();
        renderLog();
      }
    }

    // Export log as CSV
    function exportLog() {
      if (timesLog.length === 0) {
        alert('No data to export.');
        return;
      }

      let csv = 'Date,Time,Mode,Left Time,Left Reaction,Left False Start,Right Time,Right Reaction,Right False Start,Winner\n';
      
      timesLog.forEach(entry => {
        const leftTime = entry.leftFinished ? entry.leftTime : '';
        const rightTime = entry.rightFinished ? entry.rightTime : '';
        let winner = '';
        
        if (entry.singlePlayerMode) {
          if (entry.leftFinished && !entry.leftFalseStart) winner = 'Left';
          else if (entry.rightFinished && !entry.rightFalseStart) winner = 'Right';
        } else {
          if (entry.leftFalseStart && !entry.rightFalseStart && entry.rightFinished) winner = 'Right';
          else if (entry.rightFalseStart && !entry.leftFalseStart && entry.leftFinished) winner = 'Left';
          else if (!entry.leftFalseStart && !entry.rightFalseStart && entry.leftFinished && entry.rightFinished) {
            winner = entry.leftTime < entry.rightTime ? 'Left' : entry.rightTime < entry.leftTime ? 'Right' : 'Tie';
          } else if (entry.leftFinished) winner = 'Left';
          else if (entry.rightFinished) winner = 'Right';
        }
        
        csv += `"${entry.date}","${entry.time}","${entry.singlePlayerMode ? 'Single' : 'Competition'}",${leftTime},${entry.leftReaction},${entry.leftFalseStart},${rightTime},${entry.rightReaction},${entry.rightFalseStart},"${winner}"\n`;
      });

      const blob = new Blob([csv], { type: 'text/csv' });
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `gravity-worx-times-${new Date().toISOString().split('T')[0]}.csv`;
      a.click();
      window.URL.revokeObjectURL(url);
    }

    function toggleKidsMode() { ws.send('toggle_kids_mode'); }
    
    function connectWebSocket() {
      const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
      ws = new WebSocket(protocol + '://' + location.hostname + ':81');
      ws.onopen = function() { console.log('WebSocket connected'); };
      ws.onmessage = function(event) {
        const data = JSON.parse(event.data);
        const footLeftDiv = document.getElementById('foot-status-left');
        const footRightDiv = document.getElementById('foot-status-right');

        if(data.is_timer_running || data.competition_complete) {
          footLeftDiv.style.display = 'none';
          footRightDiv.style.display = 'none';
        } else 
        {
          // Show foot sensor status when timer is not running
          footLeftDiv.style.display = 'block';
          
          if(data.foot_left_pressed) {
            footLeftDiv.textContent = 'Foot Sensor: Pressed';
            footLeftDiv.className = 'foot-sensor foot-pressed';
          } else {
            footLeftDiv.textContent = 'Foot Sensor: None';
            footLeftDiv.className = 'foot-sensor foot-released';
          }
          
          footRightDiv.style.display = 'block';
          
          if(data.foot_right_pressed) {
            footRightDiv.textContent = 'Foot Sensor: Pressed';
            footRightDiv.className = 'foot-sensor foot-pressed';
          } else {
            footRightDiv.textContent = 'Foot Sensor: None';
            footRightDiv.className = 'foot-sensor foot-released';
          }
        }

        // Check if competition just completed and auto-save
        if (data.competition_complete && !lastCompetitionComplete) {
          if (data.completion_time_left > 0 || data.completion_time_right > 0) {
            addLogEntry(data);
          }
        }

        const dnfLeftBtn = document.getElementById('dnf-left-btn');
        const dnfRightBtn = document.getElementById('dnf-right-btn');

        if (data.is_timer_running && data.is_timer_running_left && !data.left_finished && !data.left_false_start) {
          dnfLeftBtn.style.display = 'block';
        } else {
          dnfLeftBtn.style.display = 'none';
        }

        if (data.is_timer_running && data.is_timer_running_right && !data.right_finished && !data.right_false_start) {
          dnfRightBtn.style.display = 'block';
        } else {
          dnfRightBtn.style.display = 'none';
        }

        lastCompetitionComplete = data.competition_complete;
        
        const leftTimer = document.getElementById('timer-left');
        const rightTimer = document.getElementById('timer-right');
        
        if(!data.single_player_mode || data.left_finished || data.left_false_start || data.reaction_time_left != 0 || (data.is_timer_running && data.foot_left_pressed)) {
          if(data.left_finished && data.completion_time_left > 0) {
            leftTimer.textContent = data.formatted_completion_time_left;
            leftTimer.style.background = 'rgba(34,197,94,0.2)';
          } else if(data.left_false_start) {
            leftTimer.textContent = 'FALSE START';
            leftTimer.style.background = 'rgba(220,38,38,0.3)';
            leftTimer.classList.add('false-start-text');
          } else {
            leftTimer.textContent = data.formatted_time;
            leftTimer.style.background = 'rgba(255,255,255,0.1)';
            leftTimer.classList.remove('false-start-text');
          }
        } else {
          leftTimer.textContent = '0:00.000';
          leftTimer.style.background = 'rgba(100,100,100,0.2)';
          leftTimer.classList.remove('false-start-text');
        }
        
        if(!data.single_player_mode || data.right_finished || data.right_false_start || data.reaction_time_right != 0 || (data.is_timer_running && data.foot_right_pressed)) {
          if(data.right_finished && data.completion_time_right > 0) {
            rightTimer.textContent = data.formatted_completion_time_right;
            rightTimer.style.background = 'rgba(34,197,94,0.2)';
          } else if(data.right_false_start) {
            rightTimer.textContent = 'FALSE START';
            rightTimer.style.background = 'rgba(220,38,38,0.3)';
            rightTimer.classList.add('false-start-text');
          } else {
            rightTimer.textContent = data.formatted_time;
            rightTimer.style.background = 'rgba(255,255,255,0.1)';
            rightTimer.classList.remove('false-start-text');
          }
        } else {
          rightTimer.textContent = '0:00.000';
          rightTimer.style.background = 'rgba(100,100,100,0.2)';
          rightTimer.classList.remove('false-start-text');
        }
                
        const modeBtn = document.getElementById('modeBtn');
        if(data.single_player_mode) {
          modeBtn.textContent = '👤 Single Player Mode';
          modeBtn.className = 'mode-toggle single-mode';
        } else {
          modeBtn.textContent = '🏆 Competition Mode';
          modeBtn.className = 'mode-toggle competition-mode';
        }

        const kidsModeBtn = document.getElementById('kidsModeBtn');
        if(data.kids_mode_sensors_enabled) {
          kidsModeBtn.textContent = '🤸 Blue Sensors: ON';
          kidsModeBtn.className = 'kids-mode-toggle enabled';
        } else {
          kidsModeBtn.textContent = '🤸 Blue Sensors: OFF';
          kidsModeBtn.className = 'kids-mode-toggle disabled';
        }
        
        const friendlyFSBtn = document.getElementById('friendlyFSBtn');
        if(data.friendly_false_starts) {
          friendlyFSBtn.textContent = 'Friendly False Starts: ON';
          friendlyFSBtn.className = 'friendly-fs-toggle enabled';
        } else {
          friendlyFSBtn.textContent = 'Friendly False Starts: OFF';
          friendlyFSBtn.className = 'friendly-fs-toggle disabled';
        }

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
            False starts as per IFSC are calucated at 0.1 seconds after the start tone sounds
          `;
        } else {
          instructionsTitle.textContent = 'Competition Instructions:';
          instructionsText.innerHTML = `
            1. Both climbers press and hold foot sensors<br>
            2. Press Start to begin audio countdown<br>
            3. Keep foot sensors pressed during entire audio countdown<br>
            4. Start climbing on the high picthed start tone<br>
            5. Hit your stop sensor when you reach the top<br>
            False starts as per IFSC are calucated at 0.1 seconds after the start tone sounds<br>
            <strong>Both climbers can still finish even if one false starts</strong><br>
          `;
        }
        
        const reactionLeftDiv = document.getElementById('reaction-time-left');
        const reactionRightDiv = document.getElementById('reaction-time-right');
        
        if((!data.single_player_mode || data.left_finished || data.left_false_start || data.reaction_time_left != 0) && 
           (data.reaction_time_left != 0 || (data.elapsed_time > 0 && !data.is_playing_audio))) {
          if(data.reaction_time_left != 0) {
            reactionLeftDiv.textContent = 'Reaction: ' + data.formatted_reaction_time_left;
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
        
        const completionLeftDiv = document.getElementById('completion-time-left');
        const completionRightDiv = document.getElementById('completion-time-right');
        const leftPanel = document.querySelector('.left-panel');
        const rightPanel = document.querySelector('.right-panel');
        
        leftPanel.classList.remove('winner', 'disqualified');
        rightPanel.classList.remove('winner', 'disqualified');
        
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
        
        if(!data.left_false_start && !data.right_false_start) {
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
          if(data.completion_time_right > 0) {
            rightPanel.classList.add('winner');
          }
        } else if(!data.left_false_start && data.right_false_start) {
          if(data.completion_time_left > 0) {
            leftPanel.classList.add('winner');
          }
        }
        
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

    // Load log on page load
    loadLog();
    connectWebSocket();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleApiStatus() {
  DynamicJsonDocument doc = buildStatusJson();
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void sendWebSocketUpdate() {
  // Quick check - don't build JSON if no clients
  uint8_t totalClients = webSocket.connectedClients();
  if (totalClients == 0) {
    return;
  }

  DynamicJsonDocument doc = buildStatusJson();
  String message;
  serializeJson(doc, message);
  
  unsigned long updateStart = millis(); // Track total update time
  
  // Send to each client individually with timeout protection
  for(uint8_t i = 0; i < totalClients; i++) {
    if(webSocket.clientIsConnected(i)) {
      // Force break if we've already spent too much time
      if(millis() - updateStart > 100) {
        Serial.printf("Update timeout reached, skipping remaining %u clients\n", totalClients - i);
        break;
      }
      
      unsigned long sendStart = millis();
      
      if(!webSocket.sendTXT(i, message)) {
        Serial.printf("Client %u send failed, disconnecting\n", i);
        webSocket.disconnect(i);
      } else {
        unsigned long sendTime = millis() - sendStart;
        if(sendTime > 50) {
          Serial.printf("Slow send to client %u: %lu ms\n", i, sendTime);
        }
      }
    }
  }
  
  unsigned long totalTime = millis() - updateStart;
  if(totalTime > 100) {
    Serial.printf("WebSocket update took %lu ms total\n", totalTime);
  }
}

void handleApiStart() {
  if (canStartCompetition()) {
    setLaneActivity();
    resetCompetitionState();
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

void handleApiToggleKidsMode() {
  kidsModeSensorsEnabled = !kidsModeSensorsEnabled;
  server.send(200, "application/json", "{\"status\":\"kids_mode_toggled\",\"enabled\":" + String(kidsModeSensorsEnabled ? "true" : "false") + "}");
  sendWebSocketUpdate();
}

void setup() {
  Serial.begin(115200);

  pinMode(START_BUTTON, INPUT_PULLUP);
  pinMode(STOP_SENSOR_LEFT, INPUT_PULLUP);
  pinMode(STOP_SENSOR_RIGHT, INPUT_PULLUP);
  pinMode(FOOT_SENSOR_LEFT, INPUT_PULLUP);
  pinMode(FOOT_SENSOR_RIGHT, INPUT_PULLUP);
  pinMode(STOP_SENSOR_LEFT_KIDS, INPUT_PULLUP);
  pinMode(STOP_SENSOR_RIGHT_KIDS, INPUT_PULLUP);

  initializeLEDs();

  ledcAttach(AUDIO_PIN, 1000, LEDC_RESOLUTION);
  ledcWrite(AUDIO_PIN, 0);

  /////////////WIFI/////////////

  Serial.println("Setting up Access Point...");

  // Configure as Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);

  // Get AP IP address
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("Access Point IP: ");
  Serial.println(apIP);
  Serial.print("Network Name: ");
  Serial.println(ap_ssid);

  // Start DNS server for captive portal
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("DNS server started for captive portal");

  /////////////WIFI/////////////

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/start", HTTP_POST, handleApiStart);
  server.on("/api/stop", HTTP_POST, handleApiStop);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/toggle_kids_mode", HTTP_POST, handleApiToggleKidsMode);

  server.on("/generate_204", HTTP_GET, handleCaptivePortal);         // Android
  server.on("/fwlink", HTTP_GET, handleCaptivePortal);               // Microsoft
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);  // Apple
  server.onNotFound(handleCaptivePortal);                            // Catch all other requests

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  webSocket.enableHeartbeat(1000, 500, 2);  // ping every 15s, timeout 3s, disconnect after 2 failed pings

  server.begin();
  Serial.println("HTTP server started");
  Serial.println("WebSocket server started on port 81");
}

void loop() {
  unsigned long loopStart = millis();
  loopCount++;

  server.handleClient();
  webSocket.loop();
  checkButtons();
  updateAudioSequence();
  updateTimer();
  updateWebSocket();
  dnsServer.processNextRequest();

  lastLoopTime = millis() - loopStart;

  // Display stats every second and checks dns (10 times per second)
  if (millis() - lastTime >= 100) {
    if (loopCount < 80) {
      Serial.print("Loops/sec: ");
      Serial.print(loopCount);
      Serial.print(" | Last loop took: ");
      Serial.print(lastLoopTime);
      Serial.println(" ms");
    }
    loopCount = 0;
    lastTime = millis();
  }
}
