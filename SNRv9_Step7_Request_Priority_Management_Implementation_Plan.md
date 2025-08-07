# Step 7: Request Priority Management - Deep Implementation Analysis

Based on analysis of the current web server architecture, here's a comprehensive implementation plan for Step 7:

## **Current Architecture Analysis**

The existing `web_server_manager.c` shows a solid foundation but has several areas where request priority management will provide critical improvements:

### **Current Limitations Identified:**
1. **No Request Classification**: All requests are handled with equal priority
2. **Single-threaded Processing**: ESP-IDF HTTP server processes requests sequentially
3. **No Load Protection**: Heavy operations could cause watchdog timeouts
4. **No Queue Management**: No mechanism to defer or prioritize requests
5. **Limited Concurrency Control**: Only basic socket limits (7 max connections)

### **Current Strengths to Build Upon:**
1. **Thread-safe Statistics**: Already using mutexes for stats tracking
2. **Modular Controller Architecture**: Clean separation of concerns
3. **Comprehensive Monitoring**: Request tracking and error handling
4. **ESP-IDF Integration**: Proper use of official HTTP server component

## **Step 7 Implementation Architecture**

### **1. Request Priority Classification System**

**Priority Levels (Highest to Lowest):**
```c
typedef enum {
    REQUEST_PRIORITY_EMERGENCY = 0,    // Emergency stop, safety shutdowns
    REQUEST_PRIORITY_IO_CRITICAL,      // Real-time IO operations
    REQUEST_PRIORITY_AUTHENTICATION,   // Login/logout operations
    REQUEST_PRIORITY_UI_CRITICAL,      // Dashboard updates, status checks
    REQUEST_PRIORITY_NORMAL,           // Standard web requests
    REQUEST_PRIORITY_BACKGROUND,       // Logging, statistics, file uploads
    REQUEST_PRIORITY_MAX
} request_priority_t;
```

**Classification Rules:**
- **EMERGENCY**: `/api/emergency/*`, `/api/io/emergency-stop`
- **IO_CRITICAL**: `/api/io/points/*/set`, `/api/irrigation/zones/*/activate`
- **AUTHENTICATION**: `/api/auth/*`
- **UI_CRITICAL**: `/api/status`, `/api/dashboard/*`, `/api/io/points` (GET)
- **NORMAL**: Static files, general API calls
- **BACKGROUND**: `/api/logs/*`, `/api/statistics/*`, file uploads

### **2. Request Queue Management System**

**Queue Architecture:**
```c
typedef struct {
    httpd_req_t *request;
    request_priority_t priority;
    uint32_t timestamp;
    uint32_t timeout_ms;
    request_context_t *context;
} queued_request_t;

typedef struct {
    queued_request_t requests[MAX_QUEUED_REQUESTS];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t semaphore;
} request_queue_t;
```

**Queue Management:**
- **Priority Queues**: Separate queues for each priority level
- **FIFO within Priority**: Requests of same priority processed in order
- **Timeout Handling**: Automatic request timeout and cleanup
- **Queue Limits**: Prevent memory exhaustion under load

### **3. Request Processing Engine**

**Processing Task Architecture:**
```c
// High-priority task for critical requests
static void request_processor_critical_task(void *pvParameters);

// Normal-priority task for standard requests  
static void request_processor_normal_task(void *pvParameters);

// Low-priority task for background requests
static void request_processor_background_task(void *pvParameters);
```

**Task Configuration:**
- **Critical Task**: Priority 10, 4096 bytes stack, Core 1
- **Normal Task**: Priority 5, 6144 bytes stack, Core 0
- **Background Task**: Priority 2, 4096 bytes stack, Core 0

### **4. Load Balancing and Protection**

**Watchdog Protection:**
```c
typedef struct {
    uint32_t max_processing_time_ms;
    uint32_t watchdog_feed_interval_ms;
    bool enable_yield_on_heavy_ops;
    uint32_t heavy_operation_threshold_ms;
} load_protection_config_t;
```

**Protection Mechanisms:**
- **Processing Time Limits**: Maximum time per request type
- **Automatic Yielding**: Yield CPU during heavy operations
- **Watchdog Feeding**: Regular watchdog resets during long operations
- **Request Deferral**: Move heavy operations to background tasks

### **5. Integration with Existing Architecture**

**Web Server Manager Integration:**
```c
// New function in web_server_manager.h
bool web_server_manager_init_priority_system(void);

// Modified request handler wrapper
static esp_err_t priority_request_wrapper(httpd_req_t *req);

// Priority classification function
static request_priority_t classify_request(httpd_req_t *req);
```

**Controller Integration:**
- **Modify Existing Controllers**: Add priority hints to controller functions
- **Request Context**: Pass priority information through request chain
- **Response Optimization**: Faster responses for high-priority requests

## **Detailed Implementation Plan**

### **Phase 1: Core Priority Infrastructure (Week 1)**

**Files to Create:**
```
components/web/include/request_priority_manager.h
components/web/request_priority_manager.c
components/web/include/request_queue.h
components/web/request_queue.c
```

**Key Functions:**
```c
// Initialize priority management system
bool request_priority_manager_init(void);

// Classify incoming request
request_priority_t request_priority_classify(httpd_req_t *req);

// Queue request for processing
esp_err_t request_priority_queue_request(httpd_req_t *req, request_priority_t priority);

// Process queued requests
void request_priority_process_queues(void);

// Get priority system statistics
bool request_priority_get_stats(priority_stats_t *stats);
```

### **Phase 2: Request Processing Tasks (Week 1)**

**Task Implementation:**
- **Critical Request Processor**: Handle EMERGENCY and IO_CRITICAL
- **Normal Request Processor**: Handle AUTHENTICATION and UI_CRITICAL  
- **Background Request Processor**: Handle NORMAL and BACKGROUND

**Load Protection:**
- **Processing Time Monitoring**: Track time per request
- **Automatic Timeouts**: Cancel requests exceeding limits
- **Watchdog Integration**: Feed watchdog during long operations

### **Phase 3: Web Server Integration (Week 2)**

**Modify `web_server_manager.c`:**
```c
// Replace direct handler registration with priority wrapper
static esp_err_t register_priority_handlers(void);

// Wrapper function for all HTTP requests
static esp_err_t priority_request_handler(httpd_req_t *req);

// Integration with existing statistics
static void update_priority_stats(request_priority_t priority, bool success);
```

**Controller Updates:**
- **Add Priority Hints**: Controllers can suggest priority levels
- **Response Optimization**: Fast-path for critical requests
- **Error Handling**: Priority-aware error responses

### **Phase 4: Advanced Features (Week 2)**

**Dynamic Priority Adjustment:**
```c
// Adjust priority based on system load
request_priority_t request_priority_adjust_for_load(request_priority_t base_priority);

// Emergency mode - only critical requests
void request_priority_enter_emergency_mode(void);

// Load shedding - drop low-priority requests
void request_priority_enable_load_shedding(bool enable);
```

**Performance Monitoring:**
```c
typedef struct {
    uint32_t requests_by_priority[REQUEST_PRIORITY_MAX];
    uint32_t average_processing_time[REQUEST_PRIORITY_MAX];
    uint32_t queue_depth[REQUEST_PRIORITY_MAX];
    uint32_t dropped_requests;
    uint32_t timeout_requests;
} priority_stats_t;
```

## **PSRAM Memory Optimization Strategy**

### **Current PSRAM Infrastructure Analysis**

SNRv9 already has a sophisticated PSRAM management system that provides:

- **8MB PSRAM detected** (4MB mapped due to ESP32 virtual address limitations)
- **Smart allocation strategies** with priority-based memory selection
- **Task creation framework** with PSRAM stack allocation
- **Thread-safe operations** with comprehensive statistics tracking

### **PSRAM Integration for Request Priority Management**

#### **1. Request Queue Storage in PSRAM**

**Priority Queues → PSRAM:**
```c
// Allocate request queues in PSRAM for maximum capacity
typedef struct {
    queued_request_t *requests;     // PSRAM-allocated array
    uint16_t max_capacity;          // Larger capacity possible with PSRAM
    uint16_t head, tail, count;     // Metadata in internal RAM
    SemaphoreHandle_t mutex;        // Critical sync in internal RAM
} priority_queue_t;

// Initialize with PSRAM allocation
bool init_priority_queue(priority_queue_t *queue, uint16_t capacity) {
    // Use ALLOC_LARGE_BUFFER to prefer PSRAM for queue storage
    queue->requests = psram_smart_malloc(
        capacity * sizeof(queued_request_t), 
        ALLOC_LARGE_BUFFER
    );
    queue->max_capacity = capacity;
    return queue->requests != NULL;
}
```

**Benefits:**
- **10x larger queues**: 1000+ requests vs 100 with internal RAM
- **Reduced memory pressure**: Keep internal RAM for critical operations
- **Better performance**: More buffering capacity during load spikes

#### **2. Request Processing Tasks with PSRAM Stacks**

**Task Configuration for PSRAM Stacks:**
```c
// Critical request processor - internal RAM for speed
psram_task_config_t critical_task_config = {
    .task_function = request_processor_critical_task,
    .task_name = "req_critical",
    .stack_size = 4096,
    .priority = 10,
    .use_psram = false,           // Keep in internal RAM for speed
    .force_internal = true        // Critical operations need fast access
};

// Normal request processor - PSRAM stack for capacity
psram_task_config_t normal_task_config = {
    .task_function = request_processor_normal_task,
    .task_name = "req_normal",
    .stack_size = 8192,           // Larger stack possible with PSRAM
    .priority = 5,
    .use_psram = true,            // Use PSRAM for larger stack
    .force_internal = false
};

// Background processor - PSRAM for maximum capacity
psram_task_config_t background_task_config = {
    .task_function = request_processor_background_task,
    .task_name = "req_background",
    .stack_size = 12288,          // Very large stack for complex operations
    .priority = 2,
    .use_psram = true,
    .force_internal = false
};
```

#### **3. Request Context and Buffer Management**

**Large Request Buffers → PSRAM:**
```c
typedef struct {
    httpd_req_t *request;           // Internal RAM - fast access
    request_priority_t priority;    // Internal RAM - metadata
    uint32_t timestamp;             // Internal RAM - metadata
    
    // Large buffers in PSRAM
    char *request_buffer;           // PSRAM - large HTTP request data
    char *response_buffer;          // PSRAM - large HTTP response data
    void *processing_context;       // PSRAM - complex processing data
} request_context_t;

// Allocate request context with PSRAM buffers
request_context_t* create_request_context(size_t buffer_size) {
    request_context_t *ctx = psram_smart_malloc(
        sizeof(request_context_t), ALLOC_NORMAL
    );
    
    if (ctx) {
        // Large buffers in PSRAM
        ctx->request_buffer = psram_smart_malloc(buffer_size, ALLOC_LARGE_BUFFER);
        ctx->response_buffer = psram_smart_malloc(buffer_size, ALLOC_LARGE_BUFFER);
        ctx->processing_context = psram_smart_malloc(
            sizeof(complex_processing_data_t), ALLOC_CACHE
        );
    }
    
    return ctx;
}
```

#### **4. Statistics and Monitoring Data in PSRAM**

**Performance Tracking → PSRAM:**
```c
typedef struct {
    // Metadata in internal RAM for fast access
    uint32_t current_index;
    uint32_t total_samples;
    SemaphoreHandle_t mutex;
    
    // Large historical data in PSRAM
    request_timing_sample_t *timing_history;    // PSRAM array
    queue_depth_sample_t *queue_history;       // PSRAM array
    load_metric_sample_t *load_history;        // PSRAM array
} priority_statistics_t;

// Initialize with PSRAM for historical data
bool init_priority_statistics(priority_statistics_t *stats) {
    stats->timing_history = psram_smart_malloc(
        MAX_HISTORY_SAMPLES * sizeof(request_timing_sample_t),
        ALLOC_CACHE
    );
    // ... allocate other history arrays in PSRAM
}
```

### **Memory Allocation Strategy**

#### **Internal RAM (Fast Access):**
- **Critical Metadata**: Queue pointers, mutexes, task handles
- **Hot Path Data**: Current request being processed
- **System Structures**: FreeRTOS primitives, interrupt handlers
- **Emergency Operations**: Safety shutdowns, critical alarms

#### **PSRAM (Large Capacity):**
- **Request Queues**: Large arrays of queued requests
- **Task Stacks**: Processing task stacks (8KB+ each)
- **Request Buffers**: HTTP request/response data
- **Historical Data**: Performance metrics, timing samples
- **Cache Data**: Processed results, lookup tables

### **Performance Optimization Techniques**

#### **1. Hybrid Data Structures:**
```c
typedef struct {
    // Hot data in internal RAM
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
    SemaphoreHandle_t mutex;
    
    // Cold data in PSRAM
    queued_request_t *queue_data;   // Large array in PSRAM
    uint32_t capacity;
    statistics_t *stats;            // Historical data in PSRAM
} hybrid_priority_queue_t;
```

#### **2. Prefetching Strategy:**
```c
// Copy critical request data to internal RAM for processing
void process_request_optimized(queued_request_t *psram_request) {
    // Copy to internal RAM for fast processing
    request_metadata_t local_metadata;
    memcpy(&local_metadata, &psram_request->metadata, sizeof(local_metadata));
    
    // Process using fast internal RAM copy
    process_request_metadata(&local_metadata);
    
    // Write results back to PSRAM
    memcpy(&psram_request->results, &local_metadata.results, sizeof(results_t));
}
```

#### **3. Cache-Friendly Access Patterns:**
```c
// Batch operations to minimize PSRAM access overhead
void process_request_batch(priority_queue_t *queue) {
    const uint32_t BATCH_SIZE = 8;
    queued_request_t batch[BATCH_SIZE];
    
    // Copy batch from PSRAM to internal RAM
    uint32_t count = dequeue_batch(queue, batch, BATCH_SIZE);
    
    // Process entire batch in internal RAM
    for (uint32_t i = 0; i < count; i++) {
        process_request_fast(&batch[i]);
    }
    
    // Write results back to PSRAM if needed
    update_results_batch(queue, batch, count);
}
```

### **Memory Usage Projections**

#### **Without PSRAM (Internal RAM Only):**
- **Request Queues**: ~20KB (limited capacity)
- **Task Stacks**: ~16KB (3 tasks × 4KB + overhead)
- **Buffers**: ~8KB (small request/response buffers)
- **Total**: ~44KB of precious internal RAM

#### **With PSRAM Optimization:**
- **Internal RAM**: ~8KB (metadata, mutexes, hot data)
- **PSRAM**: ~200KB (queues, stacks, buffers, history)
- **Internal RAM Savings**: ~36KB (82% reduction!)

### **Implementation Benefits**

#### **1. Massive Capacity Increase:**
- **Queue Capacity**: 100 → 1000+ requests per priority level
- **Task Stacks**: 4KB → 12KB (3x larger for complex operations)
- **Request Buffers**: 1KB → 16KB (handle large file uploads)

#### **2. System Reliability:**
- **Internal RAM Pressure**: Dramatically reduced
- **Memory Fragmentation**: PSRAM handles large allocations
- **System Stability**: More headroom for critical operations

#### **3. Performance Characteristics:**
- **PSRAM Access**: ~40ns (acceptable for queued operations)
- **Internal RAM**: ~10ns (reserved for hot paths)
- **Overall**: Optimal balance of speed and capacity

## **ESP32-Specific Optimizations**

### **FreeRTOS Integration:**
- **Task Affinity**: Pin critical tasks to specific cores
- **Priority Inheritance**: Prevent priority inversion
- **Semaphore Optimization**: Fast semaphores for queue access
- **PSRAM Task Creation**: Use `psram_create_task()` for optimal stack allocation

### **Hardware Considerations:**
- **Dual-Core Utilization**: Distribute processing across cores
- **Interrupt Handling**: Minimize interrupt latency for IO operations
- **Cache Optimization**: Optimize data structures for cache efficiency
- **PSRAM Integration**: Leverage existing PSRAM manager for all large allocations

## **Success Criteria and Testing**

### **Performance Metrics:**
- **Response Time**: <100ms for EMERGENCY, <500ms for IO_CRITICAL
- **Throughput**: Handle 50+ concurrent requests without degradation
- **Reliability**: Zero watchdog timeouts under normal load
- **Fairness**: Background requests complete within reasonable time

### **Load Testing Scenarios:**
1. **High-Frequency IO Operations**: Rapid relay switching
2. **Concurrent Dashboard Access**: Multiple users viewing status
3. **File Upload During Operations**: Large file transfer with IO active
4. **Emergency Scenarios**: Emergency stop during heavy load

### **Integration Testing:**
- **Existing Controller Compatibility**: All current APIs work unchanged
- **Monitoring Integration**: Priority stats visible in system monitoring
- **Error Handling**: Graceful degradation under extreme load

## **Implementation Timeline**

**Week 1:**
- Day 1-2: Core priority infrastructure and request classification
- Day 3-4: Request queue implementation and basic processing tasks
- Day 5: Initial testing and debugging

**Week 2:**
- Day 1-2: Web server manager integration
- Day 3-4: Controller updates and advanced features
- Day 5: Comprehensive testing and performance optimization

## **Integration with SNRv9 Architecture**

### **Monitoring System Integration:**
- **Task Tracking**: Priority processing tasks monitored by existing task tracker
- **Memory Monitoring**: Queue memory usage tracked by memory monitor
- **Debug Configuration**: Priority system debug output controlled by debug_config.h
- **Statistics Integration**: Priority stats included in web server statistics

### **Component Architecture:**
- **Web Component**: Request priority manager fits cleanly in components/web/
- **Core Dependencies**: Leverages existing core monitoring and PSRAM systems
- **Storage Integration**: Priority configuration can be stored via storage manager
- **Network Integration**: Works with existing WiFi and web server foundation

### **Production Readiness:**
- **Thread Safety**: Full mutex protection following established patterns
- **Error Handling**: Comprehensive error checking and graceful degradation
- **Resource Management**: Proper cleanup and resource limits
- **Documentation**: Complete integration with memory bank documentation system

This implementation will transform the web server from a simple request handler into a sophisticated, production-grade system capable of handling real-time industrial automation requirements while maintaining the reliability and monitoring capabilities already established in SNRv9.

## **Debug Configuration Strategy**

### **Integration with Existing Debug System**

SNRv9 already has a comprehensive centralized debug configuration system in `components/web/include/debug_config.h`. For Step 7's Request Priority Management, we'll extend this existing system to include comprehensive debugging measures.

### **New Debug Flags for Request Priority Management**

**Core Priority System Debug Flags:**
```c
/* =============================================================================
 * REQUEST PRIORITY MANAGEMENT DEBUG CONFIGURATION
 * =============================================================================
 */

/**
 * @brief Enable/disable request priority management debug output
 * Set to 1 to enable priority system debugging, 0 to disable
 */
#define DEBUG_REQUEST_PRIORITY 1

/**
 * @brief Enable/disable detailed request classification logging
 * Set to 1 to log every request classification decision, 0 to disable
 */
#define DEBUG_REQUEST_CLASSIFICATION 1

/**
 * @brief Enable/disable queue management debug output
 * Set to 1 to enable queue depth and operation logging, 0 to disable
 */
#define DEBUG_QUEUE_MANAGEMENT 1

/**
 * @brief Enable/disable request processing timing
 * Set to 1 to log processing times for each request, 0 to disable
 */
#define DEBUG_REQUEST_TIMING 1

/**
 * @brief Enable/disable load balancing debug output
 * Set to 1 to enable load balancing decision logging, 0 to disable
 */
#define DEBUG_LOAD_BALANCING 1

/**
 * @brief Enable/disable PSRAM allocation tracking for priority system
 * Set to 1 to track PSRAM usage by priority components, 0 to disable
 */
#define DEBUG_PRIORITY_PSRAM 1

/**
 * @brief Enable/disable emergency mode debug output
 * Set to 1 to log emergency mode transitions, 0 to disable
 */
#define DEBUG_EMERGENCY_MODE 1
```

**Timing and Reporting Configuration:**
```c
/**
 * @brief Priority system statistics report interval in milliseconds
 * How often to output priority system statistics to serial
 */
#define DEBUG_PRIORITY_REPORT_INTERVAL_MS 15000

/**
 * @brief Queue depth monitoring interval in milliseconds
 * How often to check and report queue depths
 */
#define DEBUG_QUEUE_MONITOR_INTERVAL_MS 5000

/**
 * @brief Request timing threshold in milliseconds
 * Log requests that take longer than this threshold
 */
#define DEBUG_SLOW_REQUEST_THRESHOLD_MS 1000

/**
 * @brief Maximum number of timing samples to store
 * For performance analysis and trending
 */
#define DEBUG_TIMING_HISTORY_SIZE 50
```

**Debug Tags for Priority System:**
```c
/**
 * @brief Debug output tag for request priority manager
 */
#define DEBUG_PRIORITY_MANAGER_TAG "REQ_PRIORITY"

/**
 * @brief Debug output tag for request queues
 */
#define DEBUG_QUEUE_TAG "REQ_QUEUE"

/**
 * @brief Debug output tag for request classification
 */
#define DEBUG_CLASSIFICATION_TAG "REQ_CLASS"

/**
 * @brief Debug output tag for load balancing
 */
#define DEBUG_LOAD_BALANCE_TAG "LOAD_BAL"

/**
 * @brief Debug output tag for emergency operations
 */
#define DEBUG_EMERGENCY_TAG "EMERGENCY"
```

### **Implementation Strategy**

#### **1. Conditional Compilation Patterns**

Following the existing SNRv9 pattern, we'll use conditional compilation throughout the priority management code:

```c
// Example from request classification
#if DEBUG_REQUEST_CLASSIFICATION
    ESP_LOGI(DEBUG_CLASSIFICATION_TAG, "Request %s classified as %s (URI: %s)", 
             req_id, priority_to_string(priority), req->uri);
#endif

// Example from queue management
#if DEBUG_QUEUE_MANAGEMENT
    ESP_LOGD(DEBUG_QUEUE_TAG, "Queue %s: depth=%d/%d, enqueue_time=%dms", 
             queue_name, current_depth, max_depth, enqueue_time);
#endif

// Example from timing measurement
#if DEBUG_REQUEST_TIMING
    if (processing_time > DEBUG_SLOW_REQUEST_THRESHOLD_MS) {
        ESP_LOGW(DEBUG_PRIORITY_MANAGER_TAG, "Slow request: %dms for %s priority", 
                 processing_time, priority_to_string(priority));
    }
#endif
```

#### **2. Performance-Safe Debug Macros**

Zero performance impact when disabled:

```c
#if DEBUG_REQUEST_PRIORITY && DEBUG_INCLUDE_TIMESTAMPS
#define PRIORITY_DEBUG_LOG(tag, format, ...) \
    ESP_LOGI(tag, "[%lu] " format, esp_timer_get_time()/1000, ##__VA_ARGS__)
#elif DEBUG_REQUEST_PRIORITY
#define PRIORITY_DEBUG_LOG(tag, format, ...) \
    ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define PRIORITY_DEBUG_LOG(tag, format, ...) do {} while(0)
#endif

#if DEBUG_QUEUE_MANAGEMENT
#define QUEUE_DEBUG(format, ...) \
    ESP_LOGD(DEBUG_QUEUE_TAG, format, ##__VA_ARGS__)
#else
#define QUEUE_DEBUG(format, ...) do {} while(0)
#endif

#if DEBUG_LOAD_BALANCING
#define LOAD_BALANCE_DEBUG(format, ...) \
    ESP_LOGD(DEBUG_LOAD_BALANCE_TAG, format, ##__VA_ARGS__)
#else
#define LOAD_BALANCE_DEBUG(format, ...) do {} while(0)
#endif
```

#### **3. Statistics Collection Control**

Debug statistics only compiled when needed:

```c
#if DEBUG_REQUEST_TIMING
typedef struct {
    uint32_t request_count;
    uint32_t total_processing_time;
    uint32_t min_processing_time;
    uint32_t max_processing_time;
    uint32_t slow_request_count;
    uint32_t timeout_count;
} priority_debug_stats_t;

// Only compile statistics collection when debugging enabled
static priority_debug_stats_t debug_stats[REQUEST_PRIORITY_MAX];

#define UPDATE_TIMING_STATS(priority, time_ms) \
    do { \
        debug_stats[priority].request_count++; \
        debug_stats[priority].total_processing_time += time_ms; \
        if (time_ms < debug_stats[priority].min_processing_time || \
            debug_stats[priority].min_processing_time == 0) { \
            debug_stats[priority].min_processing_time = time_ms; \
        } \
        if (time_ms > debug_stats[priority].max_processing_time) { \
            debug_stats[priority].max_processing_time = time_ms; \
        } \
        if (time_ms > DEBUG_SLOW_REQUEST_THRESHOLD_MS) { \
            debug_stats[priority].slow_request_count++; \
        } \
    } while(0)
#else
#define UPDATE_TIMING_STATS(priority, time_ms) do {} while(0)
#endif
```

#### **4. Queue Debug Monitoring**

```c
#if DEBUG_QUEUE_MANAGEMENT
static void debug_print_queue_status(void) {
    for (int i = 0; i < REQUEST_PRIORITY_MAX; i++) {
        priority_queue_t *queue = &priority_queues[i];
        ESP_LOGD(DEBUG_QUEUE_TAG, "Priority %d: %d/%d requests queued", 
                 i, queue->count, queue->max_capacity);
    }
}

#define QUEUE_STATUS_DEBUG() debug_print_queue_status()
#else
#define QUEUE_STATUS_DEBUG() do {} while(0)
#endif
```

#### **5. PSRAM Allocation Tracking**

```c
#if DEBUG_PRIORITY_PSRAM
static void debug_track_psram_allocation(const char* component, size_t size, void* ptr) {
    if (psram_is_psram_ptr(ptr)) {
        ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "PSRAM alloc: %s = %zu bytes at %p", 
                 component, size, ptr);
    } else {
        ESP_LOGD(DEBUG_PRIORITY_MANAGER_TAG, "Internal RAM alloc: %s = %zu bytes at %p", 
                 component, size, ptr);
    }
}

#define TRACK_PSRAM_ALLOC(component, size, ptr) debug_track_psram_allocation(component, size, ptr)
#else
#define TRACK_PSRAM_ALLOC(component, size, ptr) do {} while(0)
#endif
```

#### **6. Emergency Mode Debug Logging**

```c
#if DEBUG_EMERGENCY_MODE
static void debug_log_emergency_transition(bool entering_emergency) {
    if (entering_emergency) {
        ESP_LOGW(DEBUG_EMERGENCY_TAG, "ENTERING EMERGENCY MODE - Only critical requests will be processed");
    } else {
        ESP_LOGI(DEBUG_EMERGENCY_TAG, "EXITING EMERGENCY MODE - Normal request processing resumed");
    }
}

#define EMERGENCY_MODE_DEBUG(entering) debug_log_emergency_transition(entering)
#else
#define EMERGENCY_MODE_DEBUG(entering) do {} while(0)
#endif
```

### **Production Safety Features**

#### **1. Zero Performance Impact**
- When debug flags are disabled, no debug code is compiled
- Macros expand to empty statements with no runtime cost
- No memory allocation for debug structures when disabled

#### **2. Memory Efficient**
- Debug data structures only exist when debugging is enabled
- Configurable history sizes to control memory usage
- PSRAM allocation for large debug datasets

#### **3. Thread Safe**
- All debug output uses thread-safe ESP-IDF logging
- Debug statistics protected by existing mutexes
- No additional synchronization overhead

#### **4. Configurable Verbosity**
- Different debug levels for different components
- Timing thresholds to focus on performance issues
- Separate flags for different aspects of the system

### **Integration with Existing Debug Infrastructure**

#### **Leverage Existing Patterns:**
- **Timestamp Support**: Use existing `DEBUG_INCLUDE_TIMESTAMPS` flag
- **Tag System**: Follow established tag naming conventions (`DEBUG_*_TAG`)
- **Interval Configuration**: Use similar timing patterns as memory/task monitoring
- **Component Isolation**: Each component maintains its own debug_config.h copy

#### **Extend Current Capabilities:**
- **Priority-Specific Monitoring**: Track performance by request priority
- **Queue Health Monitoring**: Real-time queue depth and performance tracking
- **Load Balancing Insights**: Visibility into load balancing decisions
- **Emergency Mode Tracking**: Log critical system state transitions

### **Debug Output Examples**

**Request Classification Debug:**
```
[12:34:56.789] REQ_CLASS: Request req_001 classified as IO_CRITICAL (URI: /api/io/points/relay_01/set)
[12:34:56.790] REQ_QUEUE: Enqueued req_001 to IO_CRITICAL queue (depth: 3/100)
```

**Performance Timing Debug:**
```
[12:34:56.850] REQ_PRIORITY: Slow request: 1250ms for NORMAL priority (URI: /api/logs/download)
[12:34:56.851] LOAD_BAL: Moving NORMAL requests to background processor due to high load
```

**PSRAM Allocation Debug:**
```
[12:34:56.123] REQ_PRIORITY: PSRAM alloc: priority_queue_normal = 8192 bytes at 0x3F800000
[12:34:56.124] REQ_PRIORITY: Internal RAM alloc: queue_metadata = 64 bytes at 0x3FFB0000
```

This comprehensive debug configuration strategy ensures that Step 7's implementation will have full visibility into system behavior during development while maintaining zero overhead in production builds, following the established SNRv9 patterns for industrial-grade reliability.

## **Next Steps**

1. **Review and Approval**: Validate this implementation plan against project requirements
2. **Phase 1 Implementation**: Begin with core priority infrastructure and debug configuration
3. **Incremental Testing**: Test each phase thoroughly with comprehensive debug output
4. **Integration Validation**: Ensure compatibility with existing systems and debug infrastructure
5. **Performance Optimization**: Fine-tune for ESP32 hardware constraints using debug metrics
6. **Documentation Update**: Update memory bank with implementation patterns and debug strategies

The request priority management system represents a critical step toward production-ready industrial automation capabilities, ensuring reliable real-time performance under all operating conditions with comprehensive debugging support for development and maintenance.
