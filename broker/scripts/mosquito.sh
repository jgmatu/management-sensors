#!/bin/bash

# 1. Install Mosquitto (Broker and Clients)
echo "Installing Mosquitto via DNF..."
dnf install -y mosquitto

# 2. Configure for local dev (Fixes "Connection Refused" on v2.0+)
# We append the listener settings to allow anonymous local traffic
echo "Updating configuration to allow anonymous connections on port 1883..."
cat <<EOF >> /etc/mosquitto/mosquitto.conf

# Custom settings for local C++ dev
listener 1883
allow_anonymous true
EOF

# 3. Open Firewall port (Important for RHEL)
echo "Opening firewall port 1883..."
firewall-cmd --permanent --add-port=1883/tcp
firewall-cmd --reload

# 4. Start and Enable Service
echo "Starting Mosquitto service..."
systemctl enable --now mosquitto

# 5. Check Status
if systemctl is-active --quiet mosquitto; then
    echo "SUCCESS: Mosquitto is running on port 1883."
else
    echo "ERROR: Mosquitto failed to start. Check 'journalctl -u mosquitto'"
    exit 1
fi