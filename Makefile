# Plugins Mgr Payload and Installer
# PS4 Payload SDK:
# https://github.com/ps4-payload-dev/sdk

ifdef PS4_PAYLOAD_SDK
include $(PS4_PAYLOAD_SDK)/toolchain/orbis.mk
else
$(error PS4_PAYLOAD_SDK is undefined)
endif

PAYLOAD       := ps4-plugins-mgr
ELF           := $(PAYLOAD).elf

INSTALLER     := ps4-plugins-mgr-installer
INSTALLER_ELF := $(INSTALLER).elf

INSTALLER_DIR  := installer
GENERATED_DIR  := $(INSTALLER_DIR)/generated
PAYLOAD_DATA_H := $(GENERATED_DIR)/payload_data.h

MAIN_SRCS := \
	source/main.c \
	source/log.c \
	source/ini.c \
	source/fs.c \
	source/server.c

MAIN_HDRS := \
	source/html.h \
	source/server.h \
	source/ini.h \
	source/fs.h \
	source/log.h

INSTALLER_SRCS := installer/main.c
INSTALLER_HDRS := $(PAYLOAD_DATA_H)

CFLAGS  := -Wall -Wextra -O2 -g
LDFLAGS := -lSceNet

.PHONY: all clean installer-header

all: $(ELF) $(INSTALLER_ELF)

# Build the main Plugins Mgr payload first.
$(ELF): $(MAIN_SRCS) $(MAIN_HDRS)
	$(CC) $(CFLAGS) -o $@ $(MAIN_SRCS) $(LDFLAGS)

# Convert the freshly-built ELF into a generated C header.
$(PAYLOAD_DATA_H): $(ELF)
	mkdir -p $(GENERATED_DIR)
	@echo "/* Auto-generated from $(ELF). Do not edit manually. */" > $@
	@echo "#ifndef PLGMGR_EMBEDDED_PAYLOAD_H" >> $@
	@echo "#define PLGMGR_EMBEDDED_PAYLOAD_H" >> $@
	@echo "" >> $@
	@echo "#include <stddef.h>" >> $@
	@echo "#include <stdint.h>" >> $@
	@echo "" >> $@
	@echo "static const unsigned char EMBEDDED_PAYLOAD[] = {" >> $@
	@od -An -v -tx1 $(ELF) | awk '{ for (i = 1; i <= NF; i++) printf "0x%s, ", $$i; printf "\n" }' >> $@
	@echo "};" >> $@
	@echo "" >> $@
	@printf "static const size_t EMBEDDED_PAYLOAD_SIZE = %s;\n" "$$(wc -c < $(ELF) | tr -d '[:space:]')" >> $@
	@echo "" >> $@
	@echo "#endif" >> $@

installer-header: $(PAYLOAD_DATA_H)

# Build the installer only after the main ELF and generated header exist.
$(INSTALLER_ELF): $(INSTALLER_SRCS) $(INSTALLER_HDRS)
	$(CC) $(CFLAGS) -I$(GENERATED_DIR) -o $@ $(INSTALLER_SRCS)

clean:
	rm -f $(ELF) $(INSTALLER_ELF)
	rm -rf $(GENERATED_DIR)
