import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class SidebarService {
  private sidebarState = 'open';
  private sidebarStateChanged$ = new BehaviorSubject<string>(this.sidebarState);
  public sidebarStateObservable$ = this.sidebarStateChanged$.asObservable();

  toggle() {
    this.set(this.sidebarState === 'open' ? 'close' : 'open');
  }

  set(state: 'open' | 'close') {
    this.sidebarState = state;
    this.sidebarStateChanged$.next(this.sidebarState);
  }
}
