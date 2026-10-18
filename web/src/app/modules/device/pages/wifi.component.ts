import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subscription } from 'rxjs';
import { DeviceService } from 'src/app/core/services/device/device.service';

/** WiFi of the DeepDeck: status, scan, join (no restart), forget. */
@Component({
  selector: 'app-wifi',
  templateUrl: './wifi.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss'],
})
export class WifiComponent implements OnInit, OnDestroy {
  status: any = null;
  aps: any[] = [];
  scanning = false;
  ssid = '';
  pass = '';
  showPass = false;
  sending = false;
  message = '';
  error = '';
  private subs: Subscription[] = [];

  constructor(public device: DeviceService) {}

  ngOnInit(): void {
    this.loadStatus();
    this.subs.push(this.device.on('wifi.changed').subscribe(() => this.loadStatus()));
    this.scan();
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
  }

  loadStatus() {
    this.device.call('device.info').subscribe({
      next: (info) => (this.status = info.wifi),
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  scan() {
    this.scanning = true;
    this.error = '';
    this.device.call('wifi.scan').subscribe({
      next: (r) => {
        // strongest first, one entry per network name
        const seen = new Set<string>();
        this.aps = (r.aps || [])
          .sort((a: any, b: any) => b.rssi - a.rssi)
          .filter((ap: any) => ap.ssid && !seen.has(ap.ssid) && seen.add(ap.ssid));
        this.scanning = false;
      },
      error: (err) => {
        this.error = String(err.message || err);
        this.scanning = false;
      },
    });
  }

  bars(rssi: number): string {
    return rssi > -55 ? 'signal_wifi_4_bar' : rssi > -67 ? 'network_wifi_3_bar' : rssi > -78 ? 'network_wifi_2_bar' : 'network_wifi_1_bar';
  }

  pick(ap: any) {
    this.ssid = ap.ssid;
    this.pass = '';
  }

  forget() {
    if (!confirm('Forget the stored WiFi network?')) {
      return;
    }
    this.sending = true;
    this.device.call('wifi.forget').subscribe({
      next: () => {
        this.sending = false;
        this.message = 'Network forgotten';
        this.loadStatus();
      },
      error: (err) => {
        this.sending = false;
        this.error = String(err.message || err);
      },
    });
  }

  connect() {
    this.sending = true;
    this.error = '';
    this.device.call('wifi.set', { ssid: this.ssid, pass: this.pass }).subscribe({
      next: () => {
        this.sending = false;
        this.message = `Joining "${this.ssid}"…`;
        this.pass = '';
        setTimeout(() => this.loadStatus(), 4000);
      },
      error: (err) => {
        this.sending = false;
        this.error = String(err.message || err);
      },
    });
  }
}
