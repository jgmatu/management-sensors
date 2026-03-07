#!/bin/bash
# 10ms is very fast for psql overhead, so we use a small sleep
echo "Starting 10ms stream... Press Ctrl+C to stop."

while true; do
  # Use -c to run the command and -q for quiet mode
  psql -d javi -c "UPDATE articles SET stock_count = stock_count + 1 WHERE name = 'cafe';" > /dev/null 2>&1
  
  # sleep 0.01 is 10ms
  sleep 0.01
done
