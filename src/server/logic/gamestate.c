/* src/server/logic/gamestate.c */
#include "gamestate.h"
#include "../../common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h> // 用於 mmap
#include <unistd.h>
#include <string.h>
#include <errno.h>

// 內部全域變數 (Singleton pattern)
static GameSharedData *shm = NULL;

// --- 初始化與銷毀 ---

void gamestate_init() {
    // 1. 建立匿名共享記憶體
    shm = mmap(NULL, sizeof(GameSharedData), 
               PROT_READ | PROT_WRITE, 
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (shm == MAP_FAILED) {
        LOG_ERROR("Failed to create shared memory: %s", strerror(errno));
        exit(1);
    }

    // 2. 初始化 Process-Shared Mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    
    if (pthread_mutex_init(&shm->lock, &attr) != 0) {
        LOG_ERROR("Failed to initialize mutex: %s", strerror(errno));
        exit(1);
    }
    pthread_mutexattr_destroy(&attr);

    // 3. 設定初始數值
    shm->stage = BOSS_STAGE_1;
    shm->max_hp = BOSS_1_MAX_HP;
    shm->current_hp = BOSS_1_MAX_HP;
    shm->online_count = 0;
    shm->is_respawning = false;
    memset(shm->last_killer, 0, sizeof(shm->last_killer));

    // --- 新增：清空玩家紀錄表 ---
    // 這一步很重要，確保伺服器剛啟動時沒有髒數據
    memset(shm->players, 0, sizeof(shm->players));

    LOG_INFO("Shared Memory Initialized. Boss 1 Ready (HP: %d)", BOSS_1_MAX_HP);
}

void gamestate_destroy() {
    if (shm == NULL) return;
    pthread_mutex_destroy(&shm->lock);
    munmap(shm, sizeof(GameSharedData));
    shm = NULL;
    LOG_DEBUG("GameState resources cleaned");
}

// --- 玩家管理 ---

int gamestate_player_join() {
    if (!shm) return 0;
    
    pthread_mutex_lock(&shm->lock);
    shm->online_count++;
    int count = shm->online_count;
    pthread_mutex_unlock(&shm->lock);
    
    return count;
}

void gamestate_player_leave() {
    if (!shm) return;
    
    pthread_mutex_lock(&shm->lock);
    if (shm->online_count > 0) {
        shm->online_count--;
    }
    pthread_mutex_unlock(&shm->lock);
}

// --- 狀態讀取與寫入 ---

void gamestate_get_snapshot(GameSharedData *out_data) {
    if (!shm || !out_data) return;

    pthread_mutex_lock(&shm->lock);
    *out_data = *shm; // 記憶體拷貝
    pthread_mutex_unlock(&shm->lock);
}

bool gamestate_apply_damage(int damage, const char* attacker_name) {
    if (!shm) return false;
    
    bool just_killed = false;

    pthread_mutex_lock(&shm->lock);

    // 1. 如果正在重生中或已經死透，不接受傷害
    if (shm->is_respawning || shm->stage == BOSS_STAGE_DEAD) {
        pthread_mutex_unlock(&shm->lock);
        return false;
    }

    // 2. 扣血
    shm->current_hp -= damage;

    // 3. 判斷是否死亡
    if (shm->current_hp <= 0) {
        shm->current_hp = 0;
        
        // 只有當這一下剛好打死它，才標記 just_killed
        if (!shm->is_respawning) {
            just_killed = true;
            shm->is_respawning = true; // 鎖住，進入重生倒數
            
            if (attacker_name) {
                strncpy(shm->last_killer, attacker_name, 31);
            }
            LOG_INFO("Boss Killed by %s!", attacker_name ? attacker_name : "Unknown");
        }
    }

    pthread_mutex_unlock(&shm->lock);
    return just_killed;
}

void gamestate_spawn_next_boss() {
    if (!shm) return;

    pthread_mutex_lock(&shm->lock);
    
    if (shm->stage == BOSS_STAGE_1) {
        shm->stage = BOSS_STAGE_2;
        shm->max_hp = BOSS_2_MAX_HP;
        shm->current_hp = BOSS_2_MAX_HP;
        LOG_INFO("Boss 2 Spawned! (HP: %d)", BOSS_2_MAX_HP);
    } else {
        shm->stage = BOSS_STAGE_DEAD;
        shm->current_hp = 0;
        LOG_INFO("All Bosses Defeated. Game Complete!");
        LOG_INFO("=== CONGRATULATIONS! WORLD BOSS RAID CLEARED ===");
    }
    
    // 解除重生鎖定，清空擊殺者
    shm->is_respawning = false;
    memset(shm->last_killer, 0, sizeof(shm->last_killer));
    
    pthread_mutex_unlock(&shm->lock);
}

// --- 新增：彩蛋連擊邏輯 ---

int gamestate_update_streak(const char* name, int current_dice, bool is_win) {
    if (!shm || !name) return 0;

    int streak = 0;
    pthread_mutex_lock(&shm->lock);

    int slot_index = -1;
    
    // 1. 搜尋該玩家是否已有紀錄
    for (int i = 0; i < MAX_TRACKED_PLAYERS; i++) {
        // 如果名字有對上 (且不是空字串)
        if (shm->players[i].name[0] != '\0' && 
            strcmp(shm->players[i].name, name) == 0) {
            slot_index = i;
            break;
        }
    }
    
    // 2. 如果是新玩家，找一個空位
    if (slot_index == -1) {
        for (int i = 0; i < MAX_TRACKED_PLAYERS; i++) {
            if (shm->players[i].name[0] == '\0') { // 找到空位
                slot_index = i;
                strncpy(shm->players[i].name, name, 31);
                // 新玩家初始化
                shm->players[i].streak_count = 0;
                shm->players[i].last_dice = 0;
                break;
            }
        }
    }

    // 3. 更新連勝紀錄
    if (slot_index != -1) {
        PlayerHistory *p = &shm->players[slot_index];

        if (is_win) {
            if (p->last_dice == current_dice) {
                // 骰出一模一樣的數字且贏了 -> 連擊 +1
                p->streak_count++;
            } else {
                // 數字不同 -> 重置為 1 (這是新的連擊起點)
                p->streak_count = 1;
            }
        } else {
            // 輸了 -> 連擊中斷
            p->streak_count = 0;
        }
        
        // 紀錄這次的數字
        p->last_dice = current_dice;
        streak = p->streak_count;
    }

    pthread_mutex_unlock(&shm->lock);
    return streak; // 回傳目前的連擊數
}