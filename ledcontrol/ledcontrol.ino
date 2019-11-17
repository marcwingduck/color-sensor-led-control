/**
 * Single button LED controller to replace a defective standard controller for
 * non-addressable LEDs. Regular mode allows to adjust HSV color space
 * parameters. A TCS34725 color sensor detects ambient color that is applied
 * to the LEDs. In ambient mode the button controls only brightness.
 */

bool ambientColorEnabled = true; // start in ambient color mode

int hue = 256;        // [0,359], adjusted color mode
int sat = 66;         // [0,100], adjusted color mode
int brightness = 192; // [0,255], globally for both modes

// duration of sweep through the full value range when adjusting color
double sweepSeconds = 9.;

// pins
const int pinButton = 8;
const int pinRed = 9;
const int pinGreen = 10;
const int pinBlue = 11;

// color correct sensor readings from average values sensing white
double avgR = 0.24; // same value for all three of them disables correction
double avgG = 0.37;
double avgB = 0.39;

// values of rgb strip to display white light
double whiteR = 1; //1.00;
double whiteG = 1; //0.64;
double whiteB = 1; //0.87;

// TCS34725 ///////////////////////////////////////////////////////////////////

#include <Adafruit_TCS34725.h>

uint16_t red, green, blue, clear;
double ambientLuminance = brightness / 255.;
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_16X);
const int pinInterrupt = 2;
volatile boolean colorRead = false;

// don't change any values of globals below ///////////////////////////////////

struct ColorRGB
{
    byte r;
    byte g;
    byte b;
};

void fadeToRGB(ColorRGB c, int steps = 100, int fadeDelay = 10, bool quadratic = false);

double corR = 1.; // color correction will be calculated from avg color values given above
double corG = 1.;
double corB = 1.;

// globals ////////////////////////////////////////////////////////////////////

int adj = 0; // index for switching adjustment paramter
bool lastButtonState = HIGH;
unsigned long pushTime = 0;

ColorRGB currentColor = {};  // interpolation color
ColorRGB adjustedColor = {}; // color set by button interaction
ColorRGB ambientColor = {};  // color updated by color sensor

uint16_t maxClear = 1; // max clear value to determine brightness range
unsigned long dynTime = 0;

// moving mean /////////////////////////////////////////////////////////////////

const int windowSize = 4;

struct MovingMean
{
    double mmValues[windowSize] = {};
    double mmSum = 0.;
    int mmIndex = 0;
    int mmSize = 0;

    double mean = 0.;

    double addValue(double val)
    {
        mmSum = mmSum - mmValues[mmIndex] + val;
        mmValues[mmIndex] = val;
        mmIndex = (mmIndex + 1) % windowSize;
        mmSize = min(mmSize + 1, windowSize);
        mean = mmSum / mmSize;
        return mean;
    }
};

MovingMean ambientRed; // filter rgbc values
MovingMean ambientGreen;
MovingMean ambientBlue;
MovingMean ambientClear;
MovingMean luminance; // mean luminance

// auxiliary //////////////////////////////////////////////////////////////////

template <typename T>
const T clamp(T value, T a, T b)
{
    return value < a ? a : (value > b ? b : value);
}

template <class T, class U>
const T interp(const T &a, const T &b, const U &t)
{
    if (t < (U)1e-4)
    {
        return a;
    }
    if (t > (U)(1. - 1e-4))
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
    pinMode(pinInterrupt, INPUT_PULLUP); // active-low/open-drain
    attachInterrupt(digitalPinToInterrupt(pinInterrupt), isr, FALLING);

    applyColorRGB({0, 0, 0});

    if (!tcs.begin())
    {
        for (;;)
        {
        }
    }

    // adjusted color mode if button is held on startup
    if (digitalRead(pinButton) == LOW)
    {
        ambientColorEnabled = false;
        tcsDisable();
    }
    else
    {
        tcsEnable();
    }

    // calculate color correction
    correctColor(avgR, avgG, avgB);

    // initialize colors
    double h = hue / 360.;
    double s = sat / 100.;
    double v = brightness / 255.;
    initColors(h, s, v);

    // start animation
    fluorescentFlicker();
    //sweepToHSV(1. + h, s, v);
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
        if (millis() - pushTime < 250) // released after short push
        {
            // increment index to change next hsv value
            if (!ambientColorEnabled)
            {
                adj = (adj + 1) % 3;

                // provide user feedback
                ColorRGB inv = {255 - adjustedColor.r, 255 - adjustedColor.g, 255 - adjustedColor.b};
                for (int i = 0; i < adj + 1; i++)
                {
                    fadeToRGB(inv, 10, 8);
                    fadeToRGB(adjustedColor, 10, 5);
                }
            }
        }
        else if (millis() - pushTime < 1000) // released after long push
        {
            // toggle ambient/fixed color modes
            ambientColorEnabled = !ambientColorEnabled;

            if (ambientColorEnabled)
            {
                tcsEnable();
                ambientColor = adjustedColor; // force fade
            }
            else
            {
                tcsDisable();
                fadeToRGB(adjustedColor);
                adj = 0;
            }
        }
    }
    else if (buttonState == LOW && millis() - pushTime > 1000) // press and hold
    {
        int range = 100;

        // adjust values by sweeping through their range
        if (ambientColorEnabled || adj == 0) // change brightness
        {
            range = 255;
            brightness -= 1;
            if (brightness < 0)
            {
                brightness = 255;
            }
        }
        else if (adj == 1) // change hue
        {
            range = 359;
            hue += 1;
            if (hue > 359)
            {
                hue = 0;
            }
        }
        else if (adj == 2) // change saturation
        {
            range = 100;
            sat -= 1;
            if (sat < 0)
            {
                sat = 100;
            }
        }

        if (!ambientColorEnabled) // apply only in color mode; ambient mode is updated in loop
        {
            adjustedColor = hsv1_to_rgb255(hue / 359., sat / 100., brightness / 255.);
            applyColorRGB(adjustedColor);
        }

        delay(1000. / (range / sweepSeconds));
    }

    lastButtonState = buttonState;
}

void ambientLoop()
{
    bool newReading = false;

    if (colorRead)
    {
        tcsGetRawDataNoDelay(&red, &green, &blue, &clear);
        newReading = true;
        tcs.clearInterrupt();
        colorRead = false;
    }

    if (!ambientColorEnabled)
    {
        return;
    }

    // prevent single color channel display in low light
    if (newReading)
    {
        if (clear > 0 && red > 1 && green > 1 && blue > 1)
        {
            if (clear > maxClear) // keep track of max value for normalization
            {
                maxClear = clear;
            }

            double r = (double)red / clear;
            double g = (double)green / clear;
            double b = (double)blue / clear;
            double c = (double)clear / maxClear;

            r *= corR;
            g *= corG;
            b *= corB;

            r = clamp(r, 0., 1.);
            g = clamp(g, 0., 1.);
            b = clamp(b, 0., 1.);

            luminance.addValue(0.2126 * r + 0.7152 * g + 0.0722 * b);
            ambientLuminance = luminance.mean;

            ambientRed.addValue(r);
            ambientGreen.addValue(g);
            ambientBlue.addValue(b);
            ambientClear.addValue(c);
        }
        else
        {
            // slowly interpolate from ambient luminance (set with last sensor detection above) to user-set brightness
            ambientLuminance = interp(ambientLuminance, brightness / 255., 0.001);
            ambientRed.addValue(ambientLuminance);
            ambientGreen.addValue(ambientLuminance);
            ambientBlue.addValue(ambientLuminance);
        }
    }

    // decrement max clear in order to keep detected brightness dynamic
    if (clear < maxClear && (millis() - dynTime) > 2000)
    {
        maxClear = max(maxClear - 1, 1);
        dynTime = millis();
    }

    // calculate dynamic brightness factor
    double dyn = interp(0.13, brightness / 255., ambientClear.mean);
    ColorRGB target = {ambientRed.mean * dyn * 255, ambientGreen.mean * dyn * 255, ambientBlue.mean * dyn * 255};
    ambientColor = interpColorRGB(ambientColor, target, 0.88);
    applyColorRGB(ambientColor);
}

// tcs ////////////////////////////////////////////////////////////////////////

void tcsEnable()
{
    colorRead = false;
    tcs.enable(); // enable sensor

    // set persistence filter to generate an interrupt for every rgb cycle,
    // regardless of the integration limits
    tcs.write8(TCS34725_PERS, TCS34725_PERS_NONE);
    tcs.setInterrupt(true);
}

void tcsDisable()
{
    tcs.setInterrupt(false);
    tcs.clearInterrupt();
    colorRead = false;
    tcs.disable(); // go to sleep
}

void isr()
{
    colorRead = true;
}

void tcsGetRawDataNoDelay(uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c)
{
    *c = tcs.read16(TCS34725_CDATAL);
    *r = tcs.read16(TCS34725_RDATAL);
    *g = tcs.read16(TCS34725_GDATAL);
    *b = tcs.read16(TCS34725_BDATAL);
}

// init ///////////////////////////////////////////////////////////////////////

void correctColor(double r, double g, double b)
{
    double sum = (r + g + b);
    corR = sum / (3. * r);
    corG = sum / (3. * g);
    corB = sum / (3. * b);
}

void initColors(double h, double s, double v)
{
    adjustedColor = hsv1_to_rgb255(h, s, v);
    for (int i = 0; i < windowSize; i++)
    {
        ambientRed.addValue(adjustedColor.r / 255.);
        ambientGreen.addValue(adjustedColor.g / 255.);
        ambientBlue.addValue(adjustedColor.b / 255.);
        luminance.addValue(brightness / 255.);
    }
    ambientColor = adjustedColor;
}

// animations /////////////////////////////////////////////////////////////////

void fluorescentFlicker()
{
    fadeToRGB(adjustedColor, 20, 2);
    fadeToRGB({0, 0, 0}, 10, 2, true);
    delay(80);
    fadeToRGB(adjustedColor, 20, 2);
    fadeToRGB({0, 0, 0}, 10, 2, true);
    delay(300);
    fadeToRGB(adjustedColor, 100, 3);
    delay(3000);
}

void sweepToHSV(double h1, double s1, double v1)
{
    double h, s, v, t;
    for (int i = 0; i <= h1 * 360; i++)
    {
        t = i / (h1 * 360.);
        h = (i % 360) / 360.; // hue in range [0,1)
        s = interp(1., s1, t);
        v = interp(1., v1, t);
        applyColorRGB(hsv1_to_rgb255(h, s, v));
        delay(interp(4, 24, t * t));
    }
}

// light strip ////////////////////////////////////////////////////////////////

void setColorRGB(ColorRGB c)
{
    byte r = c.r;
    byte g = c.g;
    byte b = c.b;

    r *= whiteR;
    g *= whiteG;
    b *= whiteB;

    analogWrite(pinRed, r);
    analogWrite(pinGreen, g);
    analogWrite(pinBlue, b);
}

void applyColorRGB(ColorRGB c)
{
    currentColor = c;
    setColorRGB(c);
}

void fadeToRGB(ColorRGB c, int steps = 100, int fadeDelay = 10, bool quadratic = false)
{
    double t = 0.0f;
    for (int i = 0; i < steps; i++)
    {
        t = (i + 1) / (double)steps;
        if (quadratic)
        {
            t = t * t;
        }
        applyColorRGB(interpColorRGB(currentColor, c, t));
        delay(fadeDelay);
    }
}

ColorRGB interpColorRGB(const ColorRGB &c1, const ColorRGB &c2, double t)
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

static void rgb2hsv(double r, double g, double b, double &h, double &s, double &v)
{
    h = 0.;
    s = 0.;
    v = 0.;

    double epsilon = 0.001;

    double maxVal = max(max(r, g), b);
    double minVal = min(min(r, g), b);

    double delta = maxVal - minVal;

    if (abs(delta) < epsilon)
    {
        h = 0.;
    }
    else if (abs(r - maxVal) < epsilon)
    {
        h = 60. * (0. + (g - b) / delta);
    }
    else if (abs(g - maxVal) < epsilon)
    {
        h = 60. * (2. + (b - r) / delta);
    }
    else if (abs(b - maxVal) < epsilon)
    {
        h = 60. * (4. + (r - g) / delta);
    }

    if (h < 0.)
    {
        h += 360.;
    }

    if (maxVal < epsilon) // = 0
    {
        s = 0.;
    }
    else
    {
        s = (maxVal - minVal) / maxVal;
    }

    v = maxVal;

    h /= 360.;

    h = h < 0. ? 0. : (h > 1. ? 1. : h);
    s = s < 0. ? 0. : (s > 1. ? 1. : s);
    v = v < 0. ? 0. : (v > 1. ? 1. : v);
}

// color space helper /////////////////////////////////////////////////////////

static ColorRGB hsv1_to_rgb255(double h, double s, double v)
{
    double r, g, b;
    hsv2rgb(h, s, v, r, g, b);
    return {r * 255, g * 255, b * 255};
}
