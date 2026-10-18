import { NgModule } from '@angular/core';
import { Routes, RouterModule } from '@angular/router';
import { AuthGuard } from './core/guards/auth.guard';
import { ConnectedGuard } from './core/guards/connected.guard';
import { SimpleComponent } from '../app/layouts/simple/simple.component'

export const SITE_ROUTES: Routes = [
  {
    path: '',
    redirectTo: 'dashboard',
    pathMatch: 'full',
  },
  // {
  //    path: 'session', loadChildren: () =>
  //      import('./modules/auth/auth.module').then(m => m.AuthModule)
  // },  
  {
    path: '',
    component: SimpleComponent,
    // canActivate: [AuthGuard],
    runGuardsAndResolvers: 'always',
    children: [     
      {
        // Command Center pages: connect, dashboard (and lighting, wifi, device, log)
        path: '', loadChildren: () =>
          import('./modules/device/device.module').then(m => m.DeviceModule)
      },
      {
        path: 'macros', canActivate: [ConnectedGuard], loadChildren: () =>
          import('./modules/macros/macros.module').then(m => m.MacrosModule)
      },
      // {
      //   path: 'firmware', loadChildren: () =>
      //     import('./modules/firmware/firmware.module').then(m => m.FirmwareModule)
      // }
    ],   
  },{
    path: '**',
    redirectTo: 'dashboard'
  }
];

@NgModule({
  imports: [RouterModule.forRoot(SITE_ROUTES, { useHash: true })],
  exports: [RouterModule]
})
export class AppRoutingModule {
}
