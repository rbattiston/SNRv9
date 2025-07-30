# SNRv8 IO Configuration and Processing Architecture

## Overview

The SNRv8 system is a sophisticated ESP32-based irrigation control system with extensive signal conditioning, alarm monitoring, and scheduling capabilities. This document provides a comprehensive overview of how Analog Inputs (AI), Binary Outputs (BO), alarms, and processing work together in the system.

## IO Configuration Architecture

### Hardware Interface Types

The system supports multiple IO point types defined in the `IOPointType` enum:

#### Analog Inputs (AI)
- **`GPIO_AI`** - Direct ESP32 GPIO analog inputs (ADC pins)
- Supports 0-20mA and 0-10V inputs via external conditioning circuits
- Examples: ADC1-ADC4 (pins 34, 39, 35, 36) for 4-20mA, ADC5/ADC7 (pins 33, 32) for 0-10V

#### Binary Outputs (BO)
- **`SHIFT_REG_BO`** - Shift register-based outputs (74HC595)
- **`GPIO_BO`** - Direct ESP32 GPIO outputs
- Used for controlling solenoids, pumps, and lighting

#### Binary Inputs (BI)
- **`SHIFT_REG_BI`** - Shift register-based inputs (74HC165)
- **`GPIO_BI`** - Direct ESP32 GPIO inputs

### Shift Register Configuration

The system uses shift registers for IO expansion:

```json
"shiftRegisterConfig": {
  "outputClockPin": 22,
  "outputLatchPin": 23,
  "outputDataPin": 12,
  "outputEnablePin": 13,
  "inputClockPin": 2,
  "inputLoadPin": 0,
  "inputDataPin": 15,
  "numOutputRegisters": 1,
  "numInputRegisters": 1
}
```

## Shift Register Read/Write Operations

### Hardware Architecture

#### Output Shift Registers (74HC595)
- **Clock Pin (22)**: Controls when data is shifted into the register
- **Latch Pin (23)**: Controls when buffered data appears on outputs
- **Data Pin (12)**: Serial data input (MOSI)
- **Enable Pin (13)**: Output enable (active low, optional)

#### Input Shift Registers (74HC165)
- **Clock Pin (2)**: Controls when data is shifted out of the register
- **Load Pin (0)**: Parallel load control (loads all inputs simultaneously)
- **Data Pin (15)**: Serial data output (MISO)

### Reading Input Shift Registers

The `readInputs()` method performs the following sequence:

#### 1. Parallel Load Phase
```cpp
digitalWrite(_inputLoadPin, LOW);   // Start parallel load
delayMicroseconds(5);              // Allow inputs to settle
digitalWrite(_inputLoadPin, HIGH);  // Complete parallel load
delayMicroseconds(5);              // Stabilization delay
```

#### 2. Serial Read Phase
For each register chip (starting from highest index):
```cpp
for (int i = _numInputRegisters - 1; i >= 0; --i) {
    uint8_t currentByte = 0;
    for(int j = 0; j < 8; j++) {
        digitalWrite(_inputClockPin, LOW);        // Clock low
        int bitValue = digitalRead(_inputDataPin); // Read data bit
        digitalWrite(_inputClockPin, HIGH);       // Clock high
        currentByte = (currentByte << 1) | bitValue; // Assemble byte
    }
    tempInputBuffer[i] = currentByte; // Store assembled byte
}
```

#### 3. Buffer Update (Thread-Safe)
```cpp
{
    LockGuard lock(_srMutex, "SR_ReadBuf");
    _inputShiftRegisterStates = tempInputBuffer; // Update internal buffer
}
```

### Writing Output Shift Registers

The `writeOutputs()` method performs the following sequence:

#### 1. Buffer Copy (Thread-Safe)
```cpp
{
    LockGuard lock(_srMutex, "SR_WriteBuf");
    tempOutputBuffer = _outputShiftRegisterStates; // Copy internal buffer
}
```

#### 2. Serial Write Phase
```cpp
digitalWrite(_outputLatchPin, LOW); // Prepare latch (disable outputs)

for (int i = _numOutputRegisters - 1; i >= 0; --i) {
    uint8_t byteToWrite = tempOutputBuffer[i];
    shiftOut(_outputDataPin, _outputClockPin, MSBFIRST, byteToWrite);
}

digitalWrite(_outputLatchPin, HIGH); // Latch data (enable outputs)
```

### Individual Bit Operations

#### Reading Input Bits
```cpp
bool ShiftRegisterHandler::getInputBit(int chipIndex, int bitIndex, bool& value) {
    LockGuard lock(_srMutex, "SR_GetInBit");
    
    // Validate indices
    if (chipIndex < 0 || chipIndex >= _numInputRegisters || 
        bitIndex < 0 || bitIndex >= 8) {
        return false;
    }
    
    // Extract bit from byte
    uint8_t chipByte = _inputShiftRegisterStates[chipIndex];
    value = (chipByte >> bitIndex) & 0x01;
    return true;
}
```

#### Setting Output Bits
```cpp
bool ShiftRegisterHandler::setOutputBit(int chipIndex, int bitIndex, bool state) {
    LockGuard lock(_srMutex, "SR_SetOutBit");
    
    // Validate indices
    if (chipIndex < 0 || chipIndex >= _numOutputRegisters || 
        bitIndex < 0 || bitIndex >= 8) {
        return false;
    }
    
    // Modify bit in byte
    uint8_t& chipByte = _outputShiftRegisterStates[chipIndex];
    if (state) {
        chipByte |= (1 << bitIndex);   // Set bit
    } else {
        chipByte &= ~(1 << bitIndex);  // Clear bit
    }
    return true;
}
```

### Byte-Level Operations

#### Setting Entire Output Bytes
```cpp
bool ShiftRegisterHandler::setOutputBufferByte(int chipIndex, uint8_t byte) {
    LockGuard lock(_srMutex, "SR_SetOutByte");
    
    if (chipIndex < 0 || chipIndex >= _numOutputRegisters) {
        return false;
    }
    
    _outputShiftRegisterStates[chipIndex] = byte;
    return true;
}
```

#### Reading Output Buffer Bytes
```cpp
bool ShiftRegisterHandler::getOutputBufferByte(int chipIndex, uint8_t& byte) {
    LockGuard lock(_srMutex, "SR_GetOutByte");
    
    if (chipIndex < 0 || chipIndex >= _numOutputRegisters) {
        return false;
    }
    
    byte = _outputShiftRegisterStates[chipIndex];
    return true;
}
```

### Thread Safety Implementation

All shift register operations use mutex protection:

- **Internal Buffers**: `_inputShiftRegisterStates` and `_outputShiftRegisterStates`
- **Mutex**: `_srMutex` protects all buffer access
- **LockGuard**: RAII-style mutex management prevents deadlocks
- **Hardware Separation**: Hardware I/O operations occur outside mutex locks

### Timing Considerations

#### Critical Timing Requirements
- **Load Pulse**: 5μs minimum for parallel load operation
- **Clock Transitions**: Hardware-dependent, typically 1μs minimum
- **Setup/Hold Times**: Ensured by sequential GPIO operations

#### Performance Characteristics
- **Read Operation**: ~80μs for single 8-bit register
- **Write Operation**: ~64μs for single 8-bit register  
- **Scalability**: Linear increase with number of chained registers

### Error Handling

#### Index Validation
- Chip index must be: `0 <= chipIndex < numRegisters`
- Bit index must be: `0 <= bitIndex <= 7`
- Invalid indices return `false` without modifying state

#### Hardware Validation
- Pin configuration validated during `init()`
- Mutex creation verified before operations
- Buffer size consistency checked during operations

### Integration with IOManager

#### BO (Binary Output) Control Flow
1. `IOManager::setBOState()` calls `setOutputBit()`
2. Bit is set in internal buffer (thread-safe)
3. `writeOutputs()` is called to update hardware
4. Hardware outputs change immediately after latch

#### BI (Binary Input) Reading Flow
1. `IOManager::updateInputs()` calls `readInputs()`
2. All input registers read simultaneously
3. Internal buffer updated (thread-safe)
4. Individual bits accessed via `getInputBit()`

This architecture provides reliable, thread-safe access to shift register chains while maintaining precise timing control for hardware operations.

## AI (Analog Input) Processing Pipeline

### 1. Raw Data Acquisition

- ESP32 ADC reads raw values (0-4095 for 12-bit ADC)
- Performed in `IOManager::updateInputs()` every 1000ms via dedicated FreeRTOS task
- Raw values stored in `IOPointRuntimeState.value`

### 2. Signal Conditioning (SignalConditioner class)

Applied in this order when `signalConfig.enabled = true`:

1. **Offset Application**: `value + offset`
2. **Gain Application**: `value * gain`
3. **Scaling Factor**: `value * scalingFactor`
4. **Lookup Table Interpolation**: Linear interpolation if `lookupTableEnabled = true`
5. **Precision Rounding**: Round to specified decimal places
6. **Filtering**: Simple Moving Average (SMA) if `filterType = "SMA"`

#### Example Configuration

```json
"signalConfig": {
  "enabled": true,
  "filterType": "SMA",
  "gain": 1.0,
  "offset": 0.0,
  "scalingFactor": 1.0,
  "smaWindowSize": 25,
  "precisionDigits": 2,
  "units": "",
  "historyBufferSize": 100
}
```

### 3. Data Storage & History

- **Raw Value**: Stored in `IOPointRuntimeState.value`
- **Conditioned Value**: Stored in `IOPointRuntimeState.conditionedValue`
- **History Buffer**: Rolling buffer of `ConditionedValueEntry` objects with timestamps
- **SMA Buffer**: Circular buffer for moving average calculations

### 4. Trending Integration

- Conditioned values automatically collected by `TrendingManager`
- Binary format storage for efficiency
- Configurable sampling intervals (10s to 3600s)

## BO (Binary Output) Processing

### BO Types & Calibration

Each BO has a `boType` that affects scheduling behavior:

#### SOLENOID
- **Purpose**: Irrigation valves
- **Flow calibration**: `lphPerEmitterFlow` × `numEmittersPerPlant`
- **Volume-to-duration conversion** for precise irrigation
- **AutoPilot sensor integration** for feedback control

#### LIGHTING
- **Purpose**: Grow lights
- **Schedule-based** on/off control
- **Coordinated** with irrigation timing

#### PUMP
- **Purpose**: Water pumps
- **Similar to solenoids** but different scheduling logic

### BO Control Flow

1. **State Request**: `IOManager::setBOState(id, state)`
2. **Configuration Lookup**: Get BO config from `IOStateManager`
3. **Hardware Abstraction**: 
   - GPIO BOs: Direct `digitalWrite()`
   - Shift Register BOs: Update buffer + `writeOutputs()`
4. **State Persistence**: Save to ESP32 Preferences for power-cycle recovery
5. **Trending Collection**: Log state changes with timestamps

### Safety Features

- **Boot-time Shutdown**: All BOs turned OFF during system boot (`_ensureAllBOsOff()`)
- **Configuration Safety**: BOs turned OFF before any configuration changes
- **Manual Override**: Configurable timeouts for manual control
- **Schedule Integration**: Automatic restoration by ScheduleExecutor

## Alarm System Architecture

### Alarm Configuration

Each AI point can have comprehensive alarm monitoring:

```json
"alarmConfig": {
  "enabled": false,
  "historySamplesForAnalysis": 20,
  "rules": {
    "checkRateOfChange": true,
    "rateOfChangeThreshold": 50.0,
    "checkDisconnected": true,
    "disconnectedThreshold": 0.5,
    "checkMaxValue": true,
    "maxValueThreshold": 4090.0,
    "checkStuckSignal": true,
    "stuckSignalWindowSamples": 10,
    "stuckSignalDeltaThreshold": 1.0,
    "alarmPersistenceSamples": 1,
    "alarmClearHysteresisValue": 5.0,
    "requiresManualReset": false,
    "samplesToClearAlarmCondition": 3,
    "consecutiveGoodSamplesToRestoreTrust": 5
  }
}
```

### Alarm Types & Detection

1. **Rate of Change**: Detects rapid signal changes (sensor malfunction)
2. **Disconnected**: Detects sensor disconnection (low signal)
3. **Max Value**: Detects over-range conditions (sensor saturation)
4. **Stuck Signal**: Detects unchanging values (sensor failure)

### Alarm State Management

- **Persistence**: Alarms must persist for configured sample count
- **Hysteresis**: Prevents alarm chattering with configurable deadband
- **Trust System**: Tracks sensor reliability after alarm conditions
- **Manual Reset**: Optional requirement for human intervention

### Alarm Processing Task

- Dedicated FreeRTOS task runs every 5 seconds
- Analyzes AI history buffers for alarm conditions
- Integrates with EventLogger for audit trails
- Thread-safe access via IOStateManager

## Processing Architecture & Threading

### FreeRTOS Task Structure

#### 1. IO Polling Task (Priority 2, 1000ms interval)
- Reads all inputs (GPIO + Shift Register)
- Applies signal conditioning
- Updates runtime states
- Collects trending data

#### 2. Alarm Processing Task (5000ms interval)
- Analyzes AI points for alarm conditions
- Manages alarm state transitions
- Logs alarm events

#### 3. Schedule Executor Task (60000ms interval)
- Executes irrigation schedules
- Manages AutoPilot windows
- Controls BO states based on time/sensor feedback

#### 4. Request Priority Manager Task (2000ms interval)
- Manages web request priorities
- Prevents system overload
- Queues heavy operations

### Thread Safety & State Management

- **IOStateManager**: Central thread-safe state repository
- **Mutex Protection**: All shared data access protected
- **Copy-based Access**: Prevents lock contention
- **Handler Abstraction**: GPIO and Shift Register handlers manage hardware safely

## Configuration Data Flow

### Startup Sequence

1. **StorageManager**: Initialize LittleFS filesystem
2. **ConfigManager**: Load `io_config.json`
3. **IOManager**: Initialize hardware based on config
4. **State Restoration**: Restore BO states from Preferences
5. **Safety Shutdown**: Turn off all BOs for safety
6. **Task Creation**: Start all processing tasks

### Runtime Configuration Changes

1. **Web API**: Receive configuration updates
2. **ConfigManager**: Validate and save new config
3. **IOManager::reinit()**: Reload without safety shutdown
4. **State Preservation**: Maintain current BO states during config reload

### Configuration Validation

- Pin conflict detection
- Range validation for analog inputs
- BO type compatibility checks
- Shift register index validation

## Integration Points

### Scheduling Integration

- ScheduleExecutor reads BO configurations for flow rate calculations
- Volume-based events use `flowRateMLPerSecond` for duration conversion
- AutoPilot windows monitor AI sensors for trigger conditions

### Web Interface Integration

- Real-time data via WebSocket or polling
- Configuration management through REST APIs
- Alarm status and acknowledgment interfaces

### Trending Integration

- Automatic data collection for all enabled AI points
- Binary file format for efficient storage
- Configurable sampling rates and retention

## Key Classes and Components

### IOManager
- **Purpose**: Central coordinator for all IO operations
- **Key Methods**:
  - `updateInputs()`: Reads and processes all input points
  - `setBOState()`: Controls binary output states
  - `getAIConditionedValue()`: Retrieves processed analog values
- **Thread Safety**: Uses IOStateManager for safe concurrent access

### IOStateManager
- **Purpose**: Thread-safe repository for IO point configurations and runtime states
- **Features**: Mutex-protected access, copy-based operations
- **Data Storage**: Maintains both configuration and runtime state maps

### SignalConditioner
- **Purpose**: Static class for applying signal conditioning algorithms
- **Processing Pipeline**: Offset → Gain → Scaling → Lookup → Precision → Filtering
- **Filtering**: Simple Moving Average (SMA) implementation

### AlarmManager
- **Purpose**: Monitors AI points for alarm conditions
- **Detection Types**: Rate of change, disconnected, max value, stuck signal
- **State Management**: Persistence, hysteresis, trust tracking

### TrendingManager
- **Purpose**: Collects and stores historical data
- **Integration**: Automatic collection from IOManager during input processing
- **Storage**: Binary format for efficiency

## Configuration File Structure

The main configuration is stored in `/data/io_config.json` with the following structure:

```json
{
  "shiftRegisterConfig": { /* SR pin assignments */ },
  "ioPoints": [
    {
      "id": "unique_identifier",
      "name": "Display Name",
      "description": "Detailed description",
      "type": "GPIO_AI|GPIO_BO|SHIFT_REG_BI|SHIFT_REG_BO",
      "pin": 34,
      "chipIndex": 0,
      "bitIndex": 0,
      "isInverted": false,
      "signalConfig": { /* Signal conditioning settings */ },
      "alarmConfig": { /* Alarm monitoring settings */ },
      "boSpecificConfig": { /* BO type and calibration */ }
    }
  ]
}
```

## Performance Considerations

### Memory Management
- Copy-based state access prevents lock contention
- Circular buffers for history and SMA data
- Binary format trending storage for efficiency

### CPU Usage
- 1-second polling interval balances responsiveness and CPU load
- Dedicated tasks prevent blocking operations
- Yield calls in processing loops prevent watchdog timeouts

### System Stability
- Boot-time safety shutdown ensures safe startup
- Configuration change safety prevents unsafe states
- Request priority management prevents system overload

## Conclusion

This architecture provides a robust, scalable foundation for precision irrigation control with comprehensive monitoring, alarming, and data collection capabilities. The modular design allows for easy extension and maintenance while ensuring thread safety and system reliability.

The system successfully balances real-time control requirements with data collection needs, providing both immediate responsiveness for irrigation control and comprehensive historical data for analysis and optimization.
