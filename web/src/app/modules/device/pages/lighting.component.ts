import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subject, Subscription } from 'rxjs';
import { debounceTime } from 'rxjs/operators';
import { DeviceService } from 'src/app/core/services/device/device.service';

type Rgb = [number, number, number];

export function rgbToHex(c: Rgb): string {
  return '#' + c.map((v) => Math.max(0, Math.min(255, v | 0)).toString(16).padStart(2, '0')).join('');
}

export function hexToRgb(hex: string): Rgb {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex || '');
  return m ? [parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)] : [0, 0, 0];
}

const EFFECT_ICONS = ['power_settings_new', 'touch_app', 'palette', 'waves', 'lens', 'grid_on',
  'air', 'blur_circular', 'gradient', 'auto_awesome', 'layers'];

/**
 * Lighting: pick an effect and tune it with live preview on the DeepDeck (led.set save:false),
 * and paint per-key colours for each layer (led.keys.*). Nothing is stored until Save.
 */
@Component({
  selector: 'app-lighting',
  templateUrl: './lighting.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss'],
})
export class LightingComponent implements OnInit, OnDestroy {
  effects: { id: number; name: string; uses: string[] }[] = [];
  led: any = null; // working copy of led.get
  stored: any = null; // what is in flash
  color1 = '#ff8800';
  color2 = '#ffffff';
  ledDirty = false;

  layers: any[] = [];
  layerUuid = '';
  keyNames: string[] = [];
  keys: Rgb[] = [];
  layerColor = '#ff8800';
  brush = '#00aaff';
  keysCustom = false;
  keysDirty = false;
  copyFrom = '';
  painting = false;

  message = '';
  error = '';
  private preview$ = new Subject<void>();
  private keysPreview$ = new Subject<void>();
  private subs: Subscription[] = [];

  constructor(public device: DeviceService) {}

  ngOnInit(): void {
    this.subs.push(this.preview$.pipe(debounceTime(120)).subscribe(() => this.sendPreview()));
    this.subs.push(this.keysPreview$.pipe(debounceTime(120)).subscribe(() => this.sendKeysPreview()));
    this.device.call('led.effects').subscribe({
      next: (r) => (this.effects = r.effects),
      error: () =>
        // older firmware or the web page: the 6 effects every firmware knows
        (this.effects = ['Off', 'Reactive', 'Colour cycle', 'Rainbow wave', 'Solid', 'Per-key'].map((name, id) => ({
          id, name, uses: id === 4 ? ['color1'] : id === 0 ? [] : ['speed'],
        }))),
    });
    this.loadLed();
    this.device.call('layers.list').subscribe((r) => {
      this.layers = r.data || [];
      if (this.layers.length) {
        this.selectLayer(this.layers[0].uuid);
      }
    });
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
    // leave the DeepDeck showing what is stored, not an unsaved preview
    if (this.ledDirty && this.stored) {
      this.device.call('led.set', { ...this.stored, save: false }).subscribe();
    }
  }

  icon(id: number): string {
    return EFFECT_ICONS[id] || 'lightbulb';
  }

  uses(what: string): boolean {
    const e = this.effects.find((x) => x.id === this.led?.effect);
    return !!e && e.uses.includes(what);
  }

  // ------------------------------------------------------------------------------------------
  // Effect and settings

  loadLed() {
    this.device.call('led.get').subscribe({
      next: (led) => {
        this.stored = { ...led };
        this.led = { ...led, effect: led.effect ?? led.mode };
        this.color1 = rgbToHex(led.color1 || led.rgb || [255, 136, 0]);
        this.color2 = rgbToHex(led.color2 || [255, 255, 255]);
        this.ledDirty = false;
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  setEffect(id: number) {
    this.led.effect = id;
    this.changed();
  }

  changed() {
    this.led.color1 = hexToRgb(this.color1);
    this.led.color2 = hexToRgb(this.color2);
    this.ledDirty = true;
    this.preview$.next();
  }

  private ledParams(save: boolean) {
    return {
      effect: this.led.effect,
      brightness: this.led.brightness,
      speed: this.led.speed,
      color1: this.led.color1,
      color2: this.led.color2,
      reactive: !!this.led.reactive,
      save,
    };
  }

  private sendPreview() {
    this.device.call('led.set', this.ledParams(false)).subscribe({ error: (err) => (this.error = String(err.message || err)) });
  }

  saveLed() {
    this.device.call('led.set', this.ledParams(true)).subscribe({
      next: () => {
        this.stored = { ...this.led };
        this.ledDirty = false;
        this.flash('Lighting saved on the DeepDeck');
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  revertLed() {
    this.device.call('led.set', { ...this.stored, save: false }).subscribe(() => this.loadLed());
  }

  // ------------------------------------------------------------------------------------------
  // Per-key colours of a layer

  selectLayer(uuid: string) {
    this.layerUuid = uuid;
    this.keysDirty = false;
    this.device.call('layers.get', { uuid }).subscribe((layer) => {
      const names: string[] = [];
      for (const row of ['row0', 'row1', 'row2', 'row3']) {
        (layer[row] || []).forEach((k: any) => names.push(k.name));
      }
      this.keyNames = names;
    });
    this.device.call('led.keys.get', { uuid }).subscribe({
      next: (r) => {
        this.keys = r.keys;
        this.layerColor = rgbToHex(r.layer);
        this.keysCustom = r.custom;
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  keyHex(i: number): string {
    return this.keys[i] ? rgbToHex(this.keys[i]) : '#000000';
  }

  paint(i: number) {
    this.keys[i] = hexToRgb(this.brush);
    this.keysChanged();
  }

  // drag across keys to paint several
  enter(i: number) {
    if (this.painting) {
      this.paint(i);
    }
  }

  fill() {
    this.keys = this.keys.map(() => hexToRgb(this.brush));
    this.keysChanged();
  }

  clear() {
    this.keys = this.keys.map(() => [0, 0, 0] as Rgb);
    this.keysChanged();
  }

  copyLayer() {
    if (!this.copyFrom) {
      return;
    }
    this.device.call('led.keys.get', { uuid: this.copyFrom }).subscribe((r) => {
      this.keys = r.keys;
      this.layerColor = rgbToHex(r.layer);
      this.keysChanged();
    });
  }

  keysChanged() {
    this.keysDirty = true;
    this.keysPreview$.next();
  }

  private sendKeysPreview() {
    this.device
      .call('led.keys.set', { uuid: this.layerUuid, keys: this.keys, layer: hexToRgb(this.layerColor), save: false })
      .subscribe({ error: (err) => (this.error = String(err.message || err)) });
  }

  showPerKey() {
    this.setEffect(5);
  }

  saveKeys() {
    this.device
      .call('led.keys.set', { uuid: this.layerUuid, keys: this.keys, layer: hexToRgb(this.layerColor) })
      .subscribe({
        next: () => {
          this.keysDirty = false;
          this.keysCustom = true;
          this.flash('Key colours saved for this layer');
        },
        error: (err) => (this.error = String(err.message || err)),
      });
  }

  resetKeys() {
    this.device.call('led.keys.reset', { uuid: this.layerUuid }).subscribe(() => {
      this.selectLayer(this.layerUuid);
      this.flash('Key colours back to the defaults');
    });
  }

  private flash(text: string) {
    this.message = text;
    this.error = '';
    setTimeout(() => (this.message = ''), 3000);
  }
}
