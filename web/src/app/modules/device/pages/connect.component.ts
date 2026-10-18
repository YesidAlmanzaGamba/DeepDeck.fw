import { Component, OnDestroy, OnInit } from '@angular/core';
import { ActivatedRoute, Router } from '@angular/router';
import { Subscription } from 'rxjs';
import { DeviceEntry, DeviceService, LinkStatus } from 'src/app/core/services/device/device.service';

/** Pick the DeepDeck to configure: a paired Bluetooth device or a USB port. */
@Component({
  selector: 'app-connect',
  templateUrl: './connect.component.html',
  styleUrls: ['./device-pages.scss'],
})
export class ConnectComponent implements OnInit, OnDestroy {
  devices: DeviceEntry[] = [];
  status: LinkStatus = { connected: false };
  loading = false;
  connecting: string | null = null;
  error = '';
  private sub: Subscription;

  constructor(public device: DeviceService, private router: Router, private route: ActivatedRoute) {}

  ngOnInit(): void {
    this.sub = this.device.status$.subscribe((status) => {
      const justConnected = status.connected && !this.status.connected;
      this.status = status;
      // sent here because nothing was connected: go on once a DeepDeck is
      if (justConnected && this.route.snapshot.queryParamMap.get('next')) {
        this.router.navigateByUrl(this.route.snapshot.queryParamMap.get('next'));
      }
    });
    this.refresh();
  }

  ngOnDestroy(): void {
    this.sub?.unsubscribe();
  }

  async refresh() {
    if (!this.device.isApp) {
      return;
    }
    this.loading = true;
    this.error = '';
    try {
      this.devices = await this.device.listDevices();
    } catch (err) {
      this.error = String(err);
    }
    this.loading = false;
  }

  async connect(d: DeviceEntry) {
    this.connecting = d.kind + d.id;
    this.error = '';
    try {
      await this.device.connect({ kind: d.kind, id: d.id, name: d.name });
    } catch (err) {
      this.error = `${d.name}: ${err}. The app keeps trying in the background.`;
    }
    this.connecting = null;
  }

  async disconnect() {
    await this.device.disconnect();
  }

  isCurrent(d: DeviceEntry): boolean {
    return this.status.connected && this.status.kind === d.kind && this.status.id === d.id;
  }
}
