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

## **Next Steps**

1. **Review and Approval**: Validate this implementation plan against project requirements
2. **Phase 1 Implementation**: Begin with core priority infrastructure
3. **Incremental Testing**: Test each phase thoroughly before proceeding
4. **Integration Validation**: Ensure compatibility with existing systems
5. **Performance Optimization**: Fine-tune for ESP32 hardware constraints
6. **Documentation Update**: Update memory bank with implementation patterns and decisions

The request priority management system represents a critical step toward production-ready industrial automation capabilities, ensuring reliable real-time performance under all operating conditions.
