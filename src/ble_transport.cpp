#include "ble_transport.h"

#include "ble_config.h"
#include "led_controller.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_mac.h>

namespace {

BLEServer *server = nullptr;
bool deviceConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer * /*bleServer*/) override { deviceConnected = true; }

  void onDisconnect(BLEServer *bleServer) override {
    deviceConnected = false;
    bleServer->startAdvertising();
  }
};

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    const std::string &value = characteristic->getValue();
    if (value.empty()) {
      return;
    }

    const uint8_t *data = reinterpret_cast<const uint8_t *>(value.data());
    const size_t len = value.size();
    const uint8_t cmd = data[0];

    switch (cmd) {
      case VOLC_CMD_SET_MODE:
        if (len >= 2) {
          g_leds.setMode(data[1]);
        }
        break;
      case VOLC_CMD_SET_COLOR:
        if (len >= 4) {
          g_leds.setColor(data[1], data[2], data[3]);
        }
        break;
      case VOLC_CMD_SET_PIXEL:
        if (len >= 5) {
          g_leds.setPixel(data[1], data[2], data[3], data[4]);
        }
        break;
      case VOLC_CMD_SET_BRIGHTNESS:
        if (len >= 2) {
          g_leds.setBrightness(data[1]);
        }
        break;
      case VOLC_CMD_SET_LED_COUNT:
        if (len >= 2) {
          g_leds.setLedCount(data[1]);
        }
        break;
      default:
        break;
    }
  }
};

String buildDeviceName() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  const uint16_t suffix = (static_cast<uint16_t>(mac[4]) << 8) | mac[5];
  char name[24] = {0};
  snprintf(name, sizeof(name), "%s%04X", VOLC_BLE_DEVICE_NAME_PREFIX, suffix);
  return String(name);
}

} // namespace

void bleTransportBegin() {
  const String deviceName = buildDeviceName();

  BLEDevice::init(deviceName.c_str());
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(VOLC_BLE_SERVICE_UUID);

  BLECharacteristic *dataChar = service->createCharacteristic(
      VOLC_BLE_DATA_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  dataChar->setValue("VOLC RGB");

  BLECharacteristic *controlChar = service->createCharacteristic(
      VOLC_BLE_CONTROL_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_WRITE_NR);
  controlChar->setCallbacks(new ControlCallbacks());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(VOLC_BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void bleTransportLoop() {
  (void)deviceConnected;
}
