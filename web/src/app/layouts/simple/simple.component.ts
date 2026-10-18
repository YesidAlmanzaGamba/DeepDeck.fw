import { Component, HostListener, OnInit } from '@angular/core';
import { SidebarService } from 'src/app/shared/services/sidebar.service';
import { MenuItems } from '../../core/menu/menu-items/menu-items';

// below this window width the menu shows icons only, so the page keeps its room
const NARROW_PX = 1100;

@Component({
  selector: 'layout-simple',
  templateUrl: './simple.component.html',
  styleUrls: ['./simple.component.scss'],
})
export class SimpleComponent implements OnInit {
  stateBar = true;
  private wasNarrow: boolean | null = null;

  constructor(public menuItems: MenuItems, private sidebarService: SidebarService) {}

  ngOnInit() {
    this.sidebarService.sidebarStateObservable$.subscribe((newState: string) => {
      this.stateBar = newState === 'open';
    });
    this.onResize();
  }

  // collapse or expand only when the width crosses the limit, so the user's own choice sticks
  @HostListener('window:resize')
  onResize() {
    const narrow = window.innerWidth < NARROW_PX;
    if (narrow !== this.wasNarrow) {
      this.wasNarrow = narrow;
      this.sidebarService.set(narrow ? 'close' : 'open');
    }
  }

  toggleSideNav() {
    this.sidebarService.toggle();
  }
}
