/**
 * @file serial_transport.c
 * @brief Command Center protocol over UART0, see serial_transport.h
 */

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "serial_transport.h"

#define TAG "serial_tp"
#define SERIAL_UART CONFIG_ESP_CONSOLE_UART_NUM
// 8 KB = 0.7 s at 115200 baud: the reader task cannot run while flash is erased (layer saves,
// firmware updates with chunks in flight), the IRAM interrupt keeps filling this buffer meanwhile
#define UART_RX_BUF 8192
#define UART_TX_BUF 2048
#define READ_CHUNK 128

static serial_transport_line_cb_t line_handler = NULL;

static void handle_line(char *line, size_t len)
{
	// tolerate CRLF from terminals
	if (len > 0 && line[len - 1] == '\r')
		line[--len] = '\0';

	const size_t prefix_len = strlen(SERIAL_TRANSPORT_PREFIX);
	if (len <= prefix_len || strncmp(line, SERIAL_TRANSPORT_PREFIX, prefix_len) != 0)
		return; // not a protocol line, e.g. something typed in the monitor

	if (line_handler != NULL)
		line_handler(line + prefix_len, len - prefix_len);
}

static void serial_reader_task(void *arg)
{
	char *line = malloc(SERIAL_TRANSPORT_MAX_LINE);
	uint8_t chunk[READ_CHUNK];
	size_t len = 0;
	bool overflow = false;

	while (line != NULL)
	{
		int n = uart_read_bytes(SERIAL_UART, chunk, sizeof(chunk), pdMS_TO_TICKS(100));
		for (int i = 0; i < n; i++)
		{
			if (chunk[i] == '\n')
			{
				if (overflow)
					ESP_LOGW(TAG, "line longer than %d bytes dropped", SERIAL_TRANSPORT_MAX_LINE);
				else
				{
					line[len] = '\0';
					handle_line(line, len);
				}
				len = 0;
				overflow = false;
			}
			else if (len < SERIAL_TRANSPORT_MAX_LINE - 1)
				line[len++] = (char)chunk[i];
			else
				overflow = true;
		}
	}
	ESP_LOGE(TAG, "no memory for the line buffer");
	vTaskDelete(NULL);
}

esp_err_t serial_transport_start(serial_transport_line_cb_t handler)
{
	line_handler = handler;

	esp_err_t err = uart_driver_install(SERIAL_UART, UART_RX_BUF, UART_TX_BUF, 0, NULL, 0);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
		return err;
	}
	// Route the console (ESP_LOG, printf) through the driver too, so its writes and ours don't interleave
	esp_vfs_dev_uart_use_driver(SERIAL_UART);

	if (xTaskCreate(serial_reader_task, "serial_tp", 3072, NULL, 4, NULL) != pdPASS)
		return ESP_ERR_NO_MEM;
	return ESP_OK;
}

esp_err_t serial_transport_send_line(const char *line, size_t len)
{
	const size_t prefix_len = strlen(SERIAL_TRANSPORT_PREFIX);
	char *out = malloc(prefix_len + len + 1);
	if (out == NULL)
		return ESP_ERR_NO_MEM;

	memcpy(out, SERIAL_TRANSPORT_PREFIX, prefix_len);
	memcpy(out + prefix_len, line, len);
	out[prefix_len + len] = '\n';

	int written = uart_write_bytes(SERIAL_UART, out, prefix_len + len + 1);
	free(out);
	return (written < 0) ? ESP_FAIL : ESP_OK;
}
