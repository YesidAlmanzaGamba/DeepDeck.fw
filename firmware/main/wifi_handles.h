#ifndef WIFI_HANDLES_H_
#define WIFI_HANDLES_H_

#include <stdbool.h>
#include "esp_err.h"

/*
 * WiFi station. The DeepDeck joins the network stored with wifi.set (NVS namespace "wifiCreds")
 * and keeps retrying while it is away. There is no setup access point any more: the Command
 * Center configures the DeepDeck over Bluetooth or USB (R4.5).
 *
 * The station is started even without credentials, so wifi.scan works.
 */

/** Start the station: once, from app_main. */
void wifi_start(void);

/** Store the credentials and join that network now, without a restart. */
esp_err_t wifi_set_credentials(const char *ssid, const char *pass);

/** Forget the stored network and disconnect. */
esp_err_t wifi_forget(void);

/** True once credentials are stored. */
bool wifi_has_credentials(void);

#endif
