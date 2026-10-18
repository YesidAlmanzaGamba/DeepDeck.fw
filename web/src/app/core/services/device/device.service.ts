import { Injectable, NgZone } from '@angular/core';
import { BehaviorSubject, Observable, ReplaySubject, Subject, from, throwError } from 'rxjs';
import { filter, map } from 'rxjs/operators';

/** Where a DeepDeck can be reached, as listed by the Command Center (src-tauri/src/link.rs). */
export interface DeviceEntry {
  kind: 'ble' | 'usb';
  id: string;
  name: string;
  detail: string;
  likely: boolean;
}

export interface LinkStatus {
  connected: boolean;
  kind?: 'ble' | 'usb';
  id?: string;
  name?: string;
  reason?: string;
}

export interface DeviceEvent {
  event: string;
  data: any;
}

/** A firmware image picked for an update (src-tauri/src/firmware.rs) */
export interface FirmwareFile {
  path: string;
  size: number;
  sha256: string;
  project: string;
  version: string;
  built: string;
  data: string; // base64
}

export class DeviceError extends Error {
  constructor(public code: string, message: string) {
    super(`${code}: ${message}`);
  }
}

const LAST_DEVICE_KEY = 'cc_last_device';
const REQUEST_TIMEOUT_MS = 10000;

/**
 * The one way the pages talk to the DeepDeck (protocol: docs/protocol.md).
 *
 * Requests go as JSON lines over BLE or USB through the Command Center (Tauri), and the device
 * sends events (layer.changed, wifi.changed). The DeepDeck no longer serves a web page (R4.5), so
 * in a plain browser (ng serve) nothing can be reached: pages show "not connected".
 */
@Injectable({ providedIn: 'root' })
export class DeviceService {
  readonly isApp = !!(window as any).__TAURI__;
  readonly status$ = new BehaviorSubject<LinkStatus>({ connected: false });
  readonly events$ = new Subject<DeviceEvent>();
  readonly log$ = new ReplaySubject<string>(500);

  private nextId = 1;
  private pending = new Map<number, { resolve: (v: any) => void; reject: (e: any) => void; timer: any }>();

  constructor(private zone: NgZone) {
    if (this.isApp) {
      this.listenToApp();
    } else {
      this.status$.next({ connected: false, reason: 'open this page in the DeepDeck Command Center app' });
    }
  }

  // ------------------------------------------------------------------------------------------
  // Requests

  call<T = any>(method: string, params?: any): Observable<T> {
    if (!this.isApp) {
      return throwError(new DeviceError('app_only', 'the DeepDeck is reached through the Command Center app')); // rxjs 6
    }
    return from(this.callApp<T>(method, params));
  }

  private async callApp<T>(method: string, params?: any): Promise<T> {
    if (!this.status$.value.connected) {
      throw new DeviceError('not_connected', 'connect a DeepDeck first');
    }
    const id = this.nextId++;
    const request: any = { id, method };
    if (params !== undefined) {
      request.params = params;
    }
    const result = new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new DeviceError('timeout', `${method} got no answer`));
      }, REQUEST_TIMEOUT_MS);
      this.pending.set(id, { resolve, reject, timer });
    });
    try {
      await this.invoke('send', { line: JSON.stringify(request) });
    } catch (err) {
      const p = this.pending.get(id);
      if (p) {
        clearTimeout(p.timer);
        this.pending.delete(id);
      }
      throw new DeviceError('link', String(err));
    }
    return result;
  }

  private onLine(line: string) {
    let msg: any;
    try {
      msg = JSON.parse(line);
    } catch {
      return;
    }
    if (msg.event) {
      this.events$.next({ event: msg.event, data: msg.data });
      return;
    }
    const p = this.pending.get(msg.id);
    if (!p) {
      return;
    }
    clearTimeout(p.timer);
    this.pending.delete(msg.id);
    if (msg.error) {
      p.reject(new DeviceError(msg.error.code, msg.error.message));
    } else {
      p.resolve(msg.result);
    }
  }

  // ------------------------------------------------------------------------------------------
  // Link (app only)

  listDevices(): Promise<DeviceEntry[]> {
    return this.invoke('list_devices');
  }

  async connect(device: { kind: string; id: string; name: string }): Promise<LinkStatus> {
    const status = await this.invoke<LinkStatus>('connect', { target: device });
    localStorage.setItem(LAST_DEVICE_KEY, JSON.stringify(device));
    return status;
  }

  async disconnect(): Promise<void> {
    localStorage.removeItem(LAST_DEVICE_KEY);
    await this.invoke('disconnect');
  }

  lastDevice(): { kind: string; id: string; name: string } | null {
    try {
      return JSON.parse(localStorage.getItem(LAST_DEVICE_KEY) || 'null');
    } catch {
      return null;
    }
  }

  saveFile(defaultName: string, contents: string): Promise<string | null> {
    return this.invoke('save_text_file', { defaultName, contents });
  }

  openFile(): Promise<string | null> {
    return this.invoke('open_text_file');
  }

  /** Pick a firmware .bin; the app checks it is a DeepDeck image and reads its version. */
  openFirmwareFile(): Promise<FirmwareFile | null> {
    return this.invoke('open_firmware_file');
  }

  private invoke<T = any>(cmd: string, args?: any): Promise<T> {
    return (window as any).__TAURI__.core.invoke(cmd, args);
  }

  private async listenToApp() {
    const { listen } = (window as any).__TAURI__.event;
    // Tauri events arrive outside Angular's zone
    await listen('cc-line', (e: any) => this.zone.run(() => this.onLine(e.payload.line)));
    await listen('cc-log', (e: any) => this.zone.run(() => this.log$.next(e.payload)));
    await listen('cc-status', (e: any) =>
      this.zone.run(() => {
        this.status$.next(e.payload);
        if (!e.payload.connected) {
          this.failPending('link lost');
        }
      })
    );
    this.status$.next(await this.invoke<LinkStatus>('link_status'));
    // reconnect to the device used last time
    const last = this.lastDevice();
    if (last && !this.status$.value.connected) {
      this.connect(last).catch(() => {
        /* the app keeps retrying in the background */
      });
    }
  }

  private failPending(reason: string) {
    this.pending.forEach((p) => {
      clearTimeout(p.timer);
      p.reject(new DeviceError('link', reason));
    });
    this.pending.clear();
  }

  /** Data of the events with this name, e.g. on('layer.changed'). */
  on(name: string): Observable<any> {
    return this.events$.pipe(
      filter((e) => e.event === name),
      map((e) => e.data)
    );
  }
}
