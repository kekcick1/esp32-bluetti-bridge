# Bluetti EB3A MODBUS Register Mapping

## Confirmed Registers (via BLE MODBUS RTU)

### Connection Details
- **MODBUS Command**: `01 03 00 06 00 28 [CRC16]`
- **Function Code**: 0x03 (Read Holding Registers)
- **Start Address**: 0x0006
- **Register Count**: 40 (0x28) = 80 bytes data
- **Response**: `01 03 50 [80 bytes data] [CRC16]` = 85 bytes total

### Register Layout (from address 0x0006)

| Register | Offset | Value (hex) | Value (dec) | Interpretation | Notes |
|----------|--------|-------------|-------------|----------------|-------|
| 0x0006-0x0009 | 0-9 | 0x0000 | 0 | AC Power registers? | All zeros when AC OFF |
| 0x000A | 11-12 | 0x4542 | 17730 | Model name "EB" | ASCII: 'E','B' |
| 0x000B | 13-14 | 0x3341 | 13121 | Model name "3A" | ASCII: '3','A' |
| 0x000C-0x000F | 15-21 | 0x0000 | 0 | Unknown | |
| **0x0010** | **23-24** | **0x03FB** | **1019** | **Battery SOC × 10** | **1019/10 = 101.9% → limit to 100%** |
| 0x0011 | 25-26 | 0xCBB8 | 52152 | Unknown | |
| 0x0012 | 27-28 | 0x5FA6 | 24486 | Unknown | |
| 0x0013 | 29-30 | 0x0219 | 537 | Voltage? | 53.7V or 5.37V × 100? |
| 0x0014-0x0016 | 31-37 | 0x0000 | 0 | Unknown | |
| 0x0017 | 38-39 | 0x21FC | 8700 | Unknown | Temperature? |
| 0x0018 | 39-40 | 0x0003 | 3 | Idle power? | 3W standby |
| 0x0019 | 41-42 | 0x219B | 8603 | Unknown | |
| 0x001A | 43-44 | 0x0003 | 3 | Idle power? | 3W standby |
| 0x001B-0x0024 | 45-63 | 0x0000 | 0 | Unknown | |
| **0x0025** | **65-66** | **0x000F-0x0012** | **15-18** | **DC Output Power (W)** | **Real-time fluctuation** |
| **0x0026** | **67-68** | **0x000F-0x0012** | **15-18** | **DC Output Power (W) - duplicate** | **Confirmation register** |
| 0x0027-0x002A | 69-76 | 0x0000 | 0 | Unknown | |
| 0x002B | 77-78 | 0x0064 | 100 | Max DC limit? | Constant 100 |
| 0x002C | 79-80 | 0x0000 | 0 | Unknown | |
| **0x002D** | **81-82** | **0x0001** | **1** | **DC Output State** | **1 = ON, 0 = OFF?** |

## Implementation Notes

### Parsing Code (bluetti_device.cpp)
```cpp
// Battery SOC (offset 23, Reg 0x0010)
uint16_t batteryRaw = (data[23] << 8) | data[24];
cachedBattery = batteryRaw / 10;
if (cachedBattery > 100) cachedBattery = 100;

// DC Output Power (offset 65, Reg 0x0025)
cachedDcPower = (data[65] << 8) | data[66];

// DC Output State (offset 81, Reg 0x002D)
uint16_t dcStateRaw = (data[81] << 8) | data[82];
cachedDcState = (dcStateRaw > 0);

// AC Output State (offsets 0-9 checksum)
uint16_t acCheck = 0;
for (int i = 0; i < 5; i++) {
  acCheck += (data[3 + i*2] << 8) | data[3 + i*2 + 1];
}
cachedAcState = (acCheck > 0);
cachedAcPower = 0; // TODO: find AC power register
```

## TODO: Unidentified Registers
- **AC Output Power**: Expected in offsets 0-9 when AC is ON (need testing)
- **Input Power**: Expected somewhere in 40 registers (need testing with charger)
- **AC Output State**: Currently using checksum of offsets 0-9 (need confirmation)
- **Temperature**: Possibly Reg 0x0017 (8700) or 0x0019 (8603)
- **Voltage**: Possibly Reg 0x0013 (537 → 53.7V?)
- **Remaining capacity (Wh)**: Possibly Reg 0x0010 (1019Wh?)

## Testing Conditions
- **EB3A State**: ON, battery 100%, AC OFF, DC ON
- **Load**: Computer via DC output (~15-18W fluctuating)
- **Charger**: Not connected (input power = 0W)
- **ESP32**: T-Display with NimBLE-Arduino 1.4.3
- **BLE Bonding**: Required (bonding=true)

## References
- Based on logs from actual EB3A device (MAC: D1:4C:11:6B:6A:3D)
- Model: EB3A2308002204600
- Bluetti MQTT protocol analysis
- MODBUS RTU over BLE
