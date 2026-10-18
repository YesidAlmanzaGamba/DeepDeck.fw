import { NgModule } from '@angular/core';
import { CommonModule } from '@angular/common';
import { SharedModule } from '../../shared/shared.module';
import { DeviceRoutingModule } from './device.routing';
import { ConnectComponent } from './pages/connect.component';
import { DashboardComponent } from './pages/dashboard.component';
import { LightingComponent } from './pages/lighting.component';
import { WifiComponent } from './pages/wifi.component';
import { ManageComponent } from './pages/manage.component';
import { LogComponent } from './pages/log.component';
import { ColorPickerModule } from 'ngx-color-picker';

/** Command Center pages: connect, dashboard, lighting, wifi, device, log */
@NgModule({
  imports: [CommonModule, SharedModule, DeviceRoutingModule, ColorPickerModule],
  declarations: [ConnectComponent, DashboardComponent, LightingComponent, WifiComponent, ManageComponent, LogComponent],
})
export class DeviceModule {}
