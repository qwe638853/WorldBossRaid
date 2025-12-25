#!/bin/bash

echo "=== Graceful Shutdown Test ==="

# 清理舊的伺服器進程（如果存在）
echo "Checking for existing server processes..."
OLD_PIDS=$(lsof -ti :8888 2>/dev/null)
if [ -n "$OLD_PIDS" ]; then
    echo "Found existing server on port 8888, killing old processes..."
    kill -9 $OLD_PIDS 2>/dev/null
    sleep 1
fi

# 清理舊的日誌文件
rm -f server.log client.log

# 啟動伺服器（背景執行）
../../build/server > server.log 2>&1 &
SERVER_PID=$!
sleep 2

# 檢查共享記憶體（關閉前）
echo "Shared memory before shutdown:"
ipcs -m | grep $(whoami) || echo "No shared memory found"

# 發送 SIGQUIT 信號（模擬 Ctrl+\）
echo "Sending SIGQUIT to server..."
kill -SIGQUIT $SERVER_PID
sleep 2

# 檢查共享記憶體（關閉後）
echo "Shared memory after shutdown:"
ipcs -m | grep $(whoami) || echo "No shared memory found (cleaned up)"

# 檢查日誌中是否有清理訊息
if grep -q "cleaning up\|shutting down" server.log; then
    echo "✅ Graceful shutdown detected"
else
    echo "⚠️  No shutdown message in logs"
fi

# 清理
wait $SERVER_PID 2>/dev/null

echo "Test completed. Check server.log for details."

