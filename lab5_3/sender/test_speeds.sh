#!/bin/bash

# Speed Testing Script for Lab 5.3
# Tests multiple speeds to find the maximum reliable speed

# Test message (should be representative)
MESSAGE="hello world"

# Speed range to test (chars/sec)
SPEEDS=(1 5 10 12 15 18 20 22 25)

echo "================================================"
echo "Lab 5.3 Speed Testing"
echo "================================================"
echo "Message: $MESSAGE"
echo ""

for SPEED in "${SPEEDS[@]}"; do
    echo "----------------------------------------"
    echo "Testing speed: $SPEED chars/sec"
    echo "----------------------------------------"
    sudo ./send 1 "$MESSAGE" $SPEED
    echo ""
    echo "Did the ESP32 receive correctly? (y/n)"
    read -r response

    if [ "$response" = "n" ] || [ "$response" = "N" ]; then
        echo "FAILED at $SPEED chars/sec"
        echo ""
        echo "Last successful speed was the previous test"
        exit 0
    else
        echo "SUCCESS at $SPEED chars/sec"
        echo ""
        sleep 2
    fi
done

echo "All speeds tested successfully!"
echo "Try testing even higher speeds!"
