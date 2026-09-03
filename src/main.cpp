#include <Arduino.h>

#include <LCDWIKI_GUI.h>
#include <LCDWIKI_SPI.h>


// ============================================================
// PIN DEFINITIONS
// ============================================================

// --------------------
// DS3231M Software I2C
// --------------------

#define RTC_SDA_PIN 4
#define RTC_SCL_PIN 5

#define DS3231_ADDRESS 0x68


// --------------------
// TFT
// --------------------

#define TFT_CS     8
#define TFT_DC     9
#define TFT_RESET  10


// ============================================================
// TFT OBJECT
// ============================================================

LCDWIKI_SPI mylcd(
    ILI9341,
    TFT_CS,
    TFT_DC,
    TFT_RESET,
    -1
);


// ============================================================
// SCREEN
// ============================================================

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240


// ============================================================
// COLORS
// ============================================================

#define BLACK   0x0000
#define WHITE   0xFFFF

// Match your sample design
#define SPEED_GREEN   0x07E0
#define TRIP_RED      0xF800
#define DATE_ORANGE   0xFD20
#define TIME_CYAN     0x07FF


// ============================================================
// DISPLAY REGIONS
// ============================================================

#define SPEED_REGION_Y          0
#define SPEED_REGION_HEIGHT     120

#define INFO_BAR_Y              120
#define INFO_BAR_HEIGHT         45

#define DISTANCE_REGION_Y       165
#define DISTANCE_REGION_HEIGHT  75


// ============================================================
// TRIP MODE
// ============================================================

enum TripMode
{
    TRIP_1,
    TRIP_2,
    TOTAL
};

TripMode currentTripMode = TRIP_1;


// ============================================================
// PLACEHOLDER VALUES
// ============================================================

int currentSpeed = 870;

float trip1Distance = 1000000.0;
float trip2Distance = 1000000.0;
float totalDistance = 1000000.0;


// ============================================================
// RTC STRUCTURE
// ============================================================

struct DateTime
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;

    uint8_t day;
    uint8_t date;
    uint8_t month;

    uint16_t year;
};


// ============================================================
// SOFTWARE I2C
// ============================================================

void i2cDelay()
{
    delayMicroseconds(10);
}


void releaseSDA()
{
    pinMode(RTC_SDA_PIN, INPUT_PULLUP);
}


void pullSDA_LOW()
{
    pinMode(RTC_SDA_PIN, OUTPUT);
    digitalWrite(RTC_SDA_PIN, LOW);
}


void releaseSCL()
{
    pinMode(RTC_SCL_PIN, INPUT_PULLUP);
}


void pullSCL_LOW()
{
    pinMode(RTC_SCL_PIN, OUTPUT);
    digitalWrite(RTC_SCL_PIN, LOW);
}


// ------------------------------------------------------------
// I2C START
// ------------------------------------------------------------

void i2cStart()
{
    releaseSDA();
    releaseSCL();

    i2cDelay();

    pullSDA_LOW();

    i2cDelay();

    pullSCL_LOW();

    i2cDelay();
}


// ------------------------------------------------------------
// I2C STOP
// ------------------------------------------------------------

void i2cStop()
{
    pullSDA_LOW();

    i2cDelay();

    releaseSCL();

    i2cDelay();

    releaseSDA();

    i2cDelay();
}


// ------------------------------------------------------------
// Write one byte
// ------------------------------------------------------------

bool i2cWriteByte(uint8_t data)
{
    for (int bit = 7; bit >= 0; bit--)
    {
        if (data & (1 << bit))
        {
            releaseSDA();
        }
        else
        {
            pullSDA_LOW();
        }

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

    bool ack = (digitalRead(RTC_SDA_PIN) == LOW);

    pullSCL_LOW();

    i2cDelay();

    return ack;
}


// ------------------------------------------------------------
// Read one byte
// ------------------------------------------------------------

uint8_t i2cReadByte(bool sendAck)
{
    uint8_t data = 0;

    releaseSDA();

    for (int bit = 7; bit >= 0; bit--)
    {
        releaseSCL();

        i2cDelay();

        if (digitalRead(RTC_SDA_PIN))
        {
            data |= (1 << bit);
        }

        pullSCL_LOW();

        i2cDelay();
    }

    if (sendAck)
    {
        pullSDA_LOW();
    }
    else
    {
        releaseSDA();
    }

    i2cDelay();

    releaseSCL();

    i2cDelay();

    pullSCL_LOW();

    releaseSDA();

    i2cDelay();

    return data;
}


// ============================================================
// BCD CONVERSION
// ============================================================

uint8_t bcdToDecimal(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}


// ============================================================
// READ DS3231M
// ============================================================

bool readDateTime(DateTime &dt)
{
    uint8_t data[7];


    // --------------------------------------------------------
    // Start write transaction
    // --------------------------------------------------------

    i2cStart();

    if (!i2cWriteByte((DS3231_ADDRESS << 1) | 0))
    {
        i2cStop();
        return false;
    }


    // Start at seconds register
    if (!i2cWriteByte(0x00))
    {
        i2cStop();
        return false;
    }


    // --------------------------------------------------------
    // Repeated START
    // --------------------------------------------------------

    i2cStart();

    if (!i2cWriteByte((DS3231_ADDRESS << 1) | 1))
    {
        i2cStop();
        return false;
    }


    // --------------------------------------------------------
    // Read registers 0x00 - 0x06
    // --------------------------------------------------------

    for (int i = 0; i < 7; i++)
    {
        data[i] = i2cReadByte(i < 6);
    }


    i2cStop();


    // --------------------------------------------------------
    // Convert BCD
    // --------------------------------------------------------

    dt.seconds = bcdToDecimal(data[0] & 0x7F);

    dt.minutes = bcdToDecimal(data[1] & 0x7F);

    dt.hours = bcdToDecimal(data[2] & 0x3F);

    dt.day = bcdToDecimal(data[3] & 0x07);

    dt.date = bcdToDecimal(data[4] & 0x3F);

    dt.month = bcdToDecimal(data[5] & 0x1F);

    dt.year = 2000 + bcdToDecimal(data[6]);


    return true;
}


// ============================================================
// TEXT HELPERS
// ============================================================

int getTextWidth(
    const char *text,
    uint8_t textSize
)
{
    return strlen(text) * 6 * textSize;
}


int getCenteredX(
    const char *text,
    uint8_t textSize
)
{
    int width = getTextWidth(
        text,
        textSize
    );

    return (SCREEN_WIDTH - width) / 2;
}


// ============================================================
// SPLASH SCREEN
// ============================================================

void showSplashScreen()
{
    mylcd.Fill_Screen(BLACK);

    mylcd.Set_Text_colour(WHITE);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(3);

    const char *message = "Initializing...";

    int x = getCenteredX(
        message,
        3
    );

    mylcd.Print_String(
        message,
        x,
        105
    );
}


// ============================================================
// DRAW SPEED
// ============================================================

void drawSpeed()
{
    mylcd.Fill_Rect(
        0,
        SPEED_REGION_Y,
        SCREEN_WIDTH,
        SPEED_REGION_HEIGHT,
        BLACK
    );


    // --------------------------------------------------------
    // Speed
    // --------------------------------------------------------

    char speedString[8];

    snprintf(
        speedString,
        sizeof(speedString),
        "%03d",
        currentSpeed
    );


    mylcd.Set_Text_colour(SPEED_GREEN);
    mylcd.Set_Text_Back_colour(BLACK);

    // Large speed
    mylcd.Set_Text_Size(11);


    int speedX = getCenteredX(
        speedString,
        12
    );


    mylcd.Print_String(
        speedString,
        speedX,
        20
    );


    // --------------------------------------------------------
    // km/h
    // --------------------------------------------------------

    mylcd.Set_Text_colour(WHITE);
    mylcd.Set_Text_Size(2);

    mylcd.Print_String(
        "km/h",
        260,
        15
    );
}


// ============================================================
// DRAW TRIP MODE
// ============================================================

void drawTripMode()
{
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(2);


    // --------------------------------------------------------
    // "Trip ["
    // --------------------------------------------------------

    mylcd.Set_Text_colour(WHITE);

    mylcd.Print_String(
        "Trip [",
        5,
        133
    );


    // --------------------------------------------------------
    // Selected number
    // --------------------------------------------------------

    mylcd.Set_Text_colour(TRIP_RED);


    if (currentTripMode == TRIP_1)
    {
        mylcd.Print_String(
            "1",
            80,
            133
        );
    }
    else if (currentTripMode == TRIP_2)
    {
        mylcd.Print_String(
            "2",
            80,
            133
        );
    }
    else
    {
        mylcd.Print_String(
            "TTL",
            80,
            133
        );
    }


    // --------------------------------------------------------
    // Closing bracket
    // --------------------------------------------------------

    mylcd.Set_Text_colour(WHITE);


    if (currentTripMode == TOTAL)
    {
        mylcd.Print_String(
            "]",
            118,
            133
        );
    }
    else
    {
        mylcd.Print_String(
            "]",
            // 80,
            93,
            133
        );
    }
}


// ============================================================
// DRAW DATE
// ============================================================

void drawDate(const DateTime &dt)
{
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
        // 115,
        120,
        133
    );
}


// ============================================================
// DRAW TIME
// ============================================================

void drawTime(
    const DateTime &dt,
    bool colonVisible
)
{
    char timeString[6];


    if (colonVisible)
    {
        snprintf(
            timeString,
            sizeof(timeString),
            "%02d:%02d",
            dt.hours,
            dt.minutes
        );
    }
    else
    {
        snprintf(
            timeString,
            sizeof(timeString),
            "%02d %02d",
            dt.hours,
            dt.minutes
        );
    }


    mylcd.Set_Text_colour(TIME_CYAN);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(2);


    mylcd.Print_String(
        timeString,
        258,
        133
    );
}


// ============================================================
// DRAW DISTANCE
// ============================================================

void drawDistance()
{
    float distance;


    if (currentTripMode == TRIP_1)
    {
        distance = trip1Distance;
    }
    else if (currentTripMode == TRIP_2)
    {
        distance = trip2Distance;
    }
    else
    {
        distance = totalDistance;
    }


    // --------------------------------------------------------
    // Clear region
    // --------------------------------------------------------

    mylcd.Fill_Rect(
        0,
        DISTANCE_REGION_Y,
        SCREEN_WIDTH,
        DISTANCE_REGION_HEIGHT,
        BLACK
    );


    // --------------------------------------------------------
    // Convert distance to string
    // --------------------------------------------------------
    //
    // No decimal place.
    //

    char distanceString[20];

    dtostrf(
        distance,
        0,
        0,
        distanceString
    );


    // --------------------------------------------------------
    // Distance
    // --------------------------------------------------------

    mylcd.Set_Text_colour(WHITE);
    mylcd.Set_Text_Back_colour(BLACK);
    mylcd.Set_Text_Size(4);

    mylcd.Print_String(
        distanceString,
        10,
        185
    );


    // --------------------------------------------------------
    // km
    // --------------------------------------------------------

    mylcd.Set_Text_Size(4);

    mylcd.Print_String(
        "km",
        260,
        185
    );
}


// ============================================================
// DRAW INFO BAR
// ============================================================

void drawInfoBar(
    const DateTime &dt,
    bool colonVisible
)
{
    mylcd.Fill_Rect(
        0,
        INFO_BAR_Y,
        SCREEN_WIDTH,
        INFO_BAR_HEIGHT,
        BLACK
    );


    drawTripMode();

    drawDate(dt);

    drawTime(
        dt,
        colonVisible
    );
}


// ============================================================
// DRAW COMPLETE DASHBOARD
// ============================================================

void drawDashboard(
    const DateTime &dt,
    bool colonVisible
)
{
    drawSpeed();

    drawInfoBar(
        dt,
        colonVisible
    );

    drawDistance();
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    // Give USB Serial time to connect
    delay(2000);


    Serial.println();
    Serial.println("==============================");
    Serial.println("MOTORCYCLE DASHBOARD");
    Serial.println("==============================");


    // --------------------------------------------------------
    // Initialize software I2C
    // --------------------------------------------------------

    releaseSDA();
    releaseSCL();

    delay(100);


    // ========================================================
    // TFT INITIALIZATION
    // ========================================================

    Serial.println(
        "[TFT] Initializing..."
    );


    mylcd.Init_LCD();

    mylcd.Set_Rotation(1);

    mylcd.Set_Text_Mode(0);


    Serial.println(
        "[TFT] Initialization complete."
    );


    // ========================================================
    // SPLASH SCREEN
    // ========================================================

    Serial.println(
        "[BOOT] Showing splash screen."
    );


    showSplashScreen();


    // Keep splash screen visible for 2 seconds
    delay(2000);


    // ========================================================
    // RTC TEST
    // ========================================================

    Serial.println(
        "[RTC] Testing DS3231M..."
    );


    DateTime now;


    if (readDateTime(now))
    {
        Serial.println(
            "[RTC] DS3231M communication OK."
        );


        Serial.print(
            "[RTC] Time: "
        );


        if (now.hours < 10)
            Serial.print("0");

        Serial.print(now.hours);

        Serial.print(":");


        if (now.minutes < 10)
            Serial.print("0");

        Serial.print(now.minutes);

        Serial.print(":");


        if (now.seconds < 10)
            Serial.print("0");

        Serial.println(now.seconds);


        Serial.print(
            "[RTC] Date: "
        );


        if (now.month < 10)
            Serial.print("0");

        Serial.print(now.month);

        Serial.print("-");


        if (now.date < 10)
            Serial.print("0");

        Serial.print(now.date);

        Serial.print("-");

        Serial.println(now.year);
    }
    else
    {
        Serial.println(
            "[RTC] ERROR: DS3231M did not respond."
        );
    }


    // ========================================================
    // INITIAL DASHBOARD
    // ========================================================

    if (readDateTime(now))
    {
        drawDashboard(
            now,
            true
        );
    }
    else
    {
        Serial.println(
            "[DISPLAY] RTC unavailable."
        );
    }


    Serial.println(
        "[BOOT] Dashboard ready."
    );
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
    static unsigned long lastRTCRead = 0;

    static unsigned long lastColonToggle = 0;

    static uint8_t lastDisplayedMinute = 255;

    static bool colonVisible = true;


    unsigned long currentMillis = millis();


    // ========================================================
    // RTC UPDATE
    // ========================================================

    if (
        currentMillis - lastRTCRead >= 250
    )
    {
        lastRTCRead = currentMillis;


        DateTime now;


        if (readDateTime(now))
        {
            // ------------------------------------------------
            // Serial debugging
            // ------------------------------------------------

            Serial.print("[RTC] ");

            if (now.hours < 10)
                Serial.print("0");

            Serial.print(now.hours);

            Serial.print(":");

            if (now.minutes < 10)
                Serial.print("0");

            Serial.print(now.minutes);

            Serial.print(":");

            if (now.seconds < 10)
                Serial.print("0");

            Serial.print(now.seconds);

            Serial.print("  ");


            if (now.month < 10)
                Serial.print("0");

            Serial.print(now.month);

            Serial.print("-");


            if (now.date < 10)
                Serial.print("0");

            Serial.print(now.date);

            Serial.print("-");

            Serial.println(now.year);


            // ------------------------------------------------
            // Update display when minute changes
            // ------------------------------------------------

            if (
                now.minutes != lastDisplayedMinute
            )
            {
                lastDisplayedMinute =
                    now.minutes;


                Serial.println(
                    "[DISPLAY] Updating date/time."
                );


                drawInfoBar(
                    now,
                    colonVisible
                );
            }
        }
        else
        {
            Serial.println(
                "[RTC] ERROR: Failed to read DS3231M."
            );
        }
    }


    // ========================================================
    // BLINK CLOCK COLON
    // ========================================================

    if (
        currentMillis - lastColonToggle >= 500
    )
    {
        lastColonToggle = currentMillis;

        colonVisible = !colonVisible;


        DateTime now;


        if (readDateTime(now))
        {
            drawTime(
                now,
                colonVisible
            );
        }
    }


    // ========================================================
    // FUTURE
    // ========================================================
    //
    // Speed sensor:
    //
    // updateSpeedSensor();
    //
    // Buttons:
    //
    // updateButtons();
    //
    // Odometer:
    //
    // updateOdometer();
}