//! BLE link to the DeepDeck configuration service on Windows, straight on WinRT.
//!
//! btleplug is not used here: its Windows backend only knows devices it saw advertising (a
//! connected keyboard does not advertise) and discovers services uncached, which Windows refuses
//! for a connected HID device. WinRT opens a paired device by id and serves its cached GATT
//! database. Hosts paired with firmware older than 0.6.0 must re-pair once (see docs/protocol.md).

use std::future::IntoFuture;
use std::sync::{Arc, Mutex};

use windows::core::{Ref, GUID, HSTRING};
use windows::Devices::Bluetooth::GenericAttributeProfile::{
    GattCharacteristic, GattClientCharacteristicConfigurationDescriptorValue,
    GattCommunicationStatus, GattDeviceService, GattSession, GattValueChangedEventArgs,
    GattWriteOption,
};
use windows::Devices::Bluetooth::{BluetoothCacheMode, BluetoothConnectionStatus, BluetoothLEDevice};
use windows::Devices::Enumeration::DeviceInformation;
use windows::Foundation::TypedEventHandler;
use windows::Storage::Streams::{DataReader, DataWriter};

pub const SERVICE: GUID = GUID::from_u128(0xdd0e0001_7f4b_4c1a_9d3e_2a6b1c5e8f00);
pub const RX: GUID = GUID::from_u128(0xdd0e0002_7f4b_4c1a_9d3e_2a6b1c5e8f00);
pub const TX: GUID = GUID::from_u128(0xdd0e0003_7f4b_4c1a_9d3e_2a6b1c5e8f00);

fn e(err: windows::core::Error) -> String {
    format!("{err:?}")
}

#[derive(serde::Serialize, Clone)]
pub struct PairedDevice {
    pub name: String,
    pub id: String,
}

/// Paired BLE devices known to Windows (no scan needed)
pub async fn paired() -> Result<Vec<PairedDevice>, String> {
    let selector = BluetoothLEDevice::GetDeviceSelectorFromPairingState(true).map_err(e)?;
    let infos = DeviceInformation::FindAllAsyncAqsFilter(&selector)
        .map_err(e)?
        .into_future()
        .await
        .map_err(e)?;
    let mut list = Vec::new();
    for info in &infos {
        list.push(PairedDevice {
            name: info.Name().map_err(e)?.to_string(),
            id: info.Id().map_err(e)?.to_string(),
        });
    }
    Ok(list)
}

pub struct BleConn {
    _device: BluetoothLEDevice,
    rx: GattCharacteristic,
    _tx: GattCharacteristic,
    session: GattSession,
}

async fn characteristic(service: &GattDeviceService, uuid: GUID) -> Result<GattCharacteristic, String> {
    let result = service
        .GetCharacteristicsForUuidWithCacheModeAsync(uuid, BluetoothCacheMode::Cached)
        .map_err(e)?
        .into_future()
        .await
        .map_err(e)?;
    if result.Status().map_err(e)? != GattCommunicationStatus::Success {
        return Err(format!("characteristic {uuid:?}: {:?}", result.Status()));
    }
    result
        .Characteristics()
        .map_err(e)?
        .GetAt(0)
        .map_err(|_| format!("characteristic {uuid:?} not found"))
}

/// Open the paired device and subscribe to TX. `on_bytes` gets every notification payload,
/// `on_lost` is called once when Windows reports the device disconnected.
pub async fn connect(
    id: &str,
    on_bytes: impl Fn(Vec<u8>) + Send + Sync + 'static,
    on_lost: impl Fn() + Send + Sync + 'static,
) -> Result<(BleConn, u16, String), String> {
    let device = BluetoothLEDevice::FromIdAsync(&HSTRING::from(id))
        .map_err(e)?
        .into_future()
        .await
        .map_err(e)?;
    let name = device.Name().map_err(e)?.to_string();

    // Cached: Windows refuses an uncached discovery on a connected HID device
    let services = device
        .GetGattServicesForUuidWithCacheModeAsync(SERVICE, BluetoothCacheMode::Cached)
        .map_err(e)?
        .into_future()
        .await
        .map_err(e)?;
    if services.Status().map_err(e)? != GattCommunicationStatus::Success {
        return Err(format!("service discovery: {:?}", services.Status()));
    }
    let service = services.Services().map_err(e)?.GetAt(0).map_err(|_| {
        "the DeepDeck has no configuration service: update its firmware, or remove and pair it again in Windows"
            .to_string()
    })?;

    let rx = characteristic(&service, RX).await?;
    let tx = characteristic(&service, TX).await?;

    // Handlers are not Send: register them in blocks so they are dropped before the next await
    {
        let handler = TypedEventHandler::new(
            move |_: Ref<GattCharacteristic>, args: Ref<GattValueChangedEventArgs>| {
                if let Ok(args) = args.ok() {
                    let value = args.CharacteristicValue()?;
                    let reader = DataReader::FromBuffer(&value)?;
                    let mut bytes = vec![0u8; reader.UnconsumedBufferLength()? as usize];
                    reader.ReadBytes(&mut bytes)?;
                    on_bytes(bytes);
                }
                Ok(())
            },
        );
        tx.ValueChanged(&handler).map_err(e)?;
    }
    {
        let handler = TypedEventHandler::new(move |device: Ref<BluetoothLEDevice>, _| {
            if let Ok(device) = device.ok() {
                if device.ConnectionStatus()? == BluetoothConnectionStatus::Disconnected {
                    on_lost();
                }
            }
            Ok(())
        });
        device.ConnectionStatusChanged(&handler).map_err(e)?;
    }

    let status = tx
        .WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify,
        )
        .map_err(e)?
        .into_future()
        .await
        .map_err(e)?;
    if status != GattCommunicationStatus::Success {
        return Err(format!("enable notifications: {status:?}"));
    }

    let session = GattSession::FromDeviceIdAsync(&device.BluetoothDeviceId().map_err(e)?)
        .map_err(e)?
        .into_future()
        .await
        .map_err(e)?;
    let mtu = session.MaxPduSize().map_err(e)?;
    Ok((BleConn { _device: device, rx, _tx: tx, session }, mtu, name))
}

impl BleConn {
    /// Write a line in chunks of at most MTU-3 bytes
    pub async fn send(&self, line: &str) -> Result<(), String> {
        let mtu = self.session.MaxPduSize().map_err(e)? as usize;
        let chunk = mtu.saturating_sub(3).max(20);
        let mut data = line.as_bytes().to_vec();
        data.push(b'\n');
        for part in data.chunks(chunk) {
            // IBuffer is not Send: start the write in a block so the buffer is gone before awaiting
            let operation = {
                let writer = DataWriter::new().map_err(e)?;
                writer.WriteBytes(part).map_err(e)?;
                let buffer = writer.DetachBuffer().map_err(e)?;
                self.rx
                    .WriteValueWithOptionAsync(&buffer, GattWriteOption::WriteWithoutResponse)
                    .map_err(e)?
            };
            let status = operation.into_future().await.map_err(e)?;
            if status != GattCommunicationStatus::Success {
                return Err(format!("write: {status:?}"));
            }
        }
        Ok(())
    }
}

/// Joins notification chunks into lines
#[derive(Clone, Default)]
pub struct LineBuffer(Arc<Mutex<Vec<u8>>>);

impl LineBuffer {
    pub fn push(&self, bytes: &[u8]) -> Vec<String> {
        let mut buf = self.0.lock().unwrap();
        buf.extend_from_slice(bytes);
        let mut lines = Vec::new();
        while let Some(pos) = buf.iter().position(|&b| b == b'\n') {
            let line: Vec<u8> = buf.drain(..=pos).collect();
            lines.push(String::from_utf8_lossy(&line[..line.len() - 1]).into_owned());
        }
        lines
    }
}
