// SNRv9 Irrigation Control System - Main JavaScript

console.log('SNRv9 JavaScript loading...');

// Global configuration
var CONFIG = {
    API_BASE: '/api',
    REFRESH_INTERVAL: 30000,
    TIMEOUT: 10000
};

// Global state
var refreshTimer = null;
var isRefreshing = false;

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', function() {
    console.log('SNRv9 Web Interface initialized');
    refreshStatus();
    startAutoRefresh();
});

// Main refresh function - GLOBAL
function refreshStatus() {
    console.log('refreshStatus() called');
    
    if (isRefreshing) {
        console.log('Already refreshing, skipping...');
        return;
    }
    
    isRefreshing = true;
    var statusElement = document.getElementById('system-status');
    
    if (!statusElement) {
        console.error('Status element not found');
        isRefreshing = false;
        return;
    }
    
    console.log('Fetching system status...');
    showLoading(statusElement);
    
    fetch('/api/status')
        .then(function(response) {
            console.log('Response received:', response.status);
            if (!response.ok) {
                throw new Error('HTTP ' + response.status + ': ' + response.statusText);
            }
            return response.json();
        })
        .then(function(data) {
            console.log('Data received:', data);
            displaySystemStatus(statusElement, data);
        })
        .catch(function(error) {
            console.error('Error:', error);
            showError(statusElement, error.message);
        })
        .finally(function() {
            isRefreshing = false;
        });
}

// Display system status
function displaySystemStatus(element, data) {
    var timestamp = new Date().toLocaleString();
    var html = '';
    
    html += '<div class="status-item">';
    html += '<span>System Name:</span>';
    html += '<span class="status-value">' + (data.system ? data.system.name || 'Unknown' : 'Unknown') + '</span>';
    html += '</div>';
    
    html += '<div class="status-item">';
    html += '<span>Version:</span>';
    html += '<span class="status-value">' + (data.system ? data.system.version || 'Unknown' : 'Unknown') + '</span>';
    html += '</div>';
    
    html += '<div class="status-item">';
    html += '<span>Web Server Status:</span>';
    html += '<span class="status-value status-online">' + (data.web_server ? data.web_server.status || 'Unknown' : 'Unknown') + '</span>';
    html += '</div>';
    
    html += '<div class="status-item">';
    html += '<span>Server Port:</span>';
    html += '<span class="status-value">' + (data.web_server ? data.web_server.port || 'Unknown' : 'Unknown') + '</span>';
    html += '</div>';
    
    html += '<div class="status-item">';
    html += '<span>Uptime:</span>';
    html += '<span class="status-value">' + formatUptime(data.web_server ? data.web_server.uptime_seconds : 0) + '</span>';
    html += '</div>';
    
    html += '<div class="status-item">';
    html += '<span>Total Requests:</span>';
    html += '<span class="status-value">' + (data.web_server ? data.web_server.total_requests || 0 : 0) + '</span>';
    html += '</div>';
    
    html += '<div class="status-item">';
    html += '<span>Free Memory:</span>';
    html += '<span class="status-value">' + formatBytes(data.memory ? data.memory.free_heap : 0) + '</span>';
    html += '</div>';
    
    html += '<div class="status-item">';
    html += '<span>Last Updated:</span>';
    html += '<span class="status-value">' + timestamp + '</span>';
    html += '</div>';
    
    element.innerHTML = html;
}

// Show loading state
function showLoading(element) {
    element.innerHTML = '<div class="text-center"><span class="loading"></span> Loading system status...</div>';
}

// Show error state
function showError(element, message) {
    var html = '';
    html += '<div class="text-center text-danger">';
    html += '<p><strong>Error loading system status:</strong></p>';
    html += '<p>' + message + '</p>';
    html += '<p class="mt-20">';
    html += '<button onclick="refreshStatus()">Try Again</button>';
    html += '</p>';
    html += '</div>';
    element.innerHTML = html;
}

// Format uptime
function formatUptime(seconds) {
    if (!seconds || seconds < 0) {
        return 'Unknown';
    }
    
    var days = Math.floor(seconds / 86400);
    var hours = Math.floor((seconds % 86400) / 3600);
    var minutes = Math.floor((seconds % 3600) / 60);
    var secs = seconds % 60;
    
    if (days > 0) {
        return days + 'd ' + hours + 'h ' + minutes + 'm';
    } else if (hours > 0) {
        return hours + 'h ' + minutes + 'm ' + secs + 's';
    } else if (minutes > 0) {
        return minutes + 'm ' + secs + 's';
    } else {
        return secs + 's';
    }
}

// Format bytes
function formatBytes(bytes) {
    if (!bytes || bytes < 0) {
        return 'Unknown';
    }
    
    var units = ['B', 'KB', 'MB', 'GB'];
    var size = bytes;
    var unitIndex = 0;
    
    while (size >= 1024 && unitIndex < units.length - 1) {
        size = size / 1024;
        unitIndex++;
    }
    
    return size.toFixed(1) + ' ' + units[unitIndex];
}

// Auto refresh
function startAutoRefresh() {
    if (refreshTimer) {
        clearInterval(refreshTimer);
    }
    
    refreshTimer = setInterval(function() {
        if (!document.hidden && !isRefreshing) {
            refreshStatus();
        }
    }, CONFIG.REFRESH_INTERVAL);
}

// Make functions globally available
window.refreshStatus = refreshStatus;

console.log('SNRv9 JavaScript loaded successfully');
