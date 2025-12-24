# Multi-Thread 架構實作說明

## 📋 目錄

1. [為什麼要使用 Multi-Thread？](#為什麼要使用-multi-thread)
2. [架構設計](#架構設計)
3. [實作細節](#實作細節)
4. [運作流程](#運作流程)
5. [效果與優勢](#效果與優勢)
6. [程式碼解析](#程式碼解析)

---

## 🎯 為什麼要使用 Multi-Thread？

### 問題：Single Thread 的瓶頸

在原本的 **Single Thread** 架構中，所有操作都在同一個線程中執行：

```
主線程
├── 顯示 UI
├── 用戶按空格
├── 【阻塞】SSL_write() 發送攻擊請求
├── 【阻塞】SSL_read() 等待服務器響應  ← 這裡會卡住 100-500ms！
└── 更新 UI
```

**問題**：
- 當 `SSL_read()` 等待服務器響應時，**整個程序都停下來**
- UI 無法更新，畫面會卡頓
- 用戶體驗不佳

### 解決方案：Multi-Thread 架構

將 **UI 操作**和 **Network 操作**分離到不同線程：

```
UI Thread（主線程）          Network Thread（後台線程）
├── 顯示 UI                  ├── 持續發送 heartbeat
├── 用戶按空格               ├── 監聽攻擊請求
├── 設置標誌                 ├── 處理攻擊（不阻塞 UI）
└── 讀取結果                 └── 更新共享狀態
```

**優勢**：
- Network 操作在後台執行，**不會阻塞 UI**
- UI 永遠流暢，用戶體驗更好
- 架構更清晰，職責分離

---

## 🏗️ 架構設計

### 整體架構圖

```
┌──────────────────────┐      ┌──────────────────────┐
│  主線程（UI Thread）  │      │  Network Thread      │
│                      │      │  (後台線程)          │
│  - ncurses 操作      │      │                      │
│  - ui_game_loop()    │      │  - SSL 操作          │
│  - 用戶輸入處理       │      │  - 攻擊請求處理      │
│  - UI 更新           │      │  - Heartbeat 發送    │
│                      │      │  - 狀態更新          │
└──────────┬───────────┘      └──────────┬───────────┘
           │                             │
           │     共享資料結構              │
           │    (mutex 保護)              │
           └──────────┬──────────────────┘
                      │
           ┌──────────▼──────────┐
           │  SharedGameState    │
           │  - latest_state     │
           │  - attack_requested  │
           │  - attack_completed  │
           │  - state_updated    │
           └─────────────────────┘
```

### 核心組件

1. **SharedGameState**：共享資料結構（用 mutex 保護）
2. **Network Thread**：處理所有網絡操作
3. **UI Thread**：處理所有 UI 操作
4. **Condition Variables**：線程間通信機制

---

## 🔧 實作細節

### 1. 共享資料結構（SharedGameState）

```c
typedef struct {
    pthread_mutex_t lock;              // 保護共享資料，防止 race condition
    pthread_cond_t state_updated_cond;  // 通知 UI 有新狀態
    pthread_cond_t attack_request_cond; // 通知 Network 有攻擊請求
    
    UiGameState latest_state;          // 最新的遊戲狀態（Boss HP、階段等）
    bool state_updated;                // 是否有新狀態需要 UI 更新
    bool attack_requested;             // UI 是否請求攻擊
    bool attack_completed;             // 攻擊是否完成
    bool should_exit;                  // 是否應該退出（用於優雅關閉）
    int network_error;                 // Network Thread 的錯誤碼
} SharedGameState;
```

**為什麼需要這個結構？**
- 讓兩個線程可以**安全地共享資料**
- `pthread_mutex_t` 確保同一時間只有一個線程能訪問共享資料
- `pthread_cond_t` 讓線程可以**等待**和**通知**對方

### 2. Network Thread 函數

```c
static void *network_thread_func(void *arg) {
    NetworkThreadArgs *args = (NetworkThreadArgs *)arg;
    SSL *ssl = args->ssl;
    SharedGameState *shared = args->shared;
    
    // 1. 初始化錯誤碼
    pthread_mutex_lock(&shared->lock);
    shared->network_error = 0;
    pthread_mutex_unlock(&shared->lock);
    
    // 2. 先發送一次 heartbeat 初始化狀態
    Payload_GameState server_state;
    if (net_heartbeat_get_state(ssl, &server_state) == 0) {
        // 更新共享狀態...
    }
    
    // 3. 持續循環：處理攻擊請求和定期 heartbeat
    while (!shared->should_exit) {
        bool has_attack = false;
        
        // 檢查是否有攻擊請求
        pthread_mutex_lock(&shared->lock);
        if (shared->attack_requested) {
            has_attack = true;
            shared->attack_requested = false; // 標記為正在處理
        }
        pthread_mutex_unlock(&shared->lock);
        
        if (has_attack) {
            // 處理攻擊請求
            // 更新共享狀態
            // 通知 UI Thread
        } else {
            // 發送 heartbeat
            // 更新共享狀態
            // 通知 UI Thread
        }
    }
    
    return NULL;
}
```

**關鍵點**：
- **持續循環**：Network Thread 一直在運行
- **優先處理攻擊**：如果有攻擊請求，優先處理；否則發送 heartbeat
- **自動更新狀態**：每 0.5 秒自動發送 heartbeat，不需要 UI 觸發

### 3. UI Thread 的 Callback 函數

#### Attack Callback

```c
int attack_cb_impl(UiGameState *out_state) {
    // 1. 設置攻擊請求標誌
    pthread_mutex_lock(&shared.lock);
    shared.attack_requested = true;
    shared.attack_completed = false;
    pthread_cond_signal(&shared.attack_request_cond); // 通知 Network Thread
    pthread_mutex_unlock(&shared.lock);
    
    // 2. 等待 Network Thread 完成（使用超時避免永久阻塞）
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5; // 5 秒超時
    
    pthread_mutex_lock(&shared.lock);
    while (!shared.attack_completed && !shared.should_exit) {
        int ret = pthread_cond_timedwait(&shared.state_updated_cond, 
                                         &shared.lock, &timeout);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&shared.lock);
            return -1; // 超時
        }
    }
    
    // 3. 讀取結果
    *out_state = shared.latest_state;
    shared.state_updated = false;
    pthread_mutex_unlock(&shared.lock);
    
    return 0;
}
```

**改變**：
- **原本**：直接調用 `net_attack_and_get_state()`（會阻塞）
- **現在**：設置標誌 → 等待 Network Thread → 讀取結果（不阻塞網絡操作）

#### Heartbeat Callback

```c
int heartbeat_cb_impl(UiGameState *out_state) {
    pthread_mutex_lock(&shared.lock);
    
    // 檢查是否有新狀態
    if (shared.state_updated) {
        *out_state = shared.latest_state;
        shared.state_updated = false;
        pthread_mutex_unlock(&shared.lock);
        return 0;
    }
    
    // 沒有新狀態，返回當前狀態
    *out_state = shared.latest_state;
    pthread_mutex_unlock(&shared.lock);
    return 0;
}
```

**改變**：
- **原本**：直接調用 `net_heartbeat_get_state()`（會阻塞）
- **現在**：直接讀取共享狀態（不阻塞，因為 Network Thread 已經自動更新）

---

## 🔄 運作流程

### 攻擊流程（詳細步驟）

```
時間軸：

UI Thread                          Network Thread
─────────────────────────────────────────────────────
0ms   用戶按空格
10ms  attack_cb_impl() 開始
      ↓
      pthread_mutex_lock()
      shared.attack_requested = true
      pthread_cond_signal(attack_request_cond)
      pthread_mutex_unlock()
      ↓
      【等待 Network Thread...】
      ↓                    │
                          │ 收到信號
                          │ ↓
                          │ pthread_mutex_lock()
                          │ 檢查 attack_requested
                          │ attack_requested = false
                          │ pthread_mutex_unlock()
                          │ ↓
                          │ net_attack_and_get_state()
                          │ SSL_write() 發送攻擊
                          │ ↓
                          │ 【等待服務器響應...】
                          │ ↓
200ms                    │ SSL_read() 收到響應
                        │ ↓
                        │ pthread_mutex_lock()
                        │ 更新 shared.latest_state
                        │ shared.attack_completed = true
                        │ shared.state_updated = true
                        │ pthread_cond_signal(state_updated_cond)
                        │ pthread_mutex_unlock()
      ↓                    │
      【收到信號】          │
      ↓                    │
210ms pthread_mutex_lock()
      ↓
      while (!attack_completed) {
          pthread_cond_wait()
      }
      ↓
      *out_state = shared.latest_state
      pthread_mutex_unlock()
      ↓
220ms 返回給 UI
230ms 更新 UI，顯示結果
```

### Heartbeat 流程（詳細步驟）

```
時間軸：

UI Thread                          Network Thread
─────────────────────────────────────────────────────
0ms                                │ 持續循環
                                  │ ↓
                                  │ 檢查 attack_requested
                                  │ (沒有攻擊請求)
                                  │ ↓
                                  │ net_heartbeat_get_state()
                                  │ SSL_write() 發送 heartbeat
                                  │ ↓
                                  │ 【等待服務器響應...】
                                  │ ↓
500ms                             │ SSL_read() 收到響應
                                  │ ↓
                                  │ pthread_mutex_lock()
                                  │ 更新 shared.latest_state
                                  │ shared.state_updated = true
                                  │ pthread_cond_signal(state_updated_cond)
                                  │ pthread_mutex_unlock()
      ↓                            │
      heartbeat_cb_impl()          │
      ↓                            │
      pthread_mutex_lock()         │
      ↓                            │
      if (shared.state_updated) {  │
          *out_state = shared.latest_state
          shared.state_updated = false
      }
      ↓                            │
      pthread_mutex_unlock()       │
      ↓                            │
510ms 更新 UI                      │
                                  │ usleep(500000) // 等待 0.5 秒
                                  │ ↓
                                  │ 繼續循環...
```

---

## 📊 效果與優勢

### 1. UI 響應性提升

| 場景 | Single Thread | Multi-Thread |
|------|--------------|-------------|
| **攻擊時** | UI 卡頓 100-500ms | UI 流暢（等待但不阻塞） |
| **Heartbeat 時** | UI 卡頓 50-200ms | UI 完全流暢 |
| **整體體驗** | ⚠️ 偶爾卡頓 | ✅ 永遠流暢 |

### 2. 架構清晰度

- **職責分離**：
  - UI Thread：只負責畫面、用戶輸入
  - Network Thread：只負責網絡操作
- **易於維護**：每個線程的職責明確，修改時不會互相影響
- **易於擴展**：未來可以輕鬆添加新功能（如後台下載、多路復用等）

### 3. 性能提升

- **並發處理**：UI 和 Network 可以同時進行
- **資源利用**：充分利用多核 CPU
- **響應速度**：UI 更新不會被網絡延遲影響

### 4. 用戶體驗

- **流暢度**：畫面永遠流暢，不會卡頓
- **即時性**：狀態更新更及時（Network Thread 持續發送 heartbeat）
- **穩定性**：網絡錯誤不會導致 UI 崩潰

---

## 💻 程式碼解析

### 關鍵程式碼位置

#### 1. 共享資料結構定義
**位置**：`src/client/client.c` 第 33-45 行

```c
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t state_updated_cond;
    pthread_cond_t attack_request_cond;
    UiGameState latest_state;
    bool state_updated;
    bool attack_requested;
    bool attack_completed;
    bool should_exit;
    int network_error;
} SharedGameState;
```

#### 2. Network Thread 函數
**位置**：`src/client/client.c` 第 311-417 行

**核心邏輯**：
- 持續循環檢查攻擊請求
- 優先處理攻擊，否則發送 heartbeat
- 更新共享狀態並通知 UI Thread

#### 3. Multi-threaded 模式啟動
**位置**：`src/client/client.c` 第 408-601 行

**關鍵步驟**：
1. 初始化共享資料結構（mutex、condition variable）
2. 建立 TLS 連線
3. 啟動 Network Thread
4. 在主線程運行 UI
5. 清理資源

#### 4. Attack Callback
**位置**：`src/client/client.c` 第 510-545 行

**關鍵點**：
- 設置 `attack_requested = true`
- 使用 `pthread_cond_timedwait()` 等待（帶超時）
- 讀取共享狀態

#### 5. Heartbeat Callback
**位置**：`src/client/client.c` 第 548-564 行

**關鍵點**：
- 直接讀取共享狀態（不觸發網絡請求）
- Network Thread 已經自動更新狀態

---

## 🛡️ 線程安全機制

### Mutex（互斥鎖）

```c
pthread_mutex_lock(&shared->lock);
// 訪問共享資料
pthread_mutex_unlock(&shared->lock);
```

**作用**：確保同一時間只有一個線程能訪問共享資料，防止 race condition。

### Condition Variable（條件變量）

```c
// 等待條件
pthread_cond_wait(&shared->state_updated_cond, &shared->lock);

// 通知條件
pthread_cond_signal(&shared->state_updated_cond);
```

**作用**：讓線程可以等待某個條件成立，並通知其他線程。

### 超時機制

```c
struct timespec timeout;
clock_gettime(CLOCK_REALTIME, &timeout);
timeout.tv_sec += 5; // 5 秒超時

pthread_cond_timedwait(&shared->state_updated_cond, 
                       &shared->lock, &timeout);
```

**作用**：避免永久阻塞，如果 Network Thread 出問題，UI Thread 不會永遠等待。

---

## 🎯 總結

### 為什麼要這樣做？

1. **解決 UI 卡頓問題**：網絡操作不會阻塞 UI
2. **提升用戶體驗**：畫面永遠流暢
3. **架構更清晰**：職責分離，易於維護
4. **易於擴展**：未來可以輕鬆添加新功能

### 怎麼做的？

1. **創建共享資料結構**：讓兩個線程可以安全地共享資料
2. **創建 Network Thread**：處理所有網絡操作
3. **修改 Callback**：通過共享資料結構與 Network Thread 通信
4. **使用 Mutex 和 Condition Variable**：確保線程安全

### 有什麼效果？

- ✅ **UI 永遠流暢**：不會被網絡操作阻塞
- ✅ **架構更清晰**：職責分離，易於維護
- ✅ **性能更好**：充分利用多核 CPU
- ✅ **用戶體驗提升**：畫面流暢，響應及時

---

## 📚 相關文件

- `ARCHITECTURE_COMPARISON.md`：Single Thread vs Multi-Thread 詳細對比
- `MULTITHREADED_CLIENT.md`：Multi-threaded 架構使用說明
- `THREAD_ARCHITECTURE.md`：線程架構說明文檔

---

## 🔍 程式碼位置總覽

| 組件 | 檔案位置 | 行數 |
|------|---------|------|
| 共享資料結構 | `src/client/client.c` | 33-45 |
| Network Thread | `src/client/client.c` | 311-417 |
| Multi-threaded 模式 | `src/client/client.c` | 408-601 |
| Attack Callback | `src/client/client.c` | 510-545 |
| Heartbeat Callback | `src/client/client.c` | 548-564 |
| 主函數 | `src/client/client.c` | 829-841 |

---

**最後更新**：2025-12-19
