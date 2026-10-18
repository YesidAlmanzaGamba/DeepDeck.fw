import { Component, OnDestroy, OnInit } from '@angular/core';
import { DeviceService, FirmwareFile } from 'src/app/core/services/device/device.service';

/** Compare "0.7.0" style versions: <0 if a is older than b */
function compareVersions(a: string, b: string): number {
  const pa = a.split(/[.-]/).map((x) => parseInt(x, 10) || 0);
  const pb = b.split(/[.-]/).map((x) => parseInt(x, 10) || 0);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    if ((pa[i] || 0) !== (pb[i] || 0)) {
      return (pa[i] || 0) - (pb[i] || 0);
    }
  }
  return 0;
}

/**
 * Firmware update (fw.* in docs/protocol.md): the image goes to the DeepDeck's other firmware slot
 * in chunks; the DeepDeck checks it, restarts into it, and rolls back if the new one fails to start.
 */
@Component({
  selector: 'app-update',
  templateUrl: './update.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss'],
})
export class UpdateComponent implements OnInit, OnDestroy {
  info: any = null;
  file: FirmwareFile | null = null;
  state: 'idle' | 'sending' | 'checking' | 'restarting' | 'done' = 'idle';
  progress = 0;
  rate = 0; // bytes per second
  message = '';
  error = '';
  private cancelled = false;

  constructor(public device: DeviceService) {}

  ngOnInit(): void {
    this.loadInfo();

  }

  ngOnDestroy(): void {
    if (this.state === 'sending') {
      this.cancelled = true;
    }
  }

  loadInfo() {
    this.device.call('fw.info').subscribe({
      next: (info) => (this.info = info),
      error: (err) => (this.error = `${err.message || err} (this firmware may be older than 0.7.0: update it over the cable once)`),
    });
  }

  get viaUsb(): boolean {
    return this.device.status$.value.kind === 'usb';
  }

  /** Rough duration from measured rates: about 8 KB/s over the cable, 2 KB/s over Bluetooth */
  estimate(size: number): string {
    const seconds = size / (this.viaUsb ? 8000 : 2000);
    return seconds > 90 ? `about ${Math.round(seconds / 60)} min` : `about ${Math.round(seconds)} s`;
  }

  fileNote(): string {
    if (!this.file || !this.info) {
      return '';
    }
    const c = compareVersions(this.file.version, this.info.version);
    return c > 0 ? 'newer than the installed one' : c === 0 ? 'the same version as installed' : 'OLDER than the installed one';
  }

  async choose() {
    this.error = '';
    this.message = '';
    try {
      this.file = await this.device.openFirmwareFile();
    } catch (err) {
      this.file = null;
      this.error = String(err);
    }
  }

  private call(method: string, params?: any): Promise<any> {
    return this.device.call(method, params).toPromise(); // rxjs 6
  }

  async start() {
    const file = this.file;
    if (!file) {
      return;
    }
    if (!confirm(`Install DeepDeck firmware ${file.version} (${this.estimate(file.size)})? Keep the DeepDeck connected until it restarts.`)) {
      return;
    }
    this.state = 'sending';
    this.error = '';
    this.message = '';
    this.cancelled = false;
    this.progress = 0;
    const image = Uint8Array.from(atob(file.data), (c) => c.charCodeAt(0));
    try {
      const begin = await this.call('fw.begin', { size: image.length, sha256: file.sha256 });
      const chunk: number = begin.chunk_max;
      const started = Date.now();
      // Over the cable 3 chunks in flight keep the 115200-baud line busy (8 KB/s instead of 5; the
      // DeepDeck takes them in order). Over Bluetooth the radio is the limit: one at a time.
      const window = this.viaUsb ? 3 : 1;
      const inFlight: Promise<any>[] = [];
      for (let offset = 0; offset < image.length; offset += chunk) {
        if (this.cancelled) {
          throw new Error('cancelled');
        }
        let bin = '';
        image.subarray(offset, offset + chunk).forEach((b) => (bin += String.fromCharCode(b)));
        inFlight.push(this.call('fw.chunk', { offset, data: btoa(bin) }));
        if (inFlight.length >= window) {
          await inFlight.shift();
        }
        this.progress = Math.min(100, Math.round(((offset + chunk) * 100) / image.length));
        this.rate = (offset + chunk) / Math.max(1, (Date.now() - started) / 1000);
      }
      await Promise.all(inFlight);
      this.state = 'checking';
      const end = await this.call('fw.end');
      this.state = 'restarting';
      this.message = `Firmware ${end.version} accepted. The DeepDeck is restarting into it…`;
      this.waitForRestart();
    } catch (err) {
      this.state = 'idle';
      this.error = `Update stopped: ${(err as any)?.message || err}. The installed firmware was not changed.`;
      this.call('fw.abort').catch(() => {});
    }
  }

  cancel() {
    this.cancelled = true;
  }

  /** Over BLE the app reconnects after the restart; over USB the port stays open. Either way, ask
   * fw.info until the restarted DeepDeck answers (rollback included: up to a minute). */
  private async waitForRestart() {
    const before = this.info?.running;
    await new Promise((r) => setTimeout(r, 5000));
    for (let i = 0; i < 30 && this.state === 'restarting'; i++) {
      try {
        const info = await this.call('fw.info');
        this.info = info;
        this.state = 'done';
        this.message =
          info.running !== before
            ? `Done: the DeepDeck runs firmware ${info.version} from ${info.running}.`
            : `The DeepDeck is back on ${info.running} (${info.version}): the new firmware did not start and was rolled back.`;
        return;
      } catch {
        await new Promise((r) => setTimeout(r, 2000));
      }
    }
    if (this.state === 'restarting') {
      this.state = 'idle';
      this.error = 'The DeepDeck did not answer after the restart: check the connection in Connect.';
    }
  }

  remaining(): string {
    if (!this.file || this.rate <= 0) {
      return '';
    }
    const seconds = (this.file.size * (1 - this.progress / 100)) / this.rate;
    return seconds > 90 ? `${Math.round(seconds / 60)} min left` : `${Math.round(seconds)} s left`;
  }
}
