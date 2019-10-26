const int pinGreen = 9;
const int pinRed = 10;
const int pinBlue = 11;
const int pinButton = 8;

struct ColorRGB
{
    byte r;
    byte g;
    byte b;
};

void fadeToRGB(ColorRGB c, int steps = 100, int fadeDelay = 10);

// globals ////////////////////////////////////////////////////////////////////

ColorRGB currentColor = {}; // interpolation color
ColorRGB chosenColor = {};  // color set by button interaction
ColorRGB ambientColor = {}; // color updated by color sensor

int hsvIndex = 0;
const int hsvRange[3] = {128, 64, 64};
int hsvSpace[3] = {0, 0, 0};

bool lastButtonState = LOW;
long int pushTime = 0;

bool ambientColorEnabled = false;

// TCS34725 ///////////////////////////////////////////////////////////////////

#include <Wire.h>
#include "Adafruit_TCS34725.h"

Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
uint16_t red, green, blue, clear;
int maxAmbientBrightness = 127;

// moving mean /////////////////////////////////////////////////////////////////

const int windowSize = 16;

struct MovingMean
{
    float mmValues[windowSize];
    float mmSum = 0.;
    int mmIndex = 0;
    int mmSize = 0;

    float addValue(float val)
    {
        mmSum = mmSum - mmValues[mmIndex] + val;
        mmValues[mmIndex] = val;
        mmIndex = (mmIndex + 1) % windowSize;
        mmSize = min(mmSize + 1, windowSize);
        return mmSum / (float)mmSize;
    }
};

MovingMean ambientRed;
MovingMean ambientGreen;
MovingMean ambientBlue;
MovingMean ambientClear;

// auxiliary //////////////////////////////////////////////////////////////////

template <class T, class U>
const T interp(const T &a, const T &b, const U &t)
{
    if (t < (U)1e-3)
    {
        return a;
    }
    if (t > (U)(1. - 1e-3))
    {
        return b;
    }
    return (T)(a + t * (b - a));
}

// main ///////////////////////////////////////////////////////////////////////

void setup()
{
    pinMode(pinRed, OUTPUT);
    pinMode(pinGreen, OUTPUT);
    pinMode(pinBlue, OUTPUT);
    pinMode(pinButton, INPUT_PULLUP);

    applyColorRGB({0, 0, 0});

    if (!tcs.begin())
    {
        for (;;)
        {
        }
    }

    // sweep to default color
    sweepToHSV(360 + 256, 0.0, 0.1);
}

void loop()
{
    buttonLoop();
    ambientLoop();
}

void buttonLoop()
{
    bool buttonState = digitalRead(pinButton);

    if (lastButtonState == HIGH && buttonState == LOW) // got pushed
    {
        pushTime = millis();
    }
    else if (lastButtonState == LOW && buttonState == HIGH) // got released
    {
        if (millis() - pushTime < 250) // short push
        {
            // increment index to change next hsv value
            if (!ambientColorEnabled)
            {
                hsvIndex = (hsvIndex + 1) % 3;

                // provide user feedback
                for (int i = 0; i < hsvIndex + 1; i++)
                {
                    fadeToRGB({0, 0, 0}, 10, 5);
                    fadeToRGB({63, 63, 63}, 10, 5);
                }
                fadeToRGB(chosenColor, 10, 5);
            }
        }
        else if (millis() - pushTime < 1000) // long push
        {
            // toggle ambient mode / color selection mode
            ambientColorEnabled = !ambientColorEnabled;

            if (ambientColorEnabled)
            {
                tcs.enable(); // enable sensor
            }
            else
            {
                tcs.disable(); // go to sleep
                fadeToRGB(chosenColor);
            }
        }
    }
    else if (buttonState == LOW && millis() - pushTime > 1000) // press and hold
    {
        // adjust values by sweeping through them
        if (ambientColorEnabled) // brightness of ambient color
        {
            maxAmbientBrightness -= 1;
            if (maxAmbientBrightness < 0)
            {
                maxAmbientBrightness = 255;
            }
        }
        else // current selected hsv parameter
        {
            hsvSpace[hsvIndex] -= 1;
            if (hsvSpace[hsvIndex] < 0)
            {
                hsvSpace[hsvIndex] = hsvRange[hsvIndex] - 1;
            }
            chosenColor = hsv1_to_rgb255(hsvSpace[0] / (float)(hsvRange[0] - 1),
                                         hsvSpace[1] / (float)(hsvRange[1] - 1),
                                         hsvSpace[2] / (float)(hsvRange[2] - 1));
            applyColorRGB(chosenColor);
            delay(100);
        }
    }

    lastButtonState = buttonState;
}

void ambientLoop()
{
    if (ambientColorEnabled)
    {
        tcs.getRawData(&red, &green, &blue, &clear);

        float avgRed = ambientRed.addValue(red);
        float avgGreen = ambientGreen.addValue(green);
        float avgBlue = ambientBlue.addValue(blue);
        float avgClear = ambientClear.addValue(clear);

        if (avgClear > 0)
        {
            float r = avgRed / avgClear * maxAmbientBrightness;
            float g = avgGreen / avgClear * maxAmbientBrightness;
            float b = avgBlue / avgClear * maxAmbientBrightness;
            ambientColor = {r, g, b};
            applyColorRGB(ambientColor);
        }
    }
}

void sweepToHSV(int hue, double sat, double val)
{
    double h, s, v;
    for (int i = 0; i <= hue; i++)
    {
        h = (i % 360) / 360.;
        double t = i / (double)hue;
        s = interp(1., sat, t);
        v = interp(1., val, t);
        chosenColor = hsv1_to_rgb255(h, s, v);
        applyColorRGB(chosenColor);
        delay(interp(12, 4, t * t));
    }

    hsvSpace[0] = h * (hsvRange[0] - 1);
    hsvSpace[1] = s * (hsvRange[1] - 1);
    hsvSpace[2] = v * (hsvRange[2] - 1);
}

// RGB color space functions //////////////////////////////////////////////////

void setColorRGB(ColorRGB c)
{
    analogWrite(pinRed, c.r);
    analogWrite(pinGreen, c.g);
    analogWrite(pinBlue, c.b);
}

void applyColorRGB(ColorRGB c)
{
    currentColor = c;
    setColorRGB(c);
}

void fadeToRGB(ColorRGB c, int steps = 100, int fadeDelay = 10)
{
    float t = 0.0f;
    for (int i = 0; i < steps; i++)
    {
        t = (i + 1) / (float)steps;
        applyColorRGB(interpColorRGB(currentColor, c, t));
        delay(fadeDelay);
    }
}

ColorRGB interpColorRGB(const ColorRGB &c1, const ColorRGB &c2, float t)
{
    byte r = interp(c1.r, c2.r, t);
    byte g = interp(c1.g, c2.g, t);
    byte b = interp(c1.b, c2.b, t);
    return {r, g, b};
}

// HSV color space functions //////////////////////////////////////////////////

static void hsv2rgb(double h, double s, double v, double &r, double &g, double &b)
{
    int i = (int)(h * 6);
    double f = h * 6 - i;
    double p = v * (1 - s);
    double q = v * (1 - s * f);
    double t = v * (1 - s * (1 - f));

    switch (i % 6)
    {
    case 0:
        r = v, g = t, b = p;
        break;
    case 1:
        r = q, g = v, b = p;
        break;
    case 2:
        r = p, g = v, b = t;
        break;
    case 3:
        r = p, g = q, b = v;
        break;
    case 4:
        r = t, g = p, b = v;
        break;
    case 5:
        r = v, g = p, b = q;
        break;
    }

    r = r < 0. ? 0. : (r > 1. ? 1. : r);
    g = g < 0. ? 0. : (g > 1. ? 1. : g);
    b = b < 0. ? 0. : (b > 1. ? 1. : b);
}

// mixed color space functions ////////////////////////////////////////////////

static ColorRGB hsv1_to_rgb255(double h, double s, double v)
{
    double r, g, b;
    hsv2rgb(h, s, v, r, g, b);
    return {r * 255, g * 255, b * 255};
}