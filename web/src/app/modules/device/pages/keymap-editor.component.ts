import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subscription } from 'rxjs';
import { DeviceService } from 'src/app/core/services/device/device.service';
import { GROUPS, KeyAction, MODIFIER_NAMES, STATIC_ACTIONS, describe, dynamicActions, isModTap, modTap } from 'src/app/core/services/keyboard/key-catalog';

type KnobAction = 'cw' | 'ccw' | 'single_press' | 'long_press' | 'double_press';
type Slot = { kind: 'key'; index: number } | { kind: 'knob'; side: 'left' | 'right'; action: KnobAction };

const KNOB_ACTIONS: { id: KnobAction; label: string }[] = [
  { id: 'ccw', label: 'Turn left' },
  { id: 'cw', label: 'Turn right' },
  { id: 'single_press', label: 'Press' },
  { id: 'double_press', label: 'Double press' },
  { id: 'long_press', label: 'Long press' },
];

function emptyLayer(name: string) {
  const row = () => Array.from({ length: 4 }, () => ({ name: '', key_code: 0 }));
  const knob = () => ({ cw: 0, ccw: 0, single_press: 0, long_press: 0, double_press: 0 });
  return { name, active: true, row0: row(), row1: row(), row2: row(), row3: row(), left_encoder_map: knob(), right_encoder_map: knob() };
}

/**
 * Keymap editor: the DeepDeck's 16 keys and 2 knobs for each layer. Pick a slot, then a key from the
 * list (or drag the key onto the slot). Nothing is stored until Save (layers.update).
 */
@Component({
  selector: 'app-keymap-editor',
  templateUrl: './keymap-editor.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss', './keymap-editor.component.scss'],
})
export class KeymapEditorComponent implements OnInit, OnDestroy {
  readonly groups = GROUPS;
  readonly knobActions = KNOB_ACTIONS;
  readonly keyIndexes = Array.from({ length: 16 }, (_, i) => i);

  layers: any[] = [];
  macros: any[] = [];
  sequences: any[] = [];
  tapdance: any[] = [];
  readonly modifierNames = MODIFIER_NAMES.slice(0, 4);
  activeUuid = '';
  uuid = '';
  layer: any = null;
  private saved = '';

  selected: Slot = { kind: 'key', index: 0 };
  group = 'All';
  search = '';
  actions: KeyAction[] = STATIC_ACTIONS;
  dragOver: string | null = null;

  busy = false;
  message = '';
  error = '';
  private subs: Subscription[] = [];

  constructor(public device: DeviceService) {}

  ngOnInit(): void {
    this.subs.push(this.device.on('layer.changed').subscribe((d) => (this.activeUuid = d.uuid)));
    this.device.call('device.info').subscribe((i) => (this.activeUuid = i.layer?.uuid || ''));
    this.device.call('macros.list').subscribe({
      next: (r) => {
        this.macros = r.macros || [];
        this.rebuildActions();
      },
      error: () => {},
    });
    // firmware 0.7.0 and later: sequences and tap-dance
    this.device.call('keys.sequences.list').subscribe({ next: (r) => ((this.sequences = r.sequences), this.rebuildActions()), error: () => {} });
    this.device.call('keys.tapdance.list').subscribe({ next: (r) => ((this.tapdance = r.tapdance), this.rebuildActions()), error: () => {} });
    this.loadLayers();
  }

  ngOnDestroy(): void {
    this.subs.forEach((s) => s.unsubscribe());
  }

  get dirty(): boolean {
    return !!this.layer && JSON.stringify(this.layer) !== this.saved;
  }

  private rebuildActions() {
    this.actions = [...STATIC_ACTIONS, ...dynamicActions(this.layers, this.macros, this.sequences, this.tapdance)];
  }

  loadLayers(select?: string) {
    this.device.call('layers.list').subscribe({
      next: (r) => {
        this.layers = r.data || [];
        this.rebuildActions();
        const target = select || this.uuid || this.layers[0]?.uuid;
        if (target) {
          this.open(target, true);
        }
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  open(uuid: string, force = false) {
    if (!force && this.dirty && !confirm('Discard the unsaved changes of this layer?')) {
      return;
    }
    this.device.call('layers.get', { uuid }).subscribe({
      next: (layer) => {
        layer.uuid = uuid;
        this.layer = layer;
        this.saved = JSON.stringify(layer);
        this.uuid = uuid;
        this.selected = { kind: 'key', index: 0 };
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  // ------------------------------------------------------------------------------------------
  // Slots

  key(index: number): { name: string; key_code: number } {
    return this.layer[`row${Math.floor(index / 4)}`][index % 4];
  }

  knob(side: 'left' | 'right'): any {
    return this.layer[`${side}_encoder_map`];
  }

  keySlot(index: number): Slot {
    return { kind: 'key', index };
  }

  knobSlot(side: string, action: string): Slot {
    return { kind: 'knob', side: side as 'left' | 'right', action: action as KnobAction };
  }

  selectedKey(): { name: string; key_code: number } | null {
    return this.selected.kind === 'key' ? this.key(this.selected.index) : null;
  }

  slotTitle(): string {
    const s = this.selected;
    if (s.kind === 'key') {
      return `Key ${s.index + 1} (row ${Math.floor(s.index / 4) + 1}, column ${(s.index % 4) + 1})`;
    }
    const what = KNOB_ACTIONS.find((a) => a.id === s.action)?.label || s.action;
    return `${s.side === 'left' ? 'Left' : 'Right'} knob · ${what}`;
  }

  isSelected(slot: Slot): boolean {
    const s = this.selected;
    if (s.kind === 'key' && slot.kind === 'key') {
      return s.index === slot.index;
    }
    return s.kind === 'knob' && slot.kind === 'knob' && s.side === slot.side && s.action === slot.action;
  }

  slotId(slot: Slot): string {
    return slot.kind === 'key' ? `k${slot.index}` : `${slot.side}-${slot.action}`;
  }

  selectedCode(): number {
    const s = this.selected;
    return s.kind === 'key' ? this.key(s.index).key_code : this.knob(s.side)[s.action];
  }

  describe(code: number): string {
    return describe(code, this.actions);
  }

  /** Mod-tap: a plain key that holds a modifier when kept down (-1 = no modifier) */
  holdModifier(): number {
    const k = this.selectedKey();
    return k && isModTap(k.key_code) ? (k.key_code >> 8) & 7 : -1;
  }

  canHold(): boolean {
    const k = this.selectedKey();
    if (!k) {
      return false;
    }
    const base = isModTap(k.key_code) ? k.key_code & 0xff : k.key_code;
    return base >= 4 && base < 0xe0; // plain keys, not modifiers or special codes
  }

  setHoldModifier(value: string) {
    const k = this.selectedKey();
    const m = Number(value);
    const base = isModTap(k.key_code) ? k.key_code & 0xff : k.key_code;
    k.key_code = m < 0 ? base : modTap(base, m);
  }

  short(code: number): string {
    if (isModTap(code)) {
      const key = this.actions.find((x) => x.code === (code & 0xff));
      return `${key ? key.label : code & 0xff}/${MODIFIER_NAMES[(code >> 8) & 7]}`;
    }
    const a = this.actions.find((x) => x.code === code);
    return code === 0 ? '—' : a ? a.label : String(code);
  }

  assign(action: KeyAction, slot: Slot = this.selected) {
    if (slot.kind === 'key') {
      const k = this.key(slot.index);
      const previous = this.actions.find((x) => x.code === k.key_code);
      // keep a label the user typed; replace one that was just the old key's default
      if (!k.name || (previous && k.name === previous.label)) {
        k.name = action.label.slice(0, 6);
      }
      k.key_code = action.code;
    } else {
      this.knob(slot.side)[slot.action] = action.code;
    }
    this.selected = slot;
  }

  clearSlot() {
    this.assign(STATIC_ACTIONS[0]);
    if (this.selected.kind === 'key') {
      this.key(this.selected.index).name = '';
    }
  }

  // HTML5 drag and drop from the key list onto a slot
  dragStart(event: DragEvent, action: KeyAction) {
    event.dataTransfer?.setData('text/plain', String(action.code));
  }

  drop(event: DragEvent, slot: Slot) {
    event.preventDefault();
    this.dragOver = null;
    const code = Number(event.dataTransfer?.getData('text/plain'));
    const action = this.actions.find((a) => a.code === code);
    if (action) {
      this.assign(action, slot);
    }
  }

  filtered(): KeyAction[] {
    const q = this.search.trim().toLowerCase();
    return this.actions.filter(
      (a) =>
        a.code !== 0 &&
        (this.group === 'All' || a.group === this.group) &&
        (!q || a.label.toLowerCase().includes(q) || a.name.toLowerCase().includes(q) || String(a.code) === q)
    );
  }

  // ------------------------------------------------------------------------------------------
  // Layers

  private run(work: () => Promise<string>) {
    this.busy = true;
    this.error = '';
    this.message = '';
    work()
      .then((m) => (this.message = m))
      .catch((err) => (this.error = String(err?.message || err)))
      .finally(() => (this.busy = false));
  }

  private call(method: string, params?: any): Promise<any> {
    return this.device.call(method, params).toPromise(); // rxjs 6
  }

  save() {
    const name = (this.layer.name || '').trim();
    if (!name) {
      this.error = 'The layer needs a name';
      return;
    }
    this.layer.name = name;
    this.run(async () => {
      await this.call('layers.update', this.layer);
      this.saved = JSON.stringify(this.layer);
      const entry = this.layers.find((l) => l.uuid === this.uuid);
      if (entry) {
        entry.name = this.layer.name;
        entry.active = this.layer.active;
      }
      this.rebuildActions();
      return `"${name}" saved on the DeepDeck`;
    });
  }

  revert() {
    this.layer = JSON.parse(this.saved);
  }

  showOnDevice() {
    this.device.call('layers.set_active', { uuid: this.uuid }).subscribe({ error: (err) => (this.error = String(err.message || err)) });
  }

  create() {
    if (this.dirty && !confirm('Discard the unsaved changes of this layer?')) {
      return;
    }
    const name = prompt('Name of the new layer (up to 14 characters)', 'New layer');
    if (!name || !name.trim()) {
      return;
    }
    this.run(async () => {
      const r = await this.call('layers.create', emptyLayer(name.trim().slice(0, 14)));
      this.loadLayers(r.uuid);
      return `Layer "${name.trim()}" created`;
    });
  }

  duplicate() {
    const copy = JSON.parse(JSON.stringify(this.layer));
    delete copy.uuid;
    copy.name = `${this.layer.name}`.slice(0, 11) + ' 2';
    this.run(async () => {
      const r = await this.call('layers.create', copy);
      this.loadLayers(r.uuid);
      return `Copied as "${copy.name}"`;
    });
  }

  remove() {
    if (!confirm(`Delete the layer "${this.layer.name}"? Its key colours are deleted too.`)) {
      return;
    }
    this.run(async () => {
      await this.call('layers.delete', { uuid: this.uuid });
      this.uuid = '';
      this.layer = null;
      this.loadLayers();
      return 'Layer deleted';
    });
  }

  restoreDefaults() {
    if (!confirm('Replace ALL layers and macros with the defaults?')) {
      return;
    }
    this.run(async () => {
      await this.call('layers.restore');
      this.uuid = '';
      this.layer = null;
      this.loadLayers();
      return 'Default layers and macros restored';
    });
  }
}
