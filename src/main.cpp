#include <Arduino.h>
#include <LCDWIKI_GUI.h>
#include <LCDWIKI_SPI.h>

#define RTC_SDA_PIN 4
#define RTC_SCL_PIN 5
#define DS3231_ADDRESS 0x68

#define BUTTON_MODE 7
#define BUTTON_CLOCK 2
#define BUTTON_HOLD_TIME 2000

#define TFT_CS 8
#define TFT_DC 9
#define TFT_RESET 10

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define BLACK 0x0000
#define WHITE 0xFFFF
#define SPEED_GREEN 0x07E0
#define TRIP_RED 0xF800
#define DATE_ORANGE 0xFD20
#define TIME_CYAN 0x07FF
#define EDIT_GREEN 0x07E0

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

enum TripMode {
    TRIP_1,
    TRIP_2,
    TOTAL
};

TripMode currentTripMode = TRIP_1;

int currentSpeed = 870;

uint32_t trip1Distance = 1000000UL;
uint32_t trip2Distance = 1000000UL;
uint32_t totalDistance = 624697UL;

struct DateTime {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t date;
    uint8_t month;
    uint16_t year;
};

DateTime settingDateTime;
bool clockSettingMode = false;
uint8_t settingField = 0;

bool lastModeButtonState = HIGH;
bool lastClockButtonState = HIGH;

unsigned long modeButtonPressedAt = 0;
unsigned long clockButtonPressedAt = 0;

bool modeLongPressHandled = false;
bool clockLongPressHandled = false;

void i2cDelay() {
    delayMicroseconds(10);
}

void releaseSDA() {
    pinMode(RTC_SDA_PIN, INPUT_PULLUP);
}

void pullSDA_LOW() {
    pinMode(RTC_SDA_PIN, OUTPUT);
    digitalWrite(RTC_SDA_PIN, LOW);
}

void releaseSCL() {
    pinMode(RTC_SCL_PIN, INPUT_PULLUP);
}

void pullSCL_LOW() {
    pinMode(RTC_SCL_PIN, OUTPUT);
    digitalWrite(RTC_SCL_PIN, LOW);
}

void i2cStart() {
    releaseSDA();
    releaseSCL();
    i2cDelay();
    pullSDA_LOW();
    i2cDelay();
    pullSCL_LOW();
    i2cDelay();
}

void i2cStop() {
    pullSDA_LOW();
    i2cDelay();
    releaseSCL();
    i2cDelay();
    releaseSDA();
    i2cDelay();
}

bool i2cWriteByte(uint8_t data) {
    for (int bit = 7; bit >= 0; bit--) {
        if (data & (1 << bit))
            releaseSDA();
        else
            pullSDA_LOW();

        i2cDelay();
        releaseSCL();
        i2cDelay();
        pullSCL_LOW();
        i2cDelay();
    }

    releaseSDA();
    i2cDelay();
    releaseSCL();
    i2cDelay();

    bool ack = digitalRead(RTC_SDA_PIN) == LOW;

    pullSCL_LOW();
    i2cDelay();

    return ack;
}

uint8_t i2cReadByte(bool sendAck) {
    uint8_t data = 0;

    releaseSDA();

    for (int bit = 7; bit >= 0; bit--) {
        releaseSCL();
        i2cDelay();

        if (digitalRead(RTC_SDA_PIN))
            data |= (1 << bit);

        pullSCL_LOW();
        i2cDelay();
    }

    if (sendAck)
        pullSDA_LOW();
    else
        releaseSDA();

    i2cDelay();
    releaseSCL();
    i2cDelay();
    pullSCL_LOW();
    releaseSDA();
    i2cDelay();

    return data;
}

uint8_t bcdToDecimal(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

uint8_t decimalToBCD(uint8_t value) {
    return ((value / 10) << 4) | (value % 10);
}

bool readDateTime(DateTime &dt) {
    uint8_t data[7];

    i2cStart();

    if (!i2cWriteByte((DS3231_ADDRESS << 1) | 0)) {
        i2cStop();
        return false;
    }

    if (!i2cWriteByte(0x00)) {
        i2cStop();
        return false;
    }

    i2cStart();

    if (!i2cWriteByte((DS3231_ADDRESS << 1) | 1)) {
        i2cStop();
        return false;
    }

    for (int i = 0; i < 7; i++)
        data[i] = i2cReadByte(i < 6);

    i2cStop();

    dt.seconds = bcdToDecimal(data[0] & 0x7F);
    dt.minutes = bcdToDecimal(data[1] & 0x7F);
    dt.hours = bcdToDecimal(data[2] & 0x3F);
    dt.day = bcdToDecimal(data[3] & 0x07);
    dt.date = bcdToDecimal(data[4] & 0x3F);
    dt.month = bcdToDecimal(data[5] & 0x1F);
    dt.year = 2000 + bcdToDecimal(data[6]);

    return true;
}

bool writeDateTime(const DateTime &dt) {
    i2cStart();

    if (!i2cWriteByte((DS3231_ADDRESS << 1) | 0)) {
        i2cStop();
        return false;
    }

    if (!i2cWriteByte(0x00)) {
        i2cStop();
        return false;
    }

    if (!i2cWriteByte(decimalToBCD(0)))
        goto write_error;

    if (!i2cWriteByte(decimalToBCD(dt.minutes)))
        goto write_error;

    if (!i2cWriteByte(decimalToBCD(dt.hours)))
        goto write_error;

    if (!i2cWriteByte(decimalToBCD(dt.day)))
        goto write_error;

    if (!i2cWriteByte(decimalToBCD(dt.date)))
        goto write_error;

    if (!i2cWriteByte(decimalToBCD(dt.month)))
        goto write_error;

    if (!i2cWriteByte(decimalToBCD(dt.year - 2000)))
        goto write_error;

    i2cStop();
    return true;

write_error:
    i2cStop();
    return false;
}

uint8_t calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t date) {
    if (month < 3) {
        month += 12;
        year--;
    }

    uint16_t k = year % 100;
    uint16_t j = year / 100;

    uint16_t h =
        (date +
         ((13 * (month + 1)) / 5) +
         k +
         (k / 4) +
         (j / 4) +
         (5 * j)) % 7;

    return ((h + 6) % 7) + 1;
}

int getTextWidth(const char *text, uint8_t textSize) {
    return strlen(text) * 6 * textSize;
}

int getCenteredX(const char *text, uint8_t textSize) {
    return (SCREEN_WIDTH - getTextWidth(text, textSize)) / 2;
}

void showSplashScreen() {
    mylcd.Fill_Screen(BLACK);
    mylcd.Set_Text_colour(WHITE);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(3);

    const char *message = "Initializing...";

    mylcd.Print_String(
        message,
        getCenteredX(message, 3),
        105
    );
}

void drawSpeed() {
    mylcd.Fill_Rect(
        0,
        SPEED_REGION_Y,
        SCREEN_WIDTH,
        SPEED_REGION_HEIGHT,
        BLACK
    );

    char speedString[8];

    snprintf(
        speedString,
        sizeof(speedString),
        "%03d",
        currentSpeed
    );

    mylcd.Set_Text_colour(SPEED_GREEN);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(11);

    int speedX = getCenteredX(speedString, 12);

    mylcd.Print_String(
        speedString,
        speedX,
        20
    );

    mylcd.Set_Text_colour(WHITE);
    mylcd.Set_Text_Size(2);

    mylcd.Print_String(
        "km/h",
        260,
        15
    );
}

void drawTripMode() {
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(2);

    mylcd.Set_Text_colour(WHITE);
    mylcd.Print_String("Trip (", 5, 133);

    mylcd.Set_Text_colour(TRIP_RED);

    if (currentTripMode == TRIP_1)
        mylcd.Print_String("1", 78, 133);
    else if (currentTripMode == TRIP_2)
        mylcd.Print_String("2", 78, 133);
    else
        mylcd.Print_String("T", 78, 133);

    mylcd.Set_Text_colour(WHITE);
    mylcd.Print_String(")", 89, 133);
}

void drawDate(const DateTime &dt) {
    char dateString[11];

    snprintf(
        dateString,
        sizeof(dateString),
        "%02d-%02d-%04d",
        dt.month,
        dt.date,
        dt.year
    );

    mylcd.Set_Text_colour(DATE_ORANGE);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(2);

    mylcd.Print_String(
        dateString,
        120,
        133
    );
}

void drawTime(const DateTime &dt, bool colonVisible) {
    char timeString[6];

    if (colonVisible)
        snprintf(
            timeString,
            sizeof(timeString),
            "%02d:%02d",
            dt.hours,
            dt.minutes
        );
    else
        snprintf(
            timeString,
            sizeof(timeString),
            "%02d %02d",
            dt.hours,
            dt.minutes
        );

    mylcd.Set_Text_colour(TIME_CYAN);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(2);

    mylcd.Print_String(
        timeString,
        258,
        133
    );
}

void drawDistance() {
    uint32_t distance;

    if (currentTripMode == TRIP_1)
        distance = trip1Distance;
    else if (currentTripMode == TRIP_2)
        distance = trip2Distance;
    else
        distance = totalDistance;

    mylcd.Fill_Rect(
        0,
        DISTANCE_REGION_Y,
        SCREEN_WIDTH,
        DISTANCE_REGION_HEIGHT,
        BLACK
    );

    char distanceString[20];

    ultoa(distance, distanceString, 10);

    mylcd.Set_Text_colour(WHITE);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(4);

    mylcd.Print_String(
        distanceString,
        10,
        185
    );

    mylcd.Print_String(
        "km",
        260,
        185
    );
}

void drawDashboard(const DateTime &dt, bool colonVisible) {
    drawSpeed();
    drawTripMode();

    mylcd.Fill_Rect(
        100,
        120,
        210,
        45,
        BLACK
    );

    drawDate(dt);
    drawTime(dt, colonVisible);

    drawDistance();
}

void clearDateField(uint8_t field) {
    mylcd.Set_Text_Back_colour(BLACK);

    if (field == 0)
        mylcd.Fill_Rect(120, 130, 12, 20, BLACK);

    else if (field == 1)
        mylcd.Fill_Rect(138, 130, 12, 20, BLACK);

    else if (field == 2)
        mylcd.Fill_Rect(156, 130, 24, 20, BLACK);
}

void clearTimeField(uint8_t field) {
    mylcd.Set_Text_Back_colour(BLACK);

    if (field == 3)
        mylcd.Fill_Rect(258, 130, 12, 20, BLACK);

    else if (field == 4)
        mylcd.Fill_Rect(276, 130, 12, 20, BLACK);
}

void drawEditField(uint8_t field) {
    mylcd.Set_Text_Size(2);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_colour(EDIT_GREEN);

    char value[6];

    if (field == 0) {
        clearDateField(0);

        snprintf(
            value,
            sizeof(value),
            "%02d",
            settingDateTime.month
        );

        mylcd.Print_String(value, 120, 133);
    }

    else if (field == 1) {
        clearDateField(1);

        snprintf(
            value,
            sizeof(value),
            "%02d",
            settingDateTime.date
        );

        mylcd.Print_String(value, 138, 133);
    }

    else if (field == 2) {
        clearDateField(2);

        snprintf(
            value,
            sizeof(value),
            "%04d",
            settingDateTime.year
        );

        mylcd.Print_String(value, 156, 133);
    }

    else if (field == 3) {
        clearTimeField(3);

        snprintf(
            value,
            sizeof(value),
            "%02d",
            settingDateTime.hours
        );

        mylcd.Print_String(value, 258, 133);
    }

    else if (field == 4) {
        clearTimeField(4);

        snprintf(
            value,
            sizeof(value),
            "%02d",
            settingDateTime.minutes
        );

        mylcd.Print_String(value, 276, 133);
    }
}

void restoreEditField(uint8_t field) {
    if (field < 3) {
        clearDateField(field);

        mylcd.Set_Text_Size(2);
        mylcd.Set_Text_Back_colour(BLACK);
        mylcd.Set_Text_colour(DATE_ORANGE);

        char value[6];

        if (field == 0) {
            snprintf(
                value,
                sizeof(value),
                "%02d",
                settingDateTime.month
            );
            mylcd.Print_String(value, 120, 133);
        }

        else if (field == 1) {
            snprintf(
                value,
                sizeof(value),
                "%02d",
                settingDateTime.date
            );
            mylcd.Print_String(value, 138, 133);
        }

        else {
            snprintf(
                value,
                sizeof(value),
                "%04d",
                settingDateTime.year
            );
            mylcd.Print_String(value, 156, 133);
        }
    }

    else {
        clearTimeField(field);

        mylcd.Set_Text_Size(2);
        mylcd.Set_Text_Back_colour(BLACK);
        mylcd.Set_Text_colour(TIME_CYAN);

        char value[6];

        if (field == 3) {
            snprintf(
                value,
                sizeof(value),
                "%02d",
                settingDateTime.hours
            );
            mylcd.Print_String(value, 258, 133);
        }

        else {
            snprintf(
                value,
                sizeof(value),
                "%02d",
                settingDateTime.minutes
            );
            mylcd.Print_String(value, 276, 133);
        }
    }
}

bool isLeapYear(uint16_t year) {
    return (
        (year % 4 == 0 && year % 100 != 0) ||
        year % 400 == 0
    );
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
    if (month == 2)
        return isLeapYear(year) ? 29 : 28;

    if (
        month == 4 ||
        month == 6 ||
        month == 9 ||
        month == 11
    )
        return 30;

    return 31;
}

void enterClockSettingMode() {
    DateTime current;

    if (!readDateTime(current)) {
        Serial.println("[CLOCK] ERROR: Cannot read RTC.");
        return;
    }

    settingDateTime = current;
    settingField = 0;
    clockSettingMode = true;

    Serial.println("[CLOCK] Entering date/time setting.");

    drawEditField(settingField);
}

void nextSettingField() {
    uint8_t oldField = settingField;

    restoreEditField(oldField);

    settingField++;

    if (settingField > 4)
        settingField = 0;

    drawEditField(settingField);

    Serial.print("[CLOCK] Field: ");

    if (settingField == 0)
        Serial.println("MONTH");
    else if (settingField == 1)
        Serial.println("DAY");
    else if (settingField == 2)
        Serial.println("YEAR");
    else if (settingField == 3)
        Serial.println("HOUR");
    else
        Serial.println("MINUTE");
}

void incrementSettingField() {
    if (settingField == 0) {
        settingDateTime.month++;

        if (settingDateTime.month > 12)
            settingDateTime.month = 1;

        uint8_t maxDay = daysInMonth(
            settingDateTime.year,
            settingDateTime.month
        );

        if (settingDateTime.date > maxDay)
            settingDateTime.date = maxDay;
    }

    else if (settingField == 1) {
        settingDateTime.date++;

        if (
            settingDateTime.date >
            daysInMonth(
                settingDateTime.year,
                settingDateTime.month
            )
        )
            settingDateTime.date = 1;
    }

    else if (settingField == 2) {
        settingDateTime.year++;

        if (settingDateTime.year > 2099)
            settingDateTime.year = 2000;

        uint8_t maxDay = daysInMonth(
            settingDateTime.year,
            settingDateTime.month
        );

        if (settingDateTime.date > maxDay)
            settingDateTime.date = maxDay;
    }

    else if (settingField == 3) {
        settingDateTime.hours++;

        if (settingDateTime.hours > 23)
            settingDateTime.hours = 0;
    }

    else if (settingField == 4) {
        settingDateTime.minutes++;

        if (settingDateTime.minutes > 59)
            settingDateTime.minutes = 0;
    }

    drawEditField(settingField);
}

void saveClockSetting() {
    settingDateTime.day = calculateDayOfWeek(
        settingDateTime.year,
        settingDateTime.month,
        settingDateTime.date
    );

    settingDateTime.seconds = 0;

    if (writeDateTime(settingDateTime)) {
        Serial.println("[CLOCK] Date/time saved.");

        clockSettingMode = false;

        DateTime current;

        if (readDateTime(current)) {
            drawDashboard(current, true);
        }
    }
    else {
        Serial.println("[CLOCK] ERROR: Failed to save RTC.");
    }
}

void updateButtons() {
    bool modeState = digitalRead(BUTTON_MODE);
    bool clockState = digitalRead(BUTTON_CLOCK);

    unsigned long currentMillis = millis();

    if (
        modeState == LOW &&
        lastModeButtonState == HIGH
    ) {
        modeButtonPressedAt = currentMillis;
        modeLongPressHandled = false;
    }

    if (
        modeState == LOW &&
        !modeLongPressHandled &&
        currentMillis - modeButtonPressedAt >= BUTTON_HOLD_TIME
    ) {
        modeLongPressHandled = true;

        if (!clockSettingMode) {
            if (currentTripMode == TRIP_1) {
                trip1Distance = 0;
                Serial.println("[TRIP] Trip 1 reset.");
                drawDistance();
            }

            else if (currentTripMode == TRIP_2) {
                trip2Distance = 0;
                Serial.println("[TRIP] Trip 2 reset.");
                drawDistance();
            }

            else {
                Serial.println("[TRIP] TTL cannot be reset.");
            }
        }
    }

    if (
        modeState == HIGH &&
        lastModeButtonState == LOW
    ) {
        if (!modeLongPressHandled) {
            if (clockSettingMode) {
                nextSettingField();
            }
            else {
                if (currentTripMode == TRIP_1)
                    currentTripMode = TRIP_2;
                else if (currentTripMode == TRIP_2)
                    currentTripMode = TOTAL;
                else
                    currentTripMode = TRIP_1;

                drawTripMode();
                drawDistance();

                Serial.print("[TRIP] Mode: ");

                if (currentTripMode == TRIP_1)
                    Serial.println("TRIP 1");
                else if (currentTripMode == TRIP_2)
                    Serial.println("TRIP 2");
                else
                    Serial.println("TTL");
            }
        }
    }

    if (
        clockState == LOW &&
        lastClockButtonState == HIGH
    ) {
        clockButtonPressedAt = currentMillis;
        clockLongPressHandled = false;
    }

    if (
        clockState == LOW &&
        !clockLongPressHandled &&
        currentMillis - clockButtonPressedAt >= BUTTON_HOLD_TIME
    ) {
        clockLongPressHandled = true;

        if (clockSettingMode)
            saveClockSetting();
        else
            enterClockSettingMode();
    }

    if (
        clockState == HIGH &&
        lastClockButtonState == LOW
    ) {
        if (!clockLongPressHandled && clockSettingMode)
            incrementSettingField();
    }

    lastModeButtonState = modeState;
    lastClockButtonState = clockState;
}

void setup() {
    Serial.begin(115200);

    delay(2000);

    Serial.println();
    Serial.println("==============================");
    Serial.println("MOTORCYCLE DASHBOARD");
    Serial.println("==============================");

    pinMode(BUTTON_MODE, INPUT_PULLUP);
    pinMode(BUTTON_CLOCK, INPUT_PULLUP);

    releaseSDA();
    releaseSCL();

    delay(100);

    Serial.println("[TFT] Initializing...");

    mylcd.Init_LCD();
    mylcd.Set_Rotation(1);
    mylcd.Set_Text_Mode(0);

    Serial.println("[TFT] Initialization complete.");

    showSplashScreen();

    delay(2000);

    Serial.println("[RTC] Testing DS3231M...");

    DateTime current;

    if (readDateTime(current)) {
        Serial.println("[RTC] DS3231M communication OK.");

        Serial.print("[RTC] Time: ");

        if (current.hours < 10)
            Serial.print("0");

        Serial.print(current.hours);
        Serial.print(":");

        if (current.minutes < 10)
            Serial.print("0");

        Serial.print(current.minutes);
        Serial.print(":");

        if (current.seconds < 10)
            Serial.print("0");

        Serial.println(current.seconds);

        Serial.print("[RTC] Date: ");

        if (current.month < 10)
            Serial.print("0");

        Serial.print(current.month);
        Serial.print("-");

        if (current.date < 10)
            Serial.print("0");

        Serial.print(current.date);
        Serial.print("-");

        Serial.println(current.year);

        drawDashboard(current, true);
    }
    else {
        Serial.println("[RTC] ERROR: DS3231M did not respond.");
    }

    Serial.println("[BOOT] Dashboard ready.");
}

void loop() {
    static unsigned long lastRTCRead = 0;
    static unsigned long lastColonToggle = 0;
    static bool colonVisible = true;

    unsigned long currentMillis = millis();

    updateButtons();

    if (!clockSettingMode) {
        if (currentMillis - lastRTCRead >= 250) {
            lastRTCRead = currentMillis;

            DateTime current;

            if (readDateTime(current)) {
                Serial.print("[RTC] ");

                if (current.hours < 10)
                    Serial.print("0");

                Serial.print(current.hours);
                Serial.print(":");

                if (current.minutes < 10)
                    Serial.print("0");

                Serial.print(current.minutes);
                Serial.print(":");

                if (current.seconds < 10)
                    Serial.print("0");

                Serial.print(current.seconds);
                Serial.print("  ");

                if (current.month < 10)
                    Serial.print("0");

                Serial.print(current.month);
                Serial.print("-");

                if (current.date < 10)
                    Serial.print("0");

                Serial.print(current.date);
                Serial.print("-");

                Serial.println(current.year);
            }
        }

        if (currentMillis - lastColonToggle >= 500) {
            lastColonToggle = currentMillis;
            colonVisible = !colonVisible;

            DateTime current;

            if (readDateTime(current))
                drawTime(current, colonVisible);
        }
    }
}