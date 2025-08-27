#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>

// ================== Hardware Config ==================
#define START_BUTTON 27
#define STOP_SENSOR_LEFT  14
#define STOP_SENSOR_RIGHT  12
#define FOOT_SENSOR_LEFT 26
#define FOOT_SENSOR_RIGHT 18
#define AUDIO_PIN    22

// ================== WiFi Config ==================
const char* ssid = "Nacho WiFi";
const char* password = "airforce11";

// ================== Audio Config ==================
#define LEDC_CHANNEL     0
#define LEDC_RESOLUTION  8

// Audio sequence timing (all in milliseconds)
struct AudioStep {
  int frequency;  // 0 = silence
  unsigned long duration;
};

const AudioStep audioSequence[] = {
  {0, 500},      // Silence 500ms
  {880, 250},    // 880Hz for 250ms
  {0, 500},      // Silence 500ms
  {880, 250},    // 880Hz for 250ms
  {0, 500},      // Silence 500ms
  {1760, 150}    // 1760Hz for 150ms
};

const int AUDIO_SEQUENCE_LENGTH = sizeof(audioSequence) / sizeof(AudioStep);

// False start audio sequence
const AudioStep falseStartSequence[] = {
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100},      // Silence for 100ms
  {1568, 100},   // 1568Hz for 100ms
  {0, 100}       // Final silence for 100ms
};

const int FALSE_START_SEQUENCE_LENGTH = sizeof(falseStartSequence) / sizeof(AudioStep);

// ================== Global Variables ==================
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
bool isTimerRunning = false;
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
bool leftFootValidDuringAudio = true;  // Track if left foot stayed pressed during audio
bool rightFootValidDuringAudio = true; // Track if right foot stayed pressed during audio
bool falseStartAudioPlayed = false;    // Ensure false start audio only plays once

// Left side timing
unsigned long reactionTimeLeft = 0;
unsigned long completionTimeLeft = 0;
bool leftFinished = false;

// Right side timing
unsigned long reactionTimeRight = 0;
unsigned long completionTimeRight = 0;
bool rightFinished = false;

unsigned long audioEndTime = 0;
unsigned long lastButtonCheck = 0;
unsigned long lastWebSocketUpdate = 0;
const unsigned long BUTTON_DEBOUNCE = 50;
const unsigned long WEBSOCKET_UPDATE_INTERVAL = 50; // Update every 50ms

// Non-blocking audio sequence state
int currentAudioStep = 0;
unsigned long audioStepStartTime = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(START_BUTTON, INPUT_PULLUP);
  pinMode(STOP_SENSOR_LEFT, INPUT_PULLUP);
  pinMode(STOP_SENSOR_RIGHT, INPUT_PULLUP);
  pinMode(FOOT_SENSOR_LEFT, INPUT_PULLUP);
  pinMode(FOOT_SENSOR_RIGHT, INPUT_PULLUP);
  
  // Initialize LEDC for audio - using newer ESP32 Arduino Core functions
  ledcAttach(AUDIO_PIN, 1000, LEDC_RESOLUTION); // Start with 1kHz base frequency
  ledcWrite(AUDIO_PIN, 0); // Start with no sound
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
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

void checkButtons() {
  if (millis() - lastButtonCheck < BUTTON_DEBOUNCE) return;
  
  bool startPressed = !digitalRead(START_BUTTON);
  bool stopLeftNow = !digitalRead(STOP_SENSOR_LEFT);
  bool stopRightNow = !digitalRead(STOP_SENSOR_RIGHT);
  bool footLeftNow = (digitalRead(FOOT_SENSOR_LEFT) == LOW);
  bool footRightNow = (digitalRead(FOOT_SENSOR_RIGHT) == LOW);
  
  // During audio sequence, track if either foot sensor is released (false start)
  if (isPlayingAudio && !isPlayingFalseStart) {
    if (!footLeftNow && leftFootValidDuringAudio) {
      // Left foot released during audio - false start
      leftFootValidDuringAudio = false;
      leftFalseStart = true;
      falseStartOccurred = true;
    }
    if (!footRightNow && rightFootValidDuringAudio) {
      // Right foot released during audio - false start  
      rightFootValidDuringAudio = false;
      rightFalseStart = true;
      falseStartOccurred = true;
    }
  }
  
  // Handle foot sensor left state changes (for reaction time after audio)
  if (footLeftNow && !footLeftPressed) {
    footLeftPressed = true;
  } else if (!footLeftNow && footLeftPressed) {
    footLeftPressed = false;
    if (!isPlayingAudio && !isPlayingFalseStart && isTimerRunning && !leftFalseStart) {
      // Valid release after audio ended - calculate reaction time
      reactionTimeLeft = millis() - audioEndTime;
      sendWebSocketUpdate();
    }
  }
  
  // Handle foot sensor right state changes (for reaction time after audio)
  if (footRightNow && !footRightPressed) {
    footRightPressed = true;
  } else if (!footRightNow && footRightPressed) {
    footRightPressed = false;
    if (!isPlayingAudio && !isPlayingFalseStart && isTimerRunning && !rightFalseStart) {
      // Valid release after audio ended - calculate reaction time
      reactionTimeRight = millis() - audioEndTime;
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
    handleStopSensor(true); // true for left side
  } else if (!stopLeftNow) {
    stopLeftPressed = false;
  }
  
  // Handle stop sensor right
  if (stopRightNow && !stopRightPressed) {
    stopRightPressed = true;
    handleStopSensor(false); // false for right side
  } else if (!stopRightNow) {
    stopRightPressed = false;
  }
  
  lastButtonCheck = millis();
}

void handleFootSensorRelease(bool isLeft) {
  if (isPlayingAudio && !isPlayingFalseStart) {
    // False start occurred - foot sensor released during audio sequence
    // Just mark the false start, but let the audio continue
    falseStartOccurred = true;
    if (isLeft) {
      leftFalseStart = true;
    } else {
      rightFalseStart = true;
    }
    sendWebSocketUpdate();
  } else if (!isPlayingAudio && !isPlayingFalseStart && isTimerRunning) {
    // Valid release after audio ended - calculate reaction time
    unsigned long reactionTime = millis() - audioEndTime;
    if (isLeft && !leftFalseStart) {
      reactionTimeLeft = reactionTime;
    } else if (!isLeft && !rightFalseStart) {
      reactionTimeRight = reactionTime;
    }
    sendWebSocketUpdate(); // Send immediate update with reaction time
  }
}

void handleStartButton() {
  // Only allow start if BOTH foot sensors are pressed and not currently playing audio
  if (footLeftPressed && footRightPressed && !isPlayingAudio && !isPlayingFalseStart) {
    falseStartOccurred = false;
    leftFalseStart = false;
    rightFalseStart = false;
    leftFootValidDuringAudio = true;
    rightFootValidDuringAudio = true;
    falseStartAudioPlayed = false;
    reactionTimeLeft = 0;
    reactionTimeRight = 0;
    completionTimeLeft = 0;
    completionTimeRight = 0;
    leftFinished = false;
    rightFinished = false;
    startAudioSequence();
  }
}

void handleStopSensor(bool isLeft) {
  if (isTimerRunning) {
    // Only allow finish if that side didn't make a false start
    if ((isLeft && !leftFalseStart) || (!isLeft && !rightFalseStart)) {
      unsigned long completionTime = millis() - timerStartTime;
      if (isLeft && !leftFinished) {
        completionTimeLeft = completionTime;
        leftFinished = true;
      } else if (!isLeft && !rightFinished) {
        completionTimeRight = completionTime;
        rightFinished = true;
      }
      
      // Stop timer if both valid competitors finished, or if only one competitor is valid and finished
      bool leftValid = !leftFalseStart;
      bool rightValid = !rightFalseStart;
      bool shouldStop = false;
      
      if (leftValid && rightValid) {
        // Both competitors valid, stop when both finish
        shouldStop = leftFinished && rightFinished;
      } else if (leftValid && !rightValid) {
        // Only left competitor valid, stop when left finishes
        shouldStop = leftFinished;
      } else if (!leftValid && rightValid) {
        // Only right competitor valid, stop when right finishes
        shouldStop = rightFinished;
      }
      
      if (shouldStop) {
        stopTimer();
      }
    }
    sendWebSocketUpdate();
  }
}

void startAudioSequence() {
  isPlayingAudio = true;
  currentAudioStep = 0;
  audioStepStartTime = millis();
  
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
        audioEndTime = millis(); // Record when audio ended
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
    } else {
      stopTone();
    }
  }
}

void playTone(int frequency) {
  ledcChangeFrequency(AUDIO_PIN, frequency, LEDC_RESOLUTION);
  ledcWrite(AUDIO_PIN, 128); // 50% duty cycle for square wave
}

void stopTone() {
  ledcWrite(AUDIO_PIN, 0);
}

void completeAudioSequence() {
  isPlayingAudio = false;
  currentAudioStep = 0;
  audioEndTime = millis(); // Record when audio ended
  
  if (falseStartOccurred && !falseStartAudioPlayed) {
    // False start(s) occurred during audio, play false start sequence ONCE
    falseStartAudioPlayed = true;
    startFalseStartSequence();
  } else if (!falseStartOccurred) {
    // No false starts, start the timer normally
    startTimer();
  }
  
  sendWebSocketUpdate(); // Immediate update when audio completes
}

void completeFalseStartSequence() {
  isPlayingFalseStart = false;
  currentAudioStep = 0;
  sendWebSocketUpdate(); // Immediate update when false start sequence completes
}

void startTimer() {
  isTimerRunning = true;
  timerStartTime = millis();
  sendWebSocketUpdate(); // Immediate update when timer starts
}

void stopTimer() {
  isTimerRunning = false;
  currentElapsedTime = millis() - timerStartTime;
  sendWebSocketUpdate(); // Immediate update when timer stops
}

void resetTimer() {
  isTimerRunning = false;
  currentElapsedTime = 0;
  timerStartTime = 0;
  falseStartOccurred = false;
  leftFalseStart = false;
  rightFalseStart = false;
  leftFootValidDuringAudio = true;
  rightFootValidDuringAudio = true;
  falseStartAudioPlayed = false;
  reactionTimeLeft = 0;
  reactionTimeRight = 0;
  completionTimeLeft = 0;
  completionTimeRight = 0;
  leftFinished = false;
  rightFinished = false;
  audioEndTime = 0;
  sendWebSocketUpdate(); // Immediate update when timer resets
}

void updateTimer() {
  if (isTimerRunning) {
    currentElapsedTime = millis() - timerStartTime;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
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
          if (footLeftPressed && footRightPressed && !isPlayingAudio && !isPlayingFalseStart) {
            falseStartOccurred = false;
            leftFalseStart = false;
            rightFalseStart = false;
            leftFootValidDuringAudio = true;
            rightFootValidDuringAudio = true;
            falseStartAudioPlayed = false;
            reactionTimeLeft = 0;
            reactionTimeRight = 0;
            completionTimeLeft = 0;
            completionTimeRight = 0;
            leftFinished = false;
            rightFinished = false;
            startAudioSequence();
          }
        } else if (command == "stop") {
          if (isTimerRunning) {
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
        }
        sendWebSocketUpdate(); // Send immediate update after command
      }
      break;
      
    default:
      break;
  }
}

void updateWebSocket() {
  unsigned long currentTime = millis();
  // Only send updates if timer is running or audio is playing, or if it's been a while since last update
  if (isTimerRunning || isPlayingAudio || isPlayingFalseStart || (currentTime - lastWebSocketUpdate >= WEBSOCKET_UPDATE_INTERVAL)) {
    sendWebSocketUpdate();
    lastWebSocketUpdate = currentTime;
  }
}

void sendWebSocketUpdate() {
  // Always get fresh timer value when sending update
  if (isTimerRunning) {
    currentElapsedTime = millis() - timerStartTime;
  }
  
  DynamicJsonDocument doc(1024);
  
  doc["is_timer_running"] = isTimerRunning;
  doc["is_playing_audio"] = isPlayingAudio;
  doc["is_playing_false_start"] = isPlayingFalseStart;
  doc["false_start_occurred"] = falseStartOccurred;
  doc["left_false_start"] = leftFalseStart;
  doc["right_false_start"] = rightFalseStart;
  doc["left_false_start"] = leftFalseStart;
  doc["right_false_start"] = rightFalseStart;
  
  // Foot sensor states
  doc["foot_left_pressed"] = footLeftPressed;
  doc["foot_right_pressed"] = footRightPressed;
  doc["both_feet_ready"] = footLeftPressed && footRightPressed;
  
  // Timing data
  doc["elapsed_time"] = currentElapsedTime;
  doc["formatted_time"] = formatTime(currentElapsedTime);
  
  // Left side data
  doc["reaction_time_left"] = reactionTimeLeft;
  doc["formatted_reaction_time_left"] = formatTime(reactionTimeLeft);
  doc["completion_time_left"] = completionTimeLeft;
  doc["formatted_completion_time_left"] = formatTime(completionTimeLeft);
  doc["left_finished"] = leftFinished;
  
  // Right side data
  doc["reaction_time_right"] = reactionTimeRight;
  doc["formatted_reaction_time_right"] = formatTime(reactionTimeRight);
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

// ================== Web Server Handlers ==================

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Speed Climbing Competition Timer</title>
  <meta charset="UTF-8">
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body {
      font-family: Arial, sans-serif;
      max-width: 800px;
      margin: 0 auto;
      padding: 20px;
      background: linear-gradient(135deg, #43B75C 0%, #2E8B57 100%);
      color: white;
      min-height: 100vh;
    }
    .container {
      background: rgba(255,255,255,0.1);
      padding: 30px;
      border-radius: 15px;
      backdrop-filter: blur(10px);
      box-shadow: 0 8px 32px rgba(0,0,0,0.1);
    }
    .competition-layout {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 30px;
      margin: 20px 0;
    }
    .climber-panel {
      background: rgba(255,255,255,0.05);
      padding: 20px;
      border-radius: 10px;
      text-align: center;
    }
    .timer-display {
      font-size: 3.5em;
      font-weight: bold;
      margin: 20px 0;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.5);
      background: rgba(255,255,255,0.1);
      padding: 20px;
      border-radius: 10px;
    }
    .status {
      text-align: center;
      margin: 20px 0;
      font-size: 1.2em;
      padding: 10px;
      border-radius: 5px;
      grid-column: 1 / -1;
    }
    .running { background: rgba(34,197,94,0.3); }
    .stopped { background: rgba(239,68,68,0.3); }
    .playing { background: rgba(251,191,36,0.3); }
    .false-start-left { background: rgba(220,38,38,0.4); border: 2px solid #dc2626; }
    .false-start-right { background: rgba(220,38,38,0.4); border: 2px solid #dc2626; }
    .disqualified { 
      opacity: 0.6; 
      background: rgba(220,38,38,0.2) !important;
      border: 2px solid #dc2626 !important;
    }
    .foot-sensor {
      padding: 8px;
      border-radius: 5px;
      font-weight: bold;
      margin: 10px 0;
    }
    .foot-pressed { background: rgba(34,197,94,0.3); }
    .foot-released { background: rgba(239,68,68,0.3); }
    .reaction-time, .completion-time {
      padding: 8px;
      border-radius: 5px;
      margin: 5px 0;
      font-size: 0.9em;
    }
    .reaction-time { background: rgba(251,191,36,0.2); }
    .completion-time { background: rgba(34,197,94,0.2); }
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
      background: rgba(255,255,255,0.2);
      color: white;
      border: 2px solid rgba(255,255,255,0.3);
      padding: 15px 25px;
      margin: 5px;
      border-radius: 8px;
      cursor: pointer;
      font-size: 16px;
      font-weight: bold;
      transition: all 0.3s ease;
    }
    button:hover { background: rgba(255,255,255,0.3); transform: translateY(-2px); }
    button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }
    .button-group { text-align: center; margin: 20px 0; grid-column: 1 / -1; }
    h1 { text-align: center; margin-bottom: 30px; text-shadow: 2px 2px 4px rgba(0,0,0,0.5); }
    h2 { margin-top: 0; }
    .instructions { 
      background: rgba(255,255,255,0.1); 
      padding: 15px; 
      border-radius: 8px; 
      margin: 20px 0; 
      grid-column: 1 / -1;
      font-size: 0.9em;
    }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Speed Climbing Competition</h1>
    
    <div class='competition-layout'>
      <div id='status' class='status stopped'>System Ready - Both Climbers Place Feet</div>
      
      <div class='climber-panel left-panel'>
        <h2>LEFT CLIMBER</h2>
        <div id='timer-left' class='timer-display'>0:00.000</div>
        <div id='foot-status-left' class='foot-sensor foot-released'>Foot: Released</div>
        <div id='reaction-time-left' class='reaction-time' style='display:none'>Reaction: 0:00.000</div>
        <div id='completion-time-left' class='completion-time' style='display:none'>Time: 0:00.000</div>
      </div>
      
      <div class='climber-panel right-panel'>
        <h2>RIGHT CLIMBER</h2>
        <div id='timer-right' class='timer-display'>0:00.000</div>
        <div id='foot-status-right' class='foot-sensor foot-released'>Foot: Released</div>
        <div id='reaction-time-right' class='reaction-time' style='display:none'>Reaction: 0:00.000</div>
        <div id='completion-time-right' class='completion-time' style='display:none'>Time: 0:00.000</div>
      </div>
      
      <div class='button-group'>
        <button onclick='startSequence()' id='startBtn'>Start Competition</button>
        <button onclick='stopTimer()' id='stopBtn' disabled>Stop Timer</button>
        <button onclick='resetTimer()' id='resetBtn'>Reset</button>
      </div>
      
      <div class='instructions'>
        <strong>Competition Instructions:</strong><br>
        1. Both climbers press and hold foot sensors<br>
        2. Press Start to begin audio sequence<br>
        3. Keep foot sensors pressed during entire audio<br>
        4. Release foot sensor when ready to climb<br>
        5. Hit your stop sensor when you reach the top<br>
        6. Early foot release = FALSE START!
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
        
        // Update foot sensor status - LEFT
        const footLeftDiv = document.getElementById('foot-status-left');
        if(data.foot_left_pressed) {
          footLeftDiv.textContent = 'Foot: Pressed [OK]';
          footLeftDiv.className = 'foot-sensor foot-pressed';
        } else {
          footLeftDiv.textContent = 'Foot: Released';
          footLeftDiv.className = 'foot-sensor foot-released';
        }
        
        // Update foot sensor status - RIGHT
        const footRightDiv = document.getElementById('foot-status-right');
        if(data.foot_right_pressed) {
          footRightDiv.textContent = 'Foot: Pressed [OK]';
          footRightDiv.className = 'foot-sensor foot-pressed';
        } else {
          footRightDiv.textContent = 'Foot: Released';
          footRightDiv.className = 'foot-sensor foot-released';
        }
        
        // Update reaction times
        const reactionLeftDiv = document.getElementById('reaction-time-left');
        const reactionRightDiv = document.getElementById('reaction-time-right');
        
        if(data.reaction_time_left > 0) {
          reactionLeftDiv.textContent = 'Reaction: ' + data.formatted_reaction_time_left;
          reactionLeftDiv.style.display = 'block';
        } else {
          reactionLeftDiv.style.display = 'none';
        }
        
        if(data.reaction_time_right > 0) {
          reactionRightDiv.textContent = 'Reaction: ' + data.formatted_reaction_time_right;
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
        
        // Mark false start competitors as disqualified
        if(data.left_false_start) {
          leftPanel.classList.add('disqualified');
        }
        if(data.right_false_start) {
          rightPanel.classList.add('disqualified');
        }
        
        if(data.completion_time_left > 0 && !data.left_false_start) {
          completionLeftDiv.textContent = 'FINISH: ' + data.formatted_completion_time_left;
          completionLeftDiv.style.display = 'block';
        } else if(data.left_false_start) {
          completionLeftDiv.textContent = 'DISQUALIFIED - False Start';
          completionLeftDiv.style.display = 'block';
          completionLeftDiv.style.background = 'rgba(220,38,38,0.3)';
        } else {
          completionLeftDiv.style.display = 'none';
        }
        
        if(data.completion_time_right > 0 && !data.right_false_start) {
          completionRightDiv.textContent = 'FINISH: ' + data.formatted_completion_time_right;
          completionRightDiv.style.display = 'block';
        } else if(data.right_false_start) {
          completionRightDiv.textContent = 'DISQUALIFIED - False Start';
          completionRightDiv.style.display = 'block';
          completionRightDiv.style.background = 'rgba(220,38,38,0.3)';
        } else {
          completionRightDiv.style.display = 'none';
        }
        
        // Highlight winner (lowest completion time, excluding false starts)
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
        
        // Update status
        const statusDiv = document.getElementById('status');
        const startBtn = document.getElementById('startBtn');
        const stopBtn = document.getElementById('stopBtn');

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
          startBtn.disabled = true; stopBtn.disabled = true;
        } else if(data.is_playing_audio) {
          statusDiv.textContent = 'Audio Sequence Playing - Keep Feet Pressed!';
          statusDiv.className = 'status playing';
          startBtn.disabled = true; stopBtn.disabled = true;
        } else if(data.is_timer_running) {
          statusDiv.textContent = 'CLIMB! Timer Running';
          statusDiv.className = 'status running';
          startBtn.disabled = true; stopBtn.disabled = false;
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
            statusDiv.textContent = data.both_feet_ready ? 'Ready to Start!' : 'Waiting for Both Feet';
            statusDiv.className = 'status stopped';
          }
          startBtn.disabled = !data.both_feet_ready; 
          stopBtn.disabled = true;
        }
      };
      ws.onclose = function() { console.log('WebSocket disconnected'); setTimeout(connectWebSocket, 3000); };
      ws.onerror = function(error) { console.log('WebSocket error:', error); };
    }

    function startSequence() { ws.send('start'); }
    function stopTimer() { ws.send('stop'); }
    function resetTimer() { ws.send('reset'); }

    connectWebSocket();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleApiStatus() {
  DynamicJsonDocument doc(1024);
  
  doc["is_timer_running"] = isTimerRunning;
  doc["is_playing_audio"] = isPlayingAudio;
  doc["is_playing_false_start"] = isPlayingFalseStart;
  doc["false_start_occurred"] = falseStartOccurred;
  
  // Foot sensor states
  doc["foot_left_pressed"] = footLeftPressed;
  doc["foot_right_pressed"] = footRightPressed;
  doc["both_feet_ready"] = footLeftPressed && footRightPressed;
  
  // Timing data
  doc["elapsed_time"] = currentElapsedTime;
  doc["formatted_time"] = formatTime(currentElapsedTime);
  
  // Left side data
  doc["reaction_time_left"] = reactionTimeLeft;
  doc["formatted_reaction_time_left"] = formatTime(reactionTimeLeft);
  doc["completion_time_left"] = completionTimeLeft;
  doc["formatted_completion_time_left"] = formatTime(completionTimeLeft);
  doc["left_finished"] = leftFinished;
  
  // Right side data
  doc["reaction_time_right"] = reactionTimeRight;
  doc["formatted_reaction_time_right"] = formatTime(reactionTimeRight);
  doc["completion_time_right"] = completionTimeRight;
  doc["formatted_completion_time_right"] = formatTime(completionTimeRight);
  doc["right_finished"] = rightFinished;
  
  doc["uptime"] = millis();
  
  String response;
  serializeJson(doc, response);
  
  server.send(200, "application/json", response);
}

void handleApiStart() {
  if (footLeftPressed && footRightPressed && !isPlayingAudio && !isPlayingFalseStart) {
    falseStartOccurred = false;
    leftFalseStart = false;
    rightFalseStart = false;
    leftFootValidDuringAudio = true;
    rightFootValidDuringAudio = true;
    falseStartAudioPlayed = false;
    reactionTimeLeft = 0;
    reactionTimeRight = 0;
    completionTimeLeft = 0;
    completionTimeRight = 0;
    leftFinished = false;
    rightFinished = false;
    startAudioSequence();
    server.send(200, "application/json", "{\"status\":\"started\"}");
  } else if (!footLeftPressed || !footRightPressed) {
    server.send(400, "application/json", "{\"error\":\"both foot sensors must be pressed\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"audio already playing\"}");
  }
}

void handleApiStop() {
  if (isTimerRunning) {
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
  server.send(200, "application/json", "{\"status\":\"reset\"}");
}
