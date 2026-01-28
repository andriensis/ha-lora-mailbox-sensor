# HomeAssistant LoRa Mailbox contact sensor

This is a small custom projects built because I needed a mailbox sensor to detect when the mailman leaves some mail in my mailbox.
My mailbox is pretty distant from the apartment so I couldn't really use WiFi, Zigbee or Thread devices. Long range zigbee/zwave might have been an option but I decided to use the devices I already have to build a private LoRa network.


# Requirements
- **Heltec V2** (V3 should work too, haven't tested it) to act as a LoRa gateway
I'm in EU so I use 868 Mhz. If you're outside EU you should check local regulations to know which bandwidth you're supposed to use. Note: any device with LoRa capabilities should work here. This device will always be powered on (maybe also have a battery backup) and the only requirement other than LoRa is WiFi. We will run EspHome on this device to link it to our HomeAssistant instance.
- **Heltec nrf T114**
Any nRF52840 LoRa capable device should work here. This will be the actual device we put in the mailbox and will be battery powered. We're choosing nRF52840 devices here for their very low power consumption in deep sleep.
- **LiPo or 18650 batteries for the Heltec nrf T114**
- **Reed contact sensor** such as PS3150
- Optional: LiPo or 18650 batteries for the Heltec v2
- ESPHome
- Arduino IDE

## How it works

The Heltec V2 is acting as our LoRa gateway aka receiver in our apartment. This will always be powered on and listen to any LoRa communication happening over our private network. You might need to adjust its position inside your apartment for better communication. In my case with a distance of <100 meters (with other apartments in between of course) position doesn't really matter and I always get good readings from the sensor.

The Heltec nrf t114 will be physically placed our mailbox. You will wire a reed contact sensor to PIN 28 and the device will deep sleep until the contact sensor detects a change. Once it wakes up it will poll the contact sensor state for ~2 seconds and then send a LoRa message with: contact sensor state, telemetry (battery percentage, voltage, ...)

## Wiring

Just connect your reed contact sensor to GND and PIN28 of your Heltec nrf t114. Polarity doesn't matter. So one wire to GND and the other to PIN28.

## Setup

- Make sure to solder or attach the contact sensor to your Heltec nrf t114
- Install the ESP Home configuration in [esp-home-heltec-v2-gateway.yaml](https://github.com/andriensis/ha-lora-mailbox-sensor/blob/main/src/esp-home-heltec-v2-gateway.yaml "esp-home-heltec-v2-gateway.yaml") on our Heltec V2
Make sure the change the **ESP_HOME_ENCRYPTION_KEY** and **OTA_PASSWORD** values
- Install the arduino code found in [mailbox-contact-sensor-heltec-nrf-t114.ino](https://github.com/andriensis/ha-lora-mailbox-sensor/blob/main/src/mailbox-contact-sensor-heltec-nrf-t114.ino "mailbox-contact-sensor-heltec-nrf-t114.ino") on your Heltec nrf t114
- Power on the devices
- Make sure when the contact sensor changes state a message is sent to your gateway (check the Esp Home logs)
- Attach batteries to the nrf t114
- Place the device in your mailbox so the contact sensor is triggered when it's opened (note: a motion sensor might also work in this case but you'll probably need some resistance to prevent false alarms).

## Mailbox sensor

![Picture of the mailbox contact sensor in the mailbox](https://github.com/andriensis/ha-lora-mailbox-sensor/blob/main/mailbox-sensor.jpg?raw=true)

## Why not Meshtastic?
I actually already have a pretty stable Meshtastic network around my house. However I couldn't really use because deep sleep is not properly supported on nRF52840 devices. I wanted my battery to last a long time (months, maybe even more) and this was just not possible with Meshtastic.

## How long will the batteries last?
I have been testing this for a few weeks with x2 18650 batteries. Theoretical battery life should be >1 year. Some normal discharge will however happen due to environment conditions and so on. But overall I would say a battery life of a few months is definitely guaranteed. Also if you use 18650 you can just open the mailbox and swap them once they're discharged.
