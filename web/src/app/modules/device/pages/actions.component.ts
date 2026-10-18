import { Component, OnInit } from '@angular/core';
import { DeviceService } from 'src/app/core/services/device/device.service';
import { KeyAction, SEQUENCE_BASE, STATIC_ACTIONS, TAPDANCE_BASE, dynamicActions } from 'src/app/core/services/keyboard/key-catalog';

type Step = { keys?: number[]; delay?: number; text?: string };

/**
 * Tap-dance and sequences (keys.tapdance.*, keys.sequences.* in docs/protocol.md). They are used
 * by putting their key (TD1.., or the sequence's name) on a key or knob in the Keymap page.
 */
@Component({
  selector: 'app-actions',
  templateUrl: './actions.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss', './keymap-editor.component.scss'],
})
export class ActionsComponent implements OnInit {
  readonly tapdanceBase = TAPDANCE_BASE;
  readonly sequenceBase = SEQUENCE_BASE;
  tapdance: any[] = [];
  sequences: any[] = [];
  actions: KeyAction[] = STATIC_ACTIONS;
  plainKeys: KeyAction[] = STATIC_ACTIONS.filter((a) => a.code >= 4 && a.code <= 0xe7);
  editing: any = null; // the sequence being edited (a copy)
  newText = '';
  newDelay = 200;
  newKeys: number[] = [];
  message = '';
  error = '';

  constructor(public device: DeviceService) {}

  ngOnInit(): void {
    this.load();
  }

  private load() {
    this.device.call('keys.tapdance.list').subscribe({
      next: (r) => (this.tapdance = r.tapdance),
      error: (err) => (this.error = `${err.message || err} (needs firmware 0.7.0 or later)`),
    });
    this.device.call('keys.sequences.list').subscribe({
      next: (r) => {
        this.sequences = r.sequences;
        this.rebuild();
      },
      error: () => {},
    });
    this.device.call('macros.list').subscribe({ next: (r) => this.rebuild(r.macros), error: () => {} });
  }

  private macros: any[] = [];
  private rebuild(macros?: any[]) {
    if (macros) {
      this.macros = macros;
    }
    // what a tap-dance can do: keys, media, layers, macros, sequences (not another tap-dance)
    this.actions = [...STATIC_ACTIONS, ...dynamicActions([], this.macros, this.sequences, [])];
  }

  groups(): string[] {
    return Array.from(new Set(this.actions.map((a) => a.group)));
  }

  inGroup(group: string): KeyAction[] {
    return this.actions.filter((a) => a.group === group && a.code !== 0);
  }

  private flash(text: string) {
    this.message = text;
    this.error = '';
    setTimeout(() => (this.message = ''), 3000);
  }

  // ------------------------------------------------------------------------------------------
  // Tap-dance

  saveTapdance(t: any) {
    this.device.call('keys.tapdance.set', { id: t.id, tap: +t.tap, double: +t.double, hold: +t.hold }).subscribe({
      next: () => this.flash(`Tap-dance ${t.id + 1} saved`),
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  // ------------------------------------------------------------------------------------------
  // Sequences

  used(): any[] {
    return this.sequences.filter((s) => s.name || s.steps.length);
  }

  edit(s: any) {
    this.editing = JSON.parse(JSON.stringify(s));
  }

  newSequence() {
    const free = this.sequences.find((s) => !s.name && !s.steps.length);
    if (!free) {
      this.error = 'All 32 sequences are in use';
      return;
    }
    this.editing = { ...free, name: `Sequence ${free.id + 1}`, steps: [] };
  }

  keyName(code: number): string {
    return this.actions.find((a) => a.code === code)?.label || String(code);
  }

  stepText(step: Step): string {
    if (step.text !== undefined) {
      return `type “${step.text}”`;
    }
    if (step.delay !== undefined) {
      return `wait ${step.delay} ms`;
    }
    return `press ${(step.keys || []).map((k) => this.keyName(k)).join(' + ')}`;
  }

  addText() {
    if (this.newText) {
      this.editing.steps.push({ text: this.newText });
      this.newText = '';
    }
  }

  addDelay() {
    this.editing.steps.push({ delay: Math.max(0, Math.min(60000, +this.newDelay || 0)) });
  }

  addKeys() {
    if (this.newKeys.length) {
      this.editing.steps.push({ keys: [...this.newKeys] });
      this.newKeys = [];
    }
  }

  toggleKey(code: number) {
    const i = this.newKeys.indexOf(code);
    if (i >= 0) {
      this.newKeys.splice(i, 1);
    } else if (this.newKeys.length < 6) {
      this.newKeys.push(code);
    }
  }

  move(i: number, delta: number) {
    const steps = this.editing.steps;
    const j = i + delta;
    if (j >= 0 && j < steps.length) {
      [steps[i], steps[j]] = [steps[j], steps[i]];
    }
  }

  removeStep(i: number) {
    this.editing.steps.splice(i, 1);
  }

  saveSequence() {
    const s = this.editing;
    this.device.call('keys.sequences.set', { id: s.id, name: s.name.slice(0, 15), steps: s.steps }).subscribe({
      next: (stored) => {
        this.sequences[s.id] = stored;
        this.sequences = [...this.sequences];
        this.rebuild();
        this.editing = null;
        this.flash(`"${stored.name}" saved: put it on a key or knob in the Keymap page (group Sequences)`);
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  deleteSequence(s: any) {
    if (!confirm(`Delete the sequence "${s.name}"? Keys that use it will do nothing.`)) {
      return;
    }
    this.device.call('keys.sequences.set', { id: s.id, name: '', steps: [] }).subscribe({
      next: (stored) => {
        this.sequences[s.id] = stored;
        this.sequences = [...this.sequences];
        this.rebuild();
        this.editing = null;
      },
      error: (err) => (this.error = String(err.message || err)),
    });
  }

  /** The DeepDeck types it into the window in front, so give the user time to click one */
  testSequence(s: any) {
    this.flash('Click into a text field: the DeepDeck types the sequence in 3 seconds');
    setTimeout(() => this.device.call('keys.sequences.run', { id: s.id }).subscribe(), 3000);
  }
}
