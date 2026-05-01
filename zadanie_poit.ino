#include <DHT.h>

// --- KONFIGURÁCIA PINOV ---
#define DHTPIN 4           // Pin pre dátový kábel DHT11 (Žltý/Biely)
#define DHTTYPE DHT11      // Typ senzora (zmeň na DHT22, ak by si mal iný)
#define FAN_PWM_PIN 9      // Pin pre PWM reguláciu ventilátora (Modrý)
#define FAN_TACHO_PIN 2    // Pin pre Tachometer (Zelený) - podporuje prerušenie

DHT dht(DHTPIN, DHTTYPE);

// --- PREMENNÉ PRE MERANIE OTÁČOK (RPM) ---
// Slovo 'volatile' hovorí Arduinu, že táto premenná sa mení na pozadí v prerušení
volatile unsigned int tachPulses = 0; 
unsigned long lastRpmTime = 0;
int currentRpm = 0;

// --- PRERUŠOVACIA FUNKCIA (ISR) ---
// Táto funkcia sa bleskovo zavolá VŽDY, keď ventilátor pošle impulz,
// bez ohľadu na to, čo práve Arduino robí v hlavnom loope.
void countPulses() {
  tachPulses++;
}

void setup() {
  // Inicializácia sériovej komunikácie (rýchlosť 9600 pre USB prepojenie s PC)
  Serial.begin(9600);
  
  // Inicializácia teplomera
  dht.begin();

  // Nastavenie pinov pre ventilátor
  pinMode(FAN_PWM_PIN, OUTPUT);
  // Tachometer PC ventilátora funguje ako spínač voči zemi (Open-Collector),
  // preto tu MUSÍME zapnúť interný pull-up rezistor Arduina:
  pinMode(FAN_TACHO_PIN, INPUT_PULLUP); 

  // Nastavenie hardvérového prerušenia na pin D2. 
  // RISING znamená, že rátame impulz vždy, keď napätie stúpne z 0V na 5V.
  attachInterrupt(digitalPinToInterrupt(FAN_TACHO_PIN), countPulses, RISING);

  // Počiatočný stav: ventilátor je vypnutý (0% výkon)
  analogWrite(FAN_PWM_PIN, 0);
}

void loop() {
    // ---------------------------------------------------------
  // 1. ČASŤ: PRIJÍMANIE PRÍKAZOV Z FLASKU (Nastavenie výkonu)
  // ---------------------------------------------------------
  if (Serial.available() > 0) {
    int pwmValue = Serial.parseInt();
    
    // Pridáme podmienku, aby sme ignorovali "falošné" nuly zo znakov Enteru.
    // Ak chce Flask/Sériový monitor reálne poslať 0, stačí poslať niečo iné, 
    // ale pre istotu vyčistíme zvyšok bufferu:
    if (pwmValue >= 0 && pwmValue <= 255) {
      analogWrite(FAN_PWM_PIN, pwmValue);
    }
    
    // TOTO JE TÁ MAGICKÁ OPRAVA:
    // Odstráni z pamäte všetky zvyšné neviditeľné znaky (ako Enter),
    // aby sa na ne v ďalšom cykle nespustil parseInt() s výsledkom 0.
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
    // Dočasne pozastavíme prerušenia, aby sme mohli bezpečne prečítať a vynulovať impulzy
    noInterrupts();
    unsigned int copyPulses = tachPulses;
    tachPulses = 0; 
    interrupts(); // Znovu zapneme prerušenia

    // Matematika: Bežný PC ventilátor pošle presne 2 impulzy na 1 celú otáčku.
    // Za 1 sekundu sme napočítali napr. 40 impulzov = 20 otáčok za sekundu.
    // Za minútu to je: 20 * 60 = 1200 RPM.
    currentRpm = (copyPulses / 2) * 60;
    lastRpmTime = currentTime;

    // --- Čítanie teploty z DHT11 ---
    float temperature = dht.readTemperature();

    // --- Odoslanie paketu do Pythonu ---
    // Pošleme to v jednoduchom tvare oddelenom čiarkou: TEPLOTA,RPM
    // Príklad výstupu: 25.40,1200
    
    if (isnan(temperature)) {
      Serial.print("ERR"); // Ak sa senzor odpojil, pošli error
    } else {
      Serial.print(temperature);
    }
    
    Serial.print(",");
    Serial.println(currentRpm);
  }
}