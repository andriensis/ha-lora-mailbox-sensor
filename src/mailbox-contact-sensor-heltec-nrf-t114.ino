#include "Arduino.h"
#include "heltec_nrf_lorawan.h"

#define RF_FREQUENCY                868000000  // change this if outside EU
#define TX_OUTPUT_POWER             22        
#define LORA_BANDWIDTH              0         
#define LORA_SPREADING_FACTOR       12       
#define LORA_CODINGRATE             4          
#define LORA_PREAMBLE_LENGTH        8          
#define LORA_SYMBOL_TIMEOUT         0          
#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON        false

#define CONTACT_PIN 28             // Contact sensor input
#define VBAT_ADC_PIN 4             // Battery ADC
#define VBAT_CTRL_PIN 6            // Battery measurement control

#define POST_TX_DELAY 3000                     // Wait 3 seconds after TX before sleep
#define BUFFER_SIZE 30                         // Payload buffer size

#define DEVICE_ID 0x01 // change this if using multiple devices on the same custom lora network to identify them

__attribute__((section(".noinit"))) uint32_t messageCounter;
__attribute__((section(".noinit"))) uint32_t bootCount;
__attribute__((section(".noinit"))) uint8_t lastContactState;
__attribute__((section(".noinit"))) uint32_t magicNumber;

#define MAGIC_NUMBER 0xCAFEBABE

static RadioEvents_t RadioEvents;
bool txDone = false;
bool txTimeout = false;

void OnTxDone(void) {
    debug_printf("TX done\r\n");
    txDone = true;
}

void OnTxTimeout(void) {
    Radio.Sleep();
    debug_printf("TX Timeout\r\n");
    txTimeout = true;
}

void enableBatteryMeasurement() {
    pinMode(VBAT_CTRL_PIN, OUTPUT);
    digitalWrite(VBAT_CTRL_PIN, HIGH);
    delay(10);
}

void disableBatteryMeasurement() {
    digitalWrite(VBAT_CTRL_PIN, LOW);
}

float readBatteryVoltage() {
    enableBatteryMeasurement();
    
    int rawValue = analogRead(VBAT_ADC_PIN);
    
    debug_printf("[Debug] Battery ADC raw value: %d\r\n", rawValue);
    
    float voltage = (rawValue / 1023.0) * 3.6 * 4.91;
    
    disableBatteryMeasurement();
    
    return voltage;
}

uint8_t calculateBatteryPercentage(float voltage) {
    if (voltage >= 4.20) return 100;
    if (voltage >= 4.15) return 98;
    if (voltage >= 4.10) return 95;
    if (voltage >= 4.05) return 92;
    if (voltage >= 4.00) return 88;
    if (voltage >= 3.95) return 83;
    if (voltage >= 3.90) return 78;
    if (voltage >= 3.85) return 72;
    if (voltage >= 3.80) return 66;
    if (voltage >= 3.75) return 59;
    if (voltage >= 3.70) return 52;
    if (voltage >= 3.65) return 45;
    if (voltage >= 3.60) return 38;
    if (voltage >= 3.55) return 32;
    if (voltage >= 3.50) return 26;
    if (voltage >= 3.45) return 21;
    if (voltage >= 3.40) return 17;
    if (voltage >= 3.35) return 13;
    if (voltage >= 3.30) return 10;
    if (voltage >= 3.25) return 7;
    if (voltage >= 3.20) return 5;
    if (voltage >= 3.10) return 3;
    if (voltage >= 3.00) return 1;
    
    return 0;
}

// ========== CONTACT SENSOR ==========

bool readContactState() {
    return digitalRead(CONTACT_PIN);
}

// ========== LORA TRANSMISSION ==========

void sendLoRaMessage(bool isContactEvent) {
    // Read telemetry
    float voltage = readBatteryVoltage();
    uint8_t batteryPercent = calculateBatteryPercentage(voltage);
    uint8_t contactState = readContactState() ? 1 : 0;
    
    debug_printf("\r\n[Telemetry] Reading sensors...\r\n");
    debug_printf("  Contact State: %s\r\n", contactState ? "CLOSED" : "OPEN");
    debug_printf("  Battery Voltage: %.2f V\r\n", voltage);
    debug_printf("  Battery Percent: %d %%\r\n", batteryPercent);
    debug_printf("  Boot Count: %d\r\n", bootCount);
    debug_printf("  Message Count: %d\r\n", messageCounter);
    
    uint8_t payload[11];
    payload[0] = DEVICE_ID;
    payload[1] = messageCounter & 0xFF;
    payload[2] = (messageCounter >> 8) & 0xFF;
    payload[3] = (contactState & 0x01) | ((isContactEvent ? 1 : 0) << 1);
    payload[4] = batteryPercent;
    
    uint16_t voltageMillivolts = (uint16_t)(voltage * 1000.0);
    payload[5] = (voltageMillivolts >> 8) & 0xFF;
    payload[6] = voltageMillivolts & 0xFF;
    
    uint32_t timestamp = millis();
    payload[7] = (timestamp >> 24) & 0xFF;
    payload[8] = (timestamp >> 16) & 0xFF;
    payload[9] = (timestamp >> 8) & 0xFF;
    payload[10] = timestamp & 0xFF;
    
    debug_printf("[LoRa] Sending message #%d\r\n", messageCounter);
    debug_printf("  Hex: ");
    for (int i = 0; i < 11; i++) {
        debug_printf("%02X ", payload[i]);
    }
    debug_printf("\r\n");
    
    txDone = false;
    txTimeout = false;
    
    Radio.Send(payload, 11);
    
    // Wait for TX to complete (with timeout)
    unsigned long txStart = millis();
    while (!txDone && !txTimeout && (millis() - txStart < 5000)) {
        TimerLowPowerHandler();
        Radio.IrqProcess();
        delay(10);
    }
    
    if (txDone) {
        debug_printf("[LoRa] Message sent successfully!\r\n");
    } else {
        debug_printf("[LoRa] TX timeout or failed\r\n");
    }
    
    messageCounter++;
}

void goToDeepSleep() {
    debug_printf("\r\n[Sleep] Entering deep sleep...\r\n");
    debug_printf("[Sleep] Will wake ONLY on contact change\r\n");
    
    delay(200);
    
    Radio.Sleep();
    
    NRF_UART0->ENABLE = 0;
    
    uint8_t currentState = digitalRead(CONTACT_PIN);
    uint32_t pin = g_ADigitalPinMap[CONTACT_PIN];
    
    if (currentState) {
        // Currently HIGH (open), wake on LOW (closed)
        nrf_gpio_cfg_sense_input(pin, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
        debug_printf("[Sleep] Configured to wake when contact CLOSES\r\n");
    } else {
        // Currently LOW (closed), wake on HIGH (open)  
        nrf_gpio_cfg_sense_input(pin, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_HIGH);
        debug_printf("[Sleep] Configured to wake when contact OPENS\r\n");
    }
    
    debug_printf("[Sleep] Entering System OFF mode\r\n");
    delay(100);
    
    NRF_POWER->EVENTS_SLEEPENTER = 0;
    NRF_POWER->EVENTS_SLEEPEXIT = 0;
    
    // Enter System OFF mode
    // This will reset the chip on wake-up (GPIO sense or RESET button)
    NRF_POWER->SYSTEMOFF = 1;
    
    while(1) {
        __WFE();
    }
}


void setup() {
    boardInit(LORA_DEBUG_ENABLE, LORA_DEBUG_SERIAL_NUM, 115200);
    delay(100);
    
    bool firstBoot = (magicNumber != MAGIC_NUMBER);
    if (firstBoot) {
        magicNumber = MAGIC_NUMBER;
        messageCounter = 0;
        bootCount = 0;
        lastContactState = 0xFF;  // Unknown
    }
    
    bootCount++;
    
    debug_printf("\r\n\r\n=================================\r\n");
    debug_printf("Heltec T114 Contact Sensor\r\n");
    debug_printf("With Deep Sleep Support\r\n");
    debug_printf("=================================\r\n");
    debug_printf("Boot #%d %s\r\n", bootCount, firstBoot ? "(FIRST BOOT)" : "(WAKE UP)");
    debug_printf("=================================\r\n\r\n");
    
    pinMode(CONTACT_PIN, INPUT_PULLUP);
    delay(10); 
    
    // Sample the contact state multiple times over a period
    // This catches quick open/close events (like mailbox opening briefly)
    debug_printf("[Sensor] Sampling contact state for 2 seconds...\r\n");
    
    uint8_t initialContact = readContactState() ? 1 : 0;
    uint8_t finalContact = initialContact;
    bool stateChangedDuringSampling = false;
    
    unsigned long samplingStart = millis();
    while (millis() - samplingStart < 2000) {  // Sample for 2 seconds
        uint8_t currentSample = readContactState() ? 1 : 0;
        
        if (currentSample != finalContact) {
            finalContact = currentSample;
            stateChangedDuringSampling = true;
            debug_printf("[Sensor]   State changed to: %s\r\n", 
                        finalContact ? "CLOSED" : "OPEN");
        }
        
        delay(50);  // Check every 50ms
    }
    
    if (stateChangedDuringSampling) {
        debug_printf("[Sensor] State changed during sampling period\r\n");
    }
    
    debug_printf("[Sensor] Final state after sampling: %s\r\n", 
                finalContact ? "CLOSED" : "OPEN");
    
    bool contactChanged = false;
    if (firstBoot) {
        // First boot - no previous state to compare
        debug_printf("[Sensor] First boot - current state: %s\r\n", 
                    finalContact ? "CLOSED" : "OPEN");
        contactChanged = false;
    } else {
        // Woke up from sleep - check if state changed
        if (lastContactState != finalContact) {
            contactChanged = true;
            debug_printf("[Sensor] Contact CHANGED: %s -> %s\r\n",
                        lastContactState ? "CLOSED" : "OPEN",
                        finalContact ? "CLOSED" : "OPEN");
        } else {
            debug_printf("[Sensor] Contact unchanged: %s\r\n",
                        finalContact ? "CLOSED" : "OPEN");
            debug_printf("[Sensor] This is likely a manual reset or bounce\r\n");
            contactChanged = false;
        }
    }
    
    lastContactState = finalContact;
    
    pinMode(VBAT_ADC_PIN, INPUT);
    pinMode(VBAT_CTRL_PIN, OUTPUT);
    disableBatteryMeasurement();
    
    debug_printf("[Battery] ADC initialized\r\n");
    
    debug_printf("\r\n[LoRa] Initializing radio...\r\n");
    
    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
    
    debug_printf("[LoRa] Configuration:\r\n");
    debug_printf("  Frequency: %d Hz\r\n", RF_FREQUENCY);
    debug_printf("  Spreading Factor: SF%d\r\n", LORA_SPREADING_FACTOR);
    debug_printf("  Bandwidth: 125 kHz\r\n");
    debug_printf("  Coding Rate: 4/%d\r\n", LORA_CODINGRATE + 4);
    debug_printf("  TX Power: %d dBm\r\n", TX_OUTPUT_POWER);
    
    delay(500);
    sendLoRaMessage(contactChanged);
    
    delay(POST_TX_DELAY);
    
    // Go to deep sleep - will wake on contact change
    goToDeepSleep();
}


void loop() {
    delay(1000);
    goToDeepSleep();
}
