import { Injectable } from '@angular/core';

export interface ChildrenItems {
    state: string;
    name: string;
    type?: string;
    label?: string;
}

export interface Menu {
    state: string;
    name: string;
    type: string;
    icon: string;
    label?: string;
    appOnly?: boolean;
    webOnly?: boolean;
    children?: ChildrenItems[];
}

const MENUITEMS: Menu[] = [
    {
        state: 'dashboard',
        name: 'DASHBOARD',
        type: 'link',
        icon: 'dashboard',
    },
    {
        state: 'keymap',
        name: 'KEYMAP',
        type: 'link',
        icon: 'keyboard',
    },    
    {
        state: 'macros',
        name: 'MACROS',
        type: 'link',
        icon: 'add_to_photos',
    },      
    {
        state: 'actions',
        name: 'TAP & SEQUENCES',
        type: 'link',
        icon: 'touch_app',
    },
    {
        state: 'commands',
        name: 'COMMANDS',
        type: 'link',
        icon: 'code',
        appOnly: true,
    },
    {
        state: 'apps',
        name: 'APPS',
        type: 'link',
        icon: 'apps',
        appOnly: true,
    },
    {
        state: 'lighting',
        name: 'LIGHTING',
        type: 'link',
        icon: 'lightbulb',
    },
    {
        state: 'wifi',
        name: 'WIFI',
        type: 'link',
        icon: 'wifi',
    },
    {
        state: 'device',
        name: 'DEVICE',
        type: 'link',
        icon: 'memory',
    },
    {
        state: 'update',
        name: 'UPDATE',
        type: 'link',
        icon: 'system_update',
    },
    {
        state: 'log',
        name: 'LOG',
        type: 'link',
        icon: 'description',
        appOnly: true,
    },
    {
        state: 'connect',
        name: 'CONNECT',
        type: 'link',
        icon: 'link',
        appOnly: true,
    },
    // {
    //     state: 'firmware',
    //     name: 'FIRMWARE',
    //     type: 'link',
    //     icon: 'wysiwyg',      
    // },

    
];


@Injectable()
export class MenuItems {
    get(type): Menu[] {
        switch(type){
            case 0:
                // appOnly pages need the Command Center (Bluetooth / USB link); webOnly pages are replaced in it
                const isApp = !!(window as any).__TAURI__;
                return MENUITEMS.filter(item => (isApp ? !item.webOnly : !item.appOnly));                
            
        }        
    }
}
