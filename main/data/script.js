let currentMode = true; // true = AUTO
//Status update every 2 seconds
setInterval(updateStatus, 2000);
updateStatus();
loadSettings();

async function updateStatus() {
    try {
        const response = await fetch('/status');
        const data = await response.json();
        
        document.getElementById('moisture').textContent = data.moisture + '%';
        document.getElementById('status').textContent = data.status;
        document.getElementById('mode').textContent = data.autoMode ? 'AUTO' : 'MANUAL';
        document.getElementById('relay').textContent = data.relayOn ? 'ON' : 'OFF';
        
        currentMode = data.autoMode;
        document.getElementById('modeToggle').checked = data.autoMode;
        
        // We activate the buttons only in manual mode
        const isManual = !data.autoMode;
        document.getElementById('btnOn').disabled = !isManual;
        document.getElementById('btnOff').disabled = !isManual;
    } catch (error) {
        console.error('Error updating status:', error);
    }
}

async function toggleMode() {
    const isAuto = document.getElementById('modeToggle').checked;
    try {
        const response = await fetch('/mode', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: 'mode=' + (isAuto ? 'auto' : 'manual')
        });
        if (response.ok) {
            updateStatus();
        }
    } catch (error) {
        console.error('Error toggling mode:', error);
        alert('Ошибка переключения режима');
    }
}

async function controlRelay(action) {
    try {
        const response = await fetch('/relay', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: 'state=' + action
        });
        if (response.ok) {
            updateStatus();
        }
    } catch (error) {
        console.error('Error controlling relay:', error);
        alert('Ошибка управления реле');
    }
}

async function saveSettings() {
    const breakInt = document.getElementById('breakInterval').value;
    const cycleInt = document.getElementById('cycleInterval').value;
    const pulses = document.getElementById('totalPulses').value;
    
    try {
        const response = await fetch('/update', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: `breakInterval=${breakInt}&cycleInterval=${cycleInt}&totalPulses=${pulses}`
        });
        if (response.ok) {
            alert('Settings saved successfully!');
        }
    } catch (error) {
        console.error('Error saving settings:', error);
        alert('Error saving settings');
    }
}

async function loadSettings() {
    try {
        const response = await fetch('/settings');
        const data = await response.json();
        
        document.getElementById('breakInterval').value = data.breakInterval;
        document.getElementById('cycleInterval').value = data.cycleInterval;
        document.getElementById('totalPulses').value = data.totalPulses;
    } catch (error) {
        console.error('Error loading settings:', error);
    }
}