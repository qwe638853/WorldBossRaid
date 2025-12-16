# World Boss Raid - High Concurrency Game System

**World Boss Raid** 是一套基於 Linux System Programming 開發的高併發多人連線遊戲系統。本專案展示了從底層 Socket 通訊、自定義二進位協定、多行程/多執行緒架構 (Multi-Process/Multi-Thread) 到 IPC (行程間通訊) 的完整實作。

系統模擬了大量客戶端同時攻擊世界王 (World Boss) 的場景，並透過 Ncurses 提供即時的終端機視覺化介面。

## Key Features (專案特色)

* **高效能伺服器 (High Performance Server)**
    * 採用 **Master-Worker 多行程架構** (Process Pool)，有效利用多核心 CPU。
    * 使用 **Shared Memory (共享記憶體)** 管理世界王血量與遊戲狀態，實現零拷貝 (Zero-Copy) 數據共享。
    * 實作 **Semaphore (信號量)** 機制，確保並發攻擊下的資料一致性 (Data Consistency) 與執行緒安全。
* **高併發客戶端 (Stress Test Client)**
    * 採用 **Multi-thread (Pthreads)** 架構，單一 Client 程式可模擬 100+ 個併發連線進行壓力測試。
    * **Ncurses TUI 介面**：即時繪製 Boss 動畫、動態血條與攻擊特效，且不影響傳輸效能。
* **自定義通訊協定 (Custom Binary Protocol)**
    * 不依賴 HTTP/JSON，自行設計緊湊的 **Binary Protocol**。
    * 支援 **Checksum 完整性檢查**，防止封包損壞。
    * 封包結構包含：Header (Length, OpCode, SeqNum) + Body (Payload Union)。
* **容錯與穩定性**
    * 實作 **Graceful Shutdown**：捕捉 Signal (SIGINT)，確保伺服器關閉時正確釋放 IPC 資源 (避免殭屍行程)。
    * 具備心跳機制 (Heartbeat) 與斷線偵測。

## Project Structure (檔案架構)

```text
WorldBossRaid/
├── Makefile                # 自動化編譯腳本
├── README.md               # 專案說明文件
├── src/
│   ├── common/             # [共用層] 協定與工具
│   │   └── protocol.h      # 定義 Packet 結構、OpCode、Payload
│   │
│   ├── server/             # [伺服器端]
│   │   ├── server.c        # 程式入口，Socket 初始化，Master-Worker 管理
│   │   └── logic/          # [業務邏輯層]
│   │       ├── gamestate.c # IPC 管理 (Shared Memory 建立與銷毀)
│   │       ├── gamestate.h
│   │       ├── dice.c      # 傷害計算與機率邏輯
│   │       └── dice.h
│   │
│   └── client/             # [客戶端]
│       ├── client.c        # 建立連線、收發封包、多執行緒壓力測試
│       └── ui/             # [介面層]
│           ├── boss.c      # Ncurses 繪製 Boss 與血條
│           ├── boss.h
│           ├── player.c    # 繪製玩家狀態
│           └── player.h
```

## Build & Run (編譯與執行)

### Prerequisites (環境需求)

* Linux Environment (Ubuntu/Debian/CentOS)
* GCC Compiler
* Make
* Ncurses Library

```bash
sudo apt-get install libncurses5-dev libncursesw5-dev
```

### Compilation (編譯)

專案包含完整的 Makefile，只需執行：

```bash
make
```

編譯成功後將產生 server 與 client 兩個執行檔。

### Usage (執行)

1. 啟動伺服器 (Server)

```bash
./server
# Server 將會在 Port 8888 啟動，並建立 Worker Pool 等待連線
```

2. 啟動客戶端 (Client)

```bash
./client
# 啟動 Ncurses 介面，自動連線並開始攻擊
```

3. 清除編譯檔

```bash
make clean
```

## Protocol Specification (協定規範)

通訊採用固定 Header 長度 + 變長 Body 的設計：

| Byte Offset | Field | Type | Description |
|------------|-------|------|-------------|
| 0-3 | Length | uint32_t | 封包總長度 (Header + Body) |
| 4-5 | OpCode | uint16_t | 操作碼 (e.g., 0x11 Attack) |
| 6-7 | Checksum | uint16_t | 簡單總和檢查碼 |
| 8-11 | SeqNum | uint32_t | 封包序列號 |
| 12+ | Body | Union | 根據 OpCode 決定 Payload 結構 |

主要 OpCodes:

* OP_JOIN (0x10): 玩家加入請求
* OP_ATTACK (0x11): 攻擊請求 (含傷害數值)
* OP_GAME_STATE (0x21): 廣播 Boss 當前血量 (Server -> Client)

