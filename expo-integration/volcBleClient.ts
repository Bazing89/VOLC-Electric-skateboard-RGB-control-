import type { Device } from "react-native-ble-plx";
import { Buffer } from "buffer";

import {
  VOLC_BLE_CONTROL_CHAR,
  VOLC_BLE_SERVICE,
  VOLC_MAX_LEDS,
  VolcLedCommand,
  VolcLedMode,
} from "./volcBleConstants";

export function isVolcDevice(name: string | null | undefined): boolean {
  return name?.startsWith("VOLC-") ?? false;
}

export class VolcBleClient {
  constructor(private readonly device: Device) {}

  async connect(): Promise<Device> {
    const connected = await this.device.connect();
    await connected.discoverAllServicesAndCharacteristics();
    return connected;
  }

  async disconnect(): Promise<void> {
    await this.device.cancelConnection();
  }

  async setMode(mode: VolcLedMode): Promise<void> {
    await this.writeControl(Buffer.from([VolcLedCommand.SetMode, mode]));
  }

  async setColor(r: number, g: number, b: number): Promise<void> {
    await this.writeControl(Buffer.from([VolcLedCommand.SetColor, r, g, b]));
  }

  async setPixel(index: number, r: number, g: number, b: number): Promise<void> {
    await this.writeControl(
      Buffer.from([VolcLedCommand.SetPixel, index, r, g, b]),
    );
  }

  async setBrightness(brightness: number): Promise<void> {
    const clamped = Math.max(0, Math.min(255, Math.round(brightness)));
    await this.writeControl(Buffer.from([VolcLedCommand.SetBrightness, clamped]));
  }

  /** Send strip length on connect before color/mode commands. */
  async setLedCount(count: number): Promise<void> {
    const clamped = Math.max(1, Math.min(VOLC_MAX_LEDS, Math.round(count)));
    await this.writeControl(Buffer.from([VolcLedCommand.SetLedCount, clamped]));
  }

  async turnOff(): Promise<void> {
    await this.setMode(VolcLedMode.Off);
  }

  async startRainbow(): Promise<void> {
    await this.setMode(VolcLedMode.Rainbow);
  }

  private async writeControl(payload: Buffer): Promise<void> {
    const base64 = payload.toString("base64");
    await this.device.writeCharacteristicWithResponseForService(
      VOLC_BLE_SERVICE,
      VOLC_BLE_CONTROL_CHAR,
      base64,
    );
  }
}
