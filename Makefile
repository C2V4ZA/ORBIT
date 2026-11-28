ROOT := $(CURDIR)
TOOLS := $(abspath ../build-tools)
PS3DEV ?= $(TOOLS)/ps3dev
PSL1GHT ?= $(PS3DEV)
export PS3DEV PSL1GHT
export PATH := $(PS3DEV)/bin:$(PS3DEV)/ppu/bin:$(PS3DEV)/spu/bin:$(PATH)
MBED := $(TOOLS)/mbedtls-2.28.10
PORT := $(PS3DEV)/portlibs/ppu
CORE := src/games.c src/covers.c src/covers_net.c src/image.c src/gif.c src/anim.c src/jpeg.c src/avatar.c src/upload.c src/emoji.c src/sfx.c src/suggest.c src/update.c src/directory.c src/sealed.c src/presence.c src/goldhen_plugins.c src/watch.c src/i18n.c src/platform.c src/store.c src/network.c src/nat.c src/session.c src/relay.c src/bore.c src/voice_core.c src/transition.c vendor/cJSON.c vendor/monocypher.c
INCS := -Iinclude -Ivendor
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-misleading-indentation -DCJSON_NESTING_LIMIT=16 $(INCS)
HOSTLIBS := $(MBED)/library/libmbedtls.a $(MBED)/library/libmbedx509.a $(MBED)/library/libmbedcrypto.a -lpthread -lm -lz
PPUCC := $(PS3DEV)/ppu/bin/ppu-gcc
PPUFLAGS := $(CFLAGS) -D__PPU__ -mcpu=cell -mhard-float -ffunction-sections -fdata-sections -I$(PS3DEV)/ppu/include -I$(PORT)/include -I$(PORT)/include/freetype2
PPULIBS := -L$(PORT)/lib -L$(PS3DEV)/ppu/lib -lfreetype -lpng -lz -lmbedtls -lmbedx509 -lmbedcrypto -laudio -lrsx -lgcm_sys -lsysutil -lio -lnet -lnetctl -lsysmodule -lpthread -lrt -llv2 -lm
.PHONY: host test ps3 clean
host: build/orbit-test-host
build:
	mkdir -p build
build/orbit-test-host: $(CORE) tools/host.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
build/test-core: $(CORE) tests/test_core.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
build/test-network: $(CORE) tests/test_network.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
build/test-app: $(CORE) src/app.c src/voice_host.c tests/test_app.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
build/test-transition: src/transition.c tests/test_transition.c | build
	$(CC) $(CFLAGS) $^ -lpthread -lm -o $@
build/test-sessions: $(CORE) tests/test_sessions.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
test: build/test-core build/test-network build/test-app build/test-sessions build/test-social build/test-friends build/test-voice build/test-notify build/test-games build/test-emoji build/test-i18n build/test-suggest build/test-update build/test-image build/test-upload build/test-directory build/test-sealed build/test-gif build/test-pictures build/test-covers build/test-transition build/test-shield build/test-goldhen build/orbit-lang
	./build/test-core
	./build/test-network
	./build/test-sessions
	./build/test-app
	./build/test-social
	./build/test-friends
	./build/test-voice
	./build/test-notify
	./build/test-games
	./build/test-emoji
	./build/test-i18n
	./build/test-suggest
	./build/test-update
	./build/test-image
	./build/test-upload
	./build/test-directory
	./build/test-sealed
	./build/test-gif
	./build/test-pictures
	./build/test-covers
	./build/test-transition
	./build/test-shield
	./build/test-goldhen
	./build/orbit-lang check lang/*.lang
	python3 tools/font_coverage.py assets/OrbitSans.ttf lang/*.lang
preview: build/preview
	./build/preview
build/preview: src/ui.c src/ui_next.c src/i18n.c src/games.c src/covers.c src/image.c src/gif.c src/anim.c src/jpeg.c src/avatar.c src/emoji.c src/suggest.c src/transition.c tools/preview.c $(wildcard include/*.h) | build
	$(CC) $(CFLAGS) -I$(PORT)/include/freetype2 -I$(PORT)/include src/ui.c src/i18n.c src/games.c src/covers.c src/image.c src/gif.c src/anim.c src/jpeg.c src/avatar.c vendor/monocypher.c vendor/qrcodegen.c src/emoji.c src/suggest.c src/transition.c tools/preview.c -lpthread /usr/lib/x86_64-linux-gnu/libfreetype.so.6 -lz -lm -o $@
ps3: build/orbit.elf
PPUSRC := $(CORE) vendor/qrcodegen.c src/app.c src/ui.c src/main_ps3.c src/rsxutil.c src/voice_ps3.c src/dualsense.c
PPUOBJ := $(patsubst %.c,build/ppu/%.o,$(PPUSRC))
build/ppu/%.o: %.c
	mkdir -p $(dir $@)
	$(PPUCC) $(PPUFLAGS) -MMD -MP -c $< -o $@
build/orbit.elf: $(PPUOBJ) build/mic-import.o | build
	$(PPUCC) $^ -Wl,--gc-sections $(PPULIBS) -lusb -o $@
-include $(PPUOBJ:.o=.d)
clean:
	rm -f build/orbit-test-host build/test-core build/orbit.elf

# Isolated audio diagnostic: no additional imports in the chat executable.
probe: build/audio-probe.elf
build/mic-import.o: vendor/mic-import/exports.S vendor/mic-import/libexport.c vendor/mic-import/exports.h vendor/mic-import/config.h | build
	$(PPUCC) -S -m32 -Ivendor/mic-import vendor/mic-import/libexport.c -o build/mic-libexport.S
	$(PPUCC) -c build/mic-libexport.S -o build/mic-libexport.o
	$(PPUCC) -c -mregnames -mcpu=cell -D__ASSEMBLY__ -Ivendor/mic-import -I$(PS3DEV)/ppu/include vendor/mic-import/exports.S -o build/mic-exports.o
	$(PS3DEV)/ppu/bin/ppu-ld -r build/mic-exports.o build/mic-libexport.o -o $@
build/audio-probe.elf: build/ppu/src/audio_probe.o build/ppu/src/ui.o build/ppu/src/rsxutil.o build/mic-import.o
	$(PPUCC) $^ -Wl,--gc-sections $(PPULIBS) -lusb -o $@

# DualSense-over-USB test (separate package, does not touch Orbit itself)
controller: build/ds-probe.elf
build/ds-probe.elf: build/ppu/src/ds_probe.o build/ppu/src/ui.o build/ppu/src/rsxutil.o | build
	$(PPUCC) $^ -Wl,--gc-sections $(PPULIBS) -lusb -o $@

# Orbit 1.0: servers, roles, friends, private chats, migration from 0.9.5
build/test-social: $(CORE) tests/test_social.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
build/test-friends: $(CORE) src/notify_core.c src/app.c src/voice_host.c tests/test_friends.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
build/test-voice: $(CORE) src/voice_host.c tests/test_voice.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm

# Plugin-Test: loads the background-plugin test modules and shows the PRX loader result codes
plugintest: build/prx-tester.elf
build/prx-tester.elf: build/ppu/src/prx_tester.o build/ppu/src/ui.o build/ppu/src/i18n.o build/ppu/src/rsxutil.o | build
	$(PPUCC) $^ -Wl,--gc-sections $(PPULIBS) -o $@

# Orbit 1.1: background notifications (host watch connections + plugin logic), end to end
build/test-notify: $(CORE) src/notify_core.c src/presence_worker.c tests/test_notify.c | build
	$(CC) $(CFLAGS) -DWATCH_PING_MS=2000 -DWATCH_IDLE_MS=6500 -DPRESENCE_REFRESH_MS=1500 -DPRESENCE_POLL_MS=300 -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm

# ORBIT NEXT: game detection and the strict image decoder
build/test-games: $(CORE) tests/test_games.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm

# ORBIT NEXT: emoji pictures and UI sounds
build/test-emoji: src/emoji.c src/sfx.c tests/test_emoji.c | build
	$(CC) $(CFLAGS) $^ -o $@ -lm
build/test-i18n: src/i18n.c tests/test_i18n.c $(wildcard include/i18n*.h) | build
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $@
build/test-suggest: src/i18n.c src/suggest.c tests/test_suggest.c | build
	$(CC) $(CFLAGS) $^ -o $@

# ORBIT NEXT: signed updates
build/test-update: $(CORE) tests/test_update.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
# 1.2: public community directory service (local use / tests only, see include/directory.h)
build/orbit-dird: tools/orbit_dird.c src/directory.c src/platform.c src/i18n.c vendor/cJSON.c vendor/monocypher.c | build
	$(CC) $(CFLAGS) $^ -o $@ -lm
build/test-directory: $(CORE) src/notify_core.c src/app.c src/voice_host.c tests/test_directory.c | build build/orbit-dird
	$(CC) $(CFLAGS) -I$(MBED)/include $(filter %.c,$^) $(HOSTLIBS) -o $@ -lm
build/test-shield: $(CORE) src/app.c src/voice_host.c tests/test_shield.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
build/test-goldhen: src/goldhen_plugins.c tests/test_goldhen.c | build
	$(CC) $(CFLAGS) $^ -o $@
build/test-sealed: src/sealed.c vendor/monocypher.c tests/test_sealed.c | build
	$(CC) $(CFLAGS) $^ -o $@
build/test-gif: src/gif.c src/anim.c src/image.c src/jpeg.c src/avatar.c vendor/monocypher.c tests/test_gif.c | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $^ -o $@ -lz -lm
build/test-pictures: $(CORE) tests/test_pictures.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
# game covers by Title ID (source-agnostic, opt-in; ASan/UBSan because it parses downloads)
build/test-covers: $(CORE) tests/test_covers.c | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
build/orbit-lang: src/i18n.c tools/orbit_lang.c $(wildcard include/i18n*.h) | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
build/orbit-sign: tools/orbit_sign.c vendor/cJSON.c vendor/monocypher.c | build
	$(CC) $(CFLAGS) $^ -o $@ -lm
build/test-image: src/image.c src/gif.c src/anim.c src/jpeg.c src/avatar.c src/platform.c src/i18n.c vendor/monocypher.c tests/test_image.c | build
	$(CC) $(CFLAGS) $^ -o $@ -lz -lm
build/test-upload: $(CORE) tests/test_upload.c | build
	$(CC) $(CFLAGS) -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
# Fuzzing with AddressSanitizer + UBSan (docs/next/THREAT-MODEL.md). FUZZ_ITER inputs per target.
FUZZ_ITER ?= 20000
build/fuzz: $(CORE) tests/fuzz/fuzz.c | build
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I$(MBED)/include $^ $(HOSTLIBS) -o $@ -lm
fuzz: build/fuzz
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 ./build/fuzz all $(FUZZ_ITER)
.PHONY: fuzz
