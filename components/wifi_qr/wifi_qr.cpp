#include "wifi_qr.h"
#include "wqr_qrcodegen.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

namespace esphome::wifi_qr {

using tessera::Font;
namespace ui = tessera::ui;

// Version 12 holds two values of 64 plain characters with every one escaped (281 bytes) at the medium level of error
// correction, far more than a real network (a name of 32 bytes, a password of 63); a usual one is a version 3 or 4,
// and the encoder picks the smallest that fits. A longer text shows "too_long" instead of a code.
static constexpr int MAX_VERSION = 12;
static constexpr int MIN_MODULE = 2;  // pixels a module, at least: fewer and a phone cannot read the code
static constexpr int QUIET = 4;  // the light border a reader needs around the code, in modules

// A value of the WIFI: text, with the characters the format reserves escaped.
static void escape_into(std::string &out, const tessera::Text &value) {
  for (const char ch : value) {
    if (strchr("\\;,:\"", ch)) out += '\\';
    out += ch;
  }
}

// What a phone's camera reads as "join this network": WIFI:T:WPA;S:<name>;P:<password>;H:true;;
static std::string wifi_text(const WifiQr::Network &n) {
  std::string t = "WIFI:T:";
  t += n.security;
  t += ";S:";
  escape_into(t, n.ssid);
  t += ';';
  if (strcmp(n.security, "nopass") != 0) {
    t += "P:";
    escape_into(t, n.password);
    t += ';';
  }
  if (n.hidden) t += "H:true;";
  t += ';';
  return t;
}

// The largest of `faces` the text fits in at `width`, else the last.
static Font fitting(const std::string &text, int width, std::initializer_list<Font> faces) {
  Font face = *faces.begin();
  for (Font f : faces) {
    face = f;
    if (ui::text_width(text, f) <= width) break;
  }
  return face;
}

// The text without the characters the screen's fonts cannot draw (an emoji in a network's name), which would show as
// a box or nothing; the spaces around one that is left out fold into one. The code itself keeps the name whole.
static std::string drawable(const std::string &text, Font face) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = text[i];
    size_t n = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
    n = std::min(n, text.size() - i);
    const std::string ch = text.substr(i, n);
    i += n;
    if (!ui::covers(ch, face)) continue;
    if (ch == " " && (out.empty() || out.back() == ' ')) continue;
    out += ch;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

static void place(lv_obj_t *label, int x, int y, int width, Font face) {
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, width, ui::line_height(face));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

// The tile: what it is, the network's name and what a tap does. It draws once; nothing on it changes.
class NetworkTile : public tessera::Tile {
 public:
  explicit NetworkTile(WifiQr *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    net_.ssid = c.options["ssid"] | "";
    net_.password = c.options["password"] | "";
    const char *security = c.options["security"] | "WPA";
    // A network without a password is an open one, whatever the choice says.
    if (net_.password.empty() || strcmp(security, "nopass") == 0) security = "nopass";
    else if (strcmp(security, "WEP") != 0) security = "WPA";
    snprintf(net_.security, sizeof net_.security, "%s", security);
    net_.hidden = c.options["hidden"] | false;
    net_.show_password = c.options["show_password"] | true;
    // A number option may come as 60 or 60.0; a float reads both.
    net_.close_after = std::max(0, static_cast<int>(c.options["close_after"] | 60.0f));

    const int w = c.width, h = c.height;
    // Each line is the person's own words when they wrote some, else Tessera's: the title, the network's name and
    // what a tap does. Without a network the tile asks for one and has nothing to tap for.
    const bool has_ssid = !net_.ssid.empty();
    auto own = [&](const char *option, const std::string &otherwise) {
      const char *words = c.options[option] | "";
      return words[0] ? std::string(words) : otherwise;
    };
    const std::string title = drawable(own("top_line", plugin_->text("title")), Font::BODY);
    const std::string name = has_ssid ? drawable(own("middle_line", roomy::plain(net_.ssid)), Font::TITLE)
                                      : std::string(plugin_->text("no_network"));
    const std::string hint = has_ssid ? drawable(own("bottom_line", plugin_->text("tap_to_show")), Font::BODY)
                                      : std::string();
    const Font name_face = fitting(name, w, {Font::TITLE, Font::BODY_LARGE, Font::BODY});

    title_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    name_ = ui::label(c.parent, name_face, theme::INK);
    hint_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    ui::set_text(title_, title);
    ui::set_text(name_, name);
    ui::set_text(hint_, hint);

    // Each line has its switch. Without a network the middle line asks for one whatever its switch says, and the
    // bottom one has no tap to tell of.
    bool show_title = c.options["show_top_line"] | true;
    const bool show_name = !has_ssid || ((c.options["show_middle_line"] | true) && !name.empty());
    bool show_hint = has_ssid && (c.options["show_bottom_line"] | true);

    // The lines that show, together in the middle of the card with a small gap between; on a card too low for them the
    // hint goes first, the title next.
    const int gap = ui::px(4);
    const int th = ui::line_height(Font::BODY), nh = ui::line_height(name_face), hh = ui::line_height(Font::BODY);
    auto total = [&]() {
      const int lines = show_title + show_name + show_hint;
      return (show_title ? th : 0) + (show_name ? nh : 0) + (show_hint ? hh : 0) + std::max(0, lines - 1) * gap;
    };
    if (total() > h) show_hint = false;
    if (total() > h) show_title = false;
    struct Line {
      lv_obj_t *label;
      bool show;
      Font face;
      int height;
    };
    int y = std::max(0, (h - total()) / 2);
    for (const Line &line : {Line{title_, show_title, Font::BODY, th}, Line{name_, show_name, name_face, nh},
                             Line{hint_, show_hint, Font::BODY, hh}}) {
      if (!line.show) {
        lv_obj_add_flag(line.label, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      place(line.label, 0, y, w, line.face);
      y += line.height + gap;
    }
  }

  void on_theme() override {
    ui::set_color(title_, theme::MUTED);
    ui::set_color(name_, theme::INK);
    ui::set_color(hint_, theme::MUTED);
  }

  // The card with the code, over the whole width of the glass, named after the network.
  void on_tap() override {
    if (net_.ssid.empty()) return;
    plugin_->next = net_;
    // The card's top bar writes its title in Tessera's own font; a name of only emoji leaves it to the tile's name.
    const std::string name = drawable(roomy::plain(net_.ssid), Font::TITLE);
    const std::string title = name.empty() ? std::string() : tessera::fill(plugin_->text("card_title"), "name", name);
    tessera::open_card(plugin_->plugin_id(), "code", "", tile_, title);
  }

 private:
  WifiQr *plugin_;
  int tile_ = -1;
  WifiQr::Network net_{};
  lv_obj_t *title_{}, *name_{}, *hint_{};
};

// The card: the code as large as the room allows, with what to do and the password beside or under it.
// A tap anywhere closes it, as Back does.
class CodeCard : public tessera::Card {
 public:
  explicit CodeCard(WifiQr *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    const WifiQr::Network &net = plugin_->next;
    seconds_left_ = net.close_after;
    {
      uint8_t temp[wqr_BUFFER_LEN_FOR_VERSION(MAX_VERSION)];  // the encoder's room, needed only while it works
      ok_ = wqr_encodeText(wifi_text(net).c_str(), temp, qr_, wqr_Ecc_MEDIUM, wqr_VERSION_MIN, MAX_VERSION,
                           wqr_Mask_AUTO, true);
    }

    const int w = c.width, h = c.height, gap = ui::px(12);
    std::string hint = plugin_->text(ok_ ? "scan" : "too_long"), pass;
    if (ok_ && net.show_password) {
      pass = strcmp(net.security, "nopass") == 0
                 ? std::string(plugin_->text("open_network"))
                 : tessera::fill(plugin_->text("password"), "password", roomy::plain(net.password));
    }

    // Wide glass: the code on the left half, the words in the middle of the right. Otherwise the words under the code.
    const bool beside = w > h * 4 / 3;
    const int text_w = beside ? w / 2 - gap : w;
    const Font hint_face = fitting(hint, text_w, {Font::BODY_LARGE, Font::BODY});
    // The largest font that has every character of the password; one the screen cannot write is left out, rather than
    // shown with letters missing that a guest would then type wrong. The code holds it whole either way.
    Font pass_face = Font::BODY;
    bool pass_fits = false;
    for (Font f : {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE, Font::BODY}) {
      if (!ui::covers(pass, f)) continue;
      pass_face = f;
      pass_fits = true;
      if (ui::text_width(pass, f) <= text_w) break;
    }
    if (!pass_fits) pass.clear();

    const int modules = ok_ ? wqr_getSize(qr_) + 2 * QUIET : 1;
    int hint_h = ui::line_height(hint_face), pass_h = pass.empty() ? 0 : gap / 2 + ui::line_height(pass_face);
    // Under the code on a low room: the password goes first, then the hint, so the code keeps whole modules a phone
    // can read.
    if (!beside && ok_ && std::min(w, h - hint_h - pass_h - gap) < modules * MIN_MODULE) {
      pass.clear();
      pass_h = 0;
      if (std::min(w, h - hint_h - gap) < modules * MIN_MODULE) hint_h = 0;
    }
    const int words_h = hint_h + pass_h;

    int box = beside ? std::min(h, w / 2) - gap : std::min(w, h - words_h - (words_h ? gap : 0));
    int code_x, code_y, words_x, words_y;
    if (beside) {
      code_x = (w / 2 - box) / 2;
      code_y = (h - box) / 2;
      words_x = w / 2;
      words_y = (h - words_h) / 2;
    } else {
      code_x = (w - box) / 2;
      code_y = std::max(0, (h - box - gap - words_h) / 2);
      words_x = 0;
      words_y = code_y + box + gap;
    }

    // Whole pixels for each module, or a phone sees blurred edges; the code then sits in the middle of its box.
    module_ = std::max(1, box / modules);
    const int side = module_ * modules;
    code_ = lv_obj_create(c.parent);
    lv_obj_remove_style_all(code_);
    lv_obj_remove_flag(code_, LV_OBJ_FLAG_CLICKABLE);  // a tap on the code is the card's, which closes
    lv_obj_remove_flag(code_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(code_, code_x + (box - side) / 2, code_y + (box - side) / 2);
    lv_obj_set_size(code_, side, side);
    if (ok_) lv_obj_add_event_cb(code_, paint_code, LV_EVENT_DRAW_MAIN, this);
    else lv_obj_add_flag(code_, LV_OBJ_FLAG_HIDDEN);

    hint_ = ui::label(c.parent, hint_face, theme::MUTED);
    ui::set_text(hint_, hint);
    if (!ok_) words_y = (h - hint_h) / 2;
    if (hint_h) place(hint_, words_x, words_y, text_w, hint_face);
    else lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    pass_ = ui::label(c.parent, pass_face, theme::INK);
    ui::set_text(pass_, pass);
    if (pass.empty()) lv_obj_add_flag(pass_, LV_OBJ_FLAG_HIDDEN);
    else place(pass_, words_x, words_y + hint_h + gap / 2, text_w, pass_face);
  }

  void on_theme() override {
    ui::set_color(hint_, theme::MUTED);
    ui::set_color(pass_, theme::INK);
    lv_obj_invalidate(code_);
  }

  void on_tap() override { tessera::close_card(); }

  // Once a second while open: the card closes by itself when the tile's time is up (0: it waits for Back or a tap).
  void on_tick(uint32_t epoch) override {
    if (seconds_left_ > 0 && --seconds_left_ == 0) tessera::close_card();
  }

 private:
  static void paint_code(lv_event_t *e) {
    static_cast<CodeCard *>(lv_event_get_user_data(e))->paint(lv_event_get_layer(e));
  }

  // Dark modules on a light plate in both looks: many phones do not read a code the other way round. The darker of
  // the ink and the card's colour is the module, the lighter the plate.
  void paint(lv_layer_t *layer) {
    lv_area_t area;
    lv_obj_get_coords(code_, &area);
    const lv_color_t ink = ui::color(theme::INK), card = ui::color(theme::CARD);
    const bool ink_dark = lv_color_luminance(ink) < lv_color_luminance(card);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_COVER;
    d.bg_color = ink_dark ? card : ink;
    d.radius = module_;
    lv_draw_rect(layer, &d, &area);
    // Each run of dark modules in a row is one rectangle: a few hundred for a usual code. The glass is drawn in strips,
    // so only the rows of the strip being drawn.
    d.bg_color = ink_dark ? ink : card;
    d.radius = 0;
    const int size = wqr_getSize(qr_);
    const lv_area_t clip = layer->_clip_area;
    const int top = area.y1 + QUIET * module_;
    const int first = std::max(0, (static_cast<int>(clip.y1) - top) / module_);
    const int last = std::min(size - 1, (static_cast<int>(clip.y2) - top) / module_);
    for (int y = first; y <= last; y++) {
      for (int x = 0; x < size;) {
        if (!wqr_getModule(qr_, x, y)) {
          x++;
          continue;
        }
        const int start = x;
        while (x < size && wqr_getModule(qr_, x, y)) x++;
        lv_area_t run;
        run.x1 = area.x1 + (QUIET + start) * module_;
        run.y1 = area.y1 + (QUIET + y) * module_;
        run.x2 = area.x1 + (QUIET + x) * module_ - 1;
        run.y2 = run.y1 + module_ - 1;
        lv_draw_rect(layer, &d, &run);
      }
    }
  }

  WifiQr *plugin_;
  bool ok_ = false;
  int module_ = 1;
  int seconds_left_ = 0;
  uint8_t qr_[wqr_BUFFER_LEN_FOR_VERSION(MAX_VERSION)];
  lv_obj_t *code_{}, *hint_{}, *pass_{};
};

void WifiQr::setup() {
  add_tile("network", [this]() { return new NetworkTile(this); });
  add_card("code", [this]() { return new CodeCard(this); }, true);
}

}  // namespace esphome::wifi_qr
