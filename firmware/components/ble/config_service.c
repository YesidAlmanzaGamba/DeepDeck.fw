/**
 * @file config_service.c
 * @brief BLE GATT service for the DeepDeck Command Center (see config_service.h).
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_gatt_defs.h"
#include "config_service.h"

#define TAG "config_svc"

#define ATT_MTU_DEFAULT 23
#define RX_MAX_WRITE 512 // largest single write accepted on RX (one chunk)
#define CONGEST_WAIT_MS 500

/* 128-bit UUIDs, little endian. Base dd0e0000-7f4b-4c1a-9d3e-2a6b1c5e8f00:
 * service dd0e0001-..., RX dd0e0002-..., TX dd0e0003-... */
#define DD_UUID128(id) {0x00, 0x8f, 0x5e, 0x1c, 0x6b, 0x2a, 0x3e, 0x9d, \
                        0x1a, 0x4c, 0x4b, 0x7f, (id), 0x00, 0x0e, 0xdd}

static const uint8_t service_uuid[16] = DD_UUID128(0x01);
static const uint8_t rx_uuid[16] = DD_UUID128(0x02);
static const uint8_t tx_uuid[16] = DD_UUID128(0x03);

enum
{
	CFG_IDX_SVC,
	CFG_IDX_RX_CHAR,
	CFG_IDX_RX_VAL,
	CFG_IDX_TX_CHAR,
	CFG_IDX_TX_VAL,
	CFG_IDX_TX_CCC,
	CFG_IDX_NB,
};

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t char_decl_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t ccc_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t rx_props = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t tx_props = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static uint8_t rx_value[1] = {0};
static uint8_t tx_value[1] = {0};
static uint8_t tx_ccc[2] = {0x00, 0x00};

static const esp_gatts_attr_db_t config_gatt_db[CFG_IDX_NB] = {
	[CFG_IDX_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, sizeof(service_uuid), sizeof(service_uuid), (uint8_t *)service_uuid}},

	[CFG_IDX_RX_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&rx_props}},
	[CFG_IDX_RX_VAL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)rx_uuid, ESP_GATT_PERM_WRITE_ENCRYPTED, RX_MAX_WRITE, sizeof(rx_value), rx_value}},

	[CFG_IDX_TX_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&tx_props}},
	[CFG_IDX_TX_VAL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)tx_uuid, ESP_GATT_PERM_READ_ENCRYPTED, RX_MAX_WRITE, sizeof(tx_value), tx_value}},
	[CFG_IDX_TX_CCC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&ccc_uuid, ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED, sizeof(tx_ccc), sizeof(tx_ccc), tx_ccc}},
};

static esp_gatt_if_t config_gatts_if = ESP_GATT_IF_NONE;
static uint16_t handles[CFG_IDX_NB];
static bool connected = false;
static bool notify_enabled = false;
static volatile bool congested = false;
static uint16_t conn_id = 0;
static uint16_t mtu = ATT_MTU_DEFAULT;

static config_service_line_cb_t line_handler = NULL;
static char rx_line[CONFIG_SERVICE_MAX_LINE];
static size_t rx_len = 0;
static bool rx_overflow = false;

esp_err_t config_service_register_app(void)
{
	return esp_ble_gatts_app_register(CONFIG_SERVICE_APP_ID);
}

void config_service_set_line_handler(config_service_line_cb_t handler)
{
	line_handler = handler;
}

// Append a written chunk and hand over every complete line
static void rx_append(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		if (data[i] == '\n')
		{
			if (rx_overflow)
				ESP_LOGW(TAG, "line longer than %d bytes dropped", CONFIG_SERVICE_MAX_LINE);
			else if (rx_len > 0 && line_handler != NULL)
			{
				rx_line[rx_len] = '\0';
				line_handler(rx_line, rx_len);
			}
			rx_len = 0;
			rx_overflow = false;
		}
		else if (rx_len < CONFIG_SERVICE_MAX_LINE - 1)
		{
			rx_line[rx_len++] = (char)data[i];
		}
		else
		{
			rx_overflow = true;
		}
	}
}

static esp_err_t send_chunk(const uint8_t *data, uint16_t len)
{
	for (int waited = 0; congested && waited < CONGEST_WAIT_MS; waited += 10)
		vTaskDelay(pdMS_TO_TICKS(10));
	return esp_ble_gatts_send_indicate(config_gatts_if, conn_id, handles[CFG_IDX_TX_VAL], len, (uint8_t *)data, false);
}

esp_err_t config_service_send_line(const char *line, size_t len)
{
	if (!connected || !notify_enabled || config_gatts_if == ESP_GATT_IF_NONE)
		return ESP_ERR_INVALID_STATE;

	const size_t chunk = (mtu > 3) ? mtu - 3 : 20;
	esp_err_t err = ESP_OK;
	for (size_t sent = 0; sent < len && err == ESP_OK; sent += chunk)
	{
		size_t n = (len - sent < chunk) ? len - sent : chunk;
		err = send_chunk((const uint8_t *)line + sent, n);
	}
	if (err == ESP_OK)
		err = send_chunk((const uint8_t *)"\n", 1);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "send failed: %s", esp_err_to_name(err));
	return err;
}

void config_service_indicate_service_changed(esp_bd_addr_t remote_bda)
{
	if (config_gatts_if == ESP_GATT_IF_NONE)
		return;
	esp_err_t err = esp_ble_gatts_send_service_change_indication(config_gatts_if, remote_bda);
	ESP_LOGI(TAG, "service changed indication: %s", esp_err_to_name(err));
}

bool config_service_gatts_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
								esp_ble_gatts_cb_param_t *param)
{
	if (event == ESP_GATTS_REG_EVT)
	{
		if (param->reg.app_id != CONFIG_SERVICE_APP_ID)
			return false;
		if (param->reg.status != ESP_GATT_OK)
		{
			ESP_LOGE(TAG, "app register failed: %d", param->reg.status);
			return true;
		}
		config_gatts_if = gatts_if;
		esp_ble_gatts_create_attr_tab(config_gatt_db, gatts_if, CFG_IDX_NB, 0);
		return true;
	}

	if (config_gatts_if == ESP_GATT_IF_NONE || gatts_if != config_gatts_if)
		return false;

	switch (event)
	{
	case ESP_GATTS_CREAT_ATTR_TAB_EVT:
		if (param->add_attr_tab.status == ESP_GATT_OK && param->add_attr_tab.num_handle == CFG_IDX_NB)
		{
			memcpy(handles, param->add_attr_tab.handles, sizeof(handles));
			esp_ble_gatts_start_service(handles[CFG_IDX_SVC]);
			ESP_LOGI(TAG, "service started");
		}
		else
		{
			ESP_LOGE(TAG, "attribute table failed: %d", param->add_attr_tab.status);
		}
		break;

	case ESP_GATTS_CONNECT_EVT:
		connected = true;
		conn_id = param->connect.conn_id;
		mtu = ATT_MTU_DEFAULT;
		notify_enabled = false;
		rx_len = 0;
		break;

	case ESP_GATTS_DISCONNECT_EVT:
		connected = false;
		notify_enabled = false;
		congested = false;
		break;

	case ESP_GATTS_MTU_EVT:
		mtu = param->mtu.mtu;
		ESP_LOGI(TAG, "MTU %d", mtu);
		break;

	case ESP_GATTS_CONGEST_EVT:
		congested = param->congest.congested;
		break;

	case ESP_GATTS_WRITE_EVT:
		if (param->write.handle == handles[CFG_IDX_TX_CCC] && param->write.len == 2)
		{
			notify_enabled = (param->write.value[0] & 0x01) != 0;
			ESP_LOGI(TAG, "notifications %s", notify_enabled ? "on" : "off");
		}
		else if (param->write.handle == handles[CFG_IDX_RX_VAL] && !param->write.is_prep)
		{
			rx_append(param->write.value, param->write.len);
		}
		break;

	default:
		break;
	}
	return true;
}
