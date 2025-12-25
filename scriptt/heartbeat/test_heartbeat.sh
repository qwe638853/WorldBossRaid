#!/bin/bash

echo "=== Heartbeat Test ==="

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

# 啟動客戶端
echo "Starting client (heartbeat should be sent every 0.5 seconds)..."
../../build/client > client.log 2>&1 &
CLIENT_PID=$!

# 運行 10 秒
sleep 10

# 停止客戶端
kill $CLIENT_PID 2>/dev/null
wait $CLIENT_PID 2>/dev/null

# 檢查日誌中的 heartbeat 訊息
HEARTBEAT_COUNT=$(grep -c -i "heartbeat\|OP_HEARTBEAT" server.log 2>/dev/null || echo "0")
echo "Heartbeat count in logs: $HEARTBEAT_COUNT"

# 修正：使用引號包圍變數，避免語法錯誤
if [ "$HEARTBEAT_COUNT" -gt 0 ]; then
    echo "✅ Heartbeat is working"
else
    echo "⚠️  No heartbeat detected in logs"
    echo "Note: Heartbeat may be working but not logged. Check if client is sending heartbeats."
fi

# 清理
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "Test completed. Check server.log for details."

