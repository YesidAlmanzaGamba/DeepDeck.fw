import { Injectable, NgZone } from '@angular/core';
import { HttpClient, HttpParams } from '@angular/common/http';
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
 * In the Command Center (Tauri) requests go as JSON lines over BLE or USB, and the device sends
 * events (layer.changed, wifi.changed). In a plain browser (the web page served by the DeepDeck)
 * the methods that have an /api endpoint are mapped onto HTTP, so that page keeps working until
 * the portal is removed.
 */
@Injectable({ providedIn: 'root' })
export class DeviceService {
  readonly isApp = !!(window as any).__TAURI__;
  readonly status$ = new BehaviorSubject<LinkStatus>({ connected: false });
  readonly events$ = new Subject<DeviceEvent>();
  readonly log$ = new ReplaySubject<string>(500);

  private nextId = 1;
  private pending = new Map<number, { resolve: (v: any) => void; reject: (e: any) => void; timer: any }>();

  constructor(private http: HttpClient, private zone: NgZone) {
    if (this.isApp) {
      this.listenToApp();
    } else {
      // the web page is served by the DeepDeck itself: it is "connected" by definition
      this.status$.next({ connected: true, kind: undefined, name: 'DeepDeck (WiFi)' });
    }
  }

  // ------------------------------------------------------------------------------------------
  // Requests

  call<T = any>(method: string, params?: any): Observable<T> {
    return this.isApp ? from(this.callApp<T>(method, params)) : this.callHttp<T>(method, params);
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

  // The web page's HTTP API (firmware/main/server.c), for the methods it has
  private callHttp<T>(method: string, params: any = {}): Observable<T> {
    const base = `http://${localStorage.getItem('ipAddress') || location.host}/api`;
    const uuid = new HttpParams().set('uuid', params.uuid || '');
    const body = JSON.stringify(params);
    switch (method) {
      case 'device.info':
        return this.http.get<T>(`${base}/config`);
      case 'layers.list':
        return this.http.get<T>(`${base}/layers/layer_names`);
      case 'layers.get':
        return this.http.get<T>(`${base}/layers`, { params: uuid });
      case 'layers.create':
        return this.http.post<T>(`${base}/layers`, body);
      case 'layers.update':
        return this.http.put<T>(`${base}/layers`, body);
      case 'layers.delete':
        return this.http.delete<T>(`${base}/layers`, { params: uuid });
      case 'layers.restore':
        return this.http.put<T>(`${base}/layers/restore`, null);
      case 'macros.list':
        return this.http.get<T>(`${base}/macros`);
      case 'macros.update':
        return this.http.put<T>(`${base}/macros`, body);
      case 'led.set':
        return this.http.post<T>(`${base}/led`, body);
      case 'wifi.set':
        return this.http.post<T>(`${base}/connect`, body);
      default:
        return throwError(() => new DeviceError('app_only', `${method} needs the DeepDeck Command Center app`));
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
