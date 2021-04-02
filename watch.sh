#!/bin/bash

# Inspired by https://gist.github.com/dln/3128643

# Wait for file changes, clear the terminal, and build
inotifywait -q -r -e create,modify,move,delete src/ && \
  echo -ne "\033c" && \
  (make && echo done.) 2>&1

# Kill any running server
ccms_pid=$(pidof ccms)
if [ "$ccms_pid" != "" ]; then 
	kill $ccms_pid
fi
# Launch it again in the background
bin/ccms&

# Re-exec ourselves for the next edit
exec $0 $@
