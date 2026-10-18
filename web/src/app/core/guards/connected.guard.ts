import { Injectable } from '@angular/core';
import { CanActivate, Router, RouterStateSnapshot, ActivatedRouteSnapshot, UrlTree } from '@angular/router';
import { DeviceService } from '../services/device/device.service';

/** Pages that talk to the DeepDeck need a connection: go to Connect first. */
@Injectable({ providedIn: 'root' })
export class ConnectedGuard implements CanActivate {
  constructor(private device: DeviceService, private router: Router) {}

  canActivate(route: ActivatedRouteSnapshot, state: RouterStateSnapshot): boolean | UrlTree {
    if (this.device.status$.value.connected) {
      return true;
    }
    return this.router.createUrlTree(['/connect'], { queryParams: { next: state.url } });
  }
}
