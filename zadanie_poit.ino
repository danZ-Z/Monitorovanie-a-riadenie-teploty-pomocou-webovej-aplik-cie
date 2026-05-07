#include <DHT.h>

// --- KONFIGURÁCIA PINOV ---
#define DHTPIN 4           // Pin pre dátový kábel DHT11
#define DHTTYPE DHT11      // Typ senzora 
#define FAN_PWM_PIN 9      // Pin pre PWM reguláciu ventilátora
#define FAN_TACHO_PIN 2    // Pin pre Tachometer

DHT dht(DHTPIN, DHTTYPE);

// --- PREMENNÉ PRE MERANIE OTÁČOK (RPM) ---
volatile unsigned int tachPulses = 0; 
unsigned long lastRpmTime = 0;
int currentRpm = 0;

// --- PRERUŠOVACIA FUNKCIA (ISR) ---
void countPulses() {
  tachPulses++;
}

void setup() {
  Serial.begin(9600);
  
  // Inicializácia teplomera
  dht.begin();

  // Nastavenie pinov pre ventilátor
  pinMode(FAN_PWM_PIN, OUTPUT);

  pinMode(FAN_TACHO_PIN, INPUT_PULLUP); 


  attachInterrupt(digitalPinToInterrupt(FAN_TACHO_PIN), countPulses, RISING);

  analogWrite(FAN_PWM_PIN, 0);
}

void loop() {
    // ---------------------------------------------------------
  // 1. ČASŤ: PRIJÍMANIE PRÍKAZOV Z FLASKU (Nastavenie výkonu)
  // ---------------------------------------------------------
  if (Serial.available() > 0) {
    int pwmValue = Serial.parseInt();
    
    if (pwmValue >= 0 && pwmValue <= 255) {
      analogWrite(FAN_PWM_PIN, pwmValue);
    }

    while(Serial.available() > 0) {
      Serial.read();
    }
  }

  // ---------------------------------------------------------
  // 2. ČASŤ: ODOSIELANIE DÁT DO FLASKU (Každú 1 sekundu)
  // ---------------------------------------------------------
  unsigned long currentTime = millis();
  if (currentTime - lastRpmTime >= 1000) {
    
    // --- Výpočet RPM ---
    noInterrupts();
    unsigned int copyPulses = tachPulses;
    tachPulses = 0; 
    interrupts();

    currentRpm = (copyPulses / 2) * 60;
    lastRpmTime = currentTime;

    float temperature = dht.readTemperature();
    
    if (isnan(temperature)) {
      Serial.print("ERR"); 
    } else {
      Serial.print(temperature);
    }
    
    Serial.print(",");
    Serial.println(currentRpm);
  }
}