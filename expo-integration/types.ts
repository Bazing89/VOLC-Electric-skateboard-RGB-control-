/** App-side state — device does not push live LED state; update after each write. */
export type Rgb = { r: number; g: number; b: number };

export type VolcLedState = {
  deviceId: string;
  displayName: string;
  macSuffix: string | null;
  deviceLabel: string | null;
  rssi: number | null;
  connected: boolean;
  ledCount: number;
  mode: 0 | 1 | 2;
  brightness: number;
  solidColor: Rgb;
  pixels: Rgb[];
};

export type VolcScanEntry = {
  id: string;
  displayName: string;
  macSuffix: string | null;
  rssi: number | null;
  name: string | null;
  localName: string | null;
};
