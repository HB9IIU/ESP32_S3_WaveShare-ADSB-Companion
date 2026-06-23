#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "HB9IIUdisplayInit.h"
#include "lv_driver.h"

LV_FONT_DECLARE(JetBrainsMono_Bold_20);
LV_FONT_DECLARE(JetBrainsMono_Bold_28);

namespace FreshSetup {

namespace {

constexpr int kMaximumNetworks = 20;
constexpr uint32_t kBackground = 0x1A1C25;
constexpr uint32_t kHeader = 0x2A2C38;
constexpr uint32_t kField = 0x0E1018;
constexpr uint32_t kAccent = 0x00FF66;

enum class KeyboardMode { Lower, Upper, Symbols };

static bool initialized = false;
static bool submitted = false;
static bool passwordVisible = false;
static KeyboardMode keyboardMode = KeyboardMode::Lower;
static lv_obj_t* root = nullptr;
static lv_obj_t* passwordArea = nullptr;
static lv_obj_t* keyboard = nullptr;
static lv_obj_t* networkLabel = nullptr;
static lv_obj_t* networkPopup = nullptr;
static lv_obj_t* passwordEyeLabel = nullptr;
static lv_obj_t* homeLatitudeArea = nullptr;
static lv_obj_t* homeLongitudeArea = nullptr;
static lv_obj_t* activeCoordinateArea = nullptr;
static int networkCount = 0;
static int selectedNetwork = 0;
static String networkSsids[kMaximumNetworks];
static int32_t networkRssi[kMaximumNetworks] = {0};

static const char* keyboardLower[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL", "\n",
    "ABC", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "SYM", "a", "s", "d", "f", "g", "h", "j", "k", "l", "_", "\n",
    "SPACE", "z", "x", "c", "v", "b", "n", "m", ".", "OK", ""
};

static const char* keyboardUpper[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL", "\n",
    "abc", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "SYM", "A", "S", "D", "F", "G", "H", "J", "K", "L", "_", "\n",
    "SPACE", "Z", "X", "C", "V", "B", "N", "M", ".", "OK", ""
};

static const char* keyboardSymbols[] = {
    "abc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL", "\n",
    "ABC", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "\n",
    "SYM", "_", "-", "+", "=", "?", "/", "\\", ".", ",", ":", "\n",
    "SPACE", "[", "]", "{", "}", "<", ">", ";", "'", "OK", ""
};

static const char* coordinateKeyboardMap[] = {
    "7", "8", "9", "DEL", "\n",
    "4", "5", "6", "LAT", "\n",
    "1", "2", "3", "LON", "\n",
    "NEG", ".", "0", "CLR", ""
};

inline void service() {
    lv_timer_handler();
    delay(5);
}

inline void ensureInitialized() {
    if (initialized) return;
    lv_init();
    lvgl_setup();
    initialized = true;
}

inline void clearPage() {
    if (root) {
        lv_obj_del(root);
        root = nullptr;
    }
    passwordArea = nullptr;
    keyboard = nullptr;
    networkLabel = nullptr;
    networkPopup = nullptr;
    passwordEyeLabel = nullptr;
    homeLatitudeArea = nullptr;
    homeLongitudeArea = nullptr;
    activeCoordinateArea = nullptr;
    lv_obj_clean(lv_scr_act());
    lv_refr_now(nullptr);
}

inline lv_obj_t* createPage(const char* title) {
    clearPage();
    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 800, 480);
    lv_obj_set_style_bg_color(root, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 800, 60);
    lv_obj_set_style_bg_color(header, lv_color_hex(kHeader), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

    lv_obj_t* label = lv_label_create(header);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE4F7EA), 0);
    lv_obj_center(label);
    return root;
}

inline void styleButtonMatrix(lv_obj_t* matrix, const lv_font_t* font) {
    lv_obj_set_style_bg_color(
        matrix, lv_color_hex(kBackground), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(matrix, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(matrix, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(matrix, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(matrix, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(matrix, 0, LV_PART_MAIN);

    lv_obj_set_style_text_font(matrix, font, LV_PART_ITEMS);
    lv_obj_set_style_text_color(
        matrix, lv_color_hex(0xF4F7FF), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(
        matrix, lv_color_hex(0x6C738A), LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_color(
        matrix, lv_color_hex(0x363C4F), LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_dir(
        matrix, LV_GRAD_DIR_VER, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(matrix, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(
        matrix, lv_color_hex(0x9098B0), LV_PART_ITEMS);
    lv_obj_set_style_border_width(matrix, 2, LV_PART_ITEMS);
    lv_obj_set_style_radius(matrix, 8, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(
        matrix, lv_color_hex(0x4B5167),
        LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(
        matrix, 2, LV_PART_ITEMS | LV_STATE_PRESSED);
}

inline void applyKeyboardMode(KeyboardMode mode) {
    keyboardMode = mode;
    if (!keyboard) return;
    const char** map = keyboardLower;
    if (mode == KeyboardMode::Upper) map = keyboardUpper;
    if (mode == KeyboardMode::Symbols) map = keyboardSymbols;
    lv_btnmatrix_set_map(keyboard, map);
}

inline void updateNetworkLabel() {
    if (!networkLabel) return;
    if (networkCount <= 0) {
        lv_label_set_text(networkLabel, "No networks found");
        return;
    }
    char text[64];
    snprintf(text, sizeof(text), "%s  (%ld dBm)",
             networkSsids[selectedNetwork].c_str(),
             static_cast<long>(networkRssi[selectedNetwork]));
    lv_label_set_text(networkLabel, text);
}

inline void closeNetworkPopup() {
    if (!networkPopup) return;
    lv_obj_del(networkPopup);
    networkPopup = nullptr;
    lv_refr_now(nullptr);
}

inline void selectNetwork(lv_event_t* event) {
    const intptr_t index =
        reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (index < 0 || index >= networkCount) return;
    selectedNetwork = static_cast<int>(index);
    updateNetworkLabel();
    closeNetworkPopup();
}

inline void closeNetworkPopupEvent(lv_event_t* event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        lv_event_get_target(event) == networkPopup) {
        closeNetworkPopup();
    }
}

inline void openNetworkPopup(lv_event_t*) {
    if (networkPopup || networkCount <= 0) return;

    networkPopup = lv_obj_create(root);
    lv_obj_remove_style_all(networkPopup);
    lv_obj_set_size(networkPopup, 800, 480);
    lv_obj_set_pos(networkPopup, 0, 0);
    lv_obj_set_style_bg_color(
        networkPopup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(networkPopup, LV_OPA_30, 0);
    lv_obj_clear_flag(networkPopup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(networkPopup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        networkPopup, closeNetworkPopupEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_move_foreground(networkPopup);

    lv_obj_t* box = lv_obj_create(networkPopup);
    lv_obj_set_size(box, 620, 290);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1E2130), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(box);
    lv_label_set_text(title, "Select WiFi Network");
    lv_obj_set_style_text_font(title, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD8E2F0), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 0);

    lv_obj_t* list = lv_list_create(box);
    lv_obj_set_size(list, 596, 236);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x151826), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 10, 0);
    lv_obj_set_style_pad_row(list, 6, 0);

    for (int i = 0; i < networkCount; ++i) {
        String line = networkSsids[i] + "  (" +
                      String(networkRssi[i]) + " dBm)";
        lv_obj_t* button = lv_list_add_btn(list, nullptr, line.c_str());
        lv_obj_set_style_text_font(
            button, &JetBrainsMono_Bold_20, 0);
        lv_obj_set_style_text_color(
            button, lv_color_hex(0xF2F5FB), 0);
        lv_obj_set_style_bg_color(
            button,
            lv_color_hex(i == selectedNetwork ? 0x183A24 : 0x262B3A),
            0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_bg_color(
            button, lv_color_hex(0x183A24), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(
            button, lv_color_hex(kAccent), LV_STATE_PRESSED);
        if (i == selectedNetwork) {
            lv_obj_set_style_text_color(
                button, lv_color_hex(kAccent), 0);
        }
        lv_obj_add_event_cb(
            button, selectNetwork, LV_EVENT_CLICKED,
            reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }
    lv_refr_now(nullptr);
}

inline void togglePassword(lv_event_t*) {
    passwordVisible = !passwordVisible;
    lv_textarea_set_password_mode(passwordArea, !passwordVisible);
    if (passwordEyeLabel) {
        lv_label_set_text(
            passwordEyeLabel,
            passwordVisible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }
}

inline void keyboardEvent(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    const uint16_t id = lv_btnmatrix_get_selected_btn(keyboard);
    const char* text = lv_btnmatrix_get_btn_text(keyboard, id);
    if (!text) return;
    if (!strcmp(text, "ABC")) {
        applyKeyboardMode(KeyboardMode::Upper);
    } else if (!strcmp(text, "abc")) {
        applyKeyboardMode(KeyboardMode::Lower);
    } else if (!strcmp(text, "SYM")) {
        applyKeyboardMode(KeyboardMode::Symbols);
    } else if (!strcmp(text, "DEL")) {
        lv_textarea_del_char(passwordArea);
    } else if (!strcmp(text, "SPACE")) {
        lv_textarea_add_char(passwordArea, ' ');
    } else if (!strcmp(text, "OK")) {
        submitted = true;
    } else {
        lv_textarea_add_text(passwordArea, text);
    }
}

inline int scanNetworks() {
    tft.fillScreen(TFT_BLACK);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Scanning WiFi networks...", 400, 240);
    tft.setTextDatum(TL_DATUM);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int count = WiFi.scanNetworks();
    if (count < 0) count = 0;
    count = min(count, kMaximumNetworks);

    int indices[kMaximumNetworks];
    for (int i = 0; i < count; ++i) indices[i] = i;
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
                const int swap = indices[i];
                indices[i] = indices[j];
                indices[j] = swap;
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        networkSsids[i] = WiFi.SSID(indices[i]);
        networkRssi[i] = WiFi.RSSI(indices[i]);
    }
    WiFi.scanDelete();
    return count;
}

inline bool promptWiFi(char* ssid, size_t ssidSize,
                       char* password, size_t passwordSize) {
    ensureInitialized();
    networkCount = scanNetworks();
    selectedNetwork = 0;
    submitted = false;
    passwordVisible = false;
    keyboardMode = KeyboardMode::Lower;

    createPage("WiFi Setup");

    lv_obj_t* networkText = lv_label_create(root);
    lv_label_set_text(networkText, "Select Network");
    lv_obj_set_style_text_font(
        networkText, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(
        networkText, lv_color_hex(0x97A2B8), 0);
    lv_obj_set_pos(networkText, 16, 78);

    lv_obj_t* networkField = lv_obj_create(root);
    lv_obj_set_pos(networkField, 210, 68);
    lv_obj_set_size(networkField, 450, 48);
    lv_obj_set_style_bg_color(networkField, lv_color_hex(0x2A2E38), 0);
    lv_obj_set_style_border_color(networkField, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(networkField, 2, 0);
    lv_obj_set_style_radius(networkField, 10, 0);
    lv_obj_clear_flag(networkField, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(networkField, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        networkField, openNetworkPopup, LV_EVENT_CLICKED, nullptr);
    networkLabel = lv_label_create(networkField);
    lv_obj_set_style_text_font(
        networkLabel, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(
        networkLabel, lv_color_hex(0xF2F5FB), 0);
    lv_obj_center(networkLabel);
    updateNetworkLabel();

    lv_obj_t* dropdown = lv_btn_create(root);
    lv_obj_set_pos(dropdown, 674, 68);
    lv_obj_set_size(dropdown, 106, 48);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x252830), 0);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(dropdown, 2, 0);
    lv_obj_set_style_radius(dropdown, 10, 0);
    lv_obj_add_event_cb(
        dropdown, openNetworkPopup, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* dropdownLabel = lv_label_create(dropdown);
    lv_label_set_text(dropdownLabel, LV_SYMBOL_DOWN);
    lv_obj_center(dropdownLabel);

    lv_obj_t* passwordText = lv_label_create(root);
    lv_label_set_text(passwordText, "Password");
    lv_obj_set_style_text_font(
        passwordText, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(
        passwordText, lv_color_hex(0x97A2B8), 0);
    lv_obj_set_pos(passwordText, 16, 140);

    passwordArea = lv_textarea_create(root);
    lv_obj_set_pos(passwordArea, 210, 128);
    lv_obj_set_size(passwordArea, 500, 50);
    lv_obj_set_style_bg_color(passwordArea, lv_color_hex(kField), 0);
    lv_obj_set_style_border_color(passwordArea, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(passwordArea, 2, 0);
    lv_obj_set_style_text_font(
        passwordArea, &JetBrainsMono_Bold_20, 0);
    lv_textarea_set_one_line(passwordArea, true);
    lv_textarea_set_password_mode(passwordArea, true);
    lv_textarea_set_max_length(passwordArea, 63);

    lv_obj_t* show = lv_btn_create(root);
    lv_obj_set_pos(show, 720, 128);
    lv_obj_set_size(show, 60, 50);
    lv_obj_add_event_cb(show, togglePassword, LV_EVENT_CLICKED, nullptr);
    passwordEyeLabel = lv_label_create(show);
    lv_label_set_text(passwordEyeLabel, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(passwordEyeLabel);

    keyboard = lv_btnmatrix_create(root);
    lv_obj_set_pos(keyboard, 16, 190);
    lv_obj_set_size(keyboard, 768, 274);
    styleButtonMatrix(keyboard, &JetBrainsMono_Bold_20);
    applyKeyboardMode(KeyboardMode::Lower);
    lv_obj_add_event_cb(
        keyboard, keyboardEvent, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_refr_now(nullptr);

    while (!submitted) service();

    if (networkCount <= 0) return false;
    strncpy(ssid, networkSsids[selectedNetwork].c_str(), ssidSize - 1);
    ssid[ssidSize - 1] = '\0';
    strncpy(password, lv_textarea_get_text(passwordArea), passwordSize - 1);
    password[passwordSize - 1] = '\0';
    return true;
}

inline void setActiveCoordinate(lv_obj_t* area) {
    activeCoordinateArea = area;
    // Active field: thick bright-green border. Inactive: thin dark border.
    lv_obj_set_style_border_color(homeLatitudeArea,
        lv_color_hex(area == homeLatitudeArea ? kAccent : 0x1E2A40), 0);
    lv_obj_set_style_border_width(homeLatitudeArea,
        area == homeLatitudeArea ? 3 : 1, 0);
    lv_obj_set_style_border_color(homeLongitudeArea,
        lv_color_hex(area == homeLongitudeArea ? kAccent : 0x1E2A40), 0);
    lv_obj_set_style_border_width(homeLongitudeArea,
        area == homeLongitudeArea ? 3 : 1, 0);
}

inline void coordinateFocus(lv_event_t* event) {
    setActiveCoordinate(lv_event_get_target(event));
}

inline void coordinateKeyboardEvent(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
        !activeCoordinateArea) {
        return;
    }
    const uint16_t id = lv_btnmatrix_get_selected_btn(keyboard);
    const char* text = lv_btnmatrix_get_btn_text(keyboard, id);
    if (!text) return;
    if (!strcmp(text, "DEL")) {
        lv_textarea_del_char(activeCoordinateArea);
    } else if (!strcmp(text, "CLR")) {
        lv_textarea_set_text(activeCoordinateArea, "");
    } else if (!strcmp(text, "NEG")) {
        // Toggle leading minus sign
        const char* cur = lv_textarea_get_text(activeCoordinateArea);
        if (cur && cur[0] == '-') {
            char buf[17];
            strncpy(buf, cur + 1, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            lv_textarea_set_text(activeCoordinateArea, buf);
        } else {
            lv_textarea_set_cursor_pos(activeCoordinateArea, 0);
            lv_textarea_add_char(activeCoordinateArea, '-');
            lv_textarea_set_cursor_pos(
                activeCoordinateArea, LV_TEXTAREA_CURSOR_LAST);
        }
    } else if (!strcmp(text, "LAT")) {
        setActiveCoordinate(homeLatitudeArea);
    } else if (!strcmp(text, "LON")) {
        setActiveCoordinate(homeLongitudeArea);
    } else {
        lv_textarea_add_text(activeCoordinateArea, text);
    }
}

inline void saveCoordinates(lv_event_t*) {
    submitted = true;
}

inline lv_obj_t* createCoordinateField(
        int32_t y, const char* label, const char* hint, const char* value) {
    // Card: x=380, w=410 → right edge at x=790 (10 px margin from screen edge)
    lv_obj_t* card = lv_obj_create(root);
    lv_obj_set_pos(card, 380, y);
    lv_obj_set_size(card, 410, 120);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1C2134), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x252B3E), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, label);
    lv_obj_set_style_text_font(title, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xC8D4E8), 0);
    lv_obj_set_pos(title, 14, 8);

    lv_obj_t* hintLabel = lv_label_create(card);
    lv_label_set_text(hintLabel, hint);
    lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x5A6880), 0);
    lv_obj_set_pos(hintLabel, 180, 12);

    lv_obj_t* area = lv_textarea_create(card);
    lv_obj_set_pos(area, 10, 42);
    lv_obj_set_size(area, 386, 64);
    lv_obj_set_style_bg_color(area, lv_color_hex(kField), 0);
    lv_obj_set_style_border_color(area, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(area, 2, 0);
    lv_obj_set_style_text_font(area, &JetBrainsMono_Bold_28, 0);
    lv_obj_set_style_text_align(area, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_one_line(area, true);
    lv_textarea_set_accepted_chars(area, "+-.0123456789");
    lv_textarea_set_max_length(area, 16);
    lv_textarea_set_text(area, value);
    lv_obj_add_event_cb(area, coordinateFocus, LV_EVENT_CLICKED, nullptr);
    return area;
}

inline bool validCoordinate(const char* text, double minimum, double maximum,
                            double& value) {
    if (!text || !text[0]) return false;
    char* end = nullptr;
    value = strtod(text, &end);
    return end && *end == '\0' && value >= minimum && value <= maximum;
}

inline bool promptHome(double initialLatitude, double initialLongitude,
                       double& latitude, double& longitude) {
    ensureInitialized();
    submitted = false;
    createPage("Home Coordinates");

    char latitudeText[24];
    char longitudeText[24];
    snprintf(latitudeText,  sizeof(latitudeText),  "%.7f", initialLatitude);
    snprintf(longitudeText, sizeof(longitudeText), "%.7f", initialLongitude);

    // ── Right side: two coordinate cards, vertically centred ─────────────────
    // Screen 800×480, header 60 px.  Content area y=68..472 (404 px).
    // Two cards 120 px each + 20 px gap = 260 px → top offset = (404-260)/2 = 72
    // Lat card: y=140  Lon card: y=280  Cards right edge: 382+406=788 (12 px margin)
    homeLatitudeArea  = createCoordinateField(140, "Latitude",  "(+N / -S)", latitudeText);
    homeLongitudeArea = createCoordinateField(280, "Longitude", "(+E / -W)", longitudeText);

    // ── Left side: numpad panel ───────────────────────────────────────────────
    // Panel: x=12, y=68, w=330, h=404  → right edge 342, gap to cards 38 px
    // Keyboard: (10, 8) in panel, size 310×318  (4 rows × ~79 px each)
    // Save btn: (10, 336) in panel, size 310×56  → bottom 392, pad 12 px

    lv_obj_t* keypadPanel = lv_obj_create(root);
    lv_obj_set_pos(keypadPanel, 12, 68);
    lv_obj_set_size(keypadPanel, 330, 404);
    lv_obj_set_style_bg_color(keypadPanel, lv_color_hex(0x171C27), 0);
    lv_obj_set_style_bg_opa(keypadPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(keypadPanel, 16, 0);
    lv_obj_set_style_border_width(keypadPanel, 0, 0);
    lv_obj_set_style_pad_all(keypadPanel, 0, 0);
    lv_obj_clear_flag(keypadPanel, LV_OBJ_FLAG_SCROLLABLE);

    keyboard = lv_btnmatrix_create(keypadPanel);
    lv_obj_set_pos(keyboard, 10, 8);
    lv_obj_set_size(keyboard, 310, 318);
    lv_btnmatrix_set_map(keyboard, coordinateKeyboardMap);
    styleButtonMatrix(keyboard, &JetBrainsMono_Bold_28);
    lv_obj_add_event_cb(keyboard, coordinateKeyboardEvent,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* saveButton = lv_btn_create(keypadPanel);
    lv_obj_set_pos(saveButton, 10, 336);
    lv_obj_set_size(saveButton, 310, 56);
    lv_obj_set_style_bg_color(saveButton, lv_color_hex(0x1A6B3C), 0);
    lv_obj_set_style_bg_grad_color(saveButton, lv_color_hex(0x0F4526), 0);
    lv_obj_set_style_bg_grad_dir(saveButton, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(saveButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(saveButton, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(saveButton, 2, 0);
    lv_obj_set_style_radius(saveButton, 10, 0);
    lv_obj_set_style_bg_color(saveButton, lv_color_hex(0x0D5C2E), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(saveButton, 2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(saveButton, saveCoordinates, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* saveLabel = lv_label_create(saveButton);
    lv_label_set_text(saveLabel, "SAVE");
    lv_obj_set_style_text_font(saveLabel, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(saveLabel, lv_color_hex(kAccent), 0);
    lv_obj_center(saveLabel);

    setActiveCoordinate(homeLatitudeArea);
    lv_refr_now(nullptr);

    while (true) {
        while (!submitted) service();
        submitted = false;
        double enteredLatitude  = 0.0;
        double enteredLongitude = 0.0;
        if (validCoordinate(lv_textarea_get_text(homeLatitudeArea),
                            -90.0, 90.0, enteredLatitude) &&
            validCoordinate(lv_textarea_get_text(homeLongitudeArea),
                            -180.0, 180.0, enteredLongitude)) {
            latitude  = enteredLatitude;
            longitude = enteredLongitude;
            clearPage();
            tft.fillScreen(TFT_BLACK);
            return true;
        }

        lv_obj_t* error = lv_label_create(root);
        lv_label_set_text(error, "Invalid coordinates");
        lv_obj_set_style_text_font(error, &JetBrainsMono_Bold_20, 0);
        lv_obj_set_style_text_color(error, lv_color_hex(0xFF5555), 0);
        lv_obj_set_pos(error, 382, 420);
        lv_obj_del_delayed(error, 2000);
    }
}

inline bool promptAdsbServer(char* url, size_t urlSize, const char* currentUrl) {
    ensureInitialized();
    submitted = false;
    passwordVisible = false;
    keyboardMode = KeyboardMode::Lower;

    createPage("ADSB Server");

    lv_obj_t* urlText = lv_label_create(root);
    lv_label_set_text(urlText, "Server URL");
    lv_obj_set_style_text_font(urlText, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(urlText, lv_color_hex(0x97A2B8), 0);
    lv_obj_set_pos(urlText, 16, 88);

    // Fixed "http://" prefix — styled to look like the left part of the field.
    // User only types the host:port; the prefix is prepended on save.
    constexpr int32_t kPrefixX = 210, kPrefixW = 110, kFieldY = 68, kFieldH = 50;
    lv_obj_t* prefixBox = lv_obj_create(root);
    lv_obj_set_pos(prefixBox, kPrefixX, kFieldY);
    lv_obj_set_size(prefixBox, kPrefixW, kFieldH);
    lv_obj_set_style_bg_color(prefixBox, lv_color_hex(0x1A1E2C), 0);
    lv_obj_set_style_bg_opa(prefixBox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(prefixBox, 0, 0);
    lv_obj_set_style_radius(prefixBox, 0, 0);
    lv_obj_set_style_pad_all(prefixBox, 0, 0);
    lv_obj_clear_flag(prefixBox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* prefixLabel = lv_label_create(prefixBox);
    lv_label_set_text(prefixLabel, "http://");
    lv_obj_set_style_text_font(prefixLabel, &JetBrainsMono_Bold_20, 0);
    lv_obj_set_style_text_color(prefixLabel, lv_color_hex(0x00D4FF), 0);
    lv_obj_center(prefixLabel);

    // Strip the "http://" prefix before pre-filling — user edits host:port only.
    const char* hostPart = currentUrl ? currentUrl : "";
    if (strncmp(hostPart, "http://", 7) == 0) hostPart += 7;

    passwordArea = lv_textarea_create(root);
    lv_obj_set_pos(passwordArea, kPrefixX + kPrefixW, kFieldY);
    lv_obj_set_size(passwordArea, 570 - kPrefixW, kFieldH);
    lv_obj_set_style_bg_color(passwordArea, lv_color_hex(kField), 0);
    lv_obj_set_style_border_color(passwordArea, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(passwordArea, 2, 0);
    lv_obj_set_style_text_font(passwordArea, &JetBrainsMono_Bold_20, 0);
    lv_textarea_set_one_line(passwordArea, true);
    lv_textarea_set_password_mode(passwordArea, false);
    lv_textarea_set_max_length(passwordArea, static_cast<uint16_t>(urlSize - 8));
    lv_textarea_set_text(passwordArea, hostPart);

    keyboard = lv_btnmatrix_create(root);
    lv_obj_set_pos(keyboard, 16, 140);
    lv_obj_set_size(keyboard, 768, 328);
    styleButtonMatrix(keyboard, &JetBrainsMono_Bold_20);
    applyKeyboardMode(KeyboardMode::Lower);
    lv_obj_add_event_cb(keyboard, keyboardEvent, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_refr_now(nullptr);

    while (!submitted) service();

    const char* entered = lv_textarea_get_text(passwordArea);
    if (!entered || !entered[0]) {
        clearPage();
        tft.fillScreen(TFT_BLACK);
        return false;
    }
    snprintf(url, urlSize, "http://%s", entered);
    clearPage();
    tft.fillScreen(TFT_BLACK);
    return true;
}

}  // namespace

}  // namespace FreshSetup
