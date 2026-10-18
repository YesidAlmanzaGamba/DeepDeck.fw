/**
 * @file serial_transport.h
 * @brief Command Center protocol over the USB cable (UART0, the same port as the log console).
 *
 * Protocol lines are prefixed with SERIAL_TRANSPORT_PREFIX in both directions, so the app can tell
 * them apart from log output, and the device ignores whatever else is typed on the console.
 * Each outgoing line is written with a single uart_write_bytes() call, so log lines cannot split it.
 *
 * The app must open the port with DTR and RTS de-asserted: on this board they drive the ESP32
 * EN/IO0 auto-reset circuit, and asserting them reboots the device.
 */

#ifndef SERIAL_TRANSPORT_H_
#define SERIAL_TRANSPORT_H_

#include <stddef.h>
#include "esp_err.h"

#define SERIAL_TRANSPORT_PREFIX "@DD "
#define SERIAL_TRANSPORT_MAX_LINE 4096

/// Called with each received protocol line, without prefix and line ending
typedef void (*serial_transport_line_cb_t)(const char *line, size_t len);

/** @brief Install the UART0 driver and start reading protocol lines. */
esp_err_t serial_transport_start(serial_transport_line_cb_t handler);

/** @brief Send one protocol line (prefix and '\n' are added). */
esp_err_t serial_transport_send_line(const char *line, size_t len);

#endif /* SERIAL_TRANSPORT_H_ */
