EE_BIN = disc-launcher-raw.elf
EE_BIN_STRIPPED = disc-launcher-stripped.elf
EE_BIN_PACKED = disc-launcher.elf
EE_OBJS = main.o cnf_lite.o $(EE_C_DIR)ps2dev9.o $(EE_C_DIR)ps2atad.o $(EE_C_DIR)ps2fs.o $(EE_C_DIR)ps2hdd.o $(EE_C_DIR)iomanx.o $(EE_C_DIR)filexio.o $(EE_C_DIR)usbd.o $(EE_C_DIR)usbmass.o

EE_C_DIR = modules/

EE_INCS := -I$(PS2SDK)/ports/include -I$(PS2DEV)/gsKit/include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I.
EE_GPVAL = -G0
EE_CFLAGS = -O2 -D_EE -Os -mgpopt -mno-gpopt $(EE_GPVAL) -Wall $(EE_INCS) -Wno-stringop-truncation -I$(PS2DEV)/gsKit/include
EE_LDFLAGS = -L$(PS2SDK)/ee/lib -L$(PS2DEV)/gsKit/lib
EE_LIBS += -lfileXio -lpatches -lelf-loader-nocolour -L$(PS2SDK)/ports/lib -L$(PS2DEV)/gsKit/lib/ -ldmakit -lgskit -ldebug
EE_NEWLIB_NANO ?= 1
EE_COMPACT_EXECUTABLE ?= 1

BIN2C = $(PS2SDK)/bin/bin2c

all: create_dirs $(EE_BIN_PACKED)

create_dirs:
	mkdir -p $(EE_C_DIR)

$(EE_BIN_STRIPPED): $(EE_BIN)
	echo "Stripping..."
	$(EE_STRIP) -o $@ $<

$(EE_BIN_PACKED): $(EE_BIN_STRIPPED)
	echo "Compressing..."
	ps2-packer $< $@

clean:
	rm -rf $(EE_BIN) $(EE_BIN_STRIPPED) $(EE_BIN_PACKED) $(EE_OBJS) $(EE_C_DIR)

$(EE_C_DIR)ps2dev9.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	$(BIN2C) $< $@ ps2dev9_irx

$(EE_C_DIR)ps2atad.c: $(PS2SDK)/iop/irx/ps2atad.irx
	$(BIN2C) $< $@ ps2atad_irx

$(EE_C_DIR)ps2fs.c: $(PS2SDK)/iop/irx/ps2fs.irx
	$(BIN2C) $< $@ ps2fs_irx

$(EE_C_DIR)ps2hdd.c: $(PS2SDK)/iop/irx/ps2hdd-osd.irx
	$(BIN2C) $< $@ ps2hdd_irx

$(EE_C_DIR)iomanx.c: $(PS2SDK)/iop/irx/iomanX.irx
	$(BIN2C) $< $@ iomanx_irx

$(EE_C_DIR)usbd.c: $(PS2SDK)/iop/irx/usbd.irx
	$(BIN2C) $< $@ usbd_irx

$(EE_C_DIR)usbmass.c: $(PS2SDK)/iop/irx/usbmass_bd.irx
	$(BIN2C) $< $@ usbmass_bd_irx

$(EE_C_DIR)filexio.c: $(PS2SDK)/iop/irx/fileXio.irx
	$(BIN2C) $< $@ filexio_irx

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.eeglobal