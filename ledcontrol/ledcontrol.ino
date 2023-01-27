/**
 * LED controller to replace a defective standard controller for
 * non-addressable LEDs.
 * A TCS34725 color sensor detects ambient color that is applied to the LEDs
 * after some averaging. Holding the button longer than one second starts
 * sweeping through the brightness.
 * Static color mode is default. Ambient lighting mode is switched to by holding
 * the button for 0.5..1.0 seconds. In this mode a short push (less than 0.5
 * seconds) changes the parameter adjusted by sweep mode. Parameters are HSV
 * color space in the following order: brightness, hue, saturation.
 * The LEDs blink once, twice or thrice to let you know which parameter will be
 * adjusted. An additional switch controls the LED of the TCS34725.
 */

enum Mode
{
    STATIC = 0,
    AMBIENT,
    N_MODES
};

Mode mode = STATIC;  // initialize in static color mode

int hue = 256;         // [0,359], static color mode
int sat = 66;          // [0,100], static color mode
int brightness = 192;  // [0,255], global for both modes

// duration of sweep through the full value range when adjusting color
const double sweepSeconds = 16.;

// pins
const int pinInterrupt = 2;
const int pinSensorLight = 4;
const int pinButton = 8;
const int pinRed = 9;
const int pinGreen = 10;
const int pinBlue = 11;
const int pinSwitchL = 12;
const int pinSwitchR = 13;

// color correct sensor readings from average values sensing white, same value for all three of them disables correction
double avgR = 0.24;
double avgG = 0.37;
double avgB = 0.39;

// values of rgb strip to display white light
double whiteR = 1.;  // 1.00;
double whiteG = 1.;  // 0.64;
double whiteB = 1.;  // 0.87;

// TCS34725 ////////////////////////////////////////////////////////////////////

#include <Adafruit_TCS34725.h>

uint16_t red, green, blue, clear;
double ambientLuminance = brightness / 255.;
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_16X);
volatile boolean colorRead = false;

// don't change any values of globals below ////////////////////////////////////

struct ColorRGB
{
    byte r;
    byte g;
    byte b;
};

void fadeToRGB(ColorRGB c, int steps = 100, int fadeDelay = 10, bool quadratic = false);

// color correction will be calculated from avg color values given above
double corR = 1.;
double corG = 1.;
double corB = 1.;

// inputs //////////////////////////////////////////////////////////////////////

enum InputType
{
    BUTTON = 0,
    SWITCH_LEFT,
    SWITCH_RIGHT,
    N_INPUTS
};

int inputPin[N_INPUTS];
bool reading[N_INPUTS];
bool lastReading[N_INPUTS];
bool inputState[N_INPUTS];
bool lastInputState[N_INPUTS];
unsigned long lastDebounceTime[N_INPUTS];

unsigned long debounceDelay = 50;

unsigned long pushTime = 0;  // button push time

// globals /////////////////////////////////////////////////////////////////////

ColorRGB currentColor = {};   // interpolation color
ColorRGB adjustedColor = {};  // color set by button interaction
ColorRGB ambientColor = {};   // color updated by color sensor

int adjustIndex = 0;    // index for switching adjustment parameter
uint16_t maxClear = 1;  // max clear value to determine brightness range
unsigned long dynTime = 0;

// filter //////////////////////////////////////////////////////////////////////

struct LowPassFilter
{
    double a = 0.4;
    double y0 = 0.;

    double update(double x1)
    {
        y0 = a * x1 + (1.0 - a) * y0;
        return y0;
    }
};

// filtered rgbc/luminance values
LowPassFilter ambientRed;
LowPassFilter ambientGreen;
LowPassFilter ambientBlue;
LowPassFilter ambientClear;
LowPassFilter luminance;

// auxiliary ///////////////////////////////////////////////////////////////////

// template definition required by vscode/platform.io, but results in
// compilation error with Arduino IDE
template <typename T>
const T clamp(const T &x, const T &a, const T &b);

template <typename T>
const T clamp(const T &x, const T &a, const T &b)
{
    return x < a ? a : (x > b ? b : x);
}

// template definition required by vscode/platform.io, but results in
// compilation error with Arduino IDE
template <class T, class U>
const T interp(const T &a, const T &b, const U &t);

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

// main ////////////////////////////////////////////////////////////////////////

void setup()
{
    for (int i = 0; i < N_INPUTS; i++)
    {
        lastReading[i] = HIGH;
        inputState[i] = HIGH;
        lastInputState[i] = HIGH;
        lastDebounceTime[i] = 0;
    }

    inputPin[BUTTON] = pinButton;
    inputPin[SWITCH_LEFT] = pinSwitchL;
    inputPin[SWITCH_RIGHT] = pinSwitchR;

    pinMode(pinSensorLight, OUTPUT);
    digitalWrite(pinSensorLight, LOW);  // turn off sensor LED

    pinMode(pinRed, OUTPUT);
    pinMode(pinGreen, OUTPUT);
    pinMode(pinBlue, OUTPUT);

    pinMode(pinButton, INPUT_PULLUP);  // active low
    pinMode(pinSwitchL, INPUT_PULLUP);
    pinMode(pinSwitchR, INPUT_PULLUP);

    pinMode(pinInterrupt, INPUT_PULLUP);  // active-low/open-drain
    attachInterrupt(digitalPinToInterrupt(pinInterrupt), isr, FALLING);

    applyColorRGB({0, 0, 0});

    if (!tcs.begin())
    {
        for (;;)
        {
        }
    }

    // switch color mode if button is held during startup
    if (digitalRead(pinButton) == LOW)
    {
        mode = (Mode)((mode + 1) % N_MODES);
    }

    if (mode == AMBIENT)
    {
        tcsEnable();
    }
    else
    {
        tcsDisable();
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
    // sweepToHSV(1. + h, s, v);
}

void loop()
{
    switchLoop();
    buttonLoop();
    ambientLoop();
}

bool debounceInput(InputType input)
{
    bool changed = false;

    reading[input] = digitalRead(inputPin[input]);

    if (reading[input] != lastReading[input])
    {
        lastDebounceTime[input] = millis();
    }

    if ((millis() - lastDebounceTime[input]) > debounceDelay)
    {
        if (reading[input] != inputState[input])
        {
            inputState[input] = reading[input];
            changed = true;
        }
    }

    lastReading[input] = reading[input];

    return changed;
}

void switchLoop()
{
    if (debounceInput(SWITCH_LEFT))
    {
        digitalWrite(pinSensorLight, !inputState[SWITCH_LEFT]);
    }

    if (debounceInput(SWITCH_RIGHT))
    {
        digitalWrite(pinSensorLight, !inputState[SWITCH_RIGHT]);
    }
}

void buttonLoop()
{
    bool changed = debounceInput(BUTTON);

    // button is pushed down, thus no change event
    if (inputState[BUTTON] == LOW && millis() - pushTime > 1000)  // press and hold
    {
        int range = 100;

        // adjust values by sweeping through their range
        if (mode == AMBIENT || adjustIndex == 0)  // change brightness
        {
            range = 255;
            brightness -= 1;
            if (brightness < 0)
            {
                brightness = 255;
            }
        }
        else if (adjustIndex == 1)  // change hue
        {
            range = 359;
            hue += 1;
            if (hue > 359)
            {
                hue = 0;
            }
        }
        else if (adjustIndex == 2)  // change saturation
        {
            range = 100;
            sat -= 1;
            if (sat < 0)
            {
                sat = 100;
            }
        }

        adjustedColor = hsv1_to_rgb255(hue / 359., sat / 100., brightness / 255.);

        if (mode == STATIC)  // apply only in static color mode; ambient mode is updated in ambient loop
        {
            applyColorRGB(adjustedColor);
        }

        delay(1000. / (range / sweepSeconds));
    }

    if (!changed)
    {
        return;  // nothing changed (literally)
    }

    if (inputState[BUTTON] == LOW)  // got pushed
    {
        pushTime = millis();
    }
    else  // got released
    {
        if (millis() - pushTime < 300)  // released after short push
        {
            // increment index to change next hsv value
            if (mode == STATIC)
            {
                adjustIndex = (adjustIndex + 1) % 3;

                // provide user feedback
                ColorRGB inv = {255 - adjustedColor.r, 255 - adjustedColor.g, 255 - adjustedColor.b};
                for (int i = 0; i < adjustIndex + 1; i++)
                {
                    fadeToRGB(inv, 10, 8);
                    fadeToRGB(adjustedColor, 10, 5);
                }
            }
        }
        else if (millis() - pushTime < 1000)  // released after long push
        {
            // toggle ambient/fixed color modes
            mode = (Mode)((mode + 1) % N_MODES);

            if (mode == AMBIENT)
            {
                tcsEnable();
                ambientColor = adjustedColor;  // force fade
            }
            else
            {
                tcsDisable();
                fadeToRGB(adjustedColor);  // fade from ambient to static color
                adjustIndex = 0;           // reset adjust index
            }
        }
    }
}

void ambientLoop()
{
    if (mode != AMBIENT)
    {
        return;
    }

    bool newReading = false;

    // handle interrupt
    if (colorRead)  // set true by interrupt
    {
        // get colors
        tcsGetRawDataNoDelay(&red, &green, &blue, &clear);
        newReading = true;

        // clear and reset
        tcs.clearInterrupt();
        colorRead = false;
    }

    if (newReading)
    {
        // prevent displaying a single primary color in low light settings
        if (clear > 0 && red > 1 && green > 1 && blue > 1)
        {
            if (clear > maxClear)  // keep track of max value for normalization
            {
                maxClear = clear;
            }

            double r = (double)red / clear;
            double g = (double)green / clear;
            double b = (double)blue / clear;
            double c = (double)clear / maxClear;

            r = clamp(r * corR, 0., 1.);
            g = clamp(g * corG, 0., 1.);
            b = clamp(b * corB, 0., 1.);

            luminance.update(0.2126 * r + 0.7152 * g + 0.0722 * b);
            ambientLuminance = luminance.y0;

            ambientRed.update(r);
            ambientGreen.update(g);
            ambientBlue.update(b);
            ambientClear.update(c);
        }
        else
        {
            // slowly interpolate from ambient luminance (set with last sensor detection above) to user-set brightness
            ambientLuminance = interp(ambientLuminance, brightness / 255., 0.001);
            ambientRed.update(ambientLuminance);
            ambientGreen.update(ambientLuminance);
            ambientBlue.update(ambientLuminance);
            // ambientClear.update(ambientLuminance);  // not sure if necessary
        }
    }

    // decrement max clear in order to keep detected brightness dynamic
    if (clear < maxClear && (millis() - dynTime) > 2000)
    {
        maxClear = max(maxClear - 1, 1);  // prevent division by zero
        dynTime = millis();
    }

    // calculate dynamic brightness factor
    double dyn = interp(0.13, brightness / 255., ambientClear.y0);
    ColorRGB target = {ambientRed.y0 * dyn * 255, ambientGreen.y0 * dyn * 255, ambientBlue.y0 * dyn * 255};
    ambientColor = interpColorRGB(ambientColor, target, 0.88);
    applyColorRGB(ambientColor);
}

// tcs ////////////////////////////////////////////////////////////////////////

void tcsEnable()
{
    colorRead = false;  // reset
    tcs.enable();       // enable sensor

    // set persistence filter to generate an interrupt for every rgb cycle, regardless of the integration limits
    tcs.write8(TCS34725_PERS, TCS34725_PERS_NONE);
    tcs.setInterrupt(true);
}

void tcsDisable()
{
    tcs.setInterrupt(false);
    tcs.clearInterrupt();
    tcs.disable();  // go to sleep
    colorRead = false;
}

// Interrupt Service Routine
void isr()
{
    colorRead = true;
}

void tcsGetRawDataNoDelay(uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c)
{
    *r = tcs.read16(TCS34725_RDATAL);
    *g = tcs.read16(TCS34725_GDATAL);
    *b = tcs.read16(TCS34725_BDATAL);
    *c = tcs.read16(TCS34725_CDATAL);
}

// init ////////////////////////////////////////////////////////////////////////

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
    ambientRed.y0 = adjustedColor.r / 255.;
    ambientGreen.y0 = adjustedColor.g / 255.;
    ambientBlue.y0 = adjustedColor.b / 255.;
    luminance.y0 = brightness / 255.;
    ambientColor = adjustedColor;
}

// animations //////////////////////////////////////////////////////////////////

void fluorescentFlicker()
{
    fadeToRGB(adjustedColor, 20, 2);
    fadeToRGB({0, 0, 0}, 10, 2, true);
    delay(80);
    fadeToRGB(adjustedColor, 20, 2);
    fadeToRGB({0, 0, 0}, 10, 2, true);
    delay(300);
    fadeToRGB(adjustedColor, 100, 3);
    delay(1000);
}

void sweepToHSV(double h1, double s1, double v1)
{
    double h, s, v, t;
    for (int i = 0; i <= h1 * 360; i++)
    {
        t = i / (h1 * 360.);
        h = (i % 360) / 360.;  // hue in range [0,1)
        s = interp(1., s1, t);
        v = interp(1., v1, t);
        applyColorRGB(hsv1_to_rgb255(h, s, v));
        delay(interp(4, 24, t * t));
    }
}

// light strip /////////////////////////////////////////////////////////////////

void setColorRGB(ColorRGB c)
{
    analogWrite(pinRed, c.r * whiteR);
    analogWrite(pinGreen, c.g * whiteG);
    analogWrite(pinBlue, c.b * whiteB);
}

void applyColorRGB(ColorRGB c)
{
    currentColor = c;
    setColorRGB(c);
}

void fadeToRGB(ColorRGB c, int steps, int fadeDelay, bool quadratic)
{
    double t;
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

// HSV color space functions ///////////////////////////////////////////////////

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

    r = clamp(r, 0., 1.);
    g = clamp(g, 0., 1.);
    b = clamp(b, 0., 1.);
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

    if (maxVal < epsilon)  // = 0
    {
        s = 0.;
    }
    else
    {
        s = (maxVal - minVal) / maxVal;
    }

    v = maxVal;

    h /= 360.;

    h = clamp(h, 0., 1.);
    s = clamp(s, 0., 1.);
    v = clamp(v, 0., 1.);
}

// color space helper //////////////////////////////////////////////////////////

static ColorRGB hsv1_to_rgb255(double h, double s, double v)
{
    double r, g, b;
    hsv2rgb(h, s, v, r, g, b);
    return {r * 255, g * 255, b * 255};
}
