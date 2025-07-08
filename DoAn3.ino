#define BLYNK_TEMPLATE_ID "TMPL6WJZ78ir6"
#define BLYNK_TEMPLATE_NAME "Do An3"
#define BLYNK_AUTH_TOKEN "3er4xEMLmz6saObN0_1QAbcoZvAn1JjN"

#include <DHT.h>
#include <WiFi.h>
#include <Arduino.h>
#include <esp_now.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define DHTpin 2 // D4
#define RXp2 16
#define TXp2 17
#define led 5 //d5
#define fan 18 //d18

// Khởi tạo LCD với địa chỉ I2C là 0x27, 16 cột và 2 dòng
LiquidCrystal_I2C lcd(0x27, 16, 2);
const int maximum_people = 1;
const int DHTTYPE = DHT22;
float temp = 0;
float humid = 0;
bool fanState = false;
bool ledState = false;
bool tuDong = true;
int humanCount = 0;
unsigned long last_manu_time = 0;
unsigned long last_lcd_update = 0;  // Thêm biến để kiểm soát thời gian cập nhật LCD
const unsigned long LCD_UPDATE_INTERVAL = 2000; // Cập nhật LCD mỗi 2 giây
int tempAC = 25;

// Địa chỉ MAC của ESP32 Slave
uint8_t slaveAddress[] = {0x38,0x18,0x2B,0xEA,0x98,0xB4};

DHT dht(DHTpin, DHTTYPE);

char auth[] = "ypkViQjn8NIOe71UiIlaw9oInhC9DrJ5";
char ssid[] = "Nguyen Thi Quy";
char pass[] = "benmiller";

// AC Control Constants
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

// Callback khi gửi dữ liệu ESP-NOW
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("\r\nLast Packet Send Status:\t");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup_wifi_ESP_NOW() {
  
  // Khởi động WiFi ở chế độ AP_STA
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // In MAC Address để debug
  Serial.print("ESP_MASTER MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  Serial.println("\nWiFi connected");
  
  // In channel hiện tại để debug
  Serial.print("Current WiFi channel: ");
  Serial.println(WiFi.channel());
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Khởi tạo ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Đăng ký callback gửi dữ liệu
  esp_now_register_send_cb(OnDataSent);

  // Thiết lập peer với channel AUTO (0)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, slaveAddress, 6);
  peerInfo.channel = 0;         // AUTO CHANNEL - không cố định
  peerInfo.encrypt = false;

  // Đăng ký peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
    return;
  }

  Serial.println("ESP-NOW peer added successfully");
}

// Cấu trúc dữ liệu điều khiển
typedef struct ac_control_message {
    bool power;           // true = ON, false = OFF
    uint8_t temperature;  // 16-30°C
    uint8_t mode;        // COOL/DRY/FAN/AUTO/HEAT
    uint8_t fanSpeed;    // AUTO/LOW/MID/HIGH
    bool swing;          // true = ON, false = OFF
} ac_control_message;

// Biến lưu trạng thái hiện tại với swing mặc định là false
ac_control_message currentState = {
    .power = false,
    .temperature = 25,
    .mode = LG_AC_MODE_AUTO,
    .fanSpeed = LG_AC_FAN_LOW,
    .swing = false
};

// Hàm gửi trạng thái hiện tại đến Slave
void sendCurrentState() {
    esp_err_t result = esp_now_send(slaveAddress, (uint8_t *)&currentState, sizeof(currentState));
    if (result == ESP_OK) {
        Serial.println("Sent with success\r\n");
        // Cập nhật trạng thái lên Blynk
        Blynk.virtualWrite(V12, currentState.power);
        Blynk.virtualWrite(V10, currentState.temperature);
    } else {
        Serial.println("Error sending the data\r\n");
    }
}

void getTempHumid(){
  temp = dht.readTemperature();
  humid = dht.readHumidity();
}

void HandleTemp(){
  if (humanCount <= 0){
    fanState = false;
    ledState = false;
    if(currentState.power)
    {
      currentState.power = false;
      sendCurrentState();
    }
  }
  else{
    ledState = true;
    if(temp > 25 && tuDong){
      fanState = true;
      if(!currentState.power)  // Chỉ tự động bật điều hòa khi ở chế độ tự động
      {
        currentState.power = true;
        currentState.temperature = 20;
        currentState.mode = LG_AC_MODE_COOL;
        currentState.fanSpeed = LG_AC_FAN_AUTO;
        sendCurrentState();
      }
    }
    else if ((temp < 14) && tuDong){
      fanState = false;
      if(currentState.power)
      {
        currentState.power = false;
        sendCurrentState();
      }
    }
    else if ((temp < 25) && tuDong){
      fanState = true;
    }
  }
}

void getUART(){
  if (Serial2.available()>1) {
    String receivedString = Serial2.readString();
    String msg = receivedString.substring(7);
    humanCount = msg.toInt();
    Serial.print("Human number: ");
    Serial.println(humanCount);
  }
}

BLYNK_CONNECTED() {
  Blynk.syncAll();
}
BLYNK_WRITE(V4){ // AC Power
    int buttonState = param.asInt();
    if (buttonState == 1) {  // Chỉ xử lý khi nút được nhấn và trạng thái thay đổi
        currentState.power = !currentState.power;  // Đảo trạng thái nguồn
        tuDong = false;
        
        if (currentState.power) {
            // Khởi tạo trạng thái mặc định khi bật nguồn
            currentState.temperature = 25;
            currentState.mode = LG_AC_MODE_COOL;
            currentState.fanSpeed = LG_AC_FAN_AUTO;
            currentState.swing = false;
        }
        sendCurrentState(); // Gửi trạng thái sau khi thay đổi
        last_manu_time = millis();
    }
}
BLYNK_WRITE(V1){ // tang temp AC
      if (param.asInt() == 1 && currentState.temperature < LG_AC_MAX_TEMP) {
        currentState.temperature++;
        sendCurrentState();
    }
    tuDong = false;
    last_manu_time = millis();
}
BLYNK_WRITE(V2){ // giam temp AC
    if (param.asInt() == 1 && currentState.temperature > LG_AC_MIN_TEMP) {
        currentState.temperature--;
        sendCurrentState();
    }
    tuDong = false;
    last_manu_time = millis();
}
BLYNK_WRITE(V5) { // Xử lý chọn Mode
    switch (param.asInt()) {
        case 0: currentState.mode = LG_AC_MODE_AUTO; break;
        case 1: currentState.mode = LG_AC_MODE_FAN; break;
        case 2: currentState.mode = LG_AC_MODE_DRY; break;
        case 3: currentState.mode = LG_AC_MODE_COOL; break;
        case 4: currentState.mode = LG_AC_MODE_HEAT; break;
    }
    tuDong = false;
    sendCurrentState();
    last_manu_time = millis();
}
BLYNK_WRITE(V9) { // Xử lý chọn Speed
    switch (param.asInt()) {
        case 0: currentState.fanSpeed = LG_AC_FAN_AUTO;break;
        case 1: currentState.fanSpeed = LG_AC_FAN_LOW; break;
        case 2: currentState.fanSpeed = LG_AC_FAN_MID; break;
        case 3: currentState.fanSpeed = LG_AC_FAN_HIGH; break;
    }
    tuDong = false;
    sendCurrentState();
    last_manu_time = millis();
}

BLYNK_WRITE(V6){  // led
  int p = param.asInt();
  tuDong = false;
  last_manu_time = millis();
  digitalWrite(led,p);
}
BLYNK_WRITE(V7){ // fan
  int p = param.asInt();
  tuDong = false;
  last_manu_time = millis();
  digitalWrite(fan,p);
}

void setup() {
  Serial.begin(115200); 
  Wire.begin();  // Khởi tạo I2C trước khi sử dụng LCD
  delay(1000);
  
  // Khởi tạo LCD an toàn
  lcd.init();
  delay(100);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  
  dht.begin();
  Serial.println("Ready!");
  Serial2.begin(115200, SERIAL_8N1, RXp2, TXp2); 
  setup_wifi_ESP_NOW();

  pinMode(led, OUTPUT);
  pinMode(fan, OUTPUT);
  digitalWrite(fan, LOW);
  digitalWrite(led, LOW);

  Blynk.virtualWrite(V6, LOW); //den
  Blynk.virtualWrite(V7, LOW); //fan
}

void loop() {
  Blynk.run();
  getTempHumid();
  // humanCount = 20;
  HandleTemp();

  uint8_t timeout = millis() - last_manu_time;
  if (timeout >= 1800000) {
    tuDong = true;
    Serial.println("Switched to AUTO mode");
  }

  if (tuDong) {
    getUART();
    if(ledState) {
      digitalWrite(led, HIGH);
    } else {
      digitalWrite(led, LOW);
    }
    
    if(fanState) {
      digitalWrite(fan, HIGH);
    } else {
      digitalWrite(fan, LOW);
    }
    Blynk.virtualWrite(V6, ledState);
    Blynk.virtualWrite(V7, fanState);
  }

  // Cập nhật LCD với tần suất định kỳ để tránh refresh quá nhanh
  unsigned long currentMillis = millis();
  if (currentMillis - last_lcd_update >= LCD_UPDATE_INTERVAL) {
    last_lcd_update = currentMillis;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("People Count: ");
    lcd.print(humanCount);
    
    if (humanCount > maximum_people) {
      lcd.setCursor(0, 1);
      lcd.print("Too many people!");
      Blynk.logEvent("canh_bao_qua_so_nguoi_trong_phong", "Too many people!");
    }
  }

  // Gửi dữ liệu lên Blynk
  Blynk.virtualWrite(V8, humid);
  Blynk.virtualWrite(V0, humanCount);
  Blynk.virtualWrite(V3, temp);
  Blynk.virtualWrite(V11, tuDong);
  // Debug print
  Serial.print("Power: ");
  Serial.print(currentState.power);
  Serial.print(" Mode: ");
  Serial.print(tuDong ? "AUTO" : "MANUAL");
  Serial.print(" Temp: ");
  Serial.println(currentState.temperature);
  
  delay(1000);
}