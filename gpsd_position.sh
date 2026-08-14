#!/bin/bash

SERVICE_INFO=$(avahi-browse -t -r -p _gps._tcp 2>/dev/null | grep "=;.*IPv4" | head -1)
if [ -z "$SERVICE_INFO" ]; then
    SERVICE_INFO=$(avahi-browse -t -r -p _gps._tcp 2>/dev/null | grep "=;" | head -1)
fi
if [ -z "$SERVICE_INFO" ]; then
    HOSTNAME=localhost
    IP=127.0.0.1
    PORT=2948
else
    HOSTNAME=$(echo "$SERVICE_INFO" | cut -d';' -f7)
    IP=$(echo "$SERVICE_INFO" | cut -d';' -f8)
    PORT=$(echo "$SERVICE_INFO" | cut -d';' -f9)
fi

echo "service:      $HOSTNAME ($IP:$PORT)"

# Queried over bash's own /dev/tcp rather than nc, so that nothing beyond bash need be installed. stderr is
# stashed over the connect only, to keep bash's own "Connection refused" out of the way of the message below.
exec 4>&2 2>/dev/null
exec 3<>"/dev/tcp/$IP/$PORT"
CONNECTED=$?
exec 2>&4 4>&-

if [ $CONNECTED -ne 0 ]; then
    echo "Failed to connect to $IP:$PORT (is gpsd_averaged running?)"
    exit 1
fi

printf '?POLL;\n' >&3
IFS= read -r -t 2 POSITION <&3
RECEIVED=$?
exec 3<&-
exec 3>&-
POSITION=${POSITION%$'\r'}

if [ $RECEIVED -ne 0 ] || [ -z "$POSITION" ]; then
    echo "Connected to $IP:$PORT but no response"
    exit 1
fi

if command -v jq &> /dev/null; then
    echo "$POSITION" | jq -r '"latitude:     \(.lat)°\nlongitude:    \(.lon)°\naltitude:     \(.alt)m\nsamples:      \(.samples)\nuncertainty:  \(.lat_err)m N/S, \(.lon_err)m E/W"'
else
    echo "response:     $POSITION"
fi
