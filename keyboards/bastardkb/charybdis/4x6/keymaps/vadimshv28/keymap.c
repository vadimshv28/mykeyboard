/**
 * Copyright 2021 Charly Delay <charly@codesink.dev> (@0xcharly)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H
#include <stdio.h>
#include <math.h>
#include <print.h>
#ifdef UNICODE_ENABLE
#include "process_unicode.h"

// Хелпер для отправки Unicode (Windows Alt+код)
static void send_unicode_code(uint32_t code) {
    register_code(KC_RALT);
    
    // Отправляем цифры Unicode кода
    if (code >= 1000) {
        register_code(KC_P1 + (code / 1000) - 1);
        unregister_code(KC_P1 + (code / 1000) - 1);
        code %= 1000;
    }
    if (code >= 100) {
        register_code(KC_P1 + (code / 100) - 1);
        unregister_code(KC_P1 + (code / 100) - 1);
        code %= 100;
    }
    if (code >= 10) {
        register_code(KC_P1 + (code / 10) - 1);
        unregister_code(KC_P1 + (code / 10) - 1);
        code %= 10;
    }
    if (code > 0) {
        register_code(KC_P1 + code - 1);
        unregister_code(KC_P1 + code - 1);
    }
    
    unregister_code(KC_RALT);
}
#endif

// Не определяем RGB коды напрямую, используем HSV

#ifdef CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_ENABLE
#    include "timer.h"
#endif // CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_ENABLE

enum charybdis_keymap_layers {
    LAYER_BASE_RU = 0,
    LAYER_BASE_RU_SHIFT,
    LAYER_BASE_EN,
    LAYER_BASE_EN_SHIFT,
    LAYER_SCROLL,
    LAYER_CARET,
};

enum custom_keycodes {
    RU_LSFT = SAFE_RANGE,
    RU_RSFT,
    EN_RSFT,
    EN_LSFT,
    ALT_GUI,
    SCROLL_CARET,
};

typedef enum {
    POINTER_MODE_MOUSE = 0,
    POINTER_MODE_SCROLL,
    POINTER_MODE_CARET,
} pointer_mode_t;

/** \brief Automatically enable sniping-mode on the pointer layer. */
#define CHARYBDIS_AUTO_SNIPING_ON_LAYER LAYER_CARET

#ifdef CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_ENABLE
static uint16_t auto_pointer_layer_timer = 0;

#    ifndef CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_TIMEOUT_MS
#        define CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_TIMEOUT_MS 1000
#    endif // CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_TIMEOUT_MS

#    ifndef CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_THRESHOLD
#        define CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_THRESHOLD 8
#    endif // CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_THRESHOLD
#endif     // CHARYBDIS_AUTO_POINTER_LAYER_TRIGGER_ENABLE

static pointer_mode_t pointer_mode = POINTER_MODE_MOUSE;
static int16_t caret_mode_x        = 0;
static int16_t caret_mode_y        = 0;
static uint16_t ru_rsft_timer     = 0;
static uint16_t alt_last_release  = 0;
static bool     alt_gui_mode      = false;
static uint16_t scroll_caret_last_release   = 0;
static bool     scroll_caret_symbol_mode    = false;

// Адаптивный DPI трекбола
#define DPI_MIN 150
#define DPI_MAX 450
#define DPI_SCROLL 2
#define DPI_CARET 200
#define SPEED_THRESHOLD_SLOW 1
#define SPEED_THRESHOLD_FAST 2

static uint16_t current_dpi = DPI_MIN;

#define SYMBOL_KEY_BASE  QK_USER
#define SS(str_literal) (SYMBOL_KEY_BASE | ((uint16_t)((str_literal)[0])))

#define CARET_MODE_STEP 20

static void set_pointer_mode(pointer_mode_t mode) {
    pointer_mode = mode;
    if (mode != POINTER_MODE_CARET) {
        caret_mode_x = 0;
        caret_mode_y = 0;
    }

    // Устанавливаем соответствующий DPI для каждого режима
#ifdef POINTING_DEVICE_ENABLE
    switch (mode) {
        case POINTER_MODE_MOUSE:
            // В режиме мыши DPI будет управляться адаптивно
            current_dpi = DPI_MIN;
            pointing_device_set_cpi(current_dpi);
            break;
        case POINTER_MODE_SCROLL:
            current_dpi = DPI_SCROLL;
            pointing_device_set_cpi(current_dpi);
            break;
        case POINTER_MODE_CARET:
            current_dpi = DPI_CARET;
            pointing_device_set_cpi(current_dpi);
            break;
    }
#endif
}

static void update_dpi_adaptive(int16_t x, int16_t y) {
#ifdef POINTING_DEVICE_ENABLE
    // Адаптивный DPI ТОЛЬКО в режиме мыши, не в скролле или каретке
    if (pointer_mode != POINTER_MODE_MOUSE) {
        return;
    }

    // Вычисляем скорость движения (максимум из x и y)
    uint16_t speed = MAX(abs(x), abs(y));
    uint16_t target_dpi;

    if (speed <= SPEED_THRESHOLD_SLOW) {
        target_dpi = DPI_MIN;
    } else if (speed >= SPEED_THRESHOLD_FAST) {
        target_dpi = DPI_MAX;
    } else {
        // Плавная интерполяция между MIN и MAX
        target_dpi = DPI_MIN + ((DPI_MAX - DPI_MIN) * (speed - SPEED_THRESHOLD_SLOW)) / (SPEED_THRESHOLD_FAST - SPEED_THRESHOLD_SLOW);
    }

    // Обновляем DPI только если он изменился
    if (current_dpi != target_dpi) {
        current_dpi = target_dpi;
        pointing_device_set_cpi(current_dpi);
    }
#endif
}

#define LOWER MO(LAYER_SCROLL)
#define RAISE MO(LAYER_CARET)
#define PT_Z LT(LAYER_CARET, KC_Z)
#define PT_SLSH LT(LAYER_CARET, KC_SLSH)

#ifndef POINTING_DEVICE_ENABLE
#    define DRGSCRL KC_NO
#    define DPI_MOD KC_NO
#    define S_D_MOD KC_NO
#    define SNIPING KC_NO
#endif // !POINTING_DEVICE_ENABLE

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  [LAYER_BASE_RU] = LAYOUT(
  // ╭──────────────────────────────────────────────────────╮ ╭──────────────────────────────────────────────────────╮
        KC_ESC,    KC_1,    KC_2,    KC_3,    KC_4,    KC_5,       KC_6,    KC_7,    KC_8,    KC_9,    KC_0, KC_BTN3,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
        KC_TAB,    KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,       KC_Y,    KC_U,    KC_I,    KC_O,    KC_P, KC_LBRC,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       RU_LSFT,    KC_A,    KC_S,    KC_D,    KC_F,    KC_G,       KC_H,    KC_J,    KC_K,    KC_L, KC_SCLN, RU_RSFT,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       KC_LCTL,    KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,       KC_N,    KC_M, KC_COMM,  KC_DOT, KC_RBRC, KC_TILD,
  // ╰──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────╯
                                 KC_BTN1, KC_SPC,   KC_BTN2,     KC_DEL,  KC_ENT,
                                      SCROLL_CARET, ALT_GUI,    KC_BSPC
  //                            ╰───────────────────────────╯ ╰──────────────────╯
  ),

  [LAYER_BASE_RU_SHIFT] = LAYOUT(
  // ╭──────────────────────────────────────────────────────╮ ╭──────────────────────────────────────────────────────╮
        KC_ESC,  XXXXXXX,  XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,  XXXXXXX,  XXXXXXX,  XXXXXXX,  XXXXXXX,  XXXXXXX, KC_BTN3,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
        KC_TAB,    KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,       KC_Y,    KC_U,    KC_I,    KC_O,    KC_P, KC_LBRC,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       _______,     KC_A,    KC_S,    KC_D,    KC_F,    KC_G,       KC_H,    KC_J,    KC_K,    KC_L, KC_SCLN, _______,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
        KC_LCTL,    KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,       KC_N,    KC_M, KC_COMM,  KC_DOT, KC_RBRC, KC_TILD,
  // ╰──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────╯
                                 KC_BTN1, KC_SPC,   KC_BTN2,     KC_DEL,  KC_ENT,
                                           XXXXXXX, ALT_GUI,    KC_BSPC
  //                            ╰───────────────────────────╯ ╰──────────────────╯
  ),

  [LAYER_BASE_EN] = LAYOUT(
  // ╭──────────────────────────────────────────────────────╮ ╭──────────────────────────────────────────────────────╮
        KC_ESC,    KC_1,    KC_2,    KC_3,    KC_4,    KC_5,       KC_6,    KC_7,    KC_8,    KC_9,    KC_0, KC_BTN3,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
        KC_TAB,    KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,       KC_Y,    KC_U,    KC_I,    KC_O,    KC_P, XXXXXXX,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       EN_LSFT,    KC_A,    KC_S,    KC_D,    KC_F,    KC_G,       KC_H,    KC_J,    KC_K,    KC_L, XXXXXXX, EN_RSFT,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       KC_LCTL,    KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,       KC_N,    KC_M, XXXXXXX,  XXXXXXX, XXXXXXX, XXXXXXX,
  // ╰──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────╯
                                 KC_BTN1, KC_SPC,   KC_BTN2,     KC_BTN2,  KC_ENT,
                                      SCROLL_CARET, ALT_GUI,    KC_BSPC
  //                            ╰───────────────────────────╯ ╰──────────────────╯
  ),

  [LAYER_BASE_EN_SHIFT] = LAYOUT(
  // ╭──────────────────────────────────────────────────────╮ ╭──────────────────────────────────────────────────────╮
        KC_ESC,  XXXXXXX,  XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,  XXXXXXX,  XXXXXXX,  XXXXXXX,  XXXXXXX,  XXXXXXX, KC_BTN3,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
        KC_TAB,    KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,       KC_Y,    KC_U,    KC_I,    KC_O,    KC_P, XXXXXXX,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       _______,    KC_A,    KC_S,    KC_D,    KC_F,    KC_G,       KC_H,    KC_J,    KC_K,    KC_L, XXXXXXX, _______,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
        KC_LCTL,    KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,       KC_N,    KC_M, XXXXXXX,  XXXXXXX, XXXXXXX, XXXXXXX,
  // ╰──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────╯
                                 KC_BTN1, KC_SPC,   KC_BTN2,     KC_BTN2,  KC_ENT,
                                           XXXXXXX, ALT_GUI,    KC_BSPC
  //                            ╰───────────────────────────╯ ╰──────────────────╯
  ),

  [LAYER_SCROLL] = LAYOUT(
  // ╭──────────────────────────────────────────────────────╮ ╭──────────────────────────────────────────────────────╮
       KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,      KC_F7,   KC_F8,   KC_F9,  KC_F10,  KC_F11,   KC_F12,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       KC_TAB, KC_HOME, KC_PGDN, KC_PGUP, KC_END, XXXXXXX,     XXXXXXX,   KC_P7,   KC_P8,   KC_P9, XXXXXXX, XXXXXXX,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       KC_LSFT, KC_LEFT, KC_DOWN, KC_UP, KC_RGHT, XXXXXXX,    XXXXXXX,   KC_P4,   KC_P5,   KC_P6, XXXXXXX, XXXXXXX,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       KC_LCTL, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,    XXXXXXX,   KC_P1,   KC_P2,   KC_P3, XXXXXXX, XXXXXXX,
  // ╰──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────╯
                                  XXXXXXX, XXXXXXX, QK_BOOT,    KC_CAPS, KC_P0,
                                            _______, EE_CLR,    XXXXXXX
  //                            ╰───────────────────────────╯ ╰──────────────────╯
  ),

  [LAYER_CARET] = LAYOUT(
  // ╭──────────────────────────────────────────────────────╮ ╭──────────────────────────────────────────────────────╮
       XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,     XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       KC_TAB,  SS("@"), SS(";"), SS("/"), SS("="), SS("*"), SS("("), SS(")"), SS("<"), SS(">"), SS("#"), SS("_"),
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       KC_LSFT, SS("№"), SS(":"), SS("|"), SS("%"), SS("+"), SS("["), SS("]"), SS(","), SS("."), SS("\""), SS("'"),
  // ├──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────┤
       KC_LCTL, SS("$"), SS("&"), SS("\\"), SS("^"), SS("-"), SS("{"), SS("}"), SS("?"), SS("!"), SS("`"), SS("~"),
  // ╰──────────────────────────────────────────────────────┤ ├──────────────────────────────────────────────────────╯
                                   XXXXXXX, XXXXXXX, XXXXXXX,    XXXXXXX, XXXXXXX,
                                            _______, XXXXXXX,     XXXXXXX
  //                            ╰───────────────────────────╯ ╰──────────────────╯
  ),
};
// clang-format on

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case RU_LSFT:
            if (record->event.pressed) {
                layer_on(LAYER_BASE_RU_SHIFT);
                register_code(KC_LSFT);
            } else {
                layer_off(LAYER_BASE_RU_SHIFT);
                unregister_code(KC_LSFT);
            }
            return false;
        case RU_RSFT:
            if (record->event.pressed) {
                ru_rsft_timer = timer_read();
                layer_on(LAYER_BASE_RU_SHIFT);
                register_code(KC_RSFT);
            } else {
                unregister_code(KC_RSFT);
                layer_off(LAYER_BASE_RU_SHIFT);
                if (timer_elapsed(ru_rsft_timer) < TAPPING_TERM) {
                    tap_code(KC_QUOT);
                }
            }
            return false;
        case EN_LSFT:
            if (record->event.pressed) {
                layer_on(LAYER_BASE_EN_SHIFT);
                register_code(KC_LSFT);
            } else {
                layer_off(LAYER_BASE_EN_SHIFT);
                unregister_code(KC_LSFT);
            }
            return false;
        case EN_RSFT:
            if (record->event.pressed) {
                layer_on(LAYER_BASE_EN_SHIFT);
                register_code(KC_RSFT);
            } else {
                unregister_code(KC_RSFT);
                layer_off(LAYER_BASE_EN_SHIFT);
            }
            return false;
        case ALT_GUI:
            if (record->event.pressed) {
                if (timer_elapsed(alt_last_release) < TAPPING_TERM) {
                    alt_gui_mode = true;
                    register_code(KC_LGUI);
                } else {
                    alt_gui_mode = false;
                    register_code(KC_LALT);
                }
            } else {
                if (alt_gui_mode) {
                    unregister_code(KC_LGUI);
                } else {
                    unregister_code(KC_LALT);
                }
                alt_last_release = timer_read();
            }
            return false;
        case SCROLL_CARET:
            if (record->event.pressed) {
                if (timer_elapsed(scroll_caret_last_release) < TAPPING_TERM) {
                    // Двойное нажатие - переходим на LAYER_CARET
                    scroll_caret_symbol_mode = true;
                    layer_on(LAYER_CARET);
                    set_pointer_mode(POINTER_MODE_CARET);
                } else {
                    // Одиночное удержание - переходим на LAYER_SCROLL
                    scroll_caret_symbol_mode = false;
                    layer_on(LAYER_SCROLL);
                    set_pointer_mode(POINTER_MODE_SCROLL);
                }
            } else {
                if (scroll_caret_symbol_mode) {
                    layer_off(LAYER_CARET);
                } else {
                    layer_off(LAYER_SCROLL);
                }
                set_pointer_mode(POINTER_MODE_MOUSE);
                scroll_caret_last_release = timer_read();
            }
            return false;
    }

    // Обработка символьных кейкодов
    if (keycode >= SYMBOL_KEY_BASE && keycode <= SYMBOL_KEY_BASE + 255) {
        if (record->event.pressed) {
            uint8_t symbol = keycode - SYMBOL_KEY_BASE;

            // ВСЕ символы через send_unicode_code() - ЕДИНСТВЕННЫЙ способ независимости от раскладки ОС!
            switch (symbol) {
                // Строка 1: @ ; / = * ( ) < > # _
                case '@': send_unicode_code(0x0040); break;  // U+0040 COMMERCIAL AT
                case ';': send_unicode_code(0x003B); break;  // U+003B SEMICOLON
                case '/': send_unicode_code(0x002F); break;  // U+002F SOLIDUS
                case '=': send_unicode_code(0x003D); break;  // U+003D EQUALS SIGN
                case '*': send_unicode_code(0x002A); break;  // U+002A ASTERISK
                case '(': send_unicode_code(0x0028); break;  // U+0028 LEFT PARENTHESIS
                case ')': send_unicode_code(0x0029); break;  // U+0029 RIGHT PARENTHESIS
                case '<': send_unicode_code(0x003C); break;  // U+003C LESS-THAN SIGN
                case '>': send_unicode_code(0x003E); break;  // U+003E GREATER-THAN SIGN
                case '#': send_unicode_code(0x0023); break;  // U+0023 NUMBER SIGN
                case '_': send_unicode_code(0x005F); break;  // U+005F LOW LINE

                // Строка 2: № : | % + [ ] , . " '
                case 0xE2: send_unicode_code(0x2116); break; // № U+2116 NUMERO SIGN
                case ':': send_unicode_code(0x003A); break;  // U+003A COLON
                case '|': send_unicode_code(0x007C); break;  // U+007C VERTICAL LINE
                case '%': send_unicode_code(0x0025); break;  // U+0025 PERCENT SIGN
                case '+': send_unicode_code(0x002B); break;  // U+002B PLUS SIGN
                case '[': send_unicode_code(0x005B); break;  // U+005B LEFT SQUARE BRACKET
                case ']': send_unicode_code(0x005D); break;  // U+005D RIGHT SQUARE BRACKET
                case ',': send_unicode_code(0x002C); break;  // U+002C COMMA
                case '.': send_unicode_code(0x002E); break;  // U+002E FULL STOP
                case '\"': send_unicode_code(0x0022); break; // U+0022 QUOTATION MARK
                case '\'': send_unicode_code(0x0027); break; // U+0027 APOSTROPHE

                // Строка 3: $ & \ ^ - { } ? ! ` ~
                case '$': send_unicode_code(0x0024); break;  // U+0024 DOLLAR SIGN
                case '&': send_unicode_code(0x0026); break;  // U+0026 AMPERSAND
                case '\\': send_unicode_code(0x005C); break; // U+005C REVERSE SOLIDUS
                case '^': send_unicode_code(0x005E); break;  // U+005E CIRCUMFLEX ACCENT
                case '-': send_unicode_code(0x002D); break;  // U+002D HYPHEN-MINUS
                case '{': send_unicode_code(0x007B); break;  // U+007B LEFT CURLY BRACKET
                case '}': send_unicode_code(0x007D); break;  // U+007D RIGHT CURLY BRACKET
                case '?': send_unicode_code(0x003F); break;  // U+003F QUESTION MARK
                case '!': send_unicode_code(0x0021); break;  // U+0021 EXCLAMATION MARK
                case '`': send_unicode_code(0x0060); break;  // U+0060 GRAVE ACCENT
                case '~': send_unicode_code(0x007E); break;  // U+007E TILDE
            }
        }
        return false;
    }

    return true;
}

#ifdef POINTING_DEVICE_ENABLE
report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    switch (pointer_mode) {
        case POINTER_MODE_MOUSE:
            // Адаптивный DPI в базовых слоях
            update_dpi_adaptive(mouse_report.x, mouse_report.y);
            return mouse_report;

        case POINTER_MODE_SCROLL:
            // Режим прокрутки - движения превращаем в прокрутку
            // Замедление происходит через DPI_SCROLL=2
            mouse_report.h = mouse_report.x;
            mouse_report.v = -mouse_report.y;
            mouse_report.x = 0;
            mouse_report.y = 0;
            return mouse_report;

        case POINTER_MODE_CARET:
            // Режим каретки - движения превращаем в стрелки
            caret_mode_x += mouse_report.x;
            caret_mode_y += mouse_report.y;

            while (caret_mode_x >= CARET_MODE_STEP) {
                tap_code(KC_RGHT);
                caret_mode_x -= CARET_MODE_STEP;
            }
            while (caret_mode_x <= -CARET_MODE_STEP) {
                tap_code(KC_LEFT);
                caret_mode_x += CARET_MODE_STEP;
            }

            while (caret_mode_y >= CARET_MODE_STEP) {
                tap_code(KC_DOWN);
                caret_mode_y -= CARET_MODE_STEP;
            }
            while (caret_mode_y <= -CARET_MODE_STEP) {
                tap_code(KC_UP);
                caret_mode_y += CARET_MODE_STEP;
            }

            // Убираем движение мыши
            mouse_report.x = 0;
            mouse_report.y = 0;
            mouse_report.h = 0;
            mouse_report.v = 0;
            return mouse_report;
    }

    return mouse_report;
}
#endif // POINTING_DEVICE_ENABLE

#ifdef RGB_MATRIX_ENABLE
// Forward-declare this helper function since it is defined in rgb_matrix.c.
void rgb_matrix_update_pwm_buffers(void);
#endif

bool shutdown_user(bool jump_to_bootloader) {
#ifdef RGBLIGHT_ENABLE
    rgblight_enable_noeeprom();
    rgblight_mode_noeeprom(1);
    rgblight_setrgb(RGB_RED);
#endif // RGBLIGHT_ENABLE
#ifdef RGB_MATRIX_ENABLE
    rgb_matrix_set_color_all(RGB_RED);
    rgb_matrix_update_pwm_buffers();
#endif // RGB_MATRIX_ENABLE
    return false;
}

#ifdef COMBO_ENABLE
enum combo_events {
    COMBO_BASE_RU,
    COMBO_BASE_EN,
    COMBO_LENGTH,
};

const uint16_t PROGMEM combo_base_ru[] = {KC_V, KC_B, COMBO_END};
const uint16_t PROGMEM combo_base_en[] = {KC_F, KC_G, COMBO_END};

combo_t key_combos[COMBO_LENGTH] = {
    [COMBO_BASE_RU] = COMBO_ACTION(combo_base_ru),
    [COMBO_BASE_EN] = COMBO_ACTION(combo_base_en),
};

void process_combo_event(uint16_t combo_index, bool pressed) {
    if (!pressed) {
        return;
    }

    switch (combo_index) {
        case COMBO_BASE_RU:
            layer_move(LAYER_BASE_RU);
            // Отправляем Ctrl+2 при переходе на русский слой
            register_code(KC_LCTL);
            tap_code(KC_2);
            unregister_code(KC_LCTL);
            break;
        case COMBO_BASE_EN:
            layer_move(LAYER_BASE_EN);
            // Отправляем Ctrl+1 при переходе на английский слой
            register_code(KC_LCTL);
            tap_code(KC_1);
            unregister_code(KC_LCTL);
            break;
    }
}
#endif

#ifdef RGB_MATRIX_ENABLE
bool rgb_matrix_indicators_user(void) {
    // Проверяем сначала состояние CAPS LOCK
    if (host_keyboard_led_state().caps_lock) {
        // Если CAPS LOCK включен - жёлтый во всех слоях (30% яркость)
        rgb_matrix_sethsv_noeeprom(43, 255, 76);
        return false;
    }

    // Определяем цвета для разных слоёв
    switch (get_highest_layer(layer_state)) {
        case LAYER_BASE_RU:
            // Русский слой - белый (30% яркость)
            rgb_matrix_sethsv_noeeprom(0, 0, 76);
            break;
        case LAYER_BASE_RU_SHIFT:
            // Русский Shift - белый (40% яркость)
            rgb_matrix_sethsv_noeeprom(0, 0, 102);
            break;
        case LAYER_BASE_EN:
            // Английский слой - фиолетовый (30% яркость)
            rgb_matrix_sethsv_noeeprom(191, 255, 76);
            break;
        case LAYER_BASE_EN_SHIFT:
            // Английский Shift - фиолетовый (40% яркость)
            rgb_matrix_sethsv_noeeprom(191, 255, 102);
            break;
        case LAYER_SCROLL:
            // Слой прокрутки - зелёный (30% яркость)
            rgb_matrix_sethsv_noeeprom(85, 255, 76);
            break;
        case LAYER_CARET:
            // Слой каретки - синий (30% яркость)
            rgb_matrix_sethsv_noeeprom(170, 255, 76);
            break;
        default:
            // По умолчанию - белый (30% яркость)
            rgb_matrix_sethsv_noeeprom(0, 0, 76);
            break;
    }

    return false;
}
#endif // RGB_MATRIX_ENABLE

void keyboard_post_init_user(void) {
    // Инициализируем все переменные
    ru_rsft_timer = 0;
    alt_last_release = 0;
    alt_gui_mode = false;
    scroll_caret_last_release = 0;
    scroll_caret_symbol_mode = false;
    caret_mode_x = 0;
    caret_mode_y = 0;
    pointer_mode = POINTER_MODE_MOUSE;

    // Устанавливаем начальный DPI
#ifdef POINTING_DEVICE_ENABLE
    pointing_device_set_cpi(DPI_MIN);
    current_dpi = DPI_MIN;
#endif

    // Unicode режим настроен в config.h через UNICODE_SELECTED_INPUT_MODE

    // Убеждаемся что мы начинаем с базового слоя
    layer_move(LAYER_BASE_RU);

#ifdef RGB_MATRIX_ENABLE
    rgb_matrix_enable();
    // Устанавливаем режим статического цвета
    rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
    // Устанавливаем начальный цвет (белый для базового русского слоя с яркостью 30%)
    rgb_matrix_sethsv_noeeprom(0, 0, 76);
#endif // RGB_MATRIX_ENABLE
}
