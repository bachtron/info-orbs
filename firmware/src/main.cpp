#include "Button.h"
#include "GlobalTime.h"
#include "ScreenManager.h"
#include "Utils.h"
#include "WidgetSet.h"
#include "clockwidget/ClockWidget.h"
#include "config_helper.h"
#include "icons.h"
#include "wifiwidget/WifiWidget.h"
#include <Arduino.h>
#include <WebServer.h>
#include <ESPmDNS.h>

TFT_eSPI tft = TFT_eSPI();

#ifdef WIDGET_CYCLE_DELAY
unsigned long m_widgetCycleDelay = WIDGET_CYCLE_DELAY * 1000; // Automatically cycle widgets every X seconds, set to 0 to disable
#else
unsigned long m_widgetCycleDelay = 0;
#endif
unsigned long m_widgetCycleDelayPrev = 0;

Button buttonLeft(BUTTON_LEFT);
Button buttonOK(BUTTON_OK);
Button buttonRight(BUTTON_RIGHT);

GlobalTime *globalTime; // Initialize the global time

String connectingString{""};

WifiWidget *wifiWidget{nullptr};

int connectionTimer{0};
const int connectionTimeout{10000};
bool isConnected{true};

ScreenManager *sm;
WidgetSet *widgetSet;
WebServer server(80);

// Web server handler functions
void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
    html += ".container{background:white;padding:20px;border-radius:10px;max-width:400px;margin:auto;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
    html += "h1{color:#333;text-align:center;}";
    html += ".control{margin:20px 0;padding:15px;background:#f9f9f9;border-radius:5px;}";
    html += "label{display:block;margin-bottom:10px;font-weight:bold;color:#555;}";
    html += "input[type='range']{width:100%;height:30px;}";
    html += "button{width:100%;padding:12px;margin:5px 0;border:none;border-radius:5px;font-size:16px;cursor:pointer;transition:0.3s;}";
    html += ".btn-primary{background:#4CAF50;color:white;}.btn-primary:hover{background:#45a049;}";
    html += ".btn-danger{background:#f44336;color:white;}.btn-danger:hover{background:#da190b;}";
    html += ".value{display:inline-block;min-width:50px;text-align:center;font-weight:bold;color:#333;}";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>Info Orbs Control</h1>";
    html += "<div class='control'>";
    html += "<label>Brightness: <span class='value' id='brightValue'>" + String(sm->getBrightness()) + "</span></label>";
    html += "<input type='range' id='brightness' min='10' max='255' value='" + String(sm->getBrightness()) + "' oninput='updateBright(this.value)'>";
    html += "<button class='btn-primary' onclick='setBright()'>Set Brightness</button>";
    html += "</div>";
    html += "<div class='control'>";
    html += "<button class='btn-danger' onclick='if(confirm(\"Reset device?\"))resetDevice()'>Reset Device</button>";
    html += "</div>";
    html += "<div style='text-align:center;margin-top:20px;color:#888;font-size:14px;'>Current: " + String(sm->getBrightness()) + "</div>";
    html += "</div>";
    html += "<script>";
    html += "function updateBright(val){document.getElementById('brightValue').innerText=val;}";
    html += "function setBright(){var b=document.getElementById('brightness').value;";
    html += "fetch('/brightness?value='+b).then(r=>r.text()).then(d=>{alert(d);location.reload();}).catch(e=>alert('Error'));}";
    html += "function resetDevice(){fetch('/reset').then(()=>{alert('Device resetting...');}).catch(e=>alert('Error'));}";
    html += "</script></body></html>";
    server.send(200, "text/html", html);
}

void handleBrightness() {
    if (server.hasArg("value")) {
        int brightness = server.arg("value").toInt();
        if (brightness >= 10 && brightness <= 255) {
            sm->setBrightness(brightness);
            sm->clearAllScreens();
            widgetSet->drawCurrent(true);
            Serial.printf("Brightness set to: %d\n", brightness);
            server.send(200, "text/plain", "Brightness set to " + String(brightness));
        } else {
            server.send(400, "text/plain", "Invalid brightness value (10-255)");
        }
    } else {
        server.send(400, "text/plain", "Missing value parameter");
    }
}

void handleReset() {
    server.send(200, "text/plain", "Resetting device...");
    delay(500);
    ESP.restart();
}

void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/brightness", handleBrightness);
    server.on("/reset", handleReset);
    server.begin();
    Serial.println("Web server started on port 80");
    
    // Start mDNS
    if (MDNS.begin("info-orbs")) {
        Serial.println("mDNS started: http://info-orbs.local");
        MDNS.addService("http", "tcp", 80);
    }
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    if (y >= tft.height())
        return 0;
    // Dim bitmap
    for (int i = 0; i < w * h; i++) {
        bitmap[i] = Utils::rgb565dim(bitmap[i], sm->getBrightness(), true);
    }
    tft.pushImage(x, y, w, h, bitmap);
    return 1;
}

/**
 * The ISR handlers must be static
 */
void isrButtonChangeLeft() { buttonLeft.isrButtonChange(); }
void isrButtonChangeMiddle() { buttonOK.isrButtonChange(); }
void isrButtonChangeRight() { buttonRight.isrButtonChange(); }

void setupButtons() {
    buttonLeft.begin();
    buttonOK.begin();
    buttonRight.begin();

    attachInterrupt(digitalPinToInterrupt(BUTTON_LEFT), isrButtonChangeLeft, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_OK), isrButtonChangeMiddle, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT), isrButtonChangeRight, CHANGE);
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("Starting up...");

    TJpgDec.setSwapBytes(true); // JPEG rendering setup
    TJpgDec.setCallback(tft_output);
    setupButtons();

    sm = new ScreenManager(tft);
    sm->fillAllScreens(TFT_BLACK);
    sm->setFontColor(TFT_WHITE);

    sm->selectScreen(0);
    sm->drawCentreString("Welcome", ScreenCenterX, ScreenCenterY, 29);

    sm->selectScreen(1);
    sm->drawCentreString("Info Orbs", ScreenCenterX, ScreenCenterY - 50, 22);
    sm->drawCentreString("", ScreenCenterX, ScreenCenterY - 5, 22);
    sm->drawCentreString("", ScreenCenterX, ScreenCenterY + 30, 22);
    sm->setFontColor(TFT_RED);
    sm->drawCentreString("v6.7.69", ScreenCenterX, ScreenCenterY + 65, 14);

    sm->selectScreen(2);

    TJpgDec.setJpgScale(1);
    TJpgDec.drawJpg(0, 0, logo_start, logo_end - logo_start);

    widgetSet = new WidgetSet(sm);

#ifdef GC9A01_DRIVER
    Serial.println("GC9A01 Driver");
#endif
#if HARDWARE == WOKWI
    Serial.println("Wokwi Build");
#endif

    pinMode(BUSY_PIN, OUTPUT);
    Serial.println("Connecting to WiFi");

    wifiWidget = new WifiWidget(*sm);
    wifiWidget->setup();

    globalTime = GlobalTime::getInstance();

    widgetSet->add(new ClockWidget(*sm));

    setupWebServer();

    m_widgetCycleDelayPrev = millis();
}

void checkCycleWidgets() {
    if (m_widgetCycleDelay > 0 && (m_widgetCycleDelayPrev == 0 || (millis() - m_widgetCycleDelayPrev) >= m_widgetCycleDelay)) {
        widgetSet->next();
        m_widgetCycleDelayPrev = millis();
    }
}

void checkButtons() {
    // Reset cycle timer whenever a button is pressed
    if (buttonLeft.pressedShort()) {
        // Left short press cycles widgets backward
        Serial.println("Left button short pressed -> switch to prev Widget");
        m_widgetCycleDelayPrev = millis();
        widgetSet->prev();
    } else if (buttonRight.pressedShort()) {
        // Right short press cycles widgets forward
        Serial.println("Right button short pressed -> switch to next Widget");
        m_widgetCycleDelayPrev = millis();
        widgetSet->next();
    } else {
        ButtonState leftState = buttonLeft.getState();
        ButtonState middleState = buttonOK.getState();
        ButtonState rightState = buttonRight.getState();

        // Everying else that is not BTN_NOTHING will be forwarded to the current widget
        if (leftState != BTN_NOTHING) {
            Serial.printf("Left button pressed, state=%d\n", leftState);
            m_widgetCycleDelayPrev = millis();
            widgetSet->buttonPressed(BUTTON_LEFT, leftState);
        } else if (middleState != BTN_NOTHING) {
            Serial.printf("Middle button pressed, state=%d\n", middleState);
            m_widgetCycleDelayPrev = millis();
            widgetSet->buttonPressed(BUTTON_OK, middleState);
        } else if (rightState != BTN_NOTHING) {
            Serial.printf("Right button pressed, state=%d\n", rightState);
            m_widgetCycleDelayPrev = millis();
            widgetSet->buttonPressed(BUTTON_RIGHT, rightState);
        }
    }
}

void loop() {
    server.handleClient();
    
    if (wifiWidget->isConnected() == false) {
        wifiWidget->update();
        wifiWidget->draw();
        widgetSet->setClearScreensOnDrawCurrent(); // Clear screen after wifiWidget
        delay(100);
    } else {
        if (!widgetSet->initialUpdateDone()) {
            widgetSet->initializeAllWidgetsData();
        }
        globalTime->updateTime();

        checkButtons();

        widgetSet->updateCurrent();
        widgetSet->updateBrightnessByTime(globalTime->getHour24());
        widgetSet->drawCurrent();

        checkCycleWidgets();
    }
}
