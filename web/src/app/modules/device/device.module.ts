import { NgModule } from '@angular/core';
import { CommonModule } from '@angular/common';
import { SharedModule } from '../../shared/shared.module';
import { DeviceRoutingModule } from './device.routing';
import { ConnectComponent } from './pages/connect.component';
import { DashboardComponent } from './pages/dashboard.component';

/** Command Center pages: connect, dashboard (lighting, wifi, device, log follow) */
@NgModule({
  imports: [CommonModule, SharedModule, DeviceRoutingModule],
  declarations: [ConnectComponent, DashboardComponent],
})
export class DeviceModule {}
