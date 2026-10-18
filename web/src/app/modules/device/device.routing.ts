import { NgModule } from '@angular/core';
import { Routes, RouterModule } from '@angular/router';
import { ConnectComponent } from './pages/connect.component';
import { DashboardComponent } from './pages/dashboard.component';
import { LightingComponent } from './pages/lighting.component';
import { WifiComponent } from './pages/wifi.component';
import { ManageComponent } from './pages/manage.component';
import { LogComponent } from './pages/log.component';
import { UpdateComponent } from './pages/update.component';
import { KeymapEditorComponent } from './pages/keymap-editor.component';
import { ConnectedGuard } from '../../core/guards/connected.guard';

export const routes: Routes = [
  { path: 'connect', component: ConnectComponent },
  { path: 'dashboard', component: DashboardComponent, canActivate: [ConnectedGuard] },
  { path: 'lighting', component: LightingComponent, canActivate: [ConnectedGuard] },
  { path: 'wifi', component: WifiComponent, canActivate: [ConnectedGuard] },
  { path: 'device', component: ManageComponent, canActivate: [ConnectedGuard] },
  { path: 'log', component: LogComponent },
  { path: 'update', component: UpdateComponent, canActivate: [ConnectedGuard] },
  { path: 'keymap', component: KeymapEditorComponent, canActivate: [ConnectedGuard] },
];

@NgModule({
  imports: [RouterModule.forChild(routes)],
  exports: [RouterModule],
})
export class DeviceRoutingModule {}
