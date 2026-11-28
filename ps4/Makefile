# ORBIT for PS4 (jailbroken, GoldHEN) - built with the open-source OpenOrbis PS4 Toolchain.
# The portable core, the UI renderer and the front end are compiled from the repository root (shared,
# not copied); only the platform layer lives here (src/, shim/). Run in WSL: `make` / `make pkg`.
PS3 := ..
OO_PS4_TOOLCHAIN ?= $(HOME)/ps4dev/OpenOrbis/PS4Toolchain
PORT := $(HOME)/ps4dev/portlibs
export OO_PS4_TOOLCHAIN
TC := $(OO_PS4_TOOLCHAIN)
CC := clang-18
LD := ld.lld-18
VERSION := 1.3.0
TITLE := ORBIT
TITLE_ID := ORBT00001
CONTENT_ID := IV0000-ORBT00001_00-ORBITCHATPS4V001
BUILD := build

CORE := games.c covers.c covers_net.c image.c gif.c anim.c jpeg.c avatar.c upload.c emoji.c sfx.c \
        suggest.c update.c directory.c sealed.c presence.c goldhen_plugins.c watch.c i18n.c platform.c store.c network.c \
        nat.c session.c relay.c bore.c voice_core.c transition.c app.c ui.c main_ps3.c
SRC := $(addprefix $(PS3)/src/,$(CORE)) $(PS3)/vendor/cJSON.c $(PS3)/vendor/monocypher.c \
       $(PS3)/vendor/qrcodegen.c src/ps4_platform.c src/ps4_video.c src/voice_ps4.c src/dns_fallback.c
OBJ := $(patsubst %.c,$(BUILD)/obj/%.o,$(notdir $(SRC)))

# paths on the PS4: package files are read-only under /app0, user data lives in /data/orbit
DEFS := -DORBIT_PS4 -DORBIT_NO_UPDATES -DDRAW_NATIVE_1080=1 -DORBIT_TRANSITIONS=1 -DORBIT_BLUR=1 -DORBIT_MOTION=1 -DORBIT_RENDER_THREADS=6 -D'APPDIR="/data/orbit"' \
        -D'ASSETDIR="/app0/assets"' -D'GAMES_ROOT="/user/appmeta"' -D'GAMES_DISC="/nonexistent"' \
        -D'USB_ROOT_FMT="/mnt/usb%d%s"' -D'PACKAGES_DIR="/data/pkg"' -DCJSON_NESTING_LIMIT=16
CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -isysroot $(TC) -isystem $(TC)/include \
          -std=gnu11 -O2 -Wall -Wno-misleading-indentation -Wno-unused-parameter \
          -ffunction-sections -fdata-sections $(DEFS) -include shim/ps4_fix.h -Ishim -Isrc \
          -I$(PS3)/include -I$(PS3)/vendor -I$(PORT)/include -I$(PORT)/include/freetype2
LIBS := -L$(TC)/lib -L$(PORT)/lib -lfreetype -lz -lmbedtls -lmbedx509 -lmbedcrypto -lc -lkernel \
        -lSceVideoOut -lScePad -lSceUserService -lSceSystemService -lSceNet \
        -lSceAudioOut -lSceSysmodule
LDFLAGS := -m elf_x86_64 -pie --script $(TC)/link.x --eh-frame-hdr --gc-sections --wrap=pthread_create

vpath %.c $(PS3)/src $(PS3)/vendor src

.PHONY: all pkg clean test fuzz plugin
all: $(BUILD)/eboot.bin
# PC tests of the PS4-only helpers (host gcc, no console needed)
test:
	mkdir -p build
	gcc -std=gnu11 -O2 -Wall -Wextra -Isrc src/dns_fallback.c tests/test_dns.c -lpthread -o build/test-dns
	./build/test-dns
	gcc -std=gnu11 -O2 -Wall -DORBIT_PS4 -I$(PS3)/include $(PS3)/src/i18n.c tests/test_texts.c -lpthread -o build/test-texts
	./build/test-texts
# fuzzing of the DNS reply parser with AddressSanitizer + UBSan (host gcc)
fuzz:
	mkdir -p build
	gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc src/dns_fallback.c tests/fuzz_dns.c -o build/fuzz-dns
	./build/fuzz-dns 500000

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/orbit.elf: $(OBJ)
	$(LD) $(OBJ) -o $@ $(LDFLAGS) $(LIBS) $(TC)/lib/crt1.o

$(BUILD)/eboot.bin: $(BUILD)/orbit.elf
	cd $(BUILD) && $(TC)/bin/linux/create-fself -in=orbit.elf -out=orbit.oelf --eboot eboot.bin --paid 0x3800000000000011

# 1.3: background notifications in games - GoldHEN plugin (plugin/notify_plugin_ps4.c + the shared
# notify core). Only libkernel and libSceLibcInternal: both are loaded in every PS4 process.
PLUGIN_SRC := plugin/notify_plugin_ps4.c $(PS3)/src/notify_core.c $(PS3)/src/watch.c $(PS3)/vendor/monocypher.c
PLUGIN_CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -isysroot $(TC) -isystem $(TC)/include \
          -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -fno-stack-protector \
          -ffunction-sections -fdata-sections -DORBIT_PS4 -I$(PS3)/include -I$(PS3)/vendor
plugin: $(BUILD)/orbit_notify.prx
$(BUILD)/orbit_notify.prx: $(PLUGIN_SRC) $(PS3)/include/notify_core.h $(PS3)/include/watch.h tools/verify_plugin.py
	mkdir -p $(BUILD)/plugin
	for f in $(PLUGIN_SRC); do $(CC) $(PLUGIN_CFLAGS) -c $$f -o $(BUILD)/plugin/$$(basename $${f%.c}).o || exit 1; done
	$(LD) $(BUILD)/plugin/*.o $(TC)/lib/crtlib.o -o $(BUILD)/plugin/orbit_notify.elf -m elf_x86_64 -pie \
		--script $(TC)/link.x -e _init --eh-frame-hdr --export-dynamic --gc-sections -L$(TC)/lib -lkernel -lSceLibcInternal
	python3 tools/verify_plugin.py $(BUILD)/plugin/orbit_notify.elf
	cd $(BUILD)/plugin && $(TC)/bin/linux/create-fself -in=orbit_notify.elf -out=orbit_notify.oelf \
		--lib=../orbit_notify.prx --paid 0x3800000000000011 > /dev/null

pkg: $(BUILD)/eboot.bin $(BUILD)/orbit_notify.prx
	VERSION=$(VERSION) TITLE='$(TITLE)' TITLE_ID=$(TITLE_ID) CONTENT_ID=$(CONTENT_ID) bash tools/package.sh

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
