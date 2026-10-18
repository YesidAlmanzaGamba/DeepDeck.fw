import { AfterViewChecked, Component, ElementRef, OnDestroy, OnInit, ViewChild } from '@angular/core';
import { Subscription } from 'rxjs';
import { DeviceService } from 'src/app/core/services/device/device.service';

const MAX_LINES = 2000;

/** The firmware's log, as printed on the USB cable (connect over USB to see it). */
@Component({
  selector: 'app-log',
  templateUrl: './log.component.html',
  styleUrls: ['./device-pages.scss', './lighting.component.scss'],
})
export class LogComponent implements OnInit, OnDestroy, AfterViewChecked {
  @ViewChild('box') box: ElementRef<HTMLDivElement>;
  lines: string[] = [];
  filter = '';
  paused = false;
  private sub: Subscription;
  private stick = true;

  constructor(public device: DeviceService) {}

  ngOnInit(): void {
    this.sub = this.device.log$.subscribe((line) => {
      if (this.paused) {
        return;
      }
      this.lines.push(line);
      if (this.lines.length > MAX_LINES) {
        this.lines.splice(0, this.lines.length - MAX_LINES);
      }
    });
  }

  ngOnDestroy(): void {
    this.sub?.unsubscribe();
  }

  ngAfterViewChecked(): void {
    if (this.stick && this.box) {
      this.box.nativeElement.scrollTop = this.box.nativeElement.scrollHeight;
    }
  }

  onScroll() {
    const el = this.box.nativeElement;
    this.stick = el.scrollTop + el.clientHeight >= el.scrollHeight - 20;
  }

  shown(): string[] {
    const f = this.filter.toLowerCase();
    return f ? this.lines.filter((l) => l.toLowerCase().includes(f)) : this.lines;
  }

  level(line: string): string {
    return line.startsWith('E ') ? 'e' : line.startsWith('W ') ? 'w' : '';
  }

  copy() {
    navigator.clipboard?.writeText(this.shown().join('\n'));
  }
}
