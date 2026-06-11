import { useCallback, useEffect, useRef, useState } from "react";
import { BleManager, type Device } from "react-native-ble-plx";

import { VOLC_BLE_SERVICE } from "./volcBleConstants";
import { isVolcDevice } from "./volcBleClient";

type ScanState = {
  devices: Device[];
  scanning: boolean;
  error: string | null;
};

export function useVolcBleScan() {
  const managerRef = useRef(new BleManager());
  const [state, setState] = useState<ScanState>({
    devices: [],
    scanning: false,
    error: null,
  });

  const stopScan = useCallback(() => {
    managerRef.current.stopDeviceScan();
    setState((prev) => ({ ...prev, scanning: false }));
  }, []);

  const startScan = useCallback(() => {
    setState({ devices: [], scanning: true, error: null });

    managerRef.current.startDeviceScan(
      [VOLC_BLE_SERVICE],
      { allowDuplicates: false },
      (error, device) => {
        if (error) {
          setState((prev) => ({
            ...prev,
            scanning: false,
            error: error.message,
          }));
          return;
        }
        if (!device) return;

        const name = device.name ?? device.localName;
        if (!isVolcDevice(name)) return;

        setState((prev) => {
          if (prev.devices.some((d) => d.id === device.id)) {
            return prev;
          }
          return { ...prev, devices: [...prev.devices, device] };
        });
      },
    );
  }, []);

  useEffect(() => {
    const manager = managerRef.current;
    return () => {
      manager.stopDeviceScan();
      manager.destroy();
    };
  }, []);

  return { ...state, startScan, stopScan, manager: managerRef.current };
}
