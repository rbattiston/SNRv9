// SNRv9 Time Management JavaScript
console.log('SNRv9 Time Management JavaScript loading...');

// Global configuration
var TIME_CONFIG = {
    API_BASE: '/api/time',
    REFRESH_INTERVAL: 1000, // 1 second for real-time clock
    STATS_REFRESH_INTERVAL: 30000, // 30 seconds for statistics
    TIMEOUT: 10000
};

// IANA to POSIX timezone mapping table
var TIMEZONE_MAPPING = {
    'UTC': 'UTC0',
    'America/New_York': 'EST5EDT,M3.2.0,M11.1.0',
    'America/Chicago': 'CST6CDT,M3.2.0,M11.1.0', 
    'America/Denver': 'MST7MDT,M3.2.0,M11.1.0',
    'America/Los_Angeles': 'PST8PDT,M3.2.0,M11.1.0',
    'Europe/London': 'GMT0BST,M3.5.0/1,M10.5.0',
    'Europe/Paris': 'CET-1CEST,M3.5.0,M10.5.0/3',
    'Asia/Tokyo': 'JST-9',
    'Australia/Sydney': 'AEST-10AEDT,M10.1.0,M4.1.0/3'
};

// Global state
var clockTimer = null;
var statsTimer = null;
var isRefreshing = false;
var currentTimeData = null;

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', function() {
    console.log('Time Management interface initialized');
    initializeCurrentTime();
    refreshTimeStatus();
    loadTimeConfiguration();
    startAutoRefresh();
});

// Initialize current time display - NEVER use browser time
function initializeCurrentTime() {
    // Show "initializing" state until server data loads
    document.getElementById('local-time').textContent = 'Initializing...';
    document.getElementById('utc-time').textContent = 'Waiting for ESP32 time data...';
    document.getElementById('timezone-info').textContent = 'Timezone: Loading...';
    
    // Start clock update timer (will only update when we have server data)
    clockTimer = setInterval(updateClockDisplay, 1000);
}

// Update clock display - ONLY use ESP32 time data, NEVER browser time
function updateClockDisplay() {
    // Only update if we have valid server time data
    if (!currentTimeData || !currentTimeData.current_time) {
        // Show ESP32 status instead of browser time
        document.getElementById('local-time').textContent = 'ESP32 Time Not Set';
        document.getElementById('utc-time').textContent = 'Waiting for time synchronization...';
        document.getElementById('timezone-info').textContent = 'Timezone: Not configured';
        return;
    }
    
    // Check if time is valid (not epoch time)
    if (currentTimeData.current_time.unix_timestamp < 946684800) { // Year 2000
        document.getElementById('local-time').textContent = 'ESP32 Time Invalid (Epoch: ' + currentTimeData.current_time.unix_timestamp + ')';
        document.getElementById('utc-time').textContent = 'Time source: ' + (currentTimeData.system.time_source || 'none');
        document.getElementById('timezone-info').textContent = 'Timezone: ' + (currentTimeData.timezone.name || 'Unknown');
        return;
    }
    
    // Get UTC time from ESP32 (NEVER use browser time)
    var utcTime = new Date(currentTimeData.current_time.unix_timestamp * 1000);
    
    // Calculate ESP32's local time using ESP32's timezone offset
    var esp32LocalTime;
    if (currentTimeData.timezone && typeof currentTimeData.timezone.offset_seconds === 'number') {
        // Apply ESP32's timezone offset to get ESP32's local time
        esp32LocalTime = new Date(utcTime.getTime() + (currentTimeData.timezone.offset_seconds * 1000));
    } else {
        // Fallback to UTC if no timezone info
        esp32LocalTime = utcTime;
    }
    
    // Format ESP32's local time manually (no browser timezone interference)
    var esp32LocalTimeStr = formatESP32LocalTime(esp32LocalTime);
    
    // Format UTC time
    var utcTimeStr = utcTime.toISOString().replace('T', ' ').substring(0, 19) + ' UTC';
    
    // Get timezone name from ESP32
    var timezoneName = 'Unknown';
    if (currentTimeData.timezone && currentTimeData.timezone.name) {
        // Extract short timezone name (e.g., "CST" from "CST6CDT,M3.2.0,M11.1.0")
        var tzName = currentTimeData.timezone.name;
        if (tzName.includes('CST')) timezoneName = 'CST';
        else if (tzName.includes('EST')) timezoneName = 'EST';
        else if (tzName.includes('MST')) timezoneName = 'MST';
        else if (tzName.includes('PST')) timezoneName = 'PST';
        else if (tzName.includes('UTC')) timezoneName = 'UTC';
        else timezoneName = tzName.split(',')[0]; // Take first part
    }
    
    // Display ESP32's time (never browser time)
    document.getElementById('local-time').textContent = esp32LocalTimeStr + ' (ESP32)';
    document.getElementById('utc-time').textContent = utcTimeStr;
    document.getElementById('timezone-info').textContent = 'Timezone: ' + timezoneName;
}

// Format ESP32's local time manually to avoid browser timezone interference
function formatESP32LocalTime(date) {
    var weekdays = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    var months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 
                  'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    
    var weekday = weekdays[date.getUTCDay()];
    var month = months[date.getUTCMonth()];
    var day = date.getUTCDate();
    var year = date.getUTCFullYear();
    var hours = String(date.getUTCHours()).padStart(2, '0');
    var minutes = String(date.getUTCMinutes()).padStart(2, '0');
    var seconds = String(date.getUTCSeconds()).padStart(2, '0');
    
    return weekday + ', ' + month + ' ' + day + ', ' + year + ', ' + hours + ':' + minutes + ':' + seconds;
}

// Refresh time status from server
async function refreshTimeStatus() {
    if (isRefreshing) return;
    
    isRefreshing = true;
    
    try {
        console.log('Fetching time status...');
        const response = await fetch(TIME_CONFIG.API_BASE + '/status');
        
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        
        const data = await response.json();
        console.log('Time status received:', data);
        
        currentTimeData = data;
        displayTimeStatus(data);
        updateSyncStatus(data);
        updateTimeStatistics(data);
        
    } catch (error) {
        console.error('Error fetching time status:', error);
        showErrorMessage('Failed to fetch time status: ' + error.message);
        updateSyncStatus({ sync_status: 'error', error_message: error.message });
    } finally {
        isRefreshing = false;
    }
}

// Display time status from server - NEVER use browser time
function displayTimeStatus(data) {
    // This function is now handled by updateClockDisplay() which uses ESP32-only time
    // Keep this function for compatibility but don't override the ESP32 time display
    console.log('Time status updated from server:', data);
}

// Update sync status display
function updateSyncStatus(data) {
    var statusElement = document.getElementById('sync-status-display');
    var indicator = statusElement.querySelector('.status-indicator');
    
    var status = data.sync_status || 'unknown';
    var message = '';
    
    // Remove all status classes
    statusElement.className = 'sync-status';
    indicator.className = 'status-indicator';
    
    switch (status) {
        case 'synced':
            statusElement.classList.add('synced');
            indicator.classList.add('green');
            message = '✅ NTP Synchronized';
            if (data.last_sync) {
                message += ' (' + formatTimeAgo(data.last_sync) + ')';
            }
            break;
            
        case 'syncing':
            statusElement.classList.add('syncing');
            indicator.classList.add('yellow');
            message = '🔄 Synchronizing...';
            break;
            
        case 'error':
            statusElement.classList.add('error');
            indicator.classList.add('red');
            message = '❌ Sync Error';
            if (data.error_message) {
                message += ': ' + data.error_message;
            }
            break;
            
        case 'manual':
            statusElement.classList.add('manual');
            indicator.classList.add('gray');
            message = '⏰ Manual Time';
            break;
            
        default:
            statusElement.classList.add('manual');
            indicator.classList.add('gray');
            message = '❓ Unknown Status';
    }
    
    statusElement.innerHTML = '<span class="status-indicator ' + indicator.classList[1] + '"></span>' + message;
}

// Update time statistics
function updateTimeStatistics(data) {
    var stats = data.statistics || {};
    
    document.getElementById('stat-sync-success').textContent = 
        stats.sync_success_rate ? (stats.sync_success_rate * 100).toFixed(1) + '%' : '--';
    
    document.getElementById('stat-last-sync').textContent = 
        stats.last_successful_sync ? formatTimeAgo(stats.last_successful_sync) : 'Never';
    
    document.getElementById('stat-time-source').textContent = 
        stats.time_source || data.sync_status || 'Unknown';
    
    document.getElementById('stat-uptime').textContent = 
        stats.system_uptime ? formatUptime(stats.system_uptime) : '--';
    
    document.getElementById('stat-sync-count').textContent = 
        stats.total_sync_attempts || 0;
    
    document.getElementById('stat-time-accuracy').textContent = 
        stats.time_accuracy ? stats.time_accuracy + 'ms' : '--';
}

// Load time configuration
async function loadTimeConfiguration() {
    try {
        // Load current configuration and populate forms
        const response = await fetch(TIME_CONFIG.API_BASE + '/status');
        if (response.ok) {
            const data = await response.json();
            populateConfigurationForms(data);
        }
    } catch (error) {
        console.error('Error loading configuration:', error);
    }
}

// Populate configuration forms with current settings
function populateConfigurationForms(data) {
    if (data.ntp_config) {
        document.getElementById('ntp-server').value = data.ntp_config.primary_server || '';
        document.getElementById('ntp-server-backup').value = data.ntp_config.backup_server || '';
        document.getElementById('sync-interval').value = data.ntp_config.sync_interval_minutes || 60;
    }
    
    // Update current timezone display
    var currentTimezoneDisplay = document.getElementById('current-timezone-display');
    if (data.current_time && data.current_time.timezone) {
        currentTimezoneDisplay.textContent = data.current_time.timezone;
        
        // Try to reverse-map POSIX timezone to IANA for dropdown selection
        var ianaTimezone = findIANATimezone(data.current_time.timezone);
        if (ianaTimezone) {
            var select = document.getElementById('timezone-select');
            for (var i = 0; i < select.options.length; i++) {
                if (select.options[i].value === ianaTimezone) {
                    select.selectedIndex = i;
                    break;
                }
            }
        }
    } else {
        currentTimezoneDisplay.textContent = 'Not set';
    }
    
    // Set current date/time for manual setting - use ESP32 time if available
    var now;
    if (currentTimeData && currentTimeData.current_time && currentTimeData.current_time.unix_timestamp > 946684800) {
        // Use ESP32's current time
        now = new Date(currentTimeData.current_time.unix_timestamp * 1000);
    } else {
        // Fallback to UTC time (never browser local time)
        now = new Date();
    }
    
    // Format as UTC to avoid browser timezone interference
    document.getElementById('manual-date').value = now.toISOString().split('T')[0];
    document.getElementById('manual-time').value = now.toISOString().split('T')[1].substring(0, 8);
}

// Helper function to find IANA timezone from POSIX timezone
function findIANATimezone(posixTimezone) {
    for (var iana in TIMEZONE_MAPPING) {
        if (TIMEZONE_MAPPING[iana] === posixTimezone) {
            return iana;
        }
    }
    return null;
}

// Force NTP synchronization
async function forceSyncNTP() {
    try {
        showLoadingOverlay('Forcing NTP synchronization...');
        
        const response = await fetch(TIME_CONFIG.API_BASE + '/ntp/sync', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            }
        });
        
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        
        const result = await response.json();
        showSuccessMessage('NTP synchronization initiated successfully');
        
        // Refresh status after a short delay
        setTimeout(refreshTimeStatus, 2000);
        
    } catch (error) {
        console.error('Error forcing NTP sync:', error);
        showErrorMessage('Failed to force NTP sync: ' + error.message);
    } finally {
        hideLoadingOverlay();
    }
}

// Save NTP configuration
async function saveNTPConfig(event) {
    event.preventDefault();
    
    var config = {
        primary_server: document.getElementById('ntp-server').value,
        backup_server: document.getElementById('ntp-server-backup').value,
        sync_interval_minutes: parseInt(document.getElementById('sync-interval').value)
    };
    
    try {
        showLoadingOverlay('Saving NTP configuration...');
        
        const response = await fetch(TIME_CONFIG.API_BASE + '/ntp/config', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(config)
        });
        
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        
        const result = await response.json();
        showSuccessMessage('NTP configuration saved successfully');
        
        // Refresh status
        setTimeout(refreshTimeStatus, 1000);
        
    } catch (error) {
        console.error('Error saving NTP config:', error);
        showErrorMessage('Failed to save NTP configuration: ' + error.message);
    } finally {
        hideLoadingOverlay();
    }
}

// Save timezone configuration
async function saveTimezoneConfig(event) {
    event.preventDefault();
    
    var selectedTimezone = document.getElementById('timezone-select').value;
    
    if (!selectedTimezone) {
        showErrorMessage('Please select a timezone');
        return;
    }
    
    // Convert IANA timezone to POSIX format if it's a known mapping
    var posixTimezone = TIMEZONE_MAPPING[selectedTimezone] || selectedTimezone;
    
    console.log('Converting timezone:', selectedTimezone, '->', posixTimezone);
    
    try {
        showLoadingOverlay('Setting timezone...');
        
        const response = await fetch(TIME_CONFIG.API_BASE + '/ntp/config', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ timezone: posixTimezone })
        });
        
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        
        const result = await response.json();
        showSuccessMessage('Timezone set successfully to: ' + selectedTimezone + ' (' + posixTimezone + ')');
        
        // Refresh status
        setTimeout(refreshTimeStatus, 1000);
        
    } catch (error) {
        console.error('Error setting timezone:', error);
        showErrorMessage('Failed to set timezone: ' + error.message);
    } finally {
        hideLoadingOverlay();
    }
}

// Set manual time
async function setManualTime(event) {
    event.preventDefault();
    
    var date = document.getElementById('manual-date').value;
    var time = document.getElementById('manual-time').value;
    var confirmed = document.getElementById('confirm-manual-time').checked;
    
    if (!date || !time || !confirmed) {
        showErrorMessage('Please fill all fields and confirm the warning');
        return;
    }
    
    var datetime = new Date(date + 'T' + time);
    var unixTimestamp = Math.floor(datetime.getTime() / 1000);
    
    try {
        showLoadingOverlay('Setting manual time...');
        
        const response = await fetch(TIME_CONFIG.API_BASE + '/manual', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ 
                unix_timestamp: unixTimestamp,
                local_time: datetime.toISOString()
            })
        });
        
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        
        const result = await response.json();
        showSuccessMessage('Manual time set successfully');
        
        // Reset form
        document.getElementById('manual-time-form').reset();
        
        // Refresh status
        setTimeout(refreshTimeStatus, 1000);
        
    } catch (error) {
        console.error('Error setting manual time:', error);
        showErrorMessage('Failed to set manual time: ' + error.message);
    } finally {
        hideLoadingOverlay();
    }
}


// Auto refresh functionality
function startAutoRefresh() {
    // Refresh time status every 30 seconds
    statsTimer = setInterval(function() {
        if (!document.hidden && !isRefreshing) {
            refreshTimeStatus();
        }
    }, TIME_CONFIG.STATS_REFRESH_INTERVAL);
}

// Utility functions
function formatTimeAgo(timestamp) {
    if (!timestamp) return 'Never';
    
    var now = new Date();
    var time = new Date(timestamp * 1000);
    var diffMs = now - time;
    var diffSecs = Math.floor(diffMs / 1000);
    var diffMins = Math.floor(diffSecs / 60);
    var diffHours = Math.floor(diffMins / 60);
    var diffDays = Math.floor(diffHours / 24);
    
    if (diffDays > 0) {
        return diffDays + ' day' + (diffDays > 1 ? 's' : '') + ' ago';
    } else if (diffHours > 0) {
        return diffHours + ' hour' + (diffHours > 1 ? 's' : '') + ' ago';
    } else if (diffMins > 0) {
        return diffMins + ' minute' + (diffMins > 1 ? 's' : '') + ' ago';
    } else {
        return 'Just now';
    }
}

function formatUptime(seconds) {
    if (!seconds || seconds < 0) return '--';
    
    var days = Math.floor(seconds / 86400);
    var hours = Math.floor((seconds % 86400) / 3600);
    var minutes = Math.floor((seconds % 3600) / 60);
    
    if (days > 0) {
        return days + 'd ' + hours + 'h ' + minutes + 'm';
    } else if (hours > 0) {
        return hours + 'h ' + minutes + 'm';
    } else {
        return minutes + 'm';
    }
}

// UI feedback functions
function showSuccessMessage(message) {
    showMessage(message, 'success');
}

function showErrorMessage(message) {
    showMessage(message, 'error');
}

function showMessage(message, type) {
    // Create temporary message
    var messageDiv = document.createElement('div');
    messageDiv.className = type + '-box';
    messageDiv.textContent = message;
    messageDiv.style.position = 'fixed';
    messageDiv.style.top = '20px';
    messageDiv.style.right = '20px';
    messageDiv.style.zIndex = '1000';
    messageDiv.style.maxWidth = '400px';
    messageDiv.style.boxShadow = '0 4px 12px rgba(0,0,0,0.15)';
    
    document.body.appendChild(messageDiv);
    
    setTimeout(function() {
        if (document.body.contains(messageDiv)) {
            document.body.removeChild(messageDiv);
        }
    }, type === 'error' ? 5000 : 3000);
}

function showLoadingOverlay(message) {
    var overlay = document.createElement('div');
    overlay.id = 'loading-overlay';
    overlay.className = 'loading-overlay';
    overlay.innerHTML = '<div><span class="loading"></span> ' + message + '</div>';
    
    document.querySelector('.container').appendChild(overlay);
}

function hideLoadingOverlay() {
    var overlay = document.getElementById('loading-overlay');
    if (overlay) {
        overlay.remove();
    }
}

// Cleanup on page unload
window.addEventListener('beforeunload', function() {
    if (clockTimer) clearInterval(clockTimer);
    if (statsTimer) clearInterval(statsTimer);
});

console.log('SNRv9 Time Management JavaScript loaded successfully');
