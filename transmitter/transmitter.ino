#include <NimBLEDevice.h>

void setup() {
  Serial.begin(115200);
  delay(2000);  // 시리얼 안정화 대기

  NimBLEDevice::init("MY-BEACON");

  // 현재 보드의 MAC 주소 출력 (수신기에 등록할 주소)
  Serial.print("BLE MAC: ");
  Serial.println(NimBLEDevice::getAddress().toString().c_str());

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setName("MY-BEACON");
  adv->addServiceUUID("12345678-1234-1234-1234-1234567890ab");
  adv->enableScanResponse(true);
  adv->start();

  Serial.println("Advertising started");
}

void loop() {
  delay(1000);
}