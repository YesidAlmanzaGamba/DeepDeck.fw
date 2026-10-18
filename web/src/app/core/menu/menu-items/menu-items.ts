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
        state: 'network',
        name: 'SETTINGS',
        type: 'link',
        icon: 'settings',
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
                // pages marked appOnly need the Command Center (Bluetooth / USB link)
                return MENUITEMS.filter(item => !item.appOnly || !!(window as any).__TAURI__);                
            
        }        
    }
}
