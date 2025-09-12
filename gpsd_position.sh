#!/bin/bash

SERVICE_INFO=$(avahi-browse -t -r -p _gps._tcp 2>/dev/null | grep "=;.*IPv4" | head -1)
if [ -z "$SERVICE_INFO" ]; then
    SERVICE_INFO=$(avahi-browse -t -r -p _gps._tcp 2>/dev/null | grep "=;" | head -1)
fi
if [ -z "$SERVICE_INFO" ]; then
    echo "No GPS service found on network"
    exit 1
fi

HOSTNAME=$(echo "$SERVICE_INFO" | cut -d';' -f7)
IP=$(echo "$SERVICE_INFO" | cut -d';' -f8)
PORT=$(echo "$SERVICE_INFO" | cut -d';' -f9)

echo "service:      $HOSTNAME ($IP:$PORT)"

POSITION=$(echo '?POLL;' | nc -w 2 "$IP" "$PORT" 2>/dev/null)

if [ -n "$POSITION" ]; then
    if command -v jq &> /dev/null; then
      # echo "response:     $POSITION"
        echo "$POSITION" | jq -r '"latitude:     \(.lat)°\nlongitude:    \(.lon)°\naltitude:     \(.alt)m\nsamples:      \(.samples)\nuncertainty:  \(.lat_err)m N/S, \(.lon_err)m E/W"'
    else
        echo "response:     $POSITION"
    fi
else
    echo "Failed to get position"
fi

