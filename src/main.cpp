#include <Arduino.h>
#include <LCDWIKI_GUI.h>
#include <LCDWIKI_SPI.h>

// =====================================================
// PIN DEFINITIONS
// =====================================================

#define SPEED_SENSOR_PIN 6
#define BUTTON_MODE 7

#define TFT_CS 8
#define TFT_DC 9
#define TFT_RESET 10


// =====================================================
// TFT CONFIGURATION
// =====================================================

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define BLACK 0x0000
#define WHITE 0xFFFF
#define SPEED_GREEN 0x07E0
#define TRIP_RED 0xF800
#define TIME_CYAN 0x07FF

#define SPEED_REGION_Y 0
#define SPEED_REGION_HEIGHT 120

#define INFO_BAR_Y 120
#define INFO_BAR_HEIGHT 45

#define DISTANCE_REGION_Y 165
#define DISTANCE_REGION_HEIGHT 75


LCDWIKI_SPI mylcd(
  ILI9341,
  TFT_CS,
  TFT_DC,
  TFT_RESET,
  -1
);


// =====================================================
// SPEED SENSOR CALIBRATION
// =====================================================

// Sensor produces two pulses per wheel revolution
float pulsesPerWheelRevolution = 2.0;

// Wheel circumference in meters
float wheelCircumferenceMeters = 1.675;


// =====================================================
// SPEED SENSOR VARIABLES
// =====================================================

bool lastSpeedSensorState = HIGH;

uint32_t totalSpeedPulses = 0;


// -----------------------------------------------------
// Pulse timing
// -----------------------------------------------------

// Time of the immediately previous pulse
unsigned long lastPulseTimeMicros = 0;

// Time of the pulse before the immediately previous pulse
unsigned long previousPulseTimeMicros = 0;

unsigned long lastPulseTimeMillis = 0;


// -----------------------------------------------------
// Speed values
// -----------------------------------------------------

float currentSpeed = 0.0;
float filteredSpeed = 0.0;

// Used so the first valid speed measurement
// immediately becomes the displayed speed.
bool speedMeasurementInitialized = false;


// -----------------------------------------------------
// Smoothing factor
// -----------------------------------------------------

// Lower = smoother / slower response
// Higher = faster / more responsive
const float SPEED_FILTER_ALPHA = 0.20;


// -----------------------------------------------------
// Speed timeout
// -----------------------------------------------------

// If no pulse arrives within this time,
// consider the wheel stopped.
//
// 3000 ms is used instead of 1500 ms so
// low-speed wheel movement is not prematurely
// interpreted as zero speed.
#define SPEED_TIMEOUT_MS 3000


// =====================================================
// TRIPMETER VARIABLES
// =====================================================

// Distances are stored as whole meters.

// Total odometer starts at 624,697 km.
//
// 624,697 km × 1,000 = 624,697,000 meters

uint32_t trip1DistanceMeters = 0;
uint32_t trip2DistanceMeters = 0;
uint32_t totalDistanceMeters = 624697000UL;

// Stores fractional meters between pulses.
float distanceAccumulatorMeters = 0.0;


// =====================================================
// TRIP MODE
// =====================================================

enum TripMode {
  TRIP_1,
  TRIP_2,
  TOTAL
};

TripMode currentTripMode = TRIP_1;


// =====================================================
// BUTTON VARIABLES
// =====================================================

bool lastModeButtonState = HIGH;

unsigned long modeButtonPressedAt = 0;
bool modeLongPressHandled = false;

#define BUTTON_HOLD_TIME 2000


// =====================================================
// DISPLAY CACHE VARIABLES
// =====================================================

char lastDisplayedSpeedString[8] = "";
char lastDisplayedDistanceString[16] = "";

TripMode lastDisplayedTripMode = TRIP_1;

bool dashboardInitialized = false;


// =====================================================
// FUNCTION DECLARATIONS
// =====================================================

void showSplashScreen();

void drawDashboard();
void drawSpeed();
void drawTripMode();
void drawDistance();

void updateSpeedSensor();
void updateButtons();

void changeTripMode();
void resetSelectedTrip();

int getCenteredX(const char* text, int textSize);


// =====================================================
// GET CENTERED X POSITION
// =====================================================

int getCenteredX(const char* text, int textSize) {

  int characterWidth = 6 * textSize;
  int textWidth = strlen(text) * characterWidth;

  return (SCREEN_WIDTH - textWidth) / 2;
}


// =====================================================
// SPLASH SCREEN
// =====================================================

void showSplashScreen() {

  // Completely clear the screen before drawing splash
  mylcd.Fill_Screen(BLACK);

  mylcd.Set_Text_colour(WHITE);
  mylcd.Set_Text_Back_colour(BLACK);
  mylcd.Set_Text_Size(3);

  const char* splashText = "Initializing...";

  int textWidth = strlen(splashText) * 18;
  int textX = (SCREEN_WIDTH - textWidth) / 2;

  mylcd.Print_String(
    splashText,
    textX,
    105
  );

  delay(2000);

  // Completely clear splash screen
  mylcd.Fill_Screen(BLACK);

  // Reset display caches
  lastDisplayedSpeedString[0] = '\0';
  lastDisplayedDistanceString[0] = '\0';

  dashboardInitialized = false;
}


// =====================================================
// DRAW SPEED
// =====================================================

void drawSpeed() {

  char speedString[8];

  // Display FILTERED speed
  int roundedSpeed =
    (int)(filteredSpeed + 0.5);

  // Prevent negative values
  if (roundedSpeed < 0) {
    roundedSpeed = 0;
  }

  // No leading zeros
  snprintf(
    speedString,
    sizeof(speedString),
    "%d",
    roundedSpeed
  );

  // Do not redraw if displayed integer
  // hasn't changed
  if (
    dashboardInitialized &&
    strcmp(
      speedString,
      lastDisplayedSpeedString
    ) == 0
  ) {
    return;
  }


  // ===================================================
  // ERASE ONLY PREVIOUS SPEED TEXT
  // ===================================================

  if (lastDisplayedSpeedString[0] != '\0') {

    mylcd.Set_Text_colour(BLACK);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(11);

    int oldSpeedX =
      getCenteredX(
        lastDisplayedSpeedString,
        11
      );

    mylcd.Print_String(
      lastDisplayedSpeedString,
      oldSpeedX,
      20
    );
  }


  // ===================================================
  // DRAW NEW SPEED
  // ===================================================

  mylcd.Set_Text_colour(SPEED_GREEN);
  mylcd.Set_Text_Back_colour(BLACK);
  mylcd.Set_Text_Size(11);

  int speedX =
    getCenteredX(
      speedString,
      11
    );

  mylcd.Print_String(
    speedString,
    speedX,
    20
  );


  // Remember displayed speed
  strcpy(
    lastDisplayedSpeedString,
    speedString
  );
}


// =====================================================
// DRAW TRIP MODE
// =====================================================

void drawTripMode() {

  if (
    dashboardInitialized &&
    currentTripMode == lastDisplayedTripMode
  ) {
    return;
  }


  // Clear only trip-mode area
  mylcd.Fill_Rect(
    0,
    INFO_BAR_Y,
    110,
    INFO_BAR_HEIGHT,
    BLACK
  );


  mylcd.Set_Text_colour(WHITE);
  mylcd.Set_Text_Back_colour(BLACK);
  mylcd.Set_Text_Size(2);

  mylcd.Print_String(
    "Trip (",
    5,
    133
  );


  mylcd.Set_Text_colour(TRIP_RED);


  if (currentTripMode == TRIP_1) {

    mylcd.Print_String(
      "1",
      78,
      133
    );

  }

  else if (currentTripMode == TRIP_2) {

    mylcd.Print_String(
      "2",
      78,
      133
    );

  }

  else {

    mylcd.Print_String(
      "T",
      78,
      133
    );
  }


  mylcd.Set_Text_colour(WHITE);

  mylcd.Print_String(
    ")",
    89,
    133
  );


  lastDisplayedTripMode =
    currentTripMode;
}


// =====================================================
// DRAW DISTANCE
// =====================================================

void drawDistance() {

  uint32_t selectedDistanceMeters = 0;


  // Select currently displayed tripmeter
  switch (currentTripMode) {

    case TRIP_1:

      selectedDistanceMeters =
        trip1DistanceMeters;

      break;


    case TRIP_2:

      selectedDistanceMeters =
        trip2DistanceMeters;

      break;


    case TOTAL:

      selectedDistanceMeters =
        totalDistanceMeters;

      break;
  }


  // Display whole kilometers
  uint32_t distanceKilometers =
    selectedDistanceMeters / 1000UL;


  char distanceString[16];


  ultoa(
    distanceKilometers,
    distanceString,
    10
  );


  // Do not redraw if unchanged
  if (
    dashboardInitialized &&
    strcmp(
      distanceString,
      lastDisplayedDistanceString
    ) == 0
  ) {
    return;
  }


  // Clear distance area
  mylcd.Fill_Rect(
    0,
    DISTANCE_REGION_Y,
    250,
    DISTANCE_REGION_HEIGHT,
    BLACK
  );


  mylcd.Set_Text_colour(TRIP_RED);
  mylcd.Set_Text_Back_colour(BLACK);
  mylcd.Set_Text_Size(4);

  mylcd.Print_String(
    distanceString,
    10,
    185
  );


  // Draw unit
  mylcd.Set_Text_colour(WHITE);
  mylcd.Set_Text_Back_colour(BLACK);
  mylcd.Set_Text_Size(4);

  mylcd.Print_String(
    "km",
    260,
    185
  );


  strcpy(
    lastDisplayedDistanceString,
    distanceString
  );
}


// =====================================================
// DRAW COMPLETE DASHBOARD
// =====================================================

void drawDashboard() {

  // Reset display caches
  lastDisplayedSpeedString[0] = '\0';
  lastDisplayedDistanceString[0] = '\0';

  dashboardInitialized = false;


  // Draw initial speed
  drawSpeed();


  // Draw speed unit once
  mylcd.Set_Text_colour(WHITE);
  mylcd.Set_Text_Back_colour(BLACK);
  mylcd.Set_Text_Size(2);

  mylcd.Print_String(
    "km/h",
    260,
    15
  );


  drawTripMode();
  drawDistance();


  dashboardInitialized = true;
}


// =====================================================
// UPDATE SPEED SENSOR
// =====================================================

void updateSpeedSensor() {

  bool currentSensorState =
    digitalRead(SPEED_SENSOR_PIN);


  // ===================================================
  // DETECT RISING EDGE
  // ===================================================

  if (
    currentSensorState == HIGH &&
    lastSpeedSensorState == LOW
  ) {

    unsigned long currentPulseTimeMicros =
      micros();

    unsigned long currentPulseTimeMillis =
      millis();


    totalSpeedPulses++;


    // =================================================
    // DISTANCE
    // =================================================

    // Each pulse represents:
    //
    // wheel circumference /
    // pulses per wheel revolution
    //
    // With current calibration:
    //
    // 1.675 / 2 = 0.8375 meters per pulse

    float pulseDistanceMeters =
      wheelCircumferenceMeters /
      pulsesPerWheelRevolution;


    // Accumulate fractional distance
    distanceAccumulatorMeters +=
      pulseDistanceMeters;


    // Convert accumulated distance
    // into whole meters
    while (
      distanceAccumulatorMeters >= 1.0
    ) {

      trip1DistanceMeters++;
      trip2DistanceMeters++;
      totalDistanceMeters++;

      distanceAccumulatorMeters -= 1.0;
    }


    // =================================================
    // SPEED CALCULATION
    // =================================================
    //
    // Two pulses represent one complete
    // wheel revolution.
    //
    // Therefore we measure from:
    //
    // Pulse N-2 -> Pulse N
    //
    // rather than using a single pulse interval.
    //
    // This averages the timing of both sensor
    // pulses and should reduce the effect of
    // uneven pulse spacing.

    if (
      previousPulseTimeMicros != 0 &&
      lastPulseTimeMicros != 0
    ) {

      // Time for one complete wheel revolution
      unsigned long revolutionTimeMicros =
        currentPulseTimeMicros -
        previousPulseTimeMicros;


      if (revolutionTimeMicros > 0) {

        // Convert microseconds to seconds
        float revolutionTimeSeconds =
          revolutionTimeMicros / 1000000.0;


        // Distance / time = meters per second
        float speedMetersPerSecond =
          wheelCircumferenceMeters /
          revolutionTimeSeconds;


        // Convert m/s to km/h
        currentSpeed =
          speedMetersPerSecond * 3.6;


        // =============================================
        // SPEED FILTER
        // =============================================

        if (!speedMeasurementInitialized) {

          // First valid measurement:
          // use it immediately rather than starting
          // at 20% of the actual speed.

          filteredSpeed =
            currentSpeed;

          speedMeasurementInitialized =
            true;
        }

        else {

          // Exponential moving average
          filteredSpeed =
            (SPEED_FILTER_ALPHA * currentSpeed) +
            ((1.0 - SPEED_FILTER_ALPHA) *
             filteredSpeed);
        }


        // =============================================
        // SERIAL DEBUG
        // =============================================

        Serial.print("[SPEED] Revolution time: ");
        Serial.print(
          revolutionTimeMicros
        );

        Serial.print(" us | ");

        Serial.print(
          revolutionTimeSeconds,
          4
        );

        Serial.print(" s | Raw: ");

        Serial.print(
          currentSpeed,
          2
        );

        Serial.print(" km/h | Filtered: ");

        Serial.print(
          filteredSpeed,
          2
        );

        Serial.print(" km/h | Pulse: ");

        Serial.println(
          totalSpeedPulses
        );
      }
    }

    else {

      // Not enough pulses yet for a complete
      // wheel revolution measurement.

      Serial.print(
        "[PULSE] First/initial pulse | Pulse: "
      );

      Serial.println(
        totalSpeedPulses
      );
    }


    // =================================================
    // SHIFT PULSE TIMESTAMPS
    // =================================================

    previousPulseTimeMicros =
      lastPulseTimeMicros;

    lastPulseTimeMicros =
      currentPulseTimeMicros;

    lastPulseTimeMillis =
      currentPulseTimeMillis;


    // =================================================
    // UPDATE DISPLAY
    // =================================================

    drawSpeed();

    drawDistance();
  }


  // ===================================================
  // SAVE CURRENT SENSOR STATE
  // ===================================================

  lastSpeedSensorState =
    currentSensorState;


  // ===================================================
  // SPEED TIMEOUT
  // ===================================================

  if (
    lastPulseTimeMillis != 0 &&
    millis() - lastPulseTimeMillis >=
      SPEED_TIMEOUT_MS
  ) {

    // Only do this if speed isn't already zero
    if (
      currentSpeed != 0.0 ||
      filteredSpeed != 0.0
    ) {

      currentSpeed = 0.0;
      filteredSpeed = 0.0;

      speedMeasurementInitialized =
        false;

      drawSpeed();


      Serial.println(
        "[SPEED] Timeout - wheel stopped"
      );
    }
  }
}


// =====================================================
// CHANGE TRIP MODE
// =====================================================

void changeTripMode() {

  switch (currentTripMode) {

    case TRIP_1:

      currentTripMode = TRIP_2;

      break;


    case TRIP_2:

      currentTripMode = TOTAL;

      break;


    case TOTAL:

      currentTripMode = TRIP_1;

      break;
  }


  // Trip values remain untouched
  drawTripMode();
  drawDistance();
}


// =====================================================
// RESET SELECTED TRIP
// =====================================================

void resetSelectedTrip() {

  switch (currentTripMode) {

    case TRIP_1:

      trip1DistanceMeters = 0;

      break;


    case TRIP_2:

      trip2DistanceMeters = 0;

      break;


    case TOTAL:

      // Total odometer cannot be reset

      break;
  }


  drawDistance();
}


// =====================================================
// UPDATE BUTTONS
// =====================================================

void updateButtons() {

  bool currentModeButtonState =
    digitalRead(BUTTON_MODE);


  // ===================================================
  // BUTTON JUST PRESSED
  // ===================================================

  if (
    currentModeButtonState == LOW &&
    lastModeButtonState == HIGH
  ) {

    modeButtonPressedAt =
      millis();

    modeLongPressHandled =
      false;
  }


  // ===================================================
  // BUTTON BEING HELD
  // ===================================================

  if (
    currentModeButtonState == LOW &&
    !modeLongPressHandled &&
    millis() -
      modeButtonPressedAt >=
      BUTTON_HOLD_TIME
  ) {

    resetSelectedTrip();

    modeLongPressHandled =
      true;
  }


  // ===================================================
  // BUTTON JUST RELEASED
  // ===================================================

  if (
    currentModeButtonState == HIGH &&
    lastModeButtonState == LOW
  ) {

    // Short press
    if (!modeLongPressHandled) {

      changeTripMode();
    }
  }


  lastModeButtonState =
    currentModeButtonState;
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);


  // ===================================================
  // SENSOR
  // ===================================================

  pinMode(
    SPEED_SENSOR_PIN,
    INPUT_PULLUP
  );


  // ===================================================
  // MODE BUTTON
  // ===================================================

  pinMode(
    BUTTON_MODE,
    INPUT_PULLUP
  );


  // ===================================================
  // TFT
  // ===================================================

  mylcd.Init_LCD();

  mylcd.Set_Rotation(1);


  // ===================================================
  // SPLASH SCREEN
  // ===================================================

  showSplashScreen();


  // ===================================================
  // INITIAL DASHBOARD
  // ===================================================

  drawDashboard();


  // ===================================================
  // ESTABLISH INITIAL SENSOR STATE
  // ===================================================

  lastSpeedSensorState =
    digitalRead(SPEED_SENSOR_PIN);
}


// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  updateSpeedSensor();

  updateButtons();
}