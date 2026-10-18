/**
 * Every action a key or knob can have, as the firmware understands it (firmware/main/key_definitions.h,
 * keyboard_config.h). `label` is what the OLED shows by default (at most 6 characters).
 */
export interface KeyAction {
  code: number;
  label: string;
  name: string;
  group: string;
}

export const LAYER_DOWN = 401;
export const LAYER_UP = 402;
export const LAYER_HOLD_BASE = 291; // hold layer N = 291 + N, up to 308
export const MACRO_BASE = 500;

function range(group: string, first: number, labels: string[], names?: string[]): KeyAction[] {
  return labels.map((label, i) => ({ code: first + i, label, name: names ? names[i] : label, group }));
}

const LETTERS = range('Letters', 4, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split(''));
const NUMBERS = range('Numbers', 30, '1234567890'.split(''));

const EDITING: KeyAction[] = [
  { code: 40, label: 'Enter', name: 'Enter', group: 'Editing' },
  { code: 41, label: 'Esc', name: 'Escape', group: 'Editing' },
  { code: 42, label: 'Bksp', name: 'Backspace', group: 'Editing' },
  { code: 43, label: 'Tab', name: 'Tab', group: 'Editing' },
  { code: 44, label: 'Space', name: 'Space', group: 'Editing' },
  { code: 57, label: 'Caps', name: 'Caps Lock', group: 'Editing' },
  { code: 73, label: 'Ins', name: 'Insert', group: 'Editing' },
  { code: 76, label: 'Del', name: 'Delete', group: 'Editing' },
  { code: 101, label: 'Menu', name: 'Application / menu key', group: 'Editing' },
];

const SYMBOLS: KeyAction[] = [
  [45, '-', 'Minus'], [46, '=', 'Equal'], [47, '[', 'Left bracket'], [48, ']', 'Right bracket'],
  [49, '\\', 'Backslash'], [51, ';', 'Semicolon'], [52, "'", 'Quote'], [53, '`', 'Grave'],
  [54, ',', 'Comma'], [55, '.', 'Dot'], [56, '/', 'Slash'],
].map(([code, label, name]) => ({ code: code as number, label: label as string, name: name as string, group: 'Symbols' }));

const NAVIGATION: KeyAction[] = [
  { code: 82, label: 'Up', name: 'Arrow up', group: 'Navigation' },
  { code: 81, label: 'Down', name: 'Arrow down', group: 'Navigation' },
  { code: 80, label: 'Left', name: 'Arrow left', group: 'Navigation' },
  { code: 79, label: 'Right', name: 'Arrow right', group: 'Navigation' },
  { code: 74, label: 'Home', name: 'Home', group: 'Navigation' },
  { code: 77, label: 'End', name: 'End', group: 'Navigation' },
  { code: 75, label: 'PgUp', name: 'Page up', group: 'Navigation' },
  { code: 78, label: 'PgDn', name: 'Page down', group: 'Navigation' },
  { code: 70, label: 'PrtSc', name: 'Print screen', group: 'Navigation' },
  { code: 71, label: 'ScrLk', name: 'Scroll lock', group: 'Navigation' },
  { code: 72, label: 'Pause', name: 'Pause', group: 'Navigation' },
];

const FUNCTION_KEYS = [
  ...range('Function', 58, Array.from({ length: 12 }, (_, i) => `F${i + 1}`)),
  ...range('Function', 104, Array.from({ length: 12 }, (_, i) => `F${i + 13}`)),
];

const NUMPAD: KeyAction[] = [
  ...range('Numpad', 89, '123456789'.split('').map((d) => `KP${d}`), '123456789'.split('').map((d) => `Keypad ${d}`)),
  { code: 98, label: 'KP0', name: 'Keypad 0', group: 'Numpad' },
  { code: 99, label: 'KP.', name: 'Keypad .', group: 'Numpad' },
  { code: 84, label: 'KP/', name: 'Keypad /', group: 'Numpad' },
  { code: 85, label: 'KP*', name: 'Keypad *', group: 'Numpad' },
  { code: 86, label: 'KP-', name: 'Keypad -', group: 'Numpad' },
  { code: 87, label: 'KP+', name: 'Keypad +', group: 'Numpad' },
  { code: 88, label: 'KPEnt', name: 'Keypad Enter', group: 'Numpad' },
  { code: 83, label: 'NumLk', name: 'Num lock', group: 'Numpad' },
];

const MODIFIERS = range('Modifiers', 224, ['LCtrl', 'LShift', 'LAlt', 'LWin', 'RCtrl', 'RShift', 'RAlt', 'RWin'],
  ['Left Ctrl', 'Left Shift', 'Left Alt', 'Left Win / Cmd', 'Right Ctrl', 'Right Shift', 'Right Alt', 'Right Win / Cmd']);

// Only these media keys are sent by the firmware (keypress_handles.c, rotary encoder)
const MEDIA = range('Media', 168, ['Next', 'Prev', 'Stop', 'Play', 'Mute', 'Vol+', 'Vol-'],
  ['Next track', 'Previous track', 'Stop', 'Play / pause', 'Mute', 'Volume up', 'Volume down']);

const NONE: KeyAction = { code: 0, label: '', name: 'Nothing', group: 'Special' };

export const STATIC_ACTIONS: KeyAction[] = [
  NONE, ...LETTERS, ...NUMBERS, ...EDITING, ...SYMBOLS, ...NAVIGATION, ...FUNCTION_KEYS, ...NUMPAD, ...MODIFIERS, ...MEDIA,
];

export const GROUPS = ['All', 'Letters', 'Numbers', 'Editing', 'Symbols', 'Navigation', 'Function', 'Numpad', 'Modifiers', 'Media', 'Layers', 'Macros'];

/** Layer keys and macros depend on the device: build them from layers.list and macros.list */
export function dynamicActions(layers: { name: string; pos: number }[], macros: { name: string; keycode: number; key: number[] }[]): KeyAction[] {
  const out: KeyAction[] = [
    { code: LAYER_UP, label: 'Layer>', name: 'Next layer', group: 'Layers' },
    { code: LAYER_DOWN, label: '<Layer', name: 'Previous layer', group: 'Layers' },
  ];
  for (const l of layers) {
    if (l.pos <= 17) {
      out.push({ code: LAYER_HOLD_BASE + l.pos, label: `H:${l.name}`.slice(0, 6), name: `Hold for layer "${l.name}"`, group: 'Layers' });
    }
  }
  for (const m of macros) {
    if (m.name && m.key && m.key.some((k) => k !== 0)) {
      out.push({ code: m.keycode, label: m.name.slice(0, 6), name: `Macro "${m.name}"`, group: 'Macros' });
    }
  }
  return out;
}

export function describe(code: number, all: KeyAction[]): string {
  const a = all.find((x) => x.code === code);
  if (a) {
    return a.name;
  }
  if (code >= MACRO_BASE && code <= 600) {
    return `Macro ${code - MACRO_BASE} (empty)`;
  }
  if (code >= LAYER_HOLD_BASE && code <= 308) {
    return `Hold for layer ${code - LAYER_HOLD_BASE + 1}`;
  }
  return `Code ${code}`;
}
