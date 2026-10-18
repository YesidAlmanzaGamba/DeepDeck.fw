import { Component, OnDestroy, OnInit } from '@angular/core';
import { DeviceService } from 'src/app/core/services/device/device.service';

interface Rule {
  exe: string;
  uuid: string;
  layer_name: string;
}

/**
 * Per-application layers (src-tauri/src/app_layers.rs): when a program comes to the front, the app
 * switches the DeepDeck to its layer. Works while the window is hidden in the tray.
 */
@Component({
  selector: 'app-app-layers',
  templateUrl: './app-layers.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss'],
})
export class AppLayersComponent implements OnInit, OnDestroy {
  enabled = false;
  rules: Rule[] = [];
  fallback = '';
  recent: string[] = [];
  current = '';
  layers: any[] = [];
  lastSwitch = '';
  newExe = '';
  dirty = false;
  message = '';
  error = '';
  private timer: any;
  private unlisten: (() => void) | null = null;

  constructor(public device: DeviceService) {}

  async ngOnInit() {
    this.device.call('layers.list').subscribe({
      next: (r) => (this.layers = r.data || []),
      error: (err) => (this.error = String(err.message || err)),
    });
    await this.load(true);
    // the list of recent programs grows while the user works in them
    this.timer = setInterval(() => this.load(false), 2000);
    if (this.device.isApp) {
      this.unlisten = await (window as any).__TAURI__.event.listen('cc-app-layer', (e: any) => {
        const layer = this.layers.find((l) => l.uuid === e.payload.uuid);
        this.lastSwitch = `${e.payload.exe} → ${layer ? layer.name : e.payload.uuid}`;
      });
    }
  }

  ngOnDestroy(): void {
    clearInterval(this.timer);
    this.unlisten?.();
  }

  private async load(all: boolean) {
    if (!this.device.isApp) {
      return;
    }
    const st = await (window as any).__TAURI__.core.invoke('app_layers_get');
    this.recent = st.recent;
    this.current = st.current;
    if (all || !this.dirty) {
      this.enabled = st.enabled;
      this.rules = st.rules;
      this.fallback = st.fallback || '';
    }
  }

  layerName(uuid: string): string {
    return this.layers.find((l) => l.uuid === uuid)?.name || '(deleted layer)';
  }

  suggestions(): string[] {
    return this.recent.filter((exe) => !this.rules.some((r) => r.exe === exe));
  }

  add(exe: string) {
    exe = exe.trim().toLowerCase();
    if (!exe) {
      return;
    }
    if (!exe.includes('.')) {
      exe += '.exe';
    }
    if (this.rules.some((r) => r.exe === exe)) {
      this.error = `${exe} already has a layer`;
      return;
    }
    const layer = this.layers[0];
    this.rules.push({ exe, uuid: layer?.uuid || '', layer_name: layer?.name || '' });
    this.newExe = '';
    this.changed();
  }

  remove(rule: Rule) {
    this.rules = this.rules.filter((r) => r !== rule);
    this.changed();
  }

  changed() {
    this.dirty = true;
    this.error = '';
  }

  async save() {
    for (const r of this.rules) {
      r.layer_name = this.layerName(r.uuid);
    }
    try {
      await (window as any).__TAURI__.core.invoke('app_layers_set', {
        value: { enabled: this.enabled, rules: this.rules, fallback: this.fallback || null },
      });
      this.dirty = false;
      this.message = 'Saved. The layer changes when you switch programs.';
      setTimeout(() => (this.message = ''), 3000);
    } catch (err) {
      this.error = String(err);
    }
  }
}
