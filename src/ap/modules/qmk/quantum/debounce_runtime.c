#include "debounce_runtime.h"
#include "debounce.h"
#include "matrix.h"
#include <stddef.h>
#include <string.h>

#ifndef QMK_DEFAULT_DEBOUNCE_TYPE
#define QMK_DEFAULT_DEBOUNCE_TYPE      sym_defer_pk
#endif
#ifndef QMK_DEFAULT_DEBOUNCE_DELAY
#ifdef DEBOUNCE
#define QMK_DEFAULT_DEBOUNCE_DELAY     DEBOUNCE
#else
#define QMK_DEFAULT_DEBOUNCE_DELAY     5U
#endif
#endif

// V251115R3: 보드 config.h의 디바운스 기본값을 런타임 기본값으로 반영
#define DEBOUNCE_RUNTIME_CFG_sym_defer_pk        { .type = DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK,       .pre_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY), .post_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY) }
#define DEBOUNCE_RUNTIME_CFG_sym_eager_pk        { .type = DEBOUNCE_RUNTIME_TYPE_SYM_EAGER_PK,       .pre_ms = 1U,                                       .post_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY) }
#define DEBOUNCE_RUNTIME_CFG_asym_eager_defer_pk { .type = DEBOUNCE_RUNTIME_TYPE_ASYM_EAGER_DEFER_PK, .pre_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY), .post_ms = (uint8_t)(QMK_DEFAULT_DEBOUNCE_DELAY) }
#define DEBOUNCE_RUNTIME_CFG_JOIN(type)          DEBOUNCE_RUNTIME_CFG_##type
#define DEBOUNCE_RUNTIME_CFG(type)               DEBOUNCE_RUNTIME_CFG_JOIN(type)  // V251115R3: 토큰 전달 시 매크로 확장 허용


// V251115R1: VIA 런타임 디바운스 엔진이 각 알고리즘을 동적으로 전환하도록 구현
typedef bool (*debounce_algo_init_t)(uint8_t num_rows);
typedef bool (*debounce_algo_run_t)(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
typedef void (*debounce_algo_free_t)(void);


bool debounce_sym_defer_pk_init(uint8_t num_rows);
bool debounce_sym_defer_pk_run(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
void debounce_sym_defer_pk_free(void);

bool debounce_sym_eager_pk_init(uint8_t num_rows);
bool debounce_sym_eager_pk_run(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
void debounce_sym_eager_pk_free(void);

bool debounce_asym_eager_defer_pk_init(uint8_t num_rows);
bool debounce_asym_eager_defer_pk_run(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
void debounce_asym_eager_defer_pk_free(void);


typedef struct
{
  debounce_runtime_type_t type;
  debounce_algo_init_t    init;
  debounce_algo_run_t     run;
  debounce_algo_free_t    free;
  uint8_t                 max_pre_ms;
  uint8_t                 max_post_ms;
} debounce_algo_entry_t;

static const debounce_algo_entry_t k_algorithms[] =
{
  {
    .type        = DEBOUNCE_RUNTIME_TYPE_SYM_DEFER_PK,
    .init        = debounce_sym_defer_pk_init,
    .run         = debounce_sym_defer_pk_run,
    .free        = debounce_sym_defer_pk_free,
    .max_pre_ms  = UINT8_MAX,
    .max_post_ms = UINT8_MAX,
  },
  {
    .type        = DEBOUNCE_RUNTIME_TYPE_SYM_EAGER_PK,
    .init        = debounce_sym_eager_pk_init,
    .run         = debounce_sym_eager_pk_run,
    .free        = debounce_sym_eager_pk_free,
    .max_pre_ms  = UINT8_MAX,
    .max_post_ms = UINT8_MAX,
  },
  {
    .type        = DEBOUNCE_RUNTIME_TYPE_ASYM_EAGER_DEFER_PK,
    .init        = debounce_asym_eager_defer_pk_init,
    .run         = debounce_asym_eager_defer_pk_run,
    .free        = debounce_asym_eager_defer_pk_free,
    .max_pre_ms  = 127,
    .max_post_ms = 127,
  },
};


// V260909R1: 메인 루프만 설정/실행을 소유한다. 고정 메모리라 재시도/입력 bypass가 필요 없다.
typedef struct {
  const debounce_algo_entry_t *algo;
  debounce_runtime_config_t config;
  uint8_t rows;
  bool config_ready;
  bool ready;
  bool reconcile;
} debounce_runtime_state_t;
static debounce_runtime_state_t g_runtime;
static const debounce_runtime_config_t k_default_config = DEBOUNCE_RUNTIME_CFG(QMK_DEFAULT_DEBOUNCE_TYPE);

static const debounce_algo_entry_t *debounce_runtime_find_algo(debounce_runtime_type_t type)
{
  for (size_t i = 0; i < sizeof(k_algorithms) / sizeof(k_algorithms[0]); i++)
    if (k_algorithms[i].type == type) return &k_algorithms[i];
  return NULL;
}

static uint8_t debounce_runtime_clamp_delay(uint8_t value, uint8_t max_value)
{
  if (value == 0U) return 1U;
  return value > max_value ? max_value : value;
}

bool debounce_runtime_apply_config(const debounce_runtime_config_t *config)
{
  if (config == NULL) return false;
  const debounce_algo_entry_t *next = debounce_runtime_find_algo(config->type);
  if (next == NULL) return false;  // V260909R1: 잘못된 새 설정은 기존 정상 엔진을 건드리지 않는다.
  debounce_runtime_config_t sanitized = *config;
  sanitized.pre_ms = debounce_runtime_clamp_delay(config->pre_ms, next->max_pre_ms);
  sanitized.post_ms = debounce_runtime_clamp_delay(config->post_ms, next->max_post_ms);
  if (g_runtime.config_ready && g_runtime.algo == next &&
      g_runtime.config.pre_ms == sanitized.pre_ms && g_runtime.config.post_ms == sanitized.post_ms)
    return true;  // 동일 설정 SAVE/SET은 진행 중인 debounce deadline을 재시작하지 않는다.

  // 각 엔진의 고정 저장소는 독립적이다. 새 엔진이 준비된 뒤 이전 엔진을 정리한다.
  if (g_runtime.rows != 0U && !next->init(g_runtime.rows)) return false;
  if (g_runtime.algo != NULL && g_runtime.algo != next) g_runtime.algo->free();
  g_runtime.algo = next;
  g_runtime.config = sanitized;
  g_runtime.config_ready = true;
  g_runtime.ready = g_runtime.rows != 0U;
  g_runtime.reconcile = true;
  return true;
}

const debounce_runtime_config_t *debounce_runtime_get_config(void)
{
  return g_runtime.config_ready ? &g_runtime.config : &k_default_config;
}

const debounce_runtime_config_t *debounce_runtime_get_default_config(void)
{
  return &k_default_config;
}

uint8_t debounce_runtime_press_delay(void) { return debounce_runtime_get_config()->pre_ms; }
uint8_t debounce_runtime_release_delay(void) { return debounce_runtime_get_config()->post_ms; }

void debounce_init(uint8_t num_rows)
{
  if (num_rows == 0U || num_rows > MATRIX_ROWS) return;
  if (!g_runtime.config_ready && !debounce_runtime_apply_config(&k_default_config)) return;
  if (!g_runtime.algo->init(num_rows)) return;
  g_runtime.rows = num_rows;
  g_runtime.ready = true;
  g_runtime.reconcile = true;
}

bool debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed)
{
  if (raw == NULL || cooked == NULL || num_rows == 0U || num_rows > MATRIX_ROWS) return false;
  if (!g_runtime.ready || num_rows != g_runtime.rows) debounce_init(num_rows);
  if (!g_runtime.ready) return false;
  // V260909R1: 새 raw edge가 없어도 이전 엔진의 pending press/release를 다시 추적한다.
  changed = changed || g_runtime.reconcile;
  g_runtime.reconcile = false;
  return g_runtime.algo->run(raw, cooked, num_rows, changed);
}

void debounce_free(void)
{
  if (g_runtime.algo != NULL) g_runtime.algo->free();
  g_runtime.rows = 0U;
  g_runtime.ready = false;
  g_runtime.reconcile = true;
}
