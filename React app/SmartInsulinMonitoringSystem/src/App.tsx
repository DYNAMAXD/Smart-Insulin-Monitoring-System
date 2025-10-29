import React, { useState, useEffect } from 'react';
import './App.css';

const App = () => {
  const [status, setStatus] = useState({
    doseActive: 'NO',
    vialTaken: 'NO', 
    vialReplaced: 'NO',
    currentWeight: '0.00 g',
    initialWeight: '0.00 g',
    wifi: 'Disconnected',
    time: 'Not Synced'
  });

  const [alerts, setAlerts] = useState([]);
  const [esp32IP, setEsp32IP] = useState('192.168.128.100');
  const [connectionStatus, setConnectionStatus] = useState('Connecting...');
  const [debugInfo, setDebugInfo] = useState('');
  const [isTesting, setIsTesting] = useState(false);

  // Test connection function
  const testConnection = async () => {
    setIsTesting(true);
    setDebugInfo('Testing connection...');
    
    try {
      // Test basic connectivity
      setDebugInfo('Pinging ESP32...');
      const startTime = Date.now();
      
      const response = await fetch(`http://${esp32IP}/api/status`, {
        method: 'GET',
        headers: {
          'Accept': 'application/json',
        },
      });
      
      const responseTime = Date.now() - startTime;
      
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      
      const data = await response.json();
      setDebugInfo(`✅ Connected successfully! Response time: ${responseTime}ms`);
      
      // Update status with the fetched data
      setStatus({
        doseActive: data.doseActive ? 'YES' : 'NO',
        vialTaken: data.vialTaken ? 'YES' : 'NO',
        vialReplaced: data.vialReplaced ? 'YES' : 'NO',
        currentWeight: `${data.currentWeight?.toFixed(2) || '0.00'} g`,
        initialWeight: `${data.initialWeight?.toFixed(2) || '0.00'} g`,
        wifi: data.wifiConnected ? 'Connected' : 'Disconnected',
        time: data.timeSynced ? 'Synced' : 'Not Synced'
      });
      
      setConnectionStatus('Connected');
      
    } catch (error) {
      const errorMsg = `❌ Connection failed: ${error.message}`;
      setDebugInfo(errorMsg);
      setConnectionStatus('Disconnected');
      console.error('Connection test failed:', error);
    } finally {
      setIsTesting(false);
    }
  };

  // Test alerts endpoint
  const testAlertsEndpoint = async () => {
    try {
      setDebugInfo('Testing alerts endpoint...');
      const response = await fetch(`http://${esp32IP}/api/alerts`);
      
      if (!response.ok) {
        throw new Error(`Alerts endpoint: HTTP ${response.status}`);
      }
      
      const data = await response.json();
      setDebugInfo(prev => prev + '\n✅ Alerts endpoint working!');
      
    } catch (error) {
      setDebugInfo(prev => prev + `\n❌ Alerts endpoint failed: ${error.message}`);
    }
  };

  // Manual connection test
  const runFullConnectionTest = async () => {
    await testConnection();
    await testAlertsEndpoint();
  };

  // Fetch status periodically
  useEffect(() => {
    const fetchStatus = async () => {
      // Don't fetch if we're manually testing or connection is bad
      if (isTesting || connectionStatus === 'Testing') return;
      
      try {
        const response = await fetch(`http://${esp32IP}/api/status`);
        
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        
        const data = await response.json();
        
        setStatus({
          doseActive: data.doseActive ? 'YES' : 'NO',
          vialTaken: data.vialTaken ? 'YES' : 'NO',
          vialReplaced: data.vialReplaced ? 'YES' : 'NO',
          currentWeight: `${data.currentWeight?.toFixed(2) || '0.00'} g`,
          initialWeight: `${data.initialWeight?.toFixed(2) || '0.00'} g`,
          wifi: data.wifiConnected ? 'Connected' : 'Disconnected',
          time: data.timeSynced ? 'Synced' : 'Not Synced'
        });
        
        setConnectionStatus('Connected');
      } catch (error) {
        setConnectionStatus('Disconnected');
      }
    };

    // Only start interval if we have a valid IP and not testing
    if (esp32IP && !isTesting) {
      const interval = setInterval(fetchStatus, 2000);
      fetchStatus(); // Initial fetch
      
      return () => clearInterval(interval);
    }
  }, [esp32IP, isTesting]);

  // Fetch alerts periodically
  useEffect(() => {
    const fetchAlerts = async () => {
      if (isTesting || connectionStatus === 'Disconnected') return;
      
      try {
        const response = await fetch(`http://${esp32IP}/api/alerts`);
        
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        
        const data = await response.json();
        setAlerts(Array.isArray(data) ? data.reverse() : []);
      } catch (error) {
        console.log('Failed to fetch alerts');
      }
    };

    if (esp32IP && !isTesting) {
      const interval = setInterval(fetchAlerts, 3000);
      fetchAlerts(); // Initial fetch
      
      return () => clearInterval(interval);
    }
  }, [esp32IP, isTesting, connectionStatus]);

  const getAlertIcon = (alert: string) => {
    if (alert.includes('DOSE START')) return '💊';
    if (alert.includes('VIAL TAKEN')) return '⬆️';
    if (alert.includes('VIAL REPLACED')) return '⬇️';
    if (alert.includes('MEDICINE TAKEN')) return '📊';
    if (alert.includes('RESULT') && alert.includes('✓')) return '✅';
    if (alert.includes('RESULT') && alert.includes('✗')) return '❌';
    if (alert.includes('DOSE COMPLETE')) return '🏁';
    if (alert.includes('DOSE CANCELLED')) return '⏰';
    if (alert.includes('SYSTEM') || alert.includes('AUTO TARE')) return '📡';
    if (alert.includes('WEIGHT')) return '⚖️';
    return '📢';
  };

  const getAlertColor = (alert: string) => {
    if (alert.includes('RESULT ✓') || alert.includes('DOSE COMPLETE')) return '#4caf50';
    if (alert.includes('RESULT ✗') || alert.includes('CANCELLED')) return '#f44336';
    return '#2196f3';
  };

  return (
    <div className="app">
      {/* Navbar */}
      <nav className="navbar">
        <div className="nav-brand">
          <span className="nav-icon">💊</span>
          Smart Insulin Monitoring System
          <span className="connection-status" style={{ 
            color: connectionStatus === 'Connected' ? '#4caf50' : '#f44336',
            fontSize: '0.8rem',
            marginLeft: '1rem'
          }}>
            {connectionStatus}
          </span>
        </div>
        <div className="nav-status">
          <span className="status-item">Dose Active: <strong>{status.doseActive}</strong></span>
          <span className="status-item">Vial Taken: <strong>{status.vialTaken}</strong></span>
          <span className="status-item">Vial Replaced: <strong>{status.vialReplaced}</strong></span>
          <span className="status-item">Current Weight: <strong>{status.currentWeight}</strong></span>
        </div>
      </nav>

      {/* Main Content */}
      <main className="main-content">
        <div className="alerts-container">
          <h2 className="alerts-title">Real-time Updates</h2>
          <div className="alerts-list">
            {alerts.length === 0 ? (
              <div className="no-alerts">No alerts yet. Waiting for updates...</div>
            ) : (
              alerts.map((alert, index) => (
                <div 
                  key={index} 
                  className="alert-card"
                  style={{ borderLeftColor: getAlertColor(alert) }}
                >
                  <div className="alert-header">
                    <span className="alert-icon">{getAlertIcon(alert)}</span>
                    <span className="alert-message">{alert}</span>
                  </div>
                </div>
              ))
            )}
          </div>
        </div>

        {/* Debug Section */}
        <div className="debug-section">
          <h3>Connection Debug</h3>
          
          {/* IP Configuration */}
          <div className="config-section">
            <label>
              ESP32 IP Address:
              <input 
                type="text" 
                value={esp32IP}   
                onChange={(e) => setEsp32IP(e.target.value.replace(/\s/g, ''))} // Remove any spaces
                placeholder="192.168.1.100"
                disabled={isTesting}
              />
            </label>
          </div>

          {/* Test Buttons */}
          <div className="test-buttons">
            <button 
              onClick={runFullConnectionTest}
              disabled={isTesting}
              className="test-button"
            >
              {isTesting ? 'Testing...' : 'Test Connection'}
            </button>
            
            <button 
              onClick={() => {
                setDebugInfo('');
                setConnectionStatus('Connecting...');
              }}
              disabled={isTesting}
              className="clear-button"
            >
              Clear Debug
            </button>
          </div>

          {/* Debug Info */}
          {debugInfo && (
            <div className="debug-info">
              <h4>Debug Information:</h4>
              <pre>{debugInfo}</pre>
              
              {/* Direct Links */}
              <div className="direct-links">
                <p>Test these URLs directly in your browser:</p>
                <a href={`http://${esp32IP}/api/status`} target="_blank" rel="noopener noreferrer">
                  http://{esp32IP}/api/status
                </a>
                <br />
                <a href={`http://${esp32IP}/api/alerts`} target="_blank" rel="noopener noreferrer">
                  http://{esp32IP}/api/alerts
                </a>
              </div>
            </div>
          )}

          {/* Status Details */}
          <div className="status-details">
            <h4>Detailed Status:</h4>
            <div className="status-grid">
              <div className="status-item-detailed">
                <span>WiFi:</span>
                <strong>{status.wifi}</strong>
              </div>
              <div className="status-item-detailed">
                <span>Time Sync:</span>
                <strong>{status.time}</strong>
              </div>
              <div className="status-item-detailed">
                <span>Initial Weight:</span>
                <strong>{status.initialWeight}</strong>
              </div>
            </div>
          </div>
        </div>
      </main>
    </div>
  );
};

export default App;