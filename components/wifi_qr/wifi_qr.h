#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::wifi_qr {

// The plugin: an ESPHome component and a tessera::Plugin. It registers the tile and the card in setup().
class WifiQr : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // What the card shows next: the tile that was tapped hands it over just before it opens the card, which copies it.
  // The name and password as Text, whole: an option of 64 characters may be more bytes than that.
  struct Network {
    tessera::Text ssid;
    tessera::Text password;
    char security[8];   // WPA, WEP or nopass, as a phone reads it in the code
    bool hidden;
    bool show_password;
    int close_after;    // seconds the card stays open, 0 until Back or a tap
  };
  Network next{};
};

}  // namespace esphome::wifi_qr
