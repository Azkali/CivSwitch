# CivSwitch

XSS toolchain for the Switch 1 edition of Civilization 7.  
Only the **1.0.0** version of the game in physical cartridge is supported.  

The Pico-based workflow has been tested at Switch 1 firmware 22.5.0 and
Switch 2 firmware 20.4.0 and 23.0.1 with a Pico 2 (RP2350). Pico 1 (RP2040)
is untested. T3Switch has been tested as a USB keyboard against a normal PC;
its console workflow has not been tested.

Components:

- **T3Switch** — a Linux FunctionFS USB keyboard for the XpressReal T3. It
  types a user-supplied file using the Pico macro's key map, timings and batch
  structure. It does not build or interpret the file. Gamepad A presses are
  logged as placeholders, so it is not yet a verified replacement for the
  Pico controller. See [T3Switch/README.md](T3Switch/README.md).
- **PicoSwitch** — clean, compilable Pico firmware (RP2040 + RP2350) that types a chat-XSS payload into the Switch (USB HID macro).

  Default payload = the Remote JS Loader bootstrap (exploit core + TCP loader).

  Ships with `compress.js` (chat transport: terser -> deflateRaw -> base-25 chunked `img/onerror` lines with censor armor).
- **installer-creator** — given any JavaScript payload, produces the boot-persistent installer (`XSTRIG.Civ7Save` trigger written to savedata) to send through the Remote JS Loader.

## Layout

`core.js` at the root is the shared payload used by PicoSwitch and the installer
creator. `send_js.py` sends a payload to a running loader. T3Switch reads an
existing file and does not generate a payload.

## T3 keyboard setup

Connect the T3's **OTG port2** to a normal PC with a data cable, then build
and run the keyboard test on the T3:

```sh
cd T3Switch
make
sudo tools/check-t3.sh
sudo scripts/hid-gadget-up.sh
printf 'hello world\n' > /tmp/hello.txt
sudo ./hidkbd_ffs --payload /tmp/hello.txt --mode text --cps 20 --start-delay-ms 5000
sudo scripts/hid-gadget-down.sh
```

Focus a text editor on the PC during the five-second delay. It should receive
`hello world` followed by a newline. This PC smoke test and a ten-line mixed
case and punctuation test passed on the T3. The default Pico-style mode sends
Esc and two Enter presses per nonempty line; `--mode text` is used above for
plain text. Actual gamepad A presses are not implemented, so the default mode
only logs where they would occur. USB packet timing and disconnect during
typing remain unverified. See the [T3Switch guide](T3Switch/README.md) for
prerequisites, options, timing and teardown behavior.

## Pico workflow

[Video Tutorial](https://www.reddit.com/r/switch2hacks/comments/1xll4wf/civswitch_hack_start_to_finish/) from switch2hacks reddit

1. **Build + flash** the loader UF2 (see PicoSwitch/README.md).

   On the Switch, go to "System" menu and change system language to English(US).  
   Go to "Controllers & Accessories" menu and change USB Keyboard to English(US).  
   
   These are required to type "English" letters (code) using USB keyboard.  
   (Higher fw might not need to change system language as they support typing english without language change)  
   
   NOTE for JP-only Switch2 : 
   1. If you have another Switch, you can connect the two consoles via local wireless multiplayer and plug the Pico on the other device, allowing it to send the payload over the network.  
   2. If you're on a higher fw version (I'm not sure exact version), you can simply try plugging the Pico and see if it can type English text without changing the system language.  
   
   Connect to Internet using [90DNS](https://gitlab.com/a/90dns) to block Nintendo servers.  
   
   Then launch game and navigate main menu -> MULTIPLAYER -> WIRELESS -> CREATE GAME -> HOST LOBBY -> CHAT then plug the Pico in (in docked mode, focus the chat box first to receive Pico input).  

   The Pico types the chat XSS (~87 lines, ~20 min); the Remote JS Loader comes up on the console (TCP :9026) for that session.  
2. **Install the persistent boot trigger** from the PC: `python installer-creator/install.py <console-ip>` — uploads the save template, splices the compressed payload into it on-console, wipes `save:/saves/Single/`, and writes the trigger.
3. From then on, every boot: main menu -> Load Game -> the payload runs from savedata (and if the payload is the loader, :9026 is up at boot — and survives console sleep/wake).  
4. You can revert system language after installer finished.  

## Sending payloads (`send_js.py`)

```
python send_js.py <console-ip> [port] file.js
```

Sends a JS file as-is to the Remote JS Loader (default port 9026), then prints the log output the console streams back (read until close or timeout).  

## Build (Pico firmware)

On Ubuntu 24.04:

```
sudo apt install -y git build-essential cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib python3 nodejs npm
git clone --branch 2.3.1 --depth 1 --recurse-submodules --shallow-submodules https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
export PICO_SDK_PATH=$HOME/pico-sdk
cd PicoSwitch
npm install                    # terser for compress.js
node compress.js               # reads ../core.js -> payload.txt
cmake --preset pico2 && cmake --build --preset pico2    # RP2350 -> build-pico2/src/civ7-pico2.uf2
cmake --preset pico1 && cmake --build --preset pico1    # RP2040 -> build-pico1/src/civ7-pico1.uf2
```

## Credits

- The JS exploit core in `core.js` referenced [inject.js](https://github.com/NetflixNHack/Netflix-N-Hack/blob/main/inject.js) from [Netflix-N-Hack](https://github.com/NetflixNHack/Netflix-N-Hack) and its fork by [ufm42](https://github.com/ufm42/Netflix-N-Hack/blob/main/inject.js) 
- The PicoSwitch firmware is based on [Pico-Switch-Controller](https://github.com/Element2424/Pico-Switch-Controller) by [Element2424](https://github.com/Element2424)

