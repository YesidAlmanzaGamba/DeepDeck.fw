import { Component, OnInit } from '@angular/core';
import { DeviceService } from 'src/app/core/services/device/device.service';

const BACKUP_FORMAT = 'deepdeck-backup';
const BACKUP_VERSION = 1;

/** Device: rename, restart, backup and restore of the whole configuration, factory reset. */
@Component({
  selector: 'app-manage',
  templateUrl: './manage.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss'],
})
export class ManageComponent implements OnInit {
  info: any = null;
  name = '';
  desktop: { close_to_tray: boolean; notify_link_lost: boolean; autostart: boolean } | null = null;
  screen: { screen_brightness: number; screen_off_minutes: number; leds_off_with_screen: boolean } | null = null;
  readonly offChoices = [0, 1, 2, 5, 10, 15, 30, 60, 120];
  private screenTimer: any;
  busy = '';
  message = '';
  error = '';

  constructor(public device: DeviceService) {}

  async loadDesktop() {
    if (this.device.isApp) {
      this.desktop = await (window as any).__TAURI__.core.invoke('desktop_get');
    }
  }

  async saveDesktop() {
    try {
      await (window as any).__TAURI__.core.invoke('desktop_set', { value: this.desktop });
      this.message = 'Saved';
      this.error = '';
    } catch (err) {
      this.error = String(err);
      this.loadDesktop();
    }
  }

  loadScreen() {
    this.device.call('device.settings.get').subscribe({
      next: (s) => (this.screen = s),
      error: () => (this.screen = null), // firmware older than 0.7.0
    });
  }

  /** Brightness previews while dragging; stored a moment after the last change */
  screenChanged() {
    clearTimeout(this.screenTimer);
    this.screenTimer = setTimeout(() => {
      const { screen_brightness, screen_off_minutes, leds_off_with_screen } = this.screen;
      this.device.call('device.settings.set', { screen_brightness, screen_off_minutes: +screen_off_minutes, leds_off_with_screen }).subscribe({
        error: (err) => (this.error = String(err.message || err)),
      });
    }, 300);
  }

  ngOnInit(): void {
    this.loadDesktop();
    this.loadScreen();
    this.device.call('device.info').subscribe({
      next: (info) => {
        this.info = info;
        this.name = info.name;
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  private call(method: string, params?: any): Promise<any> {
    return this.device.call(method, params).toPromise(); // rxjs 6: no firstValueFrom
  }

  private async run(label: string, job: () => Promise<string>) {
    this.busy = label;
    this.error = '';
    this.message = '';
    try {
      this.message = await job();
    } catch (err) {
      this.error = String((err as any)?.message || err);
    }
    this.busy = '';
  }

  rename() {
    this.run('rename', async () => {
      await this.call('device.set_name', { name: this.name });
      return 'Name stored. It is used after the next restart; the computer may show the new name only after pairing again.';
    });
  }

  restart() {
    if (!confirm('Restart the DeepDeck now?')) {
      return;
    }
    this.run('restart', async () => {
      await this.call('device.restart');
      return 'Restarting. The app reconnects by itself.';
    });
  }

  backup() {
    this.run('backup', async () => {
      const info = await this.call('device.info');
      const list = (await this.call('layers.list')).data;
      const layers = [];
      const layerColors: any = {};
      for (const l of list) {
        layers.push(await this.call('layers.get', { uuid: l.uuid }));
        try {
          const colors = await this.call('led.keys.get', { uuid: l.uuid });
          layerColors[l.uuid] = { keys: colors.keys, layer: colors.layer, custom: colors.custom };
        } catch {
          /* firmware without per-key colours */
        }
      }
      const backup = {
        format: BACKUP_FORMAT,
        version: BACKUP_VERSION,
        created: new Date().toISOString(),
        device: { name: info.name, fw: info.fw },
        active_layer: info.layer?.uuid,
        layers,
        macros: (await this.call('macros.list')).macros,
        led: await this.call('led.get'),
        layer_colors: layerColors,
      };
      const stamp = new Date().toISOString().slice(0, 10);
      const path = await this.device.saveFile(`deepdeck-${info.name}-${stamp}.json`, JSON.stringify(backup, null, 2));
      return path ? `Backup saved to ${path}` : 'Backup cancelled';
    });
  }

  restore() {
    this.run('restore', async () => {
      const text = await this.device.openFile();
      if (!text) {
        return 'Restore cancelled';
      }
      const backup = JSON.parse(text);
      if (backup.format !== BACKUP_FORMAT || !Array.isArray(backup.layers)) {
        throw new Error('this file is not a DeepDeck backup');
      }
      if (!confirm(`Replace the layers, macros and lighting of this DeepDeck with the backup from ${backup.created?.slice(0, 10)}?`)) {
        return 'Restore cancelled';
      }

      // layers: update the ones that exist, create the others, then delete those not in the backup
      const current = (await this.call('layers.list')).data;
      const keep = new Set(backup.layers.map((l: any) => l.uuid));
      for (const layer of backup.layers) {
        const exists = current.some((c: any) => c.uuid === layer.uuid);
        await this.call(exists ? 'layers.update' : 'layers.create', layer);
      }
      for (const c of current) {
        if (!keep.has(c.uuid)) {
          await this.call('layers.delete', { uuid: c.uuid });
        }
      }
      // macros are addressed by position: update the existing slots, add the missing ones
      const macros = (await this.call('macros.list')).macros;
      for (const m of backup.macros || []) {
        if (macros.some((x: any) => x.keycode === m.keycode)) {
          await this.call('macros.update', m);
        } else {
          await this.call('macros.create', { name: m.name, key: m.key });
        }
      }
      if (backup.led) {
        await this.call('led.set', { ...backup.led, save: true });
      }
      for (const [uuid, colors] of Object.entries<any>(backup.layer_colors || {})) {
        if (colors.custom) {
          await this.call('led.keys.set', { uuid, keys: colors.keys, layer: colors.layer });
        } else {
          await this.call('led.keys.reset', { uuid });
        }
      }
      if (backup.active_layer) {
        await this.call('layers.set_active', { uuid: backup.active_layer }).catch(() => {});
      }
      return `Restored ${backup.layers.length} layers and ${(backup.macros || []).length} macros.`;
    });
  }

  factoryReset() {
    if (!confirm('Restore the default layers and macros? Your layers, macros and key colours are replaced.')) {
      return;
    }
    this.run('reset', async () => {
      await this.call('layers.restore');
      return 'Default layers and macros restored.';
    });
  }
}
