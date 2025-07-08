#include <Arduino.h>
#include <IRremote.hpp>
#include <WiFi.h>
#include <esp_now.h>

/* Definitions */
#define IR_SEND_PIN 4          // ESP32 GPIO pin for IR LED
#define STATUS_LED_PIN 2       // ESP32 built-in LED for connection status

// LG AC Constants
#define LG_KHZ 38
#define LG_AC_SIGNATURE    0x88
#define LG_AC_POWER_OFF    0x00
#define LG_AC_POWER_ON     0x08
#define LG_AC_MODE_COOL    0x08
#define LG_AC_MODE_DRY     0x04
#define LG_AC_MODE_FAN     0x02
#define LG_AC_MODE_AUTO    0x0B
#define LG_AC_MODE_HEAT    0x0C
#define LG_AC_FAN_AUTO     0x0
#define LG_AC_FAN_LOW      0x1
#define LG_AC_FAN_MID      0x2
#define LG_AC_FAN_HIGH     0x3

const uint8_t LG_AC_MIN_TEMP = 16;
const uint8_t LG_AC_MAX_TEMP = 30;

// Structure for AC control message from Master
typedef struct ac_control_message {
    bool power;           // true = ON, false = OFF
    uint8_t temperature;  // 16-30°C
    uint8_t mode;        // COOL/DRY/FAN/AUTO/HEAT
    uint8_t fanSpeed;    // AUTO/LOW/MID/HIGH
    bool swing;          // true = ON, false = OFF
} ac_control_message;

// Current AC state
ac_control_message currentState = {
    .power = false,
    .temperature = 25,
    .mode = LG_AC_MODE_COOL,
    .fanSpeed = LG_AC_FAN_AUTO,
    .swing = false
};

// Master MAC Address (replace with your master's MAC)
uint8_t masterMAC[] = {0xEC,0xE3,0x34,0xBE,0x97,0x1C};  // Replace with actual MAC

// ESP-NOW Connection Status
bool masterConnected = false;

// Function to calculate IR command checksum
uint8_t calculateChecksum(uint16_t command) {
    uint8_t checksum = 0;
    for (int i = 0; i < 4; i++) {
        checksum += (command >> (i * 4)) & 0xF;
    }
    return checksum & 0xF;
}

// Function to create LG AC IR command
uint32_t createLGACCommand(uint8_t temp, uint8_t mode, uint8_t fanSpeed, bool swing, bool powerOn) {
    uint16_t command = 0;
    
    // Power
    if (powerOn) {
        command |= LG_AC_POWER_ON;
    }
    
    // Temperature (offset from 16 degrees)
    temp = constrain(temp, LG_AC_MIN_TEMP, LG_AC_MAX_TEMP);
    uint8_t tempCode = temp - 15;
    command |= (tempCode << 4);
    
    // Mode
    command |= mode;
    
    // Fan Speed
    command |= (fanSpeed << 4);
    
    // Create full command
    uint32_t fullCommand = ((uint32_t)LG_AC_SIGNATURE << 20) | (command << 4);
    
    // Add checksum
    fullCommand |= calculateChecksum(command);
    
    return fullCommand;
}

// Callback when data is received from Master
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (len == sizeof(ac_control_message)) {
        memcpy(&currentState, incomingData, sizeof(ac_control_message));
        
        // Print received data for debugging
        Serial.println("\n--------------------");
        Serial.println("Received AC Command:");
        Serial.print("From: ");
        for(int i=0; i<6; i++) {
            Serial.print(info->src_addr[i], HEX);
            if(i<5) Serial.print(":");
        }
        Serial.println();
        
        Serial.print("Power: ");
        Serial.println(currentState.power ? "ON" : "OFF");
        Serial.print("Temp: ");
        Serial.print(currentState.temperature);
        Serial.println("C");
        Serial.print("Mode: ");
        switch(currentState.mode) {
            case LG_AC_MODE_COOL: Serial.println("COOL"); break;
            case LG_AC_MODE_DRY: Serial.println("DRY"); break;
            case LG_AC_MODE_FAN: Serial.println("FAN"); break;
            case LG_AC_MODE_AUTO: Serial.println("AUTO"); break;
            case LG_AC_MODE_HEAT: Serial.println("HEAT"); break;
            default: Serial.println("UNKNOWN");
        }
        Serial.print("Fan: ");
        switch(currentState.fanSpeed) {
            case LG_AC_FAN_AUTO: Serial.println("AUTO"); break;
            case LG_AC_FAN_LOW: Serial.println("LOW"); break;
            case LG_AC_FAN_MID: Serial.println("MID"); break;
            case LG_AC_FAN_HIGH: Serial.println("HIGH"); break;
            default: Serial.println("UNKNOWN");
        }
        Serial.print("Swing: ");
        Serial.println(currentState.swing ? "ON" : "OFF");

        // Create and send IR command
        uint32_t acCommand = createLGACCommand(
            currentState.temperature,
            currentState.mode,
            currentState.fanSpeed,
            currentState.swing,
            currentState.power
        );
        
        IrSender.sendLGRaw(acCommand, 0);
        Serial.print("IR Command: 0x");
        Serial.println(acCommand, HEX);
        Serial.println("--------------------\n");
    }
}

// Callback when data is sent to Master
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    masterConnected = (status == ESP_NOW_SEND_SUCCESS);
    Serial.print("Status: ");
    Serial.println(masterConnected ? "Connected" : "Disconnected");
}

void setup() {
    Serial.begin(115200);
    
    // Initialize IR sender
    IrSender.begin(IR_SEND_PIN, true, LED_BUILTIN);
    
    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // Set up ESP-NOW
    WiFi.mode(WIFI_STA);
    
    // Get current WiFi channel

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    // Add Master as peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, masterMAC, 6);
    peerInfo.channel = 1;  // Use current WiFi channel
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    Serial.println("\nLG AC IR Controller Ready");
    Serial.print("Slave MAC: ");
    uint8_t mac[6];
    WiFi.macAddress(mac);
    for(int i=0; i<6; i++) {
        Serial.print(mac[i], HEX);
        if(i<5) Serial.print(":");
    }
    Serial.println("\n");
}

void loop() {
    // Update status LED based on master connection
    digitalWrite(STATUS_LED_PIN, masterConnected ? HIGH : LOW);
    
} 