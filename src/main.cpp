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

// The sensor produces two pulses per wheel revolution
float pulsesPerWheelRevolution = 2.0;

// Wheel circumference in meters
float wheelCircumferenceMeters = 1.675;

// =====================================================
// SPEED SENSOR VARIABLES
// =====================================================

bool lastSpeedSensorState = HIGH;

uint32_t totalSpeedPulses = 0;

// Pulse timing
unsigned long lastPulseTimeMicros = 0;
unsigned long lastPulseTimeMillis = 0;

// Speed values
float currentSpeed = 0.0;
float filteredSpeed = 0.0;

// Smoothing factor:
// Lower value = smoother but slower response
// Higher value = faster response but more sensitive to noise
const float SPEED_FILTER_ALPHA = 0.20;

// If no pulse arrives within this time, consider the wheel stopped
#define SPEED_TIMEOUT_MS 1500

// =====================================================
// TRIPMETER VARIABLES
// =====================================================

// Distances are stored as whole meters.
//
// Total odometer starts at 624,697 km.
//
// 624,697 km × 1,000 = 624,697,000 meters

uint32_t trip1DistanceMeters = 0;
uint32_t trip2DistanceMeters = 0;
uint32_t totalDistanceMeters = 624697000UL;

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
  // Completely clear the screen before drawing the splash
  mylcd.Fill_Screen(BLACK);

  mylcd.Set_Text_colour(WHITE);
  mylcd.Set_Text_Back_colour(BLACK);
  mylcd.Set_Text_Size(3);

  const char* splashText = "Initializing...";

  int textWidth = strlen(splashText) * 18;
  int textX = (SCREEN_WIDTH - textWidth) / 2;

  mylcd.Print_String(splashText, textX, 105);

  delay(2000);

  // Completely clear the splash screen.
  // This prevents leftover splash pixels from appearing
  // behind the dashboard.
  mylcd.Fill_Screen(BLACK);

  // Reset all display caches so the dashboard is fully drawn.
  lastDisplayedSpeedString[0] = '\0';
  lastDisplayedDistanceString[0] = '\0';

  dashboardInitialized = false;
}

// =====================================================
// DRAW SPEED
// =====================================================

void drawSpeed() {
    char speedString[8];

    int roundedSpeed = (int)(currentSpeed + 0.5);

    // Prevent negative values
    if (roundedSpeed < 0) {
    roundedSpeed = 0;
    }

    snprintf(
    speedString,
    sizeof(speedString),
    "%d",
    roundedSpeed
    );

    // Skip drawing if the displayed value has not changed
    if (
    dashboardInitialized &&
    strcmp(speedString, lastDisplayedSpeedString) == 0
    ) {
    return;
    }

    // Clear only the actual number area.
    // Adjust these values if your large font extends beyond this region.
    mylcd.Fill_Rect(
    65,
    15,
    150,
    105,
    BLACK
    );

    mylcd.Set_Text_colour(SPEED_GREEN);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(11);

    // Center the number based on its actual digit count
    int speedX = getCenteredX(speedString, 11);

    mylcd.Print_String(
    speedString,
    speedX,
    20
    );

    // Draw the unit separately
    mylcd.Set_Text_colour(WHITE);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(2);

    mylcd.Print_String(
    "km/h",
    260,
    15
    );

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

  // Clear only the trip-mode area
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

  lastDisplayedTripMode = currentTripMode;
}

// =====================================================
// DRAW DISTANCE
// =====================================================

void drawDistance() {
  uint32_t selectedDistanceMeters = 0;

  // Select the currently displayed tripmeter.
  // The other tripmeter values remain untouched.
  switch (currentTripMode) {
    case TRIP_1:
      selectedDistanceMeters = trip1DistanceMeters;
      break;

    case TRIP_2:
      selectedDistanceMeters = trip2DistanceMeters;
      break;

    case TOTAL:
      selectedDistanceMeters = totalDistanceMeters;
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

  // Do not redraw if the displayed value has not changed
  if (
    dashboardInitialized &&
    strcmp(distanceString, lastDisplayedDistanceString) == 0
  ) {
    return;
  }

  // Clear only the distance number area
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

  // Redraw the unit because it is inside the cleared area
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
  // Reset display caches so every element is redrawn
  lastDisplayedSpeedString[0] = '\0';
  lastDisplayedDistanceString[0] = '\0';

  dashboardInitialized = false;

  drawSpeed();
  drawTripMode();
  drawDistance();

  dashboardInitialized = true;
}

// =====================================================
// UPDATE SPEED SENSOR
// =====================================================

void updateSpeedSensor() {
    bool currentSensorState = digitalRead(SPEED_SENSOR_PIN);

    // Detect a LOW-to-HIGH transition
    if (
        currentSensorState == HIGH &&
        lastSpeedSensorState == LOW
    ) {
        unsigned long currentPulseTimeMicros = micros();
        unsigned long currentPulseTimeMillis = millis();

        totalSpeedPulses++;

        // Only calculate speed if this is not the first pulse
        if (lastPulseTimeMicros != 0) {
        unsigned long pulseIntervalMicros =
            currentPulseTimeMicros - lastPulseTimeMicros;

        // Avoid invalid or extremely short pulse intervals
        if (pulseIntervalMicros > 1000) {
            float pulseDistanceMeters =
            wheelCircumferenceMeters / pulsesPerWheelRevolution;

            float pulseIntervalSeconds =
            pulseIntervalMicros / 1000000.0;

            float speedMetersPerSecond =
            pulseDistanceMeters / pulseIntervalSeconds;

            float rawSpeedKph =
            speedMetersPerSecond * 3.6;

            // Exponential moving average
            filteredSpeed =
            filteredSpeed +
            SPEED_FILTER_ALPHA * (rawSpeedKph - filteredSpeed);

            currentSpeed = filteredSpeed;

            Serial.print("[SPEED] Pulse interval: ");
            Serial.print(pulseIntervalMicros);
            Serial.print(" us | Raw speed: ");
            Serial.print(rawSpeedKph, 2);
            Serial.print(" km/h | Filtered speed: ");
            Serial.print(currentSpeed, 2);
            Serial.print(" km/h | Total pulses: ");
            Serial.println(totalSpeedPulses);
        }
        }

        lastPulseTimeMicros = currentPulseTimeMicros;
        lastPulseTimeMillis = currentPulseTimeMillis;
    }

    lastSpeedSensorState = currentSensorState;

    // If no pulse has arrived for a while, gradually bring speed to zero
    if (
        lastPulseTimeMillis != 0 &&
        millis() - lastPulseTimeMillis >= SPEED_TIMEOUT_MS
    ) {
        filteredSpeed = 0.0;
        currentSpeed = 0.0;
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

  // Only change the displayed mode.
  // Trip 1, Trip 2, and Total retain their values.
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
      // The total odometer cannot be reset
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

  // Button has just been pressed
  if (
    currentModeButtonState == LOW &&
    lastModeButtonState == HIGH
  ) {
    modeButtonPressedAt = millis();
    modeLongPressHandled = false;
  }

  // Button is being held
  if (
    currentModeButtonState == LOW &&
    !modeLongPressHandled &&
    millis() - modeButtonPressedAt >= BUTTON_HOLD_TIME
  ) {
    resetSelectedTrip();

    modeLongPressHandled = true;
  }

  // Button has just been released
  if (
    currentModeButtonState == HIGH &&
    lastModeButtonState == LOW
  ) {
    // Short press changes the trip mode
    if (!modeLongPressHandled) {
      changeTripMode();
    }
  }

  lastModeButtonState = currentModeButtonState;
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  pinMode(SPEED_SENSOR_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MODE, INPUT_PULLUP);

  mylcd.Init_LCD();
  mylcd.Set_Rotation(1);

  // Show and then completely clear the splash screen
  showSplashScreen();

  // Draw the initial dashboard
  drawDashboard();

  lastSpeedSensorState = digitalRead(SPEED_SENSOR_PIN);
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  updateSpeedSensor();
  updateButtons();
}