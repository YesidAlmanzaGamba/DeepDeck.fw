import { NgModule } from '@angular/core';
import { Routes, RouterModule } from '@angular/router';
import { ConnectComponent } from './pages/connect.component';
import { DashboardComponent } from './pages/dashboard.component';
import { ConnectedGuard } from '../../core/guards/connected.guard';

export const routes: Routes = [
  { path: 'connect', component: ConnectComponent },
  { path: 'dashboard', component: DashboardComponent, canActivate: [ConnectedGuard] },
];

@NgModule({
  imports: [RouterModule.forChild(routes)],
  exports: [RouterModule],
})
export class DeviceRoutingModule {}
