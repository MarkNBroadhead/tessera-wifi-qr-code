# Wi-Fi QR Code

A tile with your Wi-Fi network's name. Tap it and the screen shows a large QR code over its whole width: a guest
points their phone's camera at it and joins the network, without typing the password.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **Wi-Fi QR Code** tile from the library's Plugins group.
3. In the inspector, fill in the network name and the password, and choose the security (WPA for almost every home
   network). Leave the password empty for an open network.
4. Tap the tile on the screen. Tap the code, or Back, to close it.

## Good to know

- The code is the standard `WIFI:` code that the cameras of iPhones and Android phones read.
- The code is always dark on a light square, also when the screen is dark, because many phones cannot read it the
  other way round.
- The password is kept in the screen's layout as a tile option, as every option is, and is in the code anyway: use a
  guest network if you would rather not show your main one.
- The tile shows three lines: "Wi-Fi QR code", the network name and "Tap for the code". Write your own words in
  **Top line**, **Middle line** or **Bottom line** to change any of them; leave one empty to keep the usual words.
  Turn off **Show the top line** (or the middle or bottom one) to hide it; the lines left move to the middle.
- The screen's fonts have no emoji: a name such as "😍 Home" shows as "Home" on the tile and the card, while the code
  holds the whole name, emoji and all.
- Turn off **Show the password** to show only the code, without the password written under it.
- It uses nothing outside the screen: no Home Assistant entity and no internet service.

## Credits

The QR code is made with Project Nayuki's [QR Code generator library](https://github.com/nayuki/QR-Code-generator)
(MIT License), in `components/wifi_qr/wqr_qrcodegen.c`.
