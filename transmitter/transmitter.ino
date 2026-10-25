#include <NimBLEDevice.h>
#include "secrets.h"

void setup() {
  Serial.begin(115200);
  delay(2000);

  NimBLEDevice::init(SECRET_BEACON_NAME);

  Serial.print("BLE MAC: ");
  Serial.println(NimBLEDevice::getAddress().toString().c_str());

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setName(SECRET_BEACON_NAME);
  adv->addServiceUUID(SECRET_SERVICE_UUID);
  adv->enableScanResponse(true);
  adv->start();

  Serial.println("Advertising started");
}

void loop() {
  delay(1000);
}