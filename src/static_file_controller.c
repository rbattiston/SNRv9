/**
 * @file static_file_controller.c
 * @brief Static File Controller implementation for SNRv9 Irrigation Control System
 */

#include "static_file_controller.h"
#include "debug_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * PRIVATE CONSTANTS AND MACROS
 * =============================================================================
 */

#if DEBUG_INCLUDE_TIMESTAMPS
#define TIMESTAMP_FORMAT "[%02d:%02d:%02d.%03d] "
#define GET_TIMESTAMP() esp_timer_get_time() / 1000ULL
#define FORMAT_TIMESTAMP(ms) \
    (int)((ms / 3600000) % 24), \
    (int)((ms / 60000) % 60), \
    (int)((ms / 1000) % 60), \
    (int)(ms % 1000)
#else
#define TIMESTAMP_FORMAT ""
#define GET_TIMESTAMP() 0
#define FORMAT_TIMESTAMP(ms) 
#endif

/* =============================================================================
 * PRIVATE TYPE DEFINITIONS
 * =============================================================================
 */

typedef struct {
    static_file_stats_t stats;
    SemaphoreHandle_t stats_mutex;
    cache_config_t cache_config;
    cache_entry_t cache_entries[16];  // Small cache for ESP32
    uint32_t cache_entry_count;
    SemaphoreHandle_t cache_mutex;
    bool initialized;
} static_file_context_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * =============================================================================
 */

static static_file_context_t g_static_file = {0};
static const char *TAG = "STATIC_FILE";

/* Advanced MIME type mappings with caching and compression settings */
static const mime_type_mapping_t g_mime_mappings[] = {
    // Text files - compressible, short cache for dynamic content
    {".html", "text/html", false, true, 300},                    // 5 minutes
    {".htm",  "text/html", false, true, 300},                    // 5 minutes
    {".txt",  "text/plain", false, true, 300},                   // 5 minutes
    {".xml",  "application/xml", false, true, 300},              // 5 minutes
    {".json", "application/json", false, true, 60},              // 1 minute (API responses)
    
    // Static assets - compressible, long cache
    {".css",  "text/css", true, true, 86400},                    // 24 hours
    {".js",   "application/javascript", true, true, 86400},      // 24 hours
    {".svg",  "image/svg+xml", true, true, 86400},               // 24 hours
    
    // Images - not compressible (already compressed), long cache
    {".ico",  "image/x-icon", true, false, 604800},              // 7 days
    {".png",  "image/png", true, false, 604800},                 // 7 days
    {".jpg",  "image/jpeg", true, false, 604800},                // 7 days
    {".jpeg", "image/jpeg", true, false, 604800},                // 7 days
    {".gif",  "image/gif", true, false, 604800},                 // 7 days
    {".webp", "image/webp", true, false, 604800},                // 7 days
    
    // Fonts - not compressible, very long cache
    {".woff", "font/woff", true, false, 2592000},                // 30 days
    {".woff2", "font/woff2", true, false, 2592000},              // 30 days
    {".ttf",  "font/ttf", true, false, 2592000},                 // 30 days
    {".otf",  "font/otf", true, false, 2592000},                 // 30 days
    
    {NULL, NULL, false, false, 0}  // Sentinel
};

/* =============================================================================
 * EMBEDDED CONTENT
 * =============================================================================
 */

/* Main Dashboard HTML with comprehensive debugging */
static const char dashboard_html[] = 
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>SNRv9 Irrigation Control System</title>\n"
"    <style>\n"
"        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
"        body {\n"
"            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n"
"            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
"            color: #333; min-height: 100vh; padding: 20px;\n"
"        }\n"
"        .container {\n"
"            max-width: 1200px; margin: 0 auto;\n"
"            background: rgba(255,255,255,0.95);\n"
"            border-radius: 15px; padding: 30px;\n"
"            box-shadow: 0 20px 40px rgba(0,0,0,0.1);\n"
"        }\n"
"        .header {\n"
"            text-align: center; margin-bottom: 30px;\n"
"            border-bottom: 3px solid #667eea; padding-bottom: 20px;\n"
"        }\n"
"        .header h1 {\n"
"            color: #667eea; font-size: 2.5em; margin-bottom: 10px;\n"
"            text-shadow: 2px 2px 4px rgba(0,0,0,0.1);\n"
"        }\n"
"        .status-grid {\n"
"            display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));\n"
"            gap: 20px; margin-bottom: 30px;\n"
"        }\n"
"        .status-card {\n"
"            background: #f8f9fa; border-radius: 10px; padding: 20px;\n"
"            border-left: 5px solid #667eea;\n"
"            box-shadow: 0 5px 15px rgba(0,0,0,0.08);\n"
"            transition: transform 0.3s ease;\n"
"        }\n"
"        .status-card:hover { transform: translateY(-5px); }\n"
"        .status-card h3 {\n"
"            color: #667eea; margin-bottom: 15px;\n"
"            font-size: 1.3em; display: flex; align-items: center;\n"
"        }\n"
"        .status-item {\n"
"            display: flex; justify-content: space-between;\n"
"            margin-bottom: 10px; padding: 8px 0;\n"
"            border-bottom: 1px solid #e9ecef;\n"
"        }\n"
"        .status-item:last-child { border-bottom: none; }\n"
"        .status-label { font-weight: 600; color: #495057; }\n"
"        .status-value {\n"
"            font-weight: bold; color: #28a745;\n"
"            font-family: 'Courier New', monospace;\n"
"        }\n"
"        .debug-section {\n"
"            background: #f1f3f4; border-radius: 10px; padding: 20px;\n"
"            margin-top: 20px; border: 2px solid #dee2e6;\n"
"        }\n"
"        .debug-section h3 {\n"
"            color: #dc3545; margin-bottom: 15px;\n"
"            font-size: 1.2em;\n"
"        }\n"
"        .debug-log {\n"
"            background: #2d3748; color: #e2e8f0;\n"
"            padding: 15px; border-radius: 8px;\n"
"            font-family: 'Courier New', monospace;\n"
"            font-size: 0.9em; max-height: 200px;\n"
"            overflow-y: auto; white-space: pre-wrap;\n"
"        }\n"
"        .error { color: #dc3545 !important; }\n"
"        .success { color: #28a745 !important; }\n"
"        .warning { color: #ffc107 !important; }\n"
"        .loading {\n"
"            display: inline-block; width: 20px; height: 20px;\n"
"            border: 3px solid #f3f3f3;\n"
"            border-top: 3px solid #667eea;\n"
"            border-radius: 50%; animation: spin 1s linear infinite;\n"
"        }\n"
"        @keyframes spin {\n"
"            0% { transform: rotate(0deg); }\n"
"            100% { transform: rotate(360deg); }\n"
"        }\n"
"        .refresh-btn {\n"
"            background: #667eea; color: white; border: none;\n"
"            padding: 12px 24px; border-radius: 8px;\n"
"            cursor: pointer; font-size: 1em; font-weight: 600;\n"
"            transition: background 0.3s ease;\n"
"            margin: 10px 5px;\n"
"        }\n"
"        .refresh-btn:hover { background: #5a6fd8; }\n"
"        .refresh-btn:disabled {\n"
"            background: #6c757d; cursor: not-allowed;\n"
"        }\n"
"        @media (max-width: 768px) {\n"
"            .container { padding: 15px; }\n"
"            .header h1 { font-size: 2em; }\n"
"            .status-grid { grid-template-columns: 1fr; }\n"
"        }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <div class=\"header\">\n"
"            <h1>🌱 SNRv9 Irrigation Control</h1>\n"
"            <p>Real-time System Monitoring Dashboard</p>\n"
"            <div style=\"margin-top: 15px;\">\n"
"                <button class=\"refresh-btn\" onclick=\"refreshData()\">🔄 Refresh Data</button>\n"
"                <button class=\"refresh-btn\" onclick=\"toggleAutoRefresh()\">⏱️ Auto Refresh: <span id=\"autoStatus\">ON</span></button>\n"
"                <button class=\"refresh-btn\" onclick=\"clearDebugLog()\">🗑️ Clear Debug</button>\n"
"            </div>\n"
"        </div>\n"
"\n"
"        <div class=\"status-grid\">\n"
"            <div class=\"status-card\">\n"
"                <h3>🖥️ System Information</h3>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">System Name:</span>\n"
"                    <span class=\"status-value\" id=\"systemName\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Version:</span>\n"
"                    <span class=\"status-value\" id=\"systemVersion\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Uptime:</span>\n"
"                    <span class=\"status-value\" id=\"systemUptime\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Last Update:</span>\n"
"                    <span class=\"status-value\" id=\"lastUpdate\">Never</span>\n"
"                </div>\n"
"            </div>\n"
"\n"
"            <div class=\"status-card\">\n"
"                <h3>🌐 Web Server Status</h3>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Status:</span>\n"
"                    <span class=\"status-value\" id=\"serverStatus\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Port:</span>\n"
"                    <span class=\"status-value\" id=\"serverPort\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Total Requests:</span>\n"
"                    <span class=\"status-value\" id=\"totalRequests\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Success Rate:</span>\n"
"                    <span class=\"status-value\" id=\"successRate\">Loading...</span>\n"
"                </div>\n"
"            </div>\n"
"\n"
"            <div class=\"status-card\">\n"
"                <h3>💾 Memory Usage</h3>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Free Heap:</span>\n"
"                    <span class=\"status-value\" id=\"freeHeap\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Min Free Heap:</span>\n"
"                    <span class=\"status-value\" id=\"minFreeHeap\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Memory Usage:</span>\n"
"                    <span class=\"status-value\" id=\"memoryUsage\">Loading...</span>\n"
"                </div>\n"
"                <div class=\"status-item\">\n"
"                    <span class=\"status-label\">Health Status:</span>\n"
"                    <span class=\"status-value\" id=\"memoryHealth\">Loading...</span>\n"
"                </div>\n"
"            </div>\n"
"        </div>\n"
"\n"
"        <div class=\"debug-section\">\n"
"            <h3>🔧 Debug Information & Client Logs</h3>\n"
"            <div class=\"debug-log\" id=\"debugLog\">Dashboard initialized. Waiting for data...\n</div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <script>\n"
"        let autoRefresh = true;\n"
"        let refreshInterval;\n"
"        let requestCount = 0;\n"
"        let errorCount = 0;\n"
"\n"
"        function debugLog(message, type = 'info') {\n"
"            const timestamp = new Date().toLocaleTimeString();\n"
"            const logElement = document.getElementById('debugLog');\n"
"            const typePrefix = {\n"
"                'info': '[INFO]',\n"
"                'error': '[ERROR]',\n"
"                'warning': '[WARN]',\n"
"                'success': '[SUCCESS]'\n"
"            };\n"
"            \n"
"            const logEntry = `${timestamp} ${typePrefix[type]} ${message}\n`;\n"
"            logElement.textContent += logEntry;\n"
"            logElement.scrollTop = logElement.scrollHeight;\n"
"            \n"
"            // Keep only last 50 lines\n"
"            const lines = logElement.textContent.split('\n');\n"
"            if (lines.length > 50) {\n"
"                logElement.textContent = lines.slice(-50).join('\n');\n"
"            }\n"
"        }\n"
"\n"
"        function formatBytes(bytes) {\n"
"            if (bytes === 0) return '0 B';\n"
"            const k = 1024;\n"
"            const sizes = ['B', 'KB', 'MB', 'GB'];\n"
"            const i = Math.floor(Math.log(bytes) / Math.log(k));\n"
"            return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];\n"
"        }\n"
"\n"
"        function formatUptime(ms) {\n"
"            const seconds = Math.floor(ms / 1000);\n"
"            const minutes = Math.floor(seconds / 60);\n"
"            const hours = Math.floor(minutes / 60);\n"
"            const days = Math.floor(hours / 24);\n"
"            \n"
"            if (days > 0) return `${days}d ${hours % 24}h ${minutes % 60}m`;\n"
"            if (hours > 0) return `${hours}h ${minutes % 60}m ${seconds % 60}s`;\n"
"            if (minutes > 0) return `${minutes}m ${seconds % 60}s`;\n"
"            return `${seconds}s`;\n"
"        }\n"
"\n"
"        function updateStatus(data) {\n"
"            try {\n"
"                debugLog(`Processing status data: ${JSON.stringify(data).substring(0, 100)}...`, 'info');\n"
"                \n"
"                // System Information\n"
"                document.getElementById('systemName').textContent = data.system.name || 'Unknown';\n"
"                document.getElementById('systemVersion').textContent = data.system.version || 'Unknown';\n"
"                document.getElementById('systemUptime').textContent = formatUptime(data.system.uptime_ms || 0);\n"
"                \n"
"                // Web Server Status\n"
"                const serverStatus = data.web_server.status || 'unknown';\n"
"                const statusElement = document.getElementById('serverStatus');\n"
"                statusElement.textContent = serverStatus.toUpperCase();\n"
"                statusElement.className = 'status-value ' + (serverStatus === 'running' ? 'success' : 'error');\n"
"                \n"
"                document.getElementById('serverPort').textContent = data.web_server.port || 'Unknown';\n"
"                document.getElementById('totalRequests').textContent = data.web_server.total_requests || 0;\n"
"                \n"
"                // Calculate success rate\n"
"                const total = data.web_server.total_requests || 0;\n"
"                const successful = data.web_server.successful_requests || 0;\n"
"                const successRate = total > 0 ? ((successful / total) * 100).toFixed(1) : '100.0';\n"
"                const rateElement = document.getElementById('successRate');\n"
"                rateElement.textContent = `${successRate}%`;\n"
"                rateElement.className = 'status-value ' + (parseFloat(successRate) >= 95 ? 'success' : 'warning');\n"
"                \n"
"                // Memory Information\n"
"                const freeHeap = data.memory.free_heap || 0;\n"
"                const minFreeHeap = data.memory.min_free_heap || 0;\n"
"                \n"
"                document.getElementById('freeHeap').textContent = formatBytes(freeHeap);\n"
"                document.getElementById('minFreeHeap').textContent = formatBytes(minFreeHeap);\n"
"                \n"
"                // Estimate total heap (ESP32 typically has ~320KB)\n"
"                const estimatedTotal = 320 * 1024;\n"
"                const usedHeap = estimatedTotal - freeHeap;\n"
"                const usagePercent = ((usedHeap / estimatedTotal) * 100).toFixed(1);\n"
"                \n"
"                const usageElement = document.getElementById('memoryUsage');\n"
"                usageElement.textContent = `${usagePercent}%`;\n"
"                usageElement.className = 'status-value ' + (parseFloat(usagePercent) < 70 ? 'success' : parseFloat(usagePercent) < 85 ? 'warning' : 'error');\n"
"                \n"
"                // Memory health assessment\n"
"                const healthElement = document.getElementById('memoryHealth');\n"
"                if (freeHeap > 100000) {\n"
"                    healthElement.textContent = 'EXCELLENT';\n"
"                    healthElement.className = 'status-value success';\n"
"                } else if (freeHeap > 50000) {\n"
"                    healthElement.textContent = 'GOOD';\n"
"                    healthElement.className = 'status-value success';\n"
"                } else if (freeHeap > 20000) {\n"
"                    healthElement.textContent = 'WARNING';\n"
"                    healthElement.className = 'status-value warning';\n"
"                } else {\n"
"                    healthElement.textContent = 'CRITICAL';\n"
"                    healthElement.className = 'status-value error';\n"
"                }\n"
"                \n"
"                document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();\n"
"                debugLog(`Status update completed successfully. Memory: ${formatBytes(freeHeap)}, Server: ${serverStatus}`, 'success');\n"
"                \n"
"            } catch (error) {\n"
"                debugLog(`Error updating status: ${error.message}`, 'error');\n"
"                errorCount++;\n"
"            }\n"
"        }\n"
"\n"
"        function refreshData() {\n"
"            const refreshBtn = document.querySelector('.refresh-btn');\n"
"            refreshBtn.disabled = true;\n"
"            refreshBtn.innerHTML = '<span class=\"loading\"></span> Refreshing...';\n"
"            \n"
"            requestCount++;\n"
"            debugLog(`Initiating API request #${requestCount} to /api/status`, 'info');\n"
"            \n"
"            fetch('/api/status')\n"
"                .then(response => {\n"
"                    debugLog(`API response received: ${response.status} ${response.statusText}`, response.ok ? 'success' : 'error');\n"
"                    if (!response.ok) {\n"
"                        throw new Error(`HTTP ${response.status}: ${response.statusText}`);\n"
"                    }\n"
"                    return response.json();\n"
"                })\n"
"                .then(data => {\n"
"                    debugLog('JSON data parsed successfully', 'success');\n"
"                    updateStatus(data);\n"
"                })\n"
"                .catch(error => {\n"
"                    debugLog(`API request failed: ${error.message}`, 'error');\n"
"                    errorCount++;\n"
"                    \n"
"                    // Update UI to show error state\n"
"                    const elements = ['systemName', 'systemVersion', 'serverStatus', 'freeHeap'];\n"
"                    elements.forEach(id => {\n"
"                        const element = document.getElementById(id);\n"
"                        if (element) {\n"
"                            element.textContent = 'ERROR';\n"
"                            element.className = 'status-value error';\n"
"                        }\n"
"                    });\n"
"                })\n"
"                .finally(() => {\n"
"                    refreshBtn.disabled = false;\n"
"                    refreshBtn.innerHTML = '🔄 Refresh Data';\n"
"                    debugLog(`Request #${requestCount} completed. Total errors: ${errorCount}`, 'info');\n"
"                });\n"
"        }\n"
"\n"
"        function toggleAutoRefresh() {\n"
"            autoRefresh = !autoRefresh;\n"
"            const statusElement = document.getElementById('autoStatus');\n"
"            statusElement.textContent = autoRefresh ? 'ON' : 'OFF';\n"
"            \n"
"            if (autoRefresh) {\n"
"                startAutoRefresh();\n"
"                debugLog('Auto-refresh enabled (5 second interval)', 'info');\n"
"            } else {\n"
"                stopAutoRefresh();\n"
"                debugLog('Auto-refresh disabled', 'warning');\n"
"            }\n"
"        }\n"
"\n"
"        function startAutoRefresh() {\n"
"            if (refreshInterval) clearInterval(refreshInterval);\n"
"            refreshInterval = setInterval(refreshData, 5000);\n"
"        }\n"
"\n"
"        function stopAutoRefresh() {\n"
"            if (refreshInterval) {\n"
"                clearInterval(refreshInterval);\n"
"                refreshInterval = null;\n"
"            }\n"
"        }\n"
"\n"
"        function clearDebugLog() {\n"
"            document.getElementById('debugLog').textContent = 'Debug log cleared.\n';\n"
"            requestCount = 0;\n"
"            errorCount = 0;\n"
"            debugLog('Debug log cleared and counters reset', 'info');\n"
"        }\n"
"\n"
"        // Initialize dashboard\n"
"        document.addEventListener('DOMContentLoaded', function() {\n"
"            debugLog('Dashboard DOM loaded, initializing...', 'info');\n"
"            debugLog(`User Agent: ${navigator.userAgent}`, 'info');\n"
"            debugLog(`Screen Resolution: ${screen.width}x${screen.height}`, 'info');\n"
"            debugLog(`Viewport: ${window.innerWidth}x${window.innerHeight}`, 'info');\n"
"            \n"
"            // Initial data load\n"
"            refreshData();\n"
"            \n"
"            // Start auto-refresh\n"
"            if (autoRefresh) {\n"
"                startAutoRefresh();\n"
"            }\n"
"            \n"
"            debugLog('Dashboard initialization complete', 'success');\n"
"        });\n"
"\n"
"        // Handle page visibility changes\n"
"        document.addEventListener('visibilitychange', function() {\n"
"            if (document.hidden) {\n"
"                debugLog('Page hidden, pausing auto-refresh', 'warning');\n"
"                stopAutoRefresh();\n"
"            } else if (autoRefresh) {\n"
"                debugLog('Page visible, resuming auto-refresh', 'info');\n"
"                startAutoRefresh();\n"
"                refreshData(); // Immediate refresh when page becomes visible\n"
"            }\n"
"        });\n"
"    </script>\n"
"</body>\n"
"</html>";

/* =============================================================================
 * PRIVATE FUNCTION DECLARATIONS
 * =============================================================================
 */

static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t file_handler(httpd_req_t *req);
static void update_request_stats(bool success, size_t bytes_served);
static const char* get_file_extension(const char *path);
static esp_err_t serve_file_from_data(httpd_req_t *req, const char *filename);

/* =============================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool static_file_controller_init(void)
{
    if (g_static_file.initialized) {
        ESP_LOGW(TAG, "Static file controller already initialized");
        return false;
    }

    // Create mutex for thread-safe statistics access
    g_static_file.stats_mutex = xSemaphoreCreateMutex();
    if (g_static_file.stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create statistics mutex");
        return false;
    }

    // Create mutex for thread-safe cache access
    g_static_file.cache_mutex = xSemaphoreCreateMutex();
    if (g_static_file.cache_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create cache mutex");
        vSemaphoreDelete(g_static_file.stats_mutex);
        return false;
    }

    // Initialize statistics
    memset(&g_static_file.stats, 0, sizeof(static_file_stats_t));
    
    // Initialize cache configuration with defaults
    g_static_file.cache_config.etag_enabled = true;
    g_static_file.cache_config.conditional_requests = true;
    g_static_file.cache_config.compression_enabled = false;  // Disabled for now (ESP32 CPU constraints)
    g_static_file.cache_config.default_cache_age = STATIC_FILE_CACHE_MAX_AGE;
    g_static_file.cache_config.max_cache_entries = 16;
    
    // Initialize cache entries
    memset(g_static_file.cache_entries, 0, sizeof(g_static_file.cache_entries));
    g_static_file.cache_entry_count = 0;
    
    g_static_file.initialized = true;
    ESP_LOGI(TAG, "Static file controller initialized successfully with advanced caching");
    return true;
}

bool static_file_controller_register_handlers(httpd_handle_t server)
{
    if (!g_static_file.initialized) {
        ESP_LOGE(TAG, "Static file controller not initialized");
        return false;
    }

    if (server == NULL) {
        ESP_LOGE(TAG, "Invalid server handle");
        return false;
    }

    // Register root path handler
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };

    esp_err_t ret = httpd_register_uri_handler(server, &root_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register root handler: %s", esp_err_to_name(ret));
        return false;
    }

    // Register individual file handlers
    httpd_uri_t test_uri = {
        .uri = "/test.html",
        .method = HTTP_GET,
        .handler = file_handler,
        .user_ctx = NULL
    };

    ret = httpd_register_uri_handler(server, &test_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register test.html handler: %s", esp_err_to_name(ret));
        return false;
    }

    httpd_uri_t app_js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = file_handler,
        .user_ctx = NULL
    };

    ret = httpd_register_uri_handler(server, &app_js_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register app.js handler: %s", esp_err_to_name(ret));
        return false;
    }

    httpd_uri_t style_css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = file_handler,
        .user_ctx = NULL
    };

    ret = httpd_register_uri_handler(server, &style_css_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register style.css handler: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Static file handlers registered successfully");
    return true;
}

bool static_file_controller_unregister_handlers(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Invalid server handle");
        return false;
    }

    esp_err_t ret = httpd_unregister_uri_handler(server, "/", HTTP_GET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister root handler: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Static file handlers unregistered successfully");
    return true;
}

bool static_file_controller_get_stats(static_file_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_static_file.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &g_static_file.stats, sizeof(static_file_stats_t));
        xSemaphoreGive(g_static_file.stats_mutex);
        return true;
    }

    return false;
}

void static_file_controller_reset_stats(void)
{
    if (xSemaphoreTake(g_static_file.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&g_static_file.stats, 0, sizeof(static_file_stats_t));
        xSemaphoreGive(g_static_file.stats_mutex);
    }
}

void static_file_controller_print_status(void)
{
    uint32_t timestamp = GET_TIMESTAMP();
    static_file_stats_t stats;
    
    printf(TIMESTAMP_FORMAT "%s: === STATIC FILE CONTROLLER STATUS ===\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
    
    if (static_file_controller_get_stats(&stats)) {
        printf(TIMESTAMP_FORMAT "%s: Total Requests: %lu\n",
               FORMAT_TIMESTAMP(timestamp), TAG, (unsigned long)stats.total_requests);
        
        printf(TIMESTAMP_FORMAT "%s: Successful: %lu, Failed: %lu\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               (unsigned long)stats.successful_requests, (unsigned long)stats.failed_requests);
        
        printf(TIMESTAMP_FORMAT "%s: Cache Hits: %lu, Bytes Served: %lu\n",
               FORMAT_TIMESTAMP(timestamp), TAG,
               (unsigned long)stats.cache_hits, (unsigned long)stats.bytes_served);
    }
    
    printf(TIMESTAMP_FORMAT "%s: =====================================\n", 
           FORMAT_TIMESTAMP(timestamp), TAG);
}

bool static_file_controller_get_mime_type(const char *extension, 
                                         char *mime_type, 
                                         size_t mime_type_size)
{
    if (extension == NULL || mime_type == NULL || mime_type_size == 0) {
        return false;
    }

    for (int i = 0; g_mime_mappings[i].extension != NULL; i++) {
        if (strcmp(extension, g_mime_mappings[i].extension) == 0) {
            strncpy(mime_type, g_mime_mappings[i].mime_type, mime_type_size - 1);
            mime_type[mime_type_size - 1] = '\0';
            return true;
        }
    }

    // Default to text/plain for unknown extensions
    strncpy(mime_type, "text/plain", mime_type_size - 1);
    mime_type[mime_type_size - 1] = '\0';
    return false;
}

bool static_file_controller_is_cacheable(const char *extension)
{
    if (extension == NULL) {
        return false;
    }

    for (int i = 0; g_mime_mappings[i].extension != NULL; i++) {
        if (strcmp(extension, g_mime_mappings[i].extension) == 0) {
            return g_mime_mappings[i].cacheable;
        }
    }

    return false;
}

esp_err_t static_file_controller_serve_embedded(httpd_req_t *req,
                                               const char *content,
                                               size_t content_length,
                                               const char *mime_type,
                                               bool cacheable)
{
    if (req == NULL || content == NULL || mime_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    
    // Set content type
    esp_err_t ret = httpd_resp_set_type(req, mime_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set content type: %s", esp_err_to_name(ret));
        update_request_stats(false, 0);
        return ret;
    }

    // Set caching headers if cacheable
    if (cacheable) {
        char cache_header[64];
        snprintf(cache_header, sizeof(cache_header), "max-age=%d", STATIC_FILE_CACHE_MAX_AGE);
        httpd_resp_set_hdr(req, "Cache-Control", cache_header);
        httpd_resp_set_hdr(req, "ETag", "\"static-v1\"");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
    }

    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // Send content
    ret = httpd_resp_send(req, content, content_length);
    
    if (ret == ESP_OK) {
        update_request_stats(true, content_length);
        printf(TIMESTAMP_FORMAT "%s: Served embedded content (%zu bytes, %s)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, content_length, mime_type);
    } else {
        update_request_stats(false, 0);
        ESP_LOGE(TAG, "Failed to send embedded content: %s", esp_err_to_name(ret));
    }

    return ret;
}

/* =============================================================================
 * PRIVATE FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

static esp_err_t root_handler(httpd_req_t *req)
{
    uint32_t timestamp = GET_TIMESTAMP();
    
    printf(TIMESTAMP_FORMAT "%s: Root path requested from %s\n",
           FORMAT_TIMESTAMP(timestamp), TAG, 
           req->aux ? (char*)req->aux : "unknown");

    // Serve the main dashboard HTML
    return static_file_controller_serve_embedded(req, 
                                                dashboard_html, 
                                                strlen(dashboard_html),
                                                "text/html",
                                                false);
}

static void update_request_stats(bool success, size_t bytes_served)
{
    if (xSemaphoreTake(g_static_file.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_static_file.stats.total_requests++;
        if (success) {
            g_static_file.stats.successful_requests++;
            g_static_file.stats.bytes_served += bytes_served;
        } else {
            g_static_file.stats.failed_requests++;
        }
        g_static_file.stats.last_request_time = GET_TIMESTAMP();
        xSemaphoreGive(g_static_file.stats_mutex);
    }
}

static const char* get_file_extension(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    const char *dot = strrchr(path, '.');
    if (dot == NULL || dot == path) {
        return NULL;
    }

    return dot;
}

static esp_err_t serve_file_from_data(httpd_req_t *req, const char *filename)
{
    uint32_t timestamp = GET_TIMESTAMP();
    
    printf(TIMESTAMP_FORMAT "%s: Attempting to serve file: %s\n",
           FORMAT_TIMESTAMP(timestamp), TAG, filename);

    // Serve content with advanced caching for specific files
    if (strcmp(filename, "test.html") == 0) {
        const char *test_html_content = 
            "<!DOCTYPE html>\n"
            "<html lang=\"en\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>SNRv9 API Test</title>\n"
            "    <link rel=\"stylesheet\" href=\"/style.css\">\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <h1>🧪 SNRv9 API Test Suite</h1>\n"
            "        \n"
            "        <div class=\"test-section\">\n"
            "            <h2>🔧 Advanced Caching Test</h2>\n"
            "            <p>This page tests the new ETag and caching system.</p>\n"
            "            <button onclick=\"testCaching()\">Test Cache Headers</button>\n"
            "            <div id=\"cache-result\"></div>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"test-section\">\n"
            "            <h2>📡 API Status Test</h2>\n"
            "            <button onclick=\"testApiStatus()\">Test /api/status</button>\n"
            "            <div id=\"api-result\"></div>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"test-section\">\n"
            "            <h2>🎨 Static File Loading</h2>\n"
            "            <p>CSS: <span id=\"css-status\">Loading...</span></p>\n"
            "            <p>JavaScript: <span id=\"js-status\">Loading...</span></p>\n"
            "        </div>\n"
            "    </div>\n"
            "\n"
            "    <script>\n"
            "        document.getElementById('js-status').textContent = '✅ Loaded';\n"
            "        document.getElementById('css-status').textContent = '✅ Loaded';\n"
            "        \n"
            "        function testCaching() {\n"
            "            const resultDiv = document.getElementById('cache-result');\n"
            "            resultDiv.innerHTML = '<p>Testing cache headers...</p>';\n"
            "            \n"
            "            fetch('/style.css', { method: 'HEAD' })\n"
            "                .then(response => {\n"
            "                    const etag = response.headers.get('ETag');\n"
            "                    const cacheControl = response.headers.get('Cache-Control');\n"
            "                    \n"
            "                    let html = '<div class=\"success\"><h3>Cache Headers:</h3>';\n"
            "                    html += '<p><strong>ETag:</strong> ' + (etag || 'Not set') + '</p>';\n"
            "                    html += '<p><strong>Cache-Control:</strong> ' + (cacheControl || 'Not set') + '</p>';\n"
            "                    html += '</div>';\n"
            "                    \n"
            "                    resultDiv.innerHTML = html;\n"
            "                })\n"
            "                .catch(error => {\n"
            "                    resultDiv.innerHTML = '<div class=\"error\"><p>Error: ' + error.message + '</p></div>';\n"
            "                });\n"
            "        }\n"
            "        \n"
            "        function testApiStatus() {\n"
            "            const resultDiv = document.getElementById('api-result');\n"
            "            resultDiv.innerHTML = '<p>Testing /api/status...</p>';\n"
            "            \n"
            "            fetch('/api/status')\n"
            "                .then(response => {\n"
            "                    if (!response.ok) {\n"
            "                        throw new Error('HTTP ' + response.status + ': ' + response.statusText);\n"
            "                    }\n"
            "                    return response.json();\n"
            "                })\n"
            "                .then(data => {\n"
            "                    resultDiv.innerHTML = '<div class=\"success\"><h3>API Success!</h3><pre>' + JSON.stringify(data, null, 2) + '</pre></div>';\n"
            "                })\n"
            "                .catch(error => {\n"
            "                    resultDiv.innerHTML = '<div class=\"error\"><h3>API Error:</h3><p>' + error.message + '</p></div>';\n"
            "                });\n"
            "        }\n"
            "        \n"
            "        console.log('Advanced caching test page loaded');\n"
            "    </script>\n"
            "</body>\n"
            "</html>";
        
        return static_file_controller_serve_with_cache(req, filename,
                                                      test_html_content, 
                                                      strlen(test_html_content),
                                                      "text/html");
    }
    
    if (strcmp(filename, "app.js") == 0) {
        const char *app_js_content = 
            "console.log('SNRv9 JavaScript with advanced caching loading...');\n"
            "\n"
            "// Enhanced configuration with caching awareness\n"
            "var CONFIG = {\n"
            "    API_BASE: '/api',\n"
            "    REFRESH_INTERVAL: 30000,\n"
            "    TIMEOUT: 10000,\n"
            "    CACHE_ENABLED: true\n"
            "};\n"
            "\n"
            "var refreshTimer = null;\n"
            "var isRefreshing = false;\n"
            "var cacheStats = { hits: 0, misses: 0 };\n"
            "\n"
            "// Enhanced fetch with cache monitoring\n"
            "function fetchWithCacheMonitoring(url, options = {}) {\n"
            "    const startTime = performance.now();\n"
            "    \n"
            "    return fetch(url, options)\n"
            "        .then(response => {\n"
            "            const loadTime = performance.now() - startTime;\n"
            "            \n"
            "            // Check if response came from cache\n"
            "            if (response.status === 304) {\n"
            "                cacheStats.hits++;\n"
            "                console.log(`Cache HIT for ${url} (${loadTime.toFixed(1)}ms)`);\n"
            "            } else {\n"
            "                cacheStats.misses++;\n"
            "                console.log(`Cache MISS for ${url} (${loadTime.toFixed(1)}ms)`);\n"
            "            }\n"
            "            \n"
            "            return response;\n"
            "        });\n"
            "}\n"
            "\n"
            "function refreshStatus() {\n"
            "    console.log('refreshStatus() called with caching support');\n"
            "    \n"
            "    if (isRefreshing) {\n"
            "        console.log('Already refreshing, skipping...');\n"
            "        return;\n"
            "    }\n"
            "    \n"
            "    isRefreshing = true;\n"
            "    \n"
            "    fetchWithCacheMonitoring('/api/status')\n"
            "        .then(response => {\n"
            "            if (!response.ok) {\n"
            "                throw new Error(`HTTP ${response.status}: ${response.statusText}`);\n"
            "            }\n"
            "            return response.json();\n"
            "        })\n"
            "        .then(data => {\n"
            "            console.log('Status data received:', data);\n"
            "            console.log(`Cache stats - Hits: ${cacheStats.hits}, Misses: ${cacheStats.misses}`);\n"
            "        })\n"
            "        .catch(error => {\n"
            "            console.error('Error fetching status:', error);\n"
            "        })\n"
            "        .finally(() => {\n"
            "            isRefreshing = false;\n"
            "        });\n"
            "}\n"
            "\n"
            "function startAutoRefresh() {\n"
            "    if (refreshTimer) {\n"
            "        clearInterval(refreshTimer);\n"
            "    }\n"
            "    \n"
            "    refreshTimer = setInterval(() => {\n"
            "        if (!document.hidden && !isRefreshing) {\n"
            "            refreshStatus();\n"
            "        }\n"
            "    }, CONFIG.REFRESH_INTERVAL);\n"
            "}\n"
            "\n"
            "// Initialize when DOM is ready\n"
            "document.addEventListener('DOMContentLoaded', function() {\n"
            "    console.log('SNRv9 Web Interface initialized with advanced caching');\n"
            "    refreshStatus();\n"
            "    startAutoRefresh();\n"
            "});\n"
            "\n"
            "// Export functions for global access\n"
            "window.refreshStatus = refreshStatus;\n"
            "window.cacheStats = cacheStats;\n"
            "\n"
            "console.log('SNRv9 JavaScript with caching loaded successfully');\n";
        
        return static_file_controller_serve_with_cache(req, filename,
                                                      app_js_content, 
                                                      strlen(app_js_content),
                                                      "application/javascript");
    }
    
    if (strcmp(filename, "style.css") == 0) {
        const char *style_css_content = 
            "/* SNRv9 Enhanced Styles with Caching Optimizations */\n"
            ":root {\n"
            "    --primary-color: #2c5aa0;\n"
            "    --secondary-color: #4a90e2;\n"
            "    --success-color: #28a745;\n"
            "    --warning-color: #ffc107;\n"
            "    --danger-color: #dc3545;\n"
            "    --light-bg: #f8f9fa;\n"
            "    --dark-text: #343a40;\n"
            "    --border-color: #dee2e6;\n"
            "    --shadow: 0 2px 4px rgba(0,0,0,0.1);\n"
            "    --cache-hit-color: #20c997;\n"
            "    --cache-miss-color: #fd7e14;\n"
            "}\n"
            "\n"
            "* {\n"
            "    margin: 0;\n"
            "    padding: 0;\n"
            "    box-sizing: border-box;\n"
            "}\n"
            "\n"
            "body {\n"
            "    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n"
            "    line-height: 1.6;\n"
            "    color: var(--dark-text);\n"
            "    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
            "    min-height: 100vh;\n"
            "    padding: 20px;\n"
            "}\n"
            "\n"
            ".container {\n"
            "    max-width: 1200px;\n"
            "    margin: 0 auto;\n"
            "    background: rgba(255,255,255,0.95);\n"
            "    border-radius: 15px;\n"
            "    padding: 30px;\n"
            "    box-shadow: 0 20px 40px rgba(0,0,0,0.1);\n"
            "}\n"
            "\n"
            "h1 {\n"
            "    text-align: center;\n"
            "    color: var(--primary-color);\n"
            "    font-size: 2.5em;\n"
            "    margin-bottom: 30px;\n"
            "    text-shadow: 2px 2px 4px rgba(0,0,0,0.1);\n"
            "}\n"
            "\n"
            ".test-section {\n"
            "    margin: 20px 0;\n"
            "    padding: 20px;\n"
            "    border: 2px solid var(--border-color);\n"
            "    border-radius: 10px;\n"
            "    background: var(--light-bg);\n"
            "    transition: transform 0.3s ease, box-shadow 0.3s ease;\n"
            "}\n"
            "\n"
            ".test-section:hover {\n"
            "    transform: translateY(-2px);\n"
            "    box-shadow: var(--shadow);\n"
            "}\n"
            "\n"
            ".test-section h2 {\n"
            "    color: var(--secondary-color);\n"
            "    margin-bottom: 15px;\n"
            "    font-size: 1.4em;\n"
            "}\n"
            "\n"
            ".success {\n"
            "    background-color: #d4edda;\n"
            "    border: 1px solid #c3e6cb;\n"
            "    color: #155724;\n"
            "    padding: 15px;\n"
            "    border-radius: 8px;\n"
            "    margin: 10px 0;\n"
            "}\n"
            "\n"
            ".error {\n"
            "    background-color: #f8d7da;\n"
            "    border: 1px solid #f5c6cb;\n"
            "    color: #721c24;\n"
            "    padding: 15px;\n"
            "    border-radius: 8px;\n"
            "    margin: 10px 0;\n"
            "}\n"
            "\n"
            "button {\n"
            "    background: var(--secondary-color);\n"
            "    color: white;\n"
            "    border: none;\n"
            "    padding: 12px 24px;\n"
            "    border-radius: 6px;\n"
            "    cursor: pointer;\n"
            "    font-size: 1rem;\n"
            "    transition: background-color 0.3s ease;\n"
            "    margin: 5px;\n"
            "}\n"
            "\n"
            "button:hover {\n"
            "    background: var(--primary-color);\n"
            "}\n"
            "\n"
            "pre {\n"
            "    background: #f8f9fa;\n"
            "    padding: 10px;\n"
            "    border-radius: 5px;\n"
            "    overflow-x: auto;\n"
            "    font-family: 'Courier New', monospace;\n"
            "    font-size: 0.9em;\n"
            "}\n";
        
        return static_file_controller_serve_with_cache(req, filename,
                                                      style_css_content, 
                                                      strlen(style_css_content),
                                                      "text/css");
    }
    
    // File not found
    printf(TIMESTAMP_FORMAT "%s: File not found: %s\n",
           FORMAT_TIMESTAMP(timestamp), TAG, filename);
    
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/html");
    
    const char *not_found_html = 
        "<!DOCTYPE html>\n"
        "<html><head><title>404 Not Found</title></head>\n"
        "<body><h1>404 Not Found</h1>\n"
        "<p>The requested file was not found on this server.</p>\n"
        "<p><a href=\"/\">Return to main page</a></p>\n"
        "</body></html>";
    
    update_request_stats(false, 0);
    return httpd_resp_send(req, not_found_html, strlen(not_found_html));
}

static esp_err_t file_handler(httpd_req_t *req)
{
    uint32_t timestamp = GET_TIMESTAMP();
    
    printf(TIMESTAMP_FORMAT "%s: File request: %s\n",
           FORMAT_TIMESTAMP(timestamp), TAG, req->uri);
    
    // Extract filename from URI (skip leading slash)
    const char *filename = req->uri + 1;
    
    return serve_file_from_data(req, filename);
}

/* =============================================================================
 * ADVANCED CACHING FUNCTION IMPLEMENTATIONS
 * =============================================================================
 */

bool static_file_controller_is_compressible(const char *extension)
{
    if (extension == NULL) {
        return false;
    }

    for (int i = 0; g_mime_mappings[i].extension != NULL; i++) {
        if (strcmp(extension, g_mime_mappings[i].extension) == 0) {
            return g_mime_mappings[i].compressible;
        }
    }

    return false;
}

uint32_t static_file_controller_get_cache_max_age(const char *extension)
{
    if (extension == NULL) {
        return 0;
    }

    for (int i = 0; g_mime_mappings[i].extension != NULL; i++) {
        if (strcmp(extension, g_mime_mappings[i].extension) == 0) {
            return g_mime_mappings[i].cache_max_age;
        }
    }

    return g_static_file.cache_config.default_cache_age;
}

bool static_file_controller_generate_etag(const char *content,
                                         size_t content_length,
                                         char *etag)
{
    if (content == NULL || etag == NULL || content_length == 0) {
        return false;
    }

    // Simple hash-based ETag generation (FNV-1a hash)
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < content_length; i++) {
        hash ^= (uint8_t)content[i];
        hash *= 16777619U;
    }

    // Include content length in hash for uniqueness
    hash ^= (uint32_t)content_length;
    hash *= 16777619U;

    snprintf(etag, STATIC_FILE_ETAG_LENGTH, "\"%08lx\"", (unsigned long)hash);
    return true;
}

bool static_file_controller_check_etag_match(httpd_req_t *req, const char *etag)
{
    if (req == NULL || etag == NULL) {
        return false;
    }

    // Check If-None-Match header
    size_t header_len = httpd_req_get_hdr_value_len(req, "If-None-Match");
    if (header_len > 0) {
        char *if_none_match = malloc(header_len + 1);
        if (if_none_match != NULL) {
            esp_err_t ret = httpd_req_get_hdr_value_str(req, "If-None-Match", 
                                                       if_none_match, header_len + 1);
            if (ret == ESP_OK) {
                bool match = (strcmp(if_none_match, etag) == 0);
                free(if_none_match);
                return match;
            }
            free(if_none_match);
        }
    }

    return false;
}

esp_err_t static_file_controller_serve_with_cache(httpd_req_t *req,
                                                 const char *filename,
                                                 const char *content,
                                                 size_t content_length,
                                                 const char *mime_type)
{
    if (req == NULL || filename == NULL || content == NULL || mime_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t timestamp = GET_TIMESTAMP();
    
    // Generate ETag if enabled
    char etag[STATIC_FILE_ETAG_LENGTH] = {0};
    bool etag_generated = false;
    
    if (g_static_file.cache_config.etag_enabled) {
        etag_generated = static_file_controller_generate_etag(content, content_length, etag);
        
        if (etag_generated) {
            printf(TIMESTAMP_FORMAT "%s: Generated ETag %s for %s\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, etag, filename);
        }
    }

    // Check for conditional requests
    if (g_static_file.cache_config.conditional_requests && etag_generated) {
        if (static_file_controller_check_etag_match(req, etag)) {
            printf(TIMESTAMP_FORMAT "%s: ETag match for %s, sending 304 Not Modified\n",
                   FORMAT_TIMESTAMP(timestamp), TAG, filename);
            
            // Send 304 Not Modified
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_set_hdr(req, "ETag", etag);
            
            // Update cache hit statistics
            if (xSemaphoreTake(g_static_file.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_static_file.stats.cache_hits++;
                g_static_file.stats.total_requests++;
                g_static_file.stats.successful_requests++;
                xSemaphoreGive(g_static_file.stats_mutex);
            }
            
            return httpd_resp_send(req, NULL, 0);
        }
    }

    // Set content type
    esp_err_t ret = httpd_resp_set_type(req, mime_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set content type: %s", esp_err_to_name(ret));
        update_request_stats(false, 0);
        return ret;
    }

    // Get file extension for cache settings
    const char *extension = get_file_extension(filename);
    bool cacheable = static_file_controller_is_cacheable(extension);
    uint32_t cache_max_age = static_file_controller_get_cache_max_age(extension);

    // Set caching headers
    if (cacheable && cache_max_age > 0) {
        char cache_header[64];
        snprintf(cache_header, sizeof(cache_header), "max-age=%lu, public", 
                (unsigned long)cache_max_age);
        httpd_resp_set_hdr(req, "Cache-Control", cache_header);
        
        if (etag_generated) {
            httpd_resp_set_hdr(req, "ETag", etag);
        }
        
        printf(TIMESTAMP_FORMAT "%s: Set cache headers for %s (max-age=%lu)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, filename, (unsigned long)cache_max_age);
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
    }

    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // Send content
    ret = httpd_resp_send(req, content, content_length);
    
    if (ret == ESP_OK) {
        update_request_stats(true, content_length);
        printf(TIMESTAMP_FORMAT "%s: Served %s with advanced caching (%zu bytes, %s)\n",
               FORMAT_TIMESTAMP(timestamp), TAG, filename, content_length, mime_type);
    } else {
        update_request_stats(false, 0);
        ESP_LOGE(TAG, "Failed to send content: %s", esp_err_to_name(ret));
    }

    return ret;
}

bool static_file_controller_configure_cache(const cache_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_static_file.cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(&g_static_file.cache_config, config, sizeof(cache_config_t));
        xSemaphoreGive(g_static_file.cache_mutex);
        
        ESP_LOGI(TAG, "Cache configuration updated: ETag=%s, Conditional=%s, Compression=%s",
                 config->etag_enabled ? "enabled" : "disabled",
                 config->conditional_requests ? "enabled" : "disabled",
                 config->compression_enabled ? "enabled" : "disabled");
        
        return true;
    }

    return false;
}

bool static_file_controller_get_cache_config(cache_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_static_file.cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(config, &g_static_file.cache_config, sizeof(cache_config_t));
        xSemaphoreGive(g_static_file.cache_mutex);
        return true;
    }

    return false;
}

bool static_file_controller_get_cache_stats(uint32_t *total_entries, float *hit_rate)
{
    if (total_entries == NULL || hit_rate == NULL) {
        return false;
    }

    static_file_stats_t stats;
    if (static_file_controller_get_stats(&stats)) {
        *total_entries = g_static_file.cache_entry_count;
        
        if (stats.total_requests > 0) {
            *hit_rate = (float)stats.cache_hits / (float)stats.total_requests;
        } else {
            *hit_rate = 0.0f;
        }
        
        return true;
    }

    return false;
}

void static_file_controller_clear_cache(void)
{
    if (xSemaphoreTake(g_static_file.cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(g_static_file.cache_entries, 0, sizeof(g_static_file.cache_entries));
        g_static_file.cache_entry_count = 0;
        xSemaphoreGive(g_static_file.cache_mutex);
        
        ESP_LOGI(TAG, "Cache cleared");
    }
}
