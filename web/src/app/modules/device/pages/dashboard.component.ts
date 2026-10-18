import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subscription, interval } from 'rxjs';
import { DeviceService } from 'src/app/core/services/device/device.service';

/** Device overview: identity, connection, WiFi, and the layers (click one to make it active). */
@Component({
  selector: 'app-dashboard',
  templateUrl: './dashboard.component.html',
  styleUrls: ['./device-pages.scss'],
})
export class DashboardComponent implements OnInit, OnDestroy {
  info: any = null;
  layers: any[] = [];
  activeUuid = '';
  error = '';
  private subs: Subscription[] = [];

  constructor(public device: DeviceService) {}

  ngOnInit(): void {
    this.load();
    this.subs.push(interval(5000).subscribe(() => this.loadInfo()));
    this.subs.push(
      this.device.on('layer.changed').subscribe((data) => {
        this.activeUuid = data.uuid;
        if (this.info) {
          this.info.layer = data;
        }
      })
    );
    this.subs.push(this.device.on('wifi.changed').subscribe(() => this.loadInfo()));
    this.subs.push(this.device.status$.subscribe((s) => s.connected && this.load()));
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
  }

  load() {
    this.loadInfo();
    this.device.call('layers.list').subscribe({
      next: (res) => (this.layers = res.data || []),
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  loadInfo() {
    this.device.call('device.info').subscribe({
      next: (info) => {
        this.info = info;
        this.activeUuid = info.layer?.uuid || '';
        this.error = '';
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  setActive(layer: any) {
    if (!this.device.isApp || !layer.active) {
      return;
    }
    this.device.call('layers.set_active', { uuid: layer.uuid }).subscribe({
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  uptime(seconds: number): string {
    if (seconds == null) {
      return '';
    }
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    return h ? `${h} h ${m} min` : `${m} min ${seconds % 60} s`;
  }
}
