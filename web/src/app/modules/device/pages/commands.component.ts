import { Component, OnDestroy, OnInit } from '@angular/core';
import { DeviceService } from 'src/app/core/services/device/device.service';
import { APPKEY_BASE } from 'src/app/core/services/keyboard/key-catalog';

interface Cmd {
  id: number;
  name: string;
  command: string;
}

/** Ready-made commands for herdr (https://herdr.dev/docs/cli-reference/) */
const PRESETS: { group: string; items: { name: string; command: string }[] }[] = [
  {
    group: 'herdr: panes',
    items: [
      { name: 'Pane left', command: 'herdr pane focus --direction left' },
      { name: 'Pane right', command: 'herdr pane focus --direction right' },
      { name: 'Pane up', command: 'herdr pane focus --direction up' },
      { name: 'Pane down', command: 'herdr pane focus --direction down' },
      { name: 'Split right', command: 'herdr pane split --current --direction right' },
      { name: 'Split down', command: 'herdr pane split --current --direction down' },
    ],
  },
  {
    group: 'herdr: tabs',
    items: [{ name: 'New tab', command: 'herdr tab create' }],
  },
];

/**
 * Computer commands (src-tauri/src/app_commands.rs): a DeepDeck key with "Command N" makes the
 * Command Center run command N on this computer, whatever window is in front.
 */
@Component({
  selector: 'app-commands',
  templateUrl: './commands.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss'],
})
export class CommandsComponent implements OnInit, OnDestroy {
  readonly presets = PRESETS;
  readonly base = APPKEY_BASE;
  commands: Cmd[] = [];
  recent: any[] = [];
  dirty = false;
  message = '';
  error = '';
  private unlisten: (() => void) | null = null;

  constructor(public device: DeviceService) {}

  async ngOnInit() {
    if (!this.device.isApp) {
      return;
    }
    await this.load();
    this.unlisten = await (window as any).__TAURI__.event.listen('cc-app-command', (e: any) => {
      this.recent = [e.payload, ...this.recent].slice(0, 20);
    });
  }

  ngOnDestroy(): void {
    this.unlisten?.();
  }

  private invoke(cmd: string, args?: any): Promise<any> {
    return (window as any).__TAURI__.core.invoke(cmd, args);
  }

  private async load() {
    const st = await this.invoke('app_commands_get');
    const byId = new Map<number, Cmd>(st.commands.map((c: Cmd) => [c.id, c]));
    this.commands = Array.from({ length: 32 }, (_, id) => byId.get(id) || { id, name: '', command: '' });
    this.recent = st.recent;
    this.dirty = false;
  }

  used(): Cmd[] {
    return this.commands.filter((c) => c.name || c.command);
  }

  addPreset(p: { name: string; command: string }) {
    const free = this.commands.find((c) => !c.name && !c.command);
    if (!free) {
      this.error = 'All 32 commands are in use';
      return;
    }
    free.name = p.name;
    free.command = p.command;
    this.changed();
  }

  addEmpty() {
    this.addPreset({ name: 'New command', command: '' });
  }

  clear(c: Cmd) {
    c.name = '';
    c.command = '';
    this.changed();
  }

  changed() {
    this.dirty = true;
    this.error = '';
  }

  async save() {
    try {
      await this.invoke('app_commands_set', { value: this.used().map((c) => ({ ...c, name: c.name.slice(0, 24) })) });
      this.dirty = false;
      this.message = 'Saved. Put "Command N" on a key or knob in the Keymap page (group Commands).';
      setTimeout(() => (this.message = ''), 4000);
    } catch (err) {
      this.error = String(err);
    }
  }

  async test(c: Cmd) {
    if (this.dirty) {
      await this.save();
    }
    await this.invoke('app_commands_run', { id: c.id });
  }

  when(t: number): string {
    return new Date(t * 1000).toLocaleTimeString();
  }
}
