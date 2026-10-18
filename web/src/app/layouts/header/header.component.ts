import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subscription } from 'rxjs';
import { DeviceService, LinkStatus } from 'src/app/core/services/device/device.service';
import { SidebarService } from 'src/app/shared/services/sidebar.service';

/** Top bar: menu toggle, logo, and the link to the DeepDeck (click to go to Connect). */
@Component({
  selector: 'app-header',
  templateUrl: './header.component.html',
  styleUrls: ['./header.component.scss'],
})
export class HeaderComponent implements OnInit, OnDestroy {
  status: LinkStatus = { connected: false };
  private sub: Subscription;

  constructor(private device: DeviceService, private sidebarService: SidebarService) {}

  ngOnInit(): void {
    this.sub = this.device.status$.subscribe((s) => (this.status = s));
  }

  ngOnDestroy(): void {
    this.sub?.unsubscribe();
  }

  toggleSideNav() {
    this.sidebarService.toggle();
  }
}
