#define BLYNK_TEMPLATE_ID "TMPL627pCUxeA"
#define BLYNK_TEMPLATE_NAME "Peringatan Kualitas Udara"
#define BLYNK_AUTH_TOKEN "OacMGeVIEf9s-5JDKMGcLNxnKUEL46Sq"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include "DHT.h"

// Pin definitions
#define DHTPIN 33   // Pin untuk DHT22 (menggantikan tombol 1)
#define DHTTYPE DHT22 // Jenis sensor DHT
#define MQ135_PIN 35  // Pin untuk sensor MQ-135 (menggantikan tombol 2)
#define GP2Y1010_VOUT 34  // Pin ADC untuk output sensor debu
#define GP2Y1010_LED 22   // Pin untuk kontrol LED sensor debu
#define BUTTON_4 2  // Pin untuk tombol darurat
#define RELAY_1 27   // Pin untuk relay 1
#define RELAY_2 26   // Pin untuk relay 2
#define RELAY_3 25   // Pin untuk relay 3
#define BUZZER 15    // Pin untuk buzzer

// WiFi and Blynk credentials
const char* ssid = "ImanFMobile";
const char* password = "12345678";
char blynkAuth[] = "OacMGeVIEf9s-5JDKMGcLNxnKUEL46Sq";

// Global variables
SemaphoreHandle_t xSemaphore; // Semaphore untuk mengatur akses ke resource yang digunakan bersama
volatile bool sendNotification = false; // Flag untuk mengirim notifikasi
volatile bool emergencyActive = false;  // Status darurat global
volatile bool mq135SensorActive = false; // Status sensor MQ-135
volatile bool button3Active = false;    // Status tombol 3
volatile bool gp2y1010Active = false;  // Status sensor debu

// Inisialisasi sensor DHT
DHT dht(DHTPIN, DHTTYPE);

float dustDensity = 0; // Variabel untuk menyimpan densitas debu
float Voc = 0.6;       // Tegangan awal sensor debu
const float K = 0.5;   // Konstanta kalibrasi sensor debu
int warning = 0;       // Flag untuk peringatan umum

// Moving Average Variables
#define N 100
float movingAverageBuffer[N] = {0}; // Buffer untuk moving average
int movingAverageIndex = 0;         // Indeks buffer moving average
float movingAverageSum = 0;         // Jumlah total buffer moving average
int warningDust = 0;                // Flag untuk peringatan debu
const int warningLEDPin = 2;        // Pin LED built-in ESP32

void setup() {
  // Initialize pin modes
  pinMode(MQ135_PIN, INPUT);
  pinMode(GP2Y1010_VOUT, INPUT);
  pinMode(GP2Y1010_LED, OUTPUT);
  pinMode(BUTTON_4, INPUT_PULLUP);
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(RELAY_3, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(warningLEDPin, OUTPUT); // DEKLARASI PIN BUILT IN LED ESP32 BOARD SEBAGAI INDIKATOR Dust Density

  // Set initial states
  digitalWrite(RELAY_1, HIGH); // Set relay ke keadaan mati (HIGH)
  digitalWrite(RELAY_2, HIGH);
  digitalWrite(RELAY_3, HIGH);
  digitalWrite(BUZZER, LOW);

  Serial.begin(115200);
  Serial.println("Relay initialized to HIGH (inactive)");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Initialize Blynk
  Blynk.begin(blynkAuth, ssid, password);

  // Initialize DHT sensor
  dht.begin();

  // Create semaphore
  xSemaphore = xSemaphoreCreateBinary(); // Membuat semaphore
  xSemaphoreGive(xSemaphore);            // Melepaskan semaphore agar bisa digunakan

  // Create tasks
  xTaskCreatePinnedToCore(taskDHTSensor, "TaskDHTSensor", 2048, NULL, 1, NULL, 1); // Membaca data DHT
  xTaskCreatePinnedToCore(taskMQ135Sensor, "TaskMQ135Sensor", 2048, NULL, 1, NULL, 1); // Membaca data MQ135
  xTaskCreatePinnedToCore(taskGP2Y1010, "TaskGP2Y1010", 2048, NULL, 1, NULL, 1); // Membaca data sensor debu
  xTaskCreatePinnedToCore(taskRelayControl, "TaskRelayControl", 1024, NULL, 1, NULL, 1); // Mengontrol relay
  xTaskCreatePinnedToCore(taskEmergencyButton, "TaskEmergencyButton", 1024, NULL, 2, NULL, 0); // Tombol darurat
}

void loop() {
  Blynk.run(); // Menjalankan Blynk loop

  // Handle notification sending
  if (sendNotification) {
    Serial.println("Preparing to send notification from loop...");
    if (Blynk.connected()) {
      Blynk.logEvent("darurat", "Darurat: Semua relay dan buzzer aktif"); // Mengirim notifikasi ke Blynk
      Serial.println("Notification sent to Blynk");
    } else {
      Serial.println("Failed to send notification: Blynk not connected"); // Gagal mengirim notifikasi
    }
    sendNotification = false; // Reset flag
  }
}

// Task untuk membaca sensor DHT
volatile bool dhtSensorActive = false; // Status sensor DHT

void taskDHTSensor(void *parameter) {
  while (true) {
    float temperature = dht.readTemperature(); // Membaca suhu dari sensor DHT
    if (!isnan(temperature)) {
      Serial.print("Suhu: ");
      Serial.print(temperature);
      Serial.println(" °C");
      if (temperature > 31.0) {
        dhtSensorActive = true; // Aktif jika suhu melebihi batas
      } else {
        dhtSensorActive = false; // Tidak aktif jika suhu normal
      }
    } else {
      Serial.println("Gagal membaca data dari sensor DHT!");
      dhtSensorActive = false;
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Delay 3 detik
  }
}

// Task untuk membaca sensor MQ-135
void taskMQ135Sensor(void *parameter) {
  while (true) {
    int sensorValue = analogRead(MQ135_PIN); // Membaca nilai ADC dari MQ-135
    Serial.print("Nilai sensor MQ-135: ");
    Serial.println(sensorValue);

    if (sensorValue > 1100) { // Ambang batas asap
      mq135SensorActive = true;
    } else {
      mq135SensorActive = false;
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Delay 3 detik
  }
}

// Task untuk membaca sensor debu GP2Y1010
void taskGP2Y1010(void *parameter) {
  while (true) {
    if (xSemaphoreTake(xSemaphore, (TickType_t)10) == pdTRUE) { // Mengambil semaphore untuk mengamankan resource
      // Masuk ke bagian critical section
      float dustValue;

      // Menyalakan LED inframerah pada sensor untuk pengukuran
      digitalWrite(GP2Y1010_LED, LOW);
      delayMicroseconds(280); // Durasi sampling waktu untuk pembacaan sensor

      // Membaca nilai ADC dari sensor GP2Y1010
      int VoRaw = analogRead(GP2Y1010_VOUT);

      // Mematikan LED inframerah setelah pengukuran
      digitalWrite(GP2Y1010_LED, HIGH);
      delayMicroseconds(9620); // Stabilkan setelah membaca

      Serial.print("Nilai ADC sensor debu: "); 
      Serial.println(VoRaw); // Tampilkan pembacaan nilai ADC sensor debu

      // Menghitung tegangan instan sensor berdasarkan nilai ADC
      float Vo = VoRaw / 4095.0 * 3.3; // Tegangan keluaran sensor dalam Volt
      float dV = Vo - Voc; // Perbedaan tegangan dari nilai baseline
      if (dV < 0) {
          dV = 0; // Pastikan perbedaan tegangan tidak negatif
          Voc = Vo; // Update nilai Voc jika ada noise negatif
      }

      // Menghitung densitas debu berdasarkan konstanta kalibrasi
      dustDensity = dV / K * 100.0; // Dalam satuan ug/m^3


      // Cetak hasil akhir densitas debu
      Serial.printf("Dust Density (avg): %.2f ug/m3\n", dustDensity);

      // Periksa apakah densitas debu melebihi ambang batas
      if (dustDensity > 35.0) {
        digitalWrite(warningLEDPin, HIGH); // Nyalakan indikator jika berbahaya
        warningDust = 1;
      } else {
        digitalWrite(warningLEDPin, LOW); // Matikan indikator jika aman
        warningDust = 0;
      }

      dustValue = dustDensity; // Salin nilai debu untuk referensi lebih lanjut

      // Selesai bagian critical section
      xSemaphoreGive(xSemaphore); // Melepaskan semaphore
    } else {
      Serial.println("Failed to acquire semaphore in taskGP2Y1010."); // Log jika semaphore gagal diambil
    }

    vTaskDelay(3000 / portTICK_PERIOD_MS); // Delay 3 detik sebelum pengukuran berikutnya
  }
}

// Task untuk kontrol relay
void taskRelayControl(void *parameter) {
  while (true) {
    if (!emergencyActive) { // Hanya kontrol relay jika tidak dalam mode darurat
      // Hitung jumlah tombol yang aktif termasuk sensor DHT, MQ-135, dan debu
      int activeInputs = (dhtSensorActive ? 1 : 0) + 
                         (mq135SensorActive ? 1 : 0) + 
                         (gp2y1010Active ? 1 : 0);

      // Atur relay berdasarkan jumlah input yang aktif
      digitalWrite(RELAY_1, activeInputs >= 1 ? LOW : HIGH); // Aktifkan relay 1 jika ada minimal 1 input aktif
      digitalWrite(RELAY_2, activeInputs >= 2 ? LOW : HIGH); // Aktifkan relay 2 jika ada minimal 2 input aktif
      digitalWrite(RELAY_3, activeInputs >= 3 ? LOW : HIGH); // Aktifkan relay 3 jika ada semua input aktif
    }
    vTaskDelay(50 / portTICK_PERIOD_MS); // Delay 50 ms
  }
}

// Task untuk tombol darurat
void taskEmergencyButton(void *parameter) {
  int button4HoldTime = 0; // Waktu tekan tombol darurat
  while (true) {
    int button4State = digitalRead(BUTTON_4); // Membaca status tombol darurat
    if (button4State == LOW) { // Jika tombol ditekan
      button4HoldTime++;
      if (button4HoldTime >= 500) { // Jika ditekan lebih dari 5 detik
        if (xSemaphoreTake(xSemaphore, (TickType_t)10) == pdTRUE) { // Mengambil semaphore
          emergencyActive = !emergencyActive; // Toggle mode darurat
          if (emergencyActive) {
            digitalWrite(RELAY_1, LOW);
            digitalWrite(RELAY_2, LOW);
            digitalWrite(RELAY_3, LOW);
            digitalWrite(BUZZER, HIGH);
            sendNotification = true; // Kirim notifikasi darurat
            Serial.println("Emergency activated: All relays and buzzer are ON.");
          } else {
            digitalWrite(RELAY_1, HIGH);
            digitalWrite(RELAY_2, HIGH);
            digitalWrite(RELAY_3, HIGH);
            digitalWrite(BUZZER, LOW);
            Serial.println("Emergency deactivated: All relays and buzzer are OFF.");
          }
          xSemaphoreGive(xSemaphore); // Melepaskan semaphore
        }
        while (digitalRead(BUTTON_4) == LOW) {
          vTaskDelay(10 / portTICK_PERIOD_MS); // Tunggu tombol dilepas
        }
        button4HoldTime = 0; // Reset waktu tekan
      }
    } else {
      button4HoldTime = 0; // Reset waktu tekan jika tombol tidak ditekan
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); // Delay 10 ms
  }
}
