#include "mousekey_config.h"
#ifdef MOUSEKEY_ENABLE
#include <stddef.h>
#include <string.h>
#include "port.h"
#include "quantum.h"
#include "mousekey.h"
#include "era_state_sync.h"
#define MOUSEKEY_CFG_SIGNATURE 0x4B53554DUL
#define MOUSEKEY_CFG_VERSION 2U

/* Persisted MOUSE v2: integer report counts and real millisecond durations.
 * v1 is rejected, and the release reset key invalidates the complete store. */
typedef struct __attribute__((packed)) {
    uint16_t cursor_ramp_ms;
    uint16_t wheel_ramp_ms;
    uint8_t interval;
    uint8_t move_delta;
    uint8_t max_speed;
    uint8_t wheel_interval;
    uint8_t wheel_max_speed;
    uint8_t wheel_delta;
    uint8_t version;
    uint8_t reserved;
    uint32_t signature;
} mousekey_config_storage_t;
_Static_assert(sizeof(mousekey_config_storage_t) == 16, "MOUSE storage size");
_Static_assert(offsetof(mousekey_config_storage_t, version) == 10, "MOUSE version offset");
_Static_assert(offsetof(mousekey_config_storage_t, signature) == 12, "MOUSE signature offset");

static mousekey_config_storage_t cfg;

static uint8_t clamp_byte(uint8_t value, uint8_t maximum) {
    return value < 1 ? 1 : value > maximum ? maximum : value;
}
static void defaults(void) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.cursor_ramp_ms = 1000;
    cfg.wheel_ramp_ms = MOUSEKEY_WHEEL_TIME_TO_MAX * MOUSEKEY_WHEEL_INTERVAL;
    cfg.interval = 10;
    cfg.move_delta = 4;
    cfg.max_speed = 16;
    cfg.wheel_interval = MOUSEKEY_WHEEL_INTERVAL;
    cfg.wheel_delta = MOUSEKEY_WHEEL_DELTA;
    cfg.wheel_max_speed = MOUSEKEY_WHEEL_DELTA * MOUSEKEY_WHEEL_MAX_SPEED;
    cfg.version = MOUSEKEY_CFG_VERSION;
    cfg.signature = MOUSEKEY_CFG_SIGNATURE;
}
static void normalize(void) {
    cfg.interval = clamp_byte(cfg.interval, 255);
    cfg.wheel_interval = clamp_byte(cfg.wheel_interval, 255);
    cfg.move_delta = clamp_byte(cfg.move_delta, MOUSEKEY_MOVE_MAX);
    cfg.max_speed = clamp_byte(cfg.max_speed, MOUSEKEY_MOVE_MAX);
    cfg.wheel_delta = clamp_byte(cfg.wheel_delta, MOUSEKEY_WHEEL_MAX);
    cfg.wheel_max_speed = clamp_byte(cfg.wheel_max_speed, MOUSEKEY_WHEEL_MAX);
    cfg.reserved = 0;
}
static void apply_runtime(void) {
    mk_delay = MOUSEKEY_DELAY / 10;
    mk_wheel_delay = MOUSEKEY_WHEEL_DELAY / 10;
    mk_interval = cfg.interval;
    mk_wheel_interval = cfg.wheel_interval;
    mk_move_delta = cfg.move_delta;
    mk_wheel_delta = cfg.wheel_delta;
    mk_cursor_top = cfg.max_speed;
    mk_wheel_top = cfg.wheel_max_speed;
    mk_cursor_ramp_ms = cfg.cursor_ramp_ms;
    mk_wheel_ramp_ms = cfg.wheel_ramp_ms;
    /* Legacy runtime variables are retained for unrelated QMK consumers. */
    mk_max_speed = cfg.cursor_ramp_ms ? (cfg.max_speed + cfg.move_delta / 2) / cfg.move_delta : 1;
    mk_wheel_max_speed = cfg.wheel_ramp_ms ? (cfg.wheel_max_speed + cfg.wheel_delta / 2) / cfg.wheel_delta : 1;
    mk_time_to_max = cfg.cursor_ramp_ms ? 1 : 0;
    mk_wheel_time_to_max = cfg.wheel_ramp_ms ? 1 : 0;
}
static uint16_t nearest(uint16_t value, const uint16_t *choices, uint8_t count) {
    uint16_t best = choices[0];
    uint16_t distance = value > best ? value - best : best - value;
    for (uint8_t i = 1; i < count; ++i) {
        uint16_t d = value > choices[i] ? value - choices[i] : choices[i] - value;
        if (d < distance) { best = choices[i]; distance = d; }
    }
    return best;
}
static uint8_t legacy_get(uint8_t id) {
    static const uint16_t constant[] = {2,4,6,8,10,12,16,24};
    static const uint16_t start[] = {1,2,4,8};
    static const uint16_t top[] = {8,16,24,32,48,64};
    static const uint16_t ramp[] = {500,750,1000,1250,1500,2000};
    static const uint16_t cursor_interval[] = {5,8,10,16,20};
    static const uint16_t wheel_interval[] = {20,40,80,160};
    switch (id) {
        case 1: return cfg.cursor_ramp_ms ? nearest(cfg.move_delta,start,(sizeof(start) / sizeof(start[0]))) : nearest(cfg.move_delta,constant,(sizeof(constant) / sizeof(constant[0])));
        case 2: return nearest(cfg.max_speed,top,(sizeof(top) / sizeof(top[0])));
        case 3: return cfg.cursor_ramp_ms ? nearest(cfg.cursor_ramp_ms,ramp,(sizeof(ramp) / sizeof(ramp[0]))) / 50U : 0;
        case 4: return nearest(cfg.interval,cursor_interval,(sizeof(cursor_interval) / sizeof(cursor_interval[0])));
        case 5: return nearest(cfg.wheel_interval,wheel_interval,(sizeof(wheel_interval) / sizeof(wheel_interval[0])));
        case 6: return cfg.wheel_ramp_ms == 0 ? 0 : cfg.wheel_max_speed <= (4 + MOUSEKEY_WHEEL_DELTA * MOUSEKEY_WHEEL_MAX_SPEED) / 2 ? 1 : 2;
        default: return 0;
    }
}
static uint16_t exact_get(uint8_t id) {
    switch (id) {
        case 8: return cfg.move_delta;
        case 9: return cfg.max_speed;
        case 10: return cfg.cursor_ramp_ms;
        case 11: return cfg.interval;
        case 12: return cfg.wheel_interval;
        case 13: return cfg.wheel_max_speed;
        case 14: return cfg.wheel_ramp_ms;
        default: return 0;
    }
}
static bool assign(uint8_t id, uint16_t value) {
    switch (id) {
        case 1: cfg.move_delta = clamp_byte(value, MOUSEKEY_MOVE_MAX); break;
        case 2: cfg.max_speed = clamp_byte(value, MOUSEKEY_MOVE_MAX); break;
        case 3: cfg.cursor_ramp_ms = value * 50U; break;
        case 4: cfg.interval = clamp_byte(value, 255); break;
        case 5: cfg.wheel_interval = clamp_byte(value, 255); break;
        case 6:
            cfg.wheel_max_speed = value == 0 ? cfg.wheel_delta : value == 1 ? 4 : MOUSEKEY_WHEEL_DELTA * MOUSEKEY_WHEEL_MAX_SPEED;
            cfg.wheel_ramp_ms = value == 0 ? 0 : MOUSEKEY_WHEEL_TIME_TO_MAX * cfg.wheel_interval;
            break;
        case 8: if (value < 1 || value > MOUSEKEY_MOVE_MAX) return false; cfg.move_delta = value; break;
        case 9: if (value < 1 || value > MOUSEKEY_MOVE_MAX) return false; cfg.max_speed = value; break;
        case 10: cfg.cursor_ramp_ms = value; break;
        case 11: if (value < 1 || value > 255) return false; cfg.interval = value; break;
        case 12: if (value < 1 || value > 255) return false; cfg.wheel_interval = value; break;
        case 13: if (value < 1 || value > MOUSEKEY_WHEEL_MAX) return false; cfg.wheel_max_speed = value; break;
        case 14: cfg.wheel_ramp_ms = value; break;
        default: return false;
    }
    return true;
}

EECONFIG_DEBOUNCE_HELPER(mousekey_cfg, EECONFIG_USER_MOUSEKEY, cfg);
void mousekey_config_init(void) {
    eeconfig_init_mousekey_cfg();
    if (cfg.version != MOUSEKEY_CFG_VERSION || cfg.signature != MOUSEKEY_CFG_SIGNATURE) {
        defaults();
        eeconfig_flush_mousekey_cfg(true);
    } else {
        mousekey_config_storage_t previous = cfg;
        normalize();
        if (memcmp(&previous,&cfg,sizeof(cfg))) eeconfig_flag_mousekey_cfg(true);
    }
    apply_runtime();
}
void mousekey_config_storage_stage_defaults(void) {
    mousekey_config_storage_t previous = cfg;
    uint8_t previous_dirty = dirty_mousekey_cfg;
    defaults();
    eeconfig_flush_mousekey_cfg(true);
    cfg = previous;
    dirty_mousekey_cfg = previous_dirty;
}
void mousekey_config_storage_apply_defaults(void) { defaults(); apply_runtime(); eeconfig_flag_mousekey_cfg(true); }
void mousekey_config_storage_flush(bool force) { eeconfig_flush_mousekey_cfg(force); }
bool mousekey_config_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || length < 3) return false;
    uint8_t id = data[2];
    if (data[0] == id_custom_save) { mousekey_config_storage_flush(true); return true; }
    if (length < 4) goto unhandled;
    if (data[0] == id_custom_get_value) {
        if (id == 7) {
            if (length < 5) goto unhandled;
            data[3] = 0xE4; data[4] = 1;
        } else if (id >= 8 && id <= 14) {
            if (length < 6) goto unhandled;
            uint16_t value = exact_get(id);
            data[3] = value >> 8; data[4] = value & 0xFF; data[5] = 0xE4;
        } else if (id >= 1 && id <= 6) data[3] = legacy_get(id);
        else goto unhandled;
        return true;
    }
    if (data[0] == id_custom_set_value) {
        bool exact = id >= 8 && id <= 14;
        if (exact && length < 5) goto unhandled;
        uint16_t value = exact ? ((uint16_t)data[3] << 8) | data[4] : data[3];
        mousekey_config_storage_t previous = cfg;
        if (!assign(id,value)) goto unhandled;
        apply_runtime();
        if (memcmp(&previous,&cfg,sizeof(cfg))) { eeconfig_flag_mousekey_cfg(true); era_state_sync_bump_config(); }
        return true;
    }
unhandled:
    data[0] = id_unhandled;
    return false;
}
#endif
